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

#include <QWidget>

class Backend;
class MonitorBuffer;
class MonitorTable;
class QCheckBox;
class QComboBox;
class QDomDocument;
class QDomElement;
class QLineEdit;
class QToolButton;

// Toolbar and MonitorTable over a MonitorBuffer
class MonitorView : public QWidget
{
    Q_OBJECT
public:
    explicit MonitorView(Backend &backend, QWidget *parent = nullptr);

    void saveXML(QDomDocument &xml, QDomElement &el) const;
    void loadXML(const QDomElement &el);

signals:
    void settingsChanged();

private:
    Backend &_backend;
    MonitorBuffer *_buffer;
    MonitorTable *_table;
    QComboBox *_viewBox;      // Fixed / Scroll
    QComboBox *_timeBox;      // Delta / Relative
    QToolButton *_pauseButton;
    QToolButton *_filterButton;
    QCheckBox *_expandBox;
    QComboBox *_refreshBox;
    QComboBox *_findColumn;
    QLineEdit *_findText;

    void showHeaderMenu(const QPoint &pos);
    void openFilterDialog();
    void updateFilterButton();
    void updateSortIndicator();
};
