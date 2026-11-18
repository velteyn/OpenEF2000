# Contributing to OpenEF2000

Thank you for your interest. OpenEF2000 is a clean-room reimplementation,
and that status is the project's most valuable asset: it is what allows the
work to exist in public at all. Every contribution must preserve it.

This document explains what we accept, what we cannot accept, and how to do
format research in a way that stays on the right side of the line.

---

## Contents

- [The one rule](#the-one-rule)
- [What you may contribute](#what-you-may-contribute)
- [What you must not contribute](#what-you-must-not-contribute)
- [Doing format research safely](#doing-format-research-safely)
- [Code contributions](#code-contributions)
- [Documentation contributions](#documentation-contributions)
- [Commit sign-off and provenance](#commit-sign-off-and-provenance)
- [Pull request checklist](#pull-request-checklist)
- [Review process](#review-process)
- [Reporting bugs](#reporting-bugs)
- [Enforcement and removal](#enforcement-and-removal)

---

## The one rule

**Contribute only original work, and only facts about the data.**

If a contribution contains material derived from the original game's
executable or assets, it will be closed without merge, and — if it has already
been merged — removed from history. This applies to everyone, including
maintainers.

## What you may contribute

| Kind | Examples |
|---|---|
| Original engine code | renderers, input handling, simulation, GUI runtime, data structures |
| Original tooling | parsers, validators, test harnesses, exporters you wrote yourself |
| Factual format specifications | byte offsets, field widths, enumerations, state machines |
| Tests and fixtures | synthetic data you created, or expected values expressed as numbers |
| Documentation | tutorials, architecture notes, build instructions, translations |
| Bug reports | reproduction steps, observed vs expected behaviour, version info |

## What you must not contribute

| Forbidden | Why |
|---|---|
| Disassembled or decompiled code | Not your work; not licensed for redistribution |
| Hex dumps of executables | Same, and they invite reconstruction of protected code |
| Game assets: textures, sprites, palettes, fonts, audio, video | Copyrighted; also breaks the "no assets in the repository" guarantee |
| 3D models, meshes or their vertex/index data | Copyrighted asset content |
| Mission, campaign or scenario data files | Copyrighted content |
| Executables, libraries, discs or installer images, in any form | Copyrighted; also a distribution problem |
| Links or instructions for obtaining the game | Outside this project's scope |
| Screenshots containing substantial game art | Only minimal illustrative use is acceptable; prefer no images |
| Pasted output of a decompiler or debugger showing original instructions | Same as disassembly |

If you are unsure whether something is acceptable, **ask first** in an issue
before opening a pull request. A rejected question costs nothing; a rejected
pull request after hours of work costs a lot.

## Doing format research safely

Reverse engineering file formats for interoperability is legitimate, and this
project depends on it. The work is safe when it stays at the level of
**observable behaviour and structure**.

**Acceptable methods**

- Opening a data file you own in a hex editor and describing the layout.
- Comparing two files that differ in one known way to isolate a field.
- Feeding a structure to your own parser and checking the result renders or
  parses sensibly.
- Reading publicly available documentation, manuals, SDKs and format
  descriptions.
- Observing program behaviour (what the software does), and writing that down
  as a behavioural specification.

**Methods that must not produce committed artefacts**

- Reading decompiler output to understand an algorithm, then committing the
  decompiler output — or a close transliteration of it.
- Copying constants, tables or control flow wholesale from a disassembly into
  source.
- Including original binaries, or extracts of them, anywhere in the tree.

**How to express what you learned.** Describe the *result*: a field, a table,
an invariant, a state machine, a formula. Write the implementation yourself
from that description. If you worked from a disassembly, say so in the
provenance section of the specification, and keep the description at the level
of facts.

## Code contributions

### Style

- C++20, no compiler extensions.
- Match the surrounding code: naming, file layout, comment density.
- Keep the format layer free of engine dependencies; it is exposed through a
  C ABI and must stay usable by tests and external tools.
- Prefer small, focused changes over large mixed pull requests.

### Requirements

- The build must be warning-clean with the project's warning flags.
- New behaviour needs either a test or a headless render that demonstrates it.
- Do not add third-party dependencies without discussing it in an issue first.
- Do not add binary files. Ever.

### Building and testing

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
ctest --test-dir build --output-on-failure
```

### Commit messages

Write in the imperative mood, explain **why** rather than restating the diff,
and keep the first line under 72 characters.

```
Fix 0-based global texture index

The shape header seeds slot 0 with the hard-coded texture, so a texture
operand of N selects global slot N, not N+1. The previous mapping shifted
every aircraft skin by one atlas.
```

## Documentation contributions

Specifications follow [`docs/FILE_FORMATS.md`](docs/FILE_FORMATS.md). When you
add a format:

- start from the template in that document;
- state the **confidence** of each claim;
- name the build/release the observation came from;
- include a *provenance* note describing the method;
- keep examples numeric or structural, never asset content.

## Commit sign-off and provenance

Every commit must carry a sign-off line:

```
Signed-off-by: Your Name <you@example.com>
```

Use `git commit -s`. The sign-off is a statement that:

1. you wrote the contribution, or it is based on work you have the right to
   submit;
2. you have not included material from the original game's executable or
   assets;
3. you agree the contribution is licensed under the project licence,
   `GPL-3.0-or-later`.

If part of your contribution is based on research into the shipped software,
add a short `Provenance:` paragraph to the commit body or the accompanying
documentation describing the method used.

## Pull request checklist

- [ ] The change is original work.
- [ ] No disassembly, decompiled output, hex dumps or game assets are included.
- [ ] No binary files are added.
- [ ] The build is warning-clean.
- [ ] Tests or a headless render demonstrate the change.
- [ ] Format claims state their confidence and provenance.
- [ ] Commits are signed off (`git commit -s`).
- [ ] The pull request describes *why*, not just *what*.

## Review process

1. A maintainer triages within a reasonable time and confirms the contribution
   fits the project's scope.
2. Review focuses on correctness, clarity and the clean-room constraints.
3. Provenance questions are asked before technical review continues. This is
   not distrust; it protects you as much as the project.
4. Merges are squashed or rebased to keep history readable.

Reviewers may ask you to rewrite a change purely for provenance reasons, even
if the code is correct. Please treat that as a normal review outcome.

## Reporting bugs

Useful reports include:

- the build you are running (commit or version);
- the release of the original data you are using (see the engine output);
- exact command line;
- expected vs actual behaviour;
- for rendering issues, a screenshot of **your** output is fine, but do not
  attach original game art as a reference.

Please do not attach game data to issues. Describe the file in terms of
structure and size instead.

## Enforcement and removal

- A pull request containing forbidden material is closed without merge.
- If forbidden material reaches the main branch, it is removed and, where
  necessary, history is rewritten.
- Repeat violations result in being blocked from contributing.

These rules exist so the project can keep existing. Thank you for helping keep
it clean.
