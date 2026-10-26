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

// Remote frame log: the ring buffer behind get_frames / wait_for_frame.
//
// Expected values follow the remote API contract (get_frames in mcp/README.md):
// sequence numbers start at 1 and keep counting past
// evictions, read() returns frames with seq >= since oldest first, and
// "latest" returns the newest N matching frames, still oldest first.

#include <QtTest>

#include "core/BusMessage.h"
#include "remote/RemoteFrameLog.h"

namespace
{

BusMessage frame(uint32_t id, BusInterfaceId itf, bool rx = true)
{
    BusMessage msg(id);
    msg.setInterfaceId(itf);
    msg.setRX(rx);
    msg.setLength(1);
    msg.setByte(0, static_cast<uint8_t>(id));
    return msg;
}

QList<uint64_t> seqs(const QList<RemoteFrame> &frames)
{
    QList<uint64_t> out;
    for (const RemoteFrame &f : frames) { out.append(f.seq); }
    return out;
}

} // namespace

class RemoteFrameLogTest : public QObject
{
    Q_OBJECT

private slots:
    void latestReturnsNewestOldestFirst()
    {
        RemoteFrameLog log(100);
        for (uint32_t id = 0x100; id < 0x105; ++id) { log.append(frame(id, 0)); }

        const QList<RemoteFrame> got = log.readLatest(3, RemoteFrameFilter());
        QCOMPARE(seqs(got), (QList<uint64_t>{ 3, 4, 5 }));
        QCOMPARE(got.first().msg.getId(), 0x102u);
        QCOMPARE(got.last().msg.getId(), 0x104u);
    }

    void latestAppliesFilterBeforeCounting()
    {
        // Frames alternate between interfaces 0 and 1; asking for the two newest
        // on interface 0 must skip the interface 1 frames in between.
        RemoteFrameLog log(100);
        for (uint32_t i = 0; i < 6; ++i) { log.append(frame(0x200 + i, i % 2)); }

        RemoteFrameFilter onItf0;
        onItf0.interfaceId = 0;
        QCOMPARE(seqs(log.readLatest(2, onItf0)), (QList<uint64_t>{ 3, 5 }));

        RemoteFrameFilter byId;
        byId.id = 0x201;
        QCOMPARE(seqs(log.readLatest(10, byId)), (QList<uint64_t>{ 2 }));

        RemoteFrameFilter txOnly;
        txOnly.rx = false;
        QVERIFY(log.readLatest(10, txOnly).isEmpty());
    }

    void latestWithLimitAboveCountReturnsAll()
    {
        RemoteFrameLog log(100);
        log.append(frame(0x1, 0));
        log.append(frame(0x2, 0));
        QCOMPARE(seqs(log.readLatest(1000, RemoteFrameFilter())), (QList<uint64_t>{ 1, 2 }));
    }

    void latestOnEmptyLog()
    {
        RemoteFrameLog log(10);
        QVERIFY(log.readLatest(5, RemoteFrameFilter()).isEmpty());
        QCOMPARE(log.nextSeq(), 1u);
    }

    void latestAfterEviction()
    {
        // Capacity 4, 10 frames: seq 7..10 are kept, numbering does not restart.
        RemoteFrameLog log(4);
        for (uint32_t i = 0; i < 10; ++i) { log.append(frame(0x300 + i, 0)); }

        QCOMPARE(log.oldestSeq(), 7u);
        QCOMPARE(log.nextSeq(), 11u);
        QCOMPARE(seqs(log.readLatest(10, RemoteFrameFilter())), (QList<uint64_t>{ 7, 8, 9, 10 }));

        uint64_t next = 0;
        bool dropped = false;
        const QList<RemoteFrame> got = log.read(2, 10, RemoteFrameFilter(), &next, &dropped);
        QVERIFY(dropped);
        QCOMPARE(seqs(got), (QList<uint64_t>{ 7, 8, 9, 10 }));
        QCOMPARE(next, 11u);
    }

    void errorFramesOnlyWhenAsked()
    {
        RemoteFrameLog log(10);
        log.append(frame(0x10, 0));
        BusMessage err = frame(0x20, 0);
        err.setErrorFrame(true);
        log.append(err);

        QCOMPARE(seqs(log.readLatest(10, RemoteFrameFilter())), (QList<uint64_t>{ 1 }));
        RemoteFrameFilter withErrors;
        withErrors.includeErrorFrames = true;
        QCOMPARE(seqs(log.readLatest(10, withErrors)), (QList<uint64_t>{ 1, 2 }));
    }
};

QTEST_GUILESS_MAIN(RemoteFrameLogTest)

#include "RemoteFrameLogTest.moc"
