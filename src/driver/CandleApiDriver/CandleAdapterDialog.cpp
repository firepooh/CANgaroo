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

#include "CandleAdapterDialog.h"

#include <QDialogButtonBox>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSet>
#include <QSpinBox>
#include <QTableWidget>
#include <QVBoxLayout>

#include "CandleAdapterTable.h"
#include "CandleApiDriver.h"
#include "driver/BusInterface.h"

namespace
{

enum Column { col_number, col_alias, col_serial, col_firmware, col_now, col_identify, col_count };

} // namespace

CandleAdapterDialog::CandleAdapterDialog(CandleApiDriver &driver, QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Assign Adapters"));

    auto *help = new QLabel(tr(
        "Give each adapter a fixed number (the N in candleN_ch0) and an alias. "
        "Use Blink to see which board a row is. Adapters set to 'auto' get the lowest "
        "free numbers in USB serial order. Numbers stay with the adapter on this PC "
        "and take effect when the adapters are scanned again (Refresh or restart)."), this);
    help->setWordWrap(true);

    _table = new QTableWidget(0, col_count, this);
    _table->setHorizontalHeaderLabels({ tr("Number"), tr("Alias"), tr("USB serial"), tr("Firmware"), tr("Now"), QString() });
    _table->verticalHeader()->hide();
    _table->setSelectionMode(QAbstractItemView::NoSelection);
    _table->horizontalHeader()->setSectionResizeMode(col_serial, QHeaderView::ResizeToContents);
    _table->horizontalHeader()->setSectionResizeMode(col_now, QHeaderView::Stretch);

    _error = new QLabel(this);
    _error->setStyleSheet(QStringLiteral("color: #c00000;"));
    _error->hide();

    auto *path = new QLabel(tr("Saved in %1").arg(CandleAdapterTable::path()), this);
    path->setTextInteractionFlags(Qt::TextSelectableByMouse);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &CandleAdapterDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &CandleAdapterDialog::reject);

    auto *layout = new QVBoxLayout(this);
    layout->addWidget(help);
    layout->addWidget(_table);
    layout->addWidget(_error);
    layout->addWidget(path);
    layout->addWidget(buttons);

    // Connected adapters with their current number, then table entries for
    // adapters that are not connected (their numbers stay reserved)
    QString loadError;
    const QList<CandleAdapterEntry> table = CandleAdapterTable::load(&loadError);
    if (!loadError.isEmpty())
    {
        _error->setText(tr("The saved table could not be read and will be replaced: %1").arg(loadError));
        _error->show();
    }

    QSet<QString> shown;
    for (const CandleApiDriver::Adapter &a : driver.adapters())
    {
        const CandleAdapterEntry *e = CandleAdapterTable::find(table, a.serial);
        QStringList names;
        for (BusInterface *intf : a.interfaces) { names.append(intf->getName()); }
        addRow(a.serial, e ? e->number : -1, a.alias, names.join(", "),
               a.interfaces.isEmpty() ? nullptr : a.interfaces.first());
        shown.insert(CandleAdapterTable::key(a.serial));
    }
    for (const CandleAdapterEntry &e : table)
    {
        if (!shown.contains(CandleAdapterTable::key(e.serial)))
        {
            addRow(e.serial, e.number, e.alias, tr("not connected"), nullptr);
        }
    }

    _table->resizeColumnsToContents();
    _table->horizontalHeader()->setSectionResizeMode(col_now, QHeaderView::Stretch);
    resize(760, 220 + 32 * _table->rowCount());
}

CandleAdapterDialog::~CandleAdapterDialog()
{
    // Stop the blinking started here
    for (const Row &r : _rows)
    {
        if (r.intf && r.identify && r.identify->isChecked()) { r.intf->identify(false); }
    }
}

void CandleAdapterDialog::addRow(const QString &serial, int number, const QString &alias, const QString &now,
                                 BusInterface *intf)
{
    const int row = _table->rowCount();
    _table->insertRow(row);

    Row r;
    r.serial = serial;
    r.intf = intf;

    r.number = new QSpinBox(_table);
    r.number->setRange(-1, 99);
    r.number->setSpecialValueText(tr("auto"));
    r.number->setValue(number);
    _table->setCellWidget(row, col_number, r.number);

    r.alias = new QLineEdit(alias, _table);
    r.alias->setPlaceholderText(tr("e.g. NO1"));
    _table->setCellWidget(row, col_alias, r.alias);

    auto *serialItem = new QTableWidgetItem(serial.isEmpty() ? tr("(no serial)") : serial);
    serialItem->setFlags(Qt::ItemIsEnabled);
    _table->setItem(row, col_serial, serialItem);

    auto *firmwareItem = new QTableWidgetItem(intf ? intf->getVersion() : QString());
    firmwareItem->setFlags(Qt::ItemIsEnabled);
    _table->setItem(row, col_firmware, firmwareItem);

    auto *nowItem = new QTableWidgetItem(now);
    nowItem->setFlags(Qt::ItemIsEnabled);
    _table->setItem(row, col_now, nowItem);

    r.identify = new QPushButton(tr("Blink"), _table);
    r.identify->setCheckable(true);
    const bool canBlink = intf && (intf->getCapabilities() & BusInterface::capability_identify);
    r.identify->setEnabled(canBlink);
    if (intf)
    {
        QPushButton *button = r.identify;
        connect(button, &QPushButton::toggled, this, [this, intf, button](bool on)
        {
            if (!intf->identify(on))
            {
                const QSignalBlocker block(button);
                button->setChecked(false);
                QMessageBox::warning(this, tr("Identify"),
                                     tr("The adapter did not accept the identify request. See the log for details."));
            }
        });
    }
    _table->setCellWidget(row, col_identify, r.identify);

    // An adapter without a serial cannot be recognised again
    if (serial.isEmpty())
    {
        r.number->setEnabled(false);
        r.alias->setEnabled(false);
    }
    _rows.append(r);
}

void CandleAdapterDialog::accept()
{
    QList<CandleAdapterEntry> entries;
    QSet<int> numbers;
    for (const Row &r : _rows)
    {
        if (r.serial.isEmpty()) { continue; }
        CandleAdapterEntry e;
        e.serial = r.serial;
        e.number = r.number->value();
        e.alias = r.alias->text().trimmed();
        if (e.number >= 0)
        {
            if (numbers.contains(e.number))
            {
                _error->setText(tr("Number %1 is used twice.").arg(e.number));
                _error->show();
                return;
            }
            numbers.insert(e.number);
        }
        entries.append(e);
    }

    QString error;
    if (!CandleAdapterTable::save(entries, &error))
    {
        _error->setText(error);
        _error->show();
        return;
    }
    QDialog::accept();
}
