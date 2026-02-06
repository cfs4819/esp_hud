#include <Arduino.h>

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

/* -------- 释放回调适配器 -------- */

static void imgf_release_adapter(int token)
{
    imgf_rx_release(imgf, token);
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
            ui_request_msg(msg, mlen, mseq);
        }

        /* ---- 图片 ---- */
        const uint8_t *png;
        size_t plen;
        uint32_t pseq;
        int token;

        if (imgf_rx_get_ready(imgf, &png, &plen, &pseq, &token)) {
            /* 零拷贝：将buffer所有权转移给UI系统 */
            ui_request_set_png(png, plen, token, imgf_release_adapter);
            /* 不再提前释放，由UI系统在使用完成后释放 */
        }

        vTaskDelay(pdMS_TO_TICKS(1));
    }
}

/* -------- Arduino entry -------- */

void setup()
{
    Serial.begin(115200);

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
        .rx_task_core     = 1,
        .read_chunk       = 8192,
        .max_receivers    = 4
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
}

void loop()
{
    delay(1000);
}
