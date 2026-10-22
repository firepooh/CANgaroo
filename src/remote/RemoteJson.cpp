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

#include "RemoteJson.h"

#include <algorithm>
#include <array>

#include <QByteArray>
#include <QJsonValue>

#include "remote/RemoteError.h"
#include "remote/RemoteFrameLog.h"

namespace
{
    [[noreturn]] void invalid(const QString &key, const QString &what)
    {
        throw RemoteError(RemoteError::InvalidParams, QString("'%1' %2").arg(key, what));
    }

    constexpr std::array<int, 16> k_fdLengths = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 12, 16, 20, 24, 32, 48, 64 };

    bool isValidFdLength(int len)
    {
        return std::find(k_fdLengths.begin(), k_fdLengths.end(), len) != k_fdLengths.end();
    }
}

uint32_t RemoteJson::toUInt(const QJsonObject &obj, const QString &key, uint32_t max)
{
    if (!obj.contains(key)) { invalid(key, "is required"); }

    const QJsonValue v = obj.value(key);
    qint64 value = -1;
    if (v.isDouble())
    {
        const double d = v.toDouble();
        if (d != static_cast<double>(static_cast<qint64>(d))) { invalid(key, "must be an integer"); }
        value = static_cast<qint64>(d);
    }
    else if (v.isString())
    {
        bool ok = false;
        value = v.toString().trimmed().toLongLong(&ok, 0);   // base 0: accepts "0x123"
        if (!ok) { invalid(key, "must be an integer or a hex string like \"0x123\""); }
    }
    else
    {
        invalid(key, "must be an integer");
    }

    if (value < 0 || value > static_cast<qint64>(max))
    {
        invalid(key, QString("must be between 0 and %1").arg(max));
    }
    return static_cast<uint32_t>(value);
}

uint32_t RemoteJson::toUInt(const QJsonObject &obj, const QString &key, uint32_t max, uint32_t def)
{
    return obj.contains(key) ? toUInt(obj, key, max) : def;
}

bool RemoteJson::toBool(const QJsonObject &obj, const QString &key)
{
    const QJsonValue v = obj.value(key);
    if (!v.isBool()) { invalid(key, "must be true or false"); }
    return v.toBool();
}

bool RemoteJson::toBool(const QJsonObject &obj, const QString &key, bool def)
{
    return obj.contains(key) ? toBool(obj, key) : def;
}

QString RemoteJson::toString(const QJsonObject &obj, const QString &key)
{
    const QJsonValue v = obj.value(key);
    if (!v.isString() || v.toString().isEmpty()) { invalid(key, "must be a non-empty string"); }
    return v.toString();
}

BusMessage RemoteJson::frameFromJson(const QJsonObject &obj)
{
    // Frames built from client parameters are meant to be sent.
    BusMessage msg;
    msg.setBusType(BusType::CAN);
    msg.setRX(false);

    const bool extended = toBool(obj, "extended", false);
    const uint32_t id = toUInt(obj, "id", extended ? 0x1FFFFFFF : 0x7FF);
    const bool fd = toBool(obj, "fd", false);
    const bool brs = toBool(obj, "brs", false);
    const bool rtr = toBool(obj, "rtr", false);

    if (brs && !fd) { invalid("brs", "needs fd=true"); }
    if (rtr && fd) { invalid("rtr", "is not allowed for CAN FD frames"); }

    QByteArray data;
    if (obj.contains("data"))
    {
        const QJsonValue v = obj.value("data");
        if (!v.isString()) { invalid("data", "must be a hex string like \"11 22 AA\""); }
        QString hex = v.toString();
        hex.remove(' ').remove(':').remove('-');
        if (hex.startsWith("0x", Qt::CaseInsensitive)) { hex = hex.mid(2); }
        if (hex.size() % 2 != 0) { invalid("data", "has an odd number of hex digits"); }
        data = QByteArray::fromHex(hex.toLatin1());
        if (data.size() * 2 != hex.size()) { invalid("data", "contains non-hex characters"); }
    }

    const int maxLen = fd ? 64 : 8;
    if (data.size() > maxLen) { invalid("data", QString("is longer than %1 bytes").arg(maxLen)); }
    if (fd && !isValidFdLength(static_cast<int>(data.size())))
    {
        invalid("data", "length must be 0-8, 12, 16, 20, 24, 32, 48 or 64 bytes for CAN FD");
    }

    msg.setExtended(extended);
    msg.setId(id);
    msg.setFD(fd);
    msg.setBRS(brs);
    msg.setRTR(rtr);

    if (rtr)
    {
        // Remote frames carry no data; "len" gives the requested DLC.
        msg.setLength(static_cast<uint8_t>(toUInt(obj, "len", 8, 0)));
    }
    else
    {
        msg.setLength(static_cast<uint8_t>(data.size()));
        for (int i = 0; i < data.size(); ++i)
        {
            msg.setByte(static_cast<uint8_t>(i), static_cast<uint8_t>(data[i]));
        }
    }

    if (obj.contains("interface"))
    {
        msg.setInterfaceId(static_cast<BusInterfaceId>(toUInt(obj, "interface", 0xFFFF)));
    }
    return msg;
}

QString RemoteJson::dataToHex(const BusMessage &msg)
{
    if (msg.isRTR()) { return {}; }

    QString hex;
    hex.reserve(msg.getLength() * 3);
    for (uint8_t i = 0; i < msg.getLength(); ++i)
    {
        if (i) { hex += ' '; }
        hex += QString("%1").arg(msg.getByte(i), 2, 16, QChar('0')).toUpper();
    }
    return hex;
}

QJsonObject RemoteJson::messageToJson(const BusMessage &msg)
{
    QJsonObject o;
    o["t"]         = msg.getFloatTimestamp();
    o["t_us"]      = static_cast<qint64>(msg.getTimestamp_us());
    o["interface"] = msg.getInterfaceId();
    o["dir"]       = msg.isRX() ? "rx" : "tx";
    o["id"]        = static_cast<qint64>(msg.getId());
    o["id_hex"]    = QString("0x%1").arg(msg.getId(), msg.isExtended() ? 8 : 3, 16, QChar('0')).toUpper().replace("0X", "0x");
    o["extended"]  = msg.isExtended();
    o["fd"]        = msg.isFD();
    o["brs"]       = msg.isBRS();
    o["rtr"]       = msg.isRTR();
    o["len"]       = msg.getLength();
    o["data"]      = dataToHex(msg);
    if (msg.isErrorFrame())
    {
        o["error"] = msg.getErrorFlagsString();
    }
    return o;
}

QJsonObject RemoteJson::frameToJson(const RemoteFrame &frame)
{
    QJsonObject o = messageToJson(frame.msg);
    o["seq"] = static_cast<qint64>(frame.seq);
    return o;
}
