TARGET = xcp_decoder_test
include(../common.pri)

SOURCES += \
    XcpDecoderTest.cpp \
    $$SRC_DIR/core/BusMessage.cpp \
    $$SRC_DIR/decoders/A2lXcpReader.cpp \
    $$SRC_DIR/decoders/XcpTables.cpp \
    $$SRC_DIR/decoders/XcpDecoder.cpp \
    $$SRC_DIR/decoders/XcpSessions.cpp \
    $$SRC_DIR/decoders/UdsDecoder.cpp \
    $$SRC_DIR/decoders/J1939Decoder.cpp \
    $$SRC_DIR/decoders/ProtocolManager.cpp

# XcpSessions is a QObject
HEADERS += \
    $$SRC_DIR/decoders/XcpSessions.h
