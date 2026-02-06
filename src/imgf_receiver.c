#include "imgf_receiver.h"
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#if __has_include("esp_heap_caps.h")
#include "esp_heap_caps.h"
static void *buf_alloc(size_t n)
{
    void *p = heap_caps_malloc(n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (p)
        return p;
    return heap_caps_malloc(n, MALLOC_CAP_8BIT);
}
static void buf_free(void *p) { heap_caps_free(p); }
#else
#include <stdlib.h>
static void *buf_alloc(size_t n) { return malloc(n); }
static void buf_free(void *p) { free(p); }
#endif

#define MAGIC_IMGF 0x46474D49u
enum
{
    BFREE = 0,
    BWRITING,
    BREADY,
    BREADING
};

struct imgf_rx
{
    imgf_rx_config_t cfg;
    SemaphoreHandle_t mtx;

    uint8_t *buf[2];
    size_t cap;

    uint8_t state[2];
    size_t len[2];
    uint32_t seq[2];

    int wr_idx;

    imgf_rx_stats_t st;
};

static void *imgf_acquire(void *user, const usb_sr_hdr_t *hdr, size_t *capacity)
{
    imgf_rx_t *h = (imgf_rx_t *)user;
    if (!h || !capacity)
        return NULL;

    xSemaphoreTake(h->mtx, portMAX_DELAY);

    int wi = h->wr_idx;
    if (h->state[wi] != BFREE)
    {
        int alt = wi ^ 1;
        if (h->state[alt] == BFREE)
            wi = alt;
    }

    if (h->state[wi] != BFREE)
    {
        if (h->cfg.drop_policy == IMGF_DROP_OLD)
        {
            int drop = (h->state[0] == BREADY) ? 0 : ((h->state[1] == BREADY) ? 1 : -1);
            if (drop >= 0)
            {
                h->state[drop] = BFREE;
                h->len[drop] = 0;
                h->st.frames_drop++;
                wi = drop;
            }
            else
            {
                h->st.frames_drop++;
                xSemaphoreGive(h->mtx);
                return NULL;
            }
        }
        else
        {
            h->st.frames_drop++;
            xSemaphoreGive(h->mtx);
            return NULL;
        }
    }

    h->wr_idx = wi;
    h->state[wi] = BWRITING;
    *capacity = h->cap;

    void *p = h->buf[wi];
    xSemaphoreGive(h->mtx);
    (void)hdr;
    return p;
}

static void imgf_commit(void *user, const usb_sr_hdr_t *hdr, void *buf, size_t len)
{
    imgf_rx_t *h = (imgf_rx_t *)user;
    (void)buf;

    xSemaphoreTake(h->mtx, portMAX_DELAY);
    int wi = h->wr_idx;
    h->state[wi] = BREADY;
    h->len[wi] = len;
    h->seq[wi] = hdr->seq;
    h->st.frames_ok++;
    h->wr_idx ^= 1;
    xSemaphoreGive(h->mtx);
}

static void imgf_drop(void *user, const usb_sr_hdr_t *hdr, int reason)
{
    imgf_rx_t *h = (imgf_rx_t *)user;
    (void)hdr;
    (void)reason;
    xSemaphoreTake(h->mtx, portMAX_DELAY);
    h->st.frames_bad++;
    /* if we were WRITING, free it */
    int wi = h->wr_idx;
    if (h->state[wi] == BWRITING)
    {
        h->state[wi] = BFREE;
        h->len[wi] = 0;
    }
    xSemaphoreGive(h->mtx);
}

imgf_rx_t *imgf_rx_create(const imgf_rx_config_t *cfg)
{
    if (!cfg || cfg->max_png_bytes < 1024)
        return NULL;

    imgf_rx_t *h = (imgf_rx_t *)buf_alloc(sizeof(*h));
    if (!h)
        return NULL;
    memset(h, 0, sizeof(*h));
    h->cfg = *cfg;
    h->cap = cfg->max_png_bytes;

    h->mtx = xSemaphoreCreateMutex();
    if (!h->mtx)
    {
        buf_free(h);
        return NULL;
    }

    for (int i = 0; i < 2; i++)
    {
        h->buf[i] = (uint8_t *)buf_alloc(h->cap);
        if (!h->buf[i])
        {
            imgf_rx_destroy(h);
            return NULL;
        }
        h->state[i] = BFREE;
    }
    h->wr_idx = 0;
    return h;
}

void imgf_rx_destroy(imgf_rx_t *h)
{
    if (!h)
        return;
    if (h->buf[0])
        buf_free(h->buf[0]);
    if (h->buf[1])
        buf_free(h->buf[1]);
    if (h->mtx)
        vSemaphoreDelete(h->mtx);
    buf_free(h);
}

void imgf_rx_get_receiver(imgf_rx_t *h, usb_sr_receiver_t *out)
{
    memset(out, 0, sizeof(*out));
    out->user = h;
    out->magic = MAGIC_IMGF;
    out->max_len = h->cap;
    out->require_crc = h->cfg.require_crc;
    out->acquire = imgf_acquire;
    out->commit = imgf_commit;
    out->drop = imgf_drop;
}

bool imgf_rx_get_ready(imgf_rx_t *h, const uint8_t **png, size_t *len, uint32_t *seq, int *token)
{
    if (!h || !png || !len || !token)
        return false;
    bool ok = false;

    xSemaphoreTake(h->mtx, portMAX_DELAY);

    int idx = -1;
    if (h->state[0] == BREADY)
        idx = 0;
    else if (h->state[1] == BREADY)
        idx = 1;

    if (idx >= 0)
    {
        h->state[idx] = BREADING;
        *png = h->buf[idx];
        *len = h->len[idx];
        if (seq)
            *seq = h->seq[idx];
        *token = idx;
        ok = true;
    }

    xSemaphoreGive(h->mtx);
    return ok;
}

void imgf_rx_release(imgf_rx_t *h, int token)
{
    if (!h || (token != 0 && token != 1))
        return;
    xSemaphoreTake(h->mtx, portMAX_DELAY);
    h->state[token] = BFREE;
    h->len[token] = 0;
    xSemaphoreGive(h->mtx);
}

void imgf_rx_get_stats(imgf_rx_t *h, imgf_rx_stats_t *out)
{
    if (!h || !out)
        return;
    xSemaphoreTake(h->mtx, portMAX_DELAY);
    *out = h->st;
    xSemaphoreGive(h->mtx);
}