/*
  Copyright (c) 2026 firepooh

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

#include "IDecoder.h"

#include <QMap>

// OBD-II on CAN: SAE J1979 / ISO 15031-5 services 0x01..0x0A carried by ISO 15765-4.
//
// Only the legislated OBD identifiers are looked at (11-bit 0x7DF, 0x7E0..0x7EF;
// 29-bit 0x18DB33F1, 0x18DAxxF1, 0x18DAF1xx). On those IDs a request with service
// 0x01..0x0A, a response 0x41..0x4A or a negative response to one of them becomes an
// "obd2" message; every other payload (UDS 0x10 and up) is left to the UDS decoder.
//
// The message id is (service << 8) | PID (PID 0 for services without one), and
// 0x7F00 | service for a negative response, so the aggregated view keeps one row
// per ECU, service and PID.
class ObdDecoder : public IDecoder
{
public:
    DecodeStatus tryDecode(const BusMessage &frame, ProtocolMessage &outMsg) override;
    void reset() override;

    [[nodiscard]] static bool isObdId(uint32_t id, bool extended);
    // Name, type, id, description and metadata of a complete payload (service byte first)
    static void describe(const QByteArray &payload, ProtocolMessage &msg);

    [[nodiscard]] static QString serviceName(uint8_t service);
    [[nodiscard]] static QString pidName(uint8_t pid);            // services 0x01 and 0x02
    [[nodiscard]] static QString vehicleInfoName(uint8_t pid);    // service 0x09
    [[nodiscard]] static int pidDataLength(uint8_t pid);          // bytes after the PID, 0 = unknown
    // Physical value of a service 0x01 / 0x02 PID; data = the bytes after the PID
    [[nodiscard]] static QString pidValue(uint8_t pid, const QByteArray &data);
    [[nodiscard]] static QString formatDtc(uint8_t a, uint8_t b); // "P0133"

private:
    struct Session
    {
        QVector<BusMessage> frames;
        QByteArray data;
        int expectedSize = 0;
        int nextSn = 1;
    };
    QMap<uint64_t, Session> m_sessions;
};
