#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C"
{
#endif

    /* -------- Transport abstraction (CDC / UART / bulk...) -------- */
    typedef struct
    {
        void *ctx;
        int (*available)(void *ctx);
        int (*read)(void *ctx, uint8_t *dst, int max_len);
    } usb_sr_transport_t;

    /* -------- Frame header -------- */
    typedef struct __attribute__((packed))
    {
        uint32_t magic;
        uint8_t type;
        uint8_t flags;
        uint16_t rsv;
        uint32_t len;
        uint32_t crc32;
        uint32_t seq;
    } usb_sr_hdr_t;

    /* -------- Receiver interface --------
       Router does NOT own receiver storage. Receiver provides buffer for payload. */
    typedef struct usb_sr_receiver usb_sr_receiver_t;

    typedef void *(*usb_sr_acquire_fn)(void *user, const usb_sr_hdr_t *hdr, size_t *capacity);
    typedef void (*usb_sr_commit_fn)(void *user, const usb_sr_hdr_t *hdr, void *buf, size_t len);
    typedef void (*usb_sr_drop_fn)(void *user, const usb_sr_hdr_t *hdr, int reason);

    /* drop reasons */
    enum
    {
        USB_SR_DROP_NO_RECEIVER = 1,
        USB_SR_DROP_BAD_LEN = 2,
        USB_SR_DROP_BAD_CRC = 3,
        USB_SR_DROP_NO_BUFFER = 4,
    };

    struct usb_sr_receiver
    {
        void *user;
        uint32_t magic;            /* which magic this receiver consumes */
        size_t max_len;            /* hard cap for this receiver */
        bool require_crc;          /* if true, crc32 must match and non-zero */
        usb_sr_acquire_fn acquire; /* provide buffer for payload */
        usb_sr_commit_fn commit;   /* called after payload fully received */
        usb_sr_drop_fn drop;       /* called when frame dropped */
    };

    /* -------- Router config -------- */
    typedef struct
    {
        int rx_task_priority; /* e.g. 18 */
        int rx_task_stack;    /* e.g. 6144 */
        int rx_task_core;     /* -1 no pin */
        int read_chunk;       /* e.g. 8192 */
        int max_receivers;    /* e.g. 4 */
        void (*on_rx_activity)(void *user, size_t bytes);
        void *on_rx_activity_user;
    } usb_sr_config_t;

    typedef struct usb_stream_router usb_stream_router_t;

    /* Create router (starts RX task). */
    usb_stream_router_t *usb_sr_create(const usb_sr_transport_t *tp,
                                       const usb_sr_config_t *cfg);

    /* Stop & free router. */
    void usb_sr_destroy(usb_stream_router_t *r);

    /* Register a receiver for a magic. Return false if full. */
    bool usb_sr_register(usb_stream_router_t *r, const usb_sr_receiver_t *rcv);

    /* Optional: set default handler for unknown magic (can be NULL -> drop silently). */
    void usb_sr_set_default(usb_stream_router_t *r, const usb_sr_receiver_t *rcv);

    /* Stats */
    typedef struct
    {
        uint64_t bytes_rx;
        uint32_t frames_ok;
        uint32_t frames_dropped;
        uint32_t resync_count;
    } usb_sr_stats_t;

    void usb_sr_get_stats(usb_stream_router_t *r, usb_sr_stats_t *out);
    void usb_sr_reset_stats(usb_stream_router_t *r);

#ifdef __cplusplus
}
#endif
