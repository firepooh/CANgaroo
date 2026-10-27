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

#include "McpController.h"

#include <algorithm>
#include <utility>

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QHostAddress>
#include <QProcess>
#include <QProcessEnvironment>
#include <QSettings>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>

#include "core/Backend.h"
#include "core/Log.h"

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace
{
    constexpr quint16 k_defaultHttpPort = 5400;      // the old CanTerminal MCP address
    constexpr quint16 k_defaultRemotePort = 52790;
    constexpr int k_defaultRemoteBuffer = 100000;
    constexpr int k_readyCheckMs = 500;
    constexpr int k_readyTimeoutChecks = 40;         // 20 s: the first start imports a lot
    constexpr qsizetype k_outputTailChars = 4000;

    QStringList mcpDirs()
    {
        const QString app = QCoreApplication::applicationDirPath();
        return { app + "/../mcp", app + "/mcp" };    // dist/ or bin/ next to mcp/, or a copy inside
    }

    QString firstExisting(const QStringList &candidates)
    {
        for (const QString &path : candidates)
        {
            if (QFileInfo::exists(path))
            {
                return QDir::toNativeSeparators(QFileInfo(path).canonicalFilePath());
            }
        }
        return {};
    }

    bool portFree(quint16 port)
    {
        QTcpServer probe;
        const bool ok = probe.listen(QHostAddress::LocalHost, port);
        probe.close();
        return ok;
    }

    QString quoted(const QString &path)
    {
        return path.contains(' ') ? QStringLiteral("\"%1\"").arg(path) : path;
    }
}

McpController::McpController(Backend &backend, QObject *parent)
    : QObject(parent),
      _backend(backend),
      _readyTimer(new QTimer(this))
{
    QSettings settings;
    _enabled = settings.value("mcp/enabled", true).toBool();
    _readOnly = settings.value("mcp/readOnly", false).toBool();

    _readyTimer->setInterval(k_readyCheckMs);
    connect(_readyTimer, &QTimer::timeout, this, &McpController::checkHttpReady);
}

McpController::~McpController()
{
    shutdown();
}

void McpController::startup(const CommandLine &commandLine)
{
    _commandLine = commandLine;
    if (_commandLine.remote)
    {
        // The command line decides this run; nothing is written to the settings.
        // Transmitting keeps the AI Read-only setting (default: allowed) unless
        // the command line says so explicitly.
        _enabled = true;
        if (_commandLine.readOnly) { _readOnly = true; }
        else if (_commandLine.options.allowTx) { _readOnly = false; }
    }
    if (_enabled)
    {
        start();
    }
    emit stateChanged();
}

void McpController::shutdown()
{
    stopHttp();
    _remote.reset();
}

void McpController::setEnabled(bool enabled)
{
    if (enabled == _enabled && (enabled == static_cast<bool>(_remote)))
    {
        return;
    }
    _enabled = enabled;
    QSettings().setValue("mcp/enabled", enabled);
    if (enabled)
    {
        start();
    }
    else
    {
        stop();
        log_info(tr("MCP: disabled"));
    }
    emit stateChanged();
}

void McpController::setReadOnly(bool readOnly)
{
    _readOnly = readOnly;
    QSettings().setValue("mcp/readOnly", readOnly);
    if (_remote)
    {
        _remote->setAllowTx(!readOnly);
    }
    emit stateChanged();
}

bool McpController::remoteListening() const
{
    return _remote && _remote->isListening();
}

quint16 McpController::remotePort() const
{
    return _remote ? _remote->port() : 0;
}

quint16 McpController::httpPort() const
{
    const int port = QSettings().value("mcp/httpPort", k_defaultHttpPort).toInt();
    return static_cast<quint16>(port > 0 && port < 65536 ? port : k_defaultHttpPort);
}

QString McpController::endpoint() const
{
    return QStringLiteral("http://127.0.0.1:%1/mcp").arg(httpPort());
}

QString McpController::registrationCommand() const
{
    return QStringLiteral("claude mcp add --transport http cangaroo %1").arg(endpoint());
}

QString McpController::stdioRegistrationCommand() const
{
    const QString py = pythonPath();
    const QString script = scriptPath();
    return QStringLiteral("claude mcp add cangaroo -- %1 %2")
        .arg(quoted(py.isEmpty() ? QStringLiteral("<mcp>\\.venv\\Scripts\\python.exe") : py),
             quoted(script.isEmpty() ? QStringLiteral("<mcp>\\cangaroo_mcp.py") : script));
}

QString McpController::pythonPath() const
{
    const QString configured = QSettings().value("mcp/python").toString();
    if (!configured.isEmpty() && QFileInfo::exists(configured))
    {
        return QDir::toNativeSeparators(configured);
    }
    QStringList candidates;
    for (const QString &dir : mcpDirs())
    {
#ifdef Q_OS_WIN
        candidates << dir + "/.venv/Scripts/python.exe";
#else
        candidates << dir + "/.venv/bin/python";
#endif
    }
    return firstExisting(candidates);
}

QString McpController::scriptPath() const
{
    const QString configured = QSettings().value("mcp/script").toString();
    if (!configured.isEmpty() && QFileInfo::exists(configured))
    {
        return QDir::toNativeSeparators(configured);
    }
    QStringList candidates;
    for (const QString &dir : mcpDirs())
    {
        candidates << dir + "/cangaroo_mcp.py";
    }
    return firstExisting(candidates);
}

QString McpController::statusText() const
{
    if (!_enabled)
    {
        return tr("MCP: off");
    }
    if (!remoteListening())
    {
        return tr("MCP: remote port in use");
    }

    QString text = _readOnly ? tr("MCP: on (read-only)") : tr("MCP: on");
    if (_commandLine.remote)
    {
        return text + tr(" · command line, TCP %1").arg(remotePort());
    }
    switch (_httpState)
    {
        case HttpState::Running:       return text + tr(" · HTTP %1").arg(httpPort());
        case HttpState::Starting:      return text + tr(" · HTTP %1 starting").arg(httpPort());
        case HttpState::PortInUse:     return text + tr(" · HTTP %1 in use").arg(httpPort());
        case HttpState::PythonMissing: return text + tr(" · MCP server not found");
        case HttpState::Failed:        return text + tr(" · HTTP failed");
        case HttpState::Off:           break;
    }
    return text;
}

void McpController::start()
{
    if (!_remote)
    {
        RemoteServer::Options options;
        if (_commandLine.remote)
        {
            options = _commandLine.options;
        }
        else
        {
            QSettings settings;
            const int port = settings.value("mcp/remotePort", k_defaultRemotePort).toInt();
            options.port = static_cast<quint16>(port >= 0 && port < 65536 ? port : k_defaultRemotePort);
            options.frameLogCapacity = std::max(1000, settings.value("mcp/remoteBuffer", k_defaultRemoteBuffer).toInt());
        }
        options.allowTx = !_readOnly;

        _remote = std::make_unique<RemoteServer>(_backend, options);
        if (!_remote->start())
        {
            _remote.reset();
            setHttpState(HttpState::Off,
                         tr("Remote control port %1 is in use. Is another cangaroo running with MCP enabled?")
                             .arg(options.port));
            log_warning(tr("MCP: %1").arg(_problem));
            return;
        }
    }

    if (_commandLine.remote)
    {
        // Whoever passed --remote runs its own MCP server (tests, start_canterminal_compat.cmd).
        setHttpState(HttpState::Off);
        return;
    }
    startHttp();
}

void McpController::stop()
{
    stopHttp();
    _remote.reset();
    _problem.clear();
}

void McpController::startHttp()
{
    if (_http)
    {
        return;
    }
    const quint16 port = httpPort();
    const QString py = pythonPath();
    const QString script = scriptPath();
    if (py.isEmpty() || script.isEmpty())
    {
        setHttpState(HttpState::PythonMissing,
                     tr("MCP server not found. Expected mcp\\.venv\\Scripts\\python.exe and mcp\\cangaroo_mcp.py "
                        "next to the program folder (%1). Create the venv as described in mcp\\README.md.")
                         .arg(QDir::toNativeSeparators(QCoreApplication::applicationDirPath())));
        log_warning(tr("MCP: %1").arg(_problem));
        return;
    }
    if (!portFree(port))
    {
        setHttpState(HttpState::PortInUse,
                     tr("HTTP port %1 is already in use (another cangaroo, or the old CanTerminal?).").arg(port));
        log_warning(tr("MCP: %1").arg(_problem));
        return;
    }

    _outputTail.clear();
    _http = new QProcess(this);
    _http->setProgram(py);
    _http->setArguments({ script, "--transport", "http", "--port", QString::number(port) });
    _http->setWorkingDirectory(QFileInfo(script).absolutePath());
    _http->setProcessChannelMode(QProcess::MergedChannels);

    // Attach to this instance, not to whatever the discovery file points at.
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    // PythonEngine puts PYTHONHOME of the embedded Python into this process; the
    // venv interpreter (another version) dies at startup if it inherits it.
    for (const char *name : { "PYTHONHOME", "PYTHONPATH", "PYTHONEXECUTABLE", "PYTHONSTARTUP" })
    {
        env.remove(QString::fromLatin1(name));
    }
    env.insert("CANGAROO_HOST", "127.0.0.1");
    env.insert("CANGAROO_PORT", QString::number(_remote->port()));
    env.insert("CANGAROO_TOKEN", _remote->token());
    env.insert("PYTHONUNBUFFERED", "1");
    env.insert("PYTHONIOENCODING", "utf-8");
    _http->setProcessEnvironment(env);

#ifdef Q_OS_WIN
    // python.exe is a console program: without this a console window pops up.
    _http->setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments *args)
    {
        args->flags |= CREATE_NO_WINDOW;
    });
#endif

    QProcess *process = _http;
    connect(process, &QProcess::readyReadStandardOutput, this, [this, process]()
    {
        _outputTail += QString::fromUtf8(process->readAllStandardOutput());
        if (_outputTail.size() > k_outputTailChars)
        {
            _outputTail = _outputTail.right(k_outputTailChars);
        }
        // uvicorn: "Uvicorn running on http://127.0.0.1:5400"
        if (_httpState == HttpState::Starting && _outputTail.contains("running on", Qt::CaseInsensitive))
        {
            _readyTimer->stop();
            setHttpState(HttpState::Running);
            log_info(tr("MCP: HTTP server on %1 (register: %2)").arg(endpoint(), registrationCommand()));
        }
    });
    connect(process, &QProcess::finished, this, [this, process](int exitCode, QProcess::ExitStatus)
    {
        _readyTimer->stop();
        _http = nullptr;
        process->deleteLater();
        const QString tail = _outputTail.trimmed().right(800);
        setHttpState(HttpState::Failed, tr("MCP server exited (code %1). %2").arg(exitCode).arg(tail));
        log_warning(tr("MCP: %1").arg(_problem));
    });
    connect(process, &QProcess::errorOccurred, this, [this, process](QProcess::ProcessError error)
    {
        if (error != QProcess::FailedToStart) { return; }
        _readyTimer->stop();
        _http = nullptr;
        process->deleteLater();
        setHttpState(HttpState::Failed, tr("Cannot start %1: %2").arg(process->program(), process->errorString()));
        log_warning(tr("MCP: %1").arg(_problem));
    });

    setHttpState(HttpState::Starting);
    _readyTries = 0;
    _readyTimer->start();
    process->start();
}

void McpController::stopHttp()
{
    _readyTimer->stop();
    if (QProcess *process = std::exchange(_http, nullptr))
    {
        process->disconnect(this);
        process->kill();
        process->waitForFinished(2000);
        delete process;
    }
    if (_httpState != HttpState::Off)
    {
        setHttpState(HttpState::Off);
    }
}

void McpController::checkHttpReady()
{
    if (!_http || _httpState != HttpState::Starting)
    {
        _readyTimer->stop();
        return;
    }
    // Fallback when the start message is not seen: is the port answering?
    QTcpSocket socket;
    socket.connectToHost(QHostAddress::LocalHost, httpPort());
    if (socket.waitForConnected(50))
    {
        socket.abort();
        _readyTimer->stop();
        setHttpState(HttpState::Running);
        log_info(tr("MCP: HTTP server on %1 (register: %2)").arg(endpoint(), registrationCommand()));
        return;
    }
    if (++_readyTries >= k_readyTimeoutChecks)
    {
        _readyTimer->stop();
        if (QProcess *process = std::exchange(_http, nullptr))
        {
            process->disconnect(this);
            process->kill();
            process->waitForFinished(2000);
            delete process;
        }
        setHttpState(HttpState::Failed, tr("MCP server did not open port %1 within %2 s. %3")
                                            .arg(httpPort())
                                            .arg(k_readyCheckMs * k_readyTimeoutChecks / 1000)
                                            .arg(_outputTail.trimmed().right(800)));
        log_warning(tr("MCP: %1").arg(_problem));
    }
}

void McpController::setHttpState(HttpState state, const QString &problem)
{
    _httpState = state;
    _problem = problem;
    emit stateChanged();
}
