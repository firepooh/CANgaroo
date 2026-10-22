/*

  Copyright (c) 2026 Schildkroet

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

#pragma once

// Decoding of gs_usb / Candlelight error frames into cangaroo error flags.
//
// The layout is the Linux SocketCAN error frame (linux/can/error.h): the ID
// carries error classes, data[1] the controller state, data[2] the protocol
// error type, data[3] its location, data[6]/data[7] the TX/RX error counters.
//
// The Candlelight 2.5 firmware (Elmue) adds:
//   - ID 0x200 for a CRC error, in both USB protocols
//   - data[5] = application flags (Firmware/settings.h eErrorAppFlags)
//   - with the ElmueSoft protocol it does NOT set the ID classes 0x04
//     (controller) and 0x08 (protocol): data[1] and data[2] are always valid
//     there, as Elmue's own sample application reads them
//     (SampleApplication C++/Source/Candlelight/Candlelight.cpp,
//     Candlelight::FormatCanErrors()).

#include <cstdint>

#include "core/BusMessage.h"

struct CandleErrorFrame
{
    BusErrors flags;
    bool rxLost = false;    // the device lost received frames before this report
};

namespace CandleError
{
    // ID classes (linux/can/error.h CAN_ERR_*, Candlelight ERID_*)
    constexpr uint32_t IdTxTimeout   = 0x0001;
    constexpr uint32_t IdController  = 0x0004;
    constexpr uint32_t IdProtocol    = 0x0008;
    constexpr uint32_t IdAck         = 0x0020;
    constexpr uint32_t IdBusOff      = 0x0040;
    constexpr uint32_t IdRestarted   = 0x0100;
    constexpr uint32_t IdCrc         = 0x0200;   // Candlelight 2.5 extension

    // data[1] controller state (CAN_ERR_CRTL_*, Candlelight ER1_*)
    constexpr uint8_t CtrlRxOverflow = 0x01;
    constexpr uint8_t CtrlTxOverflow = 0x02;
    constexpr uint8_t CtrlWarning    = 0x0C;     // RX | TX warning
    constexpr uint8_t CtrlPassive    = 0x30;     // RX | TX passive
    constexpr uint8_t CtrlActive     = 0x40;     // back to error active

    // data[2] protocol error type (CAN_ERR_PROT_*, Candlelight ER2_*)
    constexpr uint8_t ProtBit        = 0x01;
    constexpr uint8_t ProtForm       = 0x02;
    constexpr uint8_t ProtStuff      = 0x04;
    constexpr uint8_t ProtBit0       = 0x08;     // unable to send a dominant bit
    constexpr uint8_t ProtBit1       = 0x10;     // unable to send a recessive bit

    // data[3] protocol error location (CAN_ERR_PROT_LOC_*)
    constexpr uint8_t LocCrcSequence = 0x08;

    // data[5] Candlelight 2.5 application flags (eErrorAppFlags)
    constexpr uint8_t AppCanRxFail     = 0x01;   // CAN RX FIFO overflowed in the device
    constexpr uint8_t AppUsbInOverflow = 0x08;   // host did not read USB in time
    constexpr uint8_t AppCanTxTimeout  = 0x10;   // frame not acknowledged within 500 ms, aborted
}

// errId: error frame ID without the error flag bit. data: 8 bytes.
// elmue: the frame arrived over the ElmueSoft protocol (Candlelight 2.5).
inline CandleErrorFrame candleDecodeErrorFrame(uint32_t errId, const uint8_t *data, bool elmue)
{
    using namespace CandleError;
    CandleErrorFrame r;

    if (errId & IdTxTimeout) r.flags |= BusError::TxTimeout;
    if (errId & IdAck)       r.flags |= BusError::Ack;
    if (errId & IdBusOff)    r.flags |= BusError::BusOff;
    if (errId & IdRestarted) r.flags |= BusError::Restarted;
    if (errId & IdCrc)       r.flags |= BusError::Crc;

    if (elmue || (errId & IdController))
    {
        const uint8_t ctrl = data[1];
        if (ctrl & (CtrlRxOverflow | CtrlTxOverflow)) r.flags |= BusError::Overrun;
        if (ctrl & CtrlRxOverflow) r.rxLost = true;
        if (ctrl & CtrlWarning)    r.flags |= BusError::ErrorWarning;
        if (ctrl & CtrlPassive)    r.flags |= BusError::ErrorPassive;
        if (ctrl & CtrlActive)     r.flags |= BusError::ErrorActive;
    }

    if (elmue || (errId & IdProtocol))
    {
        const uint8_t prot = data[2];
        if (prot & (ProtBit | ProtBit0 | ProtBit1)) r.flags |= BusError::Bit;
        if (prot & ProtForm)  r.flags |= BusError::Form;
        if (prot & ProtStuff) r.flags |= BusError::Stuff;
    }
    if ((errId & IdProtocol) && data[3] == LocCrcSequence) r.flags |= BusError::Crc;

    // data[5] is reserved in the Linux layout: only trust it from Candlelight 2.5
    if (elmue)
    {
        const uint8_t app = data[5];
        if (app & AppCanTxTimeout) r.flags |= BusError::TxTimeout;
        if (app & (AppCanRxFail | AppUsbInOverflow))
        {
            r.flags |= BusError::Overrun;
            r.rxLost = true;
        }
    }

    // CAN_ERR_BUSERROR (0x80) or anything unclassified
    if (!r.flags) r.flags |= BusError::Generic;
    return r;
}
