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

#include <stdexcept>

#include <QString>

// Error reported to the remote client as a JSON-RPC error object.
class RemoteError : public std::runtime_error
{
public:
    enum Code
    {
        // JSON-RPC 2.0 standard codes
        ParseError     = -32700,
        InvalidRequest = -32600,
        MethodNotFound = -32601,
        InvalidParams  = -32602,
        InternalError  = -32603,

        // cangaroo remote codes
        Unauthorized   = -32000,
        TxNotAllowed   = -32001,
        NotRunning     = -32002,
        Running        = -32003,
        NotFound       = -32004,
        IoError        = -32005,
    };

    RemoteError(Code code, const QString &message)
        : std::runtime_error(message.toStdString()),
          _code(code),
          _message(message)
    {
    }

    [[nodiscard]] Code code() const noexcept { return _code; }
    [[nodiscard]] const QString &message() const noexcept { return _message; }

private:
    Code _code;
    QString _message;
};
