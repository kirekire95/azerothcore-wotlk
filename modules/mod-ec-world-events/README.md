# EC World Events Module

A C++ AzerothCore module for managing dynamic world events like town raids, defense events, and scheduled PvP activities with automated bot participation using the Playerbots module.

## Features

- **Event Types**: Raids, Defense, World Boss, Gathering, PvP Zone
- **Playerbots Integration**: Automatically recruits bots for events based on faction and level
- **Participant Tracking**: Tracks kills, deaths, damage dealt, healing done
- **Auto Rewards**: Distributes gold, honor, or items based on participation
- **Scheduling**: Support for scheduled events (day/time based)
- **Escalation**: Events can escalate with more reinforcements
- **Comprehensive Logging**: EC_LOG_* macros for console + in-game GM debugging

## Detailed Behavior Docs

- Core system documentation: `docs/WORLD_EVENTS.md`
- RAID hardening and takeover-gate rules: `docs/RAID_ASSAULT_RULES.md`

## Directory Structure

```
mod-ec-world-events/
├── src/
│   ├── ECWorldEventManager.h      # Core event engine header
│   ├── ECWorldEventManager.cpp    # Full implementation (~1200 lines)
│   ├── ECWorldEventScript.cpp     # Hooks + commands
│   └── ec_world_event_loader.cpp  # Module entry point
├── conf/
│   └── mod_ec_world_events.conf.dist
├── data/sql/
│   ├── db-world/
│   │   ├── ec_world_events.sql
│   │   ├── ec_world_events_defense.sql
│   │   └── ec_world_events_uninstall.sql
│   └── db-characters/
│       ├── ec_world_events_characters.sql
│       └── ec_world_events_uninstall.sql
├── scripts/
│   └── ec_status.sh               # Database status viewer
└── mod-ec-world-events.cmake      # CMake configuration
```

## Installation

### Prerequisites
- AzerothCore with mod-playerbots installed
- MySQL database running

### Steps

1. **Copy module to modules directory**
   ```bash
   cp -r mod-ec-world-events /path/to/azerothcore-wotlk/modules/
   ```

2. **Run SQL scripts**
   ```bash
   # World database
   mysql -u root -p acore_world < modules/mod-ec-world-events/data/sql/db-world/ec_world_events.sql

   # Characters database
   mysql -u root -p acore_characters < modules/mod-ec-world-events/data/sql/db-characters/ec_world_events_characters.sql
   ```

3. **Build AzerothCore** (local build, NOT docker compose)
   ```bash
   cd /path/to/azerothcore-wotlk
   mkdir -p build && cd build
   cmake .. \
     -DCMAKE_INSTALL_PREFIX=/path/to/env/dist \
     -DTOOLS_BUILD=none \
     -DSCRIPTS=static \
     -DMODULES=static \
     -DCMAKE_BUILD_TYPE=RelWithDebInfo \
     -DMYSQL_INCLUDE_DIR=/opt/homebrew/Cellar/mysql-client/9.6.0/include/mysql \
     -DMYSQL_LIBRARY=/opt/homebrew/Cellar/mysql-client/9.6.0/lib/libmysqlclient.dylib
   make -j8
   make install
   ```

4. **Copy config file**
   ```bash
   cp modules/mod-ec-world-events/conf/mod_ec_world_events.conf.dist env/dist/etc/mod_ec_world_events.conf
   ```

5. **Restart worldserver**

## Configuration

Edit `mod_ec_world_events.conf`:

```ini
# Enable/disable the module
ECWorldEvents.Enable = 1

# Enable debug mode (shows messages in-game to GMs)
ECWorldEvents.Debug = 0

# Debug log level (0=errors, 1=+warnings, 2=+info, 3=+debug, 4=+trace)
ECWorldEvents.DebugLogLevel = 2

# How often to check for scheduled events (ms)
ECWorldEvents.UpdateIntervalMs = 60000

# Maximum concurrent active events
ECWorldEvents.MaxActiveEvents = 3
```

## Commands

### Player Commands
| Command | Description |
|---------|-------------|
| `.ecevent list` | List all available events |
| `.ecevent status` | Show active events |
| `.ecevent help` | Show all commands |

### GM Commands (SEC_GAMEMASTER)
| Command | Description |
|---------|-------------|
| `.ecevent start <event_id>` | Start an event |
| `.ecevent stop <instance_id>` | Stop an active event |
| `.ecevent escalate <instance_id>` | Add more bots to event |
| `.ecevent reload` | Reload event definitions |

### Debug Commands (SEC_GAMEMASTER)
| Command | Description |
|---------|-------------|
| `.ecevent debug on` | Enable in-game debug output |
| `.ecevent debug off` | Disable in-game debug output |
| `.ecevent debug level <0-4>` | Set log verbosity |
| `.ecevent debug dump` | Dump full module state |
| `.ecevent debug event <id>` | Dump specific event details |
| `.ecevent debug participants <id>` | List all event participants |

## Default Events

The SQL installation creates 10 raid events:

| ID | Name | Location | Attackers | Level Range |
|----|------|----------|-----------|-------------|
| 1 | Goldshire Raid | Elwynn Forest | Horde | 1-20 |
| 2 | Sentinel Hill Raid | Westfall | Horde | 10-25 |
| 3 | Darkshire Raid | Duskwood | Horde | 20-30 |
| 4 | Astranaar Raid | Ashenvale | Horde | 20-30 |
| 5 | Auberdine Raid | Darkshore | Horde | 10-20 |
| 6 | Crossroads Raid | Barrens | Alliance | 10-25 |
| 7 | Camp Taurajo Raid | Barrens | Alliance | 20-30 |
| 8 | Grom'gol Raid | Stranglethorn | Alliance | 30-45 |
| 9 | Tarren Mill Raid | Hillsbrad | Alliance | 20-30 |
| 10 | Splintertree Raid | Ashenvale | Alliance | 20-30 |

## Defense Events (Elite Response)

Defense events are triggered by the Guard System when a zone falls to criminals. Elite faction forces respond to reclaim territories.

**Installation:**
```bash
mysql -u root -p acore_world < modules/mod-ec-world-events/data/sql/db-world/ec_world_events_defense.sql
```

### Faction-Wide Defaults (zone_id = 0)

Used as fallback when no zone-specific defense event exists:

| ID | Name | Defender | Description |
|----|------|----------|-------------|
| 101 | Kor'kron Elite Response | Horde | Thrall's elite guard dispatched to any fallen Horde zone |
| 102 | Stormwind Royal Guard Response | Alliance | King's Royal Guard dispatched to any fallen Alliance zone |

### Zone-Specific Events

Higher priority than faction-wide defaults - used when a specific zone falls:

| ID | Name | Zone | Min/Max Bots | Duration | Level Range |
|----|------|------|--------------|----------|-------------|
| 111 | Crossroads Elite Defense | Barrens | 25-80 | 45 min | 10-80 |
| 112 | Tarren Mill Elite Defense | Hillsbrad | 30-100 | 45 min | 20-80 |
| 121 | Goldshire Elite Defense | Elwynn | 20-60 | 30 min | 5-80 |
| 122 | Astranaar Elite Defense | Ashenvale | 30-90 | 45 min | 20-80 |

### Event Flow

```
1. Player kills guards → Zone threat increases
2. Zone reaches FALLEN state (ControlState::FALLEN)
3. Guard System calls: sECWorldEventMgr->ScheduleDefenseEvent(zoneId, faction, delay)
4. World Events finds matching defense event (zone-specific or faction-wide fallback)
5. After delay, event starts and spawns elite bots to reclaim the zone
6. Zone announcement broadcasts to relevant faction
```

## Database Schema

### acore_world.ec_world_events
```sql
id              INT PRIMARY KEY
name            VARCHAR(100)
description     TEXT
type            TINYINT         -- 0=Raid, 1=Defense, 2=WorldBoss, 3=Gathering, 4=PvPZone
attacker_faction TINYINT        -- 0=Neutral, 1=Alliance, 2=Horde
defender_faction TINYINT
map_id          INT
location_x/y/z  FLOAT
zone_id         INT
area_id         INT
location_name   VARCHAR(100)
min_bots        INT
max_bots        INT
duration_minutes INT
min_level       INT
max_level       INT
enabled         TINYINT
```

### acore_characters.ec_world_event_rewards
```sql
id              INT PRIMARY KEY
event_id        INT
reward_type     TINYINT         -- 0=Gold, 1=Honor, 2=Item
value           INT             -- Gold in copper, or honor points, or item entry
item_count      INT
min_kills       INT             -- Required kills for reward
participation_percent INT       -- Required participation %
```

### acore_characters.ec_world_event_participants
```sql
id              INT PRIMARY KEY
instance_id     INT
player_guid     BIGINT
player_name     VARCHAR(50)
kills           INT
deaths          INT
damage_dealt    BIGINT
healing_done    BIGINT
join_time       DATETIME
rewarded        TINYINT
```

### acore_characters.ec_world_event_bot_return_context
```sql
bot_guid             INT PRIMARY KEY
event_instance_id    INT
event_id             INT
map_id                INT
position_x/y/z        FLOAT
orientation           FLOAT
combat_strategies     TEXT
non_combat_strategies TEXT
dead_strategies       TEXT
captured_at           TIMESTAMP
```

## Playerbots Integration

The module integrates with mod-playerbots to spawn attackers:

1. **Bot Selection**
   - Gets all bots from `sRandomPlayerbotMgr->GetAllBots()`
   - Filters by level range and faction
   - Excludes bots already in other events

2. **Bot Teleportation**
   - Immediately before teleport, writes a durable return checkpoint containing the bot's exact map, coordinates, orientation, and combat/non-combat/dead strategy sets
   - During the same worldserver process, also snapshots the current travel destination/status/timer and RPG target/activity
   - Protects recruited bots from RandomPlayerbotMgr randomization, unrelated teleports, and logout while the event owns them
   - Teleports selected bots to event location only after the database checkpoint is verified
   - Spreads bots in 15-unit radius around center point
   - Notifies bots via chat message

3. **Bot Release**
   - When an event enters its ending phase, each bot is returned to its exact pre-event location after teleport acknowledgement
   - Restores the exact three strategy sets and, for a normal event end, resumes the paused travel/RPG activity with its remaining timer
   - Saves the restored character and removes the durable checkpoint atomically on the character-database worker, then verifies marker removal before releasing the bot
   - After a worldserver restart, durable location and strategy checkpoints are recovered automatically; recovery continues if creation of new events is disabled, and disabling the module mid-event cancels active events into the same return pipeline
   - Only after successful restore does the bot return to normal RandomPlayerbotMgr behavior, with random maintenance delayed for five minutes

Bots in an existing group, battleground/queue, LFG/dungeon, combat, flight, vehicle, transport, or another teleport are not eligible for event recruitment. This avoids overwriting state the event module does not own. In-process travel/RPG details contain runtime object references and therefore cannot survive a process restart; the durable location and exact strategy sets still do.

The character database table `ec_world_event_bot_return_context` is the authoritative recovery queue. For existing installs, apply `data/sql/db-characters/updates/2026_09_01_00_ec_world_event_bot_return_context.sql`. A row must never be deleted manually while its bot is away unless the bot has first been returned or deliberately recovered by an administrator.

Exact strategies are committed synchronously to the Playerbots database before the character checkpoint is removed. The Playerbots `playerbots_db_store.value` column must be `TEXT`; apply `mod-playerbots/data/sql/playerbots/updates/2026_09_01_00_playerbots_db_store_value_text.sql` on existing installs so long strategy sets are never truncated.

**Note**: If MOD_PLAYERBOTS is not defined during compile, bot spawning will be disabled and a warning logged.

## Status Script

Check module state via:
```bash
./modules/mod-ec-world-events/scripts/ec_status.sh
```

Shows:
- Tables and row counts
- Event definitions
- Active events
- Recent participants
- Event logs

## Uninstallation

```bash
# Remove from world database
mysql -u root -p acore_world < modules/mod-ec-world-events/data/sql/db-world/ec_world_events_uninstall.sql

# Remove from characters database
mysql -u root -p acore_characters < modules/mod-ec-world-events/data/sql/db-characters/ec_world_events_uninstall.sql

# Remove module directory
rm -rf modules/mod-ec-world-events

# Rebuild without module
cd build && cmake .. && make -j8
```

## Logging

The module uses the EC_LOG_* macro system:

```cpp
EC_LOG_ERROR(fmt, ...)   // Level 0: Always logged
EC_LOG_WARN(fmt, ...)    // Level 1: Warnings
EC_LOG_INFO(fmt, ...)    // Level 2: Info messages
EC_LOG_DEBUG(fmt, ...)   // Level 3: Debug details
EC_LOG_TRACE(fmt, ...)   // Level 4: Very verbose
```

When debug mode is enabled (`.ecevent debug on`), messages also appear in-game for all players with GM level.

## Architecture

```
WorldScript::OnStartup()
    └── WorldEventManager::Initialize()
            └── LoadEventDefinitions() from DB

WorldScript::OnUpdate(diff)
    └── WorldEventManager::Update(diff)
            ├── CheckScheduledEvents()
            └── UpdateActiveEvents()
                    ├── Check duration/state
                    ├── Track participants
                    └── End events & distribute rewards

PlayerScript::OnPlayerUpdateZone()
    └── WorldEventManager::OnPlayerEnterZone()
            └── Add/remove participant tracking

PlayerScript::OnPlayerCreatureKill()
    └── WorldEventManager::OnPlayerKillCreature()
            └── Update kill counts

CommandScript
    └── .ecevent commands
            └── StartEvent(), StopEvent(), etc.
```

## Realtime Bridge Integration

This module integrates with `mod-ec-realtime-bridge` to stream events to external systems (Node.js, dashboards).

### Bridge Namespace

All events use namespace: `event`

### Events Emitted

| Event Type | Description | Payload Fields |
|------------|-------------|----------------|
| `event_started` | World event began | instanceId, eventId, eventName, eventType, zoneId, attackerFaction, defenderFaction |
| `event_ended` | World event finished | instanceId, eventId, eventName, result, durationMinutes, totalParticipants, totalKills |
| `participant_joined` | Player/bot joined event | instanceId, eventId, playerGuid, playerName, faction, isBot |
| `participant_left` | Player/bot left event | instanceId, eventId, playerGuid, playerName, reason |
| `participant_killed` | Participant died | instanceId, eventId, victimGuid, victimName, killerGuid, killerName |
| `boss_killed` | World boss defeated | instanceId, eventId, bossEntry, bossName, killerGuid, killerName |
| `event_escalated` | Reinforcements arrived | instanceId, eventId, botsAdded, totalBots, escalationLevel |
| `rewards_distributed` | Event rewards given | instanceId, eventId, rewardType, recipientCount, totalValue |

### Message Format

```json
{
  "ns": "event",
  "type": "event_started",
  "timestamp": 1706000000000,
  "data": {
    "instanceId": 1001,
    "eventId": 6,
    "eventName": "Crossroads Raid",
    "eventType": "raid",
    "zoneId": 17,
    "attackerFaction": "Alliance",
    "defenderFaction": "Horde",
    "minBots": 15,
    "maxBots": 40
  }
}
```

### Event Lifecycle

```
1. GM runs .ecevent start 6 (Crossroads Raid)
   → event_started { eventName: "Crossroads Raid", attackerFaction: "Alliance" }

2. Bots teleported to location
   → participant_joined (x15-40 for each bot)

3. Combat begins, kills tracked
   → participant_killed (as players/bots die)

4. Event escalates (more bots needed)
   → event_escalated { botsAdded: 10, escalationLevel: 1 }

5. Event timer expires or objective met
   → rewards_distributed { rewardType: "gold", totalValue: 5000 }
   → event_ended { result: "attacker_victory", totalKills: 47 }
```

### Defense Event Flow

```
1. Guard System detects zone fallen
   → Guard System calls: sECWorldEventMgr->ScheduleDefenseEvent()

2. After delay, defense event starts
   → event_started { eventType: "defense", eventName: "Crossroads Elite Defense" }

3. Elite bots spawned
   → participant_joined (for each elite bot)

4. Zone reclaimed or event fails
   → event_ended { result: "defense_success" | "defense_failed" }
```

### Node.js Subscription

```typescript
// In wow-control-backend
subscription.on("event:event_started", (event) => {
  console.log(`Event started: ${event.eventName} in zone ${event.zoneId}`);
  broadcastToDiscord(`🔔 ${event.eventName} has begun!`);
});

subscription.on("event:participant_killed", (event) => {
  updateKillFeed(event.victimName, event.killerName);
});

subscription.on("event:event_ended", (event) => {
  console.log(`Event ended: ${event.eventName} - ${event.result}`);
  logEventStats(event.instanceId, event.totalParticipants, event.totalKills);
});
```

### GraphQL Subscriptions

```graphql
subscription {
  worldEventUpdates {
    instanceId
    eventId
    eventName
    eventType
    status
    participantCount
    kills
    zoneId
  }
}

subscription {
  worldEventParticipants(instanceId: 1001) {
    playerGuid
    playerName
    kills
    deaths
    damageDealt
  }
}
```

---

## License

Copyright (C) 2026 EC
