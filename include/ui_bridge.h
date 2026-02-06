#pragma once
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 初始化桥接 */
void ui_bridge_init(void);

/* 来自 MSGF：整包状态快照 */
void ui_request_msg(const uint8_t *data, size_t len, uint32_t seq);

/* 来自 IMGF：带token和释放回调的PNG设置 */
void ui_request_set_png(const uint8_t *png,
                       size_t len,
                       int imgf_token,
                       void (*release_cb)(int token));

/* 仅 LVGL 线程调用 */
void ui_bridge_apply_pending(void);

#ifdef __cplusplus
}
#endif