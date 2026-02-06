#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// 初始化：屏幕、触摸、LVGL、UI（ui_init）以及 LVGL 刷新线程
void lvgl_port_init(void);

// 给 LVGL 线程一个“循环钩子”（在 LVGL 线程内部调用）
// 这里留接口，后续你要加更多 UI 事件/队列都可以挂在这里
void lvgl_port_poll_ui(void);

#ifdef __cplusplus
}
#endif
