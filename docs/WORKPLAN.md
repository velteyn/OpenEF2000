# Work plan

Working checklist for the current campaign-generator push. `TODO.md` tracks
long-term gaps; this file is updated as each step lands, in the spirit of the
`todowrite` task list that OpenCode v2 removed.

## Current focus — `TIMESLOT` / `TAKE_OFF` schedule

- [x] Measure the gap: the capture uses a real `day hour minute` schedule
      (CAP 252 min, strike 108–126 min, sorties 2 min apart); the generator
      currently writes `hour = mission index` and caps everything at 8 hours.
- [ ] Parse all 527 captured `TIMESLOT` rows by mission, base and role.
- [ ] Derive the package wave start per base and role.
- [ ] Confirm the sortie spacing and the per-role duration table.
- [ ] Determine the timetable window (24 hours?) and its tie to the campaign
      clock (`0 0 0 2 16 336`).
- [ ] Implement the scheduler in `src/campaigngen.cpp`.
- [ ] Re-measure the takeoff distribution and update the parity tables.

## Parked

- [ ] Ground edge cases: class 63 (EWR, 29/31), stations 192/194/195 and one
      class-101 town.
- [ ] Plane mission counts: `AF_KILL`/`INTERD` 229 vs 226.
- [ ] Exact corridor identity (RNG stream position).
- [ ] Low target-value component (982–1000).
- [ ] Victory/defeat target-class sets.
- [ ] Attrition/production between turns.
