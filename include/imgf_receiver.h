#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "usb_stream_router.h"

#ifdef __cplusplus
extern "C"
{
#endif

    typedef struct imgf_rx imgf_rx_t;

    typedef enum
    {
        IMGF_DROP_NEW = 0,
        IMGF_DROP_OLD = 1
    } imgf_drop_policy_t;

    typedef struct
    {
        size_t max_png_bytes; /* e.g. 128*1024 */
        bool require_crc;
        imgf_drop_policy_t drop_policy;
    } imgf_rx_config_t;

    /* Create IMGF receiver (allocates 2 buffers; PSRAM preferred if available by your platform allocator). */
    imgf_rx_t *imgf_rx_create(const imgf_rx_config_t *cfg);
    void imgf_rx_destroy(imgf_rx_t *h);

    /* Get receiver descriptor to register into router */
    void imgf_rx_get_receiver(imgf_rx_t *h, usb_sr_receiver_t *out);

    /* Consumer API (non-blocking) */
    bool imgf_rx_get_ready(imgf_rx_t *h, const uint8_t **png, size_t *len, uint32_t *seq, int *token);
    void imgf_rx_release(imgf_rx_t *h, int token);

    /* stats */
    typedef struct
    {
        uint32_t frames_ok;
        uint32_t frames_drop;
        uint32_t frames_bad;
    } imgf_rx_stats_t;

    void imgf_rx_get_stats(imgf_rx_t *h, imgf_rx_stats_t *out);

#ifdef __cplusplus
}
#endif