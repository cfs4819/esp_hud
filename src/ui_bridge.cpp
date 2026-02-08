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
    22      2      uint16    fuel_left_dl    // 油箱余量(0.1L)
    24      2      uint16    fuel_total_dl   // 油箱总量(0.1L)
    26      ...    reserve
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
    uint16_t fuel_left_dl;
    uint16_t fuel_total_dl;
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
    UI_EV_PNG_ITEM = 2
} ui_ev_type_t;

typedef struct {
    ui_ev_type_t type;
    union {
        ui_snapshot_t snap;
        png_item_t png_item;
    };
} ui_event_t;

// 分别为MSG和IMG创建独立队列
static QueueHandle_t s_msg_q = nullptr;  // MSG队列 - 用于快速的小数据
static QueueHandle_t s_img_q = nullptr;  // IMG队列 - 用于慢速的大数据

// 当前正在被lv_img对象引用的PNG buffer信息
static bool s_active_png_valid = false;
static int s_active_png_token = -1;
static void (*s_active_png_release_cb)(int) = nullptr;

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
    char buf[16];
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

    /* 油量：0.1L -> 整数L，格式 left/total */
    int fuel_left_l = (int)(s->fuel_left_dl / 10U);
    int fuel_total_l = (int)(s->fuel_total_dl / 10U);
    snprintf(buf, sizeof(buf), "%d/%d", fuel_left_l, fuel_total_l);
    lv_label_set_text(ui_Label_Gas_Number, buf);

    /* ODO：米 -> 公里，1位小数 */
    char big_buf[24];
    float odo_km = (float)s->odo / 1000.0f;
    snprintf(big_buf, sizeof(big_buf), "%.1f", (double)odo_km);
    lv_label_set_text(ui_Label_ODO_Number1, big_buf);

    /* Trip ODO：米 -> 公里，1位小数 */
    float trip_odo_km = (float)s->trip_odo / 1000.0f;
    snprintf(big_buf, sizeof(big_buf), "%.1f", (double)trip_odo_km);
    lv_label_set_text(ui_Label_Trip_Odo, big_buf);

    /* 室外温度：0.1°C -> ±xx.x */
    float out_temp_c = (float)s->out_temp / 10.0f;
    snprintf(big_buf, sizeof(big_buf), "%+.1f", (double)out_temp_c);
    lv_label_set_text(ui_Label_Temp2, big_buf);

    /* 电池电压：mV -> V，1位小数 */
    float batt_v = (float)s->batt_mv / 1000.0f;
    snprintf(big_buf, sizeof(big_buf), "%.1f", (double)batt_v);
    lv_label_set_text(ui_Label_Battery_Number1, big_buf);
}

/* ---------- PNG处理（零拷贝版本）---------- */

static void apply_png_lvgl(const png_item_t *it)
{
    static lv_img_dsc_t img_dsc;
    img_dsc.header.always_zero = 0;
    img_dsc.header.w = 0;
    img_dsc.header.h = 0;
    img_dsc.header.cf = LV_IMG_CF_RAW_ALPHA;
    img_dsc.data_size = it->len;
    img_dsc.data = it->png;

    // 同一img_dsc地址承载不同PNG数据时，先失效缓存，避免读到旧解码结果
    lv_img_cache_invalidate_src(&img_dsc);
    lv_img_set_src(ui_Map_Bg, &img_dsc);
    
    // 新图切换成功后，释放上一张仍被对象持有的buffer
    if (s_active_png_valid && s_active_png_release_cb) {
        s_active_png_release_cb(s_active_png_token);
    }

    // 记录当前图对应的buffer，直到下一次替换时再释放
    if (it->release_cb) {
        s_active_png_valid = true;
        s_active_png_token = it->token;
        s_active_png_release_cb = it->release_cb;
    } else {
        s_active_png_valid = false;
        s_active_png_token = -1;
        s_active_png_release_cb = nullptr;
    }
}

/* ---------- API ---------- */

void ui_bridge_init(void)
{
    if (!s_msg_q) {
        /* MSG队列：只保留最新快照，降低UI线程负担 */
        s_msg_q = xQueueCreate(1, sizeof(ui_event_t));
    }
    
    if (!s_img_q) {
        /* IMG队列：深度2，适合低频大数据 */
        s_img_q = xQueueCreate(2, sizeof(ui_event_t));
    }
}

void ui_request_msg(const uint8_t *d, size_t len, uint32_t seq)
{
    (void)seq;
    if (!d || len < 22 || !s_msg_q) return;

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
    ev.snap.fuel_left_dl = 0;
    ev.snap.fuel_total_dl= 0;
    if (len >= 26) {
        ev.snap.fuel_left_dl = (uint16_t)(d[22] | (d[23] << 8));
        ev.snap.fuel_total_dl= (uint16_t)(d[24] | (d[25] << 8));
    }

    // 仪表语义：只关心最新状态，覆盖旧快照
    xQueueOverwrite(s_msg_q, &ev);
}

void ui_request_set_png(const uint8_t *png,
                       size_t len,
                       int imgf_token,
                       void (*release_cb)(int token))
{
    if (!s_img_q || !png || len == 0) return;

    ui_event_t ev;
    ev.type = UI_EV_PNG_ITEM;
    ev.png_item.png = png;
    ev.png_item.len = len;
    ev.png_item.token = imgf_token;
    ev.png_item.release_cb = release_cb;
    
    // IMG队列使用发送语义：避免数据丢失
    if (xQueueSend(s_img_q, &ev, 0) != pdTRUE) {
        // 队列满时丢弃最旧帧并释放其token，优先保留最新帧
        ui_event_t dropped;
        if (xQueueReceive(s_img_q, &dropped, 0) == pdTRUE &&
            dropped.type == UI_EV_PNG_ITEM &&
            dropped.png_item.release_cb) {
            dropped.png_item.release_cb(dropped.png_item.token);
        }

        if (xQueueSend(s_img_q, &ev, 0) != pdTRUE) {
            if (release_cb) {
                release_cb(imgf_token);
            }
            Serial0.println("[UI_BRIDGE] IMG queue full, dropped newest PNG update");
        } else {
            Serial0.println("[UI_BRIDGE] IMG queue full, replaced oldest PNG update");
        }
    }
    else {
        Serial0.println("[UI_BRIDGE] PNG queued successfully");
    }
    // 注意：这里不调用release_cb，让LVGL线程在使用完后释放
}

void ui_bridge_apply_pending(void)
{
    if (!s_msg_q && !s_img_q) return;

    ui_event_t ev;

    // 优先处理MSG数据（高频小数据）
    if (s_msg_q) {
        while (xQueueReceive(s_msg_q, &ev, 0) == pdTRUE) {
            if (ev.type == UI_EV_SNAPSHOT) {
                apply_snapshot_lvgl(&ev.snap);
            }
        }
    }

    // 然后处理IMG数据（低频大数据）
    if (s_img_q) {
        // 只处理最新一张，避免解码过期帧导致延迟累积
        bool has_latest = false;
        png_item_t latest = {};

        while (xQueueReceive(s_img_q, &ev, 0) == pdTRUE) {
            if (ev.type == UI_EV_PNG_ITEM) {
                if (has_latest && latest.release_cb) {
                    latest.release_cb(latest.token);
                }
                latest = ev.png_item;
                has_latest = true;
            }
        }

        if (has_latest) {
            Serial0.println("[UI_BRIDGE] applying latest image update");
            apply_png_lvgl(&latest);
            Serial0.println("[UI_BRIDGE] latest image update applied");
        }
    }
}

} /* extern "C" */
