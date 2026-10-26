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

// Version of this local build next to the upstream version (see localversion.pri)
namespace LocalVersion
{

// Upstream version, e.g. "0.7.1"
[[nodiscard]] QString base();
// Upstream commit this branch forked from, e.g. "2bcec7a" (empty if unknown)
[[nodiscard]] QString baseCommit();
// Local version, e.g. "r11-2740773" or "r11-2740773-dirty"
[[nodiscard]] QString local();
// Date of the local commit, e.g. "2026-10-26"
[[nodiscard]] QString localDate();
// One line for the status bar, e.g. "v0.7.1 · local r11-2740773"
[[nodiscard]] QString summary();
// Several lines for the About dialog and tooltips
[[nodiscard]] QString details();

} // namespace LocalVersion
