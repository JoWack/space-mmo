# SpaceMMO

A space MMO in Unreal Engine 5: procedurally generated galaxy, seamless planet
landing, four playable races across two factions, RuneScape-style 1–99 skills, and a
player-driven economy in which every tradeable good was manufactured by a player.

**Status: M0–M3 complete, M4 next — 798 tests passing** (161 client, 637 backend). The backend
economy core was built first, because it can be fully validated without Unreal, art, or players;
the client now closes the loop on top of it.

One thing M3 is owed: the single-player walk and the two-player trade were both confirmed by hand
*before* mining became tool-gated and before crafted tools were visible. The parts are individually
tested and the loop has not been walked again since, which `docs/tasks.md` records rather than
implies.

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

`client/` is a UE 5.8.1 C++ project built against a **source** engine, containing the three-tier
coordinate system from [ADR-0001](docs/adr/0001-coordinates.md) — galaxy space as `int64`, system
space as double kilometres, and local render space in centimetres near the origin.

```bash
cd /d/Programming/UnrealEngineSource && ./Engine/Build/BatchFiles/Build.bat SpaceMMOEditor Win64 Development -Project="D:\Programming\SpaceMMO\client\SpaceMMO.uproject" -WaitMutex -NoUBA
```

The source engine is not optional: once `BuildCookRun` has produced a dedicated server, the
project's binaries are compiled against `UnrealEngineSource` and the launcher engine can no longer
load them. `Build.bat` also **exits 0 when it has failed** — read the tail for `Result:` rather than
trusting the exit code.

Local physics grids sit on top: nested frames (ship interior → ship → planet → system) that
resolve to system space and produce render transforms relative to whichever frame is active. The
active frame always resolves to identity, which is what keeps Chaos simulating near the origin —
and what makes walking around inside a moving ship an ordinary problem rather than a hard one.

Flight is a pure 6DOF model: thrust applied in the ship's frame, velocity held in the system
frame. That separation is what makes a ship keep drifting the way it was going while it turns to
face somewhere else — most of what makes space flight feel like space flight rather than driving.
Air resistance opposes it near a planet, so ships are fast in space and slow in atmosphere: terminal
speed at sea level is 200 m/s against 2 km/s in vacuum, deliberately under the 443 m/s that would
put a ship into orbit and stop it being flyable along the ground at all.

A flyable `ASpaceMMOShipPawn` holds its position in system-space kilometres and derives its
Unreal transform from it, moving the render origin whenever it drifts past the physics budget.
Unreal's transform is a *view* of the position, not the truth of it — so the ship can fly
arbitrarily far without single-precision rendering falling apart.

**Run `scripts\play.bat`.** WASD thrusts, mouse pitches and yaws, Q/E rolls, Space/Ctrl move up and
down, Shift boosts, C toggles third and first person, E gathers, and a panel shows position, speed,
holdings, quests, market, industry and whatever deposit is within reach. Run `scriptspi.bat`
first, or the planet has no ore on it. A black window at startup is shader compilation rather than a
hang.

The scene is spawned from code — there are no authored assets yet. 352 marker cubes sit at
known positions in **system** space, so they are a live demonstration of the coordinate model
rather than scenery: they stay exactly where they are while the ship moves and the render origin
jumps beneath them. Their instances are rebuilt only when the origin actually moves, so a
rebasing bug would show up immediately as markers jumping.

A planet sits 60 km away: 20 km radius, Earth-like surface gravity, a 12 km atmosphere. Fly toward
it and the readout moves from ORBIT through ATMOSPHERE to SURFACE, with gravity pulling
inverse-square the whole way — then land on it and walk around. **The ground is a height function**
(ADR-0002), not a heightmap: a pure function of direction, evaluated identically by the client and
by the dedicated server, which is why contact is resolved against the function rather than against a
mesh the server does not have. The client tessellates it twice — a whole globe at a vertex every
331 m, and a detailed patch under the viewer that replaces it on approach.

**161 client automation tests pass**, covering coordinates, physics grids, flight, navigation,
planets, terrain, patches, ground contact, walking, boarding, netcode, the backend protocol and the
HUD panels:

```bash
"/d/Programming/UnrealEngineSource/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" "D:\Programming\SpaceMMO\client\SpaceMMO.uproject" -ExecCmds="Automation RunTests SpaceMMO" -testexit="Automation Test Queue Empty" -unattended -nopause -nosplash -log
```

`-testexit` is required: putting `Quit` in `-ExecCmds` exits as soon as tests are *queued*, and
reports success having run nothing.

## Start here

| Document | What it covers |
|---|---|
| [docs/setup.md](docs/setup.md) | Toolchain state and the UE flag pitfalls that cost real debugging time. |
| [docs/design-bible.md](docs/design-bible.md) | Races, factions, skills, item taxonomy, and the onboarding questline. Content spec; becomes `data/*.json`. |
| [docs/economy-design.md](docs/economy-design.md) | Faucets, sinks, market mechanics, and the invariants EconSim asserts. |
| [docs/tasks.md](docs/tasks.md) | The backlog, and what has been ruled out of each open question. Read this first when picking work up. |
| [docs/adr/](docs/adr/README.md) | The eleven decisions that are expensive to reverse, and why. |

## Layout

```
docs/          design bible, economy design, ADRs, tasks
services/      .NET backend (SpaceMMO.Server.sln)
  SpaceMMO.Domain/        pure game rules — no I/O, no dependencies
  SpaceMMO.Domain.Tests/  xUnit
  SpaceMMO.Data/          EF Core entities, DbContext, migrations, services
  SpaceMMO.Data.Tests/    integration tests — need the Postgres container running
  SpaceMMO.Api/           the HTTP surface, and the only thing that seeds content
  SpaceMMO.Api.Tests/     integration tests over real requests
tools/         EconSim — headless economy simulator
client/        UE 5.8 project
  SpaceMMOCore/           coordinates, physics grids, flight, terrain, walking
  SpaceMMOBackend/        talking to the API, deposits, stations, docking, the HUD
data/          skill, item, recipe, quest and universe definitions as JSON
scripts/       play, host, join, seed and the guard that refuses a stale server
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

**`--seed` is the only thing that applies content and migrations.** Editing anything under `data/`
and restarting the API does nothing; startup deliberately does not migrate, so a restart cannot
rewrite a production database.

```bash
dotnet run --project services/SpaceMMO.Api -- --seed
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
- **M1** — backend economy core: schema, XP curve, order book, quest engine, EconSim ✅
- **M2** — UE vertical slice: coordinates ✅, physics grids ✅, flight ✅, ship pawn ✅,
  planet approach ✅, dedicated server ✅, replicated flight ✅, terrain ✅, walking ✅
- **M3** — closing the loop ✅: single-player loop walked end to end ✅, two players trading a
  player-made item ✅, mining tool-gated ✅, skills awarded across the loop ✅, non-stacking items
  visible ✅, deposits say what they need ✅
- **M4** — goods that move and gear that matters: items between inventories, ship holds, hauling
  planet-locked materials by flying them, equippable tools, weapons and armour
- **M5** — combat, and the rules that already assume it: personal and ship weapons, the
  `combat` and `pilot` skill trees, security zones and pairwise aggression
  ([ADR-0008](docs/adr/0008-factions-pvp-and-markets.md),
  [ADR-0009](docs/adr/0009-retaliation.md)), and death and insurance made real
  ([ADR-0006](docs/adr/0006-death-and-insurance.md))
- **M6** — depth: careers, repeatable quest content, the repair loop, caves
  ([ADR-0011](docs/adr/0011-caves-are-authored-volumes.md))

Two corrections worth keeping, because both were wrong for a long time without anybody noticing.

**M4 used to read "universe scale: procedural galaxy, warp handoff, careers, security zones".**
[ADR-0007](docs/adr/0007-one-handcrafted-system.md) deleted the first two outright — it calls them
"the entire M4 technical spine, removed at a cost of zero written code" — and the line was never
updated, so the roadmap promised a galaxy the ADRs had already cancelled.

**There was no combat milestone at all**, while [design-bible.md](docs/design-bible.md) §2 defines
eight combat and pilot skills, defers `constitution` and `stamina` XP "to the combat milestone", and
three ADRs describe rules — who may shoot whom, what a security zone means, what dying costs — that
have nothing to shoot with. The roadmap has to name every milestone the design documents assume, or
work disappears between them.
