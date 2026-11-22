# EF2000 campaign generator — reverse-engineered specification

This document describes how the shipped EF2000 v2.0 executable turns the
static scenario (`CONFLICT.CG`) and a saved campaign (`CAMPAIGN.CG`) into a
new campaign: the regenerated `CAMPAIGN.CG` and the mission set
`MISSION.CFG`. It is a factual specification derived from two evidence
sources:

* **Runtime capture.** The original game was run under DOSBox-X with the
  in-process control server; a new campaign was started from the Campaign
  Level dialog and the generated files were captured. Four generations were
  compared.
* **Static analysis.** The shipped v2.0 executable was reconstructed and
  analysed with Ghidra. Function addresses below refer to that reconstructed
  image (image base `0x10000`). The code is not reproduced here; only the
  observed structure and rules are recorded.

No claim in this document is based on a filename or a string alone. Items
marked *inferred* still need a second evidence source.

## 1. Files

| File | Role in generation |
|---|---|
| `CONFLICT.CG` | Static scenario: target areas, mobiles, pilots, allegiances. **Never modified.** |
| `CAMPAIGN.CG` | Live campaign state. **Rewritten** by the generator. Also the source of the previous state. |
| `MISSION.CFG` | Generated mission/tasking set. **Rewritten.** |
| `NAVIG.NAV` | Base navigation. **Not modified** by generation. |

Observed: after generation, `CONFLICT.CG` and `NAVIG.NAV` are byte-identical
to their pre-run copies; only `CAMPAIGN.CG` and `MISSION.CFG` change.

## 2. Entry point and top-level flow

The generator is built into the game executable and runs synchronously after
**Accept** on the Campaign Level dialog. The UI shows a small
`Generating Campaign...` panel until the files are written.

Reference functions (v2 image):

| Address | Role |
|---|---|
| `0x1b2a8` | Campaign-level Accept path; calls the generator |
| `0xbec20` | Campaign generation orchestrator |
| `0xbe0e0` | Advance campaign clock by 8 hours |
| `0xca100` | Mission-package size policy |
| `0x10756c` | Add virtual bases/ports from the embedded base table |
| `0xca990` | Add a virtual port from a mobile/target record |
| `0xda738` | Air-defence (SAM/AAA/EWR) mission loop |
| `0xdabd0` | Build one air-defence mission |
| `0xcad30` | Mobile/tasking list setup |
| `0xcbf44` | Pilot assignment |
| `0xd8a64` | Link sites/mobiles to target areas and set target values |
| `0xd88b4` | Randomise target values and map-cell indices |
| `0xc0d28` | Write `CAMPAIGN.CG` |
| `0xc0e0c` | Write `MISSION.CFG` |
| `0xb2467` | Custom random-number generator |
| `0xb2498` | Seed the custom RNG |

Generation follows these phases:

1. **Reset / load.** Clear the live campaign state, load the scenario and the
   previous campaign, and initialise the world clock.
2. **Side and theatre setup.** Choose the active sides and the theatre state;
   the campaign clock (day, 8-hour slot, minute) is part of the state.
3. **Base and port construction.** Create the fixed set of airfields and ports,
   count the mobiles assigned to each, and build their 24-hour timeslot
   schedules.
4. **Ground order of battle.** On a fresh campaign, replace the scenario's
   ground units with one group per defence site, drawing each unit's type from
   the side's anti-air mix (section 8.3a).
5. **Air-defence tasking.** For each target area with an air-defence site
   list, allocate sites and build `GBASED` / `AIR_DEFENSE` missions.
6. **Strike, patrol and ship tasking.** Build `PLANE` and `SHIP` missions and
   their `FLIGHT`/`PYLON`/route records.
7. **Pilot assignment.** Attach pilots to flights.
8. **Linking and validation.** Match sites to target areas, set target
   values, drop invalid or unassigned records, and enforce the victory/defeat
   checks.
9. **Write.** Rewrite `CAMPAIGN.CG` and `MISSION.CFG`.

The UI wrapper (`0x1b2a8`) also performs the post-generation screen setup, so
a failed generation leaves the campaign screen open rather than launching a
mission.

## 3. Inputs and parameters

### 3.1 Campaign Level dialog

The dialog has ten difficulty rows (0 = Rookie … 6 = Pro … 9 = TopGun), an
**Influence Campaign?** Yes/No pair, and **Accept**. The dialog's values are
kept in runtime globals and are written to the campaign file:

* `DIFFICULTY` — one byte, 0..9, read from the difficulty global.
* `PERFORMANCE` — two values: an influence/performance flag and a rating
  (observed `1 50`).

Observed: editing `DIFFICULTY` in `CAMPAIGN.CG` alone does **not** change the
generated difficulty; the dialog's runtime value wins and is written back
(e.g. an input file set to 0 regenerated as 6). The save file is an output of
the setting, not its source.

*Inferred:* the difficulty scales mission-package sizes. The package policy
uses the campaign day: days 0..3 produce a small package (2), later days a
large one (25); the day parity selects an additional branch. The direct
mapping from the 0..9 difficulty byte to those thresholds is not yet pinned
down because our runtime runs all used the Pro setting.

### 3.2 World clock

`WORLD_STATUS` is a packed campaign clock. In file order:

* `day` (starts at 0, displayed as day + 1),
* `hour` — the 8-hour slot, 0 / 8 / 16,
* `minute`,
* three trailing words (the last two carry the generator's RNG state).

The advance routine (`0xbe0e0`) adds 8 to the hour; past 16 it wraps to 0 and
increments the day, and the minute is cleared. Implemented as
`advance_campaign_clock` and exposed as `--campaign-advance N`; the clock is
carried forward from the previous campaign rather than reset. The clock feeds:

* the time-slot schedules of every base,
* the mission-package policy,
* the `WORLD_STATUS` header in both output files.

### 3.3 Randomness

The generator uses a **custom 32-bit additive generator**, not the C library
`rand()`:

* state lives at `0xb2458` (two 16-bit halves), default `0x31415926`;
* each draw computes `high' = high + low`, then
  `low' = low + high'` with carry; a zero state is reset to `0x31415926`;
* the returned value is the new 32-bit state;
* the seeding routine (`0xb2498`) writes a 32-bit seed; the startup path
  reads the **DOS date/time** via `INT 21h` and seeds with it
  (`0x13aa0` → `0xb2428` → `0xffe6d`/`0xfff30`).

Consequences, confirmed by the runtime runs:

* each **game launch** has a different sequence, so two generations from the
  same files produce different mission counts and target values;
* within one launch the generator is deterministic: the same state and the
  same input files reproduce the same output;
* a reset path in the GUI/engine sets the state back to `0x31415926`, but the
  generation path does not reseed.

The random generator is used for target values, site placement and the
air-defence tasking. It does not change the fixed base table or the total
aircraft pool.

## 4. Deterministic backbone

Across all captured runs these are identical (or fixed up to the value field):

| Item | Value |
|---|---|
| Bases | **77**, fixed names, sides and types |
| Base mix | NATO 5 airfields + 10 ports; Russian 15 + 21; Finnish 3 + 8; Swedish 4 + 4; Baltic 1 + 6 |
| Aircraft total | **611** airframes |
| Aircraft mix | Su-35 291, EFA 106, JAS-39 54, F/A-18 54, Su-27 18, Su-33 14, F-22 12, Harrier 12, plus smaller types |
| Fixed roles | CAP 162, INTC 26, SEA_SUPPLY 13, REFUELLER_PATROL 6, AWACS_PATROL 2 |
| Target areas in `CAMPAIGN.CG` | **543**, same names and order every run |
| Scenario mobiles | **6458**: 3566 air (`PL`, 42 side/position groups), 2502 ground (`GB`, 500 groups of 24), 390 naval (`SH`, 6 groups) |
| Pilots | **6458**, one per mobile, carrying the mobile's `3DP` position |
| `CONFLICT.CG` / `NAVIG.NAV` | byte-identical after generation |

The 68 named bases take their **positions from the mobile pilots' `3DP`**
records; the names and order come from the `SSNAME` list. A second, embedded
table supplies the nine **virtual ports** (Gdansk, Sassnitz, Karlshamn,
Helsingor, Muros, Inverness, Baltiysk, nayyanmar, Archangelsk). Each base also
gets:

* per-class mobile counts (air, ground, ship),
* a 9-byte timeslot array,
* timeslots spaced `480 / count` minutes apart and wrapped over 24 hours.

The aircraft pool and its role totals are fixed; the generator distributes
that pool over flights and bases, so individual `FLIGHT` records and roles
vary while the totals do not.

## 5. Randomised layer

### 5.1 Target values

The value draw is **exactly recoverable**. The generator seeds its custom RNG
with the Unix timestamp of the DOS clock (`0xb2428` converts the `INT 21h`
date/time through `mktime`), and it writes that same value into the file
header. Feeding a captured file's timestamp back as the seed therefore
reproduces its draw sequence.

Rules:

* Only **airfields and ports** are valued. Every other installation consumes
  no random number.
* One draw per valued area, in output order.
* The **upper half** of the state selects the thousands digit:
  `thousands = ((state >> 16) % 5) * 1000`.
* The value is `thousands + site`, where `site` is the installation's worth,
  1000 when intact. A damaged installation rolls the value back into the
  previous thousand (982 gives 4982, not 5982).
* Rigs (type 79) and supply towns (159) are never valued; they stay at 1000.

Before the value pass a fresh campaign consumes **three clock draws**
(`0xbe068`): `WORLD_STATUS[3] = d1 % 3`, `[4] = d3 % 46`, `[5] = d2 % 360`
with `d = AX >> 1`. The fixed-clock capture's `0 0 0 2 16 336` is reproduced
exactly, so this is a generator phase, not a shell offset. `--campaign-offset`
remains as a diagnostic skip only; a replay uses `0`.

Verification: replaying a captured run's timestamp as the seed reproduces the
draw of **all 72 valued areas**: each capture value lies in the drawn thousand
or the previous one (`capture // 1000` equals `d` or `d - 1`), the difference
being the site damage. The area order is identical. An independent first
campaign capture (TACTCOM, header `1795287949`) follows the same rule.

The remaining difference is the low `site` component. It is `1000 - damage`,
and the damage is not drawn in the value loop. In the fresh capture it depends
on the installation class: airfields 9..18, bridges 0..5, supply/oil/power/
political towns 0..4, early-warning sites 0..2, rigs, ports, docks, dams and
tunnels 0.

Three routines bracket the value chain:

* `0xd88b4` is the value-randomisation pass. For records whose class byte is
  `'1'`, `'I'`, `0x0b` or `'V'` it draws once, takes the remainder modulo 5,
  adds one and multiplies by 1000, storing the result in the area record;
  everything else gets 1000. The same routine derives the map-cell index and
  then walks the area's element array, calling `0xd9bd0` per element type.
* `0xd8ac0` is the element-health pass. Each element carries a health byte at
  `+0x5`; when it is below 100 the routine adds a random amount: modulo 35 for
  element kinds 53–55 (control tower, apron, runway), modulo 10 when the
  `0xca5cc` predicate holds, otherwise modulo 5. The result is capped at 100.
* `0xd8a64` copies values from a matching *site* record into the target area
  when their positions agree: the area value becomes `site+0x23`, plus 1000
  when that is below 1000, and the area's companion word becomes `site+0x25`.
  `0xca990` builds such site records from a mobile/target record, copying its
  value at `+0x32` and setting `+0x25` to half the thousands digit.

So the low digits are already present in the site/mobile value when the area
copies it; they are not recomputed by the value loop. Empirical check:
the damage does not correlate with any bit or modulus of the value draw over
the 72 valued areas of the fresh capture, which rules out a second use of the
same random number. Set `OPENEF2000_DUMP_DRAWS=1` to print the value-pass RNG
state per valued area for that kind of comparison.

#### The two trailing SSNAME numbers are one element count

Decompiling the SSNAME parser (`0x1f83c`) shows the record layout is not
`value, airfield1, airfield2`:

1. name, side;
2. value into `area+0x32`;
3. a short **read and discarded** (it is the count used for the element bytes
   that may follow, and for the `ALL` branch that fills health 100);
4. a short stored at `area+0x2a`, which is the element count used by the
   value, element and writer passes.

The writer prints `area+0x2a` twice, which is why captured lines always have
equal trailing numbers (`31 31`, `32 32`). The two input numbers are not
independent fields; our `ScenarioArea::airfield1/airfield2` names are a
misnomer for what is really one element count.

#### A/B evidence: the low component is a later, element-dependent pass

The fixed-clock harness allows one-input experiments. Changing Vaernes' input
value (`2000` to `4000`) produced a **zero diff**, so the input value is
ignored. Changing its element count (`31 31` to `15 15`, or `1 1`) instead:

* kept every area's *thousands* digit unchanged;
* set Vaernes' low component to `000` (value `3000`, round);
* shifted the low components of neighbouring areas by a few units.

That means the low component is produced in a **later RNG-driven pass whose
draw count depends on the element count**, after the thousands pass has
finished (which is why the thousands digits remain aligned).

A second sweep pins the dependence down:

| Vaernes element count | Thousands | Low component | Damage |
|---:|---:|---:|---:|
| 1 | 3 | 000 | 0 |
| 15 | 3 | 000 | 0 |
| 23 | 3 | 995 | 5 |
| 31 | 3 | 987 | 13 |

The thousands digit never moves. Damage grows with the **trailing** labels of
the model: the first fifteen contribute nothing, 16..23 contribute five,
24..31 contribute eight. That is what a count of individually damaged labels
looks like, not a formula over the area's value. The exact health roll and
which label kinds can be damaged are the remaining question; the next REA
step is to trace the value at `area+0x32`/`base+0x23` through the passes
between `0xcad30` and `0xd8a64`, and to compare the label order the parser
stores with the `.ssd` table (`--ssd-labels`).

The same value feeds the corresponding `BASE` record, so the base and its
target area stay in sync.

### 5.1a Campaign update passes (before tasking)

Before missions are built, the generator updates the previous campaign state:

* **Link pass** (`0xd8d28`): each mobile/site record (27-byte stride) is matched
  to a target by its coordinates and side, and the target's site block is
  copied in.
* **Attrition pass** (`0xd8e50`): matching sites lose health; a site whose
  health falls below 75 (and is not already in a special state) is marked
  damaged, and a destroyed state propagates to its target. The pass only runs
  on sites that are not already resolved.
* **Repair pass** (`0xd8ac0`): a site below full health gains a small random
  amount (`RNG % 10`, or `RNG % 35` for some classes), capped at 100.

The 21-byte site record carries a health byte at offset 5 (0..100) and the
site's coordinates at +7/+9/+11. The mission count at an airfield reflects the
available sites after these passes; for example Vaxjo's 9 Patriot and 6 Rapier
mobiles become 7 and 4 missions in the sampled run.

The exact damage/availability formula and the site-to-target ownership rules
are the main remaining open item for byte-exact reproduction.

### 5.2 Air-defence tasking

`GBASED` / `AIR_DEFENSE` missions are generated from each target area's site
list. The loop is `0xda738`:

* each site record is 21 bytes; the list length is carried by the target
  area record (`+0x2a`). Sites are copied in list order, so list index *s* is
  slot *s*;
* the slot counter increments before use and wraps at four: west
  `Vulcan, Roland, Patriot, Rapier`; east `SA-6, SA-11, SA-11, ZSU-23-4`;
* the available count is the number of unused ground mobiles of the slot's
  type at the area's exact coordinate and side;
* if 9 or more remain, the mission carries eight and the next slot continues;
  if 1..8 remain, the mission carries the remainder; at zero the slot is
  skipped;
* each consumed mobile is marked tasked, so later slots of the same type find
  none and are skipped.

The mission count is therefore a function of the site list and the generated
ground roster, not a fixed per-site count. A captured run has **476**
air-defence missions; the current generator produces 494 with almost the same
unit totals (987 vs 990 SAM units). The remaining spread is the RNG stream
position (below).

### 5.3 Strike targets and packages

`PLANE`/`SHIP` missions carry the strike, CAP, interception, SEAD and support
tasking. The air-defence/strike package policy uses the campaign day:

* days 0..3 → a small package (2),
* later days → a large package (25),
* day parity selects an additional branch,
* the influence flag alters the branch taken.

The target areas attached to strike missions are a subset of the 543 campaign
areas; observed **28–33** per generated mission set, with the same type mix
(AIRFIELD, PORT, EWR, CHOKE_POINT, POLITICAL_INDUSTRIAL, POL_TYPE).

### 5.3a Package basing and composition

Package basing is driven by the stored order of battle, not by a cursor over
all airfields:

* **Targets** are the other side's installations, kept in campaign (area)
  order. Airfield denial uses the first three enemy airfields; interdiction
  uses ports, oil rigs, bridges, tunnels and supply towns (types 79, 91, 95,
  96, 127, 142, 143, 204); SEAD uses early-warning sites (type 63).
  Interdiction and SEAD targets must lie within 191 grid steps (Manhattan) of
  the attacking airfield.
* **Airfield denial assignment** is a block schedule, not a per-base cycle.
  Each side takes the first three enemy airfields in campaign order; one
  airfield serves a block of **three packages**, then the next. A block target
  farther than 150 units (Chebyshev) from the package's base is replaced by
  the previous target in the list (or the nearest one). This reproduces the
  captured NATO sequence exactly: Rygge 3 × Vaernes, then 2 × Orland continued
  by Gardemoen 1 × Orland, then Molde 3 × Bodo. The captured Russian sequence
  is the same shape (2 × Kristiansand, 3 × Rygge); our package count is one
  higher there, so it emits three Kristiansand packages.
* **Strike airfields** form role-specific blocks. Airfields are ranked by the
  suitability of their aircraft type — `TORNADO` then `HARRIER_GR6` for
  airfield denial, `F117A` then `F111R` for interdiction, `SU25` then `SU34`
  then `MIG27` for the Russian equivalents, and multi-role fighters for SEAD.
  Each airfield is filled before the next is used, which reproduces the
  captured blocks (`Rygge` 6, `Molde` 3; `Gardemoen` 10, `Molde` 1;
  `Orland` 2, `Vaernes` 4; `Narvic` 1; `Bodo` 2).
* **CAP** is not spread evenly. Each side's CAP quota is allocated from the
  highest-numbered airfield down: an airfield receives eight sorties while
  four remain for every lower airfield, and the remainder goes to the lowest.
  This reproduces the captured per-airfield counts exactly for all four sides
  (Russian 6..13 → 4, 14..26 → 8; NATO 1..4 → 4, 5 → 2; Swedish and Finnish
  8 each).
* **Support** flights use the airframe type, not the side's default fighter.
  Tankers take the first airfields holding `R767`/`IL78M` (NATO 2, 3, 5;
  Russian 8, 12, 13), AWACS the base holding `E767`/`A50`, JSTARS the base
  holding `J767`, and the anti-tanker/anti-AWACS flights launch from the first
  tanker base. Captured bases match exactly.
* **Combat types** follow the role: interceptors prefer the dedicated
  interceptor (`F15`, `SU35`), escorts the multi-role fighter (`EFA`, `F22`,
  `SU35`), and strike flights the first suitable airframe in the stored order
  at their airfield. Wild-weasel and escort flights are only added when the
  airfield still holds the aircraft for them, which is why some captured
  packages are a single strike flight.

Measured against the capture: all 26 CAP airfields and all 8 support bases
match; the flyable airframe types used are 638 against 612, and the per-type
flight counts agree within a few sorties (`SU35` 119/119, `JAS39` 27/27,
`F18` 27/27, `SU27` 9/9, `SU33` 6/6, `TORNADO` 4/4). The airfield-denial
target sequence now matches the captured NATO block exactly (3-3-3 across
Vaernes, Orland, Bodo); the remaining difference is the number of packages
each base contributes, not which target a package receives.

### 5.3b Corridor source: `NAVIG.NAV`

Strike corridors are not invented geometry. Every intermediate fix in the
captured strike routes is a waypoint of the base-navigation file: **571 of 598**
captured strike fixes (95.5%) match a `NAVIG.NAV` waypoint exactly, and the
routes are complete corridor routes from that file.

The captured package selects one route for the outbound leg and one for the
egress leg:

* the outbound route is taken forward; its final point is written as the
  `INGRESS` fix (altitude 5), followed by the target;
* the egress route is taken reversed, so it starts near the target and ends
  near the base.

Examples on the reference base (Rygge, 118/42):

| Package | Target | Outbound | Egress |
|---|---|---|---|
| 694 | Vaernes | entry 63, corridor 4, route 7 `(92,71)…(120,133)` | entry 63, corridor 5, route 6 reversed `(135,159)…(83,81)` |
| 695 | Vaernes | entry 63, corridor 5, route 6 | the same route reversed |
| 697 | Orland | entry 62, corridor 0, route 3 | entry 62, corridor 3, route 5 reversed |
| 712 | Steinkjer Tn | entry 63, corridor 5, route 9 | entry 63, corridor 3, route 6 reversed |

The **entry selection is now decoded**: the entry id is the target's map cell,
`id = (x / 40) + 10 * (9 - (y - 1) / 40)`. All 19 distinct captured
(base, target) pairs land on the captured entry under this formula.

Within that entry the package takes two **uniform random draws** from the
entry's route list: one for the outbound corridor and one for the egress
corridor. The capture's corridor choice is consistent with uniform draws (the
chosen route is not the nearest, shortest or longest; over 28 packages the
identity of the outbound corridor is effectively chance-level when the random
stream differs). The generator uses the same two-draw mechanism.

Measured over the generated strike flights: **400 of 400 intermediate fixes
(100%) are exact navigation waypoints**, against 99.4% in the capture, and the
mean route length is **13.22** against 13.36. Exact corridor identity still
matches only the chance level (5/15 outbound, 1/15 egress) because the two
draws sit at a different point in our RNG stream; the candidate set is now
exact.

### 5.3b Route geometry

Mission routes are not random walks. The captured files show three shapes:

* **Fighters** (CAP, INTC) fly a compact patrol box about **five map units
  across**, offset from the base by up to eighteen units, with the loop point
  on the first patrol fix. Altitude 90.
* **AWACS and JSTARS** orbit a station about **forty units** from the base on a
  wide loop (span ~34). Altitude 250.
* **Tankers** fly a wide racetrack (span ~39) centred on their base.
* **Strike packages** take off, optionally **refuel** at a tanker track behind
  the base, fly a meandering corridor to the target, strike and recover along
  a second corridor. Every flight in a package flies the *same* geometry; the
  strike flight alone labels the ingress and target fixes, while the escort and
  wild-weasel flights show them as plain `NO_ACTION` fixes.

`REFUEL` is present when the package's base hosts a `REFUELLER_PATROL` tanker
and the target is more than about **100 map units** away. The refuel fix is the
tanker orbit's far station — roughly fourteen cells behind the orbit centre,
which itself sits about twelve cells behind the base. The generator records
that station per airfield when it emits the tanker patrol and patches the
strike `REFUEL` waypoints from it afterwards, so the fix follows the same
tanker the package would meet. The measured offsets are within about eight
cells of the capture's `(-26,-1)`, `(-19,-10)`, `(-30,-6)`, `(-27,-4)` and
`(-14,-7)`. CAP and INTC never refuel. The tanker bases are taken from the
airfields that actually hold `R767`/`IL78M`, not from a cursor over the base
list, so a package at a non-tanker airfield cannot receive a `REFUEL` fix.

Each corridor is a chain of random fixes along the base–target line with
lateral jitter; the number of fixes is drawn, not derived from range. The
captured packages carry **0–10 intermediate fixes per leg** (route lengths
5–21). Altitude is a per-package choice of **10 or 120**, with the ingress fix
at 5.

Measured over the 29 captured strike/SEAD packages: mean route length **13.31**
(capture) against **13.10** (generator), full range 5–21 against 7–22, altitude
120/10 split 17/11 against 19/10. The two wild-weasel/escort flights of seven
multi-flight packages are exact copies of their strike route in both files.
The refuel share is 19/29 in the capture but lower in the generator because
package-to-base assignment is still approximate (see Package composition).

The generator reproduces the fighter box and the support orbits; measured
against the capture the fighter patrol span is 5.3 units against 5.0.

### 5.3c Attack patterns

The attack geometry is **constant**, not generated: every captured
`LAYDOWN_ATTACK` block is byte-identical to every other, and so are the
`MOBILE_ATTACK` and `STAND_OFF_ATTACK` blocks. Each pattern carries six routes:

| Pattern | Routes | Nodes | Used by |
|---|---:|---:|---|
| `LAYDOWN_ATTACK` | 6 | 4 | strike against an airfield |
| `STAND_OFF_ATTACK` | 6 | 5 | strike against anything else, and SEAD |
| `MOBILE_ATTACK` | 6 | 2 | wild-weasel flights |

Each strike flight's `TARGET_AREA` lists the installation's own sub-elements:
the `.ssd` label entries (excluding the defence ring and the dispersal parking
spots), rotated by the area heading, with the kind naming the element
(`HANGAR8`, `CONTROL_TOWER`, `RUNWAY`, …). A mission carries two, four or six
of them, and the strike flight's size follows that count.

The three templates are reproduced exactly. Which pattern appears depends on
the flight role and the target class: airfield-denial packages strike airfields
(laydown), interdiction and SEAD go after the ports and other installations
(stand-off), and wild-weasel elements always use the mobile pattern.

Airfield-denial packages carry either a wild-weasel element or an escort, and
some add an escort on top of the weasels. Measured against the capture the
pattern counts are now 15 / 9 / 23 against 13 / 9 / 20; the remaining spread is
the package policy, which the original also varies from turn to turn.

### 5.4 Victory and defeat

The generator evaluates the war state before tasking and stops if the campaign
is over. The loss branch is clear from the binary: the fraction of a side's
mobiles marked dead or crippled is compared against **0.75** (collapse) and
**0.95** (destruction). A second branch counts held strategic targets and is
gated on a zero or greater-than-eleven count; the exact target-class sets it
uses are still open. `--campaign-status` prints the ratios and thresholds.

## 6. Output invariants

`CAMPAIGN.CG`:

* `DIFFICULTY` and `PERFORMANCE` from the runtime globals;
* `WORLD_STATUS` from the campaign clock;
* `NUM_TARGET_AREAS` 543, same names/order, values recomputed;
* `NUM_MOBILES`, `MOB`/`PIL` blocks and `NUM_ALLEGIANCES` rewritten from the
  current state.

`MISSION.CFG`:

* `NUM_MISSIONS` and `INCLUDE_PILOTS`;
* each `MISSION` line is `MISSION <kind> <side> <role> <status> <base> <id>
  <groups>`, where `<base>` is the flying base index (`-1` for a ground
  battery) and `<groups>` is the number of flights or groups in the package;
* `WORLD_STATUS`;
* `MISSION`, `GROUP`, `3DP`, `WP_ROUTE`, `WAYPOINT`, `TARGET`,
  `TARGET_AREA`, `ATTACK_PATTERN`, `ATTACK_ROUTE`, `ATTACK_NODE`,
  `FLIGHT`, `PYLON`, `PIL`, `BASE`, `TIMESLOTS`/`TIMESLOT` in the order the
  writers emit them.

Observed generated sets:

| Run | Missions | GBASED | PLANE | SHIP | Flights | Bases | Strike areas |
|---|---:|---:|---:|---:|---:|---:|---:|
| baseline | 723 | 481 | 229 | 13 | 612 | 77 | 33 |
| Pro A | 733 | 495 | 225 | 13 | 611 | 77 | 28 |
| Pro B | 729 | 487 | 229 | 13 | 611 | 77 | 33 |
| Pro C | 741 | 502 | 226 | 13 | 611 | 77 | 30 |

The variation is concentrated in `GBASED` air-defence tasking, flight
tasking roles, target values and timeslots; the base table, aircraft totals
and fixed roles do not vary.

## 7. Evidence and open questions

**Observed:** the phase order, the fixed base table and aircraft pool, the
custom RNG and its date/time seed, the target-value formula's thousands digit,
the 77-base timeslot structure, the 481–502 air-defence range, the 543
target-area order, and the fact that only `CAMPAIGN.CG` and `MISSION.CFG` are
rewritten.

**Inferred / still open:**

* the exact mapping from the 0..9 difficulty byte and the influence flag to
  the package-size thresholds;
* the low `site` component of a target value (982..1000): it varies per turn
  and is not drawn in the value loop, so it comes from the installation's
  sub-target list or the site-health pass;
* the `MOB` and `PIL` numeric tail semantics;
* the attack-pattern/node generation rules (routes, actions, counts);
* whether the RNG is advanced by other game systems between launch and
  generation (the seed is at startup; the number of draws before generation
  depends on startup work).

## 8. The fixed target/base table

The generator does not take its area list from `CONFLICT.CG`. It walks a
**543-entry table stored as a plain-text block inside `DID1ST.DAT`**:

```
x  y  altitude  model.sss  "Name"  ;type
164  14  0  airfld_1.sss  "Vaxjo"  ;40
```

`type` is the installation class:

| Type | Meaning | Count |
|---|---|---:|
| 40..47 | airfield | 28 |
| 90..95 | port (supply, oil, power, political, army) | 31 |
| 204..207 | naval station / dock | 13 |
| 63 | reserve airfield (`early1.sss`) | 31 |
| 79 | oil rig | 18 |
| 96..143, 159 | towns, industry, EWR, bridges, chokepoints | 422 |

The scenario's 542 `SSNAME` records are matched to the table **by name**. The
one table entry missing from `CONFLICT.CG` (`Visby`) is added after its nearest
preceding table neighbour, landing between Shimsk and Vardo. The written
`CAMPAIGN.CG` therefore has **543** areas.

**The output order comes from the scenario file, not the table.** `CONFLICT.CG`
already lists the areas in the generator's order (the rigs, for example, start
at Sleipner rather than at the table's Albuskjell). Every captured run keeps
that order unchanged, so it is carried forward with the ownership.

### 8.1 The live order of battle is carried forward

Mobiles come from the **previous `CAMPAIGN.CG`**, not from `CONFLICT.CG`;
`CONFLICT.CG` only seeds the first campaign. Between turns the mix changes by
offsetting conversions and losses, e.g. one captured step moved a side from
269 to 255 SA-11 while its ZSU count rose from 92 to 126, and dropped 30
Swedish BEDFORDs. The totals drift by a few dozen units per turn.

An airframe belongs to the base at its position and side, and a base carries at
most **49** airframes; the surplus stays in reserve. That reproduces the
captured split exactly: 1,235 `OK` (based) and 2,331 `RESERVED`. The flights
then draw from the based airframes, preferring the role's type.

Combat results live in those same fields, not in a separate debrief file: a
played campaign carries `DEAD` mobiles (170 in the later 3DFX state) and
reduced strengths (156, 82, 76). The generator skips `DEAD` and `CRIPPLED`
units when it tasks, and never revives them. The debrief screen reads the same
state to display the outcome.

### 8.2 Ownership is inherited, not recomputed

The previous `CAMPAIGN.CG` is the ownership source. In the shipped state 35
NATO areas are already Russian (`Vaernes`, `Bodo`, `Trondheim`, the
Steinkjer/Ballangen/Trofors corridor, ...). Those sides survive generation
unchanged, and the `BASE` blocks take the side of their area. Editing
`CONFLICT.CG` alone therefore does not reset the front.

The two 1997-era campaign files shipped in `Campaign_SourceData` (all
difficulties) carry exactly the same 35 flips. A **fresh** campaign generated
from `CONFLICT.CG` alone reproduces them as well: both fixed-clock captures
flip exactly 35 NATO areas, and the set is the NATO areas within a
**120-unit Chebyshev square** of the original Russian territory (all flipped
areas are at distance ≤115, all kept areas ≥124). The generator now applies
that advance once, against the original sides, before basing and tasking.
Whether the shipped game computes this front or loads an authored mask is not
settled; the measured rule matches both fresh captures exactly (35/35).

### 8.3 Defence slots and the mobile link

Every area carries a defence-slot list. Its positions come from the
installation supershape (`ss_loadtarg`, `0x4b910`): the label list is preceded
by a class byte and a 16-bit count (`0x4b948`), and every label whose kind is
`0xFFFF` is one defence point. The 20-byte `.ssd` records carry the same ring
for some models; for airfields they match the captured offsets exactly
(`trond.ssd`: `-200 -864`, `-703 669`, `190 682`, `233 -734`, …).

The slot count follows the target class:

| Class | Slots |
|---|---:|
| airfields 40..47 | 8 |
| 63 (EWR) | 1 |
| 91 (supply dock) | 2 |
| 90, 92..95, 205, 206 (ports) | 1 |
| 96..111 (towns) | 3 |
| 127 (bridges) | 4 |
| 142/143 (tunnels) | 1 |
| 192 | 1 |
| 193 | 3 |
| 194 | 1 |
| 195 | 4 |
| 204 | 5 |
| 79, 159, 207 | 0 (no defence sites) |

The slot loop picks a weapon class by counter, not by side alone:

* **Western** (side classes 0 and 2): `Vulcan, Roland, Patriot, Rapier`
* **Eastern** (side classes 1, 3, 4): `SA-6, SA-11, SA-11, ZSU-23-4`

Slot 0 asks for a Vulcan (west) or SA-6 (east), slot 1 for Roland or SA-11, and
so on. Roland never appears in the shipped order of battle, so that slot is
always empty. An eastern airfield therefore produces at most three missions
(SA-6, SA-11, ZSU) unless the SA-11 pool survives to slot 2.

A ground mobile becomes available to an area when it sits at the **area's exact
coordinate and on the area's side** — the link pass is a coordinate
comparison, and every generated ground unit is placed on its area.

### 8.3a Ground order of battle

A fresh campaign **regenerates the whole ground roster** from the target areas
(`0xbd96c`); `CONFLICT.CG` ground mobiles are discarded. Later turns carry the
previous roster in `CAMPAIGN.CG`. The routine is called **twice**
(`0xbee2f` and `0xbf1e8`): the first roster is built and discarded, the
side/victory evaluation consumes one draw per side (nine in the shipped
campaign), and the second roster is the one written. The first pass therefore
belongs to the shared RNG stream and cannot be skipped. The generator
allocates one group per defence site:

1. `groups` = sites whose kind byte is `0xFF`, plus `0xFE` entries when the
   area class is not `'I'`/`'O'` (`0xda678`).
2. `units` per group (`0xda6c0`) is fixed by the class byte: `1`, `2`, `H` or
   `I` gives 3; `V` and `0x0b` give 2; every other class draws `rng % 2 + 1`.
3. For each group, one draw selects the type (`d = AX % 10`, where `AX` is the
   low word of the custom RNG's 32-bit result).
4. For each unit, one draw decides: odd → a SAM of the group's type, even → a
   `BEDFORD_1` truck.
5. The SAM type weights are 5/3/2:
   * west `d < 5` **Patriot**, `5..7` **Rapier**, `8..9` **Vulcan**
   * east `d < 5` **SA-11**, `5..7` **SA-6**, `8..9` **ZSU-23-4**
   * side classes 5..8 (Danish/Norwegian/US/UK) never get a SAM.

This yields the captured totals: ~50% trucks, 50/30/20 type weights by side,
24 units at an airfield (8 groups × 3), 3 or 6 at a town (3 groups × 1..2),
4 or 8 at a bridge (4 groups × 1..2), 1 or 2 at a tunnel or EWR site. The
double pass reproduces **2519 ground units exactly** and the **composition of
510 of 543 areas**; the 33 residuals are class 63 (29 EWR sites) and the four
special stations 192/194/195 plus one class-101 town. Set
`OPENEF2000_DUMP_GROUND=1` to print the RNG state before, between and after
the two passes.

The supershapes of the static town models (`v_*`, `f_*`, `ctydoc_i`,
`tun_*`, `s_*`) are not shipped in either archive. The generator falls back to
per-class measured offset tables for those; the order is slot order, normalised
to heading zero and rotated per area. With them the captured town rings are
reproduced exactly, e.g. `v_amytwn`: slot 0 `38 489`, slot 1 `130 870`, slot 2
`-19 881`; `bridge1`: `145 -305`, `-502 -231`, `448 211`, `-609 214`.

### 8.3b Clock, values and the shared RNG stream

The generator's custom RNG is shared by every phase, so the draw order is part
of the format:

1. **Clock (3 draws, `0xbe068`).** `WORLD_STATUS[3] = d1 % 3`,
   `[4] = d3 % 46`, `[5] = d2 % 360` when `d = AX >> 1`. For the fixed-clock
   capture this is `0 0 0 2 16 336`, matched byte for byte.
2. **Target values (one draw per valued area).** Valued classes are the
   airfields, the ports and class 204: `value = (d % 5 + 1) * 1000 - damage`.
   All 72 valued areas in the capture match this draw; towns, bridges,
   tunnels, EWR sites and the political targets consume no draw and stay at
   `1000 - damage`.
3. **Difficulty marker (`0xbeabc`).** Draws `AX >> 1` until
   `DIFFICULTY + d % 3 - 1` is outside `{3, 5, 6}`. The rejected draws still
   advance the stream.
4. **First ground roster (`0xbd96c`).** One unit-size draw per area (for
   random classes), then one type draw and one per-unit draw per group. Its
   output is discarded but its draws advance the stream.
5. **Side/victory evaluation (`0xbe900`/`0xbe968`/`0xbe7c4`).** One draw per
   side — nine in the shipped campaign.
6. **Second ground roster (`0xbd96c`).** The same sequence again; this is the
   roster that is written.
7. **Tasking and routes.** The remaining draws build CAP boxes, corridors and
   flight details.

The clock, value and ground phases are now exact. With the seed
`829929601` the first pass starts after 76 draws, ends at 4449, the nine
inter-pass draws land on 4458, and the second pass starts there. Its unit
types match the capture in 510 of 543 areas.

### 8.3c Air-defence measurements

| | Captured | Generated |
|---|---:|---:|
| Air-defence missions | 476 | 479 |
| Vulcan units | 73 | 75 |
| Patriot units | 193 | 191 |
| Rapier units | 59 | 59 |
| SA-6 units | 236 | 236 |
| SA-11 units | 337 | 337 |
| ZSU-23-4 units | 89 | 91 |
| Ground units, all types | 2519 | **2519** |
| Ground compositions exact | — | **510 / 543 areas** |

### 8.3d Timetable rows and takeoff slots

`MISSION.CFG` carries a timetable per base. Each row is 9 bytes:

```
+0 day   +1 hour   +2 minute   +3 action   +4 role
+5 mission id (word)           +7 wave/flight (word)
```

`0xc0b44` writes a base block: `NUM_TIMESLOTS` counts rows whose action byte
is non-zero, then each row is written with
`TIMESLOT %hd %hd %hd %s %s %hd %hd`. Free rows (action 0) are omitted from
the file.

The skeleton is built when the base is created (`0xca990`):

* slot count `base+0x46` comes from `0xca80c` (capped at two for class `'1'`),
* row count `base+0x48 = (60 / (0xca8f8(class) / (base+0x44 >> 16))) << 3`,
* rows are spaced `480 / rows` minutes from the base hour, wrapping at 60
  minutes and 24 hours,
* action, role, id and wave start at zero.

In the fixed-clock capture the row count varies by base: Vaxjo, Rygge and
Gardemoen use 240 rows (a slot every two minutes), while the Russian base at
id 6 uses 480 rows (one-minute slots). CAP wave 1 occupies the first slots
from 0:00 and wave 2 starts at 252 minutes. Support flights take the next free
rows; strike and interdiction takeoffs come later and are **target-arrival
driven**, not simply the next free row (Rygge AF_KILL: 1:24, 3:00, 3:52,
4:58, 6:18). Each flight reserves a takeoff row and a landing row at
`takeoff + duration`.

`0xc2a80` returns the first free row, `0xc2aac` fills one row (action, role,
id, wave), and `0xc34f0`/`0xc34c8` find a free row near a preferred index.
`0xc34c8` scans forward from the preferred row and accepts the first free row
that leaves enough free rows in the following window; it falls back to the
last row of the base. `0xc30b4` converts the row count to the slot spacing:
`480 / rows` minutes.

The same slot spacing feeds the preferred takeoff. In `0xc3bd4` (CAP) the
preferred landing is `takeoff + round(252 / spacing)`; the base start is a
type/side constant (30, 270, 315, 350 or 320 minutes) plus `wave * 90`
(plus 45 minutes for waves 4..7), then a campaign-RNG jitter of `% 5 + 10`
minutes and a `% 360` phase. `0xc45cc` (strike/interdiction/SEAD) selects
type-specific altitude and speed constants and applies a `% 10` jitter to the
start. Exact takeoff parity therefore also depends on reproducing the full
tasking RNG order, not just the formula.

The generator now builds this timetable: two-minute rows over the eight-hour
turn, takeoff and landing rows reserved, CAP sorties alternating between the
two 252-minute waves, and rows beyond the window omitted except for a takeoff
whose landing falls outside it. It writes 424 rows against the capture's 450.
All 162 CAP takeoffs land at the right hour (81 at 0:00, 81 at 4:xx) and
**150/162 at the exact minute**; each successive CAP sortie is one slot longer
(252/254/256/258 minutes for a two-minute base), which spaces the landings
and leaves the intermediate slots free for the second wave. Strike and interdiction preferred times remain
approximate: they use our wave offset instead of the decoded target-arrival
formula.

### 8.4 Tasking totals

The air tasking totals are invariant. One captured run:

| Role | NATO | Russian | Swedish | Finnish | Total |
|---|---:|---:|---:|---:|---:|
| CAP | 18 | 96 | 24 | 24 | 162 |
| INTC | 5 | 15 | 3 | 3 | 26 |
| AF_KILL | 9 | 6 |  |  | 15 |
| INTERD | 11 | 1 |  |  | 12 |
| REFUELLER_PATROL | 3 | 3 |  |  | 6 |
| SEAD |  | 2 |  |  | 2 |
| AWACS_KILL |  | 1 |  |  | 1 |
| REFUELLER_KILL | 1 | 1 |  |  | 2 |
| AWACS_PATROL | 1 | 1 |  |  | 2 |
| JSTARS_PATROL | 1 |  |  |  | 1 |

Sea supply: 13 convoys, scaled from the six ship groups (one per 30 hulls).
The file order is always all `GBASED`, then all `SHIP`, then all `PLANE`, then
the `BASE` blocks and the `FLAK` box list.

Flight composition per mission:

| Role | Flights | Notes |
|---|---:|---|
| CAP, INTC | 1 | 2-ship intercept |
| AWACS / JSTARS / REFUELLER patrol | 2 | platform plus a 4-ship escort |
| AF_KILL | 2–3 | 4-ship strike, 2-ship wild weasel, optional 4-ship escort |
| INTERD | 1–2 | 4-ship strike, optional escort |
| SEAD | 3 | 6-ship strike, 4-ship SEAD, 4-ship escort |
| AWACS/REFUELLER kill | 1 | 2-ship intercept |

That yields 264–266 `FLIGHT` records for the 226–229 air missions, matching
the captured range. Escorts take whatever the airfield still holds after the
strike and wild-weasel flights, so their size is 1–4 ships rather than always
four; support-package escorts stay at four. The captured mix is 18 four-ship
escorts, two 2-ship, two 1-ship and one 3-ship. The remaining difference is
*which* packages receive an escort: the capture assigns them through its own
package plan, which is not decoded yet.

### 8.5 Timetable

The captured `TIMESLOT` rows describe one **eight-hour campaign turn** (0:00 to
7:59); anything that would land later is not written. The rules are:

* one `TAKE_OFF` and, when it falls inside the turn, one `LANDING` per flight —
  not per mission. The final field is the flight's index inside the package
  (0, 1, 2);
* support and anti-tanker/anti-AWACS packages launch at the top of the turn,
  two minutes apart per flight;
* CAP flights launch in waves about 252 minutes apart; strikes queue behind the
  CAP waves;
* convoys launch in groups of three, fifteen minutes apart, with the next
  convoy on the same base 45 minutes later;
* the timetable is sorted chronologically per base and mixes take-offs and
  landings. `SEA_SUPPLY` take-offs are written as `LAUNCH`.

The generator now follows this structure. Measured row counts on the reference
capture: **453 rows against 455**, with `CAP` 162 take-offs and 102 landings
against 162/103, all 18 support take-offs and landings exact, `SEAD` 6/6,
`SEA_SUPPLY` 39 launches and 6 landings, and the three kill packages at the top
of the turn. The remaining row difference is the strike flight mix (`AF_KILL`
28 against 33, `INTERD` 19 against 17), which follows package composition;
exact strike launch minutes still differ because the original's wave planner is
not decoded.

## 9. Implementation status

The clean-room generator is in the engine:

* `src/mission.h/.cpp` — lossless `MISSION.CFG` reader/writer. **Byte-exact
  round trip** on a full 16,971-line generated file, including the LF-only
  first line and the numeric `FLAK` rows.
* `src/conflict.h/.cpp` — full `CONFLICT.CG`/`CAMPAIGN.CG` parser and writer.
* `src/campaigngen.h/.cpp` — target-table loader, area/base construction,
  defence rings, mobile linking, air-defence tasking, sea and air tasking,
  timeslot schedule, and both writers.
* `src/campaign.h/.cpp` — the exact custom 32-bit additive RNG.
* CLI: `--mission-roundtrip FILE [--mission-write OUT]`, `--conflict FILE
  [--conflict-roundtrip OUT]`, `--campaign-selftest`, `--campaign-generate
  FILE --campaign-state FILE --campaign-out DIR`, `--campaign-seed N`,
  `--campaign-difficulty N`, and `--campaign-accept-dir DIR` which drives the
  GUI Accept path in isolation.
* **GUI wiring.** The Campaign Level dialog's Accept now runs the generator
  from `gpwGenerateCampaign`: it rebuilds both files in the data directory,
  asks the driver to reload the mission data, closes the "Generating
  Campaign..." panel and opens the campaign screen -- the original sequence.

Verified against captured runs. Two comparisons:

| Check | From `CONFLICT.CG` | From a captured previous state |
|---|---|---|
| Bases (side, type, name, position) | 77/77 | 77/77 |
| Areas | 543, identical order | 543, identical order |
| Target values (thousands digit) | **72/72 valued draws** | carried forward |
| Replay parameter | `--campaign-seed <header>` `--campaign-offset 0` | same |
| World status | **0 0 0 2 16 336 exact** | carried forward |
| Ground units | **2519 vs 2519** | carried forward |
| Air-defence missions | 479 vs 476 | 476 vs 476 |
| Air missions | 229 vs 226 | 229 vs 226 |
| Sea supply | 13 vs 13 | 13 vs 13 |
| Flights | 265 vs 263 | 265 vs 263 |
| FLAK boxes | 99 vs 83 | 99 vs 83 |

Group totals now match within two units (Vulcan 75/73, Rapier 59/59,
SA-6 236/236, Patriot 191/193, SA-11 337/337, ZSU 91/89). The second column
uses a state whose units had already been converted, so its larger spread is
the missing attrition pass, not the tasking.

Not yet byte-exact: the per-area order in which the value draws are taken, the
mobile-to-area link table, the turn-to-turn production/attrition mix, patrol
geometry, and the exact area tail order.

## 10. Reconstruction plan

1. **Model and loader.** Parse `CONFLICT.CG`, `CAMPAIGN.CG`, `MISSION.CFG`
   with the existing campaign parser; represent target areas, mobiles,
   pilots, bases, timeslots.
2. **Deterministic backbone.** Emit the 77 bases from the embedded table and
   the fixed aircraft pool; reproduce the timeslot schedule exactly.
3. **RNG.** Implement the 32-bit additive generator and the date/time seed;
   expose a fixed-seed mode for tests.
4. **Randomised layer.** Implement target-value assignment, the
   air-defence site loop and the package policy; compare against captures.
5. **Writers.** Emit `CAMPAIGN.CG` and `MISSION.CFG` in the observed order,
   then byte-compare a fixed-seed run against a captured run.
6. **UI.** Done: Accept raises the `Generating Campaign...` panel, its
   `onopen` hook runs the generator, and the campaign screen follows.

Acceptance: a fixed seed and the shipped scenario reproduce a captured
mission set exactly (modulo the timestamp comment), and the deterministic
counts match the table in §6.
