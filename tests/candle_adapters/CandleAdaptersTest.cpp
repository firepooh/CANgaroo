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

// Candle adapter table: device numbers per USB serial.
//
// Expected values follow the numbering rules agreed for the feature, not
// cangaroo output: a table number wins; adapters without one get the lowest
// number no table entry reserves, in serial order; adapters without a serial
// come last. Serials are matched on the 16-digit chip ID part, because
// Candlelight 2.5 appends the USB interface count (USBD_GetSerialNumber()).

#include <QtTest>

#include <QTemporaryDir>

#include "driver/CandleApiDriver/CandleAdapterTable.h"

namespace
{

const QString NO1 = QStringLiteral("207E3689514350052");
const QString NO2 = QStringLiteral("208A36AC514350062");
const QString NO3 = QStringLiteral("1F0011223344556672");

CandleAdapterEntry entry(const QString &serial, int number, const QString &alias = QString())
{
    CandleAdapterEntry e;
    e.serial = serial;
    e.number = number;
    e.alias = alias;
    return e;
}

} // namespace

class CandleAdaptersTest : public QObject
{
    Q_OBJECT

private slots:
    void withoutTableSerialOrder()
    {
        // Windows lists NO2 first; the names follow the serials instead
        QCOMPARE(CandleAdapterTable::assignNumbers({ NO2, NO1 }, {}), (QList<int>{ 1, 0 }));
        QCOMPARE(CandleAdapterTable::assignNumbers({ NO1, NO2 }, {}), (QList<int>{ 0, 1 }));
    }

    void tableNumbersWin()
    {
        const QList<CandleAdapterEntry> table = { entry(NO2, 0, "NO2"), entry(NO1, 1, "NO1") };
        QCOMPARE(CandleAdapterTable::assignNumbers({ NO1, NO2 }, table), (QList<int>{ 1, 0 }));
    }

    void autoSkipsReservedNumbers()
    {
        // NO1 has 0 but is not connected: NO2 (auto) must not take 0
        const QList<CandleAdapterEntry> table = { entry(NO1, 0) };
        QCOMPARE(CandleAdapterTable::assignNumbers({ NO2 }, table), (QList<int>{ 1 }));

        // NO3 is fixed to 1: the autos take 0 and 2, in serial order (NO1 < NO2)
        const QList<CandleAdapterEntry> table2 = { entry(NO3, 1) };
        QCOMPARE(CandleAdapterTable::assignNumbers({ NO2, NO3, NO1 }, table2), (QList<int>{ 2, 1, 0 }));
    }

    void aliasOnlyEntryIsAuto()
    {
        const QList<CandleAdapterEntry> table = { entry(NO2, -1, "NO2") };
        QCOMPARE(CandleAdapterTable::assignNumbers({ NO2, NO1 }, table), (QList<int>{ 1, 0 }));
    }

    void duplicateTableNumberFallsBackToAuto()
    {
        // A hand-edited file with 0 twice: the first adapter keeps it
        const QList<CandleAdapterEntry> table = { entry(NO1, 0), entry(NO2, 0) };
        QCOMPARE(CandleAdapterTable::assignNumbers({ NO1, NO2 }, table), (QList<int>{ 0, 1 }));
    }

    void adaptersWithoutSerialComeLast()
    {
        QCOMPARE(CandleAdapterTable::assignNumbers({ QString(), NO2, QString(), NO1 }, {}),
                 (QList<int>{ 2, 1, 3, 0 }));
    }

    void keyIgnoresInterfaceCountDigit()
    {
        QCOMPARE(CandleAdapterTable::key(NO1), QStringLiteral("207E368951435005"));
        // The same chip after a firmware build with another interface count
        const QList<CandleAdapterEntry> table = { entry(QStringLiteral("207E3689514350053"), 4) };
        QCOMPARE(CandleAdapterTable::assignNumbers({ NO1 }, table), (QList<int>{ 4 }));
        QVERIFY(CandleAdapterTable::find(table, NO1) != nullptr);
        QVERIFY(CandleAdapterTable::find(table, QString()) == nullptr);
    }

    void saveAndLoad()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        qputenv("CANGAROO_ADAPTERS", dir.filePath("sub/adapters.json").toUtf8());

        QString error;
        QVERIFY(CandleAdapterTable::load(&error).isEmpty());   // no file yet
        QVERIFY(error.isEmpty());

        const QList<CandleAdapterEntry> table = {
            entry(NO1, 0, "NO1"),
            entry(NO2, -1, "NO2"),
            entry(NO3, -1),                                     // nothing set: not saved
        };
        QVERIFY(CandleAdapterTable::save(table, &error));

        const QList<CandleAdapterEntry> loaded = CandleAdapterTable::load(&error);
        QVERIFY(error.isEmpty());
        QCOMPARE(loaded.size(), 2);
        QCOMPARE(loaded[0].serial, NO1);
        QCOMPARE(loaded[0].number, 0);
        QCOMPARE(loaded[0].alias, QStringLiteral("NO1"));
        QCOMPARE(loaded[1].serial, NO2);
        QCOMPARE(loaded[1].number, -1);

        QFile broken(dir.filePath("sub/adapters.json"));
        QVERIFY(broken.open(QIODevice::WriteOnly));
        broken.write("{ not json");
        broken.close();
        QVERIFY(CandleAdapterTable::load(&error).isEmpty());
        QVERIFY(!error.isEmpty());

        qunsetenv("CANGAROO_ADAPTERS");
    }
};

QTEST_GUILESS_MAIN(CandleAdaptersTest)

#include "CandleAdaptersTest.moc"
