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

#include <QString>

// Code-to-name tables from ASAM MCD-1 XCP (protocol layer 1.0 - 1.4)
namespace XcpTables
{

// Lowest PID that identifies a command packet; below this a master frame is a STIM-DTO
constexpr uint8_t command_pid_min = 0xC0;
constexpr uint8_t pid_res = 0xFF;   // positive response
constexpr uint8_t pid_err = 0xFE;   // error
constexpr uint8_t pid_ev = 0xFD;    // event
constexpr uint8_t pid_serv = 0xFC;  // service request

// Command name, or an empty string for an unassigned code
[[nodiscard]] QString command(uint8_t pid);
// Sub-command of the 0xC0 escape (XCP 1.3+)
[[nodiscard]] QString level1(uint8_t sub);
// Sub-command of TRANSPORT_LAYER_CMD for the CAN transport layer
[[nodiscard]] QString transportLayer(uint8_t sub);
[[nodiscard]] QString error(uint8_t code);
[[nodiscard]] QString event(uint8_t code);
[[nodiscard]] QString service(uint8_t code);
[[nodiscard]] QString getIdType(uint8_t type);
// RESOURCE bits reported by CONNECT / protected by GET_STATUS
[[nodiscard]] QString resource(uint8_t r);
[[nodiscard]] QString sessionStatus(uint8_t s);

} // namespace XcpTables
