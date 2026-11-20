# Roadmap

Two products from one engine, built in that order.

1. **OpenEF2000** — an engine that runs the 1995 game faithfully on modern
   hardware. It ships no game data: the player supplies their own copy.
2. **A standalone game** — invented aircraft, factions, map, lore, UI and
   audio, with the same *mechanics*. It needs no game data and is free.

The second is only possible because game **mechanics are not copyrightable**.
Copyright covers expression: the models, textures, names, mission text and
scenario files. Reimplementing the rules is what a clean-room engine does. The
second product therefore takes the rules and replaces every piece of
expression.

## Why this order

Parity work is engine work. The campaign generator, tasking, flight model and
AI built for product A are exactly what product B needs; only the data that
drives them changes. Building B first would mean guessing at rules that the
original can still be measured against today.

## Architecture: three seams

The engine currently reads EF2000's formats directly. Both products share the
engine and differ only at three boundaries:

| Seam | Product A | Product B |
|---|---|---|
| **Data source** | `DID.DAT`, `CONFLICT.CG`, `MISSION.CFG`, `NAVIG.NAV` | a content pack in an owned format |
| **Asset source** | original `.3` meshes, ILBM palettes | glTF/OBJ, PNG, our own UI |
| **Audio source** | none yet | Ogg/Opus tracks and effects |

The internal model (`Scenario`, `Dataset`, missions, bases, flights) already
exists. Product A is an *adapter* that reads the original files into it;
product B is a native writer for the same model.

### Constants belong to the content pack

Nothing specific to EF2000 should calcify into the engine:

- the number of target areas, bases and factions,
- the installation classes and their defence-slot counts,
- the weapon-class cycles and the per-side role matrix,
- the campaign map and its starting ownership.

EF2000's values become **content pack 1**; the fictional game brings its own.
This is what keeps the campaign generator reusable instead of hard-coded.

## Milestones

### M0 — parity (active)

Finish the reconstruction of the original data path so it can be measured
against captured runs:

- campaign generation: values and area order are exact; air-defence tasking is
  within 0.6 %; turn-to-turn attrition, the low value component and the site
  ring templates remain;
- mission file: byte-exact round trip;
- GUI: Campaign Level Accept runs the generator end to end.

Exit criteria: a captured run is reproduced from its seed, with the remaining
differences understood and documented.

### M1 — content seams

- introduce a data-source interface; move the original parsers behind it;
- move the campaign constants listed above into a content pack;
- add an asset-source interface (original `.3` alongside glTF);
- add an audio-source interface with a silent default.

Exit criteria: the engine boots from a pack description, and product A is a
pack that happens to read the original files.

### M2 — content pack B

- factions, aircraft and weapons tables;
- an owned campaign map and starting state (generated, not copied);
- the existing campaign generator driven by pack B's tables;
- UI skin, briefing text, sound and music;
- flight model, damage and AI tuned against the same rules as product A.

Exit criteria: a complete campaign can be generated and flown with no game
data present.

### M3 — release

- product B packaged with its assets, free, no game data required;
- product A released as an engine that requires the player's own copy;
- licences: code GPL-3.0-or-later; assets of our own authorship, with a
  per-asset provenance note.

## Guardrails

**Product A** ships no assets, no extracted data and no decompiled material.
The public-tree guard enforces this; `docs/` records facts, never code.

**Product B** must be entirely ours:

- no `EF2000`, `Eurofighter` or DiD names or marks;
- invented aircraft shapes, never traced from the original models or from a
  real type;
- no real air-force insignia or manufacturer logos;
- an owned map and scenario, not DiD's files.

**Generated assets.** A model or track produced entirely by a generative
service may have no human authorship, which makes licensing unreliable.
Either use it as a blockout that a person then reworks, or dedicate it to the
public domain and record that in its provenance note. Never feed the original
game's models or screenshots into a generator and ship the output: that is a
derivative asset.

## Current position

| Layer | State |
|---|---|
| Archive and resource readers | working |
| Scenario and campaign parsing | working, byte-exact round trip |
| Campaign generation | values and area order exact; tasking 99.4 %; attrition open |
| Campaign clock | 8-hour advance implemented and carried across turns |
| Mission file writer | byte-exact round trip |
| GUI shell and strategic map | working prototype |
| 3D models, textures, palettes | working |
| Terrain and 3D world | container mapped, elevation source not located |
| Audio | not started |
| Flight model and avionics | not started |
| Content seams | not started |
