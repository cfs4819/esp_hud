#pragma once
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// 初始化：屏幕、触摸、LVGL、UI（ui_init）以及 LVGL 刷新线程
void lvgl_port_init(void);

// 给 LVGL 线程一个“循环钩子”（在 LVGL 线程内部调用）
// 这里留接口，后续你要加更多 UI 事件/队列都可以挂在这里
void lvgl_port_poll_ui(void);

// 请求 LVGL 进入暂停态（停止 lv_timer_handler 的处理、屏幕 sleep）
void lvgl_port_suspend(void);

// 恢复 LVGL 运行（屏幕 wakeup，恢复定时器）
void lvgl_port_resume(void);

// 当前是否已进入暂停态
bool lvgl_port_is_suspended(void);

#ifdef __cplusplus
}
#endif
