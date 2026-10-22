# Remote control endpoint (JSON-RPC over localhost TCP) used by the MCP server
# in mcp/. Enabled with: qmake6 CONFIG+=remote

QT += network
DEFINES += REMOTE_CONTROL

SOURCES += \
    $$PWD/RemoteApi.cpp \
    $$PWD/RemoteFrameLog.cpp \
    $$PWD/RemoteJson.cpp \
    $$PWD/RemoteServer.cpp

HEADERS += \
    $$PWD/RemoteApi.h \
    $$PWD/RemoteError.h \
    $$PWD/RemoteFrameLog.h \
    $$PWD/RemoteJson.h \
    $$PWD/RemoteServer.h
