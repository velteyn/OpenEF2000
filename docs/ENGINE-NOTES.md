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
  `key <k>` in the `.gui` file, so pressing that key activates it. This doubles
  as a check that the DSL's bindings are read correctly.
- **Synthetic mouse clicks need a long press.** A guest polling the PS/2 mouse
  can miss a short synthetic click; holding the button for about a second
  registers reliably.

### Confirmed so far

| Observation | Conclusion |
|---|---|
| Pressing `C` opens **Campaign Level Setting** (TopGun…Rookie, "Player Performace / Influence Campaign?" Yes/No, Accept) | `gpwClickCampaign` opens the `campaign_level` window |
| That dialog's position, size, labels and the `Performace` typo match `gui\mainmenu.gui` exactly | the parser reads geometry and localised text correctly |
| Hovering a panel highlights its title bar; the pointer is an X | selection is driven by `select entering` / `unselect leaving`, as modelled |
| Pressing `Esc` at the main menu does nothing | the menu bar's `efamenu` widget also binds `esc` (with no routine) and wins over `quitsel`; this matches the runtime's topmost-window-first dispatch |
| A synthesised click on the `campaign_level` **Yes** checkbox toggles it | widgets that bind `call click` do receive synthetic input |

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

### Harness limitation: `call release` is not reachable synthetically

The main-menu panels bind `call release`, and no combination tried here
activated them: press durations of 0.3 s, 1.0 s and 1.2 s; clicks on the body
and on the title strip; with and without a warm-up click; with motion after
release to force polling. Hover always worked, and a `call click` widget did
toggle, so input reaches the guest — the *release* transition does not.

The consequence is a boundary on what this harness can harvest:

- **reachable** — keyboard-bound widgets (`key`), and `call click` widgets;
- **not reachable** — window-level panels that activate on `call release`,
  which includes the Quit confirmation (`QuitEF2000` → `sure`) and campaign
  start (`gcStartNewCampaign`).

`Return` also failed to trigger `arcadesel` (`key ret`, `screen arcade`) even
though `C` works, so the keyboard path is not uniformly reliable either; the
likely cause is that `screen arcade` needs state that the main menu has not
set up, but that is unresolved.

### Where that leaves the routine harvest

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
* The GUI parses and renders, but screen navigation depends on routines from
  the original code that are not reconstructed yet.
* No campaign/mission generator.
* No flight model, avionics, audio or network.
* Map labels can overlap; no zoom/pan yet.
* Mission index is the file order, not the in-game mission number.

## Next

1. Reconstruct the GUI routine layer so the menus navigate.
2. Draw the mission planner map with its radar and SAM coverage overlays.
3. Put the meshes into the 3D world with terrain instead of one at a time.
4. Reimplement the operational mission generator against `MISSION.CFG`.
