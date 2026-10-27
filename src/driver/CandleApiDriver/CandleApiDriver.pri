QMAKE_CFLAGS += -std=gnu11

SOURCES += \
    $$PWD/CandleApiDriver.cpp \
    $$PWD/CandleApiInterface.cpp \
    $$PWD/CandleSharedDevice.cpp \
    $$PWD/CandleAdapterTable.cpp \
    $$PWD/CandleAdapterDialog.cpp \
    $$PWD/api/candle.c \
    $$PWD/api/candle_ctrl_req.c \
    $$PWD/api/candle_elmue.c \
    $$PWD/CandleApiTiming.cpp

#win32:LIBS += -L$$PWD/api/ -lcandle_api
win32: LIBS += -lSetupApi
win32: LIBS += -lOle32
win32: LIBS += -lwinusb
win32: LIBS += -lcfgmgr32

HEADERS += \
    $$PWD/api/candle.h \
    $$PWD/CandleApiDriver.h \
    $$PWD/CandleApiInterface.h \
    $$PWD/CandleErrorFrame.h \
    $$PWD/CandleSharedDevice.h \
    $$PWD/CandleAdapterTable.h \
    $$PWD/CandleAdapterDialog.h \
    $$PWD/api/candle_ctrl_req.h \
    $$PWD/api/candle_elmue.h \
    $$PWD/api/candle_defs.h \
    $$PWD/api/ch_9.h \
    $$PWD/CandleApiTiming.h

FORMS +=
