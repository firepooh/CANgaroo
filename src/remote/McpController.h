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

#include <memory>

#include <QObject>
#include <QString>

#include "remote/RemoteServer.h"

class Backend;
class QProcess;
class QTimer;

// Switches the MCP access on and off from the UI (MCP menu), like UartTerminal:
//   - the remote control endpoint inside cangaroo (RemoteServer, TCP JSON-RPC), and
//   - the MCP server mcp/cangaroo_mcp.py as a child process on streamable HTTP,
//     so Claude Code registers it with one command:
//       claude mcp add --transport http cangaroo http://127.0.0.1:<port>/mcp
//
// Settings (QSettings, group "mcp"): enabled (default true), readOnly (default
// false: AI may transmit), httpPort (5400), remotePort (52790), remoteBuffer
// (100000 frames), python / script (default: <exe>/../mcp/.venv, cangaroo_mcp.py).
//
// The --remote* command line options still work. They decide the endpoint
// options for that run and leave the HTTP server to whoever started cangaroo
// (test scripts, mcp/start_canterminal_compat.cmd). Transmitting follows the
// readOnly setting unless --remote-allow-tx or --remote-read-only is given.
class McpController : public QObject
{
    Q_OBJECT

public:
    enum class HttpState
    {
        Off,            // MCP disabled, or the command line manages it
        Starting,       // process started, port not answering yet
        Running,
        PortInUse,      // another program (another cangaroo, CanTerminal) has the port
        PythonMissing,  // no mcp/.venv python or no cangaroo_mcp.py
        Failed,         // the process exited
    };

    struct CommandLine
    {
        bool remote = false;            // --remote or --remote-port given
        bool readOnly = false;          // --remote-read-only given
        RemoteServer::Options options;  // from --remote-port / -token / -allow-tx / -buffer
    };

    explicit McpController(Backend &backend, QObject *parent = nullptr);
    ~McpController() override;

    // Called once after the main window is shown.
    void startup(const CommandLine &commandLine);
    void shutdown();

    [[nodiscard]] bool isEnabled() const { return _enabled; }
    [[nodiscard]] bool isReadOnly() const { return _readOnly; }
    void setEnabled(bool enabled);
    void setReadOnly(bool readOnly);

    [[nodiscard]] bool commandLineMode() const { return _commandLine.remote; }
    [[nodiscard]] bool remoteListening() const;
    [[nodiscard]] quint16 remotePort() const;
    [[nodiscard]] HttpState httpState() const { return _httpState; }
    [[nodiscard]] quint16 httpPort() const;
    [[nodiscard]] QString endpoint() const;
    [[nodiscard]] QString registrationCommand() const;
    [[nodiscard]] QString stdioRegistrationCommand() const;
    [[nodiscard]] QString pythonPath() const;
    [[nodiscard]] QString scriptPath() const;
    [[nodiscard]] QString lastProblem() const { return _problem; }

    // One line for the status bar, and whether it is a problem worth color.
    [[nodiscard]] QString statusText() const;

signals:
    void stateChanged();

private:
    Backend &_backend;
    CommandLine _commandLine;
    bool _enabled = true;
    bool _readOnly = false;
    std::unique_ptr<RemoteServer> _remote;
    QProcess *_http = nullptr;
    QTimer *_readyTimer = nullptr;
    int _readyTries = 0;
    HttpState _httpState = HttpState::Off;
    QString _problem;
    QString _outputTail;

    void start();
    void stop();
    void startHttp();
    void stopHttp();
    void checkHttpReady();
    void setHttpState(HttpState state, const QString &problem = QString());
};
