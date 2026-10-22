/*

  Copyright (c) 2026 Schildkroet

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

// gs_usb / Candlelight error frame decoding (CandleErrorFrame.h).
//
// Expected flags come from outside cangaroo:
//   - linux/can/error.h (CAN_ERR_* ID classes, CAN_ERR_CRTL_*, CAN_ERR_PROT_*,
//     CAN_ERR_PROT_LOC_CRC_SEQ = 0x08)
//   - Candlelight 2.5 firmware (Elmue): Firmware/Candlelight/candlelight_def.h
//     (ERID_*, ER1_*, ER2_*), Firmware/settings.h (eErrorAppFlags) and
//     Firmware/Candlelight/buffer.c buf_store_error(), which sets the ID classes
//     0x04/0x08 only for the legacy protocol
//   - Elmue's sample application, Candlelight::FormatCanErrors(), which reads
//     data[1], data[2] and data[5] of every ElmueSoft error message
// The ElmueSoft frames below were captured from a WeAct USB2CANFD V2 with
// firmware 0x261001 on a ValueCAN 4-2 bench (CAN_TerminalV2 .test/buserr).

#include <QtTest>

#include <array>
#include <cstdint>

#include "core/BusMessage.h"
#include "driver/CandleApiDriver/CandleErrorFrame.h"

namespace
{
    using Data = std::array<uint8_t, 8>;

    BusErrors decode(uint32_t errId, const Data &d, bool elmue, bool *rxLost = nullptr)
    {
        const CandleErrorFrame r = candleDecodeErrorFrame(errId, d.data(), elmue);
        if (rxLost) *rxLost = r.rxLost;
        return r.flags;
    }
}

Q_DECLARE_METATYPE(BusErrors)

class CandleErrorFrameTest : public QObject
{
    Q_OBJECT

private slots:
    void elmueFrames_data();
    void elmueFrames();
    void legacyNeedsIdClassForControllerAndProtocol();
    void legacyControllerAndProtocolWithIdClass();
    void legacyCrcLocation();
    void legacyIgnoresAppFlags();
    void crcIdBitInBothProtocols();
    void lostFramesAreReported();
    void counterOnlyReportIsGeneric();
};

void CandleErrorFrameTest::elmueFrames_data()
{
    QTest::addColumn<uint32_t>("errId");
    QTest::addColumn<QByteArray>("data");
    QTest::addColumn<BusErrors>("expected");

    // ERID_No_ACK_received, ER1_Tx_Passive_status_reached, TEC 128
    QTest::newRow("ack + tx passive")
        << uint32_t{0x20} << QByteArray::fromHex("0020000000008000")
        << BusErrors(BusError::Ack | BusError::ErrorPassive);
    // ... 500 ms later APP_CanTxTimeout (data[5] 0x10)
    QTest::newRow("ack + tx passive + tx timeout")
        << uint32_t{0x20} << QByteArray::fromHex("0020000000108000")
        << BusErrors(BusError::Ack | BusError::ErrorPassive | BusError::TxTimeout);
    // nominal bit rate mismatch: ER1_Rx_Passive_status_reached, ER2_Frame_format_error, REC 127
    QTest::newRow("rx passive + form")
        << uint32_t{0} << QByteArray::fromHex("001002000000007f")
        << BusErrors(BusError::ErrorPassive | BusError::Form);
    // CAN FD data bit rate mismatch: ER2_Bit_stuffing_error
    QTest::newRow("rx passive + stuff")
        << uint32_t{0} << QByteArray::fromHex("001004000000007f")
        << BusErrors(BusError::ErrorPassive | BusError::Stuff);
    // ER1_Tx_Errors_at_warning_level while TEC falls from 128
    QTest::newRow("tx warning")
        << uint32_t{0} << QByteArray::fromHex("0008000000007f00")
        << BusErrors(BusError::ErrorWarning);
    // ER1_Bus_is_back_active at TEC 90
    QTest::newRow("back to active")
        << uint32_t{0} << QByteArray::fromHex("0040000000005a00")
        << BusErrors(BusError::ErrorActive);
    // ERID_Bus_is_off with ER2_Bit_stuffing_error, TEC 248
    QTest::newRow("bus off + stuff")
        << uint32_t{0x40} << QByteArray::fromHex("00000400000000f8")
        << BusErrors(BusError::BusOff | BusError::Stuff);
    // ER2_Unable_to_send_recessive_bit / _dominant_bit and the plain single bit error
    QTest::newRow("bit1")
        << uint32_t{0} << QByteArray::fromHex("0000100000000000") << BusErrors(BusError::Bit);
    QTest::newRow("bit0")
        << uint32_t{0} << QByteArray::fromHex("0000080000000000") << BusErrors(BusError::Bit);
    QTest::newRow("single bit")
        << uint32_t{0} << QByteArray::fromHex("0000010000000000") << BusErrors(BusError::Bit);
    // APP_CanTxFail (data[5] 0x02, sending while bus off) has no cangaroo flag
    QTest::newRow("bus off + tx refused")
        << uint32_t{0x40} << QByteArray::fromHex("0000000000020000")
        << BusErrors(BusError::BusOff);
}

void CandleErrorFrameTest::elmueFrames()
{
    QFETCH(uint32_t, errId);
    QFETCH(QByteArray, data);
    QFETCH(BusErrors, expected);

    Data d{};
    std::copy(data.cbegin(), data.cend(), d.begin());
    QCOMPARE(decode(errId, d, true), expected);
}

void CandleErrorFrameTest::legacyNeedsIdClassForControllerAndProtocol()
{
    // Linux layout: data[1] / data[2] are only defined with CAN_ERR_CRTL / CAN_ERR_PROT
    const Data d{0x00, 0x20, 0x02, 0x00, 0x00, 0x00, 0x80, 0x00};
    QCOMPARE(decode(0x20, d, false), BusErrors(BusError::Ack));
}

void CandleErrorFrameTest::legacyControllerAndProtocolWithIdClass()
{
    // buf_store_error() in legacy mode: ERID_Controller_problem / ERID_Protocol_violation set
    const Data d{0x00, 0x10, 0x04, 0x00, 0x00, 0x00, 0x00, 0x7F};
    QCOMPARE(decode(0x04 | 0x08, d, false), BusErrors(BusError::ErrorPassive | BusError::Stuff));
}

void CandleErrorFrameTest::legacyCrcLocation()
{
    // CAN_ERR_PROT with location CAN_ERR_PROT_LOC_CRC_SEQ
    const Data d{0x00, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00, 0x00};
    QCOMPARE(decode(0x08, d, false), BusErrors(BusError::Crc));
}

void CandleErrorFrameTest::legacyIgnoresAppFlags()
{
    // data[5] is reserved in the Linux layout; legacy firmware clones the
    // timeout into ERID_Tx_Timeout instead
    const Data d{0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x00, 0x00};
    QCOMPARE(decode(0x00, d, false), BusErrors(BusError::Generic));
    QCOMPARE(decode(0x01, d, false), BusErrors(BusError::TxTimeout));
}

void CandleErrorFrameTest::crcIdBitInBothProtocols()
{
    // ERID_CRC_Error = 0x200, set by buf_store_error() for both protocols
    const Data d{};
    QCOMPARE(decode(0x200, d, true), BusErrors(BusError::Crc));
    QCOMPARE(decode(0x200, d, false), BusErrors(BusError::Crc));
}

void CandleErrorFrameTest::lostFramesAreReported()
{
    bool lost = false;

    // APP_CanRxFail: the device's CAN RX FIFO overflowed (captured at 12,000 frames/s)
    QCOMPARE(decode(0, Data{0, 0, 0, 0, 0, 0x01, 0, 0}, true, &lost), BusErrors(BusError::Overrun));
    QVERIFY(lost);

    // APP_UsbInOverflow: the host did not read USB in time
    QCOMPARE(decode(0, Data{0, 0, 0, 0, 0, 0x08, 0, 0}, true, &lost), BusErrors(BusError::Overrun));
    QVERIFY(lost);

    // legacy: ER1_Rx_Buffer_Overflow under ERID_Controller_problem
    QCOMPARE(decode(0x04, Data{0, 0x01, 0, 0, 0, 0, 0, 0}, false, &lost), BusErrors(BusError::Overrun));
    QVERIFY(lost);

    // ER1_Tx_Buffer_Overflow is an overrun, but no received frame was lost
    QCOMPARE(decode(0x04, Data{0, 0x02, 0, 0, 0, 0, 0, 0}, false, &lost), BusErrors(BusError::Overrun));
    QVERIFY(!lost);
}

void CandleErrorFrameTest::counterOnlyReportIsGeneric()
{
    // The firmware also reports when only the error counters changed (TEC 8)
    bool lost = true;
    QCOMPARE(decode(0, Data{0, 0, 0, 0, 0, 0, 0x08, 0}, true, &lost), BusErrors(BusError::Generic));
    QVERIFY(!lost);
}

QTEST_APPLESS_MAIN(CandleErrorFrameTest)
#include "CandleErrorFrameTest.moc"
