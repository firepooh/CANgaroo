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

#include <atomic>
#include <cstdint>
#include <deque>
#include <optional>

#include <QJsonObject>
#include <QList>
#include <QMutex>
#include <QObject>

#include "core/BusMessage.h"

// One frame as seen by remote clients: the BusMessage plus a sequence number
// that keeps increasing for the whole session (it is not reset by clear()).
struct RemoteFrame
{
    uint64_t seq = 0;
    BusMessage msg;
};

// Frame selection shared by get_frames, wait_for_frame and send_and_wait.
// Every field that is not set matches any frame.
struct RemoteFrameFilter
{
    std::optional<BusInterfaceId> interfaceId;
    std::optional<uint32_t> id;
    uint32_t mask = 0x1FFFFFFF;
    std::optional<bool> extended;
    std::optional<bool> rx;           // true: received frames, false: own TX echo
    bool includeErrorFrames = false;

    [[nodiscard]] bool matches(const BusMessage &msg) const;

    // Reads interface / id / mask / extended / dir / errors from a JSON object.
    // Throws RemoteError on malformed values.
    [[nodiscard]] static RemoteFrameFilter fromJson(const QJsonObject &obj);
};

// Thread-safe ring buffer of every frame entering the BusTrace. It is
// independent of BusTrace's own size limit and index shifting, so remote
// clients can read with a stable cursor (the sequence number).
class RemoteFrameLog : public QObject
{
    Q_OBJECT

public:
    explicit RemoteFrameLog(int capacity, QObject *parent = nullptr);

    // Called from the bus listener threads (RemoteApi feeds it from BusTrace).
    void append(const BusMessage &msg);

    // Sequence number the next frame will get: "from now on" cursor.
    [[nodiscard]] uint64_t nextSeq() const;
    [[nodiscard]] uint64_t oldestSeq() const;
    [[nodiscard]] int capacity() const noexcept { return _capacity; }

    // Returns up to `limit` frames with seq >= since that match the filter.
    // *next is the cursor to continue from; *dropped is true when frames
    // after `since` were already discarded from the ring buffer.
    QList<RemoteFrame> read(uint64_t since, int limit, const RemoteFrameFilter &filter,
                            uint64_t *next, bool *dropped = nullptr) const;

    // Returns the newest `limit` frames that match the filter, oldest first.
    QList<RemoteFrame> readLatest(int limit, const RemoteFrameFilter &filter) const;

    void clear();

signals:
    // Emitted on the owner's thread, coalesced: one signal may cover many frames.
    void framesAppended();

private:
    mutable QMutex _mutex;
    std::deque<RemoteFrame> _frames;
    uint64_t _nextSeq = 1;
    int _capacity;
    std::atomic<bool> _notifyPending{ false };
};
