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

// XCP on CAN decoder (ported from CanTerminal's XcpDecoder.cs).
//
// Expected values follow the packet layouts of ASAM MCD-1 XCP Part 2 (protocol
// layer), not decoder output: CONNECT response = FF RESOURCE COMM_MODE_BASIC
// MAX_CTO MAX_DTO(word) PROTOCOL_VERSION TRANSPORT_VERSION, with
// COMM_MODE_BASIC bit 0 = byte order (1 = Motorola) and bits 2..1 = address
// granularity; ALLOC_DAQ = D5 reserved DAQ_COUNT(word); ALLOC_ODT = D4 reserved
// DAQ_LIST_NUMBER(word) ODT_COUNT; GET_DAQ_PROCESSOR_INFO response byte 7 =
// DAQ_KEY_BYTE with the identification field type in bits 7..6; error code
// 0x20 = ERR_CMD_UNKNOWN; event code 0x06 = EV_DAQ_OVERLOAD.

#include <initializer_list>

#include <QtTest>

#include "core/BusMessage.h"
#include "decoders/ProtocolManager.h"
#include "decoders/XcpDecoder.h"
#include "decoders/XcpSessions.h"

namespace
{

constexpr uint32_t request_id = 0x601;
constexpr uint32_t response_id = 0x701;

BusMessage frame(uint32_t id, std::initializer_list<int> bytes, BusInterfaceId itf = 0)
{
    BusMessage msg(id);
    msg.setInterfaceId(itf);
    msg.setLength(static_cast<uint8_t>(bytes.size()));
    uint8_t i = 0;
    for (int b : bytes) { msg.setByte(i++, static_cast<uint8_t>(b)); }
    return msg;
}

XcpConfig session()
{
    XcpConfig c;
    c.interfaceId = 0;
    c.requestId = request_id;
    c.responseId = response_id;
    return c;
}

XcpAnnotation decode(XcpDecoder &xcp, const BusMessage &f)
{
    XcpAnnotation a;
    if (!xcp.decode(f, a)) { a.type = QStringLiteral("<not decoded>"); }
    return a;
}

} // namespace

class XcpDecoderTest : public QObject
{
    Q_OBJECT

private slots:
    void connectCommandAndIntelResponse();
    void motorolaByteOrderFromConnect();
    void absoluteOdtFromAllocDaqAndAllocOdt();
    void connectResetsTheDaqConfiguration();
    void relativeOdtWithDaqByte();
    void relativeOdtWithAlignedDaqWord();
    void errorNamesTheCommandItAnswers();
    void eventDoesNotConsumeThePendingCommand();
    void downloadMaxHasNoElementCount();
    void stimFromMasterWithoutContext();
    void ignoresOtherIdsInterfacesAndFrameFormats();
    void protocolManagerPrefersXcpOverUds();
};

void XcpDecoderTest::connectCommandAndIntelResponse()
{
    XcpDecoder xcp(session());

    const XcpAnnotation cmd = decode(xcp, frame(request_id, {0xFF, 0x00}));
    QCOMPARE(cmd.type, QStringLiteral("CTO (CONNECT)"));
    QCOMPARE(cmd.comment, QStringLiteral("MODE = 0x00 (normal)"));
    QVERIFY(cmd.fromMaster);
    QCOMPARE(cmd.kind, MessageType::Request);
    QCOMPARE(cmd.group, 0);

    // RESOURCE 0x15 = CAL/PAG (bit 0), DAQ (bit 2), PGM (bit 4); COMM_MODE_BASIC 0xC0:
    // Intel, granularity BYTE; MAX_CTO 8; MAX_DTO 0x0008 little-endian
    const XcpAnnotation rsp = decode(xcp, frame(response_id, {0xFF, 0x15, 0xC0, 0x08, 0x08, 0x00, 0x01, 0x01}));
    QCOMPARE(rsp.type, QStringLiteral("CTO (OK + INFO)"));
    QVERIFY(!rsp.fromMaster);
    QCOMPARE(rsp.kind, MessageType::PositiveResponse);
    QCOMPARE(rsp.comment, QStringLiteral("CONNECT: RESOURCE = 0x15 (CAL/PAG,DAQ,PGM)|COMM_MODE_BASIC = 0xC0 (Intel, AG=1)"
                                         "|MAX_CTO = 8|MAX_DTO = 8|PROTOCOL_LAYER = 0x01|TRANSPORT_LAYER = 0x01"));
}

void XcpDecoderTest::motorolaByteOrderFromConnect()
{
    XcpDecoder xcp(session());
    decode(xcp, frame(request_id, {0xFF, 0x00}));
    // COMM_MODE_BASIC bit 0 set: MAX_DTO 00 08 is 8 in Motorola order
    const XcpAnnotation rsp = decode(xcp, frame(response_id, {0xFF, 0x04, 0x01, 0x08, 0x00, 0x08, 0x01, 0x01}));
    QVERIFY(rsp.comment.contains(QStringLiteral("COMM_MODE_BASIC = 0x01 (Motorola, AG=1)")));
    QVERIFY(rsp.comment.contains(QStringLiteral("MAX_DTO = 8|")));

    // Every later word follows that order: DAQ_COUNT 00 02 = 2
    const XcpAnnotation alloc = decode(xcp, frame(request_id, {0xD5, 0x00, 0x00, 0x02}));
    QCOMPARE(alloc.type, QStringLiteral("CTO (ALLOC_DAQ)"));
    QCOMPARE(alloc.comment, QStringLiteral("DAQ_COUNT = 0x0002"));
}

void XcpDecoderTest::absoluteOdtFromAllocDaqAndAllocOdt()
{
    XcpDecoder xcp(session());
    decode(xcp, frame(request_id, {0xFF, 0x00}));
    decode(xcp, frame(response_id, {0xFF, 0x04, 0x00, 0x08, 0x08, 0x00, 0x01, 0x01}));
    decode(xcp, frame(request_id, {0xD6}));                          // FREE_DAQ
    decode(xcp, frame(response_id, {0xFF}));
    decode(xcp, frame(request_id, {0xD5, 0x00, 0x02, 0x00}));        // ALLOC_DAQ: 2 lists
    decode(xcp, frame(response_id, {0xFF}));
    decode(xcp, frame(request_id, {0xD4, 0x00, 0x00, 0x00, 0x03}));  // ALLOC_ODT: list 0, 3 ODTs
    decode(xcp, frame(response_id, {0xFF}));
    decode(xcp, frame(request_id, {0xD4, 0x00, 0x01, 0x00, 0x02}));  // ALLOC_ODT: list 1, 2 ODTs
    decode(xcp, frame(response_id, {0xFF}));

    // Absolute ODT numbers run across the lists: 0..2 = list 0, 3..4 = list 1
    const XcpAnnotation first = decode(xcp, frame(response_id, {0x00, 0x11, 0x22}));
    QCOMPARE(first.type, QStringLiteral("DAQ-DTO (DAQ #0|ODT #0)"));
    QCOMPARE(first.comment, QStringLiteral("Data length: 3"));
    QCOMPARE(first.group, 1);
    QVERIFY(!first.fromMaster);

    const XcpAnnotation second = decode(xcp, frame(response_id, {0x03, 0x11}));
    QCOMPARE(second.type, QStringLiteral("DAQ-DTO (DAQ #1|ODT #0)"));
    QCOMPARE(second.group, 4);

    const XcpAnnotation outside = decode(xcp, frame(response_id, {0x05, 0x11}));
    QCOMPARE(outside.type, QStringLiteral("DAQ-DTO (PID = 0x05)"));
    QCOMPARE(outside.comment, QStringLiteral("Data length: 2|PID outside the allocated ODT range"));
}

void XcpDecoderTest::connectResetsTheDaqConfiguration()
{
    XcpDecoder xcp(session());
    decode(xcp, frame(request_id, {0xD5, 0x00, 0x01, 0x00}));
    decode(xcp, frame(request_id, {0xD4, 0x00, 0x00, 0x00, 0x02}));
    QCOMPARE(decode(xcp, frame(response_id, {0x01, 0x00})).type, QStringLiteral("DAQ-DTO (DAQ #0|ODT #1)"));

    // A new CONNECT starts a new session: the old allocation no longer applies
    decode(xcp, frame(request_id, {0xFF, 0x00}));
    const XcpAnnotation dto = decode(xcp, frame(response_id, {0x01, 0x00}));
    QCOMPARE(dto.type, QStringLiteral("DAQ-DTO (PID = 0x01)"));
    QVERIFY(dto.comment.endsWith(QStringLiteral("DAQ allocation not seen in capture")));
}

void XcpDecoderTest::relativeOdtWithDaqByte()
{
    XcpDecoder xcp(session());
    decode(xcp, frame(request_id, {0xDA}));   // GET_DAQ_PROCESSOR_INFO
    // DAQ_KEY_BYTE 0x40: identification field type 1 = relative ODT + absolute DAQ (byte)
    decode(xcp, frame(response_id, {0xFF, 0x00, 0x04, 0x00, 0x01, 0x00, 0x00, 0x40}));
    const XcpAnnotation dto = decode(xcp, frame(response_id, {0x02, 0x01, 0xAA}));
    QCOMPARE(dto.type, QStringLiteral("DAQ-DTO (DAQ #1|ODT #2)"));
}

// Ported from CanTerminal's regression test: type 3 is [PID][FILL][DAQ word]
void XcpDecoderTest::relativeOdtWithAlignedDaqWord()
{
    XcpDecoder xcp(session());
    decode(xcp, frame(request_id, {0xFF, 0x00}));
    decode(xcp, frame(response_id, {0xFF, 0x00, 0x00, 0x08, 0xFF, 0x00, 0x00, 0x00}));
    decode(xcp, frame(request_id, {0xDA}));
    const XcpAnnotation info = decode(xcp, frame(response_id, {0xFF, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xC0}));
    QVERIFY(info.comment.contains(QStringLiteral("DAQ_KEY_BYTE = 0xC0 (id field: relative ODT + DAQ (word, aligned))")));
    const XcpAnnotation dto = decode(xcp, frame(response_id, {0x00, 0x00, 0x01, 0x00, 0xAA, 0xBB, 0xCC, 0xDD}));
    QCOMPARE(dto.type, QStringLiteral("DAQ-DTO (DAQ #1|ODT #0)"));
}

void XcpDecoderTest::errorNamesTheCommandItAnswers()
{
    XcpDecoder xcp(session());
    // TRANSPORT_LAYER_CMD / GET_SLAVE_ID with the "XCP" magic, identify by echo
    const XcpAnnotation cmd = decode(xcp, frame(request_id, {0xF2, 0xFF, 'X', 'C', 'P', 0x00}));
    QCOMPARE(cmd.type, QStringLiteral("CTO (GET_SLAVE_ID)"));
    QCOMPARE(cmd.comment, QStringLiteral("MAGIC = 'XCP'|MODE = 0x00 (identify by echo)"));

    const XcpAnnotation err = decode(xcp, frame(response_id, {0xFE, 0x20}));
    QCOMPARE(err.type, QStringLiteral("CTO (ERR)"));
    QCOMPARE(err.kind, MessageType::NegativeResponse);
    QCOMPARE(err.comment, QStringLiteral("ERR_CMD_UNKNOWN (0x20) ← GET_SLAVE_ID"));
}

void XcpDecoderTest::eventDoesNotConsumeThePendingCommand()
{
    XcpDecoder xcp(session());
    decode(xcp, frame(request_id, {0xFD}));   // GET_STATUS

    const XcpAnnotation ev = decode(xcp, frame(response_id, {0xFD, 0x06}));
    QCOMPARE(ev.type, QStringLiteral("CTO (EV)"));
    QCOMPARE(ev.comment, QStringLiteral("EV_DAQ_OVERLOAD (0x06)"));

    // The response after the event still answers GET_STATUS:
    // SESSION_STATUS 0x40 = DAQ_RUNNING, protection 0x05 = CAL/PAG + DAQ, configuration id 0x1234 (Intel)
    const XcpAnnotation rsp = decode(xcp, frame(response_id, {0xFF, 0x40, 0x05, 0x00, 0x34, 0x12}));
    QCOMPARE(rsp.comment, QStringLiteral("GET_STATUS: SESSION_STATUS = 0x40 (DAQ_RUNNING)|RESOURCE_PROTECTION = 0x05 (CAL/PAG,DAQ)"
                                         "|SESSION_CONFIGURATION_ID = 0x1234"));
}

// Ported from CanTerminal's regression test: DOWNLOAD_MAX's payload is the rest of the CTO
void XcpDecoderTest::downloadMaxHasNoElementCount()
{
    XcpDecoder xcp(session());
    const XcpAnnotation cto = decode(xcp, frame(request_id, {0xEE, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77}));
    QCOMPARE(cto.type, QStringLiteral("CTO (DOWNLOAD_MAX)"));
    QCOMPARE(cto.comment, QStringLiteral("DATA = 11223344556677"));
}

void XcpDecoderTest::stimFromMasterWithoutContext()
{
    XcpDecoder xcp(session());
    // Master PIDs below 0xC0 are STIM data, not commands
    const XcpAnnotation stim = decode(xcp, frame(request_id, {0x10, 0x01, 0x02}));
    QCOMPARE(stim.type, QStringLiteral("STIM-DTO (PID = 0x10)"));
    QVERIFY(stim.fromMaster);
    QCOMPARE(stim.kind, MessageType::Request);
    QCOMPARE(stim.group, 0x11);
}

void XcpDecoderTest::ignoresOtherIdsInterfacesAndFrameFormats()
{
    XcpDecoder xcp(session());
    XcpAnnotation a;
    QVERIFY(!xcp.decode(frame(0x602, {0xFF, 0x00}), a));
    QVERIFY(!xcp.decode(frame(request_id, {0xFF, 0x00}, 1), a));   // same ID on another interface

    BusMessage extended = frame(request_id, {0xFF, 0x00});
    extended.setExtended(true);
    QVERIFY(!xcp.decode(extended, a));

    BusMessage error = frame(request_id, {0xFF, 0x00});
    error.setErrorFrame(true);
    QVERIFY(!xcp.decode(error, a));
}

void XcpDecoderTest::protocolManagerPrefersXcpOverUds()
{
    // 0x7E0 / 02 10 01 is a UDS DiagnosticSessionControl single frame ...
    const BusMessage f = frame(0x7E0, {0x02, 0x10, 0x01});
    ProtocolMessage out;

    XcpSessions::instance().clear();
    ProtocolManager plain;
    QCOMPARE(plain.processFrame(f, out), DecodeStatus::Completed);
    QCOMPARE(out.protocol.toLower(), QStringLiteral("uds"));

    // ... until 0x7E0 is configured as an XCP request ID: then it is an XCP STIM packet
    XcpConfig c;
    c.requestId = 0x7E0;
    c.responseId = 0x7E8;
    XcpSessions::instance().set(c);
    ProtocolManager withXcp;
    out = ProtocolMessage();
    QCOMPARE(withXcp.processFrame(f, out), DecodeStatus::Completed);
    QCOMPARE(out.protocol, QStringLiteral("XCP"));
    QCOMPARE(out.name, QStringLiteral("STIM-DTO (PID = 0x02)"));
    XcpSessions::instance().clear();
}

QTEST_APPLESS_MAIN(XcpDecoderTest)

#include "XcpDecoderTest.moc"
