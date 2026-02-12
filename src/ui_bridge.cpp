#include "ui_bridge.h"

#include <Arduino.h>
#include <stdlib.h>
#include <string.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

#include <lvgl.h>
#include "ui.h"
#include "squareline/ui_Home.h"
#if __has_include("esp_heap_caps.h")
#include "esp_heap_caps.h"
#endif

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

// 当前正在被 lv_img 对象引用的位图数据（TRUE_COLOR 或 TRUE_COLOR_ALPHA）
static uint8_t *s_map_img_buf = nullptr;

static void *ui_alloc(size_t n)
{
#if __has_include("esp_heap_caps.h")
    void *p = heap_caps_malloc(n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (p) return p;
    return heap_caps_malloc(n, MALLOC_CAP_8BIT);
#else
    return malloc(n);
#endif
}

static void ui_free(void *p)
{
#if __has_include("esp_heap_caps.h")
    heap_caps_free(p);
#else
    free(p);
#endif
}

static bool decode_png_to_lv_img_data(const uint8_t *png,
                                      size_t len,
                                      uint8_t **out_data,
                                      size_t *out_bytes,
                                      lv_coord_t *out_w,
                                      lv_coord_t *out_h,
                                      lv_img_cf_t *out_cf)
{
    if (!png || !len || !out_data || !out_bytes || !out_w || !out_h || !out_cf) return false;

    lv_img_dsc_t src = {};
    src.header.always_zero = 0;
    src.header.w = 0;
    src.header.h = 0;
    src.header.cf = 0;
    src.data_size = len;
    src.data = png;

    lv_img_decoder_dsc_t dec = {};
    lv_res_t res = lv_img_decoder_open(&dec, &src, lv_color_black(), 0);
    if (res != LV_RES_OK || dec.header.w <= 0 || dec.header.h <= 0 || !dec.img_data) {
        lv_img_decoder_close(&dec);
        return false;
    }

    lv_img_cf_t out_cf_local = 0;
    if (dec.header.cf == LV_IMG_CF_TRUE_COLOR_ALPHA) {
        out_cf_local = LV_IMG_CF_TRUE_COLOR_ALPHA;
    } else if (dec.header.cf == LV_IMG_CF_TRUE_COLOR) {
        out_cf_local = LV_IMG_CF_TRUE_COLOR;
    } else {
        lv_img_decoder_close(&dec);
        return false;
    }

    const size_t out_sz = (size_t)lv_img_buf_get_img_size(dec.header.w, dec.header.h, out_cf_local);
    uint8_t *buf = (uint8_t *)ui_alloc(out_sz);
    if (!buf) {
        lv_img_decoder_close(&dec);
        return false;
    }
    memcpy(buf, dec.img_data, out_sz);

    *out_data = buf;
    *out_bytes = out_sz;
    *out_w = dec.header.w;
    *out_h = dec.header.h;
    *out_cf = out_cf_local;

    lv_img_decoder_close(&dec);
    return true;
}

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

/* ---------- PNG处理（一次解码转RGB565）---------- */

static void apply_png_lvgl(const png_item_t *it)
{
    uint8_t *new_data = nullptr;
    size_t new_bytes = 0;
    lv_coord_t new_w = 0;
    lv_coord_t new_h = 0;
    lv_img_cf_t new_cf = LV_IMG_CF_TRUE_COLOR;

    const bool ok = decode_png_to_lv_img_data(it->png, it->len, &new_data, &new_bytes, &new_w, &new_h, &new_cf);

    // PNG原始buffer只在当前函数解码时使用，随后即可释放
    if (it->release_cb) {
        it->release_cb(it->token);
    }

    if (!ok) {
        Serial0.println("[UI_BRIDGE] PNG decode failed, keep previous map");
        return;
    }

    static lv_img_dsc_t img_dsc;
    img_dsc.header.always_zero = 0;
    img_dsc.header.w = new_w;
    img_dsc.header.h = new_h;
    img_dsc.header.cf = new_cf;
    img_dsc.data_size = new_bytes;
    img_dsc.data = new_data;

    lv_img_cache_invalidate_src(&img_dsc);
    lv_img_set_src(ui_Map_Bg, &img_dsc);

    if (s_map_img_buf) {
        ui_free(s_map_img_buf);
    }
    s_map_img_buf = new_data;
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
    // 注意：release_cb在LVGL线程里解码完成后立即调用
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
