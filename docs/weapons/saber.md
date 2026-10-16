# Saber — draft trees

**Status: Claude's draft, 15 October 2026, for Joe to edit. Nothing here is decided** beyond the family's
charge and Dissonance ([README](README.md)). Task 192, [ADR-0015](../adr/0015-every-weapon-is-a-skill.md).
Numbers are first guesses; none has been tested.

**Resonance family, melee, on foot.** The family's only melee weapon, so it has no sibling and keeps all of
its XP. Builds 8 charge a hit.

## The idea

**A resonating blade**: a sword whose edge hums, and hits harder the longer its wielder keeps hitting. Where
the Cutter bores into one thing and the Daggers dart in and out, the Saber stands and duels: timing, guarding
and answering. Charge built in the exchange is spent in cleaving waves that reach a little past the blade.
Its trees are **Duelist** (one opponent, parries, Dissonance) and **Vanguard** (the front of a group: wide
cuts and protecting the people behind).

The setting's own, not somebody else's: no deflecting blaster bolts, no glowing hilt-only blade. The Cutter's
Beam guard already reflects laser bolts; the Saber parries blades and fists.

## Attacks

| Attack | Input | What it does | Charge |
|---|---|---|---|
| **Strike** | Click | A three-hit combo; the third cuts in an arc. Each hit adds a stack of Dissonance once per second. | +8 a hit |
| **Cleave** | Hold, release | Spends your charge on a sweeping cut that sends a wave of resonance 6 m ahead. At 100 charge, about four strikes' worth to everyone in it. | Spends all |

## The trees

14 nodes each, 28 in all, for 20 points, as every weapon's.

### Duelist — one opponent at a time

| Node | Kind | Effect |
|---|---|---|
| **Parry** | Active, 8 s | For 0.4 s you parry the next melee attack: it does nothing, and the attacker is staggered. |
| ↳ Riposte | Upgrade | A successful parry is followed at once by a free strike that critically hits. |
| ↳ Wider window | Upgrade | 0.6 s. |
| **Flourish** | Active, 14 s | A quick sequence of four cuts on one target, each adding a stack of Dissonance. |
| ↳ Finale | Upgrade | The fourth cut builds 20 charge. |
| ↳ Disarm | Upgrade | A target at three Dissonance stacks cannot use abilities for 1.5 s. |
| **Challenge** | Active, 20 s | Mark one enemy for 8 s: you deal 15% more to them, and they deal 15% less to anyone but you. |
| ↳ Answer me | Upgrade | They are pulled 3 m toward you when it lands. |
| ↳ Honour | Upgrade | Defeating them while marked fills your charge. |
| Footwork | Passive | You move 15% faster while your target is marked or at three Dissonance stacks. |
| Edge | Passive | Strikes deal +10%. |
| Weak point | Passive | Hits to a weak point critically hit, +35%. |
| Composure | Passive | Charge does not fade for 8 s after a successful parry. |
| **Perfect form** | Capstone | Three successful parries within 10 s make your next Cleave count as full charge and strike twice. |

### Vanguard — the front of the group

| Node | Kind | Effect |
|---|---|---|
| **Sweep** | Active, 12 s | A full circle cut around you, hitting everyone within 3 m and adding Dissonance to each. |
| ↳ Wide | Upgrade | 4.5 m. |
| ↳ Gathering | Upgrade | Each enemy hit builds 6 charge. |
| **Stand fast** | Active, 22 s | For 5 s you cannot be staggered or knocked back, and allies within 6 m behind you take 15% less damage. Costs 30 charge. |
| ↳ Shoulder to shoulder | Upgrade | Allies in range also cannot be staggered. |
| ↳ Rally | Upgrade | When it ends, those allies gain a stack of Harmony. |
| **Advance** | Active, 14 s | Dash 10 m forward, cutting through everyone in the way. |
| ↳ Breach | Upgrade | Those cut are staggered. |
| ↳ Hold the gap | Upgrade | At the end of the dash, enemies within 3 m are slowed 30% for 2 s. |
| Broad edge | Passive | Strike's third hit cuts in a full half-circle. |
| Bastion | Passive | You take 10% less damage while at 50 charge or more. |
| Long wave | Passive | Cleave's wave reaches 9 m. |
| Inspiring | Passive | Allies within 6 m build their weapons' charge 10% faster, or cool their heat 10% faster. |
| **Unbroken line** | Capstone | While Stand fast holds, every hit you take builds 5 charge, and its end releases a free full-charge Cleave. |

## What the draft is trying to make possible

- **A Duelist** for one-on-one, in PvP above all: Parry, Riposte, Challenge, Perfect form.
- **A Vanguard** at the front of a group with a Focus behind: Stand fast, Sweep, Advance into a doorway.
- **Two Resonance weapons**: Saber with Staff, both building charge, though the charge is each weapon's own.

## Questions this draft raises

- **Parry needs timing over a network.** A 0.4 s window is tight with latency; the server decides, and
  whether it feels fair is a playtest question.
- **Challenge** forces attention in PvP; good in a group fight, possibly frustrating against one player.
- **The Saber is the one weapon here that suggests a famous setting** even under a new family name. The
  draft leans on duelling and a front line rather than on anything glowing or deflecting; its look is the
  Visual Bible's question, in every world's style (task 187).
