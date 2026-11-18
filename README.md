# EF2000 Mission Explorer (prototype)

The first C++20 vertical slice: it reads the **original** `MISSION.CFG`,
`NAVIG.NAV` and `CONFLICT.CG` directly (no JSON intermediate), builds a
mission/world model, and renders a strategic map with a mission panel.

It also reads the **original** `DID.DAT` archive and the compiled `.3` shape
bytecode, so aircraft meshes come straight out of the game data.

It is a viewer, not the game. No assets, renderer, flight model, campaign
generator or GUI are implemented yet.

## Build

Requirements: C++20, CMake, SDL2, SDL2_ttf, SDL2_image.

```bash
cmake -S port -B port/build -DCMAKE_BUILD_TYPE=Release
cmake --build port/build -j
```

## Run

```bash
# counts and validation against the shipped data
./port/build/ef2000-explorer --data EF20003DFX2 --stats

# list missions
./port/build/ef2000-explorer --data EF20003DFX2 --list | head

# export a map
./port/build/ef2000-explorer --data EF20003DFX2 --mission 0 --focus \
    --export mission000.png

# interactive window
./port/build/ef2000-explorer --data EF20003DFX2 --focus --interactive

# 3D shapes straight out of DID.DAT
./port/build/ef2000-explorer --data EF20003DFX2 --mesh efa \
    --export-mesh efa.obj --mesh-preview efa.png
```

Options:

```
--data DIR          data directory (default: ../EF20003DFX2)
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
```

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

`--stats` on `EF20003DFX2` matches the Python parser exactly:

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

The EFA mesh measures 922 x 460 x 1339 units (10.95 x 5.47 x 15.96 m) and
matches the Python reference implementation (`re/tools/three_d.py`) exactly:
611 triangles for LOD 0.

`EF_SHAPE_TRACE=1` prints the opcode histogram of the last executed shape.

## Architecture

```
port/
  CMakeLists.txt
  src/
    model.h        typed mission/world model (Vec3, Mission, Route, Target, ...)
    parser.h/.cpp  tokenizer + MISSION.CFG / NAVIG.NAV / CONFLICT.CG parsers
    render.h/.cpp  SDL2 surface renderer, TTF labels, PNG export
    did.h/.cpp     DID.DAT directory, canonical names, entry reads
    ra.h/.cpp      RA block decompressor
    shape3d.h/.cpp .3 container + bytecode virtual machine
    mesh.h         Vec3f / MeshTriangle / Mesh
    meshview.h/.cpp OBJ export + PNG contact sheet
    main.cpp       CLI, stats, PNG export, interactive loop
```

The parser is deliberately independent of SDL and the renderer; the same
`Dataset` can feed a future core, a campaign/mission generator, or tests.
`TACTCOM` navigation aliases (`BASE_NAVIGATION`, `3D_POSITION`,
`NAVIG_CORRIDOR`, `NAVIG_ROUTE`, `WAYPOINT`) are handled.

## Current limitations

* The strategic map is still 2D; meshes are exported/rendered offline, not
  placed in a 3D scene yet.
* Textures and UVs are parsed but not sampled, so meshes are flat-shaded.
* No resource/asset loading; no IFF/ILBM/ANIM/palette yet.
* No campaign/mission generator or GUI.
* No flight model, avionics, audio or network.
* Map labels can overlap; no zoom/pan yet.
* Mission index is the file order, not the in-game mission number.

## Next

1. Put the loaded mesh in an actual 3D view with lighting and a camera.
2. Load IFF/ILBM, palettes and `.gui`/`.fad` assets into the viewer.
2. Add a mission briefing/debrief panel driven by the manual’s campaign UI.
3. Reimplement the operational mission generator against `MISSION.CFG`.
4. Add zoom/pan and a clickable target/mission list.
