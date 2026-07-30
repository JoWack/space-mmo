# SpaceMMO — Economy Design

The economy is the differentiating feature of this game, so it gets designed with the
same rigor as the coordinate system. Two rules are absolute:

1. **Credits are `long`, in minor units.** Never `float`, never `double`, never
   `decimal` on the wire. See [ADR-0005](adr/0005-money-representation.md).
2. **Every credit movement is a ledger entry.** Balances are derived from an
   append-only log, never mutated in place. If the ledger and the balances ever
   disagree, the ledger is right.

---

## 1. All goods are player-made

Per the locked scope decision: item and ship *designs* are a fixed catalog shipped in
`data/`; the entire *supply* of every tradeable good is manufactured by players from
player-gathered raw materials. No NPC vendor ever sells a finished good, and no NPC
ever buys one.

There is exactly one intentional, temporary exception — `crude_thruster`, sold via a
faction supply order so the onboarding chain can complete before `electronics` exists.
It is marked for removal in [design-bible.md](design-bible.md) §5.

**Material creation happens in exactly one place: gathering from resource nodes.** Any
other path by which an item quantity increases is a bug, and EconSim asserts against it.

---

## 2. Where credits come from

**Decision: quest rewards are the credit faucet.** There are no NPC bounties and no
NPC purchases of player goods.

This is a genuinely good choice, because a quest faucet is *bounded and observable* in
a way EVE's NPC-bounty faucet is not. But it splits into two very different regimes,
and only the first is currently designed.

### 2a. Bootstrap faucet — designed

The `main_story` onboarding chain pays **13,000 credits, once per character**
(itemized in [design-bible.md](design-bible.md) §4).

These are recorded as `StoryReward` and are **exempt from the daily cap**. A 5,000-credit
daily budget against a 13,000-credit tutorial would stall a new player's onboarding
across three days, and the game would look broken at the worst possible moment. The
exemption is safe because the faucet is bounded by construction rather than by a budget
— each quest completes once per character.

Keeping it a separate ledger reason from `QuestReward` also lets EconSim measure the
bootstrap and steady-state faucets independently, which are genuinely different things:
one scales with signups, the other with active play.

Total bootstrap supply is therefore exactly:

```
M_bootstrap = 13,000 × (number of characters ever created)
```

This is a hard ceiling, which is a very comfortable property: no amount of player
ingenuity can farm it, because each step completes once per character. Alt-account
farming is the only attack, and it's bounded at 13,000 credits per full playthrough
of a multi-step questline — a terrible hourly rate, which is exactly what you want.

### 2b. Steady-state faucet — rate-limited repeatable sidequests

A one-shot faucet alone is not sustainable. Sinks (§3) *destroy* credits, so with only
a bootstrap faucet the money supply strictly shrinks:

```
dM/dt = 0 − (sink rate)  <  0
```

Over a long-lived server the supply trends toward zero, credits become
hyper-deflationary, hoarding beats investing, and eventually players cannot pay the
fees industry requires. So there is a second, ongoing faucet.

**Decision: repeatable sidequests pay credits, bounded by a per-character daily
credit cap.**

Two independent limiters, which do different jobs:

| Limiter | Mechanism | What it prevents |
|---|---|---|
| **Per-quest cooldown** | Each repeatable quest has `cooldown_seconds` before the same character can take it again | One optimal quest being farmed in a tight loop |
| **Per-character daily cap** | A hard ceiling on credits from *all* faucet sources per character per UTC day | Total faucet rate exceeding the design target, no matter how many sources exist |

First-draft cap: **5,000 credits per character per UTC day.**

**When the cap is reached, quests still grant XP and items — only the credit reward is
withheld.** This matters: a hard wall that makes content worthless past a threshold
teaches players to stop playing at the cap. Withholding only credits keeps the
activity worth doing for progression, which is the behavior to reward.

Reset is a **fixed UTC daily boundary**, not a rolling 24-hour window. Rolling windows
are marginally fairer across timezones and considerably more work to implement,
explain, and debug; players learn a fixed reset quickly.

#### The cap is the single control point for every future faucet

This is the important structural property, and the reason to build it this way now.

The daily cap is enforced against a **faucet ledger** (`character_faucet_daily`), not
against quests specifically. Any future credit source — and there will be others —
routes through the same check:

```
GrantFaucetCredits(character, amount, reason)
    granted = min(amount, dailyCap − alreadyGrantedToday)
```

Adding a new way to earn credits then costs one call and requires **no economic
rebalancing**, because the aggregate per-player rate is already bounded. Without this,
every new faucet is a fresh balance risk and a fresh exploit surface.

Insurance payouts (§3a) are the one deliberate exception, and are accounted separately —
losing a capital ship must not be throttled by a daily budget.

#### The equilibrium condition

```
F  =  credits/player/day from capped faucets   (design target: ≤ 5,000)
S  =  credits/player/day destroyed by sinks

F > S  →  inflation      F < S  →  deflation      F ≈ S  →  stable
```

`F` is now one config value with a hard ceiling, which makes this equation something
EconSim can actually drive: sweep `F`, measure `S`, and read off the stable point.
Tuning the economy becomes a one-parameter search rather than guesswork.

Repeatable sidequests are **not in M1 scope** — the bootstrap faucet is enough to build
and test everything in M1. What *is* in M1 scope is the `GrantFaucetCredits` chokepoint
and the faucet ledger, because retrofitting a chokepoint after several faucets exist
means auditing all of them.

---

## 3. Where credits go

Sinks are what make a faucet safe. Implemented from the start, each with a
`LedgerReason` so EconSim can attribute every credit destroyed or moved:

| Sink | Type | Rationale |
|---|---|---|
| Market broker fee | % of order value, on placement | Discourages order spam; scales with wealth |
| Market sales tax | % of value, on fill | The main volumetric sink |
| Industry job fee | Flat + per-run, on job start | Ties the sink to real production volume |
| Station rent | Recurring, per storage used | Punishes infinite hoarding; makes volume matter |
| Fuel | Consumed on warp and sublight burn | Scales with travel, i.e. with trade activity |
| Insurance premium | % of ship value, on purchase | Credit sink that partially funds payouts (§3a) |
| Repair costs | Materials + credits to restore damaged items | Softer sink than destruction; see §3b |
| **Death destruction** | Items destroyed on death, by cause | **The material sink.** See §3b |

Market fees are the tunable ones — they're proportional to wealth, so they
self-scale as the economy grows. Flat fees do not, and become irrelevant over time.

**Death destruction is the most important sink in the game.** Without a material sink
of comparable scale to the material faucet, every crafted good accumulates forever and
prices fall permanently toward the cost of raw materials. It is what gives all industry
a reason to exist.

---

## 3a. Ship insurance

**Decision: insurance is optional, tiered, and pays a percentage of the ship's
recorded value.**

| Tier | Payout | Premium | Net loss if destroyed | Break-even loss rate |
|---|---|---|---|---|
| None | 0% | — | 100% of value | — |
| Basic | 40% | 6% | 66% | 15% |
| Standard | 60% | 12% | 52% | 20% |
| Premium | 80% | 20% | 40% | 25% |

*Break-even loss rate* is `premium ÷ payout` — the probability of losing the ship above
which the tier pays for itself. Higher tiers require a higher expected loss rate to be
worth buying, so premium insurance is the choice you make before something dangerous
rather than a default. That's the intended shape.

Policies cover **the hull only**. Fitted modules and cargo are resolved by the death
table in §3b, exactly as they would be uninsured. This keeps the rules legible and
matches player expectations from EVE.

### The insurance fraud vector, and how it is closed

This is the failure mode that has to be designed out rather than patched later. EVE
shipped it and spent years rebalancing around it.

The attack: build a ship cheaply, insure it, destroy it deliberately (self-destruct, or
a colluding friend), collect a payout worth more than the ship cost. That is an
unbounded credit printer.

It exists **only when the payout is pegged to a reference price rather than to what the
ship actually cost.** If payout is `r × V_reference` and an efficient industrialist can
build for `V_actual < V_reference`, then `r × V_reference > V_actual` is achievable and
destroying your own ships becomes profitable.

Four rules close it:

1. **Payout is pegged to `acquisition_value`, recorded on the ship instance at
   creation** — the sum of input material value if crafted, or the actual price paid if
   bought. Never a market reference price, never a rolling average.
2. **Payout rate is strictly below 100%.** Then for any loss, net result is
   `r·V − V − P = −V(1−r) − P < 0`. Losing an insured ship is *always* a real loss.
   No exceptions, no top tier at 100%.
3. **Self-destruct voids the policy.** Removes the zero-risk path entirely.
4. **Premiums are non-refundable** and paid up front, so there is no free option.

Collusion is covered by rule 2: a friend destroying your ship still leaves you down
`V(1−r) + P`. There is no arrangement of the rules under which deliberate destruction
profits.

### Adverse selection — the honest caveat

Insurance cannot be made a net *sink*, and it's worth being clear-eyed about why.

Players choose when to insure, so they will insure ships they expect to lose. That is
textbook adverse selection, and a game has no underwriting to defend against it.
Whenever the real loss rate among insured hulls exceeds `premium ÷ payout`, insurance
runs as a net faucet:

```
net faucet  =  V × (payout_rate × ships_lost  −  premium_rate × ships_insured)
```

Combat pilots will clear the break-even rate. So insurance *will* create credits.

This is acceptable, because it is **bounded by production**: players cannot destroy
more ships than they build, so insurance can never mint more than `payout_rate` of the
material value actually being consumed. It is a faucet whose rate is tied to genuine
economic activity — which is the only kind of unbounded-looking faucet that is safe.

The consequences follow directly:

- Insurance payouts are **exempt from the daily faucet cap** (§2b) and accounted on
  their own ledger reason, so they never mask or consume the quest budget.
- EconSim tracks a notional insurance pool: `Σ premiums − Σ payouts`. If it trends
  steeply negative, the lever is the `premium ÷ payout` ratio, and it is the only lever
  needed.
- `premium ÷ payout` is therefore a balance number, not a flavor number. It lives in
  `data/` and is expected to move.

---

## 3b. Death, loot, and destruction

**Decision: what survives a death depends on how the death happened.** A ship that
detonates does not leave a tidy pile of cargo.

Every item in a dead player's possession resolves to one of three outcomes:

| Outcome | Meaning |
|---|---|
| **Survived** | Intact and lootable |
| **Damaged** | Lootable, but at reduced condition — unusable until repaired |
| **Destroyed** | Removed from the game entirely. **This is the material sink.** |

### Death causes

| Cause | Situation |
|---|---|
| `ShipExplosion` | Hull detonation — reactor breach or catastrophic structural failure |
| `ShipDisabled` | Hull integrity lost without detonation; leaves a salvageable wreck |
| `PersonalCombatPlanet` | Killed on foot on a planet surface |
| `PersonalCombatStation` | Killed on foot inside a station |
| `EnvironmentalPlanet` | Fall, atmosphere, temperature — no attacker |
| `SelfDestruct` | Deliberate. Nothing survives, and insurance is void |

### Resolution table

Percentages are `survived / damaged / destroyed` per item category. This table is
`data/death-rules.json`, and it is **the primary calibration surface for the entire
material economy** — these numbers set how fast materials leave the game.

| Category | ShipExplosion | ShipDisabled | PersonalCombatPlanet | PersonalCombatStation | EnvironmentalPlanet |
|---|---|---|---|---|---|
| `raw` | 10 / 0 / 90 | 70 / 0 / 30 | 90 / 0 / 10 | 100 / 0 / 0 | 95 / 0 / 5 |
| `refined` | 10 / 0 / 90 | 70 / 0 / 30 | 90 / 0 / 10 | 100 / 0 / 0 | 95 / 0 / 5 |
| `component` | 5 / 0 / 95 | 60 / 0 / 40 | 90 / 0 / 10 | 100 / 0 / 0 | 95 / 0 / 5 |
| `consumable` | 5 / 0 / 95 | 50 / 0 / 50 | 80 / 0 / 20 | 100 / 0 / 0 | 85 / 0 / 15 |
| `tool` | 10 / 10 / 80 | 50 / 30 / 20 | 70 / 25 / 5 | 100 / 0 / 0 | 60 / 35 / 5 |
| `module` | 5 / 15 / 80 | 40 / 40 / 20 | 80 / 15 / 5 | 100 / 0 / 0 | 85 / 15 / 0 |
| `armor` | 5 / 15 / 80 | 40 / 35 / 25 | **0 / 70 / 30** | 100 / 0 / 0 | 10 / 60 / 30 |
| `weapon` | 5 / 10 / 85 | 45 / 35 / 20 | 60 / 30 / 10 | 100 / 0 / 0 | 70 / 25 / 5 |
| `hull` | 0 / 0 / 100 | 0 / 100 / 0 | 100 / 0 / 0 | 100 / 0 / 0 | 100 / 0 / 0 |

**The four stackable categories always have a zero `damaged` column, and that is
enforced in code, not by convention.** Stackable items are stored as `(item_def, qty)`
pairs with nowhere to record condition, so a damaged outcome for them is
unrepresentable. `DeathRuleTable` validates this at construction — which is how the
first draft of this table was caught assigning components a 10% damage chance the
storage model could not express.

`hull` resolves to *survived* for all on-foot and environmental causes: a ship parked in
orbit is unaffected by its owner being shot on a hillside. `ShipDisabled` leaves the
hull damaged rather than destroyed — the wreck exists, but the frame never flies again
without a rebuild.

Reading the design intent out of it:

- **Explosion destroys ~80–95% of everything.** This is the heavy sink and the reason
  industry has customers. It also matches physical intuition: your cargo was inside the
  thing that exploded.
- **Disabling a ship instead of destroying it preserves most of the cargo.** That's a
  deliberate tactical fork — a pirate who wants loot must disable rather than obliterate,
  which is far harder. Piracy and griefing get different optimal play, which is exactly
  the intent.
- **Armor never survives on-foot planetary combat intact** — 70% of the time it drops
  broken and needs repair, 30% of the time it's gone. Armor is the thing that was being
  shot; it should show it.
- **Station deaths lose nothing.** Stations are safe zones. This gives new players and
  traders somewhere genuinely secure, and keeps the economy's hubs from being
  gank-farms.
- **Environmental deaths are gentle on cargo but hard on armor** — falling off a cliff
  wrecks your suit and leaves the ore in your pack.

### Requirements this places on the schema

Damage-as-an-outcome only means something if items can *be* damaged, which the current
schema cannot express. Two additions:

1. **Item instances.** Non-stackable items (`tool`, `module`, `hull`, `armor`, `weapon`)
   need per-instance rows carrying `condition` (0–100) and `acquisition_value` for
   insurance. Stackable items (`raw`, `refined`, `component`, `consumable`) stay as
   `(item_def, qty)` pairs. `inventory_items` splits accordingly.
2. **Two new item categories** — `armor` and `weapon` — added to the taxonomy in
   [design-bible.md](design-bible.md) §3. The skill list already implies them via
   `armorcrafting` and `weaponcrafting`.

Condition brings a repair loop with it (a repair skill, material costs, and possibly a
condition cap that degrades with each repair so items eventually die anyway). That is a
real subsystem and it is **deferred past M3** — but `condition` goes in the schema now,
because adding a column to a table with live player items is far worse than carrying an
unused one.

### Determinism and dispute resolution

Death resolution is random, but `SpaceMMO.Domain` contains no randomness by design. So
resolution is a **pure function of an explicit seed**:

```
Resolve(loadout, cause, seed) -> per-item outcomes
```

The server draws the seed, records it on the death record, and applies the result. That
buys three things at no cost: the resolver is trivially unit-testable, EconSim can
reproduce destruction runs exactly, and any player dispute about a lost item is
replayable from the log rather than a matter of opinion.

---

## 4. Invariants EconSim asserts

These are the tests that make the economy real rather than hoped-for:

1. **Material conservation.** For every item, `Σ(gathered) + Σ(crafted outputs) −
   Σ(crafted inputs) − Σ(destroyed) == Σ(held)`. Any drift is a dupe bug.
2. **Ledger conservation.** `Σ(all ledger deltas) == Σ(all balances)`, and every
   nonzero delta carries a valid `LedgerReason`.
2b. **Daily faucet cap holds.** No character receives more than the configured daily
    cap in capped-faucet credits on any UTC day, across all faucet sources combined.
    This is the invariant that makes adding future faucets safe.
2c. **Insurance never profits deliberate loss.** For every insured hull destroyed,
    `payout < acquisition_value + premium`. Asserted structurally, not sampled — a
    single counterexample is an exploitable credit printer.
2d. **Insurance pool is tracked.** `Σ premiums − Σ payouts` is reported per run. It is
    expected to be negative (adverse selection, §3a) but must stay bounded by
    `payout_rate × material value destroyed`.
2e. **Death resolution is reproducible.** Re-running the resolver with the same loadout,
    cause, and seed produces byte-identical outcomes.
3. **Faucet attribution.** Total credits created equals the sum of quest reward
   payouts. Nothing else may create credits.
4. **No free-money loop.** No cycle of craft/refine/trade operations returns more
   credits or materials than it consumed. Checked by searching the recipe graph for
   negative-cost cycles, not just by simulation.
5. **Price convergence.** Over a long run, per-item prices settle into a band rather
   than diverging or collapsing to zero.
6. **Money supply tracks the model.** Measured `M(t)` matches `M_bootstrap` plus
   repeatable faucet minus attributed sinks, within rounding.

Invariants 1–4 are correctness and must never fail. 5 and 6 are balance signals —
they're expected to fail early and guide tuning.

---

## 5. Market mechanics

Per-station order books; there is no global market. Regional price differences are
the point, and hauling between them is a profession.

**Buy orders lock credits at placement; sell orders reserve goods.** An order on the
book therefore always represents money or material that exists and has been committed,
and can never fail to honour itself. The cost is that capital and cargo are tied up
while an order rests — the intended tradeoff, since a book full of orders nobody can pay
for is worse than a book that reflects real commitments.

Two consequences follow:

- **Money supply is `Σ balances + Σ escrow on open orders`.** Escrowed credits have left
  the buyer's balance but still exist; they live on the order until paid to a seller,
  destroyed as tax, or released on cancellation.
- **Escrow is locked at the buyer's *limit* price, while fills execute at the *resting*
  price.** The difference must be refunded, or a buyer who bid 150 and filled at 100
  would silently lose 50 per unit and those credits would vanish from the economy. When
  the buyer is the resting side the two prices are equal, so the refund falls out as
  zero and one formula covers both cases.

Mechanics:

- Limit orders only, both sides. `side ∈ {buy, sell}`.
- **Price-time priority**: best price first, oldest first at equal price.
- Immediate match on crossing, with partial fills supported.
- Orders expire; `expires_at` is mandatory, so abandoned orders self-clean.
- A fill is **one database transaction** with `SELECT … FOR UPDATE` on both order
  rows. Partial fills and two buyers hitting one sell order concurrently are the two
  bugs that dupe items or money, so both get dedicated tests.

Trades write to an append-only `trades` table. That table is both the audit trail and
the price-history source for market UI, so it's never pruned.

---

## 5a. What EconSim actually found

First real run: 68 bots, 10 simulated years, ~3 seconds. All conservation invariants held
every simulated day. Three findings, in order of how much they matter.

### The material sink gap is severe, and now quantified

| | Created | Destroyed | Held |
|---|---|---|---|
| `ferrite_ore` | 1,397,568,000 | 56,240 | 1,397,511,760 |

**Ore price collapsed to 0 cr.** Gathering throughput outruns industry consumption by roughly
four orders of magnitude, and with no ship destruction there is nothing to consume the
surplus. This is §3's warning — "without a material sink of comparable scale to the material
faucet, every crafted good accumulates forever" — as a number rather than a prediction.

The binding constraint is the deposit ceiling: `capacity × nodes × 86400 / respawn`, which is
432,000 ore/day at the current content values. Forty maxed miners can extract 383,000 of it.
**Node respawn time is the material faucet's throttle**, and it is currently set far too
generously relative to any sink that exists.

### The steady-state faucet equilibrium is ~50 cr/day, not 5,000

Sweeping `DailyQuestCredits` over a 5-year run:

| cr/day/character | Money supply vs bootstrap |
|---|---|
| 0 | 6.4% — the deflationary spiral |
| 25 | 52.2% |
| **50** | **98.3% — equilibrium** |
| 100 | 206.3% |
| 250 | 1,616.2% |

At zero the supply drains to 6% of what was ever created and the market seizes, exactly as
§2b predicted. Equilibrium `F ≈ S` lands near **50 credits per character per day**.

The first-draft daily cap of 5,000 is therefore about **100× above equilibrium**. A cap that
high would never bind on normal play and offers essentially no protection — it is a ceiling
placed above the roof. Either the cap comes down toward the same order of magnitude as
equilibrium, or it should be understood as an anti-abuse backstop only, with the real control
being the per-quest reward values.

### Broker fees are 97% of all credit destruction

| Sink | Share |
|---|---|
| Broker fee | 811,798 cr (97%) |
| Industry fee | 16,465 cr |
| Sales tax | 7,683 cr |

Sales tax was designed as "the main volumetric sink" (§3) and is doing almost nothing, because
it only applies to *filled* orders while the broker fee applies to every *placed* one. In a
market where much of what is listed never sells, that makes participation itself the tax.

Worth treating as provisional: bot order-placement behaviour drives this directly, and real
players place fewer, better-judged orders. But the structural point stands — a fee on placement
and a fee on execution have very different incidences, and the current rates put nearly all the
weight on the wrong one.

### Caveats

These numbers assume no ship destruction, no station rent, and no fuel, because none of those
exist yet. All three are credit *and* material sinks, so equilibrium `F` will rise as they land.
The tool is in the repository so the sweep can simply be re-run.

---

## 6. First-draft price targets

Anchored to the 13,000-credit bootstrap so early prices are sane relative to the only
money a new player has. All values are first-draft and expected to move once EconSim
runs — they live in `data/` so tuning them is not a deploy.

| Item | Target price | Reasoning |
|---|---|---|
| `scrap_alloy` | ~15 cr | Hand-gatherable by anyone; near-zero barrier, so near-zero price |
| `ferrite_ore` | ~40 cr | Tool-gated, so it carries the tool's cost |
| `ferrite_plate` | ~250 cr | 5 ore per plate plus job time and fees |
| `crude_mining_laser` | ~200 cr | Competes with 8 self-gathered scrap; must be cheap enough that buying is reasonable and dear enough that crafting pays |
| `shuttle_hull_section` | ~1,400 cr | 4 plates plus 5 min of job time and a level-5 gate |
| `crude_thruster` | 800 cr | Fixed faction supply price; the one non-player-made good |
| `hull_shuttle` | ~3,500 cr | Roughly 27% of bootstrap credits — affordable without questing, but the questline is clearly the better path |

The design intent in that last row: a new player *can* buy a shuttle outright with
quest money, but building one is cheaper. That makes the tutorial teach production
rather than consumption, which is the behavior a player-driven economy needs.
