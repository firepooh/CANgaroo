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

#include "MonitorTable.h"

#include <algorithm>

#include <QAbstractTableModel>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QElapsedTimer>
#include <QFontDatabase>
#include <QFontInfo>
#include <QGlyphRun>
#include <QHeaderView>
#include <QHelpEvent>
#include <QKeyEvent>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPainter>
#include <QPolygon>
#include <QScrollBar>
#include <QToolTip>

#include "core/Log.h"
#include "core/ThemeManager.h"
#include "MonitorBuffer.h"

// Column titles for the QHeaderView; the table has no item rows
class MonitorHeaderModel : public QAbstractTableModel
{
public:
    MonitorHeaderModel(MonitorBuffer *buffer, QObject *parent)
        : QAbstractTableModel(parent), _buffer(buffer)
    {
    }

    int rowCount(const QModelIndex &parent) const override
    {
        (void) parent;
        return 0;
    }

    int columnCount(const QModelIndex &parent) const override
    {
        return parent.isValid() ? 0 : MonitorBuffer::column_count;
    }

    QVariant data(const QModelIndex &index, int role) const override
    {
        (void) index;
        (void) role;
        return QVariant();
    }

    QVariant headerData(int section, Qt::Orientation orientation, int role) const override
    {
        if (orientation != Qt::Horizontal) { return QVariant(); }
        if (role == Qt::DisplayRole) { return _buffer->headerText(section); }
        if (role == Qt::TextAlignmentRole) { return QVariant(Qt::AlignLeft | Qt::AlignVCenter); }
        return QVariant();
    }

    void refresh() { emit headerDataChanged(Qt::Horizontal, 0, MonitorBuffer::column_count - 1); }

private:
    MonitorBuffer *_buffer;
};

namespace
{

constexpr int cell_padding = 4;
constexpr int branch_width = 16;   // expand arrow area in the first visible column
constexpr int child_indent = 28;   // child lines start this far into the first visible column

} // namespace

MonitorTable::MonitorTable(MonitorBuffer *buffer, QWidget *parent)
    : QAbstractScrollArea(parent), _buffer(buffer)
{
    setFocusPolicy(Qt::StrongFocus);
    viewport()->setAutoFillBackground(false);
    viewport()->setAttribute(Qt::WA_OpaquePaintEvent);

    _headerModel = new MonitorHeaderModel(buffer, this);
    _header = new QHeaderView(Qt::Horizontal, this);
    _header->setModel(_headerModel);
    _header->setSectionsMovable(true);
    _header->setSectionsClickable(false);
    _header->setHighlightSections(false);
    _header->setStretchLastSection(true);
    _header->setDefaultAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    _header->show();

    updateMetrics();
    applyDefaultColumnWidths();

    verticalScrollBar()->setSingleStep(1);
    horizontalScrollBar()->setSingleStep(20);

    connect(_header, &QHeaderView::sectionResized, this, [this]()
    {
        updateScrollBars();
        viewport()->update();
    });
    connect(_header, &QHeaderView::sectionMoved, this, [this]() { viewport()->update(); });
    connect(_buffer, &MonitorBuffer::changed, this, &MonitorTable::onBufferChanged);
    connect(_buffer, &MonitorBuffer::cleared, this, [this]()
    {
        finishMemoEdit(false);
        onBufferChanged(0);
    });
    connect(_buffer, &MonitorBuffer::headerChanged, this, [this]() { _headerModel->refresh(); });

    // The activity dots in fixed mode turn grey as time passes, without new frames
    _activityTimer.setInterval(500);
    connect(&_activityTimer, &QTimer::timeout, this, [this]()
    {
        if (isVisible() && _buffer->mode() == MonitorBuffer::Mode::Fixed && _buffer->entryCount() > 0) {
            viewport()->update();
        }
    });
    _activityTimer.start();

    _statsEnabled = qEnvironmentVariableIntValue("CANGAROO_MONITOR_STATS") >= 1;
    if (_statsEnabled) {
        _statsTimer.setInterval(5000);
        connect(&_statsTimer, &QTimer::timeout, this, [this]()
        {
            const double avgMs = _statsPaints ? static_cast<double>(_statsTotalNs) / _statsPaints / 1e6 : 0.0;
            const int n = std::max(1, _statsPaints);
            auto perPaintMs = [n](qint64 ns) { return QString::number(static_cast<double>(ns) / n / 1e6, 'f', 2); };
            const QString line = QStringLiteral("Monitor: %1 paints/s, paint avg %2 ms, max %3 ms, refresh %4 ms "
                                    "(fill %5, message lines %6, child lines %7, drawText %8 ms in %9 calls)")
                         .arg(_statsPaints / 5.0, 0, 'f', 1)
                         .arg(avgMs, 0, 'f', 2)
                         .arg(static_cast<double>(_statsMaxNs) / 1e6, 0, 'f', 2)
                         .arg(_buffer->refreshInterval())
                         .arg(perPaintMs(_statsFillNs), perPaintMs(_statsMessageNs), perPaintMs(_statsChildNs),
                              perPaintMs(_statsDrawTextNs))
                         .arg(_statsDrawTextCalls / n);
            log_info(line);
            // The log window cuts long lines; the file keeps them whole
            QFile file(QDir::temp().filePath(QStringLiteral("cangaroo-monitor-stats.log")));
            if (file.open(QIODevice::Append | QIODevice::Text)) {
                file.write((QDateTime::currentDateTime().toString(QStringLiteral("hh:mm:ss ")) + line + QLatin1Char('\n')).toUtf8());
            }
            _statsPaints = 0;
            _statsTotalNs = 0;
            _statsMaxNs = 0;
            _statsFillNs = 0;
            _statsMessageNs = 0;
            _statsChildNs = 0;
            _statsDrawTextNs = 0;
            _statsDrawTextCalls = 0;
        });
        _statsTimer.start();
    }
}

void MonitorTable::updateMetrics()
{
    // Consolas reads better than the generic fixed font on Windows; fall back if it is missing
    _monoFont = QFont(QStringLiteral("Consolas"), font().pointSize());
    _monoFont.setStyleHint(QFont::TypeWriter);
    if (!QFontInfo(_monoFont).fixedPitch()) {
        _monoFont = QFontDatabase::systemFont(QFontDatabase::FixedFont);
        _monoFont.setPointSize(font().pointSize());
    }

    _uiRawFont = QRawFont::fromFont(font());
    _monoRawFont = QRawFont::fromFont(_monoFont);

    const QFontMetrics ui(font());
    const QFontMetrics mono(_monoFont);
    _monoCharWidth = std::max(1, mono.horizontalAdvance(QLatin1Char('0')));
    _rowHeight = std::max(ui.height(), mono.height()) + 4;

    setViewportMargins(0, _header->sizeHint().height(), 0, 0);
    updateGeometries();
}

void MonitorTable::updateGeometries()
{
    const QRect vg = viewport()->geometry();
    const int h = _header->sizeHint().height();
    _header->setGeometry(vg.left(), vg.top() - h, vg.width(), h);
}

void MonitorTable::applyDefaultColumnWidths()
{
    // Widths in characters of the UI font, so they scale with the font and DPI
    const int ch = fontMetrics().averageCharWidth();
    const int widths[MonitorBuffer::column_count] = {
        9 * ch + branch_width + 2 * cell_padding,       // col_count
        14 * ch,                                         // col_time
        4 * ch,                                          // col_tx
        4 * ch,                                          // col_er
        22 * ch,                                         // col_description
        13 * ch,                                         // col_arbid
        5 * ch,                                          // col_len
        48 * _monoCharWidth + 2 * cell_padding,          // col_data (16 bytes)
        14 * ch,                                         // col_network
        10 * ch,                                         // col_node
        10 * ch,                                         // col_changecount
        28 * ch,                                         // col_timestamp
        20 * ch,                                         // col_memo
    };

    // Never narrower than the header text
    const QFontMetrics headerFm(_header->font());
    for (int col = 0; col < MonitorBuffer::column_count; ++col) {
        const int title = headerFm.horizontalAdvance(_buffer->headerText(col)) + 2 * ch + 8;
        _header->resizeSection(col, std::max(widths[col], title));
    }
}

int MonitorTable::visibleLines() const
{
    return std::max(1, viewport()->height() / _rowHeight);
}

int MonitorTable::lineAt(int y) const
{
    return verticalScrollBar()->value() + y / _rowHeight;
}

void MonitorTable::updateScrollBars()
{
    const int lines = _buffer->lineCount();
    const int visible = visibleLines();
    verticalScrollBar()->setPageStep(visible);
    verticalScrollBar()->setRange(0, std::max(0, lines - visible));

    const int width = viewport()->width();
    horizontalScrollBar()->setPageStep(width);
    horizontalScrollBar()->setRange(0, std::max(0, _header->length() - width));
}

void MonitorTable::onBufferChanged(int removedFrontLines)
{
    if (!_buffer->takeChanges(_touched)) {
        // Only frames of existing entries: repaint just their visible lines
        if (_touched.isEmpty() || !isVisible()) { return; }
        QRegion dirty;
        const int first = verticalScrollBar()->value();
        const int width = viewport()->width();
        auto [index, sub] = _buffer->entryAtLine(first);
        int y = -sub * _rowHeight;
        for (; index >= 0 && index < _buffer->entryCount() && y < viewport()->height(); ++index) {
            const MonitorBuffer::Entry &e = _buffer->entry(index);
            const int h = e.lineCount() * _rowHeight;
            if (_touched.contains(&e)) { dirty += QRect(0, std::max(0, y), width, h + std::min(0, y)); }
            y += h;
        }
        if (!dirty.isEmpty()) { viewport()->update(dirty); }
        return;
    }

    QScrollBar *bar = verticalScrollBar();
    const bool atBottom = bar->value() >= bar->maximum();
    const int value = bar->value();

    updateScrollBars();

    if (_buffer->mode() == MonitorBuffer::Mode::Scroll) {
        // Follow new frames at the bottom; elsewhere keep the same lines on screen
        bar->setValue(atBottom ? bar->maximum() : std::max(0, value - removedFrontLines));
    }
    if (isVisible()) { viewport()->update(); }
}

void MonitorTable::resizeEvent(QResizeEvent *event)
{
    QAbstractScrollArea::resizeEvent(event);
    updateGeometries();
    updateScrollBars();
}

void MonitorTable::showEvent(QShowEvent *event)
{
    QAbstractScrollArea::showEvent(event);
    // Changes taken while hidden were not painted, so start from a full repaint
    QSet<const MonitorBuffer::Entry *> ignored;
    _buffer->takeChanges(ignored);
    updateScrollBars();
    if (_buffer->mode() == MonitorBuffer::Mode::Scroll) { verticalScrollBar()->setValue(verticalScrollBar()->maximum()); }
    viewport()->update();
}

void MonitorTable::scrollContentsBy(int dx, int dy)
{
    (void) dy;
    if (dx != 0) { _header->setOffset(horizontalScrollBar()->value()); }
    finishMemoEdit(true);
    viewport()->update();
}

std::vector<MonitorTable::ColumnRect> MonitorTable::visibleColumns() const
{
    std::vector<ColumnRect> cols;
    const int width = viewport()->width();
    for (int v = 0; v < _header->count(); ++v) {
        const int logical = _header->logicalIndex(v);
        if (_header->isSectionHidden(logical)) { continue; }
        const int x = _header->sectionViewportPosition(logical);
        const int w = _header->sectionSize(logical);
        if (x + w <= 0 || x >= width) { continue; }
        cols.push_back({logical, x, w});
    }
    return cols;
}

int MonitorTable::firstVisibleColumnX() const
{
    for (int v = 0; v < _header->count(); ++v) {
        const int logical = _header->logicalIndex(v);
        if (!_header->isSectionHidden(logical)) { return _header->sectionViewportPosition(logical); }
    }
    return 0;
}

void MonitorTable::paintEvent(QPaintEvent *event)
{
    QElapsedTimer statsTimer;
    if (_statsEnabled) { statsTimer.start(); }
    // Counted when the painter is done, i.e. on every return below
    struct StatsGuard
    {
        MonitorTable *table;
        const QElapsedTimer &timer;
        const QRect dirty;
        ~StatsGuard()
        {
            if (!table->_statsEnabled) { return; }
            const qint64 ns = timer.nsecsElapsed();
            if (MonitorBuffer::traceEnabled()) {
                MonitorBuffer::trace(QStringLiteral("paint us=%1 y=%2 h=%3").arg(ns / 1000).arg(dirty.y()).arg(dirty.height()));
            }
            ++table->_statsPaints;
            table->_statsTotalNs += ns;
            table->_statsMaxNs = std::max(table->_statsMaxNs, ns);
        }
    } statsGuard{this, statsTimer, event->rect()};

    QPainter p(viewport());
    const QRect area = viewport()->rect();
    const QRect dirty = event->rect();
    p.fillRect(dirty, palette().color(QPalette::Base));
    if (_statsEnabled) { _statsFillNs += statsTimer.nsecsElapsed(); }

    const int lines = _buffer->lineCount();
    const int first = verticalScrollBar()->value();
    if (first >= lines) { return; }

    // Everything that is the same for all lines is looked up once per paint
    const bool dark = ThemeManager::instance().isDarkMode();
    const QFont uiFont = font();
    const QFontMetrics uiFm(uiFont);
    const QFontMetrics monoFm(_monoFont);
    PaintContext ctx{
        .p = p,
        .cols = visibleColumns(),
        .firstX = firstVisibleColumnX(),
        .width = area.width(),
        .nowUs = QDateTime::currentMSecsSinceEpoch() * 1000,
        .uiFont = uiFont,
        .uiFm = uiFm,
        .uiBaseline = (_rowHeight - uiFm.height()) / 2 + uiFm.ascent(),
        .monoBaseline = (_rowHeight - monoFm.height()) / 2 + monoFm.ascent(),
        .text = palette().color(QPalette::Text),
        .selectedText = dark ? palette().color(QPalette::HighlightedText) : QColor(Qt::black),
        .error = dark ? QColor(255, 110, 110) : QColor(200, 0, 0),
        .changed = dark ? QColor(255, 150, 60) : QColor(205, 85, 0),
        .tx = dark ? QColor(110, 170, 255) : QColor(0, 80, 200),
        .branch = palette().color(QPalette::Mid),
    };
    const QColor selectionColor = dark ? palette().color(QPalette::Highlight) : QColor(200, 245, 195);

    auto [entryIndex, sub] = _buffer->entryAtLine(first);
    for (int y = 0; y < area.height() && entryIndex >= 0 && entryIndex < _buffer->entryCount(); y += _rowHeight) {
        const MonitorBuffer::Entry &e = _buffer->entry(entryIndex);
        if (y + _rowHeight <= dirty.top() || y >= dirty.bottom() + 1) {
            if (++sub >= e.lineCount()) {
                sub = 0;
                ++entryIndex;
            }
            continue;
        }
        const bool selected = _hasSelection && _buffer->entryId(entryIndex) == _selectedId;
        if (selected) { p.fillRect(QRect(0, y, area.width(), _rowHeight), selectionColor); }

        const qint64 lineStart = _statsEnabled ? statsTimer.nsecsElapsed() : 0;
        if (sub == 0) {
            paintMessageLine(ctx, e, y, selected);
            if (_statsEnabled) { _statsMessageNs += statsTimer.nsecsElapsed() - lineStart; }
        } else {
            paintChildLine(ctx, e, sub - 1, y, selected);
            if (_statsEnabled) { _statsChildNs += statsTimer.nsecsElapsed() - lineStart; }
        }

        if (++sub >= e.lineCount()) {
            sub = 0;
            ++entryIndex;
        }
    }
}

void MonitorTable::PaintContext::setFont(const QFont &font)
{
    if (currentFont != &font) {
        p.setFont(font);
        currentFont = &font;
    }
}

void MonitorTable::PaintContext::setPen(const QColor &color)
{
    if (!penValid || currentPen != color.rgba()) {
        p.setPen(color);
        currentPen = color.rgba();
        penValid = true;
    }
}

void MonitorTable::drawText(PaintContext &ctx, const QPoint &pos, const QString &text, bool mono) const
{
    QElapsedTimer t;
    if (_statsEnabled) { t.start(); }

    // QPainter::drawText() shapes the string again on every call. The cell texts
    // are plain (hex, numbers, names), so look the glyphs up directly and draw them
    // as a glyph run. Fall back to drawText() when the font lacks a character
    // (e.g. Korean in a DBC name) so font fallback still works.
    const QRawFont &raw = mono ? _monoRawFont : _uiRawFont;
    QList<quint32> glyphs = raw.isValid() ? raw.glyphIndexesForString(text) : QList<quint32>();
    const bool fast = !glyphs.isEmpty() && !glyphs.contains(0u);
    if (fast) {
        const QList<QPointF> advances = raw.advancesForGlyphIndexes(glyphs);
        QList<QPointF> positions;
        positions.reserve(glyphs.size());
        qreal x = 0;
        for (const QPointF &advance : advances) {
            positions.append(QPointF(x, 0));
            x += advance.x();
        }
        QGlyphRun run;
        run.setRawFont(raw);
        run.setGlyphIndexes(glyphs);
        run.setPositions(positions);
        ctx.p.drawGlyphRun(pos, run);
    } else {
        ctx.setFont(mono ? _monoFont : ctx.uiFont);
        ctx.p.drawText(pos, text);
    }

    if (_statsEnabled) {
        _statsDrawTextNs += t.nsecsElapsed();
        ++_statsDrawTextCalls;
    }
}

void MonitorTable::drawCell(PaintContext &ctx, int x, int y, int width, const QString &text, int align, bool mono) const
{
    // QPainter::drawText(QRect, flags, ...) lays the text out again on every call;
    // the point overload is much cheaper, so position and elide here instead.
    // A cell is never clipped: text that would not fit is elided first.
    int textWidth = mono ? static_cast<int>(text.size()) * _monoCharWidth : -1;
    if (!mono && (align != Qt::AlignLeft || text.size() * ctx.uiFm.maxWidth() > width)) {
        textWidth = ctx.uiFm.horizontalAdvance(text);
    }

    QString elided;
    if (textWidth > width) {
        if (mono) {
            const int chars = std::max(0, width / _monoCharWidth - 1);
            elided = text.left(chars) + QChar(0x2026);
            textWidth = (chars + 1) * _monoCharWidth;
        } else {
            elided = ctx.uiFm.elidedText(text, Qt::ElideRight, width);
            textWidth = ctx.uiFm.horizontalAdvance(elided);
        }
    }
    const QString &shown = elided.isNull() ? text : elided;
    if (shown.isEmpty()) { return; }

    int tx = x;
    if (align == Qt::AlignRight) {
        tx = x + width - textWidth;
    } else if (align == Qt::AlignHCenter) {
        tx = x + (width - textWidth) / 2;
    }

    drawText(ctx, QPoint(tx, y + (mono ? ctx.monoBaseline : ctx.uiBaseline)), shown, mono);
}

void MonitorTable::paintMessageLine(PaintContext &ctx, const MonitorBuffer::Entry &e, int y, bool selected)
{
    QPainter &p = ctx.p;
    const bool error = e.msg.isErrorFrame();
    const QColor &textColor = error ? ctx.error : selected ? ctx.selectedText : ctx.text;

    for (const ColumnRect &c : ctx.cols) {
        int x = c.x + cell_padding;
        int w = c.width - 2 * cell_padding;

        if (c.x == ctx.firstX) {
            // Expand arrow, like a tree branch
            if (e.childLines() > 0) {
                const int cx = c.x + branch_width / 2;
                const int cy = y + _rowHeight / 2;
                const int s = std::max(3, _rowHeight / 5);
                QPolygon arrow;
                if (e.expanded) {
                    arrow << QPoint(cx - s, cy - s / 2) << QPoint(cx + s, cy - s / 2) << QPoint(cx, cy + s / 2 + 1);
                } else {
                    arrow << QPoint(cx - s / 2, cy - s) << QPoint(cx - s / 2, cy + s) << QPoint(cx + s / 2 + 1, cy);
                }
                ctx.setPen(ctx.branch);
                p.setBrush(ctx.branch);
                p.drawPolygon(arrow);
            }
            x += branch_width;
            w -= branch_width;
        }
        if (w <= 0) { continue; }

        if (c.column == MonitorBuffer::col_data && !error) {
            drawHex(ctx, x, y, w, _buffer->dataHex(e), MonitorBuffer::changedMask(e), textColor, true);
            continue;
        }

        if (c.column == MonitorBuffer::col_time && _buffer->mode() == MonitorBuffer::Mode::Fixed) {
            // Activity dot: green while the ID keeps arriving
            const int d = std::min(w, std::max(6, _rowHeight / 2 - 1));
            const QColor dot = _buffer->isActive(e, ctx.nowUs) ? QColor(60, 180, 75) : QColor(190, 190, 190);
            ctx.setPen(dot.darker(130));
            p.setBrush(dot);
            p.drawEllipse(QRect(x, y + (_rowHeight - d) / 2, d, d));
            x += d + cell_padding;
            w -= d + cell_padding;
            if (w <= 0) { continue; }
        }

        const QString text = _buffer->text(e, c.column);
        if (text.isEmpty()) { continue; }

        int align = Qt::AlignLeft;
        switch (c.column) {
            case MonitorBuffer::col_count:
            case MonitorBuffer::col_time:
            case MonitorBuffer::col_changecount:
                align = Qt::AlignRight;
                break;
            case MonitorBuffer::col_tx:
            case MonitorBuffer::col_er:
            case MonitorBuffer::col_len:
                align = Qt::AlignHCenter;
                break;
            default:
                break;
        }

        ctx.setPen(c.column == MonitorBuffer::col_tx && !error ? ctx.tx : textColor);
        drawCell(ctx, x, y, w, text, align, c.column == MonitorBuffer::col_arbid || c.column == MonitorBuffer::col_data);
    }
}

void MonitorTable::paintChildLine(PaintContext &ctx, const MonitorBuffer::Entry &e, int childLine, int y, bool selected)
{
    const QColor &textColor = selected ? ctx.selectedText : ctx.text;
    const int x = ctx.firstX + child_indent;
    const int width = ctx.width - x;
    if (width <= 0) { return; }

    ctx.setPen(textColor);
    if (childLine < e.dataLines) {
        const QString prefix = _buffer->dataLinePrefix(e, childLine);
        drawText(ctx, QPoint(x, y + ctx.monoBaseline), prefix, true);
        const uint64_t mask = (MonitorBuffer::changedMask(e) >> (childLine * MonitorBuffer::data_line_bytes))
                            & ((1ULL << MonitorBuffer::data_line_bytes) - 1);
        const int hexX = x + static_cast<int>(prefix.size()) * _monoCharWidth;
        drawHex(ctx, hexX, y, ctx.width - hexX, _buffer->dataLineHex(e, childLine), mask, textColor, false);
        return;
    }

    drawCell(ctx, x, y, width, _buffer->signalText(e, childLine - e.dataLines), Qt::AlignLeft, false);
}

void MonitorTable::drawHex(PaintContext &ctx, int x, int y, int width, const QString &hex, uint64_t mask,
                           const QColor &normal, bool elide) const
{
    // Byte i occupies characters 3i and 3i+1 of "AB CD EF"; with a fixed-pitch font the
    // changed bytes are drawn as a second string with everything else blanked out
    QString shown = hex;
    bool cut = false;
    const int maxChars = width / _monoCharWidth;
    if (elide && hex.size() > maxChars) {
        shown = hex.left(std::max(0, maxChars - 1));
        cut = true;
    }

    QString normalText = shown;
    QString changedText;
    if (mask != 0) {
        changedText = QString(shown.size(), QLatin1Char(' '));
        for (int i = 0; i < 64 && 3 * i < shown.size(); ++i) {
            if (!((mask >> i) & 1ULL)) { continue; }
            for (int k = 3 * i; k < 3 * i + 2 && k < shown.size(); ++k) {
                changedText[k] = shown[k];
                normalText[k] = QLatin1Char(' ');
            }
        }
    }
    if (cut) { normalText += QChar(0x2026); }

    const QPoint pos(x, y + ctx.monoBaseline);
    ctx.setPen(normal);
    drawText(ctx, pos, normalText, true);
    if (!changedText.isEmpty()) {
        ctx.setPen(ctx.changed);
        drawText(ctx, pos, changedText, true);
    }
}

void MonitorTable::selectEntry(int entryIndex, bool center)
{
    if (entryIndex < 0 || entryIndex >= _buffer->entryCount()) { return; }
    _hasSelection = true;
    _selectedId = _buffer->entryId(entryIndex);

    const int line = _buffer->firstLineOf(entryIndex);
    const int span = _buffer->entry(entryIndex).lineCount();
    const int visible = visibleLines();
    QScrollBar *bar = verticalScrollBar();
    if (center) {
        bar->setValue(line - visible / 2);
    } else if (line < bar->value()) {
        bar->setValue(line);
    } else if (line + span > bar->value() + visible) {
        bar->setValue(line + span - visible);
    }
    viewport()->update();
}

bool MonitorTable::find(int column, const QString &text, bool forward)
{
    const int current = _hasSelection ? _buffer->indexOfId(_selectedId) : -1;
    const int from = current >= 0 ? current : (forward ? -1 : _buffer->entryCount());
    const int found = _buffer->find(column, text, from, forward);
    if (found < 0) { return false; }
    selectEntry(found, true);
    return true;
}

void MonitorTable::mousePressEvent(QMouseEvent *event)
{
    finishMemoEdit(true);
    const auto [index, sub] = _buffer->entryAtLine(lineAt(event->position().toPoint().y()));
    if (index < 0) {
        _hasSelection = false;
        viewport()->update();
        return;
    }

    _hasSelection = true;
    _selectedId = _buffer->entryId(index);

    const int x = event->position().toPoint().x();
    const int firstX = firstVisibleColumnX();
    if (sub == 0 && x >= firstX && x < firstX + branch_width) {
        _buffer->toggleExpanded(index);
    }
    viewport()->update();
}

void MonitorTable::mouseDoubleClickEvent(QMouseEvent *event)
{
    const QPoint pos = event->position().toPoint();
    const auto [index, sub] = _buffer->entryAtLine(lineAt(pos.y()));
    if (index < 0 || sub != 0) { return; }

    if (_header->logicalIndexAt(pos.x()) == MonitorBuffer::col_memo) {
        startMemoEdit(index);
    } else {
        _buffer->toggleExpanded(index);
    }
}

void MonitorTable::keyPressEvent(QKeyEvent *event)
{
    const int current = _hasSelection ? _buffer->indexOfId(_selectedId) : -1;
    QScrollBar *bar = verticalScrollBar();

    switch (event->key()) {
        case Qt::Key_Up:
            selectEntry(current < 0 ? 0 : std::max(0, current - 1), false);
            break;
        case Qt::Key_Down:
            selectEntry(current < 0 ? 0 : std::min(_buffer->entryCount() - 1, current + 1), false);
            break;
        case Qt::Key_PageUp:
            bar->triggerAction(QAbstractSlider::SliderPageStepSub);
            break;
        case Qt::Key_PageDown:
            bar->triggerAction(QAbstractSlider::SliderPageStepAdd);
            break;
        case Qt::Key_Home:
            bar->setValue(0);
            break;
        case Qt::Key_End:
            bar->setValue(bar->maximum());
            break;
        case Qt::Key_Left:
        case Qt::Key_Right:
            if (current >= 0 && _buffer->entry(current).expanded != (event->key() == Qt::Key_Right)) {
                _buffer->toggleExpanded(current);
            }
            break;
        case Qt::Key_F2:
            if (current >= 0) { startMemoEdit(current); }
            break;
        default:
            QAbstractScrollArea::keyPressEvent(event);
            break;
    }
}

bool MonitorTable::viewportEvent(QEvent *event)
{
    if (event->type() == QEvent::ToolTip) {
        auto *help = static_cast<QHelpEvent *>(event);
        const auto [index, sub] = _buffer->entryAtLine(lineAt(help->pos().y()));
        if (index >= 0) {
            QToolTip::showText(help->globalPos(), _buffer->typeString(_buffer->entry(index).msg), viewport());
        } else {
            QToolTip::hideText();
        }
        return true;
    }
    return QAbstractScrollArea::viewportEvent(event);
}

void MonitorTable::startMemoEdit(int entryIndex)
{
    finishMemoEdit(true);
    if (_header->isSectionHidden(MonitorBuffer::col_memo)) { return; }

    selectEntry(entryIndex, false);
    const int y = (_buffer->firstLineOf(entryIndex) - verticalScrollBar()->value()) * _rowHeight;
    const int x = _header->sectionViewportPosition(MonitorBuffer::col_memo);
    const int w = _header->sectionSize(MonitorBuffer::col_memo);

    _editId = _buffer->entryId(entryIndex);
    _editor = new QLineEdit(viewport());
    _editor->setGeometry(x, y, w, _rowHeight);
    _editor->setText(_buffer->text(_buffer->entry(entryIndex), MonitorBuffer::col_memo));
    _editor->selectAll();
    _editor->show();
    _editor->setFocus();
    connect(_editor, &QLineEdit::editingFinished, this, [this]() { finishMemoEdit(true); });
}

void MonitorTable::finishMemoEdit(bool commit)
{
    if (!_editor) { return; }
    QLineEdit *editor = _editor;
    _editor = nullptr;
    if (commit) {
        const int index = _buffer->indexOfId(_editId);
        if (index >= 0) { _buffer->setMemo(index, editor->text()); }
    }
    editor->deleteLater();
    setFocus();
}
