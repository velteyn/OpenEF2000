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
- [x] **Corridor source** — the captured intermediate fixes are exact
      `NAVIG.NAV` corridor routes: 571/598 (95.5%) match a navigation waypoint,
      with the outbound route's last point used as `INGRESS` and the egress
      route reversed. Format and examples are in `FILE_FORMATS.md` §F9 and
      `CAMPAIGN-GENERATOR.md` §5.3b.
- [x] **Corridor entry** — the navigation entry is the target's map cell:
      `(x/40) + 10*(9 - (y-1)/40)`. All 19 distinct captured pairs match.
- [x] **Corridor mechanism** — the outbound and egress routes are two uniform
      draws from that entry's route list, matching the capture's non-nearest,
      non-shortest choices.
- [ ] **Exact corridor parity** — the two draws fall at a different point in
      our RNG stream, so the chosen corridor matches only by chance (5/15
      outbound, 1/15 egress). Candidate set and geometry are exact.

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
- [x] **Quick Combat Summary** — the level table (`cfg\arcroo/arcpro/arctop.txt`)
      names a `.brf` briefing per level; the panel now shows it, wraps it, and
      follows both the level and the difficulty selection. Pro and level 01
      open selected, as shipped.
- [x] **Difficulty and mission selection** — icon templates now inherit the
      bevel, `esg` group and verbs from the previous icon, so Rookie/Pro/Topgun
      and levels 02..12 are drawn and clickable like 01.
- [x] **Hi-score panel** — raised by the arcade startup routine (now run on
      every screen entry) and read from `HISCORES.DAT`.
- [x] **Simulator mission list** — `simul` fills its twenty template rows
      from the type table (`cfg\free.txt`, `basic.txt`, `weapon.txt`,
      `air2air.txt`, `air2grnd.txt`, `advanced.txt`), hides unused rows, and
      shows the selected mission's `.brf` briefing in Mission Details through
      `onopenCurrentMission`.
- [ ] **Simulator launch** — Parameters Accept records `mission_accepted` and
      the choices, but no flight is started. Needs a mission loader for the
      selected `.cfg` and the chosen time/weather/difficulty.
- [ ] **List scrollbar** — the overflow scroll works with the wheel but the
      list has no visible thumb, because the `.gui` file does not declare
      `yscroll`. Confirm from a runtime capture whether the original draws
      one.

## 5. Campaign manager

- [x] **Sortie recovery condition** — the Strategy Guide states that landing
      on an allied base and coming to a complete stop with wheel brakes on and
      engines off rearms and refuels the aircraft, so the sortie does not end
      there; shutting down at an enemy base ends the mission automatically.
      Ending without landing costs 10 % score, ejecting 10 %, killed 20 %.
      Quick Combat ends on its mission clock or end-of-mission menu. This
      explains the observed "landed but the mission did not finish" case.
- [ ] **Campaign debrief trigger** — confirm from a runtime capture whether the
      debrief opens on shutdown at the home base only or at any allied base.
      The flight loop must then model "recovered at home", "rearmed at an
      allied base", "diverted" and "lost" separately.

- [ ] Victory/defeat target-count branch (the class sets are not decoded yet).
- [ ] Attrition and production between turns. **Blocked on an oracle**: the
      original author describes a heuristic, state-driven system; our captures
      show ~1.5 % of units changing side/type per turn. Needs a fixed-seed
      runtime trace before it can be reproduced rather than guessed.

## 6. Cosmetic / smaller

- [ ] **Widget shading and font** — the original fills panels with a dithered
      grey gradient and draws text with its own bitmap font; the reconstruction
      uses flat `gui.col` fills and a host TTF. Compared side by side with a
      DOSBox-X capture via `scripts/gui-compare.sh`; see the deviation table in
      `docs/ENGINE-NOTES.md`. Reproducing it needs the original drawing rules
      or the shade/fade table, not a different palette.
- [x] **Timetable structure** — one row per flight inside the eight-hour turn,
      flight index in the final field, support/kill packages at the top, CAP
      waves ~252 minutes apart, convoys on a 45-minute cadence, chronological
      per base. Measured 455 rows against the capture's 453.
- [ ] **Exact strike wave times** — the original launches strike packages from
      about 02:04 while CAP is still airborne; the queue-based planner starts
      them at 04:16. The wave planner rules are not decoded yet.
- [x] **Escort flight sizes** — escorts take the remaining airframes at their
      airfield, 1–4 ships; support escorts stay at four. The captured 18×4,
      2×2, 2×1, 1×3 mix is now representable. Which packages receive an escort
      is still approximate.
- [x] **Refuel fix** — the strike `REFUEL` waypoint follows the tanker orbit's
      far station, recorded per airfield when the tanker patrol is emitted and
      patched into the strike route afterwards. Refuelling is now limited to
      the six tanker airfields; offsets are within ~8 cells of the capture.
      The exact tanker orbit geometry is still approximate.
