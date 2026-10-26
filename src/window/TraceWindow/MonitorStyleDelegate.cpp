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

#include "MonitorStyleDelegate.h"

#include <QApplication>
#include <QFontDatabase>
#include <QFontInfo>
#include <QHeaderView>
#include <QPainter>
#include <QTreeView>

#include "BaseTraceViewModel.h"
#include "core/ThemeManager.h"

namespace
{

QFont makeMonoFont(const QFont &uiFont)
{
    // Same choice as the Monitor window: Consolas, or the system fixed font without it
    QFont mono(QStringLiteral("Consolas"), uiFont.pointSize());
    mono.setStyleHint(QFont::TypeWriter);
    if (!QFontInfo(mono).fixedPitch()) {
        mono = QFontDatabase::systemFont(QFontDatabase::FixedFont);
        mono.setPointSize(uiFont.pointSize());
    }
    return mono;
}

Qt::Alignment alignmentOf(int column)
{
    switch (column) {
        case BaseTraceViewModel::column_index:
        case BaseTraceViewModel::column_timestamp:
            return Qt::AlignRight | Qt::AlignVCenter;
        case BaseTraceViewModel::column_direction:
        case BaseTraceViewModel::column_dlc:
            return Qt::AlignHCenter | Qt::AlignVCenter;
        default:
            return Qt::AlignLeft | Qt::AlignVCenter;
    }
}

} // namespace

MonitorStyleDelegate::MonitorStyleDelegate(QTreeView *view)
    : QStyledItemDelegate(view)
    , _monoFont(makeMonoFont(view->font()))
{
    const QFontMetrics ui(view->font());
    const QFontMetrics mono(_monoFont);
    _rowHeight = std::max(ui.height(), mono.height()) + 4;
}

void MonitorStyleDelegate::applyTo(QTreeView *tree)
{
    auto *delegate = new MonitorStyleDelegate(tree);
    tree->setItemDelegate(delegate);
    tree->setAlternatingRowColors(false);
    tree->setUniformRowHeights(true);
    tree->setIndentation(16);
    applyPalette(tree);

    QHeaderView *header = tree->header();
    header->setDefaultAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    header->setSectionsMovable(true);
    header->setStretchLastSection(true);

    // Monitor order: what was sent first, then where it came from
    const int order[] = {
        BaseTraceViewModel::column_index,
        BaseTraceViewModel::column_timestamp,
        BaseTraceViewModel::column_direction,
        BaseTraceViewModel::column_name,
        BaseTraceViewModel::column_canid,
        BaseTraceViewModel::column_dlc,
        BaseTraceViewModel::column_data,
        BaseTraceViewModel::column_channel,
        BaseTraceViewModel::column_sender,
        BaseTraceViewModel::column_type,
        BaseTraceViewModel::column_comment,
    };
    for (int visual = 0; visual < int(std::size(order)); ++visual) {
        header->moveSection(header->visualIndex(order[visual]), visual);
    }

    // Widths in characters, so they follow the font and DPI
    const int ch = QFontMetrics(tree->font()).averageCharWidth();
    const int monoCh = QFontMetrics(delegate->_monoFont).horizontalAdvance(QLatin1Char('0'));
    tree->setColumnWidth(BaseTraceViewModel::column_index, 10 * ch + 16);
    tree->setColumnWidth(BaseTraceViewModel::column_timestamp, 13 * ch);
    tree->setColumnWidth(BaseTraceViewModel::column_direction, 5 * ch);
    tree->setColumnWidth(BaseTraceViewModel::column_name, 24 * ch);
    tree->setColumnWidth(BaseTraceViewModel::column_canid, 13 * ch);
    tree->setColumnWidth(BaseTraceViewModel::column_dlc, 5 * ch);
    tree->setColumnWidth(BaseTraceViewModel::column_data, 48 * monoCh + 12);
    tree->setColumnWidth(BaseTraceViewModel::column_channel, 14 * ch);
    tree->setColumnWidth(BaseTraceViewModel::column_sender, 10 * ch);
    tree->setColumnWidth(BaseTraceViewModel::column_type, 12 * ch);
    tree->setColumnWidth(BaseTraceViewModel::column_comment, 20 * ch);

    // Never narrower than the title
    const QFontMetrics headerFm(header->font());
    for (int col = 0; col < header->count(); ++col) {
        const QString title = tree->model()->headerData(col, Qt::Horizontal).toString();
        const int min = headerFm.horizontalAdvance(title) + 2 * ch + 8;
        if (tree->columnWidth(col) < min) { tree->setColumnWidth(col, min); }
    }
}

void MonitorStyleDelegate::applyPalette(QTreeView *tree)
{
    // Light green selection with black text, as in the Monitor window; the dark
    // theme keeps its own highlight
    QPalette pal = QApplication::palette(tree);
    if (!ThemeManager::instance().isDarkMode()) {
        pal.setColor(QPalette::Highlight, QColor(200, 245, 195));
        pal.setColor(QPalette::HighlightedText, QColor(Qt::black));
    }
    tree->setPalette(pal);
}

QSize MonitorStyleDelegate::sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const
{
    return QSize(QStyledItemDelegate::sizeHint(option, index).width(), _rowHeight);
}

void MonitorStyleDelegate::initStyleOption(QStyleOptionViewItem *option, const QModelIndex &index) const
{
    QStyledItemDelegate::initStyleOption(option, index);
    option->displayAlignment = alignmentOf(index.column());

    switch (index.column()) {
        case BaseTraceViewModel::column_canid:
            // "0X601" and "0X7E0 (SID:22)" read as "601" and "7E0 (SID:22)"
            if (option->text.startsWith(QLatin1String("0x"), Qt::CaseInsensitive)) {
                option->text.remove(0, 2);
            }
            break;
        case BaseTraceViewModel::column_direction:
            // Only sent frames are marked, in blue like the Monitor window
            if (option->text == QLatin1String("TX")) {
                option->text = QStringLiteral("Tx");
                if (!(option->state & QStyle::State_Selected)) {
                    const bool dark = ThemeManager::instance().isDarkMode();
                    option->palette.setColor(QPalette::Text, dark ? QColor(110, 170, 255) : QColor(0, 80, 200));
                }
            } else {
                option->text.clear();
            }
            break;
        case BaseTraceViewModel::column_data:
            option->font = _monoFont;
            break;
        default:
            break;
    }
}

void MonitorStyleDelegate::paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const
{
    if (index.column() == BaseTraceViewModel::column_data) {
        paintData(painter, option, index);
    } else {
        QStyledItemDelegate::paint(painter, option, index);
    }
}

void MonitorStyleDelegate::paintData(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const
{
    // Every data cell is drawn here, changed bytes or not, so a line does not
    // shift when its change mask toggles (see DataColumnDelegate)
    QStyleOptionViewItem opt = option;
    initStyleOption(&opt, index);

    QStyle *style = opt.widget ? opt.widget->style() : QApplication::style();
    const QRect textRect = style->subElementRect(QStyle::SE_ItemViewItemText, &opt, opt.widget);
    const QString text = opt.text;
    opt.text.clear();
    style->drawControl(QStyle::CE_ItemViewItem, &opt, painter, opt.widget);
    if (text.isEmpty()) { return; }

    const bool selected = opt.state & QStyle::State_Selected;
    QColor normalColor = opt.palette.color(selected ? QPalette::HighlightedText : QPalette::Text);
    const QVariant fg = index.data(Qt::ForegroundRole);
    if (!selected && fg.canConvert<QColor>()) {
        normalColor = fg.value<QColor>();
    }

    const bool dark = ThemeManager::instance().isDarkMode();
    QColor changedColor = dark ? QColor(110, 170, 255) : QColor(0, 0, 255);
    changedColor.setAlpha(normalColor.alpha());

    const uint64_t changedMask = index.data(BaseTraceViewModel::ChangedBytesRole).value<uint64_t>();

    constexpr int drawFlags = Qt::AlignLeft | Qt::AlignVCenter | Qt::TextSingleLine;
    const int margin = style->pixelMetric(QStyle::PM_FocusFrameHMargin, nullptr, opt.widget) + 1;
    const QRect area = textRect.adjusted(margin, 0, -margin, 0);
    const QFontMetrics fm(_monoFont);

    painter->save();
    painter->setFont(_monoFont);
    painter->setClipRect(textRect);

    if (changedMask == 0) {
        painter->setPen(normalColor);
        painter->drawText(area, drawFlags, text);
        painter->restore();
        return;
    }

    // One token per byte ("AB CD EF" or "A B C"); the font is fixed-pitch, so a
    // token starts at the advance of everything before it
    int byteIndex = 0;
    qsizetype pos = 0;
    while (pos < text.size()) {
        qsizetype end = text.indexOf(QLatin1Char(' '), pos);
        if (end < 0) { end = text.size(); }
        if (end > pos) {
            const bool changed = byteIndex < 64 && ((changedMask >> byteIndex) & 1ULL);
            painter->setPen(changed ? changedColor : normalColor);
            const int x = area.left() + fm.horizontalAdvance(text.left(pos));
            painter->drawText(QRect(x, area.top(), area.right() - x + 1, area.height()),
                              drawFlags, text.mid(pos, end - pos));
            ++byteIndex;
        }
        pos = end + 1;
    }

    painter->restore();
}
