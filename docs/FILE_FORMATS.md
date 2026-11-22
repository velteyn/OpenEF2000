# File Format Specifications

Clean-room technical specifications for the data formats read by
OpenEF2000.

This document describes **facts about data**: byte offsets, field widths,
endianness, magic numbers, compression schemes and state machines. It contains
no code, listings or excerpts from the original software, and it is written so
that any independent implementation can interoperate with the formats.

---

## Contents

- [Purpose and policy](#purpose-and-policy)
- [Conventions](#conventions)
- [Template for a new format](#template-for-a-new-format)
- [F1 — Resource archive (`DID.DAT`)](#f1--resource-archive-diddat)
- [F2 — `RA` compressed block](#f2--ra-compressed-block)
- [F3 — Shape container (`.3`)](#f3--shape-container-3)
- [F4 — Indexed texture (`.tm`)](#f4--indexed-texture-tm)
- [F5 — Palette (`.col`)](#f5--palette-col)
- [F6 — Terrain and world (`lev/`)](#f6--terrain-and-world-lev)
- [F7 — Mission and campaign text](#f7--mission-and-campaign-text)
- [F8 — GUI description language (`.gui`)](#f8--gui-description-language-gui)
- [F9 — Hi-score table (`HISCORES.DAT`)](#f9--hi-score-table-hiscoresdat)
- [Unverified and open questions](#unverified-and-open-questions)
- [Appendix A — Magic numbers](#appendix-a--magic-numbers)
- [Appendix B — Provenance and confidence](#appendix-b--provenance-and-confidence)

---

## Purpose and policy

These specifications exist so that the formats can be implemented
independently, including by people who have never seen the original software.

Rules for this document:

1. **Facts only.** Layouts, numbers, enumerations and behaviour.
2. **No code excerpts.** No disassembly, no decompiler output, no verbatim
   source.
3. **No assets.** No palette dumps, textures, model data or audio beyond the
   minimum needed to explain a layout (a field description, not a file).
4. **Reproducible claims.** Where a statement is inferred rather than
   confirmed, it is marked as such and the evidence class is given.

Licensing: this document is released under `GPL-3.0-or-later`, the same
licence as the engine. Implementing a format from this description does not
require copying the document, so independent implementations remain free to
choose their own licence for their own code.

## Conventions

| Convention | Meaning |
|---|---|
| `u8`, `u16`, `u32` | unsigned integers of the given width |
| `s16`, `s32` | two's-complement signed integers |
| `LE` / `BE` | little-endian / big-endian |
| `+0x00` | byte offset from the start of the structure |
| `0x…` | hexadecimal |
| *unverified* | inferred, not yet confirmed by an independent method |
| *todo* | not yet decoded |

Bit numbering is MSB-first within a byte unless stated otherwise.

Strings are either **length-prefixed** (one `u8` count followed by that many
bytes) or **NUL-terminated**, as noted per format. Character data is
single-byte; where a file is "text" it uses ASCII plus the DOS box-drawing
range and should be read as Latin-1.

Colour values are discussed as **palette indices** (one `u8`) or as **palette
entries** (three `u8` components). Palette components are **6-bit** (0–63) in
most files and 8-bit in a few; the distinguishing rule is given per format.

## Template for a new format

Copy this skeleton when documenting a new structure.

```markdown
## Fn — <name> (`<extension>`)

**Purpose.** One sentence.

**Container.** Where it lives, how it is found, whether it is compressed.

**Magic / detection.** Bytes at offset 0 and how a reader distinguishes it.

**Layout.**

| Offset | Type | Field | Notes |
|---:|---|---|---|
| +0x00 | u32 | … | … |

**Semantics.** What the fields mean, ranges, valid combinations.

**Variants.** Differences between shipped versions.

**Confidence.** Confirmed / inferred / partial, and what evidence would
raise it.
```

---

## F1 — Resource archive (`DID.DAT`)

**Purpose.** A single container holding every resource: models, textures,
palettes, screens, mission text, audio.

**Container.** A flat file. Two directory variants ship across releases.

| Variant | Detection | Notes |
|---|---|---|
| `plain8` | 32-bit offset at `+0x00`, directory header unencrypted | Used by most DOS builds |
| `xorv2` | first `u32` XOR `0x35812B85` yields a valid offset | Used by later Windows-era builds |

Detection procedure: attempt the `xorv2` interpretation first; accept it only
if the derived directory offset is in range, the sub-counts are sane, and the
entry table ends exactly at the end of file. Otherwise fall back to `plain8`,
whose entry table also ends exactly at EOF. That invariant is a reliable
validator.

### Header

| Offset | Type | Field |
|---:|---|---|
| +0x00 | u32 | directory offset (`xorv2`: XOR `0x35812B85`) |
| +0x04 | u32 | format tag |
| +0x08 | u32 | hash version selector |

`hash version` selects which name-hash variant the directory uses. A value of
`1` selects the primary hash; any other value selects the alternate.

### Directory

At `directory_offset`. In `xorv2` every 16-bit field is XOR `0x2B85`, and every
byte of the prefix/extension strings is XOR `0x85`.

| Offset | Type | Field |
|---:|---|---|
| +0x00 | u16 | entry count |
| +0x02 | u16 | prefix count |
| +0x04 | u16 | extension count |
| +0x06 | u16 | string-space size in bytes |
| +0x08 | — | prefix strings, length-prefixed |
| … | — | extension strings, length-prefixed |
| — | — | entry table, `entry_count × 12` bytes |

Entry record, 12 bytes:

| Offset | Type | Field |
|---:|---|---|
| +0x00 | u32 | name hash |
| +0x04 | u32 | file offset of the payload |
| +0x08 | u32 | stored size (including any `RA` header) |

Entries are sorted by **signed** 32-bit name hash, and lookups are a binary
search using signed comparison. A reader must treat hashes `>= 0x80000000` as
negative when ordering.

### Name canonicalisation

Callers do not pass a hash. They pass a path, which the manager canonicalises
to at most eight bytes:

1. lowercase the path; normalise `/` to `\`; strip a leading `\`;
2. if the path begins with one of the archive's own directory prefixes,
   remove that prefix and prepend the single byte `0x30 + prefix_index`;
3. if the extension matches one of the archive's own extensions, replace it
   with the single byte `0x41 + extension_index`;
4. the result is the canonical key.

Prefix and extension indices are **per archive** and must be read from the
directory. They are not stable across releases.

### Name hash (primary variant)

Applied to the canonical key, limited to 15 bytes:

```
h := 0
order := 0
n := min(length, 15)

for i := 0 to n-1:
    c := key[i]
    if c = '_':
        h := h - i
    else:
        if i > 0:
            order := (order << 1) | (key[i-1] < c ? 1 : 0)
        h := (c + h * 0x24) mod 0xFFFFB

hash := order XOR (h << 8) XOR (n << 28)
```

Properties worth relying on while debugging:

- the top nibble of the hash is exactly the canonical key length;
- the hash is **not** collision-free, so a match is a candidate, not a proof;
- the alternate variant used by `plain8` shares the canonicalisation but mixes
  differently, so implementations need both.

### Storage classes

A payload is one of:

| Class | Detection | Handling |
|---|---|---|
| `raw` | no `RA` magic | used as-is |
| `RA` | `"RA"` at `+0x00` | see [F2](#f2--ra-compressed-block) |
| `JR` | *todo* | not yet decoded |

## F2 — `RA` compressed block

**Purpose.** The container compression used by most archive payloads.

**Layout.**

| Offset | Type | Field | Notes |
|---:|---|---|---|
| +0x00 | u8[2] | magic `"RA"` | |
| +0x02 | u8 | format level | observed `0` |
| +0x03 | u8 | version | observed `2` |
| +0x04 | u32 | uncompressed size | LE |
| +0x08 | u16 | flags | observed `0x7D00` |
| +0x0A | u16 | checksum | 16-bit checksum over the output |
| +0x0C | — | compressed stream | |

Blocks are contiguous. When scanning rather than following offsets, look for
the byte sequence `52 41 00 02` and reject candidates that would overlap a
valid block.

**Stream structure.** The decoder keeps two cursors: an MSB-first bit buffer
refilled a byte at a time, and a raw byte cursor used for word reads, literal
runs and one special length byte. A raw read takes the byte *after* the
currently buffered byte, even when the bit buffer still holds unread bits;
implementations must reproduce this interleaving.

The stream begins with a single `1` bit, then repeats a group consisting of a
pair count, a match tree, a literal tree and a payload of literal runs and
back-references. Trees are serialised binary tries. Match lengths and distances
come from their own trees. The decoder in `src/ra.cpp` is the executable form
of this section and is the normative description of the interleaving.

**Checksum.** A 16-bit additive checksum computed over the decompressed output.

## F3 — Shape container (`.3`)

**Purpose.** A compiled display-list mesh: vertices, primitives and a small
bytecode program that emits them.

**Container.** A stream of **big-endian 16-bit words**. Stored inside the
archive, usually `RA`-compressed.

### Header

| Word | Type | Field | Notes |
|---:|---|---|---|
| 0…n-1 | s16 | LOD distance pairs | high-to-low, terminated by `0x7FFF` |
| 8 | s16 | rendering priority | |
| 16 | u16 | class id | see table |
| 17…20 | u16 | per-LOD offsets | in bytes, relative to the word holding the offset |
| 28…31 | char[8] | optional texture name | word-wise byte-swapped |

Class identifiers:

| Class | Meaning |
|---|---|
| `0x0000` | entity (aircraft, building, prop) |
| `0x0004` | sub-entity (stores: weapons, pylons, tanks) |
| `0x0020` | terrain |
| `0x0083` / `0x0087` | ADF entity / sub-entity |
| `0x00A3` | ADF terrain |

For class `0x0000`, each LOD offset points at a word holding the vertex limit;
bytecode begins one word later, and the LOD ends where the next LOD begins. The
final word of a program is `0xFFFF`.

### Execution model

Bytecode draws from a fixed array of object-space vertices, each an `s16`
triple. A per-routine 4×3 transform on a call stack is applied at emit time.
Conditional submeshes push a call frame only when their condition is taken.

Branch targets are relative to the **word that holds the offset**, not to the
end of the instruction:

```
target_word = offset_word_index + (offset_bytes / 2)
```

### Verified opcodes

Operands are words unless stated otherwise.

| Op | Meaning |
|---|---|
| `0000` | return, or end of program at the outermost level |
| `0001` | flat line: `<colour> <p0> <p1>` |
| `0002` | flat triangle: `<colour> <p0> <p1> <p2>` |
| `0003` | flat quad: `<colour> <p0..p3>` |
| `0004` | flat polygon: `<colour> <n> <p0..pn-1>` |
| `0007` / `0008` | jump / call `<byte offset>` |
| `0015` | jump if triangle visible: `<p0> <p1> <p2> <offset>` |
| `001A` / `001B` | transparent triangle / quad |
| `001F`, `0077` | make subsequent geometry face the viewer |
| `0020` / `0021` / `0022` | jump if parameter `<` / `==` / `>` `<value> <offset>` |
| `0024` / `0025` | draw submesh if parameter `==` / `>` `<value> <offset>` |
| `0026` / `0027` | jump if distance `<=` / `>=` `<threshold> <offset>` |
| `002D` | displaced submesh: `<vertex> <offset>` |
| `002E`, `0047` | fetch UVs: `<texture> <count> <u v>…` |
| `002F`, `0088`, `008D`, `008E` | textured polygon: `<n> <p0..pn-1>` |
| `0030` | define unit length `<value>` |
| `0031` | push polygon colours: `<n> <c0..cn-1>` |
| `0032` | Gouraud polygon: `<n> <p0..pn-1>` |
| `0038`, `007D` | flat disc (quad operands) |
| `003F` / `0040` / `0042` | time-of-day submesh `<mask> <offset>` |
| `0043` / `0045` | time-of-day jump |
| `0048` | select active parameter `<index>` |
| `0049` | transformed submesh: `<roll> <pitch> <yaw> <6 words> <vertex> <offset>` |
| `0063`…`0070` | delta-vertex compression by fixed axis groups |
| `0083` | texture portion: `<u> <v> <w> <h>` |
| `0084` | clear texture portion |

Only the textured opcodes consume UVs. Flat and Gouraud primitives ignore any
UVs left by an earlier `002E`/`0047`; conflating the two is the classic
implementation error and produces apparently scrambled textures.

### Textured primitives

A texture operand is an index into the shape's own texture table: `0` selects
the shape's hard-coded texture name, and any other value selects an entry from
the global texture table (see [F4](#f4--indexed-texture-tm)). The index is
**0-based**.

UVs are texel coordinates, normally within `[0,255] × [0,191]`, but values
outside that range occur and must not be clamped in the data model.

### Shading

Flat primitives carry a palette index per vertex. That index *is* the lighting:
the mesh stores the shade directly rather than relying on a runtime light. A
renderer that applies an additional light term will not match the original
appearance.

## F4 — Indexed texture (`.tm`)

| Property | Value |
|---|---|
| Size | 256 × 192 |
| Encoding | one `u8` palette index per pixel, row-major |
| Payload | exactly 49,152 bytes after decompression |
| Transparency | index `0` is the key for masked primitives |

A global texture table maps small integers to texture names. It is a plain text
file in the archive with a `[dir]` section and a `[globaltextures]` section of
`index=name` rows. The same file also carries a `[bands]` list that partitions
the palette into brightness ramps, and a `[coltab]` reference.

## F5 — Palette (`.col`)

**Layout.** 768 bytes: 256 entries of three bytes, ordered red, green, blue.

**Component width.** Determine by inspection: if the maximum byte in the file
is `<= 63`, components are 6-bit and must be scaled by `255 / 63`; otherwise
they are 8-bit.

**Related table.** A separate 4,096-byte binary file holds sixteen 256-entry
index remap tables. Table 0 is the identity. Higher tables rotate an index
along the palette's circular ramp, which is used for distance fade rather than
for lighting.

## F6 — Terrain and world (`lev/`)

**Purpose.** The terrain and strategic map for one theatre.

**Status.** The container is mapped; the elevation source is **not** yet
located. The negative results below are recorded deliberately so the next
reader does not repeat them.

### World header

`<map>.wld` is a small ASCII file and the authoritative description of the
grid:

```
;World Info file for <path>\<map>.wld
;Created With World Editor 95 V1.0

SIZE 400 400
TAGSPACE 1024
STAMPSPACE 256
```

`SIZE` is the cell grid. `TAGSPACE` matches the tag-table count in `.map`
(1,015 used of 1,024), and `STAMPSPACE 256` matches the 256-byte stamp payload
described below.

### `<map>.map` — cell grid and tag table

Confirmed for the Norway theatre (582,154 bytes):

| Offset | Type | Field |
|---:|---|---|
| +0x00 | u32 | grid width (400) |
| +0x04 | u32 | grid height (400) |
| +0x08 | u32 | tag count (1,015) |
| +0x0C | u16 × w·h | cell grid: index into the tag table, LE |
| +0x0C + 2·w·h | — | tag records |

For Norway the grid is 400 × 400 `u16` = 320,000 bytes, values `0..1015`,
about 49 % of cells zero. The tag table begins at `0x4E20C` and holds 1,016
records of **258 bytes** (256-byte payload plus two bytes, matching
`STAMPSPACE 256`); 14 bytes remain after the last record.

Each 258-byte record read as a 16 × 16 byte field is locally coherent
(mean neighbour delta ≈ 12 of 255), so the payload is either a height patch or
a tile-sized texture. Which of the two is **unverified**.

### `<map>.4e2` — theatre bitmap

For the theatre maps, 160,000 bytes = 400 × 400 cells of one byte, but only the
values `0` and `1` occur. Reshaped as an image it renders coastlines, national
borders and settlement dots, so this is **map artwork, not elevation**.

Note that not every `.4e2` is this shape: `mt1_c1.4e2`, `mt2_c1.4e2` and
`tfc9.4e2` decompress to only ~2 KB with 114–121 distinct values, i.e. they are
a different kind of file. Do not assume a single layout for the extension.

### `<map>.lev` — rasterised map artwork

40,000 bytes. Tested as a raster under every factor pair of 40,000
(200 × 200, 100 × 400, 50 × 800, …): no layout is spatially smooth
(mean neighbour delta 18–35 of 255, versus ~48 for a random permutation), and
adjacent-cell autocorrelation is 0.59. A heightfield would show low deltas and
autocorrelation above 0.9, so `.lev` is a rasterised picture, not terrain
height.

### What is ruled out

- **Terrain is not a `.3` shape.** All 8,261 archive entries were decoded and
  parsed as shapes: the classes present are `0x0000` (entities) and `0x0004`
  (sub-entities). The two hits at `0x0020` are 49,152-byte textures that parse
  as shapes by accident. There is no terrain mesh in the archive.
- **`.lev` is not elevation** (see above).
- **`.4e2` is not elevation** (see above).

### Remaining candidate

The 16 × 16 payload inside the `.map` tag records is the only located
structure that could carry height. Settling it needs a comparison against a
known elevation at a known cell — from running the original software, not from
the files alone.

### Map naming

The active map is selected by a text command in a level script
(`set_map_name <name>`). The name does not always match the `.wld` basename:
the Norway theatre ships as `norway.wld`/`norway.map`, while the campaign map
selects `swnorway`, which has its own `.lev`, `.4e2`, `.stp` and texture atlas
but no `.map`.

## F7 — Mission and campaign text

**Purpose.** Scenario, mission and pilot data, authored in a small scripting
language.

**Encoding.** ASCII/Latin-1, line-oriented. Comments begin with `;`. The first
lines are a banner and may be ignored.

**Structure.**

- `VARIABLE <name>` / `STRING <name>` declare persistent state.
- `STRIP <name>` begins a linear script.
- `LABEL<n>:` defines a jump target.
- `IF <var> <op> <value> then GOTO <label>` and `GOTO <label>` control flow.
- `MISS <command…>`, `STAT <image> <ticks> <fade-in> <fade-out> <text-mode>`,
  `ANIM <name> <frames> <rate> <mode> …`, `MUSIC <device> <action> <track>`
  perform side effects.
- `TEXTBOX … ENDTEXT` embeds displayed text.
- Mission records include `WP_ROUTE`, `WAYPOINT <x> <y> <z> <a> <b> <role>`,
  `FLIGHT <count> <type> <role> <status>`, and
  `TARGET_AREA <id> "<description>"`.
- `TIMESLOTS <n>` is followed by `TIMESLOT <day> <hour> <minute> <action>
  <role> <mission> <wave>` rows. Every flight of a package has its own
  take-off and landing slot; the briefing time is the earliest `TAKE_OFF` row
  carrying the mission's id.

**Units.** Coordinates are grid cells for waypoints. Vertical values and
timing units differ between releases; see Appendix B.

## F9 — Base navigation (`NAVIG.NAV`)

**Purpose.** Per-airfield departure/arrival corridors and patrol routes. The
campaign generator draws its strike corridors from this file; generation never
modifies it.

**Encoding.** Line-oriented ASCII/Latin-1 with CRLF endings. The first line is
a comment; the rest is a sequence of records.

```
NUM_BASE_NAVIGATIONS <count>
BASE_NAVIGATION <type> <id> <a> <b>
3D_POSITION <x> <y> <z> <a> <b> <c>
NAVIG_CORRIDOR <id> <a> <b> <c>
NAVIG_ROUTE <count>
WAYPOINT <index> <x> <y> <z> <a> <b> <action> <c> <d> <e>
```

- `BASE_NAVIGATION` opens one entry. The position that follows is the
  airfield's map coordinate; the same position appears in many entries because
  each entry carries a different corridor set. The entry id is the **target map
  cell** it serves:
  `id = (x / 40) + 10 * (9 - (y - 1) / 40)`, using integer division. A target
  at (121,154) is cell 63; at (103,159) cell 62; at (158,234) cell 43. This
  holds for all 19 distinct (base, target) pairs in the reference capture.
- `NAVIG_CORRIDOR` groups routes that leave the airfield in the same
  direction.
- `NAVIG_ROUTE <count>` declares the number of waypoints that follow.
- `WAYPOINT` fields are the same shape as mission waypoints; `z` is 12000 in
  the shipped file and renders as altitude 120 in a mission.

The reference file has **1,118** entries, **18,424** distinct waypoint
coordinates and 543 target areas' worth of corridor variants.

**Use by the mission generator.** A strike package's intermediate fixes are
one corridor route from the attacking airfield, forward for the outbound leg
and reversed for the egress leg. On the outbound leg the route's final point
becomes the `INGRESS` fix; the `STRIKE_TARGET` is the target coordinate.
Example from the reference capture (base Rygge, 118/42):

| Package | Outbound | Egress |
|---|---|---|
| 694 → Vaernes | entry 63, corridor 4, route 7 | entry 63, corridor 5, route 6 reversed |
| 695 → Vaernes | entry 63, corridor 5, route 6 | entry 63, corridor 5, route 6 reversed |
| 697 → Orland | entry 62, corridor 0, route 3 | entry 62, corridor 3, route 5 reversed |
| 721 → Blaho (SEAD) | entry 62, corridor 0, route 2 | entry 62, corridor 6, route 9 reversed |

The rule that chooses the entry, corridor and route is not decoded yet. The
generator currently synthesises jittered corridors and snaps their points to
the nearest navigation waypoint, so the fixes are valid network coordinates
but the corridor selection is approximate.

## F8 — GUI description language (`.gui`)

**Purpose.** Declarative definition of every menu and in-flight panel.

**Encoding.** ASCII/Latin-1, line-oriented. `;` starts a comment. A trailing
`\` continues a line. Both `key value` and `key=value` forms appear, sometimes
in the same block.

**Blocks.** `window <name>` (or the older `menu=<name>`) opens a window block;
`icon <name>` opens a widget inside it. Widget coordinates are relative to the
window's client area, i.e. below the title bar. A widget that omits position or
size inherits it from the previous widget in the same window.

**Window properties.**

| Key | Meaning |
|---|---|
| `xpos`, `ypos`, `xsize`, `ysize` | geometry, screen coordinates |
| `workxsize`, `workysize` | client extent (may exceed the window, for scrolling) |
| `title <lang> <text>` | localised caption |
| `box`, `bkd` | border and fill palette index |
| `open` | visible when the screen loads |
| `moveable`, `close`, `resize` | window decoration behaviour |
| `is3d` | hosts a live 3D view |
| `redraw`, `update` | callbacks for that 3D view |
| `onopen`, `onclose` | lifecycle callbacks |
| `nojump`, `back`, `fullsize`, `xscroll`, `yscroll`, `infrontof` | layout/behaviour |

**Widget properties.**

| Key | Meaning |
|---|---|
| geometry | as above |
| `box`, `boxs` | border colour, normal and selected |
| `bkd`, `bkds` | fill colour, normal and selected |
| `line` | rule colour |
| `text <lang> <text>` | localised label; omit the language for English |
| `textpos left\|centre\|right` | alignment |
| `textlen`, `textlenoff` | fixed field width and its terminator |
| `txtclr`, `txtclrs` | text colour, normal and selected |
| `esg <n>` | exclusive selection group |
| `select <event>` / `unselect <event>` | event that sets/clears selection |
| `call <event>` | opens a block of verbs for that event |
| `key <k>`, `skey <k>` | keyboard activator, with and without Shift |
| `toggle click` | activation flips the selection instead of setting it |
| `editable <n>` | text field accepting `n` characters; `editableoff` marks the initial (non-editing) state, it does not make the field read-only |
| `disable`, `enable` | interaction state |
| `routine <name>`, `screen <name>`, `task <name>`, `openwindow <name>`, `closewindow <name>`, `submenu <name>`, `openmenu <name>`, `return <n>` | verbs |

Window properties additionally include `xscroll` / `yscroll` (client area may
be scrolled), `infrontof <window>` (stacking hint), `jumptofront` (raise on
open), `closedown <routine>` and `mainloop <routine>` (routine references).

**Colour index 0 means hollow**, not black. Files annotate this explicitly
(`bkd 0  ; hollow`).

**Selection.** `esg` groups are exclusive: selecting one member clears the
others. Selection state, hover state and the events that drive them are part of
the runtime, not the file.

**Composition.** `include <name>` merges another screen's windows, later
declarations replacing earlier ones of the same name. `screen`/`task` switch
screens at runtime. `submenu` and `openmenu` name a menu defined in the same
file (a `menu=NAME` block becomes a window), so they open that window; if the
name is not a window of the screen they are treated as a screen switch.

### Routines: the part the files cannot define

The DSL describes structure; the behaviour behind a `routine <name>` is C code
in the original. Those names are the boundary of what can be reconstructed from
the files alone, so an implementation should treat them as a registry:

- register handlers for names whose behaviour is established;
- record the rest as unimplemented rather than guessing;
- expose the per-screen list of referenced names, so the gap is a visible
  worklist rather than silent no-ops.

A screen's routine references come from `routine` verbs plus the `onopen`,
`onclose`, `redraw`, `update`, `closedown` and `mainloop` window properties.

`submenu`/`openmenu`, `screen`, `task`, `openwindow`, `closewindow` and the
keyboard bindings need no such registry: the file states their effect.

## F9 — Hi-score table (`HISCORES.DAT`)

**Purpose.** The Quick Combat hi-score list. It is a loose file next to the
archive, not an archive entry, and the game rewrites it, so its contents are
per installation and per player.

**Layout.** 4-byte header followed by ten fixed 25-byte records:

| Offset | Type | Field |
|---:|---|---|
| +0x00 | u32 | header (observed `0x00004268`) |
| +0x04 | — | record 0, then nine more at 25-byte stride |

Record:

| Offset | Type | Field |
|---:|---|---|
| +0 | char[16] | name, NUL-padded |
| +16 | u8[4] | padding, observed zero |
| +20 | u8 | difficulty level: 1 Rookie, 2 Pro, 3 Topgun |
| +21 | u32 | score, little-endian |

The level numbering follows the order of the Difficulty panel in `arcade.gui`;
it is consistent across the three shipped tables, but the mapping from numbers
to labels is inferred from that ordering rather than stated anywhere.

Record names across the shipped installs are developer and play-tester names,
which confirms this is a developer-populated default table.

## F10 — Quick Combat and Simulator tables (`cfg\*.txt`, `.brf`)

**Purpose.** Loose text resources (also present as archive entries under
`cfg\`) that list the Quick Combat levels and the Simulator mission types.
The game normally extracts them; a shipped `CFG` directory may be empty.

**Layout.** A leading count line, blank lines, then one record per line:

```
12

ARCADE1$ARCTOP01.BRF$ARCROO01.CFG

ARCADE2$ARCTOP02.BRF$ARCROO02.CFG
```

Fields are `$`-separated:

| Field | Meaning |
|---:|---|
| 0 | label shown in the browser (`ARCADE1`) |
| 1 | briefing file, e.g. `ARCTOP01.BRF` |
| 2 | mission config, e.g. `ARCROO01.CFG` |
| 3 | optional extra selector (present in `free.txt`, e.g. `$2`) |

Tables: `arcroo.txt` (Rookie), `arcpro.txt` (Pro), `arctop.txt` (Topgun). Only
the mission config differs by difficulty; the briefing is the same
`ARCTOPnn.BRF` in all three. The Simulator uses the same shape in `free.txt`,
`basic.txt`, `weapon.txt`, `air2air.txt`, `air2grnd.txt`, `advanced.txt`,
`king.txt`, `testa2g.txt` and `test.txt`, keyed by map name (Helsinki,
Murmansk, …).

**Briefing files (`.brf`).** Plain text, one phrase per line, usually
double-spaced with CR/LF; the Summary panel prints the non-empty lines. The
campaign briefing is the loose `CAMPBRF.BRF`.

The Simulator's `missions` window declares twenty `esg 1` template rows
(`first`, `2`..`20`) with a `workysize` of 400; the table supplies the labels
and unused rows stay hidden. Its `miss_info` window has no text widgets: the
briefing is written into the window's text lines when the window opens.

**Verification.** Level 1's `cfg\arctop01.brf` is the 80-byte entry whose text
is "Emergency intercept. / Destroy incoming hostile bombers / and escort
aircraft.", exactly the Summary shown by the shipped Quick Combat screen; the
other eleven lines resolve to entries with matching text.

## F11 — Aircraft type ids

The campaign generator refers to aircraft by a numeric id, not by name. The id
table is a 4-byte pointer array at file offset `0x18baf0` in the reconstructed
v2.0 image (image base `0x10000`), pointing at the name strings:

| Id | Type | Id | Type | Id | Type |
|---:|---|---:|---|---:|---|
| 0 | EFA | 13 | MIG27 | 26 | ANTONOV |
| 1 | F14 | 14 | MIG29 | 27 | IL76 |
| 2 | F15 | 15 | MIG31 | 28 | COBRA |
| 3 | F16 | 16 | F117A | 29 | CHINOOK |
| 4 | F18 | 17 | F111R | 30 | MI26 |
| 5 | F22 | 18 | TORNADO | 31 | MI24 |
| 6 | JAS39 | 19 | TU16 | 32 | R767 |
| 7 | HARRIER_GR6 | 20 | TU26 | 33 | IL78M |
| 8 | SEA_HARRIER | 21 | SU34 | 34 | J767 |
| 9 | SU27 | 22 | A10 | 35 | E767 |
| 10 | SU33 | 23 | SU25 | 36 | A50 |
| 11 | SU35 | 24 | HERCULES | 37 | (empty) |
| 12 | MIG21 | 25 | C17 | 38 | CRUISE_MISSILE |

`FUN_00109d44` builds a small array of these ids for a mission, selected by
two bytes: a class (0..5) and a mission/category code. Examples from the
binary: `{6,3}`, `{4}`, `{13,12}`, `{32}`, `{35}`, `{34}`, `{25,24,29}`,
`{3,5,9,17}`, `{6,4,7,8}`, `{0,1,17,17}`, `{12,11,17,17}`. The array is
`'%'`-terminated and is what `0xcc8bc` sums capacity over when it computes
the package quota.

The id table also matches the `MOB`/`FLIGHT` type names one-for-one, so a
consumer can translate both ways without guessing.

For the mission code `3` used by the quota call (`0xcc8bc` is invoked with
`DL=3`, `AX=2`), `0x109d44` selects the list by the base's side byte
(`base+0xb`):

| Side class | List |
|---:|---|
| 0 NATO | `1,5,4,8,2,3,4,0` = F14, F22, F18, SEA_HARRIER, F15, F16, F18, EFA |
| 1 RUSSIAN | `11,10,9,20,21,13,14,12` = SU35, SU33, SU27, TU26, SU34, MIG27, MIG29, MIG21 |

Both arrays fill all eight slots (no `'%'`), so all eight types count. SU25 is
**not** in the Russian list, which is why Orland's twelve SU25s never enter the
airfield-denial quota.

## F12 — Ground type ids and defence sites

Ground mobiles use the same numeric-id convention as aircraft. The id table is
a 4-byte pointer array at `0x18c244` (27 entries):

| Id | Type | Id | Type | Id | Type |
|---:|---|---:|---|---:|---|
| 0 | BEDFORD_1 | 9 | M198 | 18 | RM70 |
| 1 | BEDFORD_2 | 10 | MLRS | 19 | ROLAND |
| 2 | BMP_1 | 11 | MOBRIDGE | 20 | MARCONI_S373 |
| 3 | CHALENGER | 12 | M110 | 21 | MARCONI_S723 |
| 4 | D_30 | 13 | M1973 | 22 | SA_6 |
| 5 | JUG_1 | 14 | PATRIOT_1 | 23 | SA_11 |
| 6 | JUG_2 | 15 | PATRIOT_2 | 24 | T80 |
| 7 | JUG_3 | 16 | PONTOON | 25 | WARRIOR |
| 8 | M_163_VULCAN | 17 | RAPIER | 26 | ZSU_23_4_SHILKA |

The campaign generator creates ground units for a fresh campaign (`0xbd96c`).
Each defence site of the target area's supershape gets one group; `0xda678`
counts site records whose kind byte is `0xFF` (or `0xFE` for most classes) and
`0xda6c0` returns the units per group from the class byte. Per group one
custom-RNG draw selects the type, per unit one draw selects SAM or truck:

```
d = AX % 10
west (0,2): d < 5 PATRIOT_1 (14), 5..7 RAPIER (17), 8..9 VULCAN (8)
east (1,3,4): d < 5 SA_11 (23), 5..7 SA_6 (22), 8..9 ZSU (26)
otherwise BEDFORD_1 (0)
```

A site record is 21 bytes and the site's mission offsets are the 16-bit values
at `+7` (x), `+9` (y) and `+0xb` (z); `+0` is the kind and `+5` the health.

## Unverified and open questions

| Item | Status |
|---|---|
| `JR` compression | *todo* |
| `.map` placement records | partial |
| `<map>.lev` as elevation vs terrain class | *unverified* |
| Coverage radii per ground unit type | not located in layout files |
| Hardpoint offsets for store attachment | not located |
| Alternate hash variant mixing function | behaviour confirmed, formula incomplete |
| Bound/self-extracting executable layout | needs an independent dump |
| Ground-roster RNG stream position | one phase between `0xbeabc` and `0xbd96c` still unaccounted for |
| NATO package-quota divisor | special site class `'1'` branch not fully measured |

## Appendix A — Magic numbers

| Value | Meaning |
|---|---|
| `0x35812B85` | archive header XOR key (`xorv2`) |
| `0x2B85` | directory field XOR key (`xorv2`) |
| `0x85` | directory string byte XOR key (`xorv2`) |
| `52 41 00 02` | `RA` block signature |
| `0x7FFF` | shape LOD table terminator |
| `0xFFFF` | shape program terminator |
| `"FORM"`, `"ILBM"`, `"PBM "` | IFF image containers |
| `0x24`, `0xFFFFB` | name-hash multiplier and modulus |

## Appendix B — Provenance and confidence

Confidence levels used above:

- **Confirmed** — two independent methods agree (e.g. a reader and a renderer
  produce identical counts for many files).
- **Observed** — a value or behaviour was seen consistently but not proven.
- **Inferred** — plausible from surrounding structure; may change.

Where releases differ, note the release in the section rather than averaging.
Coordinate scaling and timing units are known to vary between releases; each
finding should state which build it came from.
