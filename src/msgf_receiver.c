#include "msgf_receiver.h"
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"

#if __has_include("esp_heap_caps.h")
#include "esp_heap_caps.h"
static void *mem_alloc(size_t n)
{
    void *p = heap_caps_malloc(n, MALLOC_CAP_8BIT);
    return p;
}
static void mem_free(void *p) { heap_caps_free(p); }
#else
#include <stdlib.h>
static void *mem_alloc(size_t n) { return malloc(n); }
static void mem_free(void *p) { free(p); }
#endif

#define MAGIC_MSGF 0x4647534Du /* 'MSGF' little endian */

typedef struct
{
    uint8_t *buf;
    size_t len;
    uint32_t seq;
} msg_item_t;

struct msgf_rx
{
    msgf_rx_config_t cfg;
    QueueHandle_t q;
    SemaphoreHandle_t mtx;

    /* pool */
    uint8_t *pool; /* queue_depth * max_msg_bytes */
    int depth;

    /* writing temp */
    int cur_slot;

    msgf_rx_stats_t st;
};

static void *msgf_acquire(void *user, const usb_sr_hdr_t *hdr, size_t *capacity)
{
    msgf_rx_t *h = (msgf_rx_t *)user;
    if (!h)
        return NULL;

    /* best effort: if queue full, drop new */
    if (uxQueueSpacesAvailable(h->q) == 0)
    {
        h->st.frames_drop++;
        return NULL;
    }

    xSemaphoreTake(h->mtx, portMAX_DELAY);
    int slot = h->cur_slot;
    h->cur_slot = (h->cur_slot + 1) % h->depth;
    xSemaphoreGive(h->mtx);

    uint8_t *p = h->pool + (size_t)slot * h->cfg.max_msg_bytes;
    *capacity = h->cfg.max_msg_bytes;
    (void)hdr;
    return p;
}

static void msgf_commit(void *user, const usb_sr_hdr_t *hdr, void *buf, size_t len)
{
    msgf_rx_t *h = (msgf_rx_t *)user;
    msg_item_t it = {
        .buf = (uint8_t *)buf,
        .len = len,
        .seq = hdr->seq};
    if (xQueueSend(h->q, &it, 0) != pdTRUE)
    {
        h->st.frames_drop++;
        return;
    }
    h->st.frames_ok++;
}

static void msgf_drop(void *user, const usb_sr_hdr_t *hdr, int reason)
{
    msgf_rx_t *h = (msgf_rx_t *)user;
    (void)hdr;
    (void)reason;
    h->st.frames_bad++;
}

msgf_rx_t *msgf_rx_create(const msgf_rx_config_t *cfg)
{
    if (!cfg || cfg->max_msg_bytes < 16 || cfg->queue_depth < 2)
        return NULL;

    msgf_rx_t *h = (msgf_rx_t *)mem_alloc(sizeof(*h));
    if (!h)
        return NULL;
    memset(h, 0, sizeof(*h));
    h->cfg = *cfg;
    h->depth = cfg->queue_depth;

    h->q = xQueueCreate((UBaseType_t)h->depth, sizeof(msg_item_t));
    if (!h->q)
    {
        msgf_rx_destroy(h);
        return NULL;
    }

    h->mtx = xSemaphoreCreateMutex();
    if (!h->mtx)
    {
        msgf_rx_destroy(h);
        return NULL;
    }

    h->pool = (uint8_t *)mem_alloc((size_t)h->depth * h->cfg.max_msg_bytes);
    if (!h->pool)
    {
        msgf_rx_destroy(h);
        return NULL;
    }

    h->cur_slot = 0;
    return h;
}

void msgf_rx_destroy(msgf_rx_t *h)
{
    if (!h)
        return;
    if (h->pool)
        mem_free(h->pool);
    if (h->q)
        vQueueDelete(h->q);
    if (h->mtx)
        vSemaphoreDelete(h->mtx);
    mem_free(h);
}

void msgf_rx_get_receiver(msgf_rx_t *h, usb_sr_receiver_t *out)
{
    memset(out, 0, sizeof(*out));
    out->user = h;
    out->magic = MAGIC_MSGF;
    out->max_len = h->cfg.max_msg_bytes;
    out->require_crc = h->cfg.require_crc;
    out->acquire = msgf_acquire;
    out->commit = msgf_commit;
    out->drop = msgf_drop;
}

bool msgf_rx_pop(msgf_rx_t *h, uint8_t *dst, size_t dst_cap, size_t *out_len, uint32_t *out_seq)
{
    if (!h || !dst || dst_cap == 0)
        return false;

    msg_item_t it;
    if (xQueueReceive(h->q, &it, 0) != pdTRUE)
        return false;

    size_t n = it.len;
    if (n > dst_cap)
        n = dst_cap;
    memcpy(dst, it.buf, n);

    if (out_len)
        *out_len = n;
    if (out_seq)
        *out_seq = it.seq;
    return true;
}

void msgf_rx_get_stats(msgf_rx_t *h, msgf_rx_stats_t *out)
{
    if (!h || !out)
        return;
    *out = h->st;
}