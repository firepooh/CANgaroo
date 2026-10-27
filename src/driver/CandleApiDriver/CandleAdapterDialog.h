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

#include <QDialog>
#include <QList>

class BusInterface;
class CandleApiDriver;
class QLabel;
class QLineEdit;
class QPushButton;
class QSpinBox;
class QTableWidget;

// Lets the user give each Candlelight adapter a fixed device number (the N in
// candle<N>_ch<M>) and an alias, after finding it with the Identify blinking.
// Saved per PC in CandleAdapterTable; takes effect when the adapters are
// scanned again.
class CandleAdapterDialog : public QDialog
{
    Q_OBJECT

public:
    CandleAdapterDialog(CandleApiDriver &driver, QWidget *parent = nullptr);
    ~CandleAdapterDialog() override;

public slots:
    void accept() override;

private:
    struct Row
    {
        QString serial;
        BusInterface *intf = nullptr;   // channel 0, nullptr if not connected
        QSpinBox *number = nullptr;
        QLineEdit *alias = nullptr;
        QPushButton *identify = nullptr;
    };

    void addRow(const QString &serial, int number, const QString &alias, const QString &now, BusInterface *intf);

    QTableWidget *_table;
    QLabel *_error;
    QList<Row> _rows;
};
