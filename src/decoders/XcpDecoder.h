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

#include <cstdint>
#include <optional>

#include <QMap>
#include <QString>

#include "IDecoder.h"

// CAN IDs that carry one XCP session on one interface.
// Request = master -> slave, response = slave -> master.
struct XcpConfig
{
    BusInterfaceId interfaceId = 0;
    uint32_t requestId = 0;
    uint32_t responseId = 0;
    std::optional<uint32_t> broadcastId;
    bool extended = false;

    [[nodiscard]] QString toString() const;
    bool operator==(const XcpConfig &other) const = default;
};

// One decoded XCP packet
struct XcpAnnotation
{
    QString type;       // e.g. "CTO (CONNECT)", "DAQ-DTO (DAQ #0|ODT #1)"
    QString comment;    // parameters, "KEY = value|KEY = value"
    int group = 0;      // 0 = CTO; PID + 1 for DTOs, so every ODT is its own row when aggregated
    bool fromMaster = false;
    MessageType kind = MessageType::Unknown;
};

// Decodes XCP on CAN traffic into a packet type and a parameter comment, in the
// style of a Vector/ASAM trace listing.
//
// Stateful on purpose: a slave's positive response only means something in the
// context of the command before it, and a DAQ-DTO's PID resolves to DAQ/ODT
// numbers only by following ALLOC_DAQ / ALLOC_ODT. Frames must therefore be fed
// exactly once, in capture order. When the session started before the capture,
// the missing context is reported (e.g. "DAQ-DTO (PID = 0x02)") rather than guessed.
//
// Ported from CanTerminal.Core/Xcp/XcpDecoder.cs.
class XcpDecoder : public IDecoder
{
public:
    explicit XcpDecoder(const XcpConfig &config);

    [[nodiscard]] const XcpConfig &config() const { return _config; }

    // True when the frame rides on the session's request, response or broadcast ID
    [[nodiscard]] bool matches(const BusMessage &frame) const;

    // Decodes a frame of the session; returns false for frames of other IDs
    bool decode(const BusMessage &frame, XcpAnnotation &out);

    // IDecoder: one frame is one complete XCP packet (protocol "XCP", id = group)
    DecodeStatus tryDecode(const BusMessage &frame, ProtocolMessage &outMsg) override;
    void reset() override;

    // What the decoder has learned so far, for a status line
    [[nodiscard]] QString describe() const;

private:
    static constexpr double max_pending_age_s = 5.0;

    XcpConfig _config;

    // session state, reset by CONNECT
    bool _bigEndian = false;
    uint8_t _identificationFieldType = 0;   // DAQ_KEY_BYTE bits 7-6; 0 = absolute ODT number
    bool _daqKeyKnown = false;
    std::optional<uint8_t> _pendingCmd;
    QByteArray _pendingData;
    double _pendingTs = 0.0;

    // dynamic DAQ configuration, reset by FREE_DAQ
    int _daqCount = 0;
    QMap<int, int> _odtCounts;

    XcpAnnotation decodeMaster(const BusMessage &frame, const QByteArray &d);
    XcpAnnotation decodeSlave(const BusMessage &frame, const QByteArray &d);
    XcpAnnotation decodeDto(const QByteArray &d, bool stim);

    [[nodiscard]] QString describeCommand(uint8_t pid, const QByteArray &d) const;
    [[nodiscard]] QString describeResponse(std::optional<uint8_t> cmd, const QByteArray &request, const QByteArray &d);
    void applyCommandState(uint8_t pid, const QByteArray &d);
    bool takePending(double ts, std::optional<uint8_t> &cmd, QByteArray &request);
    bool resolveAbsoluteOdt(uint8_t pid, int &daq, int &odt) const;
    void resetSession();
};
