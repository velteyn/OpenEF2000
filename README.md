# EF2000 Mission Explorer (prototype)

The first C++20 vertical slice: it reads the **original** `MISSION.CFG`,
`NAVIG.NAV` and `CONFLICT.CG` directly (no JSON intermediate), builds a
mission/world model, and renders a strategic map with a mission panel.

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

## Architecture

```
port/
  CMakeLists.txt
  src/
    model.h       typed mission/world model (Vec3, Mission, Route, Target, ...)
    parser.h/.cpp tokenizer + MISSION.CFG / NAVIG.NAV / CONFLICT.CG parsers
    render.h/.cpp SDL2 surface renderer, TTF labels, PNG export
    main.cpp      CLI, stats, PNG export, interactive loop
```

The parser is deliberately independent of SDL and the renderer; the same
`Dataset` can feed a future core, a campaign/mission generator, or tests.
`TACTCOM` navigation aliases (`BASE_NAVIGATION`, `3D_POSITION`,
`NAVIG_CORRIDOR`, `NAVIG_ROUTE`, `WAYPOINT`) are handled.

## Current limitations

* 2D strategic map only; no terrain/cockpit/3D.
* No resource/asset loading; no IFF/ILBM/ANIM/palette yet.
* No campaign/mission generator or GUI.
* No flight model, avionics, audio or network.
* Map labels can overlap; no zoom/pan yet.
* Mission index is the file order, not the in-game mission number.

## Next

1. Load IFF/ILBM, palettes and `.gui`/`.fad` assets into the viewer.
2. Add a mission briefing/debrief panel driven by the manual’s campaign UI.
3. Reimplement the operational mission generator against `MISSION.CFG`.
4. Add zoom/pan and a clickable target/mission list.
