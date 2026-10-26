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

#include "XcpSessions.h"

XcpSessions &XcpSessions::instance()
{
    static XcpSessions sessions;
    return sessions;
}

void XcpSessions::set(const XcpConfig &config)
{
    for (auto &s : _sessions) {
        if (s.interfaceId == config.interfaceId) {
            if (s == config) { return; }
            s = config;
            bump();
            return;
        }
    }
    _sessions.append(config);
    bump();
}

void XcpSessions::remove(BusInterfaceId interfaceId)
{
    const qsizetype removed = _sessions.removeIf([interfaceId](const XcpConfig &s) { return s.interfaceId == interfaceId; });
    if (removed > 0) { bump(); }
}

void XcpSessions::clear()
{
    if (_sessions.isEmpty()) { return; }
    _sessions.clear();
    bump();
}

void XcpSessions::bump()
{
    ++_revision;
    emit changed();
}
