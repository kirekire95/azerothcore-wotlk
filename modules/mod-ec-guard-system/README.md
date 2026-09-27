# EC Guard System - GTA-Style Wanted Levels for AzerothCore

A comprehensive guard response system that implements GTA-style wanted levels with faction-aware guard dispatch, escalating responses, and LocalDefence broadcasts.

## Jurisdiction safety

Guard, witness, phase, elite-response, and bounty-hunter NPCs are bound to the exact zone, map, instance, and offended faction that created the wanted response. A delayed report or a player teleport must not carry or recreate old-zone responders elsewhere. Stormwind (`1519`) and the Stormwind Stockades must therefore never receive Horde responders from an earlier Horde-zone crime.

Delayed witness alerts also retain the original area/map/instance and record the
crime without dispatching local guards if the criminal has already left. Both
bounty-hunter implementations despawn on a jurisdiction mismatch. Wanted levels
decay only while the owning player is online, keeping memory and persisted state
consistent.

`crime_faction` persists AzerothCore `TeamId` values: `0=Alliance`, `1=Horde`, `2=Neutral`. This differs from the world guard-configuration table's database-facing faction encoding. Do not convert existing wanted rows based on the old inverted schema comment.

`crime_area_id` persists the exact settlement area. When a dispatch row has a
nonzero `area_id`, legacy strategic phases and bounty hunters enforce that same
area as well; settlement events cannot follow a wanted player into another
outpost in the same larger zone.

Cross-zone hunter redistribution defaults off and should remain off:

```ini
ECGuard.HunterCrossZoneTracking = 0
```

Clearing or fully decaying a wanted level cancels pending bounty spawns and despawns active hunters. The Stockades recovery cleanup preserves affected wanted/bounty rows in dated recovery tables before clearing them.

## Native-safe preparation speech and incident recovery

Custom zone dialogue, dialogue pairs, and town-crier warnings are not native
WotLK behavior. They default off in configuration and their seed rows are also
disabled. Native civilian distress calls and ordinary GuardAI assistance are a
separate path and remain enabled.

`ECGuardSystem.Zone.PhaseBroadcastsEnabled = 0` also suppresses the custom
English phase barks and UNEASY nervous whispers/emotes/sounds.

If custom preparation speech is explicitly enabled later, it is bound to a
live wanted actor in the exact incident area/map/instance and offended faction.
Dialogue faction values use the world-table encoding (`0=Neutral`,
`1=Alliance`, `2=Horde`, `3=Any`) and must be explicitly converted from
AzerothCore `TeamId`. Town criers resolve only their configured creature entry;
there is no arbitrary-civilian or system-message fallback. Only humanoids can be
classified as civilian speakers, so critters, beasts, and schools of fish can
never speak preparation dialogue.

Every immediate or delayed NPC speech/emote boundary revalidates that the
speaker is alive, still in the world, and not being removed. AzerothCore's chat
APIs do not enforce this themselves. A lethal opening hit is handled by the kill
hook: the crime and local response still occur, but the dead civilian's response
is explicitly silent.

Zone heat saturates at `ECGuardSystem.Zone.MaxHeatPoints` (default `5000`). A
zone without a live criminal cannot re-enter a combat phase from stale heat.
The known corrupt Astranaar transient heat/alarm/control/phase row is cleared on
restart because its runtime actors were not persisted, while lifetime guard and
civilian death totals remain. Other zones retain their persisted state and use
the live-criminal phase guard above. The Astranaar row is backed up and repaired
by `2026_09_01_05_transient_zone_state_recovery.sql`; Astranaar's custom speech
rows are disabled by `2026_09_01_03_native_safe_preparation_speech.sql`.

## Town Civilian Defense

Explicit humanoid AzerothCore civilians (`CREATURE_FLAG_EXTRA_CIVILIAN`) with
an exact zone-and-faction guard configuration use the WotLK-compatible town
response. Animal creature types are always excluded even if a template carries
the generic civilian bit:

1. The first nonlethal damaging hit of each engagement makes the civilian yell
   `Guards! Help me!` in its faction language (Common or Orcish), so enemies see
   the client-scrambled language text.
2. The crime is registered immediately; it does not require a different NPC to
   survive the optional witness timer.
3. Permanent database guards keep their normal GuardAI assistance and movement.
   The civilian call can summon one or two temporary local reinforcements at
   native template level. A shared 10-second dispatch cooldown and per-player
   guard cap prevent summon spam; wanted-level escalation remains separate.
4. A kill is always registered for an explicit protected-town civilian, even if
   the damage hook was bypassed. Heuristic civilians outside configured towns
   continue to use the witness system.
   Pet and totem killing blows are attributed to their owning player.

Contested zones resolve the responding faction from the victim's relationship
to the attacker and require an exact faction row, never a single zone-wide guess
or neutral fallback. Exact neutral rows remain available for neutral towns.
Witnesses must be friendly to the victim. Temporary pursuit/alarm summons do
not change the strategic permanent-garrison count.

Ashenvale's legacy strategic state represents Astranaar area `415` only. Horde
guards and Alliance guards at other Ashenvale outposts remain valid guard
crimes, but cannot alter Astranaar's 11-Sentinel garrison or town heat.

Astranaar uses Astranaar Sentinel (`6087`) for permanent and temporary guard
responses. Darnassus Sentinel (`4262`) and Gagsprocket (`3495`) must never appear
in the Astranaar guard list. Live upgrades apply
`2026_09_01_01_astranaar_civilian_town_defense.sql` to the world database and
`2026_09_01_01_astranaar_guard_state_repair.sql` to the characters database.

The Barrens strategic state represents The Crossroads area `380` only. Its 14
permanent guards and temporary response entry are Horde Guard `3501`; Orgrimmar
Grunt `3296`, Razor Hill Grunt `5953`, and Den Grunt `5952` are not Crossroads
responders. The Crossroads migrations are
`2026_09_01_02_crossroads_guard_mapping.sql` and
`2026_09_01_02_crossroads_guard_state_repair.sql`.

The end-to-end runtime chain is visible at INFO level through
`CIVILIAN ATTACK ROUTE`, `CIVILIAN KILL ROUTE`, `TOWN CIVILIAN ALARM`,
`TOWN CIVILIAN ALARM RESPONSE`, `DispatchGuards`, and `Spawned guard` records.

## Principal Engineer Audit Status

**Status: COMPILED AND DEPLOYED ON MSI Z590 (2026-09-01)**

All critical issues have been identified and fixed:
- `GetCreatureListWithEntryInGrid` with entry=0 (fixed)
- `IsHostileTo(nullptr)` crash (fixed)
- `BuildChatPacket` missing locale parameter (fixed)
- `GetUInt32Value` replaced with `GetNpcFlags()` (fixed)
- Missing includes added (fixed)
- Unused function declaration removed (fixed)

---

## Installation Instructions

### Step 1: Copy the Module

Copy the entire `mod-ec-guard-system` folder to your AzerothCore modules directory:

```bash
# From your project root
cp -r azerothcore-wotlk/modules/mod-ec-guard-system /path/to/your/azerothcore/modules/
```

**Required Files:**
```
modules/mod-ec-guard-system/
├── mod-ec-guard-system.cmake        # CMake build config
├── README.md                        # This file
├── conf/
│   └── mod_ec_guard_system.conf.dist   # Config template
├── data/sql/
│   ├── db_world/
│   │   └── ec_guard_system_zone_config.sql
│   └── db_characters/
│       └── ec_guard_system_wanted.sql
└── src/
    ├── ec_guard_system_loader.cpp   # Module loader
    ├── ECWantedLevel.h              # Data structures
    ├── ECGuardSystemManager.h       # Manager header
    ├── ECGuardSystemManager.cpp     # Core implementation (~865 lines)
    └── ECGuardSystemScript.cpp      # Script hooks (~294 lines)
```

### Step 2: Rebuild AzerothCore

```bash
cd /path/to/azerothcore/build

# Clean rebuild recommended for new modules
rm -rf CMakeCache.txt CMakeFiles/

# Configure
cmake .. -DCMAKE_INSTALL_PREFIX=/path/to/azeroth-server

# Build (adjust -j based on your CPU cores)
make -j$(nproc)

# Install
make install
```

**Expected Output:**
```
-- AUTOLOAD: mod-ec-guard-system
--   EC Guard System: mod-playerbots NOT found, bot response disabled
```

If you have mod-playerbots installed, you'll see:
```
--   EC Guard System: mod-playerbots detected, enabling bot response
```

### Step 3: Apply SQL to Databases

**Apply to WORLD database:**
```bash
mysql -u root -p acore_world < modules/mod-ec-guard-system/data/sql/db_world/ec_guard_system_zone_config.sql
```

Or in MySQL client:
```sql
USE acore_world;
SOURCE /path/to/modules/mod-ec-guard-system/data/sql/db_world/ec_guard_system_zone_config.sql;
```

**Apply to CHARACTERS database (optional - for future persistence):**
```bash
mysql -u root -p acore_characters < modules/mod-ec-guard-system/data/sql/db_characters/ec_guard_system_wanted.sql
```

### Step 4: Copy Configuration

```bash
# Copy config to your worldserver config directory
cp modules/mod-ec-guard-system/conf/mod_ec_guard_system.conf.dist /path/to/worldserver.conf.d/mod_ec_guard_system.conf
```

Or if you use a single worldserver.conf, append the contents to it.

### Step 5: Restart Worldserver

```bash
# Stop worldserver
# Then start it again

# Check logs for:
# EC Guard System: Loaded 14 zone guard configurations in X ms
# EC Guard System: Initialized
```

---

## Configuration Reference

Edit `mod_ec_guard_system.conf`:

### Enable/Disable
```ini
ECGuardSystem.Enable = 1              # 1=Enabled, 0=Disabled
ECGuardSystem.Debug = 0               # Verbose logging
ECGuardSystem.DebugLogLevel = 2       # 0=ERROR, 1=WARN, 2=INFO, 3=DEBUG, 4=TRACE
ECGuardSystem.InGameLogging = 0       # Send logs to GMs in-game
```

### Crime Points
```ini
ECGuardSystem.CivilianAttackPoints = 50    # Points for attacking civilian
ECGuardSystem.CivilianKillPoints = 100     # Points for killing civilian
ECGuardSystem.GuardAttackPoints = 150      # Points for attacking guard
ECGuardSystem.GuardKillPoints = 300        # Points for killing guard
```

### Star Thresholds
```ini
ECGuardSystem.PointsFor1Star = 100   # ~1 civilian kill
ECGuardSystem.PointsFor2Star = 300   # ~3 civilians or 1 guard attack
ECGuardSystem.PointsFor3Star = 600   # ~1 guard kill
ECGuardSystem.PointsFor4Star = 1000  # Multiple guard kills
ECGuardSystem.PointsFor5Star = 1500  # Massacre
```

### Decay
```ini
ECGuardSystem.DecayDelaySeconds = 30     # Seconds before decay starts
ECGuardSystem.DecayRatePerSecond = 10    # Points removed per second
```

### Guard Spawning
```ini
ECGuardSystem.GuardSpawnCooldownMs = 10000          # 10 sec between spawns
ECGuardSystem.MaxActiveGuards = 6                   # Max guards per player
ECGuardSystem.GuardSearchRadius = 80.0              # Search for idle guards
ECGuardSystem.GuardPatrolDurationSeconds = 60       # Patrol before despawn
```

### LocalDefence
```ini
ECGuardSystem.BroadcastNPCDeaths = 1     # Announce kills to faction channel
```

---

## GM Commands

| Command | Permission | Description |
|---------|------------|-------------|
| `.ecguard status` | Player | Check your wanted status |
| `.ecguard status PlayerName` | Player | Check another player's status |
| `.ecguard clear` | GM | Clear your wanted level |
| `.ecguard clear PlayerName` | GM | Clear another player's wanted level |
| `.ecguard setstars 3` | GM | Set your wanted level to 3 stars |
| `.ecguard setstars PlayerName 5` | GM | Set player to 5 stars |
| `.ecguard reload` | Admin | Reload config and zone data |

---

## How It Works

### Wanted Level System

| Stars | Points | Guards | Type | Trigger |
|-------|--------|--------|------|---------|
| 0 | 0-99 | 0 | None | Clean |
| 1 | 100-299 | 1 | Regular | ~1 civilian kill |
| 2 | 300-599 | 2 | Regular | ~3 civilians |
| 3 | 600-999 | 3 | Elite | ~1 guard kill |
| 4 | 1000-1499 | 4 | Elite | Multiple guards |
| 5 | 1500+ | 5 | Elite | Massacre |

### Flow

1. **Player attacks NPC** → First hit detected via `UnitScript::OnDamage`
2. **Crime points added** → Based on civilian/guard, attack/kill
3. **NPC yells** → "Guards! Help!" or "Criminal scum!"
4. **Stars calculated** → Based on total crime points
5. **Guards dispatched**:
   - First: Search for idle guards within 80 yards
   - Alert them to attack player
   - If not enough found, spawn new guards
6. **LocalDefence broadcast** → On kill: "[ALERT] Player killed NPC in Zone!"
7. **Decay** → After 30s peace, points decay at 10/sec

### Zone Configuration

Pre-configured zones in `ec_guard_system_zone_config.sql`:

**Major Cities:**

| Zone | ID | Faction | Guards |
|------|----|---------|--------|
| Stormwind | 1519 | Alliance | 68, 1756 |
| Ironforge | 1537 | Alliance | 5595 |
| Darnassus | 1657 | Alliance | 4262 |
| Exodar | 3557 | Alliance | 16733 |
| Orgrimmar | 1637 | Horde | 3296, 14304 |
| Undercity | 1497 | Horde | 7980 |
| Thunder Bluff | 1638 | Horde | 3084 |
| Silvermoon | 3487 | Horde | 16222 |
| Shattrath | 3703 | Neutral | 19687 |
| Dalaran | 4395 | Neutral | 30659 |

**Neutral Towns:**

| Zone | ID | Guards |
|------|-----|--------|
| Booty Bay | 33 / area 35 | 4624 (Bruiser) |
| Gadgetzan | 440 / area 976 | 9460 (Bruiser) |
| Everlook | 618 / area 2255 | 11190 (Bruiser) |
| Ratchet | 17 / area 392 | 3502 (Bruiser) |

**Outdoor Alliance Zones:**

| Zone | ID | Guards |
|------|-----|--------|
| Elwynn Forest | 12 / area 87 | 1423 (Stormwind Guard) |
| Duskwood | 10 / area 42 | 10038 (Night Watch Guard) |
| Wetlands | 11 / area 150 | 1434 (Menethil Sentry) |
| Westfall | 40 / area 108 | 8096 (Protector of the People) |
| Redridge | 44 / area 69 | 10037 (Lakeshire Guard) |
| Dun Morogh | 1 / area 131 | 727 (Ironforge Mountaineer) |
| Loch Modan | 38 / area 144 | 8055 (Thelsamar Mountaineer) |
| Teldrassil | 141 / area 186 | 4262 (Sentinel) |
| Azuremyst | 3524 / area 3576 | 16733 (Peacekeeper) |
| Bloodmyst | 3525 / area 3584 | 16733 (Peacekeeper) |

**Outdoor Horde Zones:**

| Zone | ID | Guards |
|------|-----|--------|
| The Barrens | 17 / area 380 | 3501 (Horde Guard; Crossroads-native) |
| Durotar | 14 / area 362 | 5953 (Razor Hill Grunt) |
| Tirisfal Glades | 85 / area 159 | 7980 (Deathguard) |
| Silverpine | 130 / area 228 | 7980 (Deathguard) |
| Eversong Woods | 3430 / area 3462 | 16222 (Guardian) |
| Ghostlands | 3433 / area 3488 | 16222 (Guardian) |

**Contested Zones:**

| Zone | ID | Alliance | Horde |
|------|-----|----------|-------|
| Ashenvale | 331 | 6087 (area 415) | 12903 (area 431) |
| Hillsbrad | 267 | 2386 (area 271) | 2405 (area 272) |
| Stranglethorn | 33 | - | 1064 (area 117) |

Outdoor rows are settlement scoped. `area_id = 0` is reserved for true city or
whole-zone mappings; a nonzero value must belong to `zone_id`. On startup the
module rejects invalid area pairs, missing creature templates, service NPCs,
and templates that have neither AzerothCore's native guard flag nor a safe
humanoid/undead guard-like fallback. Runtime classification and spawning check
the caller/player area again, preventing Razor Hill or Astranaar responses from
appearing elsewhere in Durotar or Ashenvale.

Bloodhoof Village and Auberdine are deliberately not configured: Bloodhoof has
no generic local guard template, while Auberdine's only local generic sentinel
retains a questgiver service flag. The cleanup does not borrow Thunder Bluff or
Darnassus templates for them. Elite faction-response groups are likewise
limited to true city-zone IDs because that legacy loader has no area whitelist;
outdoor towns continue to use the ordinary area-scoped response.

**Add more zones:**
```sql
INSERT INTO `ec_guard_system_zone_config` VALUES
(zone_id, area_id, faction, guard_entry_1, guard_entry_2, elite_entry_1, captain_entry, search_radius);
-- faction: 0=Neutral, 1=Alliance, 2=Horde
-- area_id: 0 only for a true whole-zone/city mapping
```

---

## Debug Logging

Enable maximum debug output:

```ini
ECGuardSystem.Debug = 1
ECGuardSystem.DebugLogLevel = 4      # TRACE
ECGuardSystem.InGameLogging = 1      # Show to GMs
```

**Log format:** `[ECGuard][LEVEL][Function] Message`

**In-game colors:**
- Red: ERROR
- Orange: WARNING
- Green: INFO
- Cyan: DEBUG
- Gray: TRACE

**Log file:** Check your worldserver logs for `[ECGuard]` entries.

---

## Troubleshooting

### "No zone guard configurations found"
SQL not applied. Run:
```bash
mysql -u root -p acore_world < ec_guard_system_zone_config.sql
```

### Guards not spawning
1. Check if system is enabled: `.ecguard reload`
2. Check debug logs for zone config issues
3. Verify guard creature entries exist in `creature_template`

### Wanted level not decaying
- Player must be out of combat
- Must wait 30 seconds (configurable)
- Check if player is still in combat with spawned guards

### LocalDefence not broadcasting
- Only broadcasts for faction zones (not neutral)
- Player must actually kill the NPC (not just attack)
- Check `ECGuardSystem.BroadcastNPCDeaths = 1`

---

## Rollback

Each SQL file includes rollback at the bottom:

```sql
-- To remove:
/*
DROP TABLE IF EXISTS `ec_guard_system_zone_config`;
DROP TABLE IF EXISTS `ec_guard_system_wanted`;
*/
```

To completely remove the module:
1. Delete `modules/mod-ec-guard-system/`
2. Rebuild AzerothCore
3. Run the rollback SQL

---

## File Checksums

Verify your files match (line counts):
```
ECGuardSystemManager.cpp    ~865 lines
ECGuardSystemManager.h      ~172 lines
ECGuardSystemScript.cpp     ~294 lines
ECWantedLevel.h             ~165 lines
ec_guard_system_loader.cpp  ~16 lines
```

---

## Realtime Bridge Integration

This module integrates with `mod-ec-realtime-bridge` to stream events to external systems (Node.js, dashboards).

### Bridge Namespace

All events use namespace: `guard`

### Events Emitted

| Event Type | Description | Payload Fields |
|------------|-------------|----------------|
| `crime_committed` | Player attacked/killed NPC | playerGuid, playerName, victimEntry, victimName, crimeType, pointsAdded, zoneId |
| `wanted_level_changed` | Player's star level changed | playerGuid, playerName, oldStars, newStars, totalPoints, zoneId |
| `guards_dispatched` | Guards sent to player | playerGuid, playerName, guardCount, guardType, zoneId |
| `guards_defeated` | Player killed responding guards | playerGuid, playerName, guardsKilled, zoneId |
| `wanted_cleared` | Wanted level decayed to 0 | playerGuid, playerName, decayedFrom, zoneId |
| `zone_phase_changed` | Zone control state changed | zoneId, zoneName, oldPhase, newPhase, faction |

### Message Format

```json
{
  "ns": "guard",
  "type": "wanted_level_changed",
  "timestamp": 1706000000000,
  "data": {
    "playerGuid": 12345,
    "playerName": "CriminalPlayer",
    "oldStars": 2,
    "newStars": 3,
    "totalPoints": 650,
    "zoneId": 1519
  }
}
```

### Example Flow

```
1. Player attacks civilian in Stormwind
   → crime_committed { crimeType: "civilian_attack", pointsAdded: 50 }

2. Points reach 100, star level increases
   → wanted_level_changed { oldStars: 0, newStars: 1, totalPoints: 100 }

3. Guards dispatched
   → guards_dispatched { guardCount: 1, guardType: "regular" }

4. Player kills guard, points increase
   → crime_committed { crimeType: "guard_kill", pointsAdded: 300 }
   → wanted_level_changed { oldStars: 1, newStars: 3, totalPoints: 400 }

5. More elite guards dispatched
   → guards_dispatched { guardCount: 3, guardType: "elite" }
```

### Node.js Subscription

```typescript
// In wow-control-backend
subscription.on("guard:wanted_level_changed", (event) => {
  console.log(`${event.playerName} now at ${event.newStars} stars!`);
});

subscription.on("guard:zone_phase_changed", (event) => {
  if (event.newPhase === "FALLEN") {
    // Trigger defense event via mod-ec-world-events
    triggerDefenseEvent(event.zoneId, event.faction);
  }
});
```

### GraphQL Subscriptions

```graphql
subscription {
  guardSystemEvents {
    eventType
    timestamp
    playerGuid
    playerName
    stars
    zoneId
  }
}
```

---

## Credits

- **EC** - Author
- **AzerothCore Team** - Core framework
- **GTA Series** - Inspiration for wanted level system
