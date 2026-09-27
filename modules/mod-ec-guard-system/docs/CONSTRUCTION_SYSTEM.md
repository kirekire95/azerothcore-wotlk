# Peon Construction System

## Overview

The Peon Construction System adds Warcraft III-style building mechanics to the EC Guard System. During zone phase escalations, workers (Horde Peons or Alliance Peasants) spawn and build defensive structures by carrying lumber from piles to construction sites.

## Features

- **Phase-Triggered Building**: Construction automatically starts when zones enter UNEASY phase
- **Warcraft III Voice Lines**: Workers say iconic lines like "Job's done!", "Zug zug!", "More work?"
- **Visual Equipment**: Workers visually carry lumber and use tools
- **Work Animations**: Workers play `EMOTE_STATE_WORK_CHOPWOOD` while building
- **Faction-Appropriate Workers**: Horde zones get Peons, Alliance zones get Peasants
- **Interactive Objects**: Capture points, traps, guardposts spawn during escalation

---

## Architecture

### Data Flow

```
Zone Phase Escalation (UNEASY/MOBILIZING)
         |
         v
ECZonePreparation::SpawnConstructionWorkers()
         |
         +---> Spawn workers at lumber pile
         |     Emit: construction_started, worker_spawned
         |
         v
ECZonePreparation::UpdateConstructionWorkers() (called every tick)
         |
         +---> Worker picks up lumber -> walks to site -> works -> repeat
         |     Emit: worker_say (lumber, working)
         |     Emit: construction_progress
         |
         v
ECZonePreparation::CompleteConstruction()
         |
         +---> Spawn structure gameobject
         |     Workers cheer and say "Job's done!"
         |     Emit: construction_complete, worker_say (done)
         |
         v
Zone Phase IMPACT
         |
         v
ECZonePreparation::DespawnConstructionWorkers()
         |
         +---> Workers flee and despawn
              Emit: workers_fleeing
```

### Database Tables

| Table | Database | Purpose |
|-------|----------|---------|
| `ec_guard_construction_sites` | acore_world | Construction site positions, lumber pile locations |
| `ec_guard_worker_templates` | acore_world | Worker NPC configs, voice lines (pipe-delimited) |
| `ec_guard_interactive_objects` | acore_world | Capture points, traps, guardposts |
| `ec_guard_debris_templates` | acore_world | Destructible building entries |

### C++ Files

| File | Purpose |
|------|---------|
| `ECZonePreparation.h` | Struct definitions, method declarations |
| `ECZonePreparation.cpp` | Worker lifecycle, construction logic |
| `ECGuardSystemBridge.h/cpp` | Bridge emit methods for Node.js communication |

### Node.js Files

| File | Purpose |
|------|---------|
| `pubsub-integration.ts` | Routes construction events to GraphQL PubSub |
| `pubsub.ts` | `GuardConstructionEventData`, `GuardInteractiveObjectEventData` interfaces |
| `guardsystem.ts` (types) | `ConstructionEventType`, `InteractiveObjectEventType` GraphQL types |
| `guardsystem.ts` (subscriptions) | `guardSystemConstruction`, `guardSystemInteractiveObject` subscriptions |

---

## SQL Schema

### ec_guard_construction_sites

```sql
CREATE TABLE IF NOT EXISTS `ec_guard_construction_sites` (
  `id` INT UNSIGNED NOT NULL AUTO_INCREMENT PRIMARY KEY,
  `zone_id` INT UNSIGNED NOT NULL,
  `site_name` VARCHAR(100) NOT NULL DEFAULT '',
  `structure_entry` INT UNSIGNED NOT NULL,      -- GO entry when complete
  `structure_type` TINYINT UNSIGNED NOT NULL,   -- PrepGameObjectType enum
  `build_x` FLOAT, `build_y` FLOAT, `build_z` FLOAT, `build_o` FLOAT,
  `lumber_x` FLOAT, `lumber_y` FLOAT, `lumber_z` FLOAT,  -- Lumber pile
  `trips_required` TINYINT UNSIGNED DEFAULT 3,
  `min_phase` TINYINT UNSIGNED DEFAULT 1,       -- 1=UNEASY, 2=MOBILIZING
  `workers_needed` TINYINT UNSIGNED DEFAULT 2,
  `faction` TINYINT UNSIGNED DEFAULT 3,         -- 1=alliance, 2=horde, 3=any
  `priority` TINYINT UNSIGNED DEFAULT 5,
  `enabled` TINYINT(1) DEFAULT 1
);
```

### ec_guard_worker_templates

```sql
CREATE TABLE IF NOT EXISTS `ec_guard_worker_templates` (
  `id` INT UNSIGNED NOT NULL AUTO_INCREMENT PRIMARY KEY,
  `faction` TINYINT UNSIGNED NOT NULL,          -- 1=alliance, 2=horde
  `creature_entry` INT UNSIGNED NOT NULL,
  `lumber_display_id` INT UNSIGNED DEFAULT 0,   -- Visual for carrying lumber
  `tool_display_id` INT UNSIGNED DEFAULT 0,     -- Visual for building
  `walk_speed` FLOAT DEFAULT 1.0,
  `say_start` TEXT,    -- Pipe-delimited: "Zug zug!|Ready to work!"
  `say_working` TEXT,  -- Pipe-delimited: "More work?|Work work..."
  `say_done` TEXT,     -- Pipe-delimited: "Job's done!|Work complete!"
  `say_lumber` TEXT,   -- Pipe-delimited: "I got the lumber!|Got the wood!"
  `say_flee` TEXT,     -- Pipe-delimited: "Run away!|Help!"
  `enabled` TINYINT(1) DEFAULT 1
);
```

### ec_guard_interactive_objects

```sql
CREATE TABLE IF NOT EXISTS `ec_guard_interactive_objects` (
  `id` INT UNSIGNED NOT NULL AUTO_INCREMENT PRIMARY KEY,
  `zone_id` INT UNSIGNED NOT NULL,
  `gameobject_entry` INT UNSIGNED NOT NULL,
  `gameobject_type` TINYINT UNSIGNED NOT NULL,  -- 6=TRAP, 12=AREADAMAGE, 21=GUARDPOST, 29=CAPTURE_POINT
  `spawn_x` FLOAT, `spawn_y` FLOAT, `spawn_z` FLOAT, `spawn_o` FLOAT,
  `spawn_on_phase` TINYINT UNSIGNED DEFAULT 0,  -- 0=always, 1-4=phase
  `despawn_on_phase` TINYINT UNSIGNED DEFAULT 0,
  `linked_construction_id` INT UNSIGNED DEFAULT NULL,  -- FK to construction_sites
  `faction` TINYINT UNSIGNED DEFAULT 3,
  `description` VARCHAR(255) DEFAULT '',
  `enabled` TINYINT(1) DEFAULT 1
);
```

---

## Worker State Machine

```
                         +-----------------+
                         |   SPAWNED AT    |
                         |  LUMBER PILE    |
                         +--------+--------+
                                  |
                                  v
                    +-------------+-------------+
                    |   PICK UP LUMBER          |
                    |   Equip lumber visual     |
                    |   Say "I got the lumber!" |
                    +-------------+-------------+
                                  |
                                  v
                    +-------------+-------------+
                    |   WALK TO BUILD SITE      |
                    |   carryingLumber = true   |
                    +-------------+-------------+
                                  |
                                  v
                    +-------------+-------------+
                    |   ARRIVED AT SITE         |
                    |   Equip tool visual       |
                    |   Play WORK_CHOPWOOD      |
                    +-------------+-------------+
                                  |
                                  v
                    +-------------+-------------+
                    |   WORKING (3-5 sec)       |
                    |   isWorking = true        |
                    |   Say "More work?"        |
                    +-------------+-------------+
                                  |
                                  v
                    +-------------+-------------+
                    |   TRIP COMPLETE           |
                    |   tripsCompleted++        |
                    +-------------+-------------+
                                  |
                     +------------+------------+
                     |                         |
        totalTrips < required         totalTrips >= required
                     |                         |
                     v                         v
           +-------------------+    +---------------------+
           | WALK BACK TO      |    | CONSTRUCTION DONE   |
           | LUMBER PILE       |    | Spawn structure GO  |
           | (repeat cycle)    |    | Say "Job's done!"   |
           +-------------------+    | Cheer emote         |
                                    +---------------------+
```

---

## Voice Lines

### Horde Peons (creature_entry: 10556)

| Event | Lines |
|-------|-------|
| Spawn | "Ready to work!", "Zug zug!", "Something need doing?", "Dabu!" |
| Lumber | "I got the lumber!", "Got the wood!", "Okie dokie", "Lumber ready!" |
| Working | "More work?", "Work work...", "Me busy, leave me alone!", "Okie dokie" |
| Done | "Job's done!", "Work complete!", "I can do that!" |
| Flee | "Me not that kind of orc!", "Run away!", "Help!" |

### Alliance Peasants (creature_entry: 1976)

| Event | Lines |
|-------|-------|
| Spawn | "Yes, milord?", "What is it?", "Right-o!", "More work?" |
| Lumber | "Lumber ready!", "Got the timber!", "Yes, milord" |
| Working | "More work?", "What else?", "Hmm?", "Yes, milord" |
| Done | "Job's done!", "All done!", "Right then!" |
| Flee | "Help! Help!", "We're under attack!", "Run for your lives!" |

---

## Bridge Events

All events are sent through EC Realtime Bridge (port 8092) with namespace `guard`.

| Event Type | Data Fields |
|------------|-------------|
| `construction_started` | zoneId, siteId, siteName, structureEntry, workersAssigned, tripsRequired |
| `construction_progress` | zoneId, siteId, siteName, tripsCompleted, tripsRequired |
| `construction_complete` | zoneId, siteId, siteName, structureEntry |
| `worker_spawned` | zoneId, workerEntry, workerName, siteId, siteName |
| `worker_say` | zoneId, workerName, text, sayType |
| `workers_fleeing` | zoneId, workerCount |
| `interactive_object_spawned` | zoneId, goEntry, goType, description |
| `interactive_object_despawned` | zoneId, goEntry, description |

---

## GraphQL Subscriptions

### guardSystemConstruction

```graphql
subscription {
  guardSystemConstruction {
    eventType       # construction_started, progress, complete, worker_spawned, worker_say, workers_fleeing
    zoneId
    siteId
    siteName
    structureEntry
    workersAssigned
    tripsCompleted
    tripsRequired
    percentComplete
    workerEntry
    workerName
    text            # Voice line text
    sayType         # start, working, done, lumber, flee
    workerCount     # For workers_fleeing
    timestamp
  }
}
```

### guardSystemInteractiveObject

```graphql
subscription {
  guardSystemInteractiveObject {
    eventType       # interactive_object_spawned, interactive_object_despawned
    zoneId
    goEntry
    goType          # trap, area_damage, guardpost, capture_point, destructible_building
    goTypeId        # WoW gameobject type ID
    description
    timestamp
  }
}
```

---

## Configuration

In `mod_ec_guard_system.conf`:

```conf
# Enable/disable construction system
ECGuardSystem.Preparation.ConstructionEnabled = 1

# Max workers per zone
ECGuardSystem.Preparation.MaxWorkersPerZone = 10

# Work duration in milliseconds (time at build site per trip)
ECGuardSystem.Preparation.WorkDurationMs = 3000

# Enable/disable interactive objects
ECGuardSystem.Preparation.InteractiveObjectsEnabled = 1
```

---

## Zone Phase Lifecycle

```
QUIET
  - Construction not active
  - Interactive objects not spawned

UNEASY (min_phase = 1)
  - Construction sites with min_phase=1 start
  - Workers spawn at lumber piles
  - Interactive objects with spawn_on_phase=1 appear

MOBILIZING (min_phase = 2)
  - Remaining construction sites start
  - Additional workers may spawn
  - More interactive objects appear

IMPACT
  - Workers flee ("Run away!")
  - Workers despawn after 5 seconds
  - Completed structures remain
  - Traps and area damage activate

AFTERMATH
  - Construction resets
  - Workers return (optional repair mechanic)
  - Interactive objects may despawn
```

---

## Adding New Construction Sites

1. Find a zone with guard system support
2. Identify lumber pile position (near trees, sawmill, etc.)
3. Identify construction site position (strategic location)
4. Add row to `ec_guard_construction_sites`:

```sql
INSERT INTO `ec_guard_construction_sites`
  (zone_id, site_name, structure_entry, structure_type,
   build_x, build_y, build_z, build_o,
   lumber_x, lumber_y, lumber_z,
   trips_required, min_phase, workers_needed, faction, priority)
VALUES
  (17, 'New Watchtower', 35251, 15,
   -440.0, -2630.0, 96.5, 0.0,
   -470.0, -2615.0, 96.0,
   3, 1, 2, 2, 5);
```

---

## Troubleshooting

### Workers not spawning
- Check zone has construction sites defined: `SELECT * FROM ec_guard_construction_sites WHERE zone_id = X`
- Check worker templates exist for faction: `SELECT * FROM ec_guard_worker_templates WHERE faction = X`
- Verify zone is in UNEASY or MOBILIZING phase

### Workers not moving
- Ensure `FindPlayerInZone()` can find a player (needed for map access)
- Check worker is not stuck on terrain
- Verify lumber pile position is navigable

### No voice lines
- Check `say_*` columns in `ec_guard_worker_templates` are not empty
- Voice lines must be pipe-delimited: `"Line1|Line2|Line3"`

### Structure not spawning
- Verify `structure_entry` exists in `gameobject_template`
- Check all trips were completed
- Look for errors in worldserver log

### Events not reaching frontend
- Verify EC Realtime Bridge is connected (port 8092)
- Check `pubsub-integration.ts` has case for event type
- Verify GraphQL subscription is subscribed

---

## Files Modified (Audit Summary)

### SQL Files (ec-modules/mod-ec-guard-system/data/sql/db_world/)
- `ec_guard_construction_sites.sql` - Construction site definitions
- `ec_guard_worker_templates.sql` - Worker NPC configs with voice lines
- `ec_guard_interactive_objects.sql` - Interactive gameobject definitions
- `ec_guard_debris_templates.sql` - Expanded with destructible entries

### C++ Files (ec-modules/mod-ec-guard-system/src/)
- `ECZonePreparation.h` - Structs: ConstructionSite, WorkerTemplate, WorkerState, ActiveConstruction, InteractiveObjectTemplate
- `ECZonePreparation.cpp` - Full worker lifecycle implementation (~800 lines added)
- `ECGuardSystemBridge.h/cpp` - 8 emit methods for construction/interactive events

### Node.js Files (client/apps/wow-control-backend/server/)
- `services/realtime-bridge/pubsub-integration.ts` - Event routing
- `pubsub.ts` - Interfaces and channel definitions
- `schema/types/guardsystem.ts` - GraphQL types
- `schema/subscriptions/guardsystem.ts` - GraphQL subscriptions

### CLI Files (scripts/ec-sql/)
- `ec-sql.sh` - Registered 4 new SQL files, 4 new table names
