# Todo

Remaining work, ordered by what unlocks the most. Items are removed as they
land; anything blocked is marked.

## 1. Supershape label table (`.ssd`)

The installation models carry a 20-byte label table after their bytecode:

```
char name[8];      // "TARGPOS"
uint16 pad;
uint16 kind;       // 51 = target element, 0xFFFF = defence point
int32  x;
int16  y;
int16  z;
```

Verified against `trond.ssd`: entries 15–18 are exactly the four Vaernes
target elements from the capture, and entries 23–29 are the defence-ring
offsets that were previously unexplained (`190 682`, `233 -734`, …). This one
table unlocks:

- [x] **Defence-ring offsets** — the `0xFFFF` entries in order per slot, rotated
      by the area heading. Verified against the captured rings (`trond.ssd`,
      `airfld_1.ssd`); `--ssd-labels NAME` prints a model's table.
- [x] **`TARGET_AREA` sub-elements** — model entries with a valid element kind
      (DISPERSED excluded), rotated by the area heading, two to six per mission.
      The element *subset* is shuffled before selection, so it is close to the
      capture rather than identical (the exact shuffle order is still open).
- [x] **Strike flight size** — the strike flight is sized by the number of
      elements the target area exposes (2–6), as in the capture.
- [ ] **Low target-value component (982–1000)** — likely derived from the same
      table; confirm once the reader exists.

## 2. Strike routes

- [x] **Corridor length and shape** — 0–10 intermediate fixes per leg, shared
      by every flight in a package, entry/egress corridors, per-package
      altitude 10/120, ingress at 5. Measured mean 13.10 against the capture's
      13.31 over 29 packages.
- [x] **Conditional refuelling** — only packages based where a tanker patrol
      flies and with a target beyond ~100 map units.
- [ ] **Exact corridor fixes** — the original picks its intermediate fixes from
      a network that is not the area table; the generator uses jittered points
      along the base–target line. Length and altitude distributions match, the
      individual fixes do not.

## 3. Package composition

- [ ] Match the per-package flight mix and sizes exactly (escape/weasel counts,
      2–6-ship flights, SEAD flight split).

## 4. GUI

- [ ] Populate the campaign mission list (`missionsel`) from the loaded sorties.
- [ ] Populate the debrief window (results per mission).
- [ ] Briefing time should come from the sortie, not the campaign clock.

## 5. Campaign manager

- [ ] Victory/defeat target-count branch (the class sets are not decoded yet).
- [ ] Attrition and production between turns. **Blocked on an oracle**: the
      original author describes a heuristic, state-driven system; our captures
      show ~1.5 % of units changing side/type per turn. Needs a fixed-seed
      runtime trace before it can be reproduced rather than guessed.

## 6. Cosmetic / smaller

- [ ] Exact timetable launch times and wave sizes.
- [ ] Escort flight sizes (capture mixes 1–4 ships).
- [ ] `Refuel` flag position of the tanker track per side.
