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

#include <vector>

#include <QAbstractScrollArea>
#include <QColor>
#include <QFont>
#include <QFontMetrics>
#include <QRawFont>
#include <QSet>
#include <QTimer>

#include "MonitorBuffer.h"

class MonitorHeaderModel;
class QHeaderView;
class QLineEdit;
class QPainter;

// Virtual list over MonitorBuffer. Only the visible lines are painted, one
// line at a time without per-cell item delegates, and at most once per
// buffer batch. Columns come from a QHeaderView (movable, resizable).
class MonitorTable : public QAbstractScrollArea
{
    Q_OBJECT
public:
    explicit MonitorTable(MonitorBuffer *buffer, QWidget *parent = nullptr);

    [[nodiscard]] QHeaderView *header() const { return _header; }
    void applyDefaultColumnWidths();
    // Selects the next entry whose column contains text; returns false when there is none
    bool find(int column, const QString &text, bool forward);

protected:
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void showEvent(QShowEvent *event) override;
    void scrollContentsBy(int dx, int dy) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    bool viewportEvent(QEvent *event) override;

private:
    MonitorBuffer *_buffer;
    QHeaderView *_header;
    MonitorHeaderModel *_headerModel;
    QFont _monoFont;
    QRawFont _uiRawFont;
    QRawFont _monoRawFont;
    QSet<const MonitorBuffer::Entry *> _touched;
    int _monoCharWidth = 8;
    int _rowHeight = 20;
    bool _hasSelection = false;
    uint64_t _selectedId = 0;
    QLineEdit *_editor = nullptr;
    uint64_t _editId = 0;
    QTimer _activityTimer;

    // Paint statistics, logged every 5 s when CANGAROO_MONITOR_STATS=1
    bool _statsEnabled = false;
    int _statsPaints = 0;
    qint64 _statsTotalNs = 0;
    qint64 _statsMaxNs = 0;
    qint64 _statsFillNs = 0;
    qint64 _statsMessageNs = 0;
    qint64 _statsChildNs = 0;
    mutable qint64 _statsDrawTextNs = 0;
    mutable int _statsDrawTextCalls = 0;
    QTimer _statsTimer;

    void onBufferChanged(int removedFrontLines);
    void updateMetrics();
    void updateGeometries();
    void updateScrollBars();
    [[nodiscard]] int visibleLines() const;
    [[nodiscard]] int lineAt(int y) const;
    [[nodiscard]] int firstVisibleColumnX() const;
    void selectEntry(int entryIndex, bool center);
    void startMemoEdit(int entryIndex);
    void finishMemoEdit(bool commit);

    struct ColumnRect
    {
        int column;
        int x;
        int width;
    };
    [[nodiscard]] std::vector<ColumnRect> visibleColumns() const;

    // Per-paint values shared by all lines, plus the painter's current font and pen
    struct PaintContext
    {
        QPainter &p;
        std::vector<ColumnRect> cols;
        int firstX;
        int width;
        int64_t nowUs;
        QFont uiFont;
        QFontMetrics uiFm;
        int uiBaseline;
        int monoBaseline;
        QColor text;
        QColor selectedText;
        QColor error;
        QColor changed;
        QColor tx;
        QColor branch;
        const QFont *currentFont = nullptr;
        QRgb currentPen = 0;
        bool penValid = false;

        void setFont(const QFont &font);
        void setPen(const QColor &color);
    };

    void paintMessageLine(PaintContext &ctx, const MonitorBuffer::Entry &e, int y, bool selected);
    void paintChildLine(PaintContext &ctx, const MonitorBuffer::Entry &e, int childLine, int y, bool selected);
    void drawText(PaintContext &ctx, const QPoint &pos, const QString &text, bool mono) const;
    void drawCell(PaintContext &ctx, int x, int y, int width, const QString &text, int align, bool mono) const;
    void drawHex(PaintContext &ctx, int x, int y, int width, const QString &hex, uint64_t mask,
                 const QColor &normal, bool elide) const;
};
