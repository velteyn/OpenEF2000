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

**Units.** Coordinates are grid cells for waypoints. Vertical values and
timing units differ between releases; see Appendix B.

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
