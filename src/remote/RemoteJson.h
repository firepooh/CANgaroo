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

#pragma once

#include <cstdint>

#include <QJsonObject>
#include <QString>

#include "core/BusMessage.h"

struct RemoteFrame;

// JSON helpers for the remote control protocol. All parse errors throw
// RemoteError(InvalidParams) with the offending key in the message.
namespace RemoteJson
{
    // Integer or "0x..." / decimal string, checked against [0, max].
    uint32_t toUInt(const QJsonObject &obj, const QString &key, uint32_t max);
    uint32_t toUInt(const QJsonObject &obj, const QString &key, uint32_t max, uint32_t def);
    bool toBool(const QJsonObject &obj, const QString &key);
    bool toBool(const QJsonObject &obj, const QString &key, bool def);
    QString toString(const QJsonObject &obj, const QString &key);

    // {"interface", "id", "extended", "fd", "brs", "rtr", "data": "11 22 AA"}
    BusMessage frameFromJson(const QJsonObject &obj);

    // {"seq", "t", "t_us", "interface", "dir", "id", "id_hex", "extended", "fd", "brs",
    //  "rtr", "error", "len", "data"} -- data as upper-case hex bytes separated by spaces.
    QJsonObject frameToJson(const RemoteFrame &frame);
    QJsonObject messageToJson(const BusMessage &msg);

    QString dataToHex(const BusMessage &msg);
}
