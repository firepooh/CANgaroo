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

#include <cstdint>
#include <optional>

#include <QString>

class QTextStream;

// CAN IDs from the IF_DATA XCP_ON_CAN block of an A2L file
struct A2lXcpIds
{
    std::optional<uint32_t> master;
    std::optional<uint32_t> slave;
    std::optional<uint32_t> broadcast;
    bool extended = false;

    [[nodiscard]] bool hasPair() const { return master.has_value() && slave.has_value(); }
};

// Pulls the CAN IDs out of the XCP_ON_CAN block of an A2L file, the authoritative
// source when one is available. Only that block is read; this is deliberately
// not a full A2L parser, so it stays fast on the tens-of-megabytes files real
// projects have. Ported from CanTerminal.Core/Xcp/A2lXcpReader.cs.
namespace A2lXcpReader
{

// A2L marks an ID as 29-bit by setting the top bit of the value
constexpr uint32_t extended_flag = 0x80000000;

[[nodiscard]] A2lXcpIds parse(QTextStream &in);
// Reads a file; on failure returns no IDs and sets *error
[[nodiscard]] A2lXcpIds read(const QString &path, QString *error = nullptr);

} // namespace A2lXcpReader
