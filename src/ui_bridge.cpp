#include "ui_bridge.h"

#include <Arduino.h>
#include <string.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

#include <lvgl.h>
#include "ui.h"
#include "squareline/ui_Home.h"

extern "C" {

/* ---------- UI 快照结构 ---------- */

/*
Message 结构
    offset  size   类型      含义
    0       2      int16     speed_kmh
    2       2      int16     engine_speed_rpm
    4       4      int32     odo_m
    8       4      int32     trip_odo_m
    12      2      int16     outside_temp_c
    14      2      int16     inside_temp_c
    16      2      int16     battery_mv
    18      2      uint16    curr_time_min   // 0..1439
    20      2      uint16    trip_time_min
    22      ...    reserve
*/

typedef struct {
    int16_t  speed;
    int16_t  rpm;
    int32_t  odo;
    int32_t  trip_odo;
    int16_t  out_temp;
    int16_t  in_temp;
    int16_t  batt_mv;
    uint16_t cur_time_min;
    uint16_t trip_time_min;
} ui_snapshot_t;

/* ---------- PNG项结构（零拷贝方案）---------- */

typedef struct {
    const uint8_t *png;
    size_t len;
    int token;
    void (*release_cb)(int);
} png_item_t;

/* ---------- 事件类型 ---------- */

typedef enum {
    UI_EV_SNAPSHOT = 1,
    UI_EV_PNG_ITEM  // 改为存储PNG项而不是快照
} ui_ev_type_t;

typedef struct {
    ui_ev_type_t type;
    union {
        ui_snapshot_t snap;
        png_item_t png_item;
    };
} ui_event_t;

static QueueHandle_t s_ui_q = nullptr;

/* ---------- LVGL 内部工具 ---------- */

static void set_time_label(lv_obj_t *label, uint16_t min)
{
    char buf[6];
    snprintf(buf, sizeof(buf), "%02u:%02u", min / 60, min % 60);
    lv_label_set_text(label, buf);
}

/* ---------- 整体 UI 刷新 ---------- */

static void apply_snapshot_lvgl(const ui_snapshot_t *s)
{
    /* 速度 */
    char buf[8];
    snprintf(buf, sizeof(buf), "%d", (int)s->speed);
    lv_label_set_text(ui_Speed_Number_1, buf);
    lv_label_set_text(ui_Speed_Number_2, buf);

    /* 转速 */
    int rpm = s->rpm;
    if (rpm < 0) rpm = 0;
    if (rpm > 8000) rpm = 8000;
    int x = lround(rpm / 8000.0 * 180.0);
    lv_obj_set_x(ui_ImgSpeedfg, x);

    /* 时间 */
    set_time_label(ui_Label_Time3,  s->cur_time_min);
    set_time_label(ui_Label_Time_Trip, s->trip_time_min);

    /* 其余字段（ODO / 温度 / 电量）
       你后面接 UI 时直接加，不用再改协议 */
}

/* ---------- PNG处理（零拷贝版本）---------- */

static void apply_png_lvgl(const png_item_t *it)
{
    static lv_img_dsc_t img_dsc;
    img_dsc.header.always_zero = 0;
    img_dsc.header.w = 0;
    img_dsc.header.h = 0;
    img_dsc.header.cf = LV_IMG_CF_RAW;
    img_dsc.data_size = it->len;
    img_dsc.data = it->png;
    
    lv_img_set_src(ui_Map_Bg, &img_dsc);

    /* 关键：LVGL 已经拿到数据，现在可以安全释放 */
    if (it->release_cb) {
        it->release_cb(it->token);
    }
}

/* ---------- API ---------- */

void ui_bridge_init(void)
{
    if (!s_ui_q) {
        /* 深度 4 就够：UI 永远只需要最新状态 */
        s_ui_q = xQueueCreate(4, sizeof(ui_event_t));
    }
}

void ui_request_msg(const uint8_t *d, size_t len, uint32_t seq)
{
    (void)seq;
    if (!d || len < 22 || !s_ui_q) return;

    ui_event_t ev;
    ev.type = UI_EV_SNAPSHOT;

    ev.snap.speed        = (int16_t)(d[0]  | (d[1]  << 8));
    ev.snap.rpm          = (int16_t)(d[2]  | (d[3]  << 8));
    ev.snap.odo          = (int32_t)(d[4]  | (d[5]<<8) | (d[6]<<16) | (d[7]<<24));
    ev.snap.trip_odo     = (int32_t)(d[8]  | (d[9]<<8) | (d[10]<<16)| (d[11]<<24));
    ev.snap.out_temp     = (int16_t)(d[12] | (d[13]<<8));
    ev.snap.in_temp      = (int16_t)(d[14] | (d[15]<<8));
    ev.snap.batt_mv      = (int16_t)(d[16] | (d[17]<<8));
    ev.snap.cur_time_min = (uint16_t)(d[18] | (d[19]<<8));
    ev.snap.trip_time_min= (uint16_t)(d[20] | (d[21]<<8));

    /* 满了就覆盖旧的（仪表盘语义：只关心最新） */
    xQueueOverwrite(s_ui_q, &ev);
}

void ui_request_set_png(const uint8_t *png,
                       size_t len,
                       int imgf_token,
                       void (*release_cb)(int token))
{
    if (!s_ui_q || !png || len == 0) return;

    ui_event_t ev;
    ev.type = UI_EV_PNG_ITEM;
    ev.png_item.png = png;
    ev.png_item.len = len;
    ev.png_item.token = imgf_token;
    ev.png_item.release_cb = release_cb;

    // 使用发送而非覆盖，确保不会丢失重要更新
    if (xQueueSend(s_ui_q, &ev, 0) != pdTRUE) {
        // 队列满时立即释放（这种情况很少发生）
        if (release_cb) {
            release_cb(imgf_token);
        }
        Serial.println("[UI_BRIDGE] UI queue full, dropped PNG update");
    }
    // 注意：这里不调用release_cb，让LVGL线程在使用完后释放
}

void ui_bridge_apply_pending(void)
{
    if (!s_ui_q) return;

    ui_event_t ev;
    while (xQueueReceive(s_ui_q, &ev, 0) == pdTRUE) {
        if (ev.type == UI_EV_SNAPSHOT) {
            apply_snapshot_lvgl(&ev.snap);
        } else if (ev.type == UI_EV_PNG_ITEM) {
            // 零拷贝：直接使用传入的数据，渲染完成后释放
            apply_png_lvgl(&ev.png_item);
        }
    }
}

} /* extern "C" */