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

#include <QString>

// Rules shared by the toolbars of the Monitor tab and the protocol tabs
// (GUI_DESIGN.md 4.5). Kept free of widgets so they can be unit tested.
namespace TraceFind
{

// T-8: case-insensitive part of ID, Description, Info or a decoded line
[[nodiscard]] inline bool matchesText(const QString &needle, const QString &text)
{
    return !needle.isEmpty() && text.contains(needle, Qt::CaseInsensitive);
}

// T-8: Data ignores spaces on both sides, so "62F1" finds "62 F1 90"
[[nodiscard]] inline bool matchesData(const QString &needle, const QString &data)
{
    QString n = needle;
    n.remove(QChar(' '));
    if (n.isEmpty()) { return false; }
    QString d = data;
    d.remove(QChar(' '));
    return d.contains(n, Qt::CaseInsensitive);
}

// T-9: the next result from `current` (-1 = none yet), wrapping at both ends
[[nodiscard]] inline int step(int current, int total, bool forward)
{
    if (total <= 0) { return -1; }
    if (current < 0 || current >= total) { return forward ? 0 : total - 1; }
    return ((current + (forward ? 1 : -1)) % total + total) % total;
}

// T-9: "3 / 12" for the third of twelve results, "– / 12" before one is picked
[[nodiscard]] inline QString positionText(int current, int total)
{
    if (current < 0 || current >= total) { return QStringLiteral("– / %1").arg(total); }
    return QStringLiteral("%1 / %2").arg(current + 1).arg(total);
}

// T-12: the number shown on the Filter button, one per condition of the tab
[[nodiscard]] inline int filterCount(bool showTx, bool showRx, int hiddenIds, int hiddenLinFrames, int hiddenInterfaces)
{
    return (showTx ? 0 : 1) + (showRx ? 0 : 1) + hiddenIds + hiddenLinFrames + hiddenInterfaces;
}

// V-1: Scroll state from a workspace saved before the shared toolbar. The protocol
// tabs wrote mode* = "unified" (Rolling Log) or "aggregated", the Monitor tab
// mode = "scroll" or "fixed".
[[nodiscard]] inline bool scrollFromLegacyMode(const QString &mode)
{
    return mode == QLatin1String("unified") || mode == QLatin1String("scroll");
}

} // namespace TraceFind
