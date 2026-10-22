/*

  Copyright (c) 2026 jypark7

  This file is part of cangaroo.

  cangaroo is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 2 of the License, or
  (at your option) any later version.

  cangaroo is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with cangaroo.  If not, see <http://www.gnu.org/licenses/>.

*/

#include "candle_elmue.h"

#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

/* eMessageType of the firmware */
enum {
    ELMUE_MSG_TX_FRAME = 0x0A,
    ELMUE_MSG_TX_ECHO  = 0x0B,
    ELMUE_MSG_RX_FRAME = 0x0C,
    ELMUE_MSG_ERROR    = 0x0D,
    ELMUE_MSG_STRING   = 0x0E,
    ELMUE_MSG_BUSLOAD  = 0x0F,
    ELMUE_MSG_RX_BLOB  = 0x11,
};

/* Sizes of the fixed parts (kHeader = size + msg_type) */
#define ELMUE_RX_HEAD      7u   /* header, flags, can_id               (+ timestamp 4) */
#define ELMUE_TX_HEAD      8u   /* header, flags, can_id, marker                        */
#define ELMUE_ECHO_HEAD    3u   /* header, marker                      (+ timestamp 4) */
#define ELMUE_ERROR_HEAD  14u   /* header, err_id, err_data[8]          (+ timestamp 4) */

/* Same as the static candle_logf() in candle.c */
static void candle_logf(const wchar_t *fmt, ...)
{
    if (candle_log_fn == NULL) {
        return;
    }
    wchar_t buf[512];
    va_list args;
    va_start(args, fmt);
    HRESULT hr = StringCchVPrintfW(buf, 512, fmt, args);
    va_end(args);
    if (SUCCEEDED(hr)) {
        candle_log_fn(buf);
    }
}

static uint32_t rd32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void wr32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

bool candle_elmue_supported(const candle_device_t *dev)
{
    const char *env = getenv("CANGAROO_CANDLE_PROTOCOL");
    if (env && _stricmp(env, "legacy") == 0) {
        return false;
    }
    const uint32_t need = CANDLE_ELMUE_FLAG_PROTOCOL | CANDLE_ELMUE_FLAG_BLOBS;
    return dev->dconf.icount == 0 && (dev->bt_const.feature & need) == need;
}

void candle_elmue_load(candle_device_t *dev, const uint8_t *buf, DWORD len)
{
    if (len > sizeof(dev->elmue_rx)) {
        len = sizeof(dev->elmue_rx);
    }
    memcpy(dev->elmue_rx, buf, len);
    dev->elmue_rx_len = len;
    dev->elmue_rx_off = 0;
    dev->elmue_rx_left = 1;

    /* kBlob: frame_count, msg_type = MSG_RxBlob, then frame_count messages */
    if (len >= 2 && buf[1] == ELMUE_MSG_RX_BLOB) {
        dev->elmue_rx_left = buf[0];
        dev->elmue_rx_off = 2;
    }
}

bool candle_elmue_next_frame(candle_device_t *dev, candle_fd_frame_t *frame)
{
    const uint8_t *buf = dev->elmue_rx;
    const DWORD ts_len = dev->elmue_timestamps ? 4u : 0u;

    while (dev->elmue_rx_left > 0 && dev->elmue_rx_off + 2 <= dev->elmue_rx_len) {
        const uint8_t *m = buf + dev->elmue_rx_off;
        const DWORD size = m[0];
        const uint8_t type = m[1];

        if (size < 2 || dev->elmue_rx_off + size > dev->elmue_rx_len) {
            candle_logf(L"elmue: corrupt message size=%lu type=0x%02x at %lu/%lu",
                        size, type, dev->elmue_rx_off, dev->elmue_rx_len);
            dev->elmue_rx_left = 0;
            return false;
        }
        dev->elmue_rx_off += size;
        dev->elmue_rx_left--;

        memset(frame, 0, sizeof(*frame));
        frame->echo_id = 0xFFFFFFFF;

        switch (type) {
        case ELMUE_MSG_RX_FRAME: {
            if (size < ELMUE_RX_HEAD + ts_len) {
                continue;
            }
            const uint8_t *data = m + ELMUE_RX_HEAD + ts_len;
            DWORD count = size - ELMUE_RX_HEAD - ts_len;
            if (count > sizeof(frame->data)) {
                count = sizeof(frame->data);
            }
            frame->flags = m[2];
            frame->can_id = rd32(m + 3);
            frame->timestamp_us = ts_len ? rd32(m + ELMUE_RX_HEAD) : 0;
            if (frame->can_id & CANDLE_ID_RTR) {
                /* remote frames send their DLC as the only data byte */
                frame->can_dlc = count ? data[0] : 0;
            } else {
                memcpy(frame->data, data, count);
                frame->can_dlc = (frame->flags & CANDLE_FRAME_FLAG_FD)
                                 ? candle_len_to_dlc((uint8_t)count) : (uint8_t)count;
            }
            return true;
        }
        case ELMUE_MSG_TX_ECHO:
            if (size < ELMUE_ECHO_HEAD + ts_len) {
                continue;
            }
            frame->echo_id = m[2];
            frame->timestamp_us = ts_len ? rd32(m + ELMUE_ECHO_HEAD) : 0;
            return true;

        case ELMUE_MSG_ERROR:
            if (size < ELMUE_ERROR_HEAD + ts_len) {
                continue;
            }
            /* same error id and data bytes as a legacy error frame */
            frame->can_id = rd32(m + 2) | CANDLE_ID_ERR;
            frame->can_dlc = 8;
            memcpy(frame->data, m + 6, 8);
            frame->timestamp_us = ts_len ? rd32(m + ELMUE_ERROR_HEAD) : 0;
            return true;

        case ELMUE_MSG_STRING: {
            wchar_t text[256];
            DWORD n = size - 2;
            if (n >= sizeof(text) / sizeof(text[0])) {
                n = sizeof(text) / sizeof(text[0]) - 1;
            }
            for (DWORD i = 0; i < n; i++) {
                text[i] = (wchar_t)m[2 + i];
            }
            text[n] = 0;
            candle_logf(L"elmue firmware: %ls", text);
            continue;
        }
        case ELMUE_MSG_BUSLOAD:
            if (size >= 3) {
                dev->elmue_busload = m[2];
            }
            continue;

        default:
            candle_logf(L"elmue: unknown message type=0x%02x size=%lu", type, size);
            continue;
        }
    }
    dev->elmue_rx_left = 0;
    return false;
}

DWORD candle_elmue_build_tx(const candle_fd_frame_t *frame, bool fd, uint8_t *out)
{
    DWORD count;
    if (frame->can_id & CANDLE_ID_RTR) {
        /* remote frame: the DLC goes into the first data byte */
        out[ELMUE_TX_HEAD] = frame->can_dlc;
        count = 1;
    } else {
        count = fd ? candle_dlc_to_len(frame->can_dlc) : (frame->can_dlc > 8 ? 8 : frame->can_dlc);
        memcpy(out + ELMUE_TX_HEAD, frame->data, count);
    }

    out[0] = (uint8_t)(ELMUE_TX_HEAD + count);
    out[1] = ELMUE_MSG_TX_FRAME;
    out[2] = frame->flags;
    wr32(out + 3, frame->can_id);
    out[7] = (uint8_t)frame->echo_id;
    return ELMUE_TX_HEAD + count;
}
