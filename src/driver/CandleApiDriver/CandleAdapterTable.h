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

#include <QList>
#include <QString>
#include <QStringList>

// Per-PC device numbers and aliases of Candlelight adapters, keyed by USB serial.
// cangaroo names interfaces candle<N>_ch<M>. Without this table N follows the
// order Windows lists the devices in, which can change between runs.
struct CandleAdapterEntry
{
    QString serial;     // USB serial as reported by the adapter
    int number = -1;    // fixed device number, -1 = automatic
    QString alias;      // shown next to the interface name, may be empty
};

namespace CandleAdapterTable
{
// %LOCALAPPDATA%/cangaroo/candle_adapters.json, or the file in CANGAROO_ADAPTERS
QString path();

// A missing file is an empty table, not an error
QList<CandleAdapterEntry> load(QString *error = nullptr);

// Keeps only entries with a number or an alias
bool save(const QList<CandleAdapterEntry> &entries, QString *error = nullptr);

// Candlelight 2.5 builds the serial from the 64-bit chip ID (16 hex digits) and
// appends the USB interface count, which changes with the firmware build. The
// chip ID part identifies the adapter.
QString key(const QString &serial);

const CandleAdapterEntry *find(const QList<CandleAdapterEntry> &entries, const QString &serial);

// Device numbers for the adapters with these serials, in the same order.
// An adapter in the table gets its number. The others get the lowest numbers
// that no table entry reserves (absent adapters keep theirs), in serial order;
// adapters without a serial come last, in list order.
QList<int> assignNumbers(const QStringList &serials, const QList<CandleAdapterEntry> &entries);
}
