#include "lvgl_port.h"

#include <Arduino.h>
#include <esp_heap_caps.h>
#include <lvgl.h>
#include "freertos/task.h"

#include "PanelLan.h"
#include "ui.h"
#include "squareline/ui_Home.h"

#include "ui_bridge.h"

// BOARD_SC01_PLUS, BOARD_SC02, BOARD_SC05, BOARD_KC01, BOARD_BC02, BOARD_SC07
static PanelLan tft(BOARD_SC01_PLUS);

/*Change to your screen resolution*/
static const uint16_t screenWidth  = 480;
static const uint16_t screenHeight = 320;

static lv_disp_draw_buf_t draw_buf;
static lv_color_t *buf1 = nullptr;
static lv_color_t *buf2 = nullptr;
static const uint32_t draw_buf_pixels = screenWidth * screenHeight;
static lv_color_t fallback_buf1[screenWidth * 40];
static lv_color_t fallback_buf2[screenWidth * 40];
static TaskHandle_t s_lvgl_task_handle = nullptr;
static volatile bool s_suspend_requested = false;
static volatile bool s_is_suspended = false;

/* Display flushing */
static void my_disp_flush(lv_disp_drv_t *disp, const lv_area_t *area, lv_color_t *color_p)
{
    if (tft.getStartCount() == 0) {
        tft.startWrite();
    }

    tft.pushImage(area->x1,
                  area->y1,
                  area->x2 - area->x1 + 1,
                  area->y2 - area->y1 + 1,
                  (lgfx::swap565_t*)&color_p->full);

    lv_disp_flush_ready(disp);
}

/*Read the touchpad*/
static void my_touchpad_read(lv_indev_drv_t * indev_driver, lv_indev_data_t * data)
{
    (void) indev_driver;

    uint16_t touchX, touchY;
    data->state = LV_INDEV_STATE_REL;

    if (tft.getTouch(&touchX, &touchY)) {
        data->state = LV_INDEV_STATE_PR;
        data->point.x = touchX;
        data->point.y = touchY;
    }
}

// LVGL 专用线程：唯一允许调用 lv_timer_handler 和任何 lv_* API 的上下文
static void lvgl_task(void *param)
{
    (void)param;

    const TickType_t delayTicks = pdMS_TO_TICKS(5);

    for (;;) {
        if (s_suspend_requested) {
            if (!s_is_suspended) {
                lv_timer_enable(false);
                if (tft.getStartCount() > 0) {
                    tft.endWrite();
                }
                tft.sleep();
                s_is_suspended = true;
            }

            ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

            if (!s_suspend_requested && s_is_suspended) {
                tft.wakeup();
                lv_timer_enable(true);
                s_is_suspended = false;
            }
            continue;
        }

        // 处理 UI 更新请求（你未来 CDC/router/PNG decode 都通过桥接发到这里）
        lvgl_port_poll_ui();

        // LVGL 内部定时器/动画/刷新
        lv_timer_handler();

        vTaskDelay(delayTicks);
    }
}

void lvgl_port_init(void)
{
    // 屏幕
    tft.begin();
    tft.setBrightness(255);

    // LVGL
    lv_init();

    buf1 = static_cast<lv_color_t *>(
        heap_caps_malloc(draw_buf_pixels * sizeof(lv_color_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    buf2 = static_cast<lv_color_t *>(
        heap_caps_malloc(draw_buf_pixels * sizeof(lv_color_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));

    uint32_t active_pixels = draw_buf_pixels;
    bool use_full_refresh = true;

    if (!buf1 || !buf2) {
        Serial0.println("[LVGL] PSRAM draw buffer allocation failed, fallback to internal RAM");
        if (buf1) {
            heap_caps_free(buf1);
            buf1 = nullptr;
        }
        if (buf2) {
            heap_caps_free(buf2);
            buf2 = nullptr;
        }
        buf1 = fallback_buf1;
        buf2 = fallback_buf2;
        active_pixels = screenWidth * 40;
        use_full_refresh = false;
    }

    lv_disp_draw_buf_init(&draw_buf, buf1, buf2, active_pixels);

    static lv_disp_drv_t disp_drv;
    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res  = screenWidth;
    disp_drv.ver_res  = screenHeight;
    disp_drv.flush_cb = my_disp_flush;
    disp_drv.draw_buf = &draw_buf;
    disp_drv.full_refresh = use_full_refresh ? 1 : 0;
    lv_disp_drv_register(&disp_drv);

    static lv_indev_drv_t indev_drv;
    lv_indev_drv_init(&indev_drv);
    indev_drv.type    = LV_INDEV_TYPE_POINTER;
    indev_drv.read_cb = my_touchpad_read;
    lv_indev_drv_register(&indev_drv);

    // SquareLine UI init（创建对象）
    ui_init();

    // 初始化 UI 桥接（队列/共享状态）
    ui_bridge_init();

    // 创建 LVGL 线程（建议 pin 到 core1）
    xTaskCreatePinnedToCore(
        lvgl_task,
        "lvgl",
        6144,
        nullptr,
        8,
        &s_lvgl_task_handle,
        1
    );
}

void lvgl_port_poll_ui(void)
{
    // 把业务线程发来的“更新请求”应用到 LVGL 对象（只在 LVGL 线程内执行）
    ui_bridge_apply_pending();
}

void lvgl_port_suspend(void)
{
    if (!s_lvgl_task_handle || s_is_suspended) {
        return;
    }
    s_suspend_requested = true;
    xTaskNotifyGive(s_lvgl_task_handle);
    while (!s_is_suspended) {
        vTaskDelay(pdMS_TO_TICKS(1));
    }
}

void lvgl_port_resume(void)
{
    if (!s_lvgl_task_handle || !s_is_suspended) {
        return;
    }
    s_suspend_requested = false;
    xTaskNotifyGive(s_lvgl_task_handle);
    while (s_is_suspended) {
        vTaskDelay(pdMS_TO_TICKS(1));
    }
}

bool lvgl_port_is_suspended(void)
{
    return s_is_suspended;
}
