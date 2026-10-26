@echo off
setlocal

rem Starts cangaroo with remote control and the MCP server on the old CanTerminal
rem address, so test scripts and Claude sessions set up for CanTerminal
rem (http://127.0.0.1:5400/mcp, tools can_status / can_send / can_recent / can_wait_for)
rem work against cangaroo unchanged.
rem
rem Before use: build the GUI (Build_Local.cmd) and create mcp\.venv (see mcp\README.md).
rem Then set the bitrate in cangaroo and press Start; the tools need a running measurement.
rem Channel names come from mcp\channels.json (CAN1 / CAN2 by adapter USB serial).

set "HERE=%~dp0"
set "GUI=%HERE%.."

start "" "%GUI%\dist\cangaroo.exe" --remote --remote-allow-tx --remote-buffer 1000000
"%HERE%.venv\Scripts\python.exe" "%HERE%cangaroo_mcp.py" --transport http --port 5400
