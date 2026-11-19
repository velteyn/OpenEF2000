# dosbox-x-mcp

An MCP (Model Context Protocol) control plane for a DOSBox-X session, so an
agent can drive and observe a DOS program deterministically.

It exists because driving DOSBox-X with host-level synthetic input is fiddly:
window placement changes, screenshots include window chrome and the host
pointer, and "did my input do anything?" is hard to answer. This server removes
those variables.

## Tools

| Tool | Purpose |
|---|---|
| `dosbox_launch` | start DOSBox-X with a config, wait for its window |
| `dosbox_close` | terminate the session |
| `dosbox_geometry` | window rectangle and guest rectangle in host coordinates |
| `dosbox_screenshot` | 640x400 PNG of the guest screen (base64, optional path) |
| `dosbox_key` | key combination, e.g. `Return`, `shift+F7`, `Escape` |
| `dosbox_type` | type text with a per-key delay |
| `dosbox_move` | move the guest pointer |
| `dosbox_click` | move, settle, press, hold, release |
| `dosbox_wait_stable` | block until two consecutive frames match |

**Coordinates are guest coordinates**: `x` in `0..639`, `y` in `0..399`. The
server translates to host window coordinates and captures only the guest
rectangle, so callers never deal with window placement or scaling.

## Requirements

- `dosbox-x`
- `xdotool`
- `ffmpeg` (with x11grab)

## Running

As an MCP server over stdio (newline-delimited JSON-RPC 2.0):

```bash
python3 dosbox_x_mcp.py
```

Register it with an MCP client, for example:

```json
{
  "mcpServers": {
    "dosbox-x": {
      "command": "python3",
      "args": ["/path/to/dosbox_x_mcp.py"]
    }
  }
}
```

Self-test, which exercises every tool against a real session:

```bash
python3 dosbox_x_mcp.py --selftest /path/to/dbx.conf
```

## Notes on the implementation

- **Guest rectangle.** DOSBox-X draws the emulated screen 1:1, centred
  horizontally and bottom-aligned vertically: the height difference is its own
  menu bar. Centring vertically would crop half the menu bar height off the top
  of the guest.
- **Host pointer.** `x11grab` composites the host pointer unless told
  otherwise, so captures pass `-draw_mouse 0`; the host pointer is not guest
  state.
- **Focus.** Pointer and key actions focus the window first, and pointer motion
  is sent twice, because some window managers need an event before motion
  registers.

## Known limitation: release-bound widgets

Reported here so the next person does not rediscover it. In the DOS program this
was built for, widgets bound to a **click** event activate under this server's
input, while widgets bound to a **release** event do not. Observed with a DOS
GUI whose panels bind `call release`:

- moving the pointer over a panel produces its hover highlight, so motion is
  delivered;
- a click with holds from 120 ms to 900 ms changes nothing;
- toggling DOSBox-X mouse capture (`ctrl+F10`) does not help — in capture mode
  absolute moves become relative, which moves the guest pointer unexpectedly;
- keyboard bindings do work.

The likely cause is that the program only performs a widget's release action if
it saw the press on that widget, and the press is not being delivered as a
distinct polled state. Any fix belongs in input delivery, not in this server.

## Status

Developed for an interoperability project that drives a 1995 DOS flight
simulator to observe its GUI behaviour. Split out on the suggestion of the
Spice86 maintainers, who pointed out that protected-mode binaries (DOS4GW and
friends) cannot run under their emulator because protected mode is out of scope
there.
