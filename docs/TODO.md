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

- [x] **CAP per-airfield allocation** — quota from the highest airfield down,
      eight while four remain for each lower airfield. Exact on all four sides.
- [x] **Support basing** — tankers/AWACS/JSTARS launch from the airfields that
      hold those airframe types. All eight support bases match.
- [x] **Target lists** — per side, campaign order, correct target classes and a
      191-step reach limit. Airfield denial uses the first three enemy
      airfields; interdiction and SEAD use their own classes.
- [x] **Role-aware aircraft types** — interceptors, escorts and strike flights
      draw the appropriate type instead of the side's default fighter.
- [x] **Availability-based extra flights** — wild-weasel/escort/SEAD elements
      are only added when the airfield still holds the aircraft.
- [ ] **Exact target-to-airfield pairing** — the captured blocks assign
      particular targets to particular airfields; the generator still cycles
      within a reachable list. This is why `SU25` is used and `SU34` is not,
      and why the block split is 5/4 rather than 6/3 in places.

## 4. GUI

- [x] **Campaign mission list** — flying missions of the selected side, in file
      order, labelled role + target; scrolls to and highlights the selection.
- [x] **Mission overview** — location, target element, weapon, base, diversion,
      role and flight counts filled from the selected sortie.
- [x] **Debrief window** — target damage from the live campaign state, status,
      aircraft type and the selected pilot row. Enemy kills, additional targets
      and the overall rating stay zero until the flight simulation exists.
- [x] **Briefing time** — taken from the sortie's earliest `TAKE_OFF`
      `TIMESLOT`, with the campaign clock as the fallback for on-demand
      intercepts.

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
