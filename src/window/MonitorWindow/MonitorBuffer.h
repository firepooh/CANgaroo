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

#include <deque>
#include <memory>
#include <vector>

#include <QHash>
#include <QSet>
#include <QObject>
#include <QTimer>

#include "core/BusMessage.h"

class Backend;

// Frame store behind the message monitor. Not a Qt item model: MonitorTable
// reads it directly and repaints once per batch. Batches are taken every
// refresh interval, so the cost of the view does not grow with the frame rate.
//
// Fixed mode keeps one entry per frame ID (per network and direction), sorted
// by ID, with receive and data-change counters. Scroll mode keeps the last
// scroll_max_entries frames in arrival order.
//
// The view shows each entry as one message line plus, when the entry is
// expanded, its data lines (data_line_bytes each, only for frames longer than
// 8 bytes) and one line per DBC/LDF signal.
class MonitorBuffer : public QObject
{
    Q_OBJECT

public:
    enum Column
    {
        col_count,
        col_time,
        col_tx,
        col_er,
        col_description,
        col_arbid,
        col_len,
        col_data,
        col_network,
        col_node,
        col_changecount,
        col_timestamp,
        col_memo,
        column_count
    };

    enum class Mode
    {
        Fixed,
        Scroll
    };

    static constexpr int data_line_min_length = 8;
    static constexpr int data_line_bytes = 16;
    static constexpr int scroll_max_entries = 10000;
    static constexpr int refresh_min_ms = 10;
    static constexpr int refresh_max_ms = 100;
    static constexpr int refresh_default_ms = 20;
    static constexpr int trace_flush_max_ms = 50;   // upstream BusTrace flush interval
    // Highlights fade out over these times, like the message view of Vehicle Spy
    static constexpr int64_t count_fade_us = 2000000;
    static constexpr int64_t byte_fade_us = 3000000;

    struct Entry
    {
        BusMessage msg;
        BusMessage prev;        // previous frame with the same key
        uint64_t key = 0;
        uint64_t count = 1;     // fixed: frames received, scroll: sequence number
        uint64_t changeCount = 1;
        int64_t deltaUs = -1;   // fixed: since previous same key, scroll: since previous entry
        int dataLines = 0;
        int signalLines = 0;
        bool expanded = true;
        // Fixed mode highlights: host time of the last frame and of each data byte's last change
        int64_t receivedUs = 0;
        int64_t bytesChangedUs = 0;               // newest of byteChangedUs
        std::vector<int64_t> byteChangedUs;

        [[nodiscard]] int childLines() const { return dataLines + signalLines; }
        [[nodiscard]] int lineCount() const { return 1 + (expanded ? childLines() : 0); }
    };

    explicit MonitorBuffer(Backend &backend, QObject *parent = nullptr);
    ~MonitorBuffer() override;

    [[nodiscard]] Mode mode() const { return _mode; }
    void setMode(Mode mode);
    [[nodiscard]] bool isPaused() const { return _paused; }
    void setPaused(bool paused);
    [[nodiscard]] bool absoluteTime() const { return _absoluteTime; }
    void setAbsoluteTime(bool absolute);
    [[nodiscard]] int refreshInterval() const { return _processTimer.interval(); }
    // How often new frames are taken over and the view repainted (refresh_min_ms..refresh_max_ms)
    void setRefreshInterval(int ms);
    void setAllExpanded(bool expanded);
    void toggleExpanded(int entryIndex);
    void erase();

    [[nodiscard]] int entryCount() const { return static_cast<int>(_entries.size()); }
    [[nodiscard]] const Entry &entry(int index) const { return *_entries[index]; }
    // Stable identity of an entry across updates: the key in fixed mode, the sequence number in scroll mode
    [[nodiscard]] uint64_t entryId(int index) const;
    [[nodiscard]] int indexOfId(uint64_t id) const;

    // Lines as the view shows them (message lines plus expanded child lines)
    [[nodiscard]] int lineCount();
    [[nodiscard]] int firstLineOf(int entryIndex);
    // Entry index and line within the entry (0 = message line) of a view line
    [[nodiscard]] std::pair<int, int> entryAtLine(int line);

    [[nodiscard]] QString headerText(int column) const;
    [[nodiscard]] QString text(const Entry &entry, int column) const;
    [[nodiscard]] QString dataHex(const Entry &entry) const;
    [[nodiscard]] QString dataLinePrefix(const Entry &entry, int dataLine) const;
    [[nodiscard]] QString dataLineHex(const Entry &entry, int dataLine) const;
    [[nodiscard]] QString signalText(const Entry &entry, int signalIndex) const;
    [[nodiscard]] QString typeString(const BusMessage &msg) const;
    [[nodiscard]] bool isActive(const Entry &entry, int64_t nowUs) const;
    [[nodiscard]] static uint64_t changedMask(const Entry &entry);
    // Highlight strength 1 (just changed) .. 0 (faded out) of the count and of one data byte
    [[nodiscard]] double countHighlight(const Entry &entry, int64_t nowUs) const;
    [[nodiscard]] double byteHighlight(const Entry &entry, int byte, int64_t nowUs) const;
    // True while a highlight of the entry is still fading, i.e. it needs repainting over time
    [[nodiscard]] bool isFading(const Entry &entry, int64_t nowUs) const;
    [[nodiscard]] static int64_t nowUs();

    [[nodiscard]] const QHash<QString, QString> &memos() const { return _memos; }
    void setMemos(const QHash<QString, QString> &memos);
    void setMemo(int entryIndex, const QString &text);

    // Index of the next entry (after `from`, wrapping) whose column text contains `text`, or -1
    [[nodiscard]] int find(int column, const QString &text, int from, bool forward) const;

    [[nodiscard]] static int dataLineCount(const BusMessage &msg) noexcept;

    // What changed since the last call: returns true when lines were added, removed
    // or moved (the whole view must be repainted); otherwise `touched` holds the
    // entries that received frames, so only their lines need repainting.
    bool takeChanges(QSet<const Entry *> &touched);

    // Timing trace for CANGAROO_MONITOR_STATS=2: one line per event with the host
    // time in microseconds, written to %TEMP%\cangaroo-monitor-trace.log
    [[nodiscard]] static bool traceEnabled();
    static void trace(const QString &event);

signals:
    // Emitted at most once per batch. removedFrontLines: view lines dropped
    // from the start of the list (scroll mode trimming), so the view can stay put.
    void changed(int removedFrontLines);
    void cleared();
    void headerChanged();
    void memosChanged();

private:
    Backend &_backend;
    std::deque<std::unique_ptr<Entry>> _entries;
    QHash<uint64_t, Entry *> _byKey;         // fixed mode
    QHash<uint64_t, BusMessage> _lastByKey;  // scroll mode
    QHash<QString, QString> _memos;
    mutable QHash<BusInterfaceId, QString> _interfaceNames;

    std::vector<int> _lineStart;             // first view line of each entry, plus the total
    bool _linesDirty = true;

    QSet<const Entry *> _touched;            // entries updated in place since takeChanges()
    bool _structureChanged = true;

    Mode _mode = Mode::Fixed;
    bool _paused = false;
    bool _absoluteTime = false;
    bool _expandNew = true;
    int _lastProcessed = -1;
    uint64_t _seq = 0;
    int64_t _lastEntryUs = 0;

    QTimer _processTimer;

    void processNewMessages();
    void processFixed(int size);
    int processScroll(int size);
    void clearEntries();
    void rebuild();
    void updateLineStarts();

    [[nodiscard]] static uint64_t makeKey(const BusMessage &msg) noexcept;
    [[nodiscard]] static bool sortsBefore(const BusMessage &a, const BusMessage &b) noexcept;
    [[nodiscard]] static QString memoKey(const BusMessage &msg);
    [[nodiscard]] int signalCount(const BusMessage &msg) const;
    [[nodiscard]] QString interfaceName(BusInterfaceId id) const;
};
