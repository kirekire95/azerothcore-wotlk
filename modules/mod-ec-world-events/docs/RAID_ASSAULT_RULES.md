# RAID Assault Rules and Takeover Gates

This document describes the hardened RAID-event behavior for `mod-ec-world-events`.
It focuses on the specific problems observed during city assaults:

- Attacker banners spawning before a real takeover.
- Towers/bunkers burning before defenders are actually defeated.
- NPCs standing idle instead of actively engaging targets.
- PREPARING phase using the wrong worker faction visuals.
- Optional attacker/defender bot composition tuning.

## Scope

These rules apply to `EventType::RAID` events and are implemented in:

- `src/ECWorldEventManager.cpp`
- `src/ECWorldEventManager.h`
- `data/sql/db-world/ec_world_events.sql`
- `conf/mod_ec_world_events.conf.dist`

## Phase Contract

RAID flow:

1. `PREPARING`
2. `ACTIVE`
3. `ENDING`

Behavior contract:

1. `PREPARING`: city is fortifying.
2. `ACTIVE`: assault and city combat.
3. `ENDING`: outcome visuals and rewards.

No takeover visuals should appear before takeover conditions are satisfied.

## PREPARING vs ACTIVE Spawn Discipline

Rules:

1. Defensive prep objects/workers belong to the defending faction in `PREPARING`.
2. Assault troops should not teleport into final combat positions during `PREPARING`.
3. Assault troops belong in `ACTIVE`.

Example for Camp Taurajo RAID (`event_id = 7`):

1. PREPARING visuals/units should be Horde-side defense setup.
2. Alliance assault force should primarily appear in ACTIVE phase.

Use SQL spawn-phase discipline:

- `spawn_phase = 1`: PREPARING-only content.
- `spawn_phase = 2`: ACTIVE assault content.
- `despawn_phase = 2`: entities that should disappear when ACTIVE starts.

## Takeover Gate: Banners (Critical)

Problem solved:

- Attacker victory banners appeared too early.

Current gate in `SpawnEventGameObjects()`:

1. Applies only to RAID attacker-faction banner entries (`900080` alliance / `900081` horde).
2. Requires `event.outcome == ATTACKER_WIN`.
3. Requires **zero** alive defender-faction NPCs in the takeover radius.

Implementation details:

1. Defender count is computed by `CountAliveFactionCreaturesNearLocation(...)`.
2. Radius constant: `kRaidTakeoverClearRadiusYards = 90.0f`.
3. Count excludes critters, pets, totems, and triggers.
4. Count includes alive faction-matching creatures in the event zone.

Effect:

- Attacker banners are delayed until city defense is actually collapsed.

## Takeover Gate: Burning Towers/Bunkers (Critical)

Problem solved:

- Buildings could enter burning state while city defense was still standing.

Current gate in `StartBuildingCapture()`:

1. Applies to RAID events.
2. Applies when capturer is from the attacker faction.
3. Uses the same defender clear check/radius as banner gating.
4. Blocks capture start if any defender-faction NPCs remain alive nearby.

Player feedback:

- Capturer receives a system message explaining why capture is blocked.

Effect:

- Towers/bunkers only burn after defender collapse, matching takeover expectations.

## Combat Automation for Spawned Event NPCs

Problem solved:

- Military NPCs stood still or failed to acquire useful targets.

Current behavior in `ProcessEventCreatureCombat()`:

1. Runs during ACTIVE phase.
2. Aggro scan interval: `1s`.
3. Status log interval: `10s`.
4. Idle wander nudge interval: `4s`.

Combat rules:

1. Construction workers are excluded from combat automation.
2. Military NPCs first try normal hostile acquisition (`SelectNearestTarget`).
3. If no target is found, RAID fallback targeting runs:
   - prefers opposite faction targets nearby.
   - attacker-side RAID NPCs may also include neutral targets as fallback.
4. If still idle, NPC receives movement nudge (`MoveRandom`) to avoid static clumping.

Telemetry captured per event:

1. Alive combatants.
2. Engaged combatants.
3. Targets acquired.
4. Stale GUIDs cleaned.
5. Idle wander nudges issued.

## Worker Behavior Contract

Workers are visual setup actors in PREPARING only.

Rules:

1. Worker detection is name-based (`"Peon"` / `"Worker"`).
2. Workers are immune to NPC and PC aggro.
3. Workers use work emotes and short patrol movement.
4. Workers should despawn at ACTIVE transition.

Faction expectations:

1. Horde-side prep: peon-style workers.
2. Alliance-side prep: construction-worker style workers.

## Bot Composition: Attacker/Defender Mix

RAID bot mix is configurable.

Config options:

1. `ECWorldEvents.RaidDefenderBotsEnabled`
2. `ECWorldEvents.RaidDefenderBotSharePct`

Behavior:

1. Disabled (`0`): attacker-side bots only.
2. Enabled (`1`): mixed recruitment.
3. Share percent controls intended defender ratio (clamped to `0..100`).

Example:

- `RaidDefenderBotsEnabled = 1`
- `RaidDefenderBotSharePct = 50`

This produces approximately 50/50 attacker-defender recruitment for RAID events.

## What Is Counted as "Defense Still Alive"

For takeover/burning gates, defender presence is considered alive if:

1. Creature is alive and in world.
2. Creature belongs to defender faction mask.
3. Creature is in event zone and within the configured takeover radius.

Not counted:

1. Critters.
2. Pets.
3. Totems.
4. Trigger-only entities.

## QA Checklist

Use this checklist for every RAID tuning iteration:

1. Start RAID and verify PREPARING defenders/workers are faction-correct.
2. Verify assault troops do not hard-teleport into final city center during PREPARING.
3. During ACTIVE, verify military NPCs acquire targets and do not remain idle.
4. Try capturing tower/bunker before defender wipe:
   - capture should be blocked.
   - blocker message should appear.
5. Before full defender collapse, verify attacker takeover banners do not spawn.
6. After defender collapse + attacker win, verify attacker banners spawn.
7. If mixed bot mode enabled, validate approximate bot-side ratio.

## Troubleshooting Guide

If attacker banners still appear too early:

1. Confirm event is RAID type.
2. Confirm attacker banner entries are `900080/900081`.
3. Confirm spawned object has attacker-win `spawn_condition = 1`.
4. Confirm defenders are not being miscounted due wrong zone coordinates.

If towers burn too early:

1. Confirm capture attempt is attacker-faction in a RAID event.
2. Confirm defender count is non-zero at that moment (logs).
3. Confirm event location/zone and takeover radius cover intended city footprint.

If NPCs still look idle:

1. Confirm they are military (not worker entries/names).
2. Confirm they are in ACTIVE phase.
3. Check combat status logs for alive/engaged/idle values.
4. Check fallback target acquisition logs and idle wander nudge counters.

## Notes

This document intentionally defines strict "defense must fully fall first" behavior.
If future design changes require softer takeover logic (for example, percentage-based defender control), update:

1. Banner gate logic in `SpawnEventGameObjects()`.
2. Building gate logic in `StartBuildingCapture()`.
3. This document and the QA checklist in the same change.
