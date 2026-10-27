"""MCP server for cangaroo.

Bridges MCP tools to cangaroo's remote control endpoint (src/remote), a
newline-delimited JSON-RPC 2.0 socket on 127.0.0.1. Start cangaroo with
``--remote`` (and ``--remote-allow-tx`` to permit transmitting) first.

Connection settings are read from the discovery file cangaroo writes to
``<temp>/cangaroo-remote.json``. Override them with the environment
variables CANGAROO_HOST, CANGAROO_PORT and CANGAROO_TOKEN.

Usage:
    python cangaroo_mcp.py                              # stdio (Claude Code, Claude Desktop)
    python cangaroo_mcp.py --transport http --port 8765 # streamable HTTP on 127.0.0.1
"""

from __future__ import annotations

import argparse
import asyncio
import itertools
import json
import os
import tempfile
from typing import Any, Literal

from mcp.server.mcpserver import MCPServer
from mcp.server.mcpserver.exceptions import ToolError

DISCOVERY_FILE = os.path.join(tempfile.gettempdir(), "cangaroo-remote.json")

# Integers or hex strings such as "0x7E8"; cangaroo parses both.
CanId = int | str


# ToolError: the SDK passes the message to the model instead of a generic error.
class RemoteError(ToolError):
    """Error object returned by cangaroo."""

    def __init__(self, code: int, message: str):
        super().__init__(f"{message} (code {code})")
        self.code = code
        self.message = message


class CangarooClient:
    """Async JSON-RPC client. Requests may overlap (e.g. a long wait_for_frame)."""

    def __init__(self) -> None:
        self._reader: asyncio.StreamReader | None = None
        self._writer: asyncio.StreamWriter | None = None
        self._pending: dict[int, asyncio.Future] = {}
        self._ids = itertools.count(1)
        self._connect_lock = asyncio.Lock()
        self._reader_task: asyncio.Task | None = None

    @staticmethod
    def _settings() -> tuple[str, int, str]:
        info: dict[str, Any] = {}
        if os.path.exists(DISCOVERY_FILE):
            with open(DISCOVERY_FILE, encoding="utf-8") as f:
                info = json.load(f)
        host = os.environ.get("CANGAROO_HOST", info.get("host", "127.0.0.1"))
        port = int(os.environ.get("CANGAROO_PORT", info.get("port", 52790)))
        token = os.environ.get("CANGAROO_TOKEN", info.get("token", ""))
        return host, port, token

    async def _ensure_connected(self) -> None:
        async with self._connect_lock:
            if self._writer and not self._writer.is_closing():
                return
            host, port, token = self._settings()
            try:
                self._reader, self._writer = await asyncio.open_connection(host, port, limit=16 * 1024 * 1024)
            except OSError as e:
                raise ToolError(
                    f"cannot connect to cangaroo at {host}:{port} ({e}). "
                    "Start cangaroo with --remote (Build_Local.cmd output: dist\\cangaroo.exe --remote)."
                ) from e
            self._reader_task = asyncio.create_task(self._read_loop())
            await self._request("hello", {"token": token})

    async def _read_loop(self) -> None:
        assert self._reader is not None
        try:
            while line := await self._reader.readline():
                msg = json.loads(line)
                fut = self._pending.pop(msg.get("id"), None)
                if fut is None or fut.done():
                    continue
                if "error" in msg:
                    err = msg["error"]
                    fut.set_exception(RemoteError(err.get("code", 0), err.get("message", "")))
                else:
                    fut.set_result(msg.get("result"))
        finally:
            for fut in self._pending.values():
                if not fut.done():
                    fut.set_exception(ToolError("connection to cangaroo closed"))
            self._pending.clear()
            if self._writer:
                self._writer.close()

    async def _request(self, method: str, params: dict[str, Any]) -> Any:
        assert self._writer is not None
        req_id = next(self._ids)
        fut = asyncio.get_running_loop().create_future()
        self._pending[req_id] = fut
        payload = {"jsonrpc": "2.0", "id": req_id, "method": method, "params": params}
        self._writer.write((json.dumps(payload) + "\n").encode())
        await self._writer.drain()
        return await fut

    async def call(self, method: str, **params: Any) -> Any:
        clean = {k: v for k, v in params.items() if v is not None}
        await self._ensure_connected()
        return await self._request(method, clean)


client = CangarooClient()
mcp = MCPServer(
    "cangaroo",
    instructions=(
        "Controls the cangaroo CAN analyzer GUI. Typical flow: list_interfaces -> "
        "configure_interface (while stopped) -> start_measurement -> send_frame / "
        "send_and_wait / wait_for_frame / get_frames / frame_summary -> stop_measurement. "
        "Frames carry a 'seq' cursor; pass 'next' or 'cursor' from a result as 'since' to "
        "continue without gaps. Timestamps (t_us) come from the adapter, so judge timing "
        "with latency_us / period values, never with tool call durations. Transmitting "
        "needs cangaroo started with --remote-allow-tx. CAN ids accept ints or hex strings. "
        "can_status / can_send / can_recent / can_wait_for are the CanTerminal compatible tools: "
        "they use channel names such as CAN1 and CAN2 instead of interface numbers."
    ),
)


@mcp.tool()
async def status() -> dict:
    """Server info (version, allow_tx) and measurement status (running, cursor, interfaces)."""
    hello = await client.call("hello", token=client._settings()[2])
    return {"server": hello, "measurement": await client.call("measurement_status")}


@mcp.tool()
async def list_interfaces() -> list:
    """List CAN interfaces with id, driver, state, capabilities, supported bitrates and current config."""
    return await client.call("list_interfaces")


@mcp.tool()
async def configure_interface(
    interface: int,
    bitrate: int | None = None,
    sample_point: int | None = None,
    fd: bool | None = None,
    fd_bitrate: int | None = None,
    fd_sample_point: int | None = None,
    listen_only: bool | None = None,
    enabled: bool | None = None,
) -> dict:
    """Change an interface's measurement settings. Only allowed while the measurement is stopped.

    Sample points are in permille (875 = 87.5 %). Unsupported timings are reported in 'warnings'.
    """
    return await client.call(
        "configure_interface", interface=interface, bitrate=bitrate, sample_point=sample_point, fd=fd,
        fd_bitrate=fd_bitrate, fd_sample_point=fd_sample_point, listen_only=listen_only, enabled=enabled,
    )


@mcp.tool()
async def start_measurement(clear_trace: bool = False) -> dict:
    """Open the enabled interfaces and start capturing. Returns status incl. the current cursor."""
    return await client.call("start_measurement", clear_trace=clear_trace)


@mcp.tool()
async def stop_measurement() -> dict:
    """Stop capturing, close the interfaces and stop all periodic transmissions."""
    return await client.call("stop_measurement")


@mcp.tool()
async def bus_status(interface: int) -> dict:
    """Bus state (ok/warning/passive/bus_off) and rx/tx frame, error, overrun and drop counters."""
    return await client.call("bus_status", interface=interface)


@mcp.tool()
async def identify(interface: int, on: bool = True) -> dict:
    """Blink the adapter's LEDs (on=true) or stop (on=false) to tell which board an interface is.

    Works with the measurement stopped. The firmware also stops blinking when the channel starts.
    Does not touch the bus, so it needs no --remote-allow-tx.
    """
    return await client.call("identify", interface=interface, on=on)


@mcp.tool()
async def send_frame(
    interface: int,
    id: CanId,
    data: str = "",
    extended: bool = False,
    fd: bool = False,
    brs: bool = False,
    rtr: bool = False,
) -> dict:
    """Transmit one frame. data is hex bytes like "11 22 AA". Returns the cursor before sending."""
    return await client.call(
        "send_frame", interface=interface, id=id, data=data, extended=extended, fd=fd, brs=brs, rtr=rtr
    )


@mcp.tool()
async def send_periodic(
    interface: int,
    id: CanId,
    interval_ms: int,
    data: str = "",
    count: int | None = None,
    extended: bool = False,
    fd: bool = False,
    brs: bool = False,
) -> dict:
    """Transmit a frame every interval_ms (first one immediately), count times or until stopped.

    Returns a handle for stop_periodic. Periodic tasks stop when the measurement stops.
    """
    return await client.call(
        "send_periodic", interface=interface, id=id, data=data, interval_ms=interval_ms, count=count,
        extended=extended, fd=fd, brs=brs,
    )


@mcp.tool()
async def stop_periodic(handle: int | Literal["all"]) -> dict:
    """Stop a periodic transmission by handle, or all of them with "all"."""
    return await client.call("stop_periodic", handle=handle)


@mcp.tool()
async def wait_for_frame(
    id: CanId | None = None,
    mask: CanId | None = None,
    interface: int | None = None,
    extended: bool | None = None,
    dir: Literal["rx", "tx", "any"] = "rx",
    since: int | None = None,
    timeout_ms: int = 1000,
) -> dict:
    """Wait until a matching frame arrives. Without 'since' only frames arriving after the call count.

    Returns {matched: true, frame, cursor} or {matched: false, timeout_ms, cursor}.
    """
    return await client.call(
        "wait_for_frame", id=id, mask=mask, interface=interface, extended=extended, dir=dir, since=since,
        timeout_ms=timeout_ms,
    )


@mcp.tool()
async def send_and_wait(
    interface: int,
    id: CanId,
    expect_id: CanId,
    data: str = "",
    expect_mask: CanId | None = None,
    expect_interface: int | None = None,
    extended: bool = False,
    fd: bool = False,
    brs: bool = False,
    timeout_ms: int = 1000,
) -> dict:
    """Transmit a frame and wait for the response frame in one step.

    Returns the response, the TX echo and latency_us measured with adapter timestamps
    (response time minus transmit time). Use this for request/response timing checks.
    """
    expect: dict[str, Any] = {"id": expect_id}
    if expect_mask is not None:
        expect["mask"] = expect_mask
    if expect_interface is not None:
        expect["interface"] = expect_interface
    return await client.call(
        "send_and_wait", interface=interface, id=id, data=data, extended=extended, fd=fd, brs=brs,
        expect=expect, timeout_ms=timeout_ms,
    )


@mcp.tool()
async def get_frames(
    since: int | None = None,
    limit: int = 100,
    id: CanId | None = None,
    mask: CanId | None = None,
    interface: int | None = None,
    dir: Literal["rx", "tx", "any"] = "any",
    errors: bool = False,
    latest: bool = False,
) -> dict:
    """Read captured frames from cursor 'since' (default: oldest kept), at most 'limit' (max 2000).

    Continue with since=next while 'more' is true. 'dropped' means frames after 'since' were
    already discarded from the buffer. latest=true returns the newest 'limit' matching frames
    instead (still oldest first); it cannot be combined with 'since'.
    """
    return await client.call(
        "get_frames", since=since, limit=limit, id=id, mask=mask, interface=interface, dir=dir, errors=errors,
        latest=latest or None,
    )


@mcp.tool()
async def frame_summary(
    since: int | None = None,
    until: int | None = None,
    id: CanId | None = None,
    mask: CanId | None = None,
    interface: int | None = None,
    dir: Literal["rx", "tx", "any"] = "any",
) -> dict:
    """Per-ID statistics between two cursors: count, average/min/max period in ms, last data.

    Prefer this over get_frames to check cycle times or which messages are on the bus.
    """
    return await client.call(
        "frame_summary", since=since, until=until, id=id, mask=mask, interface=interface, dir=dir
    )


@mcp.tool()
async def clear_trace() -> dict:
    """Clear the GUI trace and the remote frame buffer. Returns the new cursor."""
    return await client.call("clear_trace")


@mcp.tool()
async def save_trace(path: str, format: Literal["asc", "candump", "mdf", "pcap", "pcapng"] | None = None) -> dict:
    """Save the GUI trace to a file; the format defaults to the file extension."""
    return await client.call("save_trace", path=path, format=format)


@mcp.tool()
async def load_dbc(path: str, network: str | None = None, interface: int | None = None) -> dict:
    """Load a DBC file into a network (by name, by interface, or the first network)."""
    return await client.call("load_dbc", path=path, network=network, interface=interface)


@mcp.tool()
async def decode_frame(
    seq: int | None = None,
    id: CanId | None = None,
    data: str | None = None,
    extended: bool = False,
    fd: bool = False,
) -> dict:
    """Decode a frame with the loaded DBC files: a captured one by seq, or given id + data."""
    if seq is not None:
        return await client.call("decode_frame", seq=seq)
    return await client.call("decode_frame", id=id, data=data or "", extended=extended, fd=fd)


@mcp.tool()
async def encode_frame(
    message: str | int,
    signals: dict[str, float],
    interface: int | None = None,
    send: bool = False,
) -> dict:
    """Build a frame from DBC signal values (message by name or id). With send=true it is also transmitted."""
    return await client.call("encode_frame", message=message, signals=signals, interface=interface, send=send)


# ---------------------------------------------------------------------------
# CanTerminal compatibility: can_status / can_send / can_recent / can_wait_for
#
# Test scripts written for the old CanTerminal MCP server (http://127.0.0.1:5400/mcp)
# call these four tools with channel names CAN1, CAN2 and parse their text output.
# They behave and format like CanTerminal's CanMcpTools.cs, so those scripts run
# unchanged against cangaroo. Channel names map to adapters by USB serial
# (channels.json), because cangaroo's candleN numbering can change between runs.
# ---------------------------------------------------------------------------

CHANNELS_FILE = os.environ.get(
    "CANGAROO_CHANNELS", os.path.join(os.path.dirname(os.path.abspath(__file__)), "channels.json")
)


def _channel_serials() -> dict[str, str]:
    """Channel name (upper case) -> adapter USB serial from channels.json; empty without the file."""
    try:
        with open(CHANNELS_FILE, encoding="utf-8") as f:
            data = json.load(f)
    except FileNotFoundError:
        return {}
    return {name.upper(): str(serial) for name, serial in data.get("channels", {}).items()}


def _usb_interface_path(serial: str) -> str | None:
    """Instance path tail of the adapter's CAN interface (USB\\VID_1D50&PID_606F&MI_00\\<tail>).

    cangaroo's interface details contain the same tail in the device path, so it ties a
    serial to a cangaroo interface. Windows names the interfaces of a composite device
    <ParentIdPrefix>&<interface number>, and keeps ParentIdPrefix in the parent's registry
    key, which is named after the serial. (Get-PnpDevice finds the same in about 15 s.)
    """
    try:
        import winreg

        key = rf"SYSTEM\CurrentControlSet\Enum\USB\VID_1D50&PID_606F\{serial}"
        with winreg.OpenKey(winreg.HKEY_LOCAL_MACHINE, key) as k:
            prefix, _ = winreg.QueryValueEx(k, "ParentIdPrefix")
    except (ImportError, OSError):
        return None
    return f"{prefix}&0000".lower()


async def _channels() -> tuple[dict[str, int], dict[int, str], list[dict]]:
    """(channel name -> interface id, interface id -> channel name, CAN interfaces).

    Interfaces without a channel name keep their cangaroo name (e.g. candle0_ch0).
    """
    interfaces = [i for i in await client.call("list_interfaces") if i.get("bus_type", "CAN") == "CAN"]
    by_name: dict[str, int] = {}
    for channel, serial in _channel_serials().items():
        path = _usb_interface_path(serial)
        match = [i for i in interfaces if path and f"#{path}#" in i.get("details", "").lower()]
        if match:
            by_name[channel] = match[0]["id"]
    by_id = {intf_id: name for name, intf_id in by_name.items()}
    for i in interfaces:
        by_id.setdefault(i["id"], i["name"])
    return by_name, by_id, interfaces


async def _resolve_channel(channel: str) -> int:
    by_name, _, interfaces = await _channels()
    if channel.upper() in by_name:
        return by_name[channel.upper()]
    for i in interfaces:
        if i["name"].lower() == channel.lower():
            return i["id"]
    known = list(by_name) + [i["name"] for i in interfaces if i["id"] not in by_name.values()]
    raise ToolError(f"Unknown channel '{channel}'. Channels: {', '.join(known) or 'none'} (see can_status).")


def _parse_hex_id(text: str) -> int:
    s = text.strip()
    if s.lower().startswith("0x"):
        s = s[2:]
    try:
        return int(s, 16)
    except ValueError:
        raise ToolError(f"'{text}' is not a hex arbitration id.") from None


def _format_frame(f: dict, names: dict[int, str]) -> str:
    """One frame as CanTerminal prints it: '  t=1.234567 CAN1 RX 0x123 [8] 0011... (EXT)'."""
    flags = []
    if f.get("extended"):
        flags.append("EXT")
    if f.get("fd"):
        flags.append("FD+BRS" if f.get("brs") else "FD")
    if f.get("rtr"):
        flags.append("RTR")
    if "error" in f:
        flags.append("ERR")
    id_text = f"{f['id']:08X}" if f.get("extended") else f"{f['id']:03X}"
    line = (
        f"  t={f['t']:.6f} {names.get(f['interface'], str(f['interface']))} "
        f"{'RX' if f['dir'] == 'rx' else 'TX'} 0x{id_text} [{f['len']}] {f['data'].replace(' ', '')}"
    )
    if flags:
        line += f" ({','.join(flags)})"
    if "error" in f:
        line += f"  |  {f['error']}"
    return line


@mcp.tool()
async def can_status() -> dict:
    """CanTerminal compatible. Report the channels (CAN1, CAN2, ...), whether the measurement is
    running, each channel's interface, bitrate and bus state, and how many frames were captured.
    Call this before the other can_* tools; their channel names come from here.
    """
    hello = await client.call("hello", token=client._settings()[2])
    meas = await client.call("measurement_status")
    by_name, by_id, interfaces = await _channels()
    state = {i["id"]: i for i in meas.get("interfaces", [])}

    details = []
    for i in sorted(interfaces, key=lambda i: (i["id"] not in by_name.values(), by_id[i["id"]])):
        cfg = i.get("config", {})
        details.append({
            "channel": by_id[i["id"]],
            "interface": i["id"],
            "name": i["name"],
            "firmware": i.get("firmware_version"),
            "open": state.get(i["id"], {}).get("open", False),
            "state": state.get(i["id"], {}).get("state", i.get("state")),
            "bitrate": cfg.get("bitrate"),
            "sample_point": cfg.get("sample_point"),
            "fd": cfg.get("fd"),
            "listen_only": cfg.get("listen_only"),
        })
    return {
        "connected": bool(meas.get("running")) and any(d["open"] for d in details),
        "adapter": f"cangaroo {hello.get('version')} {hello.get('local_version', '')} (Candlelight)".strip(),
        "channels": [d["channel"] for d in details],
        "dbc": None,
        "profile": "none",
        "mode": "live",
        "log": None,
        "channelDbc": [],
        "totalFrames": max(0, meas.get("cursor", 1) - 1),
        "version": hello.get("version"),
        "running": bool(meas.get("running")),
        "allowTx": hello.get("allow_tx"),
        "channelDetails": details,
    }


@mcp.tool()
async def can_send(
    channel: str,
    id: str,
    data: str | None = None,
    ext: bool | None = None,
    fd: bool = False,
    brs: bool = False,
) -> str:
    """CanTerminal compatible. Transmit one frame on a channel (e.g. "CAN1"). id is hex ("123",
    "0x18FF50E5"); data is a hex string ("0011AABB"), empty for a zero-length frame. ext defaults
    to what the id needs. Needs a running measurement and cangaroo started with --remote-allow-tx.
    """
    arb_id = _parse_hex_id(id)
    try:
        payload = bytes.fromhex((data or "").replace(" ", ""))
    except ValueError:
        raise ToolError(f"'{data}' is not a hex payload.") from None
    interface = await _resolve_channel(channel)
    try:
        await client.call(
            "send_frame", interface=interface, id=arb_id, data=payload.hex(" ").upper(),
            extended=ext if ext is not None else arb_id > 0x7FF, fd=fd, brs=brs,
        )
    except RemoteError as e:
        raise ToolError(f"{channel}: {e.message}") from None
    return f"Sent 0x{arb_id:X} on {channel} ({len(payload)} byte(s))."


@mcp.tool()
async def can_recent(count: int = 50, channel: str | None = None, id: str | None = None) -> str:
    """CanTerminal compatible. The newest captured frames, newest last (default 50, at most 1000),
    optionally of one channel and one hex id. Does not wait; use can_wait_for for a reply.
    """
    params: dict[str, Any] = {"latest": True, "limit": min(max(count, 1), 1000), "errors": True}
    if channel:
        params["interface"] = await _resolve_channel(channel)
    if id:
        params["id"] = _parse_hex_id(id)
    frames = (await client.call("get_frames", **params))["frames"]
    if not frames:
        return "No frames in the buffer matching that filter."
    _, names, _ = await _channels()
    return "\n".join([f"{len(frames)} frame(s), newest last:"] + [_format_frame(f, names) for f in frames]) + "\n"


@mcp.tool()
async def can_wait_for(id: str, channel: str | None = None, timeout_ms: int = 5000) -> str:
    """CanTerminal compatible. Block until a frame with this hex id is received (own transmissions
    do not count), then return it. timeout_ms defaults to 5000, at most 300000.
    """
    arb_id = _parse_hex_id(id)
    timeout_ms = min(max(timeout_ms, 1), 300000)
    if not (await client.call("measurement_status")).get("running"):
        raise ToolError("The measurement is not running in cangaroo, so no frame can arrive.")
    params: dict[str, Any] = {"id": arb_id, "dir": "rx", "timeout_ms": timeout_ms}
    if channel:
        params["interface"] = await _resolve_channel(channel)
    result = await client.call("wait_for_frame", **params)
    if not result.get("matched"):
        return f"Timed out: no frame with ID 0x{arb_id:X} within {timeout_ms} ms."
    _, names, _ = await _channels()
    return "Received:\n" + _format_frame(result["frame"], names)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--transport", choices=["stdio", "http"], default="stdio")
    parser.add_argument("--port", type=int, default=8765, help="HTTP port (127.0.0.1 only)")
    args = parser.parse_args()

    if args.transport == "http":
        mcp.run(transport="streamable-http", host="127.0.0.1", port=args.port)
    else:
        mcp.run(transport="stdio")


if __name__ == "__main__":
    main()
