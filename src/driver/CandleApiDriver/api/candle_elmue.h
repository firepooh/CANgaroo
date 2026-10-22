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

/*
 * ElmueSoft protocol of the Candlelight 2.5 firmware (CANable 2.5).
 *
 * Message formats follow candlelight_def.h of the firmware and the BSD
 * licensed sample applications of ElmueSoft
 * (github.com/Elmue/CANable-2.5-firmware-Slcan-and-Candlelight).
 *
 * Compared to the legacy gs_usb protocol:
 *  - the device packs every frame waiting in its buffer into one USB
 *    transfer (blob), so a backlog drains in one transfer and frames only
 *    take the bytes they need (an 8 byte FD frame: 19 instead of 80 bytes)
 *  - the TX echo carries a one-byte marker and is sent when the frame was
 *    really transmitted (ACKed), with that time stamp; the legacy echo is
 *    sent when the frame enters the device's TX FIFO
 *
 * Only single channel devices use it: with this protocol the firmware moves
 * every channel to its own USB interface, but this driver opens interface 0.
 * Setting CANGAROO_CANDLE_PROTOCOL=legacy keeps the legacy protocol.
 */

#pragma once

#include "candle.h"
#include "candle_defs.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Device mode / capability flags (eDeviceFlags in the firmware) */
#define CANDLE_ELMUE_FLAG_PROTOCOL   0x4000u   /* ELM_DevFlagProtocolElmue */
#define CANDLE_ELMUE_FLAG_BLOBS      0x8000u   /* ELM_DevFlagSendUsbBlobs  */

/* Largest USB IN transfer of the firmware (MAX_BLOB_SIZE) */
#define CANDLE_ELMUE_MAX_TRANSFER    2048u

/* Decides at device open whether channel starts will enable the protocol. */
bool candle_elmue_supported(const candle_device_t *dev);

/* Takes the bytes of one completed USB IN transfer (a blob or one message). */
void candle_elmue_load(candle_device_t *dev, const uint8_t *buf, DWORD len);

/* Next CAN frame, TX echo or error from the loaded transfer, converted to the
 * legacy frame layout: RX frames get echo_id 0xFFFFFFFF, TX echoes the marker
 * as echo_id, errors CANDLE_ID_ERR in can_id. Returns false when nothing is
 * left. String and bus load messages are logged or stored and skipped. */
bool candle_elmue_next_frame(candle_device_t *dev, candle_fd_frame_t *frame);

/* Builds a kTxFrameElmue from a legacy frame. The marker (1..255) is taken
 * from frame->echo_id; 0 asks the device for no echo. Returns the size. */
DWORD candle_elmue_build_tx(const candle_fd_frame_t *frame, bool fd, uint8_t *out);

#ifdef __cplusplus
}
#endif
