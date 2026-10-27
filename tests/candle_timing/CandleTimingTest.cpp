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

// Computed nominal bit timings for the 160 MHz Candlelight 2.5 (STM32G431).
//
// Expected values come from the bit timing definition, not from cangaroo:
//   total tq = 1 (sync) + 1 (prop) + phase_seg1 + phase_seg2
//   bitrate = clock / (BRP * total tq), sample point = (2 + phase_seg1) / total tq
// and the STM32G4 FDCAN nominal limits (RM0440 FDCAN_NBTP: NTSEG1 1..256,
// NTSEG2 1..128, NBRP 1..512). Hand-checked rows: 250k 80 % = BRP 2, 320 tq,
// 1 + 1 + 254 + 64 (CiA 601 example layout scaled to 160 MHz); 500k 80 % = the
// existing table row BRP 2, 126 / 32.

#include <QtTest>

#include "driver/CandleApiDriver/CandleTimingCalc.h"

namespace
{

constexpr uint32_t k_clock = 160000000;
const std::vector<uint32_t> k_bitrates = { 10000, 20000, 50000, 83333, 100000, 125000, 250000, 500000, 800000, 1000000 };
const std::vector<uint32_t> k_samplePoints = { 750, 800, 850, 875 };

} // namespace

class CandleTimingTest : public QObject
{
    Q_OBJECT

private slots:
    void everyCombinationExists()
    {
        for (uint32_t br : k_bitrates) {
            for (uint32_t sp : k_samplePoints) {
                QVERIFY2(CandleTimingCalc::nominal(k_clock, br, sp).has_value(),
                         qPrintable(QString("%1 bit/s %2").arg(br).arg(sp)));
            }
        }
    }

    void bitrateSamplePointAndLimits()
    {
        for (uint32_t br : k_bitrates) {
            for (uint32_t sp : k_samplePoints) {
                const auto t = CandleTimingCalc::nominal(k_clock, br, sp);
                QVERIFY(t);
                const uint32_t tq = 2 + t->phaseSeg1 + t->phaseSeg2;
                const double actual = double(k_clock) / (double(t->brp) * tq);
                QVERIFY2(std::fabs(actual - br) < 1.0, qPrintable(QString("%1 -> %2").arg(br).arg(actual)));
                QCOMPARE((2 + t->phaseSeg1) * 1000, sp * tq);
                QVERIFY(1 + t->phaseSeg1 <= 256);
                QVERIFY(t->phaseSeg2 >= 1 && t->phaseSeg2 <= 128);
                QVERIFY(t->brp >= 1 && t->brp <= 512);
                QVERIFY(tq >= 8);
            }
        }
    }

    void brp2WhenItFits()
    {
        // 250k 80 %: BRP 2 gives 320 tq, 1 + 1 + 254 + 64 (TSEG1 = 255 <= 256)
        auto t = CandleTimingCalc::nominal(k_clock, 250000, 800);
        QVERIFY(t);
        QCOMPARE(t->brp, 2u);
        QCOMPARE(t->phaseSeg1, 254u);
        QCOMPARE(t->phaseSeg2, 64u);

        // 250k 87.5 % at BRP 2 needs TSEG1 279 > 256: BRP 4, 160 tq, 138 / 20
        t = CandleTimingCalc::nominal(k_clock, 250000, 875);
        QVERIFY(t);
        QCOMPARE(t->brp, 4u);
        QCOMPARE(t->phaseSeg1, 138u);
        QCOMPARE(t->phaseSeg2, 20u);

        // 500k 80 % equals the hand-written table row
        t = CandleTimingCalc::nominal(k_clock, 500000, 800);
        QVERIFY(t);
        QCOMPARE(t->brp, 2u);
        QCOMPARE(t->phaseSeg1, 126u);
        QCOMPARE(t->phaseSeg2, 32u);

        // 1M 85 %: BRP 2, 80 tq, 2 + 66 = 68 -> 85 %
        t = CandleTimingCalc::nominal(k_clock, 1000000, 850);
        QVERIFY(t);
        QCOMPARE(t->brp, 2u);
        QCOMPARE(t->phaseSeg1, 66u);
        QCOMPARE(t->phaseSeg2, 12u);
    }

    void impossibleRequests()
    {
        QVERIFY(!CandleTimingCalc::nominal(k_clock, 500000, 0));
        QVERIFY(!CandleTimingCalc::nominal(k_clock, 500000, 1000));
        QVERIFY(!CandleTimingCalc::nominal(k_clock, 0, 800));
        // 160 MHz / 10 Mbit/s = 16 tq at BRP 1 (BRP 2 gives 8): 80 % needs 12.8 tq before the sample point
        QVERIFY(!CandleTimingCalc::nominal(k_clock, 10000000, 800));
        // A bitrate the clock cannot divide: the nearest, 160 MHz / 160 tq = 1 000 000, is 1 bit/s off
        QVERIFY(!CandleTimingCalc::nominal(k_clock, 999999, 875));
    }

    void missingKeepsExistingRows()
    {
        const std::vector<std::pair<uint32_t, uint32_t>> existing = {
            { 250000, 875 }, { 500000, 875 }, { 500000, 800 },
        };
        const auto added = CandleTimingCalc::missing(k_clock, { 250000, 500000 }, k_samplePoints, existing);
        QList<QPair<uint32_t, uint32_t>> pairs;
        for (const auto &t : added) { pairs.append({ t.bitrate, t.samplePoint }); }
        const QList<QPair<uint32_t, uint32_t>> expected = {
            { 250000, 750 }, { 250000, 800 }, { 250000, 850 },
            { 500000, 750 }, { 500000, 850 },
        };
        QCOMPARE(pairs, expected);
    }

    // 80 MHz (CANnectivity, ESP32-S31 TWAI-FD), limits TSEG1 190, phase_seg2 63, BRP 255
    void clock80MHzWithinTwaiLimits()
    {
        constexpr uint32_t clock = 80000000;
        const CandleTimingLimits &lim = CandleTimingCalc::k_80MHzLimits;
        for (uint32_t br : k_bitrates) {
            for (uint32_t sp : k_samplePoints) {
                const auto t = CandleTimingCalc::nominal(clock, br, sp, lim);
                // 800k 87.5 %: 80 MHz / 800k = 100 tq per bit, 87.5 % needs a multiple of 8 tq
                if (br == 800000 && sp == 875) {
                    QVERIFY(!t.has_value());
                    continue;
                }
                QVERIFY2(t.has_value(), qPrintable(QString("%1 bit/s %2").arg(br).arg(sp)));
                const uint32_t tq = 2 + t->phaseSeg1 + t->phaseSeg2;
                const double actual = double(clock) / (double(t->brp) * tq);
                QVERIFY2(std::fabs(actual - br) < 1.0, qPrintable(QString("%1 -> %2").arg(br).arg(actual)));
                QCOMPARE((2 + t->phaseSeg1) * 1000, sp * tq);
                QVERIFY(1 + t->phaseSeg1 <= 190);
                QVERIFY(t->phaseSeg2 >= 1 && t->phaseSeg2 <= 63);
                QVERIFY(t->brp >= 1 && t->brp <= 255);
            }
        }
    }

    void clock80MHzBenchRows()
    {
        // 500k 80 % (bench nominal rate): BRP 2, 80 tq, 1 + 1 + 62 + 16
        auto t = CandleTimingCalc::nominal(80000000, 500000, 800, CandleTimingCalc::k_80MHzLimits);
        QVERIFY(t);
        QCOMPARE(t->brp, 2u);
        QCOMPARE(t->phaseSeg1, 62u);
        QCOMPARE(t->phaseSeg2, 16u);

        // 10k 80 %: BRP 32 gives 250 tq, but TSEG1 199 > 190, so BRP 40, 200 tq, 158 / 40
        t = CandleTimingCalc::nominal(80000000, 10000, 800, CandleTimingCalc::k_80MHzLimits);
        QVERIFY(t);
        QCOMPARE(t->brp, 40u);
        QCOMPARE(t->phaseSeg1, 158u);
        QCOMPARE(t->phaseSeg2, 40u);
    }
};

QTEST_APPLESS_MAIN(CandleTimingTest)

#include "CandleTimingTest.moc"
