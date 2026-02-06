#include "usb_stream_router.h"
#include <string.h>
#include <stdio.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

#define MIN(a, b) ((a) < (b) ? (a) : (b))

static uint32_t crc32_ieee(const uint8_t *data, size_t len)
{
    uint32_t crc = 0xffffffffu;
    for (size_t i = 0; i < len; i++)
    {
        crc ^= data[i];
        for (int j = 0; j < 8; j++)
            crc = (crc >> 1) ^ (0xedb88320u & (-(int)(crc & 1)));
    }
    return ~crc;
}

struct usb_stream_router
{
    usb_sr_transport_t tp;
    usb_sr_config_t cfg;

    SemaphoreHandle_t mtx;

    usb_sr_receiver_t *receivers;
    int receiver_count;

    bool has_default;
    usb_sr_receiver_t default_rcv;

    TaskHandle_t rx_task;

    usb_sr_stats_t st;
};

static int find_any_magic(const uint8_t *b, int n)
{
    /* We cannot know all magics; we just look for 4-byte boundary candidates by scanning,
       then validate hdr after we have full header. So return any position 0..n-4. */
    /* Heuristic: look for ASCII uppercase letters? Here keep simplest: just slide; real resync happens by header validation. */
    (void)b;
    return (n >= 4) ? 0 : -1;
}

static const usb_sr_receiver_t *lookup_receiver(usb_stream_router_t *r, uint32_t magic)
{
    for (int i = 0; i < r->receiver_count; i++)
    {
        if (r->receivers[i].magic == magic)
            return &r->receivers[i];
    }
    if (r->has_default)
        return &r->default_rcv;
    return NULL;
}

static void drop_frame(const usb_sr_receiver_t *rcv, int reason, const usb_sr_hdr_t *hdr)
{
    if (rcv && rcv->drop)
        rcv->drop(rcv->user, hdr, reason);
}

static void rx_task_fn(void *arg)
{
    usb_stream_router_t *r = (usb_stream_router_t *)arg;

    enum
    {
        ST_SYNC,
        ST_HDR,
        ST_PAYLOAD
    } st = ST_SYNC;

    usb_sr_hdr_t hdr;
    uint32_t hdr_got = 0;

    const usb_sr_receiver_t *rcv = NULL;
    void *payload_buf = NULL;
    size_t payload_cap = 0;
    uint32_t pay_got = 0;

    int chunk = r->cfg.read_chunk;
    if (chunk < 512)
        chunk = 512;
    if (chunk > 16384)
        chunk = 16384;

    static uint8_t tmp_storage[16384];
    uint8_t *tmp = tmp_storage;

    while (1)
    {
        int avail = r->tp.available(r->tp.ctx);
        if (avail <= 0)
        {
            vTaskDelay(1);
            continue;
        }

        int rd = MIN(avail, chunk);
        int n = r->tp.read(r->tp.ctx, tmp, rd);
        if (n <= 0)
            continue;

        r->st.bytes_rx += (uint64_t)n;

        int off = 0;
        while (off < n)
        {

            if (st == ST_SYNC)
            {
                /* We don't know magic set globally; try to start at current off, then validate header later.
                   If header invalid, slide by 1 and try again. */
                int pos = find_any_magic(tmp + off, n - off);
                if (pos < 0)
                    break;

                /* start collecting header from off+pos */
                hdr_got = 0;
                int take = MIN((int)sizeof(hdr), n - (off + pos));
                memcpy(((uint8_t *)&hdr) + hdr_got, tmp + off + pos, (size_t)take);
                hdr_got += (uint32_t)take;
                off = off + pos + take;
                st = (hdr_got == sizeof(hdr)) ? ST_PAYLOAD : ST_HDR;

                r->st.resync_count++;
                continue;
            }

            if (st == ST_HDR)
            {
                int need = (int)sizeof(hdr) - (int)hdr_got;
                int take = MIN(need, n - off);
                memcpy(((uint8_t *)&hdr) + hdr_got, tmp + off, (size_t)take);
                hdr_got += (uint32_t)take;
                off += take;

                if (hdr_got < sizeof(hdr))
                    continue;
                st = ST_PAYLOAD;
            }

            if (st == ST_PAYLOAD)
            {
                /* validate & bind receiver once per frame */
                rcv = lookup_receiver(r, hdr.magic);
                if (!rcv)
                {
                    r->st.frames_dropped++;
                    st = ST_SYNC;
                    continue;
                }

                if (hdr.len == 0 || hdr.len > rcv->max_len)
                {
                    r->st.frames_dropped++;
                    drop_frame(rcv, USB_SR_DROP_BAD_LEN, &hdr);
                    st = ST_SYNC;
                    continue;
                }

                /* acquire payload buffer */
                payload_buf = NULL;
                payload_cap = 0;
                if (rcv->acquire)
                {
                    payload_buf = rcv->acquire(rcv->user, &hdr, &payload_cap);
                }
                if (!payload_buf || payload_cap < hdr.len)
                {
                    r->st.frames_dropped++;
                    drop_frame(rcv, USB_SR_DROP_NO_BUFFER, &hdr);
                    st = ST_SYNC;
                    continue;
                }

                /* copy payload (might already have some bytes in tmp after header) */
                pay_got = 0;
                while (off < n && pay_got < hdr.len)
                {
                    int need = (int)hdr.len - (int)pay_got;
                    int take = MIN(need, n - off);
                    memcpy(((uint8_t *)payload_buf) + pay_got, tmp + off, (size_t)take);
                    pay_got += (uint32_t)take;
                    off += take;
                }

                if (pay_got < hdr.len)
                {
                    /* need continue receiving payload across next reads */
                    while (pay_got < hdr.len)
                    {
                        int a = r->tp.available(r->tp.ctx);
                        if (a <= 0)
                        {
                            vTaskDelay(1);
                            continue;
                        }

                        int rr = MIN(a, chunk);
                        int nn = r->tp.read(r->tp.ctx, tmp, rr);
                        if (nn <= 0)
                            continue;
                        r->st.bytes_rx += (uint64_t)nn;

                        int oo = 0;
                        while (oo < nn && pay_got < hdr.len)
                        {
                            int need = (int)hdr.len - (int)pay_got;
                            int take = MIN(need, nn - oo);
                            memcpy(((uint8_t *)payload_buf) + pay_got, tmp + oo, (size_t)take);
                            pay_got += (uint32_t)take;
                            oo += take;
                        }
                    }
                }

                /* CRC check */
                bool need_crc = rcv->require_crc;
                if (need_crc)
                {
                    if (hdr.crc32 == 0)
                    {
                        r->st.frames_dropped++;
                        drop_frame(rcv, USB_SR_DROP_BAD_CRC, &hdr);
                        st = ST_SYNC;
                        continue;
                    }
                    uint32_t c = crc32_ieee((const uint8_t *)payload_buf, (size_t)hdr.len);
                    if (c != hdr.crc32)
                    {
                        r->st.frames_dropped++;
                        drop_frame(rcv, USB_SR_DROP_BAD_CRC, &hdr);
                        st = ST_SYNC;
                        continue;
                    }
                }

                /* commit */
                if (rcv->commit)
                    rcv->commit(rcv->user, &hdr, payload_buf, (size_t)hdr.len);

                r->st.frames_ok++;
                st = ST_SYNC;
            }
        }
    }
}

usb_stream_router_t *usb_sr_create(const usb_sr_transport_t *tp,
                                   const usb_sr_config_t *cfg)
{
    if (!tp || !cfg || !tp->available || !tp->read)
        return NULL;

    usb_stream_router_t *r = (usb_stream_router_t *)pvPortMalloc(sizeof(*r));
    if (!r)
        return NULL;
    memset(r, 0, sizeof(*r));
    r->tp = *tp;
    r->cfg = *cfg;

    r->mtx = xSemaphoreCreateMutex();
    if (!r->mtx)
    {
        vPortFree(r);
        return NULL;
    }

    r->receivers = (usb_sr_receiver_t *)pvPortMalloc(sizeof(usb_sr_receiver_t) * cfg->max_receivers);
    if (!r->receivers)
    {
        vSemaphoreDelete(r->mtx);
        vPortFree(r);
        return NULL;
    }
    r->receiver_count = 0;

#if (configNUM_CORES > 1)
    BaseType_t ok;
    if (cfg->rx_task_core == 0 || cfg->rx_task_core == 1)
    {
        ok = xTaskCreatePinnedToCore(rx_task_fn, "usb_sr",
                                     cfg->rx_task_stack, r,
                                     cfg->rx_task_priority, &r->rx_task,
                                     cfg->rx_task_core);
        (void)ok;
    }
    else
#endif
    {
        if (xTaskCreate(rx_task_fn, "usb_sr",
                        cfg->rx_task_stack, r,
                        cfg->rx_task_priority, &r->rx_task) != pdPASS)
        {
            usb_sr_destroy(r);
            return NULL;
        }
    }

    return r;
}

void usb_sr_destroy(usb_stream_router_t *r)
{
    if (!r)
        return;
    if (r->rx_task)
        vTaskDelete(r->rx_task);
    if (r->receivers)
        vPortFree(r->receivers);
    if (r->mtx)
        vSemaphoreDelete(r->mtx);
    vPortFree(r);
}

bool usb_sr_register(usb_stream_router_t *r, const usb_sr_receiver_t *rcv)
{
    if (!r || !rcv)
        return false;
    xSemaphoreTake(r->mtx, portMAX_DELAY);
    if (r->receiver_count >= r->cfg.max_receivers)
    {
        xSemaphoreGive(r->mtx);
        return false;
    }
    r->receivers[r->receiver_count++] = *rcv;
    xSemaphoreGive(r->mtx);
    return true;
}

void usb_sr_set_default(usb_stream_router_t *r, const usb_sr_receiver_t *rcv)
{
    if (!r)
        return;
    xSemaphoreTake(r->mtx, portMAX_DELAY);
    if (rcv)
    {
        r->default_rcv = *rcv;
        r->has_default = true;
    }
    else
    {
        r->has_default = false;
    }
    xSemaphoreGive(r->mtx);
}

void usb_sr_get_stats(usb_stream_router_t *r, usb_sr_stats_t *out)
{
    if (!r || !out)
        return;
    *out = r->st;
}

void usb_sr_reset_stats(usb_stream_router_t *r)
{
    if (!r)
        return;
    memset(&r->st, 0, sizeof(r->st));
}