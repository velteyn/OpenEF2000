# Work plan

Working checklist for the current campaign-generator push. `TODO.md` tracks
long-term gaps; this file is updated as each step lands, in the spirit of the
`todowrite` task list that OpenCode v2 removed.

## Current focus — `TIMESLOT` / `TAKE_OFF` schedule

- [x] Measure the gap: the capture uses a real `day hour minute` schedule
      (CAP 252 min, strike 108–126 min, sorties 2 min apart); the generator
      currently writes `hour = mission index` and caps everything at 8 hours.
- [x] Parse all 450 captured `TIMESLOT` rows by mission, base and role
      (the 527 line count includes 77 `NUM_TIMESLOTS` headers).
- [x] Decode the per-base 9-byte row format and the timetable skeleton
      (`0xca990`), the writer (`0xc0b44`) and the reservation helpers
      (`0xc2a80`, `0xc2aac`, `0xc34f0`).
- [ ] Derive the reservation order per role and base.
- [x] Confirm the per-base slot spacing (2 min for the western bases, 1 min
      for the Russian base in the sample) and the takeoff/landing pairing.
- [x] Decode the slot search (`0xc34c8`) and the CAP/strike preferred-time
      constants and RNG jitter (`0xc3bd4`, `0xc45cc`).
- [x] Implement the per-base timetable and reservation: 2-minute rows, 8-hour
      window, CAP alternating waves, takeoff/landing rows reserved.
- [x] CAP parity: 162/162 takeoffs at the right hour, 150/162 at the exact
      minute, 424 rows against 450.
- [ ] Decode the strike target-arrival preferred time; the remaining 12 CAP
      minute offsets are base-specific (the 1-minute Russian base).
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
