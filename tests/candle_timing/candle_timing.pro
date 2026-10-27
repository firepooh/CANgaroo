TARGET = candle_timing_test
include(../common.pri)

# The timing calculator is plain C++; the Candle driver itself is not linked.
SOURCES += \
    CandleTimingTest.cpp \
    $$SRC_DIR/driver/CandleApiDriver/CandleTimingCalc.cpp
