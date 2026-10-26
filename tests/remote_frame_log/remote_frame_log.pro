TARGET = remote_frame_log_test
include(../common.pri)

# RemoteFrameLog is a QObject; list its header so moc runs.
HEADERS += $$SRC_DIR/remote/RemoteFrameLog.h

SOURCES += \
    RemoteFrameLogTest.cpp \
    $$SRC_DIR/remote/RemoteFrameLog.cpp \
    $$SRC_DIR/remote/RemoteJson.cpp \
    $$SRC_DIR/core/BusMessage.cpp
