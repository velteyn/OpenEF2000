# Engine notes

Implementation notes for the C++20 engine. The project reads the original
data files directly -- `MISSION.CFG`, `NAVIG.NAV`, `CONFLICT.CG`, the `DID.DAT`
archive, the compiled `.3` shape bytecode, textures and the `.gui` screens --
with no intermediate conversion step.

It is a viewer and verification tool, not yet a game. See the status table in
the top-level `README.md` for the current state of each subsystem.

## Build

Requirements: C++20, CMake, SDL2, SDL2_ttf, SDL2_image.

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

## Run

```bash
# counts and validation against the shipped data
./build/openef2000 --data /path/to/game-data --stats

# list missions
./build/openef2000 --data /path/to/game-data --list | head

# export a map
./build/openef2000 --data /path/to/game-data --mission 0 --focus \
    --export mission000.png

# interactive window
./build/openef2000 --data /path/to/game-data --focus --interactive

# 3D shapes straight out of DID.DAT
./build/openef2000 --data /path/to/game-data --mesh efa \
    --export-mesh efa.obj --mesh-preview efa.png

# boot like the game: title card, then the main menu
./build/openef2000 --data /path/to/game-data --boot

# planner map with coverage overlays
./build/openef2000 --data /path/to/game-data --bounds 0,0,400,400 \
    --sam-coverage --ewr-coverage --export planner.png
```

### Coverage overlays

`--sam-coverage` and `--ewr-coverage` mirror the planner's `mpSamCoverage` and
`mpEWRCoverage` toggles. Ground units come from `GROUP` records in
`MISSION.CFG` (`PATRIOT_1/2`, `ROLAND`, `RAPIER`, `SA_6`, `SA_11`,
`MARCONI_S373/S723`, `ZSU_23_4_SHILKA`, …); rings are coloured by the side that
owns the unit.

**The radii are assumptions, not data.** The shipped files carry no range per
unit type, so the renderer uses fixed per-class values in mission grid cells —
8 (AAA), 25 (SAM), 60 (EWR) — and scales them with `--coverage-scale`. Treat
them as a placeholder until a range table is found.

A site is referenced by many mission records, so rings are deduplicated on
(position, side, class); without that the map becomes a ball of circles.

Options:

```
--data DIR          data directory (default: ./game-data)
--mission N         mission index
--stats             dataset counts
--list              list missions
--focus             fit the map to the selected mission
--export FILE.png   render to PNG
--interactive       SDL2 window
--models            archive summary + CLUMP_3.TXT coverage
--mesh NAME         load one .3 shape (e.g. efa, f22, mig_29)
--lod N             level of detail (default 0)
--export-mesh FILE  write the mesh as Wavefront OBJ
--mesh-preview FILE render a side/top/front PNG contact sheet
--camera YAW,PITCH[,ZOOM]  viewer camera angles in degrees
--clumps FILE       clump-name list for the model viewer
```

### Model viewer

`--mesh NAME --interactive` opens an orbit-camera viewport. The model list is
read from `CLUMP_3.TXT` (or a built-in aircraft list when the file is absent).

| input | action |
|---|---|
| drag / arrow keys | orbit |
| mouse wheel, `+` / `-` | zoom |
| `N` / `P` | next / previous model |
| `L` | cycle level of detail |
| `W` | wireframe |
| `C` | backface culling |
| `D` | marking decals |
| `Esc` | quit |

Controls in the window: `N`/`Down` next mission, `P`/`Up` previous,
`Home`/`End`, `S` save `ef2000-mission.png`, `Esc` quit.

## What it renders

* all `NAVIG.NAV` bases as small squares (blue = airfield, green = port)
* all mission target areas as small dots coloured by side
* selected mission routes as yellow lines with waypoint circles
* selected mission target areas as red circles with labels
* attack-pattern nodes/routes as orange dots/lines
* right panel: mission type/side/role, counts, routes, target list, mission list
* `--focus` fits the map to the selected mission instead of the whole theatre

## Validation

`--stats` on a retail installation matches an independent parser exactly:

| Metric | Value |
|---|---:|
| missions | 739 (declared 739) |
| routes | 802 |
| waypoints | 2,918 |
| flights | 242 |
| pylons | 4,840 |
| targets | 802 |
| target areas | 18 |
| attack patterns | 32 |
| attack routes | 192 |
| attack nodes | 750 |
| bases | 77 |
| base navigations | 1,118 (declared 1,118) |
| BNA waypoints | 23,657 |
| conflict areas | 542 (declared 542) |

## 3D models

`did.h/.cpp` reads `DID.DAT`/`DID1ST.DAT` in both directory variants (`plain8`
for EF2000/Super EF2000/`DID1ST.DAT`, `xorv2` for v2.0). Entries are addressed
by hashing an eight-character canonical name: the directory prefix and the
extension are replaced by their indices in the archive's own tables.
`ra.h/.cpp` decompresses the RA blocks, `shape3d.h/.cpp` parses the `.3`
container and interprets its bytecode (a small VM that emits triangles from an
object-space vertex array), and `meshview.h/.cpp` exports OBJ or renders a
contact sheet.

The EFA exterior mesh measures 922 x 460 x 1339 units (10.95 x 5.47 x 15.96 m)
and produces 288 triangles; the corresponding cockpit model produces 611. Both
were cross-checked against an independent parser during development.

`EF_SHAPE_TRACE=1` prints the opcode histogram of the last executed shape.

## Architecture

```
CMakeLists.txt
src/
    model.h        typed mission/world model (Vec3, Mission, Route, Target, ...)
    parser.h/.cpp  tokenizer + MISSION.CFG / NAVIG.NAV / CONFLICT.CG parsers
    render.h/.cpp  SDL2 surface renderer, TTF labels, PNG export
    did.h/.cpp     DID.DAT directory, canonical names, entry reads
    ra.h/.cpp      RA block decompressor
    shape3d.h/.cpp .3 container + bytecode virtual machine
    texture.h/.cpp .tm atlases, colours\2000.col, init3d slots, coltab
    mesh.h         Vec3f / MeshTriangle / Mesh
    meshview.h/.cpp OBJ export + PNG contact sheet
    gui.h/.cpp     .gui DSL parser, screen stack, event dispatch
    guirender.h/.cpp GUI panels, frames, text, backdrop
    ilbm.h/.cpp    ILBM decoder for screens and backdrops
    main.cpp       CLI, stats, PNG export, interactive loop
```

The parser is deliberately independent of SDL and the renderer; the same
`Dataset` can feed a future core, a campaign/mission generator, or tests.
`TACTCOM` navigation aliases (`BASE_NAVIGATION`, `3D_POSITION`,
`NAVIG_CORRIDOR`, `NAVIG_ROUTE`, `WAYPOINT`) are handled.

## Runtime observation

Two questions resist static analysis: what a `routine` name does, and what the
terrain height field is. Both are answered by running the original software and
watching it, which is legitimate interoperability work and stays inside the
clean-room rules as long as only *facts* are recorded — no screenshots or
captured assets go into the repository.

### Harness

The shipped DOS build runs under DOSBox-X. A throwaway config mounts the
installation and starts `EF2000.BAT`, which runs `_ef2000.exe gui`:

```
[sdl]
fullscreen = false
output = surface
windowresolution = 1024x768
[dosbox]
machine = svga_s3
memsize = 32
[cpu]
core = auto
cycles = 30000
[mixer]
nosound = true
[autoexec]
mount c "<installation>"
c:
EF2000.BAT
```

Two practical notes learned the hard way:

- **The keyboard is the reliable input path.** Each menu widget declares
  `key <k>` or `skey <k>` in the `.gui` file, so pressing that key activates it.
  This doubles as a check that the DSL's bindings are read correctly.
- **Synthetic mouse clicks do not dispatch.** The pointer moves and the guest
  receives the button, but the engine's own GUI does not act on it. Use the
  keyboard bindings; the input section below has the evidence.

### Confirmed so far

| Observation | Conclusion |
|---|---|
| Pressing `C` opens **Campaign Level Setting** (TopGun…Rookie, "Player Performace / Influence Campaign?" Yes/No, Accept) | `gpwClickCampaign` opens the `campaign_level` window |
| That dialog's position, size, labels and the `Performace` typo match `gui\mainmenu.gui` exactly | the parser reads geometry and localised text correctly |
| Hovering a panel highlights its title bar; the pointer is an X | selection is driven by `select entering` / `unselect leaving`, as modelled |
| Pressing `Esc` at the main menu does nothing | the menu bar's `efamenu` widget also binds `esc` (with no routine) and wins over `quitsel`; this matches the runtime's topmost-window-first dispatch |
| `R`/`P`/`T` move the Quick Combat difficulty radio (`gpwChangeDifficulty`); `F1`..`F12` select missions and rewrite the Summary (`gpwSelectLevel`); `Return` accepts (`gpwArcadeAccepted`) | per-icon `skey`/`key` bindings dispatch the same routines as the mouse |

### Boot sequence

`supergui\gui.txt` holds a splash for 200 ticks before the main menu
(`STAT didlog 200 FADE_IN NFADE_OUT NO_TEXT`). `didlog` is not present in the
archive under any name tried, but `iff\256\load0.lbm` (640x400) is the shipped
title card — the EF2000 V2.0 screen — and `iff\256\load.lbm` (320x200) is the
TFX Eurofighter logo. `--boot` shows `load0` for two seconds and then loads
`gui\mainmenu.gui`.

### Panel colours are shaded, not palette lookups

The running menu draws its panels as a dithered gradient: sampling one panel
gives (77,77,77) for the body, (85,85,85) for the title strip and a spread of
neighbouring greys, while the reconstruction draws a flat fill.

This is not a palette mix-up that can be fixed by choosing the right file. All
149 palettes in the archive were checked for a palette whose *low* indices
(0..15) contain those greys, which is where a widget renderer would plausibly
look, and none does; the nearest matches sit at high indices (~186..196) in
several palettes, which is consistent with a shade/fade table rather than a
direct index.

Reproducing the look therefore needs the original widget drawing code, and a
closer approximation means drawing a gradient between two entries rather than
a flat fill. Recorded so the palette hunt is not repeated.

### Behaviours confirmed by play

Reported from a play session and corroborated against the `.gui` files:

| Menu entry | Behaviour | Evidence |
|---|---|---|
| **Simulator** | opens **Type of Mission** listing Free Flight, Basic Training, Weapon Training, Air to Air Combat, Ground Attack, Advanced Training, Custom Air to Air | `simulsel` carries `screen simpick`; `simpick.gui` contains those labels verbatim |
| **Credits** | opens a large panel of credits text | `showcred` carries `openwindow credits`; the window is empty and the text is `supergui\gfxcred.txt` (5.5 KB, "DID Graphics Plus" credits, beta testers, Rendition involvement) |
| **Multiplay** | opens two mini windows, **Network Settings** and **IPX Settings** | `screen gnet`; the windows carry `onopen gnInitSettingsWindow` and are raised by `startup gnStartup` |
| **Quit** | opens a small dialog "Quit EF2000 ?" with Yes/No | `QuitEF2000` → the `sure` window, whose text is literally `Quit EF2000 ?` |
| **Campaign** | opens **Campaign Level Setting** | confirmed earlier by pressing `C` |
| **Quick Combat** | Difficulty (Rookie/Pro/Topgun), Mission Select 01–12, **Quick Combat Hi-Scores** (Name/Level/Score), Summary with Accept | `arcade.gui` contains those windows; the included `archigh.gui` supplies the hi-score panel, and the rows come from `HISCORES.DAT` |

Two of these needed code rather than data: `QuitEF2000` had to raise `sure`,
and `gnet` needed `gnStartup` to raise its panels, because screens declare a
`startup` routine that the runtime previously ignored. The runtime now runs a
screen's `startup` routine when one is registered and reports it as missing
otherwise.

The credits panel is the one place where the text resource had to be linked to
the window by hand: the window is empty in `mainmenu.gui`, and the only
matching resource is `supergui\gfxcred.txt`.

### Install differences

Three installs were compared at the data level:

| Install | Archive | `gui\*` | `supergui\gfxcred.txt` |
|---|---|---|---|
| Original DOS v2 | `xorv2`, 8,261 entries | identical bytes to 3DFX | present |
| 3DFX v2 | `xorv2`, 8,261 entries | reference | present |
| TACTCOM | `plain8`, 8,469 entries | smaller, different files | absent |

`mainmenu`, `arcade`, `simpick`, `gnet`, `archigh` and `campaign` are
byte-identical between the two v2 archives, so observations made on one hold
for the other. TACTCOM has its own cut-down variants, which is why the credits
panel needs `gfxcred.txt` only on the v2 builds.

`HISCORES.DAT` differs per installation (developer names differ between DOS and
3DFX), so hi-score contents are never a reliable test fixture.

### Harness: DOSBox-X

Two layers, built in this order.

`tools/dosbox-x-mcp/` drives a DOSBox-X session from outside: launch,
screenshot, keys and clicks in guest coordinates. Deterministic capture, but
host-level input.

A local DOSBox-X build with an **in-process** MCP server goes further: tools
call the emulator's own input and renderer paths, and it adds guest memory
access and an instruction tracer. Captures come from the renderer, buttons go
through `Mouse_ButtonPressed`, and the tracer records `CS:EIP`, registers and
opcode bytes. This is what produced the input findings below. It is a patch
series against DOSBox-X, not part of this repository.

Neither tool contains game data, and neither is needed to build or run
OpenEF2000.

### Spice86: launches, but DOS4GW aborts

Spice86 (source checkout at `~/projects/Spice86`, MCP on port 8081) starts the
DOS build and reaches the extender, then the program terminates with exit code
1. The evidence, from a run with `--CpuModel INTEL_80386`:

```
INT21H AH=4Ah: MCB ... Size: 40594  "is too small for requested size 65535"
Unhandled INT2F DOS VM subfunction: 1687      <- DPMI installation check
INT21H/4B: programName="EF2000.DAT", loadType=LoadOverlay
INT21H AH=4Ch: TERMINATE with exit code 01
Executed 5659 instructions
```

So the extender asks DOS to grow its block to 1 MB, is refused (only ~634 KB of
conventional memory exists), probes for a DPMI host through INT 2F/1687 — which
Spice86 reports as unhandled — then loads its overlay and gives up.

Getting EF2000 to run under Spice86 therefore needs emulator work, not
configuration: at minimum answer INT 2F/1687 the way a non-DPMI DOS would, and
check how 4Ah refuses the grow (DOS should still report the largest block in
BX). Nothing in the run suggests a problem with the game data.

**The dump/C# feature works and is automatic.** Every launch writes a folder
named after the executable's SHA256, containing:

| File | Contents |
|---|---|
| `spice86dumpMemoryDump.bin` | full memory image |
| `spice86dumpListing.asm` | annotated assembly listing |
| `spice86dumpCfgBlocks.json` / `CfgPartitions.json` / `CfgReload.json` | control-flow graph |
| `spice86dumpExecutionFlow.json` | observed execution |
| `spice86dumpCpuRegisters.json` | register state at exit |
| `spice86dumpGhidraSymbols.txt` | symbols for Ghidra |
| `spice86dumpCfgGeneratedOverrides.cs`, `GeneratedProject/` | generated C# skeleton |

Because the run died in the extender, that dump covers only the DOS stub, so it
is not yet useful for the GUI or terrain work.

### Quick Combat and its mission descriptions

Quick Combat was observed once, and matches the description from play: the
Difficulty panel, Mission Select 01..12, Summary and Accept, plus the hi-score
panel.

Its Summary text is mission data, not GUI text. The archive holds a run of
eleven short text entries (indices 4779–4797 in the v2 archive) that are the
per-mission descriptions, for example:

```
Emergency intercept. Destroy incoming hostile bombers and escort aircraft.
Intercept and destroy enemy MI26 transport helicopters.
Destroy enemy radar and SAM sites in the mountains.
```

Their names are not recoverable by hashing plausible stems, so the engine cannot
address them by name yet; the content is documented here so it can be located
again.

### Mouse clicks reach the guest, but the GUI does not dispatch `call release`

A driver-level trace settles the old ambiguity. The engine reads INT 33h
function 3 every frame, and its own button state flips while the button is
held and back on release; a real-mode probe confirms `BX=0001` during the
hold. The guest polls the driver hundreds of times per click.

What does not happen is the widget dispatch. An instruction trace covering
tens of thousands of instructions around a press and release shows no call
into a GUI routine, and the screen never changes. The engine gates its button
reads behind a driver-present flag set at startup from INT 33h function 0;
that flag is open, so the failure is later, inside its own event state
machine.

Hover still works because position and motion take a different path — the
engine reads the relative motion counters (function 0x0B) and maintains its
own cursor.

So synthetic clicks are not a usable harvesting route for this engine.
Keyboard is.

### Keyboard is the authoritative path

Verified on the main menu and Quick Combat:

- main menu: `S` simulator, `C` campaign, `M` multiplay, `Esc`; `Return`
  opens Quick Combat.
- difficulty: `R` rookie, `P` pro, `T` topgun (`gpwChangeDifficulty`); the
  radio indicator moves.
- mission: `F1`..`F12` (`gpwSelectLevel`); the grid selection and Summary
  text change.
- accept: `Return` (`gpwArcadeAccepted`); the mission loads and the cockpit
  appears.

Panels bound to `call release` fire when the same icon also has a `key` or
`skey` binding, because the binding takes the same routine path. Arrow keys
and Tab do nothing on these screens: bindings are per-icon hotkeys, not a
focus ring.

`skey` is a **screen-local binding, not Shift+key**: plain `R`/`P`/`T` and
`F1`..`F12` trigger it in the shipped game.

### Where that leaves the routine harvest

All three arcade routines are now harvested this way, and are implemented in
the runtime: `--gui arcade --gui-routines` reports 4 of 4 (the three above plus
`InitArcade`). The main-menu routines also have bindings (`C` for campaign,
`S`, `M`, `Esc`), so the keyboard route covers every panel that declares one;
only icons with no `key`/`skey` remain out of reach, and none of those are
known yet.

Three ways forward, in increasing cost:

1. **Ask for observations from play.** Someone playing the game can report what
   each menu entry does; that is the cheapest source for the remaining names.
2. **Spice86.** It emulates the hardware rather than forwarding host input, so
   its mouse button handling may latch the transitions DOSBox-X drops.
3. **Repack a scratch copy of the archive** with `call release` changed to
   `call click` for the panels, and observe the handlers. This modifies only a
   local scratch copy of the data, never the repository, and would settle every
   remaining main-menu routine quickly.

### Palette discrepancy

The running menu draws its panels **grey-green**; the reconstruction draws them
blue-grey from `colours\gui.col`. This build's GUI palette is therefore not
`gui.col`. That is a concrete rendering difference that file inspection alone
did not reveal.

## Menu system

EF2000 ships its GUI in two DiD archives, both readable with the same
`plain8` directory:

| Archive | GUI files | Notes |
|---|---|---|
| `DID.DAT` | 30 | first release |
| `DID1ST.DAT` | 27 | later build with larger screens; adds `missplan`, `summary`, `custom`, `directp`, `intro`, and the Campaign Level Setting dialog inside `mainmenu` |

The runtime prefers `DID1ST.DAT` when it is present, because that is the set
the shipped executable uses; `--archive FILE` overrides. `--gui-dump` prints
the windows, icons, keys, `esg` groups and verbs of a screen, which is how the
map below was built.

Observed shell flow:

- main menu: `S` simulator, `C` campaign, `M` multiplay, `Esc` quit,
  `Return` quick combat;
- `C` opens Campaign Level Setting: ten difficulty rows in one `esg` group,
  an Influence Yes/No pair, and Accept;
- Accept raises "Generating Campaign...", the `generatecamp` window, whose
  `onopen` hook is `gpwGenerateCampaign`;
- Quick Combat: `R`/`P`/`T` difficulty, `F1`..`F12` mission, `Return` accept.

Simulator flow, from the later screen set:

- `simpick` offers seven mission types; each switches to `simul` (the seventh
  goes to the `custom` editor);
- `simul` lists missions (`esg 1`); selecting one opens `miss_info`, whose
  Accept opens `params`;
- `params` groups time, weather, default weapons, weapons review and
  difficulty, each an `esg` group, and Accept calls `gpwMissionAccepted`;
- `custom` configures friendlies, enemies, guns-only, location, range, time,
  weather, difficulty and an enemy type from a 34-entry list;
- `simul2` is the debrief, with one tab per pilot.

The runtime records these choices (`sim_mission`, `sim_time`,
`sim_weather`, `sim_difficulty`, weapon options, the custom setup, the debrief
tab). Starting the mission is recorded as `mission_accepted`; the mission
loader itself is not reconstructed.

Campaign shell (`camp`): map option toggles (flags, SAM and EWR coverage),
side and layer map modes, zoom, mission list, 8-hour time advance, target view,
debrief tabs/next, accept/ready and campaign exit. Coverage is 30 of 31 (only
the shared help overlay is missing).

Mission planner (`missplan`): map options, strike and target lists, role and
aircraft type, four mission slots, assignment cursor (prev/next/make/view),
waypoint speed and clearance, ATC selection, fly/return and the scrap-mission
confirmation. Coverage is 42 of 43 (again only the help overlay).

The map is now an interactive strategic display, not a blank panel. It is
rendered bare and driven by the GUI state:

- the campaign map frames the whole theatre; the planner frames the selected
  mission, with a 160-unit minimum so a one-waypoint mission still shows
  context;
- **z-In/z-Out** change the zoom and **wheel over the map zooms around the
  cursor**; dragging inside a map window pans. A press that does not move is
  treated as a click, so panning never edits the plan;
- the **side** (Nato/Russian/Neutral) and **layer** (air defences, air support,
  navy, comms, supply, political) checkboxes filter what is drawn. Ground units
  are squares, flights diamonds, airbases open boxes and target areas circles,
  coloured by side. Unknown site kinds stay visible under every layer, and
  unknown sides are not filtered out;
- clicking a target area selects it and updates the strike list; clicking a
  base records it as the takeoff base; clicking empty map appends a waypoint,
  and Clear Route removes the appended points. These edits live in the GUI
  model, so the rendered map surface stays cacheable.

The original campaign screen was captured at **640x400** through the DOSBox-X
control server. It draws every side's flags by default, so the reconstruction
initialises both filters to "all" and treats the checkboxes as an additive
filter (nothing checked shows everything). That capture also confirms the
original's own bottom bar is clipped at y=400, matching this layout.

Mission and attack-pattern coordinates live in different spaces: pattern nodes
are local to the aim point (values in the thousands) and must not enter the map
extent, or the whole theatre collapses to a dot. They are drawn by the target
view, not the strategic map.

Summary (`summary`): menu bars, detail requests and the campaign exit. Coverage
is 13 of 13, but the bar and graph drawing routines are renderer-owned no-ops:
the statistics are not computed or drawn yet.

Weapons configuration (`weapons`): pylon/missile selection, four aircraft view
presets, rotation, clear-one/clear-all, defaults and accept — 16 of 16.

Quick Combat post-mission (`arcade2`): continuation, hi-score name acceptance
(the name is read from the enter-name field; persistence is the driver's job),
round advance and endgame — 5 of 6. `system` (memory readout, magnifier) is
2 of 2 as renderer hooks.

There is no in-flight menu in these screens: the aircraft is flown from key
bindings and the joystick. The communication menu you remember belongs to the
simulation UI, not the `.gui` shell. `players`, `rxmiss`, `txmiss`, `directp`
and `gnet` are network screens (linked players, network mission/briefing,
DirectPlay) and are out of scope.

`--gui-routines` tracks the routine layer: mainmenu 17 of 19, arcade 4 of 5,
arcade2 5 of 6, simulator 7 of 8, simul2 3 of 4, simpick 1 of 2, custom 5 of 6,
campaign 30 of 31, planner 42 of 43, summary 13 of 13, weapons 16 of 16,
system 2 of 2, intro 16 of 18. The only non-network shortfall is the shared
help overlay (`GenGUIHelp`, `CampaignHelp`, `MissplanHelp`, `CampaignHelp`):
the install ships no readable help content, so the overlay cannot be
reconstructed from data.

Two `.gui` parser facts fell out of the map work. Window-level verbs exist:
the map windows write `call Pressed` / `call Release` followed by
`routine mpMapSelect` (the installed missplan variant) or `CampMoveWP` (the
campaign map), and clicks that miss every icon must reach them. The loader now
keeps those verbs on `GuiWindow` and `GuiSystem::fire_window` dispatches them.
Also, `esg 0` means "no exclusive group": the map-option checkboxes and plain
buttons use it, so only positive `esg` values clear their siblings.

The goal is **feature parity, not pixel parity**. Rendering is deliberately a
separate layer from the GUI model: `.gui` data, runtime state and routines are
independent of `guirender.cpp` and the map/mesh renderers. That keeps the door
open for higher-detail terrain and aircraft later without touching the
simulation or UI logic.

Window hooks (`onopen`/`onclose`) are routine names, and the runtime now runs
them when a window opens or closes. That is how the credits list scrolls to
the top, and how the generating window enters the campaign flow.

## Current limitations

* The strategic map is still 2D; meshes are viewed in their own window, not
  placed in a 3D world yet.
* Textures are `tm\<NAME>.tm` (256 x 192 indexed) coloured with
  `colours\2000.col`; `--palette NAME` overrides and `--texture NAME` forces
  one atlas. Markings are small `MARKINGS` patches and look stippled when
  magnified because index 0 is the transparency key.
* Airframes are almost entirely flat-shaded with the lighting baked into the
  mesh, so they read as grey panels with a handful of textured decals.
* Meshes are open shells with mixed winding, so backface culling is off by
  default.
* ILBM screens load; ANIM playback is not implemented.
* DID1ST screens carry no palettes (and no colour directives), so the renderer
  reads `colours\gui.col` from the companion DID.DAT. Screens are legible, but
  the original grey-green bevel and the `picture` backdrop are still
  approximations.
* The GUI parses and renders, but screen navigation depends on routines from
  the original code that are not reconstructed yet. Quick Combat's difficulty,
  mission selection and Accept are implemented; its Summary text (mission
  data) and the mission loader are not.
* No campaign/mission generator.
* No flight model, avionics, audio or network.
* Map labels can overlap; no zoom/pan yet.
* Mission index is the file order, not the in-game mission number.

## Next

1. Reconstruct the GUI routine layer so the menus navigate.
2. Draw the mission planner map with its radar and SAM coverage overlays.
3. Put the meshes into the 3D world with terrain instead of one at a time.
4. Reimplement the operational mission generator against `MISSION.CFG`.
