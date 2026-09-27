# EC World Events Module

Custom AzerothCore C++ module for dynamic world events with bot participation, capturable buildings, faction-aware rewards, and SC2-style barracks guard spawning.

## Related Docs

- Full RAID assault hardening rules and QA checklist: `docs/RAID_ASSAULT_RULES.md`

## Architecture

```
ECWorldEventManager.h        Core event engine interface/types
ECWorldEventManager.cpp      Core lifecycle + phase orchestration
ECWorldEventManager.Spawning.inl      Bot/object/creature/reinforcement spawning
ECWorldEventManager.CaptureCombat.inl Building capture + NPC combat automation
ECWorldEventManager.Rewards.inl       Rewards, messaging, persistence helpers
ECWorldEventBridge.h/cpp     Realtime bridge integration (JSON over TCP)
mod_ec_world_events.conf.dist  Configuration
data/sql/db-world/
  ec_world_events.sql          Event definitions, objects, creatures, schedules, custom GameObjects
  ec_world_events_defense.sql  Defense event definitions (triggered by Guard System)
data/sql/db-characters/      Rewards, participants, logs
```

## Event Lifecycle

```
SCHEDULED ──[StartEvent]──> PREPARING ──[timer]──> ACTIVE ──[timer/condition]──> ENDING ──> COMPLETED
                                │                    │                              │
                                │ Workers build      │ Workers despawn              └── DistributeRewards()
                                │ fortifications      │ Combat begins
                                │                    │
                                └── [StopEvent] ──> CANCELLED
```

### Phase Behavior

| Phase | `spawn_phase` | What Happens |
|-------|---------------|--------------|
| PREPARING | 1 | Banners, campfires, workers spawn. Workers build barricades/towers with emotes. |
| ACTIVE | 2 | Workers despawn (`despawn_phase=2`). Barricades, towers, guards spawn. Combat begins. |
| ENDING | 3 | Victory/defeat objects spawn. Rewards distributed. |

## Event Types

| Type | Description | Example |
|------|-------------|---------|
| Raid | Attackers assault a town | Goldshire Raid (event 1) |
| Defense | Defenders protect against attack | Crossroads Elite Defense (event 111) |
| World Boss | Boss spawns with phases | (planned) |
| Gathering | Resource collection race | (planned) |
| PvP Zone | Faction territory control | (planned) |

## Custom GameObjects (900080-900114)

All world event objects use custom `gameobject_template` entries in the 900080+ range. These are created by `ec_world_events.sql` and cleaned up with module-scoped `DELETE` statements.

| Entry | DisplayId | Name | Usage |
|-------|-----------|------|-------|
| 900100 | 6411 | EC War Bonfire | Atmosphere during PREPARING |
| 900101 | 192 | EC Campfire | Command post campfire |
| 900102 | 9423 | EC Guard Tower | Alliance capturable tower |
| 900103 | 9449 | EC Alliance Bunker | Alliance capturable bunker |
| 900104 | 9482 | EC Horde Tower | Horde capturable tower |
| 900105 | 9392 | EC Wooden Barricade | Road barricades |
| 900106 | 9393 | EC Large Barricade | Larger fortifications |
| 900107 | 9395 | EC Spike Wall | Spike barriers |
| 900108 | 410 | EC Guard Tower (Burning) | Tower on fire (capture in progress) |
| 900109 | 7699 | EC Guard Tower (Destroyed) | Rubble after capture |
| 900110 | 410 | EC Alliance Bunker (Burning) | Bunker on fire |
| 900111 | 7700 | EC Alliance Bunker (Destroyed) | Bunker rubble |
| 900112 | 410 | EC Horde Tower (Burning) | Tower on fire |
| 900113 | 7699 | EC Horde Tower (Destroyed) | Tower rubble |
| 900114 | 45 | EC Barricade (Destroyed) | Destroyed barricade smoke |
| 900080 | 5771 | EC Alliance War Banner | Alliance rally point |
| 900081 | 5773 | EC Horde War Banner | Horde rally point |
| 900082 | 9165 | EC Neutral War Banner | Neutral events |

## Construction Workers (PREPARING Phase)

During the PREPARING phase, faction-appropriate construction workers spawn to "build" fortifications, adding visual life to the pre-combat setup.

**Creatures used:**
- **Horde events:** Entry 11656 (Warsong Peon) — "Work work..." / "Something need doing?"
- **Alliance events:** Entry 28569 (Construction Worker) — "Almost got this barricade up!" / "Reinforcing the perimeter!"

**Worker behavior (C++ in `SpawnEventCreatures()`):**
- Detected by name containing "Peon" or "Worker"
- Immune to combat: `UNIT_FLAG_IMMUNE_TO_NPC` + `UNIT_FLAG_IMMUNE_TO_PC`
- Random work emotes: `EMOTE_STATE_WORK`, `EMOTE_STATE_WORK_CHOPWOOD`, `EMOTE_STATE_USE_STANDING`, `EMOTE_ONESHOT_WORK_CHOPWOOD`
- Faction-specific voice lines via `MonsterSay`
- Walk speed, 5yd patrol radius around assigned position
- `spawn_phase=1, despawn_phase=2` — appear during PREPARING, vanish when ACTIVE begins

## Capturable Buildings (AV-Style)

Buildings transition through visual states: **INTACT** -> **BURNING** -> **DESTROYED**

- Players interact with capturable objects to start capture
- Capture timers tick during ACTIVE phase
- State changes swap the visible GameObject using `burning_entry` / `destroyed_entry` columns
- Guards assigned to buildings via `assigned_building_id` are alerted when attacked
- Each capturable tower/bunker has burning (fire effect) and destroyed (rubble) variants

**Capturable building wiring (SQL):**
```sql
-- Example: Guard Tower with visual state transitions
INSERT INTO ec_world_event_objects (..., is_capturable, burning_entry, destroyed_entry, ...)
VALUES (..., 1, 900108, 900109, ...);  -- 900102 intact -> 900108 burning -> 900109 destroyed
```

## Tower Guards

Capturable towers and bunkers have guards assigned via `assigned_building_id`. Guards are linked to their building using a subquery pattern:

```sql
-- Guard linked to a specific capturable building
INSERT INTO ec_world_event_creatures (..., assigned_building_id, ...)
VALUES (..., (SELECT id FROM ec_world_event_objects WHERE event_id=X AND gameobject_entry=900102 AND is_capturable=1 LIMIT 1), ...);
```

When a building is captured or destroyed, its assigned guards are alerted and respond accordingly.

## Barracks Guard Respawn (SC2-Style)

INTACT capturable buildings periodically produce guards up to a maximum cap. When a building enters BURNING or DESTROYED state, guard production stops.

**Configuration per building (SQL):**
- `guard_respawn_seconds` - Interval between guard spawns (0 = disabled)
- `max_guards` - Maximum simultaneous alive guards from this building

**Defaults:**
| Building | Entry | Respawn | Max Guards |
|----------|-------|---------|------------|
| EC Guard Tower | 900102 | 60s | 2 |
| EC Alliance Bunker | 900103 | 45s | 4 |
| EC Horde Tower | 900104 | 60s | 2 |
| EC Large Barricade | 900106 | disabled | 0 |

**Implementation:** `ProcessBuildingGuardRespawns()` runs every update tick during ACTIVE phase. It prunes dead guards from tracking lists, checks alive count against cap, and spawns new guards at the building's position with random offset.

## Faction-Filtered Broadcasts

`BroadcastEventMessage()` sends messages only to the relevant faction:
- `EventFaction::NEUTRAL` — broadcasts to all players via `SendServerMessage`
- `EventFaction::ALLIANCE` or `HORDE` — iterates sessions, filters by `GetTeamId()`, sends via `ChatHandler::SendSysMessage`

## Victory/Defeat System

Events track `attackerScore` and `defenderScore` throughout their duration. At completion, the outcome is determined:

| Outcome | Condition | Winners |
|---------|-----------|---------|
| ATTACKER_WIN | attackerScore > defenderScore | Attacking faction |
| DEFENDER_WIN | defenderScore > attackerScore | Defending faction |
| DRAW | Equal scores | Nobody (consolation rewards to all) |

### Conditional Rewards

Rewards have a `reward_condition` field:
- `0` = Any participant (always granted)
- `1` = Winning faction only
- `2` = Losing faction only (consolation)

In a DRAW, condition=0 and condition=2 rewards are granted to everyone. Condition=1 is skipped (no winners).

### Conditional Object Spawning

Objects have a `spawn_condition` field:
- `0` = Always spawn
- `1` = Spawn only if attackers win (e.g., attacker victory banners)
- `2` = Spawn only if defenders win (e.g., defender victory banners)

## Reward System

Rewards are defined in `ec_world_event_rewards` (characters DB) and distributed at event completion.

| Type | Value Column | Notes |
|------|-------------|-------|
| Gold (0) | Copper amount | e.g., 100000 = 10g |
| Honor (1) | Honor points | Requires online player |
| Item (2) | Item entry ID | `item_count` for quantity |
| Title (3) | Title ID | Requires online player |

### Offline Player Rewards

Players who participated but logged off before event completion receive rewards via in-game mail:
- Gold is accumulated and sent via `MailDraft::AddMoney()`
- Items are attached via `MailDraft::AddItem()`
- Honor and titles are skipped (cannot be mailed)
- Faction is determined by querying the player's race from the characters DB

## Event Cooldown

After an event completes, the same event cannot be started again for a cooldown period.

- **Per-event override:** `cooldown_minutes` column in `ec_world_events` (0 = use global)
- **Global default:** `ECWorldEvents.EventCooldownMinutes` config (default 30)
- **Cancelled events:** Receive half the cooldown
- **In-memory only:** Cooldowns reset on server restart

## Auto-Scheduling

Events can run automatically via `ec_world_event_schedules`:

| Column | Purpose |
|--------|---------|
| `schedule_type` | 0=interval, 1=cron, 2=onetime |
| `interval_minutes` | Minutes between runs |
| `enabled` | 0=disabled, 1=enabled |
| `next_run` | NULL = run immediately when enabled |

10 default schedules are pre-configured (all disabled by default) with staggered intervals (85-120 min). Enable with:
```sql
UPDATE ec_world_event_schedules SET enabled = 1 WHERE event_id = 1;
```

## GM Commands

```
.ecevent list              List available event definitions
.ecevent status            Show currently active events
.ecevent start <id>        Start an event by definition ID
.ecevent stop <instance>   Stop an active event by instance ID
.ecevent debug on|off      Toggle in-game debug output for GMs
```

## Configuration

| Setting | Default | Description |
|---------|---------|-------------|
| `ECWorldEvents.Enable` | 1 | Enable/disable module |
| `ECWorldEvents.Debug` | 0 | Debug mode with in-game GM messages |
| `ECWorldEvents.DebugLogLevel` | 2 | 0=errors, 1=+warnings, 2=+info, 3=+trace |
| `ECWorldEvents.UpdateIntervalMs` | 1000 | Event tick rate (ms) |
| `ECWorldEvents.MaxActiveEvents` | 10 | Max concurrent events |
| `ECWorldEvents.EventCooldownMinutes` | 30 | Default cooldown after completion |
| `ECWorldEvents.BotHardCap` | 80 | Max bots per event |
| `ECWorldEvents.MaxCreatureSpawnsPerTick` | 5 | Creatures spawned per tick (batching) |
| `ECWorldEvents.DeferredSpawnStaggerMs` | 500 | Ms between deferred spawn batches |
| `ECWorldEvents.MaxBotTeleportsPerTick` | 3 | Bots teleported per tick (batching) |
| `ECWorldEvents.AnnounceToWorld` | 1 | Global announcements (planned) |
| `ECWorldEvents.MinParticipationForReward` | 10 | Min % to receive rewards (planned) |
| `ECWorldEvents.LogToDatabase` | 1 | Log events to ec_world_event_logs |

## Database Tables

### World DB (`acore_world`)
- `ec_world_events` - Event definitions (10 raids + defense events)
- `ec_world_event_creatures` - NPC spawn templates per event
- `ec_world_event_objects` - GameObject spawn templates per event
- `ec_world_event_schedules` - Auto-scheduling configuration

### Characters DB (`acore_characters`)
- `ec_world_event_rewards` - Reward definitions per event
- `ec_world_event_participants` - Player tracking (kills, deaths, damage, healing)
- `ec_world_event_logs` - Event history with outcome

## Realtime Bridge Integration

All significant events are emitted to `mod-ec-realtime-bridge` as JSON over TCP (port 8652):

- `event_started`, `event_ended` (with outcome)
- `participant_joined`, `participant_left`, `participant_died`
- `participant_rewarded` (with actual gold/honor/item values)
- `boss_engaged`, `boss_killed`
- `building_captured`, `building_destroyed`
- `score_changed`, `escalation_triggered`

## Defined Events

### Raid Events (1-10)
| ID | Name | Location | Faction |
|----|------|----------|---------|
| 1 | Goldshire Raid | Elwynn Forest | Horde attacks |
| 2 | Sentinel Hill Raid | Westfall | Horde attacks |
| 3 | Darkshire Raid | Duskwood | Horde attacks |
| 4 | Astranaar Raid | Ashenvale | Horde attacks |
| 5 | Auberdine Raid | Darkshore | Horde attacks |
| 6 | Crossroads Raid | The Barrens | Alliance attacks |
| 7 | Camp Taurajo Raid | The Barrens | Alliance attacks |
| 8 | Grom'gol Raid | Stranglethorn | Alliance attacks |
| 9 | Tarren Mill Raid | Hillsbrad | Alliance attacks |
| 10 | Splintertree Raid | Ashenvale | Alliance attacks |

### Defense Events (101-122)

Auto-triggered by `mod-ec-guard-system` when a zone falls. Elite faction forces respond to reclaim territory. Defined in `ec_world_events_defense.sql`.

**Faction-Wide Defaults (zone_id=0, fallback when no zone-specific event exists):**

| ID | Name | Defender |
|----|------|----------|
| 101 | Kor'kron Elite Response | Horde |
| 102 | Stormwind Royal Guard Response | Alliance |

**Zone-Specific Defense Events:**

| ID | Name | Zone | Defender | Content |
|----|------|------|----------|---------|
| 111 | Crossroads Elite Defense | Barrens | Horde | Banners, bonfires, 4 barricades, 2 towers, Kor'kron leader + 2 elites, 3 peons |
| 112 | Tarren Mill Elite Defense | Hillsbrad | Horde | Banners, bonfires, 2 barricades, 1 tower, 1 large barricade, Kor'kron battlemaster + 2 elites, 2 peons |
| 121 | Goldshire Elite Defense | Elwynn Forest | Alliance | Banners, campfire, 3 barricades, 1 tower, 1 bunker, Field Marshal + 2 guards, 3 workers |
| 122 | Astranaar Elite Defense | Ashenvale | Alliance | Banner, campfire, 2 barricades, 2 towers, 1 large barricade, Sentinel Commander + 2 warriors, 4 workers |

Events 101/102 have no static objects or creatures — they are runtime fallbacks where the Guard System provides spawn coordinates dynamically.
