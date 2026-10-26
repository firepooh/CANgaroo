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

#include "XcpDecoder.h"

#include <QStringList>

#include "XcpTables.h"

namespace
{

QString hexN(uint32_t v, int digits)
{
    return QStringLiteral("0x") + QStringLiteral("%1").arg(v, digits, 16, QLatin1Char('0')).toUpper();
}

QString hex2(uint32_t v) { return hexN(v, 2); }
QString hex4(uint32_t v) { return hexN(v, 4); }
QString hex8(uint32_t v) { return hexN(v, 8); }

// Bytes from `from` to the end as "AABBCC"
QString hexBytes(const QByteArray &d, int from)
{
    return from >= d.size() ? QString() : QString::fromLatin1(d.mid(from).toHex()).toUpper();
}

QString ascii(const QByteArray &d, int from, int count)
{
    if (from >= d.size()) { return QString(); }
    count = std::min<int>(count, static_cast<int>(d.size()) - from);
    QString s;
    s.reserve(count);
    for (int i = from; i < from + count; ++i) {
        const auto c = static_cast<uint8_t>(d[i]);
        s += (c >= 0x20 && c < 0x7F) ? QChar(c) : QLatin1Char('.');
    }
    return s;
}

// Byte-order aware reader that returns 0 past the end of short frames
struct Rd
{
    const QByteArray &d;
    bool bigEndian;

    [[nodiscard]] uint8_t u8(int i) const { return i < d.size() ? static_cast<uint8_t>(d[i]) : 0; }
    [[nodiscard]] uint16_t u16(int i) const
    {
        return bigEndian ? static_cast<uint16_t>((u8(i) << 8) | u8(i + 1))
                         : static_cast<uint16_t>(u8(i) | (u8(i + 1) << 8));
    }
    [[nodiscard]] uint32_t u32(int i) const
    {
        return bigEndian
            ? (static_cast<uint32_t>(u8(i)) << 24) | (static_cast<uint32_t>(u8(i + 1)) << 16)
              | (static_cast<uint32_t>(u8(i + 2)) << 8) | u8(i + 3)
            : u8(i) | (static_cast<uint32_t>(u8(i + 1)) << 8)
              | (static_cast<uint32_t>(u8(i + 2)) << 16) | (static_cast<uint32_t>(u8(i + 3)) << 24);
    }
};

QString idFieldName(uint8_t type)
{
    switch (type) {
        case 0:  return QStringLiteral("absolute ODT");
        case 1:  return QStringLiteral("relative ODT + DAQ (byte)");
        case 2:  return QStringLiteral("relative ODT + DAQ (word)");
        default: return QStringLiteral("relative ODT + DAQ (word, aligned)");
    }
}

QString daqListMode(uint8_t mode)
{
    QStringList parts;
    if (mode & 0x01) { parts << QStringLiteral("SELECTED"); }
    if (mode & 0x02) { parts << QStringLiteral("DIRECTION=STIM"); }
    if (mode & 0x10) { parts << QStringLiteral("TIMESTAMP"); }
    if (mode & 0x20) { parts << QStringLiteral("PID_OFF"); }
    if (mode & 0x40) { parts << QStringLiteral("RUNNING"); }
    if (mode & 0x80) { parts << QStringLiteral("RESUME"); }
    return parts.isEmpty() ? QString() : QStringLiteral(" (%1)").arg(parts.join(QLatin1Char(',')));
}

QString startStopList(uint8_t mode)
{
    switch (mode) {
        case 0:  return QStringLiteral("stop");
        case 1:  return QStringLiteral("start");
        case 2:  return QStringLiteral("select");
        default: return hex2(mode);
    }
}

QString startStopSynch(uint8_t mode)
{
    switch (mode) {
        case 0:  return QStringLiteral("stop all");
        case 1:  return QStringLiteral("start selected");
        case 2:  return QStringLiteral("stop selected");
        case 3:  return QStringLiteral("prepare selected");
        default: return hex2(mode);
    }
}

// Names a command, resolving the sub-command byte where one exists
QString commandName(uint8_t cmd, const QByteArray &request)
{
    if (cmd == 0xC0 && request.size() > 1) { return XcpTables::level1(static_cast<uint8_t>(request[1])); }
    if (cmd == 0xF2 && request.size() > 1) { return XcpTables::transportLayer(static_cast<uint8_t>(request[1])); }
    const QString name = XcpTables::command(cmd);
    return name.isEmpty() ? QStringLiteral("CMD ") + hex2(cmd) : name;
}

QString describeTransportLayer(const QByteArray &d)
{
    const uint8_t sub = d.size() > 1 ? static_cast<uint8_t>(d[1]) : 0;
    // GET_SLAVE_ID carries the "XCP" magic and an identification mode
    if (sub == 0xFF && d.size() >= 6) {
        const auto mode = static_cast<uint8_t>(d[5]);
        return QStringLiteral("MAGIC = '%1'|MODE = %2").arg(ascii(d, 2, 3), hex2(mode))
             + (mode == 0 ? QStringLiteral(" (identify by echo)")
                : mode == 1 ? QStringLiteral(" (confirm by inverse echo)") : QString());
    }
    return d.size() > 2 ? QStringLiteral("PARAMS = ") + hexBytes(d, 2) : QString();
}

bool isGetSlaveId(const QByteArray &request)
{
    return request.size() > 1 && static_cast<uint8_t>(request[1]) == 0xFF;
}

} // namespace

QString XcpConfig::toString() const
{
    QString s = QStringLiteral("req 0x%1, rsp 0x%2").arg(QString::number(requestId, 16).toUpper(),
                                                        QString::number(responseId, 16).toUpper());
    if (broadcastId) { s += QStringLiteral(", bcast 0x") + QString::number(*broadcastId, 16).toUpper(); }
    if (extended) { s += QStringLiteral(" (29-bit)"); }
    return s;
}

XcpDecoder::XcpDecoder(const XcpConfig &config)
    : _config(config)
{
}

bool XcpDecoder::matches(const BusMessage &f) const
{
    if (f.busType() != BusType::CAN || f.isErrorFrame() || f.isRTR()) { return false; }
    if (f.getInterfaceId() != _config.interfaceId || f.isExtended() != _config.extended) { return false; }
    const uint32_t id = f.getId();
    return id == _config.requestId || id == _config.responseId
        || (_config.broadcastId && id == *_config.broadcastId);
}

bool XcpDecoder::decode(const BusMessage &f, XcpAnnotation &out)
{
    if (!matches(f)) { return false; }

    const bool fromMaster = f.getId() == _config.requestId
                         || (_config.broadcastId && f.getId() == *_config.broadcastId);
    const QByteArray d(reinterpret_cast<const char *>(f.getData()), f.getLength());
    out = fromMaster ? decodeMaster(f, d) : decodeSlave(f, d);
    // Which side sent the frame follows from the CAN ID alone
    out.fromMaster = fromMaster;
    return true;
}

DecodeStatus XcpDecoder::tryDecode(const BusMessage &frame, ProtocolMessage &outMsg)
{
    XcpAnnotation a;
    if (!decode(frame, a)) { return DecodeStatus::Ignored; }

    outMsg.protocol = QStringLiteral("XCP");
    outMsg.name = a.type;
    outMsg.description = a.comment;
    outMsg.type = a.kind;
    outMsg.id = static_cast<uint32_t>(a.group);
    outMsg.timestamp = static_cast<uint64_t>(frame.getFloatTimestamp() * 1000000.0);
    outMsg.payload = QByteArray(reinterpret_cast<const char *>(frame.getData()), frame.getLength());
    outMsg.rawFrames = {frame};
    return DecodeStatus::Completed;
}

void XcpDecoder::reset()
{
    resetSession();
}

// ---------------- master -> slave ----------------

XcpAnnotation XcpDecoder::decodeMaster(const BusMessage &f, const QByteArray &d)
{
    if (d.isEmpty()) { return {QStringLiteral("CTO (?)"), QStringLiteral("empty frame"), 0, true, MessageType::Request}; }

    const auto pid = static_cast<uint8_t>(d[0]);
    if (pid < XcpTables::command_pid_min) { return decodeDto(d, true); }

    QString name = XcpTables::command(pid);
    if (name.isEmpty()) { name = QStringLiteral("CMD ") + hex2(pid); }
    if (pid == 0xC0 && d.size() > 1) { name = XcpTables::level1(static_cast<uint8_t>(d[1])); }
    if (pid == 0xF2 && d.size() > 1) { name = XcpTables::transportLayer(static_cast<uint8_t>(d[1])); }

    const QString comment = describeCommand(pid, d);
    applyCommandState(pid, d);

    _pendingCmd = pid;
    _pendingData = d;
    _pendingTs = f.getFloatTimestamp();
    return {QStringLiteral("CTO (%1)").arg(name), comment, 0, true, MessageType::Request};
}

QString XcpDecoder::describeCommand(uint8_t pid, const QByteArray &d) const
{
    const Rd r{d, _bigEndian};
    switch (pid) {
        case 0xFF:
            return QStringLiteral("MODE = ") + hex2(r.u8(1))
                 + (r.u8(1) == 0 ? QStringLiteral(" (normal)") : r.u8(1) == 1 ? QStringLiteral(" (user defined)") : QString());
        case 0xFA:
            return QStringLiteral("TYPE = %1 (%2)").arg(hex2(r.u8(1)), XcpTables::getIdType(r.u8(1)));
        case 0xF9:
            return QStringLiteral("MODE = %1|SESSION_CONFIGURATION_ID = %2").arg(hex2(r.u8(1)), hex4(r.u16(2)));
        case 0xF8:
            return QStringLiteral("MODE = %1|RESOURCE = %2 (%3)").arg(hex2(r.u8(1)), hex2(r.u8(2)), XcpTables::resource(r.u8(2)));
        case 0xF7:
            return QStringLiteral("LENGTH = %1|KEY = %2").arg(hex2(r.u8(1)), hexBytes(d, 2));
        case 0xF6:
            return QStringLiteral("EXTENSION = %1|ADDRESS = %2").arg(hex2(r.u8(3)), hex8(r.u32(4)));
        case 0xF5:
            return QStringLiteral("NUMBER_OF_ELEMENTS = ") + hex2(r.u8(1));
        case 0xF4:
            return QStringLiteral("NUMBER_OF_ELEMENTS = %1|EXTENSION = %2|ADDRESS = %3").arg(hex2(r.u8(1)), hex2(r.u8(3)), hex8(r.u32(4)));
        case 0xF3:
            return QStringLiteral("BLOCK_SIZE = ") + hex8(r.u32(4));
        case 0xF2:
            return describeTransportLayer(d);
        case 0xF0:
        case 0xEF:
            return QStringLiteral("NUMBER_OF_ELEMENTS = %1|DATA = %2").arg(hex2(r.u8(1)), hexBytes(d, 2));
        // DOWNLOAD_MAX / PROGRAM_MAX carry no element count: the payload is the rest of the
        // CTO. Reading a count here would report the first data byte as one.
        case 0xEE:
        case 0xC9:
            return QStringLiteral("DATA = ") + hexBytes(d, 1);
        case 0xED:
            return QStringLiteral("NUMBER_OF_ELEMENTS = %1|EXTENSION = %2|ADDRESS = %3").arg(hex2(r.u8(1)), hex2(r.u8(3)), hex8(r.u32(4)));
        case 0xEC:
            return QStringLiteral("SHIFT = %1|AND_MASK = %2|XOR_MASK = %3").arg(hex2(r.u8(1)), hex4(r.u16(2)), hex4(r.u16(4)));
        case 0xEB:
            return QStringLiteral("MODE = %1|SEGMENT = %2|PAGE = %3").arg(hex2(r.u8(1)), hex2(r.u8(2)), hex2(r.u8(3)));
        case 0xEA:
            return QStringLiteral("MODE = %1|SEGMENT = %2").arg(hex2(r.u8(1)), hex2(r.u8(2)));
        case 0xE3:
        case 0xDF:
        case 0xD8:
            return QStringLiteral("DAQ_LIST_NUMBER = ") + hex4(r.u16(2));
        case 0xE2:
            return QStringLiteral("DAQ_LIST_NUMBER = %1|ODT_NUMBER = %2|ODT_ENTRY_NUMBER = %3").arg(hex4(r.u16(2)), hex2(r.u8(4)), hex2(r.u8(5)));
        case 0xE1:
            return QStringLiteral("BIT_OFFSET = %1|SIZE = %2|EXTENSION = %3|ADDRESS = %4").arg(hex2(r.u8(1)), hex2(r.u8(2)), hex2(r.u8(3)), hex8(r.u32(4)));
        case 0xE0:
            return QStringLiteral("MODE = %1%2|DAQ_LIST_NUMBER = %3|EVENT_CHANNEL_NUMBER = %4|PRESCALER = %5|PRIORITY = %6")
                .arg(hex2(r.u8(1)), daqListMode(r.u8(1)), hex4(r.u16(2)), hex4(r.u16(4)), hex2(r.u8(6)), hex2(r.u8(7)));
        case 0xDE:
            return QStringLiteral("MODE = %1 (%2)|DAQ_LIST_NUMBER = %3").arg(hex2(r.u8(1)), startStopList(r.u8(1)), hex4(r.u16(2)));
        case 0xDD:
            return QStringLiteral("MODE = %1 (%2)").arg(hex2(r.u8(1)), startStopSynch(r.u8(1)));
        case 0xD7:
            return QStringLiteral("EVENT_CHANNEL_NUMBER = ") + hex4(r.u16(2));
        case 0xD5:
            return QStringLiteral("DAQ_COUNT = ") + hex4(r.u16(2));
        case 0xD4:
            return QStringLiteral("DAQ_LIST_NUMBER = %1|ODT_COUNT = %2").arg(hex4(r.u16(2)), hex2(r.u8(4)));
        case 0xD3:
            return QStringLiteral("DAQ_LIST_NUMBER = %1|ODT_NUMBER = %2|ODT_ENTRIES_COUNT = %3").arg(hex4(r.u16(2)), hex2(r.u8(4)), hex2(r.u8(5)));
        case 0xD1:
            return QStringLiteral("MODE = %1|CLEAR_RANGE = %2").arg(hex2(r.u8(1)), hex8(r.u32(4)));
        case 0xD0:
        case 0xCA:
            return QStringLiteral("NUMBER_OF_ELEMENTS = %1|DATA = %2").arg(hex2(r.u8(1)), hexBytes(d, 2));
        case 0xC0:
            return d.size() > 2 ? QStringLiteral("PARAMS = ") + hexBytes(d, 2) : QString();
        default:
            // no-parameter commands: DISCONNECT, GET_STATUS, SYNCH, GET_COMM_MODE_INFO,
            // GET_DAQ_CLOCK, READ_DAQ, GET_DAQ_PROCESSOR_INFO, GET_DAQ_RESOLUTION_INFO, FREE_DAQ...
            return d.size() > 1 ? QStringLiteral("PARAMS = ") + hexBytes(d, 1) : QString();
    }
}

void XcpDecoder::applyCommandState(uint8_t pid, const QByteArray &d)
{
    const Rd r{d, _bigEndian};
    switch (pid) {
        case 0xFF: // CONNECT: a new session invalidates everything learned so far
            resetSession();
            break;
        case 0xD6: // FREE_DAQ releases the dynamic DAQ configuration
            _daqCount = 0;
            _odtCounts.clear();
            break;
        case 0xD5: // ALLOC_DAQ
            _daqCount = r.u16(2);
            _odtCounts.clear();
            break;
        case 0xD4: // ALLOC_ODT
            _odtCounts[r.u16(2)] = r.u8(4);
            break;
        default:
            break;
    }
}

// ---------------- slave -> master ----------------

XcpAnnotation XcpDecoder::decodeSlave(const BusMessage &f, const QByteArray &d)
{
    if (d.isEmpty()) { return {QStringLiteral("CTO (?)"), QStringLiteral("empty frame"), 0, false, MessageType::Unknown}; }

    const auto pid = static_cast<uint8_t>(d[0]);
    switch (pid) {
        case XcpTables::pid_res:
        {
            std::optional<uint8_t> cmd;
            QByteArray request;
            takePending(f.getFloatTimestamp(), cmd, request);
            return {d.size() > 1 ? QStringLiteral("CTO (OK + INFO)") : QStringLiteral("CTO (OK)"),
                    describeResponse(cmd, request, d), 0, false, MessageType::PositiveResponse};
        }
        case XcpTables::pid_err:
        {
            std::optional<uint8_t> cmd;
            QByteArray request;
            takePending(f.getFloatTimestamp(), cmd, request);
            const QString name = d.size() > 1 ? XcpTables::error(static_cast<uint8_t>(d[1])) : QStringLiteral("ERR (no code)");
            QString comment = d.size() > 1 ? QStringLiteral("%1 (%2)").arg(name, hex2(static_cast<uint8_t>(d[1]))) : name;
            if (cmd) { comment += QStringLiteral(" ← ") + commandName(*cmd, request); }
            return {QStringLiteral("CTO (ERR)"), comment, 0, false, MessageType::NegativeResponse};
        }
        // EV and SERV are asynchronous: they must not consume the pending command
        case XcpTables::pid_ev:
        {
            QString comment = QStringLiteral("EV (no code)");
            if (d.size() > 1) {
                comment = QStringLiteral("%1 (%2)").arg(XcpTables::event(static_cast<uint8_t>(d[1])), hex2(static_cast<uint8_t>(d[1])));
                if (d.size() > 2) { comment += QStringLiteral("|DATA = ") + hexBytes(d, 2); }
            }
            return {QStringLiteral("CTO (EV)"), comment, 0, false, MessageType::Unknown};
        }
        case XcpTables::pid_serv:
        {
            QString comment = QStringLiteral("SERV (no code)");
            if (d.size() > 1) {
                const auto code = static_cast<uint8_t>(d[1]);
                comment = QStringLiteral("%1 (%2)").arg(XcpTables::service(code), hex2(code));
                if (code == 0x01) { comment += QStringLiteral("|TEXT = '%1'").arg(ascii(d, 2, static_cast<int>(d.size()) - 2)); }
            }
            return {QStringLiteral("CTO (SERV)"), comment, 0, false, MessageType::Unknown};
        }
        default:
            return decodeDto(d, false);
    }
}

// Consumes the command this reply answers, together with the bytes sent with it:
// several commands are only distinguishable by their sub-command byte, and the
// response carries no copy of it.
bool XcpDecoder::takePending(double ts, std::optional<uint8_t> &cmd, QByteArray &request)
{
    cmd.reset();
    request.clear();
    if (!_pendingCmd) { return false; }
    // A stale pending command means its response was missed (frame loss, or the trace
    // started mid-command); don't pair it with an unrelated later reply
    if (ts - _pendingTs > max_pending_age_s) {
        _pendingCmd.reset();
        _pendingData.clear();
        return false;
    }
    cmd = _pendingCmd;
    request = _pendingData;
    _pendingCmd.reset();
    _pendingData.clear();
    return true;
}

QString XcpDecoder::describeResponse(std::optional<uint8_t> cmd, const QByteArray &request, const QByteArray &d)
{
    if (!cmd) { return d.size() > 1 ? QStringLiteral("DATA = ") + hexBytes(d, 1) : QString(); }

    // CONNECT's response defines the byte order of every multi-byte field that
    // follows, so it is applied before the remaining fields are read
    if (*cmd == 0xFF && d.size() >= 8) {
        const auto commModeBasic = static_cast<uint8_t>(d[2]);
        _bigEndian = (commModeBasic & 0x01) != 0;
        const Rd rc{d, _bigEndian};
        return QStringLiteral("CONNECT: RESOURCE = %1 (%2)|COMM_MODE_BASIC = %3 (%4, AG=%5)|MAX_CTO = %6|MAX_DTO = %7"
                              "|PROTOCOL_LAYER = %8|TRANSPORT_LAYER = %9")
            .arg(hex2(rc.u8(1)), XcpTables::resource(rc.u8(1)), hex2(commModeBasic),
                 _bigEndian ? QStringLiteral("Motorola") : QStringLiteral("Intel"))
            .arg(1 << ((commModeBasic >> 1) & 0x03))
            .arg(int(rc.u8(3)))
            .arg(int(rc.u16(4)))
            .arg(hex2(rc.u8(6)), hex2(rc.u8(7)));
    }

    const Rd r{d, _bigEndian};
    switch (*cmd) {
        case 0xFD: // GET_STATUS
            if (d.size() >= 6) {
                return QStringLiteral("GET_STATUS: SESSION_STATUS = %1 (%2)|RESOURCE_PROTECTION = %3 (%4)|SESSION_CONFIGURATION_ID = %5")
                    .arg(hex2(r.u8(1)), XcpTables::sessionStatus(r.u8(1)), hex2(r.u8(2)), XcpTables::resource(r.u8(2)), hex4(r.u16(4)));
            }
            break;
        case 0xFB: // GET_COMM_MODE_INFO
            if (d.size() >= 8) {
                return QStringLiteral("GET_COMM_MODE_INFO: COMM_MODE_OPTIONAL = %1|MAX_BS = %2|MIN_ST = %3|QUEUE_SIZE = %4|DRIVER_VERSION = %5")
                    .arg(hex2(r.u8(2))).arg(int(r.u8(4))).arg(int(r.u8(5))).arg(int(r.u8(6))).arg(hex2(r.u8(7)));
            }
            break;
        case 0xFA: // GET_ID
            if (d.size() >= 8) { return QStringLiteral("GET_ID: MODE = %1|LENGTH = %2").arg(hex2(r.u8(1))).arg(uint(r.u32(4))); }
            break;
        case 0xF8: // GET_SEED
            if (d.size() >= 2) { return QStringLiteral("GET_SEED: LENGTH = %1|SEED = %2").arg(int(r.u8(1))).arg(hexBytes(d, 2)); }
            break;
        case 0xF7: // UNLOCK
            if (d.size() >= 2) {
                return QStringLiteral("UNLOCK: PROTECTION_STATUS = %1 (still protected: %2)").arg(hex2(r.u8(1)), XcpTables::resource(r.u8(1)));
            }
            break;
        case 0xDA: // GET_DAQ_PROCESSOR_INFO
            if (d.size() >= 8) {
                _identificationFieldType = static_cast<uint8_t>((r.u8(7) >> 6) & 0x03);
                _daqKeyKnown = true;
                return QStringLiteral("GET_DAQ_PROCESSOR_INFO: DAQ_PROPERTIES = %1|MAX_DAQ = %2|MAX_EVENT_CHANNEL = %3|MIN_DAQ = %4"
                                      "|DAQ_KEY_BYTE = %5 (id field: %6)")
                    .arg(hex2(r.u8(1))).arg(int(r.u16(2))).arg(int(r.u16(4))).arg(int(r.u8(6))).arg(hex2(r.u8(7)), idFieldName(_identificationFieldType));
            }
            break;
        case 0xD9: // GET_DAQ_RESOLUTION_INFO
            if (d.size() >= 8) {
                return QStringLiteral("GET_DAQ_RESOLUTION_INFO: GRANULARITY_ODT_ENTRY_DAQ = %1|MAX_ODT_ENTRY_SIZE_DAQ = %2"
                                      "|GRANULARITY_ODT_ENTRY_STIM = %3|MAX_ODT_ENTRY_SIZE_STIM = %4|TIMESTAMP_MODE = %5|TIMESTAMP_TICKS = %6")
                    .arg(int(r.u8(1))).arg(int(r.u8(2))).arg(int(r.u8(3))).arg(int(r.u8(4))).arg(hex2(r.u8(5))).arg(int(r.u16(6)));
            }
            break;
        case 0xDC: // GET_DAQ_CLOCK
            if (d.size() >= 8) { return QStringLiteral("GET_DAQ_CLOCK: TIMESTAMP = ") + hex8(r.u32(4)); }
            break;
        case 0xDF: // GET_DAQ_LIST_MODE
            if (d.size() >= 8) {
                return QStringLiteral("GET_DAQ_LIST_MODE: MODE = %1%2|EVENT_CHANNEL_NUMBER = %3|PRESCALER = %4|PRIORITY = %5")
                    .arg(hex2(r.u8(1)), daqListMode(r.u8(1)), hex4(r.u16(4)), hex2(r.u8(6)), hex2(r.u8(7)));
            }
            break;
        case 0xF2: // TRANSPORT_LAYER_CMD / GET_SLAVE_ID
            if (d.size() >= 8 && isGetSlaveId(request)) {
                return QStringLiteral("GET_SLAVE_ID: MAGIC = '%1'|CAN_ID = %2").arg(ascii(d, 1, 3), hex8(r.u32(4)));
            }
            break;
        default:
            break;
    }

    const QString name = commandName(*cmd, request);
    return d.size() > 1 ? QStringLiteral("%1: DATA = %2").arg(name, hexBytes(d, 1)) : name;
}

// ---------------- DAQ / STIM data objects ----------------

XcpAnnotation XcpDecoder::decodeDto(const QByteArray &d, bool stim)
{
    const auto pid = static_cast<uint8_t>(d[0]);
    const QString kind = stim ? QStringLiteral("STIM-DTO") : QStringLiteral("DAQ-DTO");
    const QString comment = QStringLiteral("Data length: %1").arg(d.size());
    // Every ODT of every DAQ list shares the response CAN ID, so the PID is what makes
    // them distinct rows in an aggregated view. Group 0 is reserved for CTO traffic.
    const int group = pid + 1;
    const MessageType msgType = stim ? MessageType::Request : MessageType::Unknown;

    // The identification field layout decides how DAQ/ODT are encoded in the header
    switch (_identificationFieldType) {
        case 0: // absolute ODT number, 1-byte header
        {
            int daq = -1;
            int odt = -1;
            if (resolveAbsoluteOdt(pid, daq, odt)) {
                return {QStringLiteral("%1 (DAQ #%2|ODT #%3)").arg(kind).arg(daq).arg(odt), comment, group, stim, msgType};
            }
            break;
        }
        case 1: // relative ODT number + absolute DAQ list number (byte)
            if (d.size() >= 2) {
                return {QStringLiteral("%1 (DAQ #%2|ODT #%3)").arg(kind).arg(int(static_cast<uint8_t>(d[1]))).arg(int(pid)), comment, group, stim, msgType};
            }
            break;
        case 2: // relative ODT number + absolute DAQ list number (word): [PID][DAQ word]
            if (d.size() >= 3) {
                return {QStringLiteral("%1 (DAQ #%2|ODT #%3)").arg(kind).arg(int(Rd{d, _bigEndian}.u16(1))).arg(int(pid)), comment, group, stim, msgType};
            }
            break;
        default: // 3: the same, word-aligned: [PID][FILL][DAQ word]
            if (d.size() >= 4) {
                return {QStringLiteral("%1 (DAQ #%2|ODT #%3)").arg(kind).arg(int(Rd{d, _bigEndian}.u16(2))).arg(int(pid)), comment, group, stim, msgType};
            }
            break;
    }

    // Not enough context: say so rather than inventing DAQ/ODT numbers. The PID still
    // separates the rows, so the view stays usable even mid-session.
    const QString why = _daqCount == 0 ? QStringLiteral("DAQ allocation not seen in capture")
                                       : QStringLiteral("PID outside the allocated ODT range");
    return {QStringLiteral("%1 (PID = %2)").arg(kind, hex2(pid)), comment + QLatin1Char('|') + why, group, stim, msgType};
}

// Absolute ODT numbers run consecutively across DAQ lists, so the ALLOC_ODT counts
// collected during configuration turn a PID back into DAQ/ODT
bool XcpDecoder::resolveAbsoluteOdt(uint8_t pid, int &daq, int &odt) const
{
    int firstAbsolute = 0;
    for (int list = 0; list < _daqCount; ++list) {
        auto it = _odtCounts.constFind(list);
        if (it == _odtCounts.constEnd()) { break; }
        if (pid < firstAbsolute + it.value()) {
            daq = list;
            odt = pid - firstAbsolute;
            return true;
        }
        firstAbsolute += it.value();
    }
    daq = -1;
    odt = -1;
    return false;
}

void XcpDecoder::resetSession()
{
    _bigEndian = false;
    _identificationFieldType = 0;
    _daqKeyKnown = false;
    _daqCount = 0;
    _odtCounts.clear();
    _pendingCmd.reset();
    _pendingData.clear();
}

QString XcpDecoder::describe() const
{
    QString s = _config.toString();
    if (_daqCount > 0) {
        int odts = 0;
        for (int n : _odtCounts) { odts += n; }
        s += QStringLiteral(" — %1 DAQ list(s), %2 ODT(s)").arg(_daqCount).arg(odts);
    }
    if (_daqKeyKnown) { s += QStringLiteral(", id field: ") + idFieldName(_identificationFieldType); }
    if (_bigEndian) { s += QStringLiteral(", Motorola byte order"); }
    return s;
}
