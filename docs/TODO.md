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
- [ ] **`TARGET_AREA` sub-elements** — every non-`0xFFFF` entry is an element;
      its kind indexes the target-element table (51 = `HANGAR8`,
      53 = `CONTROL_TOWER`, 55 = `RUNWAY`, …). The capture uses the first N in
      file order.
- [ ] **Strike flight size** — the capture's 2/3/4/5/6-ship strike flights equal
      the number of elements the target area exposes.
- [ ] **Low target-value component (982–1000)** — likely derived from the same
      table; confirm once the reader exists.

## 2. Strike routes

- [ ] Reproduce the 17-waypoint ingress/egress path (currently take-off, refuel,
      ingress, target, landing).

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
