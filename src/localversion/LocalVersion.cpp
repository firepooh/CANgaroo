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

#include "LocalVersion.h"

#include "local_version.h"

#ifndef VERSION_STRING
#define VERSION_STRING "dev"
#endif

namespace LocalVersion
{

QString base()
{
    return QStringLiteral(VERSION_STRING);
}

QString baseCommit()
{
    return QStringLiteral(LOCAL_BASE_COMMIT);
}

QString local()
{
    const QString count = QStringLiteral(LOCAL_VERSION_COUNT);
    const QString hash = QStringLiteral(LOCAL_VERSION_HASH);
    return count.isEmpty() ? hash : QStringLiteral("r%1-%2").arg(count, hash);
}

QString localDate()
{
    return QStringLiteral(LOCAL_VERSION_DATE);
}

QString summary()
{
    return QStringLiteral("v%1 · local %2").arg(base(), local());
}

QString details()
{
    QString text = QStringLiteral("Base version %1").arg(base());
    if (!baseCommit().isEmpty()) {
        text += QStringLiteral(" (upstream %1)").arg(baseCommit());
    }
    text += QStringLiteral("\nLocal version %1").arg(local());
    if (!localDate().isEmpty()) {
        text += QStringLiteral(" (%1)").arg(localDate());
    }
    return text;
}

} // namespace LocalVersion
