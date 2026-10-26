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

#include <QDialog>

class McpController;

// MCP menu -> "MCP Guide...": the current state, the endpoint and the Claude Code
// registration command filled in from the real settings, with copy buttons.
class McpGuideDialog : public QDialog
{
    Q_OBJECT

public:
    McpGuideDialog(McpController &mcp, QWidget *parent = nullptr);
};
