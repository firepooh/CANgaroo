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

#include "RemoteFrameLog.h"

#include <algorithm>

#include <QMutexLocker>

#include "remote/RemoteError.h"
#include "remote/RemoteJson.h"

bool RemoteFrameFilter::matches(const BusMessage &msg) const
{
    if (msg.busType() != BusType::CAN) { return false; }
    if (msg.isErrorFrame() && !includeErrorFrames) { return false; }
    if (interfaceId && msg.getInterfaceId() != *interfaceId) { return false; }
    if (rx && msg.isRX() != *rx) { return false; }
    if (extended && msg.isExtended() != *extended) { return false; }
    if (id && ((msg.getId() & mask) != (*id & mask))) { return false; }
    return true;
}

RemoteFrameFilter RemoteFrameFilter::fromJson(const QJsonObject &obj)
{
    RemoteFrameFilter f;

    if (obj.contains("interface"))
    {
        f.interfaceId = static_cast<BusInterfaceId>(RemoteJson::toUInt(obj, "interface", 0xFFFF));
    }
    if (obj.contains("id"))
    {
        f.id = RemoteJson::toUInt(obj, "id", 0x1FFFFFFF);
    }
    if (obj.contains("mask"))
    {
        f.mask = RemoteJson::toUInt(obj, "mask", 0x1FFFFFFF);
    }
    if (obj.contains("extended"))
    {
        f.extended = RemoteJson::toBool(obj, "extended");
    }
    if (obj.contains("dir"))
    {
        const QString dir = obj.value("dir").toString();
        if (dir == "rx") { f.rx = true; }
        else if (dir == "tx") { f.rx = false; }
        else if (dir != "any")
        {
            throw RemoteError(RemoteError::InvalidParams, "dir must be 'rx', 'tx' or 'any'");
        }
    }
    if (obj.contains("errors"))
    {
        f.includeErrorFrames = RemoteJson::toBool(obj, "errors");
    }
    return f;
}

RemoteFrameLog::RemoteFrameLog(int capacity, QObject *parent)
    : QObject(parent),
      _capacity(capacity)
{
}

void RemoteFrameLog::append(const BusMessage &msg)
{
    {
        QMutexLocker locker(&_mutex);
        _frames.push_back(RemoteFrame{ _nextSeq++, msg });
        while (static_cast<int>(_frames.size()) > _capacity)
        {
            _frames.pop_front();
        }
    }

    if (!_notifyPending.exchange(true))
    {
        QMetaObject::invokeMethod(this, [this]()
        {
            _notifyPending = false;
            emit framesAppended();
        }, Qt::QueuedConnection);
    }
}

uint64_t RemoteFrameLog::nextSeq() const
{
    QMutexLocker locker(&_mutex);
    return _nextSeq;
}

uint64_t RemoteFrameLog::oldestSeq() const
{
    QMutexLocker locker(&_mutex);
    return _frames.empty() ? _nextSeq : _frames.front().seq;
}

QList<RemoteFrame> RemoteFrameLog::read(uint64_t since, int limit, const RemoteFrameFilter &filter,
                                        uint64_t *next, bool *dropped) const
{
    QList<RemoteFrame> result;
    QMutexLocker locker(&_mutex);

    const uint64_t oldest = _frames.empty() ? _nextSeq : _frames.front().seq;
    if (dropped) { *dropped = since < oldest; }
    if (since < oldest) { since = oldest; }

    // Sequence numbers are contiguous inside the buffer.
    size_t pos = static_cast<size_t>(since - oldest);
    uint64_t cursor = since;
    for (; pos < _frames.size(); ++pos)
    {
        const RemoteFrame &frame = _frames[pos];
        cursor = frame.seq + 1;
        if (filter.matches(frame.msg))
        {
            result.append(frame);
            if (result.size() >= limit) { break; }
        }
    }

    if (next) { *next = std::max(cursor, since); }
    return result;
}

QList<RemoteFrame> RemoteFrameLog::readLatest(int limit, const RemoteFrameFilter &filter) const
{
    QList<RemoteFrame> result;
    QMutexLocker locker(&_mutex);

    for (auto it = _frames.rbegin(); it != _frames.rend() && result.size() < limit; ++it)
    {
        if (filter.matches(it->msg)) { result.append(*it); }
    }
    std::reverse(result.begin(), result.end());
    return result;
}

void RemoteFrameLog::clear()
{
    QMutexLocker locker(&_mutex);
    _frames.clear();
}
