/*
  Copyright (c) 2026 firepooh

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

#include <QList>
#include <QObject>

#include "XcpDecoder.h"

// The XCP sessions to decode, at most one per interface. Shared by every
// ProtocolManager (each trace view model has its own); a change bumps the
// revision, so the managers rebuild their decoders, and emits changed(), so the
// views decode the trace again from the start with fresh session state.
class XcpSessions : public QObject
{
    Q_OBJECT
public:
    static XcpSessions &instance();

    [[nodiscard]] const QList<XcpConfig> &sessions() const { return _sessions; }
    [[nodiscard]] int revision() const { return _revision; }

    // Sets the session of config.interfaceId, replacing an earlier one
    void set(const XcpConfig &config);
    void remove(BusInterfaceId interfaceId);
    void clear();

signals:
    void changed();

private:
    XcpSessions() = default;

    QList<XcpConfig> _sessions;
    int _revision = 0;

    void bump();
};
