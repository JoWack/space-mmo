# SpaceMMO

A space MMO in Unreal Engine 5: procedurally generated galaxy, seamless planet
landing, four playable races across two factions, RuneScape-style 1–99 skills, and a
player-driven economy in which every tradeable good was manufactured by a player.

**Status: M0 complete, M1 in progress — 496 tests passing** (398 unit, 98 integration). The
backend economy core is being built first, because it can be fully validated without Unreal,
art, or players.

Implemented in `SpaceMMO.Domain`: the RuneScape XP curve, the `Credits` value type, the
credit-faucet daily cap, tiered ship insurance, cause-based death and loot resolution,
ledger-reason classification, order matching, market fees, fill settlement, and industry
slots, fees, and refunds.

Implemented in `SpaceMMO.Data`: the Postgres schema via EF Core migrations, `InventoryService`
with cost-basis tracking, a `MarketService` that escrows credits and reserves goods and settles
both atomically under `SELECT … FOR UPDATE`, and an `IndustryService` running time-gated
manufacturing jobs.

**M3's economic loop is complete end to end.** Characters can gather raw material from shared
deposits, manufacture it through time-gated jobs with skill and tool gates, and trade the
results — with credits, goods, fees, tax, and price-improvement refunds all settled and
reconciled against the ledger.

The quest engine sits on top: prerequisite chains, per-quest cooldowns, and rewards routed
through the daily faucet cap for repeatables while one-shot story rewards bypass it.

Skills, items, recipes, and the onboarding questline live in `data/` as JSON, validated before
they touch the database and upserted by key so startup is idempotent. **The shipped content is
part of the test suite** — a typo in a recipe fails the build rather than the server.

**EconSim** runs the whole economy headless against `SpaceMMO.Domain` — no database, no HTTP —
simulating a decade in about three seconds. It has already found three things worth knowing;
see [economy-design.md](docs/economy-design.md) §5a.

```bash
dotnet run --project tools/SpaceMMO.EconSim -- 3650
```

```bash
dotnet run --project tools/SpaceMMO.EconSim -- --sweep
```

**M1 is functionally complete. M2 has started.** `client/` is a UE 5.8.1 C++ project whose game
target compiles, containing the three-tier coordinate system from
[ADR-0001](docs/adr/0001-coordinates.md) — galaxy space as `int64`, system space as double
kilometres, and local render space in centimetres near the origin.

```bash
"/d/Programming/UnrealEngine/UE_5.8/Engine/Build/BatchFiles/Build.bat" SpaceMMO Win64 Development -Project="D:\Programming\SpaceMMO\client\SpaceMMO.uproject"
```

Two toolchain items still block parts of M2 — the editor build and dedicated servers. See
[docs/setup.md](docs/setup.md) §2.

## Start here

| Document | What it covers |
|---|---|
| [docs/setup.md](docs/setup.md) | Toolchain install. Everything M1 needs is installed; Unreal and Visual Studio are still pending for M2. |
| [docs/design-bible.md](docs/design-bible.md) | Races, factions, skills, item taxonomy, and the onboarding questline. Content spec; becomes `data/*.json`. |
| [docs/economy-design.md](docs/economy-design.md) | Faucets, sinks, market mechanics, and the invariants EconSim asserts. |
| [docs/adr/](docs/adr/README.md) | The six decisions that are expensive to reverse, and why. |

## Layout

```
docs/          design bible, economy design, ADRs
services/      .NET backend (SpaceMMO.Server.sln)
  SpaceMMO.Domain/        pure game rules — no I/O, no dependencies
  SpaceMMO.Domain.Tests/  xUnit
  SpaceMMO.Data/          EF Core entities, DbContext, migrations, services
  SpaceMMO.Data.Tests/    integration tests — need the Postgres container running
tools/         EconSim — headless economy simulator
client/        UE 5.8 project — SpaceMMOCore holds coordinates and physics grids
data/          item, recipe, and quest definitions as JSON
infra/         docker-compose for local Postgres
```

## Build and test

```bash
docker compose -f infra/docker-compose.yml up -d
```

```bash
dotnet test services/SpaceMMO.Server.sln
```

Start Postgres first — `SpaceMMO.Data.Tests` needs it and fails loudly without it, since a
skipped test that reports success is worse than an obvious failure. For the fast unit-only
loop:

```bash
dotnet test services/SpaceMMO.Domain.Tests
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
