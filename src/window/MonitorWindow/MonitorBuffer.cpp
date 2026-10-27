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

#include "MonitorBuffer.h"

#include <algorithm>
#include <cstring>
#include <tuple>

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QLocale>

#include "core/Backend.h"
#include "core/BusTrace.h"
#include "db/model/CanDbMessage.h"
#include "db/model/CanDbNode.h"
#include "db/model/CanDbSignal.h"
#include "db/model/LinFrame.h"
#include "db/model/LinSignal.h"

MonitorBuffer::MonitorBuffer(Backend &backend, QObject *parent)
    : QObject(parent), _backend(backend)
{
    // Frames are taken over in batches, one per refresh interval; the view
    // repaints once per batch. Without new frames a tick only compares indices.
    // Coarse timers on Windows tick in about 15.6 ms steps, too coarse for a 10-20 ms refresh
    _processTimer.setTimerType(Qt::PreciseTimer);
    connect(&_processTimer, &QTimer::timeout, this, &MonitorBuffer::processNewMessages);
    setRefreshInterval(refresh_default_ms);
    _processTimer.start();

    BusTrace *trace = backend.getTrace();
    connect(trace, &BusTrace::beforeRemove, this, [this](int count)
    {
        // BusTrace prunes from the front, so already-processed indices shift down
        _lastProcessed = std::max(-1, _lastProcessed - count);
    });
    connect(trace, &BusTrace::afterClear, this, [this]()
    {
        clearEntries();
        _lastProcessed = -1;
        emit cleared();
    });

    connect(&backend, &Backend::onSetupChanged, this, [this]()
    {
        _interfaceNames.clear();
        rebuild();
    });
    connect(&backend, &Backend::beginMeasurement, this, [this]() { _interfaceNames.clear(); });
}

MonitorBuffer::~MonitorBuffer() = default;

bool MonitorBuffer::Filter::isActive() const
{
    return !showTx || !showRx || !hiddenMessageIds.isEmpty() || !hiddenLinFrameIds.isEmpty()
        || !hiddenInterfaces.isEmpty();
}

bool MonitorBuffer::Filter::accepts(const BusMessage &msg) const
{
    // Same rules as TraceFilterModel
    if (msg.isRX() ? !showRx : !showTx) { return false; }
    if (msg.busType() == BusType::LIN) {
        if (hiddenLinFrameIds.contains(static_cast<uint8_t>(msg.getId()))) { return false; }
    } else if (hiddenMessageIds.contains(msg.getId())) {
        return false;
    }
    return !hiddenInterfaces.contains(msg.getInterfaceId());
}

void MonitorBuffer::setFilter(const Filter &filter)
{
    _filter = filter;
    rebuild();
}

bool MonitorBuffer::isSortable(int column)
{
    switch (column) {
        case col_arbid:
        case col_description:
        case col_len:
        case col_network:
        case col_node:
        case col_tx:
            return true;
        default:
            return false;
    }
}

void MonitorBuffer::setSort(int column, bool descending)
{
    if (!isSortable(column)) { return; }
    if (column == _sortColumn && descending == _sortDescending) { return; }
    _sortColumn = column;
    _sortDescending = descending;
    if (_mode != Mode::Fixed) { return; }

    std::stable_sort(_entries.begin(), _entries.end(),
        [this](const std::unique_ptr<Entry> &a, const std::unique_ptr<Entry> &b) { return sortsBefore(*a, *b); });
    _linesDirty = true;
    _structureChanged = true;
    emit changed(0);
}

void MonitorBuffer::setMode(Mode mode)
{
    if (_mode == mode) { return; }
    _mode = mode;
    rebuild();
}

void MonitorBuffer::setPaused(bool paused)
{
    _paused = paused;
    if (!_paused) { processNewMessages(); }
}

void MonitorBuffer::setAbsoluteTime(bool absolute)
{
    if (_absoluteTime == absolute) { return; }
    _absoluteTime = absolute;
    emit headerChanged();
    _structureChanged = true;
    emit changed(0);
}

void MonitorBuffer::setRefreshInterval(int ms)
{
    ms = std::clamp(ms, refresh_min_ms, refresh_max_ms);
    _processTimer.setInterval(ms);
    // Frames reach the trace in batches too; a slower trace flush would hold back a fast refresh
    _backend.getTrace()->setFlushInterval(std::min(ms, trace_flush_max_ms));
}

void MonitorBuffer::setAllExpanded(bool expanded)
{
    _expandNew = expanded;
    for (auto &e : _entries) { e->expanded = expanded; }
    _linesDirty = true;
    _structureChanged = true;
    emit changed(0);
}

void MonitorBuffer::toggleExpanded(int entryIndex)
{
    if (entryIndex < 0 || entryIndex >= entryCount()) { return; }
    Entry &e = *_entries[entryIndex];
    if (e.childLines() == 0) { return; }
    e.expanded = !e.expanded;
    _linesDirty = true;
    _structureChanged = true;
    emit changed(0);
}

void MonitorBuffer::erase()
{
    // Clears this view only; the trace and the other windows keep their frames
    clearEntries();
    _lastProcessed = static_cast<int>(_backend.getTrace()->size()) - 1;
    emit cleared();
}

uint64_t MonitorBuffer::entryId(int index) const
{
    const Entry &e = *_entries[index];
    return _mode == Mode::Fixed ? e.key : e.count;
}

int MonitorBuffer::indexOfId(uint64_t id) const
{
    if (_entries.empty()) { return -1; }
    if (_mode == Mode::Scroll) {
        // Sequence numbers are consecutive
        const uint64_t first = _entries.front()->count;
        if (id < first || id - first >= _entries.size()) { return -1; }
        return static_cast<int>(id - first);
    }
    const Entry *e = _byKey.value(id);
    if (!e) { return -1; }
    for (int i = 0; i < entryCount(); ++i) {
        if (_entries[i].get() == e) { return i; }
    }
    return -1;
}

bool MonitorBuffer::traceEnabled()
{
    static const bool enabled = qEnvironmentVariableIntValue("CANGAROO_MONITOR_STATS") == 2;
    return enabled;
}

void MonitorBuffer::trace(const QString &event)
{
    static QFile file(QDir::temp().filePath(QStringLiteral("cangaroo-monitor-trace.log")));
    if (!file.isOpen() && !file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) { return; }
    const qint64 us = QDateTime::currentMSecsSinceEpoch() * 1000;
    file.write(QStringLiteral("%1 %2\n").arg(us).arg(event).toUtf8());
    file.flush();
}

bool MonitorBuffer::takeChanges(QSet<const Entry *> &touched)
{
    const bool structural = _structureChanged || _linesDirty;
    touched.clear();
    touched.swap(_touched);
    _structureChanged = false;
    return structural;
}

void MonitorBuffer::updateLineStarts()
{
    if (!_linesDirty) { return; }
    _lineStart.resize(_entries.size() + 1);
    int line = 0;
    for (size_t i = 0; i < _entries.size(); ++i) {
        _lineStart[i] = line;
        line += _entries[i]->lineCount();
    }
    _lineStart[_entries.size()] = line;
    _linesDirty = false;
}

int MonitorBuffer::lineCount()
{
    updateLineStarts();
    return _lineStart.back();
}

int MonitorBuffer::firstLineOf(int entryIndex)
{
    updateLineStarts();
    if (entryIndex < 0 || entryIndex >= entryCount()) { return -1; }
    return _lineStart[entryIndex];
}

std::pair<int, int> MonitorBuffer::entryAtLine(int line)
{
    updateLineStarts();
    if (line < 0 || line >= _lineStart.back()) { return {-1, 0}; }
    const auto it = std::upper_bound(_lineStart.begin(), _lineStart.end() - 1, line);
    const int index = static_cast<int>(it - _lineStart.begin()) - 1;
    return {index, line - _lineStart[index]};
}

QString MonitorBuffer::headerText(int column) const
{
    switch (column) {
        case col_count:       return tr("Count");
        case col_time:        return _absoluteTime ? tr("Time (abs)") : tr("Time (rel)");
        case col_tx:          return tr("Tx");
        case col_er:          return tr("Er");
        case col_description: return tr("Description");
        case col_arbid:       return tr("ArbId/Header");
        case col_len:         return tr("Len");
        case col_data:        return tr("DataBytes");
        case col_network:     return tr("Network");
        case col_node:        return tr("Node");
        case col_changecount: return tr("ChangeCnt");
        case col_timestamp:   return tr("Timestamp");
        case col_memo:        return tr("Memo/Notes");
        default:              return QString();
    }
}

QString MonitorBuffer::text(const Entry &entry, int column) const
{
    const BusMessage &msg = entry.msg;
    const bool isLin = msg.busType() == BusType::LIN;
    const bool isCanError = msg.isErrorFrame() && !isLin;

    switch (column) {
        case col_count:
            return QString::number(entry.count);
        case col_time:
            if (_absoluteTime) {
                const double start = _backend.getTimestampAtMeasurementStart();
                return QStringLiteral("%1 s").arg(msg.getFloatTimestamp() - start, 0, 'f', 6);
            }
            if (entry.deltaUs < 0) { return QString(); }
            return QStringLiteral("%1 ms").arg(static_cast<double>(entry.deltaUs) / 1000.0, 0, 'f', 3);
        case col_tx:
            return msg.isRX() ? QString() : QStringLiteral("Tx");
        case col_er:
            return msg.isErrorFrame() ? QStringLiteral("Er") : QString();
        case col_description:
            if (isLin) {
                if (msg.isLinSleepFrame())  { return tr("Sleep"); }
                if (msg.isLinWakeupFrame()) { return tr("Wakeup"); }
                const LinFrame *frame = _backend.findLinFrame(msg);
                return frame ? frame->name() : QString();
            }
            if (isCanError) { return tr("Error Frame"); }
            if (const CanDbMessage *dbmsg = _backend.findDbMessage(msg)) { return dbmsg->getName(); }
            return QString();
        case col_arbid:
            if (isCanError) { return QString(); }
            return QString::number(msg.getId(), 16).toUpper();
        case col_len:
            return QString::number(msg.getLength());
        case col_data:
            return msg.isErrorFrame() ? msg.getDataHexString() : dataHex(entry);
        case col_network:
            return interfaceName(msg.getInterfaceId());
        case col_node:
            if (isLin) {
                const LinFrame *frame = _backend.findLinFrame(msg);
                return frame ? frame->publisher() : QString();
            }
            if (const CanDbMessage *dbmsg = _backend.findDbMessage(msg); dbmsg && dbmsg->getSender()) {
                return dbmsg->getSender()->name();
            }
            return QString();
        case col_changecount:
            return _mode == Mode::Fixed ? QString::number(entry.changeCount) : QString();
        case col_timestamp:
        {
            const int64_t us = msg.getTimestamp_us();
            const QDateTime dt = QDateTime::fromMSecsSinceEpoch(us / 1000);
            return QLocale::c().toString(dt, QStringLiteral("yyyy/MM/dd HH:mm:ss"))
                 + QStringLiteral(".%1").arg(us % 1000000, 6, 10, QLatin1Char('0'));
        }
        case col_memo:
            return _memos.value(memoKey(msg));
        default:
            return QString();
    }
}

QString MonitorBuffer::dataHex(const Entry &entry) const
{
    return BusMessage::formatDataBytes(
        QByteArray(reinterpret_cast<const char *>(entry.msg.getData()), entry.msg.getLength()), false);
}

QString MonitorBuffer::dataLinePrefix(const Entry &entry, int dataLine) const
{
    const QString label = entry.msg.isFD() ? QStringLiteral("CAN-FD") : QStringLiteral("CAN");
    const QString head = (dataLine == 0) ? label + QLatin1Char(':') : QString(label.size() + 1, QLatin1Char(' '));
    return QStringLiteral("%1%2 : ").arg(head, QStringLiteral("%1").arg(dataLine * data_line_bytes, 4, 16, QLatin1Char('0')).toUpper());
}

QString MonitorBuffer::dataLineHex(const Entry &entry, int dataLine) const
{
    const int first = dataLine * data_line_bytes;
    const int n = std::min(data_line_bytes, entry.msg.getLength() - first);
    if (n <= 0) { return QString(); }
    return BusMessage::formatDataBytes(
        QByteArray(reinterpret_cast<const char *>(entry.msg.getData()) + first, n), false);
}

QString MonitorBuffer::signalText(const Entry &entry, int signalIndex) const
{
    const BusMessage &msg = entry.msg;
    auto valueText = [](uint64_t raw, const QString &valueName, double phys, const QString &unit)
    {
        if (!valueName.isEmpty()) { return QStringLiteral("%1 - %2").arg(raw).arg(valueName); }
        return unit.isEmpty() ? QString::number(phys) : QStringLiteral("%1 %2").arg(phys).arg(unit);
    };

    if (msg.busType() == BusType::LIN) {
        const LinFrame *frame = _backend.findLinFrame(msg);
        if (!frame || signalIndex < 0 || signalIndex >= frame->signalList().size()) { return QString(); }
        const LinSignal *sig = frame->signalList().at(signalIndex);
        const uint64_t raw = sig->extractRawValue({msg.getData(), static_cast<std::size_t>(msg.getLength())});
        return QStringLiteral("%1 = %2").arg(sig->name(),
            valueText(raw, sig->getValueName(raw), sig->convertToPhysical(raw), sig->unit()));
    }

    CanDbMessage *dbmsg = _backend.findDbMessage(msg);
    CanDbSignal *sig = dbmsg ? dbmsg->getSignal(signalIndex) : nullptr;
    if (!sig) { return QString(); }
    if (!sig->isPresentInMessage(msg)) { return QStringLiteral("%1 = -").arg(sig->name()); }
    const uint64_t raw = sig->extractRawDataFromMessage(msg);
    return QStringLiteral("%1 = %2").arg(sig->name(),
        valueText(raw, sig->getValueName(raw), sig->convertRawValueToPhysical(raw), sig->getUnit()));
}

QString MonitorBuffer::typeString(const BusMessage &msg) const
{
    if (msg.busType() == BusType::LIN) { return QStringLiteral("LIN"); }
    if (msg.isErrorFrame()) { return tr("CAN error frame"); }
    QString s = msg.isFD() ? QStringLiteral("CAN FD") : QStringLiteral("CAN");
    if (msg.isBRS()) { s += QStringLiteral(", BRS"); }
    if (msg.isESI()) { s += QStringLiteral(", ESI"); }
    s += msg.isExtended() ? tr(", extended ID") : tr(", standard ID");
    if (msg.isRTR()) { s += QStringLiteral(", RTR"); }
    return s;
}

bool MonitorBuffer::isActive(const Entry &entry, int64_t nowUs) const
{
    if (!_backend.isMeasurementRunning()) { return false; }
    // Active while frames keep coming at their own rate (at least 1 s of grace)
    const int64_t limitUs = std::max<int64_t>(1000000, 3 * entry.deltaUs);
    return nowUs - entry.msg.getTimestamp_us() <= limitUs;
}

int64_t MonitorBuffer::nowUs()
{
    return QDateTime::currentMSecsSinceEpoch() * 1000;
}

double MonitorBuffer::countHighlight(const Entry &entry, int64_t nowUs) const
{
    if (_mode != Mode::Fixed || entry.receivedUs == 0) { return 0.0; }
    const int64_t age = nowUs - entry.receivedUs;
    return age >= count_fade_us ? 0.0 : 1.0 - static_cast<double>(std::max<int64_t>(0, age)) / count_fade_us;
}

double MonitorBuffer::byteHighlight(const Entry &entry, int byte, int64_t nowUs) const
{
    if (_mode == Mode::Scroll) {
        // A scroll line is a snapshot: mark the bytes that differ from the previous frame, no fading
        return byte < 64 && ((changedMask(entry) >> byte) & 1ULL) ? 0.5 : 0.0;
    }
    if (byte < 0 || byte >= static_cast<int>(entry.byteChangedUs.size())) { return 0.0; }
    const int64_t changed = entry.byteChangedUs[byte];
    if (changed == 0) { return 0.0; }
    const int64_t age = nowUs - changed;
    return age >= byte_fade_us ? 0.0 : 1.0 - static_cast<double>(std::max<int64_t>(0, age)) / byte_fade_us;
}

bool MonitorBuffer::isFading(const Entry &entry, int64_t nowUs) const
{
    if (_mode != Mode::Fixed) { return false; }
    // A little longer than the fade, so the last repaint clears the highlight
    constexpr int64_t grace_us = 200000;
    return (entry.receivedUs != 0 && nowUs - entry.receivedUs < count_fade_us + grace_us)
        || (entry.bytesChangedUs != 0 && nowUs - entry.bytesChangedUs < byte_fade_us + grace_us);
}

uint64_t MonitorBuffer::changedMask(const Entry &entry)
{
    const BusMessage &cur = entry.msg;
    const BusMessage &prev = entry.prev;
    if (prev.getLength() == 0 || cur.isErrorFrame() || prev.isErrorFrame()) { return 0; }

    uint64_t mask = 0;
    const int len = std::min(cur.getLength(), prev.getLength());
    for (int i = 0; i < len; ++i) {
        if (cur.getData()[i] != prev.getData()[i]) { mask |= (1ULL << i); }
    }
    for (int i = len; i < cur.getLength(); ++i) { mask |= (1ULL << i); }
    return mask;
}

void MonitorBuffer::setMemos(const QHash<QString, QString> &memos)
{
    _memos = memos;
    _structureChanged = true;
    emit changed(0);
}

void MonitorBuffer::setMemo(int entryIndex, const QString &text)
{
    if (entryIndex < 0 || entryIndex >= entryCount()) { return; }
    // The memo belongs to the frame ID, so every entry of that ID shows it
    const QString key = memoKey(_entries[entryIndex]->msg);
    const QString trimmed = text.trimmed();
    _structureChanged = true;
    if (trimmed.isEmpty()) {
        _memos.remove(key);
    } else {
        _memos.insert(key, trimmed);
    }
    emit memosChanged();
    emit changed(0);
}

int MonitorBuffer::find(int column, const QString &text, int from, bool forward) const
{
    const int n = entryCount();
    if (n == 0 || text.isEmpty()) { return -1; }
    for (int i = 1; i <= n; ++i) {
        const int idx = ((from + (forward ? i : -i)) % n + n) % n;
        if (this->text(*_entries[idx], column).contains(text, Qt::CaseInsensitive)) { return idx; }
    }
    return -1;
}

int MonitorBuffer::dataLineCount(const BusMessage &msg) noexcept
{
    if (msg.busType() != BusType::CAN || msg.isErrorFrame() || msg.getLength() <= data_line_min_length) {
        return 0;
    }
    return (msg.getLength() + data_line_bytes - 1) / data_line_bytes;
}

int MonitorBuffer::signalCount(const BusMessage &msg) const
{
    if (msg.busType() == BusType::LIN) {
        const LinFrame *frame = _backend.findLinFrame(msg);
        return frame ? static_cast<int>(frame->signalList().size()) : 0;
    }
    if (msg.isErrorFrame()) { return 0; }
    const CanDbMessage *dbmsg = _backend.findDbMessage(msg);
    return dbmsg ? static_cast<int>(dbmsg->getSignals().size()) : 0;
}

uint64_t MonitorBuffer::makeKey(const BusMessage &msg) noexcept
{
    // Bit 63: RX; bit 62: error frame; bits 32-47: interface; bits 30-31: bus type;
    // bit 29: extended; bits 0-28: frame ID
    return static_cast<uint64_t>(msg.isRX()) << 63
         | static_cast<uint64_t>(msg.isErrorFrame()) << 62
         | static_cast<uint64_t>(msg.getInterfaceId()) << 32
         | static_cast<uint64_t>(static_cast<uint8_t>(msg.busType()) & 0x3) << 30
         | static_cast<uint64_t>(msg.isExtended()) << 29
         | (static_cast<uint64_t>(msg.getId()) & 0x1FFFFFFFull);
}

bool MonitorBuffer::sortsBefore(const Entry &a, const Entry &b) const
{
    // Error frames stay last. ID order keeps the same ID on several networks together.
    const BusMessage &ma = a.msg;
    const BusMessage &mb = b.msg;
    if (ma.isErrorFrame() != mb.isErrorFrame()) { return mb.isErrorFrame(); }

    int cmp = 0;
    switch (_sortColumn) {
        case col_description:
        case col_network:
        case col_node:
            cmp = text(a, _sortColumn).compare(text(b, _sortColumn), Qt::CaseInsensitive);
            break;
        case col_len:
            cmp = static_cast<int>(ma.getLength()) - static_cast<int>(mb.getLength());
            break;
        case col_tx:
            cmp = static_cast<int>(!ma.isRX()) - static_cast<int>(!mb.isRX());
            break;
        default:
            break;
    }
    if (cmp != 0) { return _sortDescending ? cmp > 0 : cmp < 0; }

    auto order = [](const BusMessage &m)
    {
        return std::make_tuple(static_cast<int>(m.busType()), m.isExtended(), m.getId(), m.getInterfaceId(), !m.isRX());
    };
    if (_sortColumn == col_arbid && _sortDescending) { return order(mb) < order(ma); }
    return order(ma) < order(mb);
}

QString MonitorBuffer::memoKey(const BusMessage &msg)
{
    const QString bus = msg.busType() == BusType::LIN ? QStringLiteral("LIN") : QStringLiteral("CAN");
    return QStringLiteral("%1:%2%3").arg(bus, QString::number(msg.getId(), 16).toUpper(),
                                         msg.isExtended() ? QStringLiteral("x") : QString());
}

QString MonitorBuffer::interfaceName(BusInterfaceId id) const
{
    auto it = _interfaceNames.constFind(id);
    if (it != _interfaceNames.constEnd()) { return it.value(); }
    const QString name = _backend.getInterfaceName(id);
    _interfaceNames.insert(id, name);
    return name;
}

void MonitorBuffer::processNewMessages()
{
    if (_paused) { return; }

    const int size = static_cast<int>(_backend.getTrace()->size());
    if (_lastProcessed >= size) { _lastProcessed = size - 1; }
    if (_lastProcessed + 1 >= size) { return; }

    if (traceEnabled()) {
        // Newest frame's adapter time: host time minus this is the delivery delay
        const BusMessage newest = _backend.getTrace()->getMessage(size - 1);
        trace(QStringLiteral("batch frames=%1 newest_if=%2 newest_ts=%3")
                  .arg(size - 1 - _lastProcessed).arg(newest.getInterfaceId()).arg(newest.getTimestamp_us()));
        for (int i = std::max(_lastProcessed + 1, size - 50); i < size; ++i) {
            const BusMessage m = _backend.getTrace()->getMessage(i);
            trace(QStringLiteral("frame if=%1 id=%2 ts=%3").arg(m.getInterfaceId()).arg(m.getId()).arg(m.getTimestamp_us()));
        }
    }

    int removedLines = 0;
    if (_mode == Mode::Fixed) {
        processFixed(size);
    } else {
        removedLines = processScroll(size);
    }
    emit changed(removedLines);
}

void MonitorBuffer::processFixed(int size)
{
    BusTrace *trace = _backend.getTrace();
    // Highlights fade from the time the frames reach the view, not from their bus time
    const int64_t now = nowUs();

    for (int i = _lastProcessed + 1; i < size; ++i) {
        const BusMessage msg = trace->getMessage(i);
        if (!_filter.accepts(msg)) {
            _lastProcessed = i;
            continue;
        }
        const uint64_t key = makeKey(msg);

        if (Entry *e = _byKey.value(key)) {
            const bool changed = msg.getLength() != e->msg.getLength()
                || std::memcmp(msg.getData(), e->msg.getData(), msg.getLength()) != 0;
            e->deltaUs = msg.getTimestamp_us() - e->msg.getTimestamp_us();
            if (changed && !msg.isErrorFrame()) {
                const int len = msg.getLength();
                const int oldLen = e->msg.getLength();
                e->byteChangedUs.resize(len, 0);
                for (int b = 0; b < len; ++b) {
                    if (b >= oldLen || msg.getData()[b] != e->msg.getData()[b]) { e->byteChangedUs[b] = now; }
                }
                e->bytesChangedUs = now;
            }
            e->receivedUs = now;
            e->prev = e->msg;
            e->msg = msg;
            ++e->count;
            if (changed) { ++e->changeCount; }
            _touched.insert(e);
            // A frame ID whose length changes (e.g. 8 -> 64 bytes) needs other data lines
            const int lines = dataLineCount(msg);
            if (lines != e->dataLines) {
                e->dataLines = lines;
                _linesDirty = true;
            }
        } else {
            auto entry = std::make_unique<Entry>();
            entry->msg = msg;
            entry->key = key;
            entry->dataLines = dataLineCount(msg);
            entry->signalLines = signalCount(msg);
            entry->expanded = _expandNew;
            entry->receivedUs = now;
            entry->byteChangedUs.assign(msg.getLength(), 0);

            const auto pos = std::upper_bound(_entries.begin(), _entries.end(), entry,
                [this](const std::unique_ptr<Entry> &e, const std::unique_ptr<Entry> &r) { return sortsBefore(*e, *r); });
            _byKey.insert(key, entry.get());
            _entries.insert(pos, std::move(entry));
            _linesDirty = true;
        }
        _lastProcessed = i;
    }
}

int MonitorBuffer::processScroll(int size)
{
    BusTrace *trace = _backend.getTrace();

    // Frames that would be trimmed right away are not taken over at all
    const int first = std::max(_lastProcessed + 1, size - scroll_max_entries);
    for (int i = first; i < size; ++i) {
        BusMessage msg = trace->getMessage(i);
        if (!_filter.accepts(msg)) { continue; }
        auto entry = std::make_unique<Entry>();
        entry->msg = std::move(msg);
        entry->key = makeKey(entry->msg);
        entry->count = ++_seq;
        entry->prev = _lastByKey.value(entry->key);
        _lastByKey.insert(entry->key, entry->msg);
        const int64_t us = entry->msg.getTimestamp_us();
        entry->deltaUs = (_lastEntryUs > 0) ? us - _lastEntryUs : -1;
        _lastEntryUs = us;
        entry->dataLines = dataLineCount(entry->msg);
        entry->signalLines = signalCount(entry->msg);
        entry->expanded = _expandNew;
        _entries.push_back(std::move(entry));
    }
    _lastProcessed = size - 1;
    if (size > first) {
        _linesDirty = true;
        _structureChanged = true;
    }

    // Only the line offsets of the entries change, so trimming costs no more than the removed entries
    int removedLines = 0;
    const int excess = entryCount() - scroll_max_entries;
    if (excess > 0) {
        for (int i = 0; i < excess; ++i) {
            removedLines += _entries.front()->lineCount();
            _entries.pop_front();
        }
        _linesDirty = true;
    }
    return removedLines;
}

void MonitorBuffer::clearEntries()
{
    _structureChanged = true;
    _touched.clear();
    _entries.clear();
    _byKey.clear();
    _lastByKey.clear();
    _seq = 0;
    _lastEntryUs = 0;
    _linesDirty = true;
}

void MonitorBuffer::rebuild()
{
    clearEntries();
    const int size = static_cast<int>(_backend.getTrace()->size());
    _lastProcessed = (_mode == Mode::Fixed) ? -1 : std::max(-1, size - 1 - scroll_max_entries);
    emit cleared();
    processNewMessages();
}
