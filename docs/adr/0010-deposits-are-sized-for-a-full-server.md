# ADR-0010 — Deposits are sized for a full server, so raw ore is worthless until there is one

**Status:** Accepted · 2026-08-10 · records a premise
[ADR-0008](0008-factions-pvp-and-markets.md)'s market rests on and nobody had written down

## Context

EconSim has reported the same thing since its first run: raw ore is created several
orders of magnitude faster than it is consumed, and its price sits on the floor. It
reads as the most alarming number in the report, and it has been treated as a balance
bug three separate times.

Measuring it rather than arguing about it ruled out every candidate cause:

- **Simulator error.** Real, and fixed. `SimulationConfig` assumed thirty deposits of
  every ore where `data/universe/origin.json` authors two ferrite nodes and one node
  per planet-locked ore — a fifteen- and sixtyfold overstatement. Correcting it moved
  the planet-locked ores from 66 to 235 credits. It did not move ferrite at all.
- **Population mix.** `--sweep population` moved miners from 40 down to 4 while pilots
  rose from 25 to 75. Ore created stayed at 52.5 million from 40 miners down to 12 and
  was still 23 million at four; the price never left 0.01 at any ratio.
- **Missing material sink.** `--sweep loss` at 25% per day — every ship destroyed every
  four days — reached 1.5 million units consumed against 52.5 million created. The
  price did not move.

The arithmetic explains why none of them are levers. Two ferrite nodes at 200 units on
a twenty-minute respawn yield 28,800 units per day. One level-99 miner produces about
3,200. So roughly **nine dedicated miners saturate the entire server's supply**, and
the tenth changes nothing — the ceiling binds long before the miners do. On the other
side, twenty-five pilots losing ships at 5% per day consume about a hundred units a
day. Even at seventy-five pilots it is 273.

A deposit's yield is a **per-server rate**. Demand is **per-player**. At a hundred
players those two numbers are about a hundredfold apart, and no arrangement of a
hundred players closes it.

## Decision

**Deposits are sized for a populated server, and the current floor price is the correct
answer to a nearly empty one.** We are not shrinking node yields to make the chart look
healthy.

Concretely:

- `data/universe/origin.json` keeps its authored deposit yields.
- A raw material trading at the floor is **not** a balance finding while the simulated
  population is around a hundred. It is the expected result of server-scale supply
  meeting player-scale demand.
- EconSim's price index for raw materials is therefore not a signal to act on. Its
  manufactured-goods prices, its conservation invariants, and its cross-faction demand
  check are.

## Consequences

The uncomfortable one first: **this decision cannot be validated at the scale we can
currently simulate.** It predicts that ore prices recover as population grows, and
nothing available today tests that. It is a premise, recorded so that it is visible
rather than assumed, and it should be revisited the first time real concurrent players
exist in any number.

Rejected alternatives, and why:

- **Scale deposits to the population we actually expect near-term.** Tempting, and it
  would produce a healthy-looking ore market immediately. It also bakes a hundred-bot
  artefact into shipped content, and every value would have to be raised again — during
  live play, with players who had learned the old rates — as soon as the population
  grew. Tuning content against a simulation's population rather than the game's is the
  specific mistake this ADR exists to avoid.
- **Make yield scale with concurrent population.** Defensible, and probably where this
  ends up if deposits ever become genuinely contended. It is a server-side design
  change rather than a data edit, it makes a node's behaviour depend on who else is
  logged in, and it is not worth building before there is anybody to contend with.

What follows from accepting it:

- Nobody should "fix" the ore price again. Three sessions have now started down that
  road; the elimination record is in task #90 and summarised above.
- Gathering must stay worth doing for reasons other than the sale price of raw ore —
  XP, and feeding one's own crafting. It already does, but that is now load-bearing
  rather than incidental.
- **This is a sequencing statement about the whole economy.** ADR-0008 assumes a
  player-driven market in which every tradeable good was manufactured by a player. That
  market cannot form at a hundred players, and its absence in EconSim is not evidence
  against the design.
- If a future EconSim run reports a raw material price that is *not* on the floor, that
  is worth reading closely — under this decision it means demand has reached
  server-scale supply, which would be the first sign the premise has flipped.

## Notes

The three sweeps behind this are reproducible:

```
dotnet run --project tools/SpaceMMO.EconSim -- --sweep population
dotnet run --project tools/SpaceMMO.EconSim -- --sweep loss
dotnet run --project tools/SpaceMMO.EconSim -- --sweep capital
```
