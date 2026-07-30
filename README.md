# SpaceMMO

A space MMO in Unreal Engine 5: procedurally generated galaxy, seamless planet
landing, four playable races across two factions, RuneScape-style 1–99 skills, and a
player-driven economy in which every tradeable good was manufactured by a player.

**Status: M0 complete, M1 in progress — 176 tests passing.** The backend economy core is
being built first, because it can be fully validated without Unreal, art, or players.

Implemented so far in `SpaceMMO.Domain`: the RuneScape XP curve, the `Credits` value
type, the credit-faucet daily cap, tiered ship insurance, and cause-based death and loot
resolution. Still to come in M1: the Postgres schema, the order-book matching engine, the
quest engine, and EconSim.

## Start here

| Document | What it covers |
|---|---|
| [docs/setup.md](docs/setup.md) | Toolchain install. **Read this first** — the .NET SDK is not yet installed, so nothing builds until it is. |
| [docs/design-bible.md](docs/design-bible.md) | Races, factions, skills, item taxonomy, and the onboarding questline. Content spec; becomes `data/*.json`. |
| [docs/economy-design.md](docs/economy-design.md) | Faucets, sinks, market mechanics, and the invariants EconSim asserts. |
| [docs/adr/](docs/adr/README.md) | The five decisions that are expensive to reverse, and why. |

## Layout

```
docs/          design bible, economy design, ADRs
services/      .NET backend (SpaceMMO.Server.sln)
  SpaceMMO.Domain/        pure game rules — no I/O, no dependencies
  SpaceMMO.Domain.Tests/  xUnit
tools/         EconSim — headless economy simulator
client/        UE5 project (M2)
data/          item, recipe, and quest definitions as JSON
infra/         docker-compose for local Postgres
```

## Build and test

```bash
dotnet test services/SpaceMMO.Server.sln
```

```bash
docker compose -f infra/docker-compose.yml up -d
```

## The three constraints worth knowing before you touch anything

1. **`SpaceMMO.Domain` has no I/O.** No database, no HTTP, no clock, no randomness —
   pure functions over plain types. This is what lets EconSim simulate tens of
   thousands of days in seconds. If a rule appears to need I/O, the caller does the
   I/O and passes the result in.

2. **Credits are `long` minor units on an append-only ledger.** Never `float`, never
   `double`. Balances are derived from the ledger, and when they disagree, the ledger
   wins. See [ADR-0005](docs/adr/0005-money-representation.md).

3. **Server-authoritative for anything with value.** Mining, XP, inventory, crafting,
   and market orders are re-simulated server-side. The client sends *intent* and never
   reports outcomes. A player-driven economy makes every exploit a money printer.

## Roadmap

- **M0** — repo, docs, ADRs, toolchain ✅
- **M1** — backend economy core: schema, XP curve, order book, quest engine, EconSim
- **M2** — UE vertical slice: coordinates, flight, one landable planet, dedicated server
- **M3** — closing the loop: mine → craft → sell, two players trading a player-made item
- **M4** — universe scale: procedural galaxy, warp handoff, careers, security zones
