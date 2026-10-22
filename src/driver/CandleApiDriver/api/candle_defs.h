/*

  Copyright (c) 2016 Hubert Denkmair <hubert@denkmair.de>
  Copyright (c) 2026 Schildkroet

  This file is part of the candle windows API.

  This library is free software: you can redistribute it and/or
  modify it under the terms of the GNU Lesser General Public
  License as published by the Free Software Foundation, either
  version 3 of the License, or (at your option) any later version.

  This library is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  Lesser General Public License for more details.

  You should have received a copy of the GNU Lesser General Public
  License along with this library.  If not, see <http://www.gnu.org/licenses/>.

*/

#pragma once

#include <stdint.h>

#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0600
#endif

#include <windows.h>
#include <winbase.h>
#include <winusb.h>
#include <setupapi.h>
#include <devguid.h>
#include <regstr.h>

#undef __CRT__NO_INLINE
#include <strsafe.h>
#define __CRT__NO_INLINE

#include "candle.h"

#define CANDLE_MAX_DEVICES 32
/* Outstanding USB IN requests. Legacy gs_usb firmware sends one frame per
 * transfer, so this is also how many frames the host controller can take
 * while the reader thread is not scheduled: 1024 = 240 ms at a saturated
 * 500 kbit/s bus (~4300 frames/s). 30 (7 ms) lost frames on a busy PC. */
#define CANDLE_URB_COUNT 1024

#pragma pack(push,1)

typedef struct {
    uint32_t byte_order;
} candle_host_config_t;

typedef struct {
    uint8_t reserved1;
    uint8_t reserved2;
    uint8_t reserved3;
    uint8_t icount;
    uint32_t sw_version;
    uint32_t hw_version;
} candle_device_config_t;

typedef struct {
    uint32_t mode;
    uint32_t flags;
} candle_device_mode_t;

#pragma pack(pop)


typedef struct {
    OVERLAPPED ovl;
    bool pending;
    uint8_t *buf;       /* rx_buf_size bytes inside candle_device_t.rx_buf */
} canlde_rx_urb;

typedef struct {
    wchar_t path[256];
    candle_devstate_t state;
    candle_err_t last_error;

    HANDLE deviceHandle;
    WINUSB_INTERFACE_HANDLE winUSBHandle;
    UCHAR interfaceNumber;
    UCHAR bulkInPipe;
    UCHAR bulkOutPipe;
    USHORT bulkInMaxPacket;
    HANDLE txEvent;     /* pre-allocated event for timed overlapped writes */

    candle_device_config_t dconf;
    candle_capability_t bt_const;
    /* Per-channel capabilities: index 0..dconf.icount, maximum 8 channels */
    candle_capability_t ch_caps[8];
    canlde_rx_urb rxurbs[CANDLE_URB_COUNT];
    HANDLE rxevents[CANDLE_URB_COUNT];
    unsigned rx_next_urb; /* oldest pending URB: the next one to complete */
    /* One allocation for all URB buffers while the device is open. A legacy
     * frame is at most 80 bytes; RAW_IO needs a multiple of the max packet
     * size, so a full-speed device gets 128 bytes per URB instead of 512. */
    uint8_t *rx_buf;
    DWORD rx_buf_size;

    /* ElmueSoft protocol (candle_elmue.c): capable = decided at open,
     * active = enabled by the running channel start. */
    bool elmue_capable;
    bool elmue_active;
    bool elmue_timestamps;
    uint8_t elmue_busload;
    uint8_t elmue_rx[2048];     /* the last USB IN transfer, being parsed */
    DWORD elmue_rx_len;
    DWORD elmue_rx_off;
    int elmue_rx_left;          /* messages left in elmue_rx */
} candle_device_t;

typedef struct {
    uint8_t num_devices;
    candle_err_t last_error;
    candle_device_t dev[CANDLE_MAX_DEVICES];
} candle_list_t;

