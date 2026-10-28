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

// OBD-II on CAN decoder.
//
// Expected values are worked out by hand from SAE J1979 / ISO 15031-5, not taken
// from decoder output:
// - service 01 PID 0C engine speed = (256A + B) / 4 rpm: 1A F8 -> 6904 / 4 = 1726 rpm
// - PID 0D vehicle speed = A km/h; PID 05 coolant = A - 40 degC (7B -> 83)
// - PID 14 O2 sensor: A / 200 V, B * 100 / 128 - 100 % (5A 80 -> 0.45 V, 0 %)
// - PID 00 bitmask: bit 7 of A = PID 01 ... bit 0 of D = PID 20. BE 1F A8 13 ->
//   01 03 04 05 06 07 0C 0D 0E 0F 10 11 13 15 1C 1F 20
// - PID 01: A bit 7 = MIL, A bits 6..0 = DTC count (83 -> MIL on, 3)
// - PID A6 odometer: 32-bit / 10 km (00 01 E2 40 = 123456 -> 12345.6 km)
// - DTC: A bits 7..6 = P/C/B/U, then four hex digits (01 33 -> P0133, D0 16 -> U1016)
// - service 03 on CAN: 43, number of DTCs, two bytes per DTC
// - service 09 PID 02 on CAN: 49 02, number of data items (01), 17 VIN characters
// ISO-TP framing (ISO 15765-2): single frame 0L, first frame 1L LL, consecutive 2N,
// flow control 3x. OBD IDs (ISO 15765-4): 7DF, 7E0-7EF, 18DB33F1, 18DAxxF1, 18DAF1xx.

#include <initializer_list>

#include <QtTest>

#include "core/BusMessage.h"
#include "decoders/ObdDecoder.h"
#include "decoders/ProtocolManager.h"

namespace
{

BusMessage frame(uint32_t id, std::initializer_list<int> bytes, bool rx = true)
{
    BusMessage msg(id);
    if (id > 0x7FF) { msg.setExtended(true); }
    msg.setRX(rx);
    msg.setLength(static_cast<uint8_t>(bytes.size()));
    uint8_t i = 0;
    for (int b : bytes) { msg.setByte(i++, static_cast<uint8_t>(b)); }
    return msg;
}

} // namespace

class ObdDecoderTest : public QObject
{
    Q_OBJECT

private slots:
    void identifiers();
    void currentDataRequest();
    void engineSpeed();
    void singleBytePids();
    void multiPidResponse();
    void supportedPids();
    void monitorStatus();
    void oxygenSensor();
    void odometer();
    void freezeFrame();
    void storedDtcs();
    void clearDtcs();
    void negativeResponse();
    void vinMultiFrame();
    void udsOnSameIdStaysUds();
    void udsMultiFrameOnSameId();
    void notAnObdId();
    void extendedIds();
};

void ObdDecoderTest::identifiers()
{
    QVERIFY(ObdDecoder::isObdId(0x7DF, false));
    QVERIFY(ObdDecoder::isObdId(0x7E0, false));
    QVERIFY(ObdDecoder::isObdId(0x7EF, false));
    QVERIFY(!ObdDecoder::isObdId(0x7DE, false));
    QVERIFY(!ObdDecoder::isObdId(0x7F0, false));
    QVERIFY(ObdDecoder::isObdId(0x18DB33F1, true));
    QVERIFY(ObdDecoder::isObdId(0x18DA10F1, true));    // request to ECU 0x10
    QVERIFY(ObdDecoder::isObdId(0x18DAF110, true));    // response from ECU 0x10
    QVERIFY(!ObdDecoder::isObdId(0x18DAF9FE, true));   // UDS between other addresses
    QVERIFY(!ObdDecoder::isObdId(0x18FEF100, true));   // J1939 broadcast
    QVERIFY(!ObdDecoder::isObdId(0x7DF, true));        // 29-bit 0x7DF is not the OBD ID
}

void ObdDecoderTest::currentDataRequest()
{
    ObdDecoder obd;
    ProtocolMessage out;
    QCOMPARE(obd.tryDecode(frame(0x7DF, { 0x02, 0x01, 0x0C, 0x55, 0x55, 0x55, 0x55, 0x55 }, false), out),
             DecodeStatus::Completed);
    QCOMPARE(out.protocol, QString("obd2"));
    QCOMPARE(out.type, MessageType::Request);
    QCOMPARE(out.id, 0x010Cu);
    QCOMPARE(out.name, QString("Engine speed"));
    QCOMPARE(out.description, QString("request"));
    QCOMPARE(out.payload, QByteArray::fromHex("010C"));
}

void ObdDecoderTest::engineSpeed()
{
    ObdDecoder obd;
    ProtocolMessage out;
    QCOMPARE(obd.tryDecode(frame(0x7E8, { 0x04, 0x41, 0x0C, 0x1A, 0xF8, 0xAA, 0xAA, 0xAA }), out), DecodeStatus::Completed);
    QCOMPARE(out.type, MessageType::PositiveResponse);
    QCOMPARE(out.id, 0x010Cu);                          // same row key as the request
    QCOMPARE(out.name, QString("Engine speed"));
    QCOMPARE(out.description, QString("1726 rpm"));
    QVERIFY(out.metadata.isEmpty());

    // 1A F9 = 6905 / 4 = 1726.25
    QCOMPARE(obd.tryDecode(frame(0x7E8, { 0x04, 0x41, 0x0C, 0x1A, 0xF9 }), out), DecodeStatus::Completed);
    QCOMPARE(out.description, QString("1726.25 rpm"));
}

void ObdDecoderTest::singleBytePids()
{
    QCOMPARE(ObdDecoder::pidValue(0x0D, QByteArray::fromHex("32")), QString("50 km/h"));
    QCOMPARE(ObdDecoder::pidValue(0x05, QByteArray::fromHex("7B")), QString("83 °C"));
    QCOMPARE(ObdDecoder::pidValue(0x05, QByteArray::fromHex("00")), QString("-40 °C"));
    QCOMPARE(ObdDecoder::pidValue(0x04, QByteArray::fromHex("FF")), QString("100 %"));
    QCOMPARE(ObdDecoder::pidValue(0x06, QByteArray::fromHex("80")), QString("0 %"));    // 128 * 100 / 128 - 100
    QCOMPARE(ObdDecoder::pidValue(0x06, QByteArray::fromHex("00")), QString("-100 %"));
    QCOMPARE(ObdDecoder::pidValue(0x0E, QByteArray::fromHex("80")), QString("0 ° before TDC"));   // 128 / 2 - 64
    QCOMPARE(ObdDecoder::pidValue(0x10, QByteArray::fromHex("0190")), QString("4 g/s"));         // 400 / 100
    QCOMPARE(ObdDecoder::pidValue(0x42, QByteArray::fromHex("3039")), QString("12.345 V"));      // 12345 / 1000
    QCOMPARE(ObdDecoder::pidValue(0x1C, QByteArray::fromHex("06")), QString("EOBD"));
    QCOMPARE(ObdDecoder::pidValue(0x51, QByteArray::fromHex("04")), QString("diesel"));
    QCOMPARE(ObdDecoder::pidValue(0x0C, QByteArray::fromHex("1A")), QString("incomplete: 1A"));
}

void ObdDecoderTest::multiPidResponse()
{
    // A request for 0C and 0D answered in one response: 41 0C A B 0D A
    ObdDecoder obd;
    ProtocolMessage out;
    QCOMPARE(obd.tryDecode(frame(0x7E8, { 0x06, 0x41, 0x0C, 0x1A, 0xF8, 0x0D, 0x32, 0x00 }), out), DecodeStatus::Completed);
    QCOMPARE(out.name, QString("Engine speed, Vehicle speed"));
    QCOMPARE(out.description, QString("Engine speed: 1726 rpm; Vehicle speed: 50 km/h"));
    QCOMPARE(out.metadata.value("0C Engine speed").toString(), QString("1726 rpm"));
    QCOMPARE(out.metadata.value("0D Vehicle speed").toString(), QString("50 km/h"));
}

void ObdDecoderTest::supportedPids()
{
    ObdDecoder obd;
    ProtocolMessage out;
    QCOMPARE(obd.tryDecode(frame(0x7E8, { 0x06, 0x41, 0x00, 0xBE, 0x1F, 0xA8, 0x13, 0x00 }), out), DecodeStatus::Completed);
    QCOMPARE(out.id, 0x0100u);
    QCOMPARE(out.name, QString("Supported PIDs [01-20]"));
    QCOMPARE(out.description, QString("01 03 04 05 06 07 0C 0D 0E 0F 10 11 13 15 1C 1F 20"));
    // PID 20 lists 21..40
    QCOMPARE(ObdDecoder::pidValue(0x20, QByteArray::fromHex("80000001")), QString("21 40"));
}

void ObdDecoderTest::monitorStatus()
{
    QCOMPARE(ObdDecoder::pidValue(0x01, QByteArray::fromHex("83076500")), QString("MIL on, 3 DTC(s)"));
    QCOMPARE(ObdDecoder::pidValue(0x01, QByteArray::fromHex("00076500")), QString("MIL off, 0 DTC(s)"));
}

void ObdDecoderTest::oxygenSensor()
{
    QCOMPARE(ObdDecoder::pidValue(0x14, QByteArray::fromHex("5A80")), QString("0.45 V, trim 0 %"));
    QCOMPARE(ObdDecoder::pidValue(0x14, QByteArray::fromHex("5AFF")), QString("0.45 V, trim not used"));
}

void ObdDecoderTest::odometer()
{
    QCOMPARE(ObdDecoder::pidValue(0xA6, QByteArray::fromHex("0001E240")), QString("12345.6 km"));
}

void ObdDecoderTest::freezeFrame()
{
    // Service 02: PID, frame number, data
    ObdDecoder obd;
    ProtocolMessage out;
    QCOMPARE(obd.tryDecode(frame(0x7E8, { 0x05, 0x42, 0x0C, 0x00, 0x1A, 0xF8 }), out), DecodeStatus::Completed);
    QCOMPARE(out.id, 0x020Cu);
    QCOMPARE(out.name, QString("Engine speed (freeze frame 0)"));
    QCOMPARE(out.description, QString("1726 rpm"));
    // PID 02 of service 02 is the DTC that stored the freeze frame
    QCOMPARE(obd.tryDecode(frame(0x7E8, { 0x05, 0x42, 0x02, 0x00, 0x01, 0x33 }), out), DecodeStatus::Completed);
    QCOMPARE(out.description, QString("P0133"));
}

void ObdDecoderTest::storedDtcs()
{
    QCOMPARE(ObdDecoder::formatDtc(0x01, 0x33), QString("P0133"));
    QCOMPARE(ObdDecoder::formatDtc(0xD0, 0x16), QString("U1016"));
    QCOMPARE(ObdDecoder::formatDtc(0x41, 0x23), QString("C0123"));
    QCOMPARE(ObdDecoder::formatDtc(0x9A, 0xBC), QString("B1ABC"));

    ObdDecoder obd;
    ProtocolMessage out;
    QCOMPARE(obd.tryDecode(frame(0x7DF, { 0x01, 0x03 }, false), out), DecodeStatus::Completed);
    QCOMPARE(out.name, QString("Stored DTCs"));
    QCOMPARE(out.type, MessageType::Request);

    QCOMPARE(obd.tryDecode(frame(0x7E8, { 0x06, 0x43, 0x02, 0x01, 0x33, 0xD0, 0x16 }), out), DecodeStatus::Completed);
    QCOMPARE(out.id, 0x0300u);
    QCOMPARE(out.description, QString("2 DTC(s): P0133, U1016"));

    QCOMPARE(obd.tryDecode(frame(0x7E9, { 0x02, 0x47, 0x00 }), out), DecodeStatus::Completed);
    QCOMPARE(out.name, QString("Pending DTCs"));
    QCOMPARE(out.description, QString("no DTCs"));
}

void ObdDecoderTest::clearDtcs()
{
    ObdDecoder obd;
    ProtocolMessage out;
    QCOMPARE(obd.tryDecode(frame(0x7DF, { 0x01, 0x04 }, false), out), DecodeStatus::Completed);
    QCOMPARE(out.name, QString("Clear DTCs"));
    QCOMPARE(obd.tryDecode(frame(0x7E8, { 0x01, 0x44 }), out), DecodeStatus::Completed);
    QCOMPARE(out.type, MessageType::PositiveResponse);
    QCOMPARE(out.description, QString("DTCs and stored values cleared"));
}

void ObdDecoderTest::negativeResponse()
{
    ObdDecoder obd;
    ProtocolMessage out;
    QCOMPARE(obd.tryDecode(frame(0x7E8, { 0x03, 0x7F, 0x01, 0x12 }), out), DecodeStatus::Completed);
    QCOMPARE(out.type, MessageType::NegativeResponse);
    QCOMPARE(out.id, 0x7F01u);                          // not 0x0100, the row of PID 00
    QCOMPARE(out.name, QString("Current data"));
    QCOMPARE(out.description, QString("negative response: SubFunction Not Supported"));
    // A negative response to a UDS service is not OBD
    QCOMPARE(obd.tryDecode(frame(0x7E8, { 0x03, 0x7F, 0x22, 0x31 }), out), DecodeStatus::Ignored);
}

void ObdDecoderTest::vinMultiFrame()
{
    // 49 02 01 + "1G1JC5444R7252367" = 20 bytes: first frame (6) + 2 consecutive frames (7 + 7)
    ProtocolManager mgr;
    ProtocolMessage out;
    QCOMPARE(mgr.processFrame(frame(0x7DF, { 0x02, 0x09, 0x02 }, false), out), DecodeStatus::Completed);
    QCOMPARE(out.name, QString("VIN"));
    QCOMPARE(out.id, 0x0902u);

    QCOMPARE(mgr.processFrame(frame(0x7E8, { 0x10, 0x14, 0x49, 0x02, 0x01, '1', 'G', '1' }), out), DecodeStatus::Consumed);
    // The tester's flow control is not part of the response
    QCOMPARE(mgr.processFrame(frame(0x7E0, { 0x30, 0x00, 0x00 }, false), out), DecodeStatus::Ignored);
    QCOMPARE(mgr.processFrame(frame(0x7E8, { 0x21, 'J', 'C', '5', '4', '4', '4', 'R' }), out), DecodeStatus::Consumed);
    QCOMPARE(mgr.processFrame(frame(0x7E8, { 0x22, '7', '2', '5', '2', '3', '6', '7' }), out), DecodeStatus::Completed);
    QCOMPARE(out.protocol, QString("obd2"));
    QCOMPARE(out.name, QString("VIN"));
    QCOMPARE(out.description, QString("1G1JC5444R7252367"));
    QCOMPARE(out.payload.size(), 20);
    QCOMPARE(out.rawFrames.size(), 3);
}

void ObdDecoderTest::udsOnSameIdStaysUds()
{
    // UDS ReadDataByIdentifier on the OBD response ID goes to the UDS decoder
    ProtocolManager mgr;
    ProtocolMessage out;
    QCOMPARE(mgr.processFrame(frame(0x7E8, { 0x05, 0x62, 0xF1, 0x90, 0x12, 0x34 }), out), DecodeStatus::Completed);
    QCOMPARE(out.protocol, QString("uds"));
    QCOMPARE(out.id, 0x62u);
}

void ObdDecoderTest::udsMultiFrameOnSameId()
{
    // A UDS first frame on 0x7E8 must not leave an OBD session that eats the consecutive frames
    ProtocolManager mgr;
    ProtocolMessage out;
    QCOMPARE(mgr.processFrame(frame(0x7E8, { 0x10, 0x0A, 0x62, 0xF1, 0x90, 0x41, 0x42, 0x43 }), out), DecodeStatus::Consumed);
    QCOMPARE(mgr.processFrame(frame(0x7E8, { 0x21, 0x44, 0x45, 0x46, 0x47, 0x00, 0x00, 0x00 }), out), DecodeStatus::Completed);
    QCOMPARE(out.protocol, QString("uds"));
    QCOMPARE(out.payload.size(), 10);
}

void ObdDecoderTest::notAnObdId()
{
    ObdDecoder obd;
    ProtocolMessage out;
    QCOMPARE(obd.tryDecode(frame(0x123, { 0x02, 0x01, 0x0C }), out), DecodeStatus::Ignored);

    // The same payload on 0x123 is not decoded by anyone (UDS wants SID 0x10 and up)
    ProtocolManager mgr;
    QCOMPARE(mgr.processFrame(frame(0x123, { 0x02, 0x01, 0x0C }), out), DecodeStatus::Ignored);
}

void ObdDecoderTest::extendedIds()
{
    // 29-bit OBD response from ECU 0x10 is OBD, not J1939 PGN 0xDA00 and not UDS
    ProtocolManager mgr;
    ProtocolMessage out;
    QCOMPARE(mgr.processFrame(frame(0x18DB33F1, { 0x02, 0x01, 0x0D }, false), out), DecodeStatus::Completed);
    QCOMPARE(out.protocol, QString("obd2"));
    QCOMPARE(out.type, MessageType::Request);
    QCOMPARE(mgr.processFrame(frame(0x18DAF110, { 0x03, 0x41, 0x0D, 0x32 }), out), DecodeStatus::Completed);
    QCOMPARE(out.protocol, QString("obd2"));
    QCOMPARE(out.description, QString("50 km/h"));
}

QTEST_APPLESS_MAIN(ObdDecoderTest)
#include "ObdDecoderTest.moc"
