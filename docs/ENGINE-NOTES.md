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
| Pressing `C` opens a dialog titled **Campaign Level Setting** listing TopGun…Rookie, "Player Performace / Influence Campaign?" Yes/No and Accept | `gpwClickCampaign` opens the `campaign_level` window |
| The dialog's position, size, labels and the `Performace` typo match `gui\mainmenu.gui` byte for byte | the parser reads the window geometry and localised text correctly |
| Hovering a menu panel highlights its title bar; the pointer is an X | selection state is driven by `select entering` / `unselect leaving`, as modelled |

The `campaign_level` window is modal in practice: it has no `close` verb, so it
stays up until Accept. That limits how far a single run can explore without
starting a campaign.

The game's panels render grey-green; the reconstruction currently draws them
blue-grey from `colours\gui.col`, so the GUI palette for this build is not
`gui.col` and still needs identifying.

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
