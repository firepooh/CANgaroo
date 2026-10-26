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

#include "RemoteServer.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QPointer>
#include <QRandomGenerator>
#include <QTcpServer>
#include <QTcpSocket>

#include "core/Backend.h"
#include "core/Log.h"
#include "remote/RemoteApi.h"
#include "remote/RemoteError.h"

namespace
{
    constexpr qsizetype k_maxLineBytes = 1024 * 1024;

    QString randomToken()
    {
        QString token;
        for (int i = 0; i < 4; ++i)
        {
            token += QString("%1").arg(QRandomGenerator::system()->generate(), 8, 16, QChar('0'));
        }
        return token;
    }

    // Compares without an early exit so the time does not depend on the match length.
    bool sameToken(const QString &a, const QString &b)
    {
        const QByteArray x = a.toUtf8();
        const QByteArray y = b.toUtf8();
        if (x.size() != y.size()) { return false; }
        unsigned char diff = 0;
        for (qsizetype i = 0; i < x.size(); ++i)
        {
            diff |= static_cast<unsigned char>(x[i] ^ y[i]);
        }
        return diff == 0;
    }
}

RemoteServer::RemoteServer(Backend &backend, const Options &options, QObject *parent)
    : QObject(parent),
      _backend(backend),
      _options(options),
      _server(new QTcpServer(this)),
      _api(new RemoteApi(backend, options.allowTx, options.frameLogCapacity, this))
{
    if (_options.token.isEmpty())
    {
        _options.token = randomToken();
    }
    connect(_server, &QTcpServer::newConnection, this, &RemoteServer::onNewConnection);
}

RemoteServer::~RemoteServer()
{
    // Only remove the file this instance wrote: a second cangaroo whose port was
    // taken must not delete the discovery file of the one that is listening.
    if (!_wroteDiscoveryFile) { return; }
    QFile file(discoveryFilePath());
    if (file.open(QIODevice::ReadOnly))
    {
        const QJsonObject o = QJsonDocument::fromJson(file.readAll()).object();
        file.close();
        if (o.value("pid").toInteger() != QCoreApplication::applicationPid()) { return; }
    }
    QFile::remove(discoveryFilePath());
}

bool RemoteServer::isListening() const
{
    return _server->isListening();
}

void RemoteServer::setAllowTx(bool allowTx)
{
    _options.allowTx = allowTx;
    _api->setAllowTx(allowTx);
    if (_server->isListening())
    {
        writeDiscoveryFile();
        log_info(tr("Remote: transmit %1").arg(allowTx ? tr("allowed") : tr("blocked")));
    }
}

QString RemoteServer::discoveryFilePath()
{
    return QDir(QDir::tempPath()).filePath("cangaroo-remote.json");
}

bool RemoteServer::start()
{
    if (!_server->listen(QHostAddress::LocalHost, _options.port))
    {
        log_error(tr("Remote: cannot listen on 127.0.0.1:%1: %2").arg(_options.port).arg(_server->errorString()));
        return false;
    }

    writeDiscoveryFile();
    log_info(tr("Remote: listening on 127.0.0.1:%1, transmit %2")
                 .arg(port())
                 .arg(_options.allowTx ? tr("allowed") : tr("blocked")));
    return true;
}

quint16 RemoteServer::port() const
{
    return _server->serverPort();
}

void RemoteServer::writeDiscoveryFile()
{
    _wroteDiscoveryFile = true;
    QJsonObject o;
    o["host"]     = "127.0.0.1";
    o["port"]     = port();
    o["token"]    = _options.token;
    o["pid"]      = static_cast<qint64>(QCoreApplication::applicationPid());
    o["allow_tx"] = _options.allowTx;

    QFile file(discoveryFilePath());
    if (file.open(QIODevice::WriteOnly | QIODevice::Truncate))
    {
        file.write(QJsonDocument(o).toJson(QJsonDocument::Indented));
        file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    }
    else
    {
        log_warning(tr("Remote: cannot write %1").arg(discoveryFilePath()));
    }
}

void RemoteServer::onNewConnection()
{
    while (QTcpSocket *socket = _server->nextPendingConnection())
    {
        _clients.insert(socket, Client{});
        connect(socket, &QTcpSocket::readyRead, this, [this, socket]() { onReadyRead(socket); });
        connect(socket, &QTcpSocket::disconnected, this, [this, socket]() { onDisconnected(socket); });
    }
}

void RemoteServer::onDisconnected(QTcpSocket *socket)
{
    _api->cancelPending(socket);
    _clients.remove(socket);
    socket->deleteLater();
}

void RemoteServer::onReadyRead(QTcpSocket *socket)
{
    auto it = _clients.find(socket);
    if (it == _clients.end()) { return; }

    it->buffer += socket->readAll();
    qsizetype newline;
    while ((newline = it->buffer.indexOf('\n')) >= 0)
    {
        const QByteArray line = it->buffer.left(newline).trimmed();
        it->buffer.remove(0, newline + 1);
        if (!line.isEmpty())
        {
            handleLine(socket, line);
            it = _clients.find(socket);   // handleLine may close the connection
            if (it == _clients.end()) { return; }
        }
    }

    if (it->buffer.size() > k_maxLineBytes)
    {
        sendError(socket, QJsonValue(), RemoteError::InvalidRequest, "request line too long");
        socket->disconnectFromHost();
    }
}

void RemoteServer::handleLine(QTcpSocket *socket, const QByteArray &line)
{
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(line, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject())
    {
        sendError(socket, QJsonValue(), RemoteError::ParseError, "invalid JSON");
        return;
    }

    const QJsonObject request = doc.object();
    const QJsonValue id = request.value("id");
    const QString method = request.value("method").toString();
    const QJsonObject params = request.value("params").toObject();
    if (method.isEmpty())
    {
        sendError(socket, id, RemoteError::InvalidRequest, "missing 'method'");
        return;
    }

    Client &client = _clients[socket];
    if (method == "hello")
    {
        if (!sameToken(params.value("token").toString(), _options.token))
        {
            log_warning(tr("Remote: rejected client with a wrong token"));
            sendError(socket, id, RemoteError::Unauthorized, "wrong token");
            socket->disconnectFromHost();
            return;
        }
        client.authenticated = true;
    }
    else if (!client.authenticated)
    {
        sendError(socket, id, RemoteError::Unauthorized, "call 'hello' with the token first");
        return;
    }

    // Async methods may reply after the client is gone: guard the socket.
    QPointer<QTcpSocket> guard(socket);
    _api->setCurrentOwner(socket);
    _api->call(method, params, [guard, id](const QJsonValue &result, const RemoteError *error)
    {
        if (!guard) { return; }
        if (error)
        {
            sendError(guard, id, error->code(), error->message());
            return;
        }
        QJsonObject response;
        response["jsonrpc"] = "2.0";
        response["id"] = id;
        response["result"] = result;
        send(guard, response);
    });
    _api->setCurrentOwner(nullptr);
}

void RemoteServer::send(QTcpSocket *socket, const QJsonObject &obj)
{
    socket->write(QJsonDocument(obj).toJson(QJsonDocument::Compact));
    socket->write("\n");
}

void RemoteServer::sendError(QTcpSocket *socket, const QJsonValue &id, int code, const QString &message)
{
    QJsonObject error;
    error["code"] = code;
    error["message"] = message;

    QJsonObject response;
    response["jsonrpc"] = "2.0";
    response["id"] = id.isUndefined() ? QJsonValue() : id;
    response["error"] = error;
    send(socket, response);
}
