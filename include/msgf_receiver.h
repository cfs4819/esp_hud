#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "usb_stream_router.h"

#ifdef __cplusplus
extern "C"
{
#endif

    typedef struct msgf_rx msgf_rx_t;

    typedef struct
    {
        size_t max_msg_bytes; /* e.g. 1024 */
        int queue_depth;      /* e.g. 4 or 8 */
        bool require_crc;
    } msgf_rx_config_t;

    msgf_rx_t *msgf_rx_create(const msgf_rx_config_t *cfg);
    void msgf_rx_destroy(msgf_rx_t *h);

    void msgf_rx_get_receiver(msgf_rx_t *h, usb_sr_receiver_t *out);

    /* consumer API: pop one message (non-blocking) */
    bool msgf_rx_pop(msgf_rx_t *h, uint8_t *dst, size_t dst_cap, size_t *out_len, uint32_t *out_seq);

    /* stats */
    typedef struct
    {
        uint32_t frames_ok;
        uint32_t frames_drop;
        uint32_t frames_bad;
    } msgf_rx_stats_t;

    void msgf_rx_get_stats(msgf_rx_t *h, msgf_rx_stats_t *out);

#ifdef __cplusplus
}
#endif