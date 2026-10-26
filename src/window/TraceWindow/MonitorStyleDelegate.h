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

#include <QFont>
#include <QStyledItemDelegate>

class QTreeView;

// Draws the trace trees like the Monitor window: UI font, compact lines, IDs
// without "0x", "Tx" only on sent frames, and data bytes in a fixed-pitch font
// with the bytes that changed since the previous frame in blue.
class MonitorStyleDelegate : public QStyledItemDelegate
{
    Q_OBJECT
public:
    explicit MonitorStyleDelegate(QTreeView *view);

    QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const override;
    void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const override;

    // Font, palette, header and column order of the Monitor window for one trace tree
    static void applyTo(QTreeView *tree);
    // Selection colours; called again when the theme changes
    static void applyPalette(QTreeView *tree);

protected:
    void initStyleOption(QStyleOptionViewItem *option, const QModelIndex &index) const override;

private:
    void paintData(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const;

    QFont _monoFont;
    int _rowHeight;
};
