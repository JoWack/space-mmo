# Focus — draft trees

**Status: Claude's draft, 15 October 2026, for Joe to edit. Nothing here is decided** beyond the family's
charge, Dissonance and Harmony ([README](README.md)), and the Focus being the healer (ADR-0015). Task 192,
[ADR-0015](../adr/0015-every-weapon-is-a-skill.md). Numbers are first guesses; none has been tested.

**Resonance family, ranged, on foot: the healing weapon.** A hand-held resonator. Builds charge, and earns
its XP, by **healing** (ADR-0015): 1 charge for every 2% of a target's health restored. Shares 20% of its XP
with the Staff.

## The idea

**Keep the group standing.** The Focus heals at range and makes allies stronger with Harmony; it barely
hurts anyone. Charge builds as you heal, and spends on big moments: a burst of healing across the whole
group, or a shield when it is needed. Its trees are **Mender** (healing, more of it, faster) and **Warden**
(protection: shields, Harmony, keeping allies alive before they need healing).

## Attacks

| Attack | Input | What it does | Charge |
|---|---|---|---|
| **Mending beam** | Click and hold on an ally | A beam to 20 m that heals the ally you aim at, 2% of their health a second, and gives them one stack of Harmony every 2 s. On an enemy it does a little damage and adds Dissonance. | Built by healing |
| **Bloom** | Hold the heavy input, release | Spends your charge on a burst of healing centred where you aim, 6 m across: at 100 charge, 30% of every ally's health. | Spends all |

## The trees

14 nodes each, 28 in all, for 20 points, as every weapon's.

### Mender — heal more, heal faster

| Node | Kind | Effect |
|---|---|---|
| **Restore** | Active, 10 s | An instant heal on one ally, 15% of their health. |
| ↳ Overflow | Upgrade | Healing past their full health becomes a shield, up to 10%. |
| ↳ Quick hands | Upgrade | 7 s cooldown. |
| **Renewal** | Active, 16 s | Heals one ally 4% of their health a second for 6 s. |
| ↳ Spreading | Upgrade | When it ends, it jumps to the most injured ally within 10 m, at half the strength. |
| ↳ Deep roots | Upgrade | It also removes one harmful effect: Scorch, Stagger or Dissonance. |
| **Sanctuary** | Active, 30 s | A field 5 m across for 6 s: allies inside heal 3% a second. Costs 40 charge. |
| ↳ Wider | Upgrade | 7 m. |
| ↳ Steadfast | Upgrade | Allies inside cannot be staggered. |
| Attuned | Passive | Your healing is 10% stronger. |
| Long reach | Passive | Mending beam reaches 30 m. |
| Triage | Passive | Healing an ally below 35% health is 25% stronger. |
| Efficient | Passive | Bloom heals as much at 80 charge as at 100. |
| **Second wind** | Capstone | Once a minute, an ally you are healing who would fall is held at 1 health for 2 s instead, and healed 20% of their health. |

### Warden — keep them from needing it

| Node | Kind | Effect |
|---|---|---|
| **Ward** | Active, 14 s | A shield on one ally absorbing 20% of their health for 6 s. |
| ↳ Thorned | Upgrade | Enemies who hit the shield take one stack of Dissonance. |
| ↳ Lasting | Upgrade | It lasts 10 s. |
| **Chorus** | Active, 20 s | Every ally within 10 m gains three stacks of Harmony at once. |
| ↳ Sustain | Upgrade | Harmony from Chorus lasts 10 s. |
| ↳ Resounding | Upgrade | Allies with three Harmony stacks build their own weapon's charge or cool its heat 20% faster. |
| **Silence** | Active, 18 s | A burst that gives every enemy within 8 m of the point you aim three stacks of Dissonance. Costs 30 charge. |
| ↳ Stifle | Upgrade | Their next ability is delayed 1 s. |
| ↳ Wider | Upgrade | 12 m. |
| In tune | Passive | Harmony stacks last 7 s. |
| Bulwark | Passive | Your shields are 20% stronger. |
| Steady | Passive | You take 15% less damage while your Mending beam is on an ally. |
| Composure | Passive | You build 5 charge whenever one of your shields breaks. |
| **Grand harmony** | Capstone | Allies with three Harmony stacks take 15% less damage. |

## What the draft is trying to make possible

- **A Mender** for a group in PvE: Renewal and Sanctuary through a long fight, Second wind for the save.
- **A Warden** in PvP, where healing alone loses to burst damage: Ward before the burst, Silence the
  enemy's damage, Chorus for the push.
- **With the Staff** as the second weapon, sharing XP: something to fight with when nobody needs healing.

## Questions this draft raises

- **Healing in PvP.** One healer behind a group changes faction-space fights a great deal (ADR-0009); the
  numbers here are guesses and healing is the first thing PvP balance usually cuts.
- **Healing yourself**: nothing here does, deliberately. A healer who can always heal themselves is very
  hard to kill alone. Worth deciding outright.
- **Second wind** prevents a death, which ADR-0006 (and its rethink, task 103) gives weight to.
- **Healing earns XP** (ADR-0015), so healing an ally who is being hurt on purpose is a way to farm it.
  Healing done in combat only, or with a cap, would close that.
