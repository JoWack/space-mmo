# ADR-0009 — Aggression is pairwise, and faction space is defended rather than hunted

**Status:** Accepted · 2026-08-06 · amends the PvP rule of
[ADR-0008](0008-factions-pvp-and-markets.md), whose market and material decisions stand

## Context

ADR-0008 said faction space allowed "PvP against the opposing faction, in their half
only" and asserted the whole rule was a pure function of two factions, a position, and
a protection flag — "no regions table, no zone actors, no new state."

Implementing it exposed that the sentence has two readings, and they are different
games. Either the locals may engage an intruder, or the intruder may engage the
locals; the table does not say which, and whichever is chosen, the other party has no
recourse. A rule under which a raider may shoot but the defender may not is obviously
wrong; the mirror image, where a raider may be shot but cannot shoot back, is no
better.

The reading that survives contact is neither: **defence initiates, and anyone who is
shot may answer.** That cannot be expressed without knowing who has shot whom, which
is the state ADR-0008 said would not be needed.

## Decision

### Faction space belongs to its owners

Beyond the contested approach, in the half belonging to a faction:

- Members of the owning faction may attack an intruder on sight.
- The intruder may **not** fire first.
- Anyone who has been attacked may answer their attacker for
  `RetaliationWindow` (five minutes by default).

Your own space is therefore not a shield — it is a place where you have the first shot
and your enemy does not. Raiding stays possible and stays costly, which is what makes
it a raid.

### The contested approach is unchanged and remains hot

Inside `R_contested`, opposing factions may engage each other unprovoked on both sides
of the divide. No provocation, no locals. This is still the load-bearing part: it is
the risk attached to the only route to the only global market.

### Aggression is pairwise, not a flag

Shooting somebody makes you answerable **to them**, and to nobody else. A defender who
fires on an intruder does not become fair game for every other ship in the system.

This is deliberately not EVE's global criminal flag. A global flag is a reputation
system, and reputation needs somewhere to live, someone to decay it, and a UI to
explain it. Pairwise aggression needs one timestamp per pair and answers the only
question the rules actually ask.

### Protection is a shield, not a firing position

New-player protection (ADR-0008) still blocks unprovoked attacks anywhere. It no
longer blocks retaliation: a protected character who opens fire may be shot back at by
the person they hit. Without that, protection is a licence to farm kills.

### The function stays pure; the state lives outside it

`Pvp.CanAttack` takes the provoking timestamp as an argument rather than looking it up.
The caller reads the pair's last exchange and the clock; the rule decides. So the rule
is still evaluated identically on the server, in a test, and in EconSim, and still has
nothing stored that can drift out of step with the world.

What is new is that **something must record when one character attacks another** — a
per-pair timestamp with a five-minute lifetime. It has no schema yet, because nothing
shoots yet.

## Consequences

Positive:

- Defence is meaningful without home space being invulnerable.
- No global flagging system, no reputation, no decay job.
- The rule is still one pure function, and its asymmetry is directly testable —
  inverting it fails four tests.

Negative, and accepted:

- **There is now state.** ADR-0008's claim that PvP needed none was wrong, and any
  design that lets a victim answer back will be. Per-pair, short-lived, and outside the
  rule, but state.
- Whoever shoots first in faction space is decided by geography, so a defender who is
  slow to notice an intruder gets no advantage from the rule. Reaction time is the real
  contest, which is a fight about latency as much as about tactics.
- A pairwise window means a gang can be answered only one attacker at a time. If that
  reads as unfair in practice, the fix is to widen retaliation to the attacker's whole
  gang, which needs a notion of gangs that does not exist.
- Five minutes is a guess. It is content, not code.

## Alternatives considered

**Both halves simply hot past the ring.** Rejected: it makes the dividing plane
decorative, and the four homeworlds stop being anybody's home in any sense the rules
can see.

**Your own half is an absolute sanctuary.** Rejected: raiders could be shot and could
not shoot back, so raiding has no expression, and the only cross-faction combat left is
in the contested ring.

**Global criminal flagging, EVE-style.** Rejected as premature. It is a reputation
system wearing a combat system's clothes, and none of its machinery is needed to answer
"may I shoot this person right now".
