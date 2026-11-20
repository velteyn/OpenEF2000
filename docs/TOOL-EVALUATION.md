# Tool evaluation list

Tools considered for reverse engineering, reconstruction and verification,
with the decision and the next experiment for each.

| Tool | Role | Status | Verdict |
|---|---|---|---|
| DOSBox-X + in-process MCP (`tools/dosbox-x-mcp`) | Run the original with graphics, input, screenshots, memory and tracing | Adopted | Primary observation harness; keep |
| REA + Ghidra (REA 6.9 / Ghidra 12.1) | Native/LE static analysis, decompilation, call graphs, checkpoints | Adopted | Primary static-analysis harness; keep |
| **kvikdos-ng** (`xor2003/kvikdos`, fork of `pts/kvikdos`) | Headless, KVM-accelerated DOS emulator with protected-mode/DPMI support and call/RAM-dump hooks | **To evaluate** | High potential for a headless generator oracle; not a DOSBox-X replacement |
| EMU2 | Simple headless 80286 DOS emulator | Not adopted | No protected mode, no graphics; less capable than the current harness |
| dosemu2 | DOS emulator with KVM | Not adopted | Larger scope; no MCP-style control and no advantage over DOSBox-X for this game |
| MS-DOS Player | DOS API + CPU emulation | Not adopted | Windows-oriented embedding; no graphics observation path |

## kvikdos-ng

**Source:** <https://github.com/xor2003/kvikdos> (fork of
<https://github.com/pts/kvikdos>)
**License:** GPL-2.0-or-later.

### What it is

A very fast **headless** DOS emulator for Linux (KVM) and Windows (WHPX).
It emulates a small DOS/BIOS subset and runs the guest CPU natively through
the hypervisor. It supports:

* 16-bit real mode and 32-bit protected mode;
* Phar Lap 386|DOS-Extender/TNT bound programs;
* DPMI clients through a resident host (CWSDPMI, HDPMI32, …), with an
  embedded HDPMI32 fallback;
* DOS files/directories, batch files, XMS/EMS subsets;
* a minimal 80x25 text console and an 80x25 text mode for IDEs.

It deliberately has **no graphics, mouse, joystick, sound or CD-ROM**, and
runs **one DOS program at a time**.

### Why it is interesting for OpenEF2000

Two features matter for the campaign generator work:

1. **Arbitrary guest subroutine calls.** The emulator exposes
   `--call-near`, `--call-far`, `--call-ss` and `--call-set` to call a guest
   routine at an address and probe its result. If EF2000 loads under
   kvikdos, we can call the campaign generator entry directly instead of
   driving the Campaign Level dialog.
2. **Guest RAM dumps.** `--hlt-ok` / `--hlt-dump=FILE` dump guest memory.
   Together with a direct generator call, this can expose the internal
   target/base/site tables that are still blocking byte-exact
   reproduction.

A third, practical advantage is speed: KVM makes many generator runs cheap,
which suits fixed-seed sweeps and byte comparisons.

### Limits and risks

* **No graphics or mouse.** It cannot replace DOSBox-X for the GUI, maps,
  planner interaction or screenshots.
* **EF2000 uses DOS/4GW.** kvikdos-ng documents Phar Lap and DPMI support,
  and says programs needing DOS4GW "may work through the `--dpmi=` host
  mechanism; coverage is incremental." Whether the shipped TACTCOM or v2
  executable runs is an open experiment, not an established fact.
* **No CD-ROM.** The v2 install may expect a CD data set; the disk-based
  TACTCOM install is the better first target.
* **KVM permission.** This host has `/dev/kvm` but the current user is not
  in the `kvm` group, so a run needs `sudo adduser "$(id -nu)" kvm`
  (or equivalent) and a re-login.
* **One program at a time**; no nested DOS.

### Decision

**Keep DOSBox-X + MCP as the primary harness.** kvikdos-ng is not a
replacement: the game is graphical and the current work needs screenshots,
mouse, map interaction and UI state.

**Evaluate kvikdos-ng as a generator oracle.** It is the only considered
tool that could run the generator headlessly with fixed inputs and dump the
internal tables. That is exactly the remaining gap for byte-exact
reproduction.

### Experiment plan (when scheduled)

1. Enable KVM for the user and verify `kvikdos --kvm-check`.
2. Build/run the **TACTCOM** `EF2000.BAT` or `_EF2000.EXE` from a writable
   data copy with `--dpmi=auto` (and, if needed, an explicit HDPMI32).
3. If it loads, reach the campaign screen once (for environment/paths), then
   use `--call-far` to invoke the generator entry (`0xbec20` in the v2
   image; locate the TACTCOM address by pattern) with the campaign globals
   preset.
4. Patch the custom RNG state (`0xb2458` in v2) through `--poke-word` or a
   small preload, run the generator for a fixed seed, and dump guest RAM.
5. Compare the written `CAMPAIGN.CG` / `MISSION.CFG` and the RAM dump with
   the captured DOSBox runs. Success would unblock the geometry/ownership
   tables and allow a fixed-seed byte-parity test.
6. If DOS/4GW does not run under the DPMI host, record the failure and keep
   the DOSBox-X memory/trace path.

**Priority:** medium. It does not block the current GUI/reconstruction work;
it is a targeted tool for the generator parity problem.

## Related alternatives

* **EMU2** — headless, 80286, no protected mode; simpler than kvikdos but
  cannot run the 32-bit game image.
* **dosemu2** — broader DOS environment with KVM; no built-in call/RAM-dump
  hooks and no MCP control surface.
* **MS-DOS Player** — embeds DOS programs into Win32; no graphics
  observation path for the original game.
