/*

  Copyright (c) 2026 jypark7

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

#pragma once

#include <QByteArray>
#include <QHash>
#include <QJsonObject>
#include <QObject>
#include <QString>

class Backend;
class QTcpServer;
class QTcpSocket;
class RemoteApi;

// Local remote control endpoint: newline-delimited JSON-RPC 2.0 over TCP,
// bound to 127.0.0.1 only. A client must call "hello" with the token first.
//
// The port and token are also written to <temp>/cangaroo-remote.json so a
// local client (the MCP server) can find them without extra configuration.
class RemoteServer : public QObject
{
    Q_OBJECT

public:
    struct Options
    {
        quint16 port = 0;            // 0: pick a free port
        QString token;               // empty: generate a random one
        bool allowTx = false;
        int frameLogCapacity = 100000;
    };

    RemoteServer(Backend &backend, const Options &options, QObject *parent = nullptr);
    ~RemoteServer() override;

    bool start();
    [[nodiscard]] quint16 port() const;
    [[nodiscard]] static QString discoveryFilePath();

private:
    struct Client
    {
        QByteArray buffer;
        bool authenticated = false;
    };

    Backend &_backend;
    Options _options;
    QTcpServer *_server;
    RemoteApi *_api;
    QHash<QTcpSocket *, Client> _clients;

    void onNewConnection();
    void onReadyRead(QTcpSocket *socket);
    void onDisconnected(QTcpSocket *socket);
    void handleLine(QTcpSocket *socket, const QByteArray &line);
    void writeDiscoveryFile() const;
    static void send(QTcpSocket *socket, const QJsonObject &obj);
    static void sendError(QTcpSocket *socket, const QJsonValue &id, int code, const QString &message);
};
