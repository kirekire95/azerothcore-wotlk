# Alterac Valley / Battlefield audit for EC World Events

Date: 2026-09-27

## Scope and provenance

This audit compares the custom persistent open-world settlement simulation with
AzerothCore's existing Alterac Valley and Battlefield implementations.

Primary local sources:

- `src/server/game/Battlegrounds/Zones/BattlegroundAV.cpp`
- `src/server/game/Battlegrounds/Zones/BattlegroundAV.h`
- `src/server/game/Battlefield/Battlefield.cpp`
- `src/server/game/Battlefield/BattlefieldMgr.cpp`

The custom 80% settlement defeat rule, persistent economy, regional links,
physical sacking, and open-world persistence are project decisions. They are not
claimed to reproduce Blizzard AV.

## Patterns worth adopting

### 1. Explicit strategic-node state machines

AV keeps node ownership and state separate and transitions nodes through
controlled/assaulted/destroyed states. Assaulted nodes remain contested until a
timer resolves, and defenders can reverse an assault before completion.

EC World Events already has INTACT/BURNING/DESTROYED buildings and staged raid
objectives. The next refactor should make strategic settlement assets explicit
persistent nodes rather than infer every strategic role from generic buildings.

### 2. Data-defined defender tiers instead of generic stat inflation

AV armor-scrap thresholds (500/1000/1500 progress) repopulate controlled nodes.
`PopulateNode` selects a different defender creature tier at each threshold.

EC currently increases guard capacity, respawn speed, and reinforcement count
from settlement development. Keep those effects, but add data-defined guard
tiers so development selects real creature templates/equipment appropriate to
that settlement. Never manufacture stat multipliers when a real creature tier is
available.

### 3. Resource ownership should have periodic strategic output

AV mines have an owner, periodically add reinforcement value, can be reclaimed
to neutral, and repopulate with creatures appropriate to the current owner.

EC regional resources currently generate abstract income. Evolve them into
physical resource nodes with owner/health/state. Their periodic output should
feed the settlement treasury or reinforcement capacity only while operational.

### 4. Important assets should have discrete consequences

AV player deaths cost reinforcement points; captain deaths and destroyed towers
cause larger discrete reinforcement losses. This makes target selection matter.

EC should retain the requested 80% population defeat condition, but important
settlement assets can independently modify reinforcement capacity, income,
warning time, guard tier availability, or regional support. They should not
replace the 80% rule.

### 5. Player zone membership should be event state, not repeated discovery

Battlefield tracks players entering/leaving the zone and keeps per-team sets for
players in the battlefield/war. EC currently recomputes defending players from
the map each attrition evaluation.

For correctness, dynamic entry/leave behavior already works. For scale and UI,
adopt explicit per-settlement participant sets updated on zone/area changes,
with a reconciliation scan as a safety net.

### 6. World states are the right presentation mechanism

AV updates world states for reinforcement scores, node ownership, and mines.
Battlefield sends initial/update world states to participants.

EC should expose town survival, current settlement damage/development, strategic
asset state, and active reinforcement alerts through world states where the
3.3.5a client supports them, rather than relying only on chat messages.

## Patterns not to copy directly

- AV resets as an instanced battleground; EC settlements persist.
- AV's fixed reinforcement-point victory condition does not replace the
  project's dynamic 80% settlement-population rule.
- AV graveyard/spirit-healer ownership is battleground-specific and should not
  override normal open-world resurrection without a separate design decision.
- AV frequently despawns/repopulates a whole node to apply upgrades. In the open
  world, persistent residents should not visibly vanish just because a
  development threshold was reached; upgrades should apply on replacement,
  reinforcement, recovery, or controlled transition.
- AV's mine neutral-reclaim timer is useful as a pattern, not a requirement for
  every EC resource.

## Current EC gaps exposed by the audit

1. Guard development still uses generic quantity/speed bonuses; no data-defined
   guard tier table exists.
2. Resource rows are economic records, not physical capturable/destroyable
   nodes with ownership.
3. Regional support is economic transfer; troop support is not yet a physical
   convoy/march from the source settlement.
4. No AV-style world-state presentation exists for town survival/resources.
5. Strategic assets do not yet have typed consequences (barracks, trade,
   watchtower, supply depot, leader).
6. Player presence is dynamically counted but not maintained as explicit
   Battlefield-style membership state.
7. Recovery needs an explicit persistent node transition model rather than
   merely reducing a settlement-wide damage percentage.

## Recommended implementation order

1. Add persistent strategic resource/node state and typed effects.
2. Add data-defined guard development tiers.
3. Make regional military support physically travel from source nodes.
4. Add Battlefield-style participant tracking plus reconciliation.
5. Add world-state/UI reporting.
6. Refine recovery/ownership transitions around the persistent node model.

This order preserves the project's existing 80% attrition and staged invasion
work while replacing the remaining abstract systems with proven WoW-style
patterns.
