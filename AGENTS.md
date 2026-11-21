# AGENTS.md

Working rules for anyone — human or automated agent — making changes to
this repository.

The repository is **public**. The research that produced it is **private**.
This file defines the boundary between them and the checks that keep it
intact. Read it before you stage a single file.

---

## Contents

- [1. The two workspaces](#1-the-two-workspaces)
- [2. What may exist here](#2-what-may-exist-here)
- [3. Red lines](#3-red-lines)
- [4. Moving knowledge from private to public](#4-moving-knowledge-from-private-to-public)
- [5. Working in this repository](#5-working-in-this-repository)
- [6. Pre-commit check](#6-pre-commit-check)
- [7. If you are unsure](#7-if-you-are-unsure)

---

## 1. The two workspaces

| | Private research workspace | This repository |
|---|---|---|
| Purpose | Discovering and verifying formats | Shipping a clean-room engine |
| Contains | game installs, extracted assets, relocated executables, decompiler output, scratch notes | original code, factual specifications, tests, build files |
| Committed anywhere? | No | Yes |
| Published? | Never | Yes |

The private workspace is **not** inside this repository and must never be
copied into it. If you find a directory in this tree that looks like research
output — extracted archives, decoded images, `.elf`/`.exe` files, `out/`,
`build/` artefacts, `decompile-*.txt` — it does not belong here. Stop and
report it rather than deleting it yourself.

The only thing that crosses the boundary is **knowledge**: a fact about a file
format, re-expressed in your own words and implemented in your own code.

## 2. What may exist here

Allowed:

- Original C++20 source implementing engine and format logic.
- Factual specifications: offsets, types, enumerations, magic numbers,
  compression schemes, state transitions.
- Tests whose fixtures are synthetic or numeric.
- Documentation, build scripts, CI configuration.

Not allowed:

- Game data: archives, textures, palettes, models, audio, mission or campaign
  files.
- Extracted or converted derivatives of game data, including PNGs and OBJs
  produced from it.
- Executables, libraries, disc images, or any binary blob.
- Disassembly, decompiler output, hex dumps, symbol maps of the original
  binary, or code transliterated from them.
- Screenshots containing substantial original artwork.
- Absolute paths or references to the private workspace.

If a file's provenance cannot be stated in one sentence, it does not belong in a
commit.

## 3. Red lines

These are absolute. A change that crosses one is rejected, regardless of how
useful it is or how much work it represents.

1. **Never commit game data or anything derived from it.** Not even a single
   palette, texture or mesh "for testing".
2. **Never commit decompiled or disassembled material**, in whole or in part,
   including a "cleaned up" transcription.
3. **Never commit binaries.** No executables, no libraries, no archives, no
   images, no fonts.
4. **Never weaken this file or `CONTRIBUTING.md`** to make a change fit.
5. **Never push to a remote you have not been asked to push to**, and never
   force-push shared history.

## 4. Moving knowledge from private to public

When research produces a new finding, transfer it like this:

1. **In the private workspace**, note the observation: which file, which
   offset, what changes when a field changes, what the effect is.
2. **Write the fact down** as a specification entry, using the template in
   `docs/FILE_FORMATS.md`. Record the confidence level and the method.
3. **Implement it** from that description, in this repository, in your own
   code. Do not paste from a disassembly listing.
4. **Verify it** against the data at runtime, and record the verification in
   the commit message.

The test is simple: someone with no access to your private workspace should be
able to read the specification and write an equivalent implementation. If the
description only makes sense with a disassembly open next to it, it is not a
specification yet.

### Document before implementing

A discovery does not exist until it is written down in this repository. Write
it into the appropriate document — `docs/FILE_FORMATS.md` for a format,
`docs/CAMPAIGN-GENERATOR.md` for generator behaviour, `docs/ENGINE-NOTES.md`
for engine or shell behaviour, `docs/TODO.md` for what remains open — with the
evidence, the method and a confidence level.

Do this **before** writing the implementation, or in the same change, never
only after it. A long agent session can be interrupted or lose its connection;
code without the note leaves the next session guessing, while a note with an
open implementation can always be picked up again. The note and the code
should agree; if they diverge, the note is the record and the code is fixed to
match it.

If a finding is not yet solid, record it as an open question with what is known
and what would settle it. An honest *todo* is useful; an undocumented
assumption is not.

## 5. Working in this repository

### Build and test

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
ctest --test-dir build --output-on-failure
./build/openef2000 --help
```

The build must be warning-clean. New behaviour needs a test or a headless
render that demonstrates it.

### Game data for local testing

Point `--data` at a directory you own, outside this repository:

```bash
./build/openef2000 --data /path/outside/this/repo --stats
```

Never place that directory inside the tree. `game-data/` is ignored as a
safety net, not as an invitation: a copied directory is still a breach if it
is ever force-added.

### Code

- C++20, no compiler extensions, match the surrounding style.
- Keep the format layer free of engine and SDL dependencies.
- Prefer small commits with imperative, explanatory messages.
- Document each solid discovery in `docs/` before or with its implementation,
  so an interrupted session cannot lose it (see [Document before
  implementing](#document-before-implementing)).
- Sign off every commit (`git commit -s`), as required by `CONTRIBUTING.md`.

## 6. Pre-commit check

Run this before every commit. It fails on the file classes that must never be
published. Also check that any solid discovery in the change has a matching
`docs/` note: a change that implements a finding without recording it is not
ready to commit.

```bash
./scripts/check-public-tree.sh
```

It checks tracked files for forbidden extensions, binary content, oversized
files, and private-workspace paths. A commit that fails this check is wrong
even if the code is correct.

If you are scripting a workflow, make this the last step before `git commit`,
and treat a non-zero exit as a hard stop.

## 7. If you are unsure

Stop and ask. Specifically:

- A file's origin is unclear — ask before staging it.
- You need an asset to reproduce a bug — describe the structure instead of
  attaching it.
- A specification cannot be written without quoting something — say so, and
  leave the field marked *todo* rather than importing material.

A blocked task costs a conversation. A leaked asset or a decompiled snippet in
public history costs the project.
