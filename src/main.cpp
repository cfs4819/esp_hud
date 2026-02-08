#include <Arduino.h>
#include <esp_system.h>

#include "USB.h"
#include "USBCDC.h"
USBCDC USBSerial;

extern "C" {
#include "usb_stream_router.h"
#include "imgf_receiver.h"
#include "msgf_receiver.h"
}

#include "lvgl_port.h"
#include "ui_bridge.h"

/* -------- CDC transport -------- */

static int tp_available(void *ctx){
    return ((USBCDC*)ctx)->available();
}
static int tp_read(void *ctx, uint8_t *dst, int max){
    return ((USBCDC*)ctx)->read(dst, max);
}

/* -------- router / receivers -------- */

static usb_stream_router_t *router;
static imgf_rx_t *imgf;
static msgf_rx_t *msgf;
static volatile uint32_t g_last_usb_rx_ms = 0;
static volatile bool g_ui_suspended = false;
static volatile bool g_resume_requested = false;

/* -------- 释放回调适配器 -------- */

static void imgf_release_adapter(int token)
{
    imgf_rx_release(imgf, token);
}

static void on_usb_rx_activity(void *user, size_t bytes)
{
    (void)user;
    (void)bytes;
    g_last_usb_rx_ms = millis();
    if (g_ui_suspended) {
        g_resume_requested = true;
    }
}

static void handle_msg_command(const uint8_t *msg, size_t len, uint32_t seq)
{
    if (!msg || len < 1) {
        return;
    }

    const uint8_t cmd = msg[0];
    const uint8_t *payload = msg + 1;
    const size_t payload_len = len - 1;

    switch (cmd) {
        case 0x00:
            ui_request_msg(payload, payload_len, seq);
            break;

        case 0x01:
            Serial0.println("[MSG] CMD=0x01 restart requested");
            vTaskDelay(pdMS_TO_TICKS(20));
            esp_restart();
            break;

        default:
            // Reserved for future commands.
            Serial0.printf("[MSG] unknown CMD=0x%02X, len=%u\n", cmd, (unsigned)len);
            break;
    }
}

/* -------- 业务线程 -------- */

static void app_task(void *param)
{
    (void)param;

    for (;;) {
        /* ---- 短消息（先留空） ---- */
        uint8_t msg[1024];
        size_t mlen;
        uint32_t mseq;
        while (msgf_rx_pop(msgf, msg, sizeof(msg), &mlen, &mseq)) {
            handle_msg_command(msg, mlen, mseq);
        }

        /* ---- 图片 ---- */
        const uint8_t *png;
        size_t plen;
        uint32_t pseq;
        int token;

        if (imgf_rx_get_ready(imgf, &png, &plen, &pseq, &token)) {
                /* 零拷贝：将buffer所有权转移给UI系统 */
                Serial0.println("Got PNG");
                ui_request_set_png(png, plen, token, imgf_release_adapter);
                        /* 不再提前释放，由UI系统在使用完成后释放 */
        }

        vTaskDelay(pdMS_TO_TICKS(1));
    }
}

static void power_mgr_task(void *param)
{
    (void)param;
    const uint32_t idle_sleep_ms = 60 * 1000;
    const TickType_t poll_ticks = pdMS_TO_TICKS(200);

    for (;;) {
        const uint32_t now = millis();
        const uint32_t last = g_last_usb_rx_ms;

        if (!g_ui_suspended && (uint32_t)(now - last) >= idle_sleep_ms) {
            Serial0.println("[PM] USB idle 60s, suspend UI");
            lvgl_port_suspend();
            g_ui_suspended = true;
        }

        if (g_ui_suspended && g_resume_requested) {
            g_resume_requested = false;
            g_last_usb_rx_ms = millis();
            lvgl_port_resume();
            g_ui_suspended = false;
            Serial0.println("[PM] USB activity detected, resume UI");
        }

        vTaskDelay(poll_ticks);
    }
}

/* -------- Arduino entry -------- */

void setup()
{
    Serial0.begin(115200);

    /* UI */
    lvgl_port_init();

    /* USB CDC */
    USB.begin();
    USBSerial.begin();

    usb_sr_transport_t tp = {
        .ctx = &USBSerial,
        .available = tp_available,
        .read = tp_read
    };

    usb_sr_config_t rcfg = {
        .rx_task_priority = 18,
        .rx_task_stack    = 6144,
        .rx_task_core     = 0,
        .read_chunk       = 8192,
        .max_receivers    = 4,
        .on_rx_activity   = on_usb_rx_activity,
        .on_rx_activity_user = nullptr
    };

    router = usb_sr_create(&tp, &rcfg);

    imgf_rx_config_t icfg = {
        .max_png_bytes = 128 * 1024,
        .require_crc   = false,
        .drop_policy   = IMGF_DROP_OLD
    };
    imgf = imgf_rx_create(&icfg);
    usb_sr_receiver_t ir;
    imgf_rx_get_receiver(imgf, &ir);
    usb_sr_register(router, &ir);

    msgf_rx_config_t mcfg = {
        .max_msg_bytes = 1024,
        .queue_depth   = 8,
        .require_crc   = false
    };
    msgf = msgf_rx_create(&mcfg);
    usb_sr_receiver_t mr;
    msgf_rx_get_receiver(msgf, &mr);
    usb_sr_register(router, &mr);

    g_last_usb_rx_ms = millis();

    /* 业务线程 */
    xTaskCreatePinnedToCore(
        app_task,
        "app",
        4096,
        nullptr,
        4,
        nullptr,
        0
    );

    xTaskCreatePinnedToCore(
        power_mgr_task,
        "pm",
        4096,
        nullptr,
        3,
        nullptr,
        0
    );
    Serial0.println("Init done");
}

void loop()
{
    delay(1000);
}
