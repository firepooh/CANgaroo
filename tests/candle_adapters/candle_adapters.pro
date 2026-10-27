TARGET = candle_adapters_test
include(../common.pri)

# The adapter table is plain Qt; the Candle driver itself is not linked.
SOURCES += \
    CandleAdaptersTest.cpp \
    $$SRC_DIR/driver/CandleApiDriver/CandleAdapterTable.cpp
