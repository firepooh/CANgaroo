TARGET = candle_error_frame_test
include(../common.pri)

# CandleErrorFrame.h is header-only; it needs BusMessage only.
SOURCES += \
    CandleErrorFrameTest.cpp \
    $$SRC_DIR/core/BusMessage.cpp
