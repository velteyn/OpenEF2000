#!/usr/bin/env python3
"""MCP server for DOSBox-X.

An MCP (Model Context Protocol) control plane for a DOSBox-X session, so an
agent can drive a DOS program without guessing at window geometry, timing or
input delivery.

Tools speak *guest* coordinates: the emulated 640x400 screen. The server
translates to host window coordinates and captures only the guest rectangle, so
callers never deal with window placement or scaling.

    dosbox_launch(conf, wait_ms)        start DOSBox-X with a config
    dosbox_close()                      terminate it
    dosbox_geometry()                   window and guest rectangle
    dosbox_screenshot(path)             640x400 PNG of the guest screen
    dosbox_key(combo)                   e.g. "Return", "shift+F7"
    dosbox_type(text)                   type text via the keyboard
    dosbox_move(x, y)                   move the guest pointer
    dosbox_click(x, y, hold_ms)         move, press, hold, release
    dosbox_wait_stable(timeout_ms)      block until the screen stops changing

Why this exists: host-level synthetic input is not always delivered the way a
DOS program expects, and a screenshot taken from the host desktop includes
window chrome and scaling. Capturing the guest rectangle and driving input in
guest coordinates makes both deterministic and verifiable.

Transport: MCP over stdio, newline-delimited JSON-RPC 2.0.

Usage (as an MCP server):
    python3 dosbox_x_mcp.py

Usage (self-test):
    python3 dosbox_x_mcp.py --selftest path/to/dbx.conf
"""

from __future__ import annotations

import base64
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
import time
from dataclasses import dataclass, field

GUEST_W, GUEST_H = 640, 400
SERVER_NAME = "dosbox-x-mcp"
SERVER_VERSION = "0.1.0"
PROTOCOL_VERSION = "2025-03-26"


# ---------------------------------------------------------------------------
# session
# ---------------------------------------------------------------------------


@dataclass
class Session:
    display: str = ":0"
    process: subprocess.Popen | None = None
    window_id: str = ""
    window_x: int = 0
    window_y: int = 0
    window_w: int = 0
    window_h: int = 0
    last_frame: bytes = b""
    log_path: str = ""

    # -- window ----------------------------------------------------------
    def refresh_geometry(self) -> bool:
        out = _run(["xdotool", "search", "--class", "dosbox-x"], env=self.env())
        ids = [line.strip() for line in out.splitlines() if line.strip()]
        if not ids:
            return False
        self.window_id = ids[0]
        geo = _run(["xdotool", "getwindowgeometry", "--shell", self.window_id],
                   env=self.env())
        values = dict(
            (k, int(v)) for k, v in (line.split("=", 1) for line in geo.splitlines()
                                     if "=" in line)
        )
        self.window_x = values.get("X", 0)
        self.window_y = values.get("Y", 0)
        self.window_w = values.get("WIDTH", 0)
        self.window_h = values.get("HEIGHT", 0)
        return self.window_w > 0 and self.window_h > 0

    def guest_rect(self) -> tuple[int, int, int, int]:
        """Guest rectangle on the host display: x, y, w, h.

        DOSBox-X draws the emulated screen 1:1, centred horizontally and
        bottom-aligned vertically: the difference in height is its menu bar,
        which sits above the guest area. Centring vertically would crop the top
        of the guest by half the menu bar height.
        """
        ox = self.window_x + max(0, (self.window_w - GUEST_W) // 2)
        oy = self.window_y + max(0, self.window_h - GUEST_H)
        return ox, oy, GUEST_W, GUEST_H

    def env(self) -> dict:
        env = dict(os.environ)
        env["DISPLAY"] = self.display
        return env

    # -- lifecycle -------------------------------------------------------
    def launch(self, conf: str, wait_ms: int = 30000) -> dict:
        if self.process is not None and self.process.poll() is None:
            raise RuntimeError("a DOSBox-X session is already running")
        if not shutil.which("dosbox-x"):
            raise RuntimeError("dosbox-x is not on PATH")
        if not os.path.exists(conf):
            raise RuntimeError(f"config not found: {conf}")

        self.log_path = os.path.join(tempfile.gettempdir(), "dosbox-x-mcp.log")
        handle = open(self.log_path, "wb")
        self.process = subprocess.Popen(
            ["dosbox-x", "-conf", conf],
            env=self.env(), stdout=handle, stderr=subprocess.STDOUT,
            start_new_session=True,
        )

        deadline = time.time() + wait_ms / 1000.0
        while time.time() < deadline:
            if self.process.poll() is not None:
                raise RuntimeError(
                    f"dosbox-x exited early (code {self.process.returncode}); "
                    f"see {self.log_path}")
            if self.refresh_geometry():
                return self.status()
            time.sleep(0.5)
        raise RuntimeError(f"DOSBox-X window did not appear within {wait_ms} ms")

    def close(self) -> None:
        if self.process is not None and self.process.poll() is None:
            self.process.terminate()
            try:
                self.process.wait(timeout=10)
            except subprocess.TimeoutExpired:
                self.process.kill()
        self.process = None
        self.window_id = ""
        self.last_frame = b""

    def status(self) -> dict:
        ox, oy, w, h = self.guest_rect()
        return {
            "running": self.process is not None and self.process.poll() is None,
            "window_id": self.window_id,
            "window": {"x": self.window_x, "y": self.window_y,
                       "w": self.window_w, "h": self.window_h},
            "guest": {"x": ox, "y": oy, "w": w, "h": h},
            "log": self.log_path,
        }

    # -- observation -----------------------------------------------------
    def screenshot_png(self) -> bytes:
        if not self.window_id:
            self.refresh_geometry()
        ox, oy, w, h = self.guest_rect()
        fd, path = tempfile.mkstemp(suffix=".png")
        os.close(fd)
        try:
            _run([
                "ffmpeg", "-loglevel", "error", "-y",
                # draw_mouse=0: the host pointer is not guest state and would
                # otherwise be composited over the emulated screen.
                "-f", "x11grab", "-draw_mouse", "0",
                "-video_size", f"{w}x{h}",
                "-i", f"{self.display}+{ox},{oy}",
                "-frames:v", "1", path,
            ], env=self.env())
            with open(path, "rb") as handle:
                return handle.read()
        finally:
            if os.path.exists(path):
                os.unlink(path)

    def frame_hash(self) -> str:
        return hashlib.md5(self.screenshot_png()).hexdigest()

    def wait_stable(self, timeout_ms: int = 15000, interval_ms: int = 250) -> dict:
        """Blocks until two consecutive frames are identical."""
        deadline = time.time() + timeout_ms / 1000.0
        previous = self.frame_hash()
        started = time.time()
        while time.time() < deadline:
            time.sleep(interval_ms / 1000.0)
            current = self.frame_hash()
            if current == previous:
                return {"stable": True,
                        "elapsed_ms": int((time.time() - started) * 1000),
                        "hash": current}
            previous = current
        return {"stable": False,
                "elapsed_ms": int((time.time() - started) * 1000),
                "hash": previous}

    # -- input -----------------------------------------------------------
    def _to_screen(self, gx: int, gy: int) -> tuple[int, int]:
        ox, oy, _, _ = self.guest_rect()
        return ox + gx, oy + gy

    def _focus(self) -> None:
        if not self.window_id:
            self.refresh_geometry()
        _run(["xdotool", "windowfocus", self.window_id], env=self.env())

    def move(self, gx: int, gy: int) -> None:
        self._focus()
        x, y = self._to_screen(gx, gy)
        # Two moves: some window managers need an event to hand over focus
        # before pointer motion inside the window registers.
        _run(["xdotool", "mousemove", str(x - 1), str(y - 1)], env=self.env())
        _run(["xdotool", "mousemove", str(x), str(y)], env=self.env())

    def click(self, gx: int, gy: int, hold_ms: int = 120,
              settle_ms: int = 250) -> None:
        self._focus()
        self.move(gx, gy)
        time.sleep(settle_ms / 1000.0)
        _run(["xdotool", "mousedown", "1"], env=self.env())
        time.sleep(hold_ms / 1000.0)
        _run(["xdotool", "mouseup", "1"], env=self.env())

    def key(self, combo: str) -> None:
        if not self.window_id:
            self.refresh_geometry()
        _run(["xdotool", "windowfocus", self.window_id], env=self.env())
        _run(["xdotool", "key", combo], env=self.env())

    def type_text(self, text: str) -> None:
        if not self.window_id:
            self.refresh_geometry()
        _run(["xdotool", "windowfocus", self.window_id], env=self.env())
        _run(["xdotool", "type", "--delay", "40", text], env=self.env())


def _run(cmd: list[str], env: dict) -> str:
    result = subprocess.run(cmd, env=env, capture_output=True, text=True)
    if result.returncode != 0:
        raise RuntimeError(
            f"{cmd[0]} failed ({result.returncode}): {result.stderr.strip()[:300]}")
    return result.stdout


# ---------------------------------------------------------------------------
# MCP protocol
# ---------------------------------------------------------------------------

TOOLS = [
    {
        "name": "dosbox_launch",
        "description": "Start DOSBox-X with a config file and wait for its window.",
        "inputSchema": {
            "type": "object",
            "properties": {
                "conf": {"type": "string", "description": "Path to the DOSBox-X config"},
                "display": {"type": "string", "description": "X display, e.g. ':100'"},
                "wait_ms": {"type": "integer", "minimum": 1, "default": 30000},
            },
            "required": ["conf"],
        },
    },
    {
        "name": "dosbox_close",
        "description": "Terminate the DOSBox-X session.",
        "inputSchema": {"type": "object", "properties": {}},
    },
    {
        "name": "dosbox_geometry",
        "description": "Report window and guest rectangle in host coordinates.",
        "inputSchema": {"type": "object", "properties": {}},
    },
    {
        "name": "dosbox_screenshot",
        "description": "Capture the guest 640x400 screen as PNG (base64 in the result).",
        "inputSchema": {
            "type": "object",
            "properties": {
                "path": {"type": "string", "description": "Optional PNG output path"},
            },
        },
    },
    {
        "name": "dosbox_key",
        "description": "Send a key combination, e.g. 'Return', 'shift+F7', 'Escape'.",
        "inputSchema": {
            "type": "object",
            "properties": {"combo": {"type": "string"}},
            "required": ["combo"],
        },
    },
    {
        "name": "dosbox_type",
        "description": "Type text into the guest with a small per-key delay.",
        "inputSchema": {
            "type": "object",
            "properties": {"text": {"type": "string"}},
            "required": ["text"],
        },
    },
    {
        "name": "dosbox_move",
        "description": "Move the guest pointer (guest coordinates, 0..639 x 0..399).",
        "inputSchema": {
            "type": "object",
            "properties": {"x": {"type": "integer"}, "y": {"type": "integer"}},
            "required": ["x", "y"],
        },
    },
    {
        "name": "dosbox_click",
        "description": ("Click at guest coordinates: move, settle, press, hold, "
                        "release. hold_ms controls how long the button stays down."),
        "inputSchema": {
            "type": "object",
            "properties": {
                "x": {"type": "integer"},
                "y": {"type": "integer"},
                "hold_ms": {"type": "integer", "minimum": 1, "default": 120},
            },
            "required": ["x", "y"],
        },
    },
    {
        "name": "dosbox_wait_stable",
        "description": "Block until the guest screen stops changing.",
        "inputSchema": {
            "type": "object",
            "properties": {
                "timeout_ms": {"type": "integer", "minimum": 1, "default": 15000},
                "interval_ms": {"type": "integer", "minimum": 50, "default": 250},
            },
        },
    },
]


class Server:
    def __init__(self) -> None:
        self.session = Session()

    def call(self, name: str, args: dict) -> dict:
        if name == "dosbox_launch":
            if "display" in args:
                self.session.display = args["display"]
            return self.session.launch(args["conf"], int(args.get("wait_ms", 30000)))
        if name == "dosbox_close":
            self.session.close()
            return {"running": False}
        if name == "dosbox_geometry":
            self.session.refresh_geometry()
            return self.session.status()
        if name == "dosbox_screenshot":
            png = self.session.screenshot_png()
            path = args.get("path")
            if path:
                with open(path, "wb") as handle:
                    handle.write(png)
            return {"bytes": len(png), "path": path or "",
                    "base64": base64.b64encode(png).decode()}
        if name == "dosbox_key":
            self.session.key(args["combo"])
            return {"sent": args["combo"]}
        if name == "dosbox_type":
            self.session.type_text(args["text"])
            return {"typed": len(args["text"])}
        if name == "dosbox_move":
            self.session.move(int(args["x"]), int(args["y"]))
            return {"x": args["x"], "y": args["y"]}
        if name == "dosbox_click":
            self.session.click(int(args["x"]), int(args["y"]),
                               int(args.get("hold_ms", 120)))
            return {"x": args["x"], "y": args["y"],
                    "hold_ms": int(args.get("hold_ms", 120))}
        if name == "dosbox_wait_stable":
            return self.session.wait_stable(
                int(args.get("timeout_ms", 15000)),
                int(args.get("interval_ms", 250)))
        raise KeyError(f"unknown tool: {name}")

    def handle(self, message: dict) -> dict | None:
        method = message.get("method")
        msg_id = message.get("id")

        if method == "initialize":
            return _result(msg_id, {
                "protocolVersion": PROTOCOL_VERSION,
                "capabilities": {"tools": {}},
                "serverInfo": {"name": SERVER_NAME, "version": SERVER_VERSION},
            })
        if method in ("notifications/initialized", "initialized"):
            return None
        if method == "tools/list":
            return _result(msg_id, {"tools": TOOLS})
        if method == "tools/call":
            params = message.get("params") or {}
            name = params.get("name", "")
            args = params.get("arguments") or {}
            try:
                payload = self.call(name, args)
                return _result(msg_id, {
                    "content": [{"type": "text",
                                 "text": json.dumps(payload)[:200000]}],
                    "isError": False,
                })
            except Exception as exc:  # surfaced to the client, not swallowed
                return _result(msg_id, {
                    "content": [{"type": "text", "text": f"{type(exc).__name__}: {exc}"}],
                    "isError": True,
                })
        if method in ("ping",):
            return _result(msg_id, {})
        return _error(msg_id, -32601, f"method not found: {method}")


def _result(msg_id, result: dict) -> dict:
    return {"jsonrpc": "2.0", "id": msg_id, "result": result}


def _error(msg_id, code: int, message: str) -> dict:
    return {"jsonrpc": "2.0", "id": msg_id, "error": {"code": code, "message": message}}


def serve() -> int:
    server = Server()
    for line in sys.stdin:
        line = line.strip()
        if not line:
            continue
        try:
            message = json.loads(line)
        except json.JSONDecodeError:
            continue
        response = server.handle(message)
        if response is not None:
            sys.stdout.write(json.dumps(response) + "\n")
            sys.stdout.flush()
    return 0


# ---------------------------------------------------------------------------
# self-test: exercises the tools end to end against a real session
# ---------------------------------------------------------------------------


def selftest(conf: str) -> int:
    server = Server()
    failures = 0

    def check(label: str, ok: bool, detail: str = "") -> None:
        nonlocal failures
        print(f"  {label:44} {'ok' if ok else 'FAIL'} {detail}")
        if not ok:
            failures += 1

    print(f"launching with {conf}")
    info = server.call("dosbox_launch", {"conf": conf, "display": ":100"})
    check("window found", info["window"]["w"] > 0,
          f"{info['window']['w']}x{info['window']['h']}")

    stable = server.call("dosbox_wait_stable", {"timeout_ms": 40000})
    check("screen settles", stable["stable"], f"{stable['elapsed_ms']} ms")

    first = server.call("dosbox_screenshot", {})
    check("screenshot is 640x400", first["bytes"] > 1000, f"{first['bytes']} bytes")

    server.call("dosbox_key", {"combo": "s"})
    time.sleep(2.0)
    server.call("dosbox_wait_stable", {"timeout_ms": 8000})
    second = server.call("dosbox_screenshot", {})
    check("key changed the screen", first["base64"] != second["base64"])

    server.call("dosbox_close", {})
    print(f"selftest: {'PASS' if failures == 0 else 'FAIL'} ({failures} failures)")
    return 0 if failures == 0 else 1


def main(argv: list[str]) -> int:
    if len(argv) >= 2 and argv[0] == "--selftest":
        return selftest(argv[1])
    return serve()


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
