#include "lvgl_port.h"

#include <Arduino.h>
#include <lvgl.h>

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
static lv_color_t buf[2][ screenWidth * 100 ];

/* Display flushing */
static void my_disp_flush(lv_disp_drv_t *disp, const lv_area_t *area, lv_color_t *color_p)
{
    if (tft.getStartCount() == 0) {
        tft.startWrite();
    }

    tft.pushImageDMA(area->x1,
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
    tft.setBrightness(240);

    // LVGL
    lv_init();
    lv_disp_draw_buf_init(&draw_buf, buf[0], buf[1], screenWidth * 100);

    static lv_disp_drv_t disp_drv;
    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res  = screenWidth;
    disp_drv.ver_res  = screenHeight;
    disp_drv.flush_cb = my_disp_flush;
    disp_drv.draw_buf = &draw_buf;
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
        nullptr,
        1
    );
}

void lvgl_port_poll_ui(void)
{
    // 把业务线程发来的“更新请求”应用到 LVGL 对象（只在 LVGL 线程内执行）
    ui_bridge_apply_pending();
}
