# OpenEF2000

An independent, open-source engine reimplementation that runs the classic 1995
tactical flight simulator natively on modern operating systems.

OpenEF2000 is a **clean-room** project. It contains no original game code
and no original game assets. It reads the data files from a copy of the game
that you already own, and renders them with a modern engine.

> **Legal notice.** OpenEF2000 is not affiliated with, endorsed by, or
> connected to Digital Image Design, Ocean Software, Atari, Bandai Namco, or
> Eurofighter Jagdflugzeug GmbH. All trademarks are the property of their
> respective owners. See [Legal and trademark disclaimer](#legal-and-trademark-disclaimer).

---

## Table of contents

- [Overview](#overview)
- [Project status](#project-status)
- [Key features](#key-features)
- [Architecture](#architecture)
- [Requirements: bring your own assets](#requirements-bring-your-own-assets)
- [Build and run](#build-and-run)
- [Repository layout](#repository-layout)
- [Documentation](#documentation)
- [Contributing](#contributing)
- [Legal and trademark disclaimer](#legal-and-trademark-disclaimer)
- [License](#license)

---

## Overview

The target title is a 1995 DOS-era combat flight simulator with a dynamic
campaign, a large fixed-wing and rotary-wing fleet, and a software-rendered 3D
world. It was built for hardware that no longer exists: VGA palettes, a
32-bit DOS extender, and CPU-only rasterisation.

OpenEF2000 reimplements the engine on top of a portable C++20 core. It
consumes the original data archives directly — no conversion step, no
intermediate asset pipeline — so the behaviour being reproduced is the
behaviour of the shipped game, not a modern reinterpretation of it.

The design goals are:

- **Native.** Runs on Linux, macOS and Windows from a single codebase.
- **Faithful.** Original archives, palettes and file formats are read as-is.
- **Verifiable.** Every subsystem is expected to be checkable against the data
  it reads, using headless renders and deterministic tests.
- **Clean.** No proprietary code, no proprietary assets, no binary blobs.

## Project status

Early and actively evolving. The table below is the honest state of each
subsystem; it is updated as work lands.

| Area | Status |
|---|---|
| Archive container and resource extraction | Working |
| Strategic map (2D) | Working prototype, with SAM/EWR coverage overlays |
| 3D model format and bytecode interpreter | Working |
| Texture and palette decoding | Working |
| GUI description language and runtime | Boots from the title card into the main menu; routine layer partly recovered |
| Terrain and 3D world | Container mapped; elevation source not yet located |
| Audio | Not started |
| Flight model and avionics | Not started |
| Scenario and campaign text | Working: `CONFLICT.CG` / `CAMPAIGN.CG` parsed and written, byte-exact round trip |
| Campaign and mission generation | Working: full generator, deterministic from a seed; parity work continuing |
| Multiplayer | Not started |

There is no playable game yet. The current build is a research and
verification tool: it loads the original data, renders meshes, screens and the
strategic map, regenerates campaigns from the shipped scenario, and exports
PNGs and data files so results can be compared.

## Key features

The feature set below is the target shape of the engine. Items marked
*placeholder* are architectural slots that are documented but not implemented.

### Rendering

- **Backend:** *placeholder* — software rasteriser today; the interface is
  designed so a hardware backend can be added without touching scene code.
- **Palette-accurate output:** 6-bit VGA palettes, indexed textures, and the
  original per-polygon shading model are preserved rather than emulated.
- **Headless rendering:** any view can be written to PNG for regression tests.

### Audio

- **Backend:** *placeholder* — mixer and output device abstraction to be
  defined. Format decoding for the original audio containers is planned.
- **Requirements:** the engine must run silently when no audio device is
  available.

### Input

- **Devices:** *placeholder* — keyboard, mouse and joystick, with a single
  declarative binding table shared by all devices.
- **GUI and in-cockpit modes** are expected to share one dispatch layer so
  bindings cannot diverge between screens.

### Physics and simulation

- **Flight model:** *placeholder* — to be reconstructed from documented
  behaviour and observable outputs.
- **AI and mission logic:** *placeholder* — consumes the original mission and
  scenario definitions.
- **Determinism:** the simulation must be reproducible from a seed and an
  initial state, so scenario generation can be validated.

### Campaign generation

The campaign generator is reconstructed as a working subsystem, not just a
specification.

- **Inputs:** the shipped scenario (`CONFLICT.CG`), the previous campaign state
  (`CAMPAIGN.CG`, which carries ownership, damage and the live order of battle)
  and the fixed 543-entry target/base table stored inside `DID1ST.DAT`.
- **Outputs:** a regenerated `CAMPAIGN.CG` and a complete `MISSION.CFG` —
  air-defence missions, strike packages, patrols, convoys, flights, weapons,
  pilots, base timetables and the flak list.
- **Deterministic:** the game's custom 32-bit additive RNG is implemented
  exactly. Its seed is the Unix timestamp of the DOS clock, which the generator
  also writes into the file header, so a captured run can be replayed offline.
- **Verified:** the 77-base table matches the original on side, type, name and
  position; the area order is identical; and replaying a captured timestamp
  reproduces the thousands digit of all 543 target values with zero mismatches.
- **In the shell:** Campaign Level → Accept runs the generator, rewrites both
  files and opens the campaign map, matching the original sequence.

What remains is exact parity on the randomised details (the low component of
each target value, the mobile-to-area link, turn-to-turn attrition and patrol
geometry). The current state is recorded in
[`docs/CAMPAIGN-GENERATOR.md`](docs/CAMPAIGN-GENERATOR.md).

## Architecture

Three layers, with a hard boundary between them:

```
+-----------------------------------------------------------+
|  Engine (C++20)                                           |
|    renderer  |  audio  |  input  |  simulation  |  GUI    |
+-----------------------------------------------------------+
|  Format layer (C ABI)                                     |
|    archives | shapes | textures | terrain | text | GUI    |
+-----------------------------------------------------------+
|  Research tooling (Python)                                |
|    format probes, catalogues, reference implementations   |
+-----------------------------------------------------------+
```

- **Format layer.** Pure, dependency-free readers for the original formats.
  Exposed through a C ABI so the engine, tests and external tools can all use
  it, and so it can be validated independently.
- **Engine.** Owns rendering, input, audio and simulation. No format details
  leak upward.
- **Research tooling.** Python scripts used to probe and verify formats. These
  are development tools, not shipped components, and they are the executable
  form of the specifications in [`docs/FILE_FORMATS.md`](docs/FILE_FORMATS.md).

## Requirements: bring your own assets

**OpenEF2000 ships with zero game assets.** No textures, no models, no
audio, no mission data, and no executable code from the original game is
included in this repository, in releases, or in build artefacts.

To run the engine you must **legally own an original copy of the game** and
provide its data files yourself. A retail CD-ROM, an official re-release, or a
lawful archival copy all qualify, subject to the licence terms that came with
your copy.

### Expected layout

Point the engine at a directory containing the original data. The engine reads
the archives in place and never modifies them. Campaign generation rewrites
only the two campaign files, exactly as the original does.

```
<game-data>/
  DID.DAT            primary resource archive
  DID1ST.DAT         secondary archive (some releases)
  CLUMP_3.TXT        model name list
  CLUMP_SS.TXT       shape-scene name list
  CONFLICT.CG        static scenario
  CAMPAIGN.CG        live campaign state (rewritten by generation)
  MISSION.CFG        generated mission set (rewritten by generation)
  NAVIG.NAV          base navigation
  ...                any other files shipped with your copy
```

If the campaign files are absent, the generator seeds the first campaign from
`CONFLICT.CG` alone.

The default lookup is a `game-data` directory next to the binary, so the
simplest setup is to create it and copy your files in. Otherwise pass the
directory explicitly:

```bash
openef2000 --data "/path/to/game-data"
```

### What you must not do

- Do not commit game data to this repository.
- Do not attach game data to issues, discussions or pull requests.
- Do not publish extracted assets (textures, models, palettes, audio, mission
  files) derived from the game, in any form.

See [`CONTRIBUTING.md`](CONTRIBUTING.md) for the full policy.

## Build and run

### Prerequisites

- A C++20 compiler (GCC 11+, Clang 14+, MSVC 19.30+)
- CMake 3.20 or newer
- SDL2, SDL2_ttf and SDL2_image development packages
- Python 3.10+ (optional, for the research tooling)

Install the dependencies:

```bash
# Debian / Ubuntu
sudo apt install build-essential cmake pkg-config libsdl2-dev libsdl2-ttf-dev libsdl2-image-dev

# Fedora
sudo dnf install gcc-c++ cmake pkgconf-pkg-config SDL2-devel SDL2_ttf-devel SDL2_image-devel

# macOS (Homebrew)
brew install cmake sdl2 sdl2_ttf sdl2_image
```

### Configure and build

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

### Run

```bash
./build/openef2000 --data "/path/to/game-data"
```

Examples:

```bash
# Print archive statistics
./build/openef2000 --data "/path/to/game-data" --stats

# List the GUI screens found in the archive
./build/openef2000 --data "/path/to/game-data" --gui-list

# Render a GUI screen headlessly
./build/openef2000 --data "/path/to/game-data" --gui mainmenu --gui-render menu.png

# Render a 3D model headlessly
./build/openef2000 --data "/path/to/game-data" --mesh efa1 --render efa.png

# Inspect a scenario, then regenerate a campaign into another directory
./build/openef2000 --conflict "/path/to/game-data/CONFLICT.CG"
./build/openef2000 --data "/path/to/game-data" \
  --campaign-generate "/path/to/game-data/CONFLICT.CG" \
  --campaign-out /tmp/campaign

# Replay a captured run: the file header timestamp is the generator seed
./build/openef2000 --data "/path/to/game-data" \
  --campaign-generate "/path/to/game-data/CONFLICT.CG" \
  --campaign-seed 1795127529 --campaign-out /tmp/campaign

# Read and rewrite a mission file, and run the generator checks
./build/openef2000 --mission-roundtrip "/path/to/game-data/MISSION.CFG"
./build/openef2000 --data "/path/to/game-data" --campaign-selftest
```

### Tests

```bash
ctest --test-dir build --output-on-failure
```

CI runs the repository guard, the build, the GUI selftest and the smoke test on
**Linux** and **Windows** (MSYS2/MinGW-w64). Standard GitHub-hosted runners are
free for public repositories, so the Windows job adds no cost.

## Repository layout

```
CMakeLists.txt        build definition
src/                  engine and format layer (C++20)
  did, ra            archive container and block decompression
  shape3d            `.3` container and bytecode interpreter
  texture            textures, palettes, global texture slots
  gui, guirender     `.gui` parser, runtime and software renderer
  ilbm               IFF/ILBM image decoder
  parser, model      mission and campaign text, world model
  conflict           `CONFLICT.CG` / `CAMPAIGN.CG` reader and writer
  mission            `MISSION.CFG` line model, byte-exact reader/writer
  campaign           custom 32-bit additive RNG
  campaigngen        target table, bases, tasking and both writers
  render, meshview   strategic map and mesh export
  modelview          orbit-camera 3D viewer
docs/                 published specifications and engine notes
scripts/              repository guard (see AGENTS.md)
AGENTS.md             working rules: the private/public boundary
CONTRIBUTING.md       contribution and clean-room policy
```

Anything derived from the original game — extracted archives, decoded images,
relocated executables — is excluded by `.gitignore` and must never be
committed.

## Documentation

| Document | Contents |
|---|---|
| [`docs/FILE_FORMATS.md`](docs/FILE_FORMATS.md) | Clean-room specifications for the archive, mesh, texture, terrain, mission and GUI formats |
| [`docs/CAMPAIGN-GENERATOR.md`](docs/CAMPAIGN-GENERATOR.md) | Campaign generator: phases, inputs, the 543-entry base table, RNG and seed, tasking rules, output invariants and current parity status |
| [`docs/TOOL-EVALUATION.md`](docs/TOOL-EVALUATION.md) | Tools considered for RE and verification, with decisions and the kvikdos-ng experiment plan |
| [`CONTRIBUTING.md`](CONTRIBUTING.md) | Clean-room rules, what is accepted, review process |
| [`docs/ENGINE-NOTES.md`](docs/ENGINE-NOTES.md) | Module map, build targets, viewer controls, limitations |
| [`AGENTS.md`](AGENTS.md) | Working rules for automated agents: the private/public boundary and pre-commit checks |
| [`tools/dosbox-x-mcp`](tools/dosbox-x-mcp/README.md) | MCP server for driving a DOSBox-X session |

The specifications are written for interoperability. They describe byte
layouts, field meanings, compression schemes and state machines — facts about
the data, not code from the game.

## Contributing

Contributions are welcome, and the project is explicitly designed to make
clean contributions easy. Please read [`CONTRIBUTING.md`](CONTRIBUTING.md)
before opening a pull request. In short:

- Submit **original code** and **factual format specifications**.
- Do not submit disassembled or decompiled code, executable hex dumps, or game
  assets.
- Sign off your commits to attest that the work is your own.

## Legal and trademark disclaimer

OpenEF2000 is an independent, non-commercial interoperability project.

- **OpenEF2000 is not affiliated with, endorsed by, sponsored by, or
  connected to Digital Image Design, Ocean Software, Atari, Bandai Namco, or
  Eurofighter Jagdflugzeug GmbH**, nor to any of their subsidiaries,
  affiliates or successors.
- **All trademarks, service marks, trade names and logos are the property of
  their respective owners.** References to a game title, company name or
  product name are made solely to identify the software that this engine is
  designed to interoperate with. Such references do not imply any association
  or approval.
- **No original game assets are distributed.** This repository contains no
  textures, 3D models, audio, mission data, executables or other copyrighted
  material from the original game. Users must supply their own lawfully
  obtained copy.
- **No original code is distributed.** The engine is a clean-room
  reimplementation. It was produced by observing and documenting the
  behaviour and file formats of the shipped software, not by copying its
  source. Any decompiled or disassembled material used during research is
  treated as documentation of facts and is not redistributed.
- **Interoperability only.** The purpose of this project is to allow
  lawfully-owned software to run on modern hardware, which is a legitimate
  interoperability goal. The project does not circumvent copy protection, does
  not distribute keys, and does not enable use of the software without a
  licensed copy.

If you are a rights holder and believe something in this repository is
infringing, please open an issue or contact the maintainers so it can be
addressed promptly.

## License

The engine source and the documentation in `docs/` are released under the
**GNU General Public License v3.0 or later** (`GPL-3.0-or-later`). See
[`LICENSE`](LICENSE) for the full text.

Game data is **not** covered by this licence and is **not** distributed. Any
data you supply remains subject to the terms of the licence that came with your
copy of the game.

In short: you may use, study, share and modify this engine, provided derivative
works remain under the same licence and you keep the source available. See the
full text for the authoritative terms.
