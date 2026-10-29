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

// Expected values follow the numbered rules of GUI_DESIGN.md (umbrella repository),
// section 4.5: T-8 (what Find looks at), T-9 (wrapping, "3 / 12"), T-12 (Filter count)
// and V-1 (Scroll state from older workspaces).

#include <QtTest>

#include "window/MonitorWindow/TraceFind.h"

class TraceFindTest : public QObject
{
    Q_OBJECT

private slots:
    void textIgnoresCase();
    void dataIgnoresSpaces();
    void emptyTextMatchesNothing();
    void stepWrapsAround();
    void stepStartsAtAnEnd();
    void positionText();
    void filterCount();
    void legacyScrollMode();
};

void TraceFindTest::textIgnoresCase()
{
    // T-8: case-insensitive
    QVERIFY(TraceFind::matchesText("engine", "Engine speed"));
    QVERIFY(TraceFind::matchesText("7e8", "0x7E8 (41 0C)"));
    QVERIFY(!TraceFind::matchesText("brake", "Engine speed"));
}

void TraceFindTest::dataIgnoresSpaces()
{
    // T-8: Data ignores spaces on both sides
    QVERIFY(TraceFind::matchesData("62F1", "62 F1 90 4B"));
    QVERIFY(TraceFind::matchesData("62 f1 90", "62 F1 90 4B"));
    QVERIFY(TraceFind::matchesData("1904", "62 F1 90 4B"));     // across a byte boundary
    QVERIFY(!TraceFind::matchesData("F2", "62 F1 90 4B"));
}

void TraceFindTest::emptyTextMatchesNothing()
{
    QVERIFY(!TraceFind::matchesText(QString(), "anything"));
    QVERIFY(!TraceFind::matchesData("   ", "62 F1"));
}

void TraceFindTest::stepWrapsAround()
{
    // T-9: from the end back to the start, and the other way
    QCOMPARE(TraceFind::step(11, 12, true), 0);
    QCOMPARE(TraceFind::step(0, 12, false), 11);
    QCOMPARE(TraceFind::step(4, 12, true), 5);
    QCOMPARE(TraceFind::step(4, 12, false), 3);
    QCOMPARE(TraceFind::step(0, 0, true), -1);              // nothing to go to
}

void TraceFindTest::stepStartsAtAnEnd()
{
    // No row picked yet: ↓ goes to the first result, ↑ to the last
    QCOMPARE(TraceFind::step(-1, 12, true), 0);
    QCOMPARE(TraceFind::step(-1, 12, false), 11);
}

void TraceFindTest::positionText()
{
    // T-9: "3 / 12" in the Find box
    QCOMPARE(TraceFind::positionText(2, 12), QString("3 / 12"));
    QCOMPARE(TraceFind::positionText(-1, 12), QString("– / 12"));
    QCOMPARE(TraceFind::positionText(-1, 0), QString("– / 0"));
}

void TraceFindTest::filterCount()
{
    // T-12: one per condition of the tab
    QCOMPARE(TraceFind::filterCount(true, true, 0, 0, 0), 0);
    QCOMPARE(TraceFind::filterCount(false, true, 0, 0, 0), 1);
    QCOMPARE(TraceFind::filterCount(true, true, 2, 0, 0), 2);   // "Filter · 2"
    QCOMPARE(TraceFind::filterCount(false, false, 3, 1, 2), 8);
}

void TraceFindTest::legacyScrollMode()
{
    // V-1: protocol tabs wrote mode* = unified (Rolling Log) / aggregated,
    // the Monitor tab mode = scroll / fixed
    QVERIFY(TraceFind::scrollFromLegacyMode("unified"));
    QVERIFY(TraceFind::scrollFromLegacyMode("scroll"));
    QVERIFY(!TraceFind::scrollFromLegacyMode("aggregated"));
    QVERIFY(!TraceFind::scrollFromLegacyMode("fixed"));
    QVERIFY(!TraceFind::scrollFromLegacyMode(QString()));
}

QTEST_GUILESS_MAIN(TraceFindTest)
#include "TraceFindTest.moc"
