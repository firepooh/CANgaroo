"""Starts cangaroo_mcp.py over stdio and calls tools that do not transmit.

cangaroo must already run with --remote; the can_* checks also need a running
measurement. Run:
    .venv\\Scripts\\python.exe selftest.py
"""

import asyncio
import json
import os
import sys

from mcp import ClientSession, StdioServerParameters
from mcp.client.stdio import stdio_client

HERE = os.path.dirname(os.path.abspath(__file__))


def text_of(result) -> str:
    return " ".join(getattr(c, "text", "") for c in result.content)


async def main() -> int:
    params = StdioServerParameters(command=sys.executable, args=[os.path.join(HERE, "cangaroo_mcp.py")])
    async with stdio_client(params) as (read, write):
        async with ClientSession(read, write) as session:
            await session.initialize()

            tools = await session.list_tools()
            print("tools:", ", ".join(t.name for t in tools.tools))

            failures = 0
            for name, args in [
                ("status", {}),
                ("list_interfaces", {}),
                ("wait_for_frame", {"id": "0x7FF", "timeout_ms": 200}),
                ("frame_summary", {}),
                ("send_frame", {"interface": 0, "id": "0x123", "data": "11 22"}),
                ("can_status", {}),
                ("can_recent", {"count": 5}),
                ("get_frames", {"latest": True, "limit": 3}),
            ]:
                result = await session.call_tool(name, args)
                body = text_of(result)
                flag = "ERROR" if result.is_error else "ok"
                print(f"{name}: {flag}: {body[:300]}")
                if result.is_error and name != "send_frame":
                    failures += 1

            # CanTerminal compatible answers that scripts parse
            checks = [
                ("can_wait_for", {"id": "7FF", "timeout_ms": 200}, "Timed out: no frame with ID 0x7FF within 200 ms."),
                ("can_recent", {"count": 5, "id": "0x7FE"}, "No frames in the buffer matching that filter."),
            ]
            for name, args, expected in checks:
                result = await session.call_tool(name, args)
                body = text_of(result)
                ok = not result.is_error and body == expected
                print(f"{name}: {'ok' if ok else 'FAIL'}: {body[:300]}")
                failures += 0 if ok else 1
            result = await session.call_tool("can_send", {"channel": "NO_SUCH", "id": "123"})
            ok = result.is_error and "Unknown channel 'NO_SUCH'" in text_of(result)
            print(f"can_send unknown channel: {'ok' if ok else 'FAIL'}: {text_of(result)[:300]}")
            failures += 0 if ok else 1
            return failures


if __name__ == "__main__":
    sys.exit(asyncio.run(main()))
