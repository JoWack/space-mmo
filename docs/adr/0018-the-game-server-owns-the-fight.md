# ADR-0018 — The game server owns the fight; the backend owns what it costs

**Status:** Accepted · 2026-11-06 · drafted by Claude on 26 October at Joe's request (task 202) · amends
[ADR-0003](0003-topology.md)'s third point, narrowing "persistence and authority in the backend" from every
combat action to wealth

## Context

ADR-0003 put persistence and economy in the backend so that "a UE server crash must never be able to destroy or
duplicate a player's assets", and accepted that "every economy action costs a round trip from UE server to backend".
When combat was built (task 194), that rule was applied to every hit: the game server traces the shot, then asks the
API, and the API decides in one Postgres transaction whether the attack was allowed, what it did, the target's health,
the shooter's XP and the attack record. Only then does anybody see damage.

That is stricter than the industry, and the cost has been measured three times:

- **19 October, Postgres in Docker:** the server waited **958 ms** for a hit, typically, and 2.3 s at p90 (198).
- **25 October, Postgres on Windows:** **415 ms** typically, 857 ms at p90. Each of a hit's eight round trips cost
  17 ms under the load of two clients and a server on one machine (202).
- **6 November:** eight round trips made five (202's option 1), and fought once: the API's part of a warm hit
  155 ms typically, the server's wait 356 ms. More than half of that wait is now outside the API.

Meanwhile the bolt comes back from the server **136 ms** after the click typically, and 297 ms at p90 (26 shots, 6
November; 198 had about 50 ms, on a machine doing less). The damage follows **517 ms** after the click for a warm hit,
and 3.7 s for the first. Everything between the bolt and the damage is the backend, and waiting to read its answer.
Joe, after 198: "it should be as instantaneous as possible for smooth gameplay"; and on 26 October, asked whether
drawing the hit at once and letting the API correct it was the industry standard: "I would expect that we're following
the industry standard and doing something tried and true."

**What is tried and true** (Claude's account of common practice in online action games and MMOs, not something
measured here). The server simulating the area owns the fight in memory, meaning health, cooldowns and who is fighting
whom. It decides a hit and replicates the result within its own tick, so the shooter sees it one network round trip
after firing whatever the database is doing. Clients predict only their own cosmetics, as 198's bolt does. The
database is written behind: the fight's state on a timer, at logout and on death. What is wealth (items, currency,
trades, what a death takes) is written in a transaction when it happens. So a server crash can cost a few minutes of
play, the "rollback" MMO players know, and never duplicates anything that was written in a transaction.

**ADR-0003's concern is wealth, and health is not wealth.** A UE crash that loses two seconds of a fight loses nobody
an item. A crash that loses a death's costs, or applies them twice, does. That line is where this ADR redraws the
boundary.

**This project already does it once.** The game server steps the walk model, counts the stamina spent and reports it
every 30 s, and the API awards the XP (task 101).

## Decision

Proposed by Claude and accepted by Joe on 6 November: "yes, I accept ADR-0018", answering a question that also asked
whether (a) was his answer to point 6. So (a) stands below, and is the line to change if that reads too much into it;
nothing is built on it yet.

### 1. The game server decides every hit and applies it at once

It already traces the shot and knows who was hit, where and from which side. It now also:

- **judges whether the attack is allowed** (ADR-0008 and ADR-0009: faction space, protection, retaliation; point 6
  says how);
- **works out the damage** from the weapon it holds, the zone and the facing (ADR-0017's multipliers);
- **takes it off the health it holds** for that character, and replicates the result. Health already replicates,
  and the client already mirrors the API's
  regeneration rule (`HealthAt`, task 194). The shooter sees the number in the server's next update to them after the
  trace.

The client decides nothing it did not decide before: aim and the press, as today. The trace stays the server's.

### 2. The backend still owns everything that is wealth, or outlives the fight

| Owned in memory by the game server, while the character is in its world | Owned by the API, in transactions |
|---|---|
| Health and the last moment of combat | Items, credits, the ledger |
| Who has attacked whom, for the retaliation window | XP: the weapon's, its siblings' (ADR-0015), constitution's third (ADR-0017) |
| The weapon in hand, its damage, cooldown and range | What a death costs (ADR-0016): materials lost, dropped and destroyed, and wear |
| Whether each character is protected | The death itself, and where they come back |

### 3. The fight is reported behind, in batches

- **Every 2 s while anyone is fighting**, the game server sends the API one report covering everything since the
  last: each hit's attacker, target, weapon skill, damage dealt and time, plus each fighter's health and last moment of
  combat as they stand. The API awards the XP, writes the attack records and stores health, in one transaction.
- **Also at once when a character leaves the world** (logout or disconnect), so the API's health is never older
  than their last moment there, and the next join starts from it.
- **The answer carries what changed**, such as a constitution level that raises the maximum health, as the stamina
  report's answer carries the pool's new size today.

Two seconds is what a crash may cost: XP and attack records not yet reported, and health back to its last report for
characters who are reconnecting to a restarted server anyway. It is a line to change.

### 4. A death is reported at once, by itself

When the game server takes someone to 0, it plays the death as it does now and immediately reports the death, the
killer and where it happened. The API records it and takes what it costs (task 103's `DeathCostService`), in one
transaction, and from then on knows the character is dead, so respawning works as now. A death is wealth, so it never
waits for a batch.

If the game server crashes before the API answers, the death is either written whole or not at all. If not, the
character keeps what they carried. Nothing can be created that way, only not taken.

### 5. Every report is applied once

A report the API applied, whose answer was lost, will be sent again, and a write-behind system that is careless
about that is exactly how MMOs have duplicated things. So **every report carries an id the game server makes**. The
API records the ids it has applied, in the same transaction as the report, and answers a repeat with the first answer
without applying it again. The stamina report does not do this today: it takes back a report whose answer failed and
sends it again, so a lost answer is awarded twice (task 203, a small instance of the same thing, to be fixed the same
way).

### 6. Who may attack whom: the rule ported to C++

Today the C# `Pvp.CanAttack` decides, from the faction space, protection and the pair's attack record. The game
server needs the answer within its tick. **(a) is decided**, against (b) and (c):

- **(a) The rule ported to C++, kept identical by shared cases.** `Pvp.CanAttack`, `FactionSpace`'s zones and
  territory, and `Hits.Damage` are about 360 lines of pure C#, comments included. Ported, they run in the server's tick. The C# suite
  writes a file of cases (positions, factions, protection, attack times, and the answer) from the real faction
  space, and the client suite reads it and must agree, so neither copy can drift without a test going red. The C#
  stays the reference, where the rules are designed and argued. Protection only ever ends, so a server that learns
  late that someone finished the main story protects them a little too long, never too short. It is read on joining
  and on leaving a station, where quests are turned in.
- **(b) Ask the API once per pair, and hold the answer.** Only one rule set, but the first shot at anyone new waits
  for a round trip. The held answer goes stale the moment either of them fires, because retaliation is what firing
  changes, so the server would have to apply retaliation itself anyway: a partial port with a cache in front of it.
- **(c) Keep the API judging every hit, without waiting.** The hit is shown at once and the API confirms or refuses
  it afterwards: 202's option 3, with its corrections, a hit drawn and then taken back, kept. It also keeps a
  database transaction per hit on the API.

### 7. Built in stages, each playable

1. The C++ rules and their shared cases (task 204).
2. The game server decides and applies hits, and sends batched reports with ids; the API gains the report endpoint
   and the table of applied ids. `POST /combat/hits` keeps working until the switch, so either path can be fought
   (task 205).
3. Deaths reported at once (task 206).
4. `POST /combat/hits` and `CombatService.HitAsync` retired. The C# rules stay, as the reference and for the API's own
   uses (task 207).

Each lands in M6, where combat already is.

## Consequences

Positive:

- **The damage shows when the bolt does**: 136 ms after the click typically, and 297 ms at p90, in the fight of 6
  November, against 517 ms for a warm hit and 3.7 s for the first. The backend's cost leaves the fight entirely,
  however many fight at once.
- **The API's load stops scaling with the rate of fire.** A hit was a five-round-trip transaction; a fight becomes one
  small transaction every 2 s, and one per death.
- **ADR-0003's guarantee still holds where it matters.** UE never owns an item or a credit, a death's costs are one
  transaction, and with report ids nothing is applied twice.
- Lag compensation, rewinding targets to where the shooter saw them, becomes possible later, because the decision is
  made where the history of positions is. Not decided here.

Negative, and accepted:

- **It does not steady the game server's own frames.** A hit can show no sooner than the server's next update to the
  shooter, and in the fight of 6 November those frames stalled 100–525 ms, 27 times in 45 s of shooting (task 208).
  Some of that may be the backend competing for the same cores, which this removes; the rest is separate work, and
  the bolt's 136 ms is a floor only until that is done.
- **Combat rules in two languages.** With (a), every change to who may shoot whom, or to damage, is made twice, and
  the shared cases are what keep the copies equal. A rule changed in C# without its cases being regenerated is the
  failure to watch for.
- **The API's view of health is up to 2 s old** while a character is in the world. Anything the API decides from it
  must allow for that, or ask the server. Today that is only respawning, which a death's own report keeps right; a
  character joining reads what their leaving wrote.
- **A server crash loses up to 2 s of a fight** for everyone in it: some XP, and health back to its last report.
  This is the deliberate trade; the old path lost nothing and cost 415 ms a hit.
- **Retaliation state lives in two places**: the server's memory for the fight, and the API's records for later.
  The server loads a character's recent records when they join, so leaving and coming back does not wipe a licence
  to answer.
- **More moving parts on the game server**: a report queue, retries, ids. It is the same shape as the stamina report,
  once that is fixed (task 203).

## Alternatives considered

**Keep every hit a transaction and make it cheaper** (202's options 1 and 2). Both done, and worth doing, but the floor
is the database's round trips under load: 415 ms at p50 after moving Postgres out of Docker. Nothing done to the
database brings that down to a tick. This is what runs until this ADR lands.

**Draw the hit at once and let the API confirm or correct it** (202's option 3, point 6's (c)). Shows damage as fast
as this ADR does, but a refused hit is drawn and then taken back, a kill can be shown that the API does not grant,
and every hit is still a transaction on the API. A halfway house that only exists because of the per-hit transaction.

**Move combat persistence into UE as well.** Rejected for ADR-0003's reasons, which stand: UE writing deaths' costs
or XP to its own storage makes a crash or an exploit into duplication or loss. This ADR moves only what is not wealth.

**Let the shooter's client decide hits** ("favour the shooter" by trusting the client). Rejected: clients lie, and
every hit in this game moves XP and can end in a death that costs materials. Making it feel fair to the shooter is
lag compensation's job, on the server.
