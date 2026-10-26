TARGET = decoders_test
include(../common.pri)

SOURCES += \
    DecodersTest.cpp \
    $$SRC_DIR/core/BusMessage.cpp \
    $$SRC_DIR/decoders/UdsDecoder.cpp \
    $$SRC_DIR/decoders/J1939Decoder.cpp \
    $$SRC_DIR/decoders/ProtocolManager.cpp \
    $$SRC_DIR/decoders/XcpTables.cpp \
    $$SRC_DIR/decoders/XcpDecoder.cpp \
    $$SRC_DIR/decoders/XcpSessions.cpp

# ProtocolManager routes XCP sessions first; XcpSessions is a QObject
HEADERS += \
    $$SRC_DIR/decoders/XcpSessions.h
