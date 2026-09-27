# GTA-Style Arrest & Jail System Design

## Overview

Extends mod-ec-guard-system with vehicle-based arrests and jail mechanics.

## Integration Points

```
Existing System                    New System
─────────────────                  ──────────────
ECGuardSystemManager    ────────►  ECArrestManager
   │ WantedState                      │ ArrestState
   │ DispatchGuards()                 │ StartArrest()
   │ IsPlayerWanted()                 │ DriveToJail()
   ▼                                  ▼
ECBountyHunterManager              ECJailManager
   │ SpawnHunter()                    │ JailLocations
   │ TrackTarget()                    │ Punishments
   ▼                                  ▼
ECZoneState                        ECPoliceCar (Vehicle)
   │ SecurityLevel                    │ Driver AI
   │ GuardCount                       │ Waypoint paths
```

## Core Components

### 1. ECArrestManager

```cpp
class ECArrestManager
{
public:
    static ECArrestManager* instance();

    // Arrest flow
    void CheckArrestConditions(Player* player);
    void StartArrest(Player* player, Creature* arrestingGuard);
    void SpawnPoliceCar(Player* player);
    void ForceIntoVehicle(Player* player, Creature* car);
    void DriveToJail(Creature* car, Player* prisoner);
    void ReleaseFromVehicle(Player* player);
    void ProcessJailSentence(Player* player);

    // State tracking
    bool IsPlayerUnderArrest(ObjectGuid playerGuid);
    ArrestState* GetArrestState(ObjectGuid playerGuid);

private:
    std::unordered_map<ObjectGuid, ArrestState> m_arrestStates;
};

struct ArrestState
{
    ObjectGuid playerGuid;
    ObjectGuid arrestingGuardGuid;
    ObjectGuid policeCarGuid;
    ArrestPhase phase;          // SUBDUING, IN_VEHICLE, TRANSPORTING, JAILED
    uint32 jailTimeRemaining;   // ms
    Position arrestLocation;
    uint32 jailLocationId;
};

enum class ArrestPhase
{
    NONE,
    SUBDUING,       // Player being stunned/rooted
    IN_VEHICLE,     // Player forced into vehicle
    TRANSPORTING,   // Vehicle driving to jail
    JAILED,         // Player in jail cell
    RELEASED        // Sentence complete
};
```

### 2. ECJailManager

```cpp
class ECJailManager
{
public:
    static ECJailManager* instance();

    void LoadJailLocations();
    const JailLocation* GetNearestJail(uint32 mapId, Position pos);
    void TeleportToJail(Player* player, uint32 jailId);
    void StartJailTimer(Player* player, uint32 durationMs);
    void ReleaseFromJail(Player* player);
    void ApplyJailPunishment(Player* player, WantedStars stars);

private:
    std::map<uint32, JailLocation> m_jailLocations;
};

struct JailLocation
{
    uint32 id;
    std::string name;           // "Stormwind Stockade", "Orgrimmar Holding"
    uint32 mapId;
    Position cellPosition;
    Position releasePosition;
    TeamId faction;
    uint32 defaultSentenceMs;   // Base jail time
};

struct JailPunishment
{
    WantedStars stars;
    uint32 jailTimeMs;          // Jail duration
    uint32 goldFine;            // Gold penalty
    uint32 debuffSpellId;       // Debuff applied on release
    uint32 debuffDurationMs;    // How long debuff lasts
};
```

### 3. ECPoliceCar (Vehicle System)

```cpp
// Database entries needed:
// creature_template - Police car vehicle
// vehicle_template_accessory - Driver seat (NPC)
// vehicle_template_accessory - Prisoner seat (Player)

class ECPoliceCarAI : public CreatureAI
{
public:
    ECPoliceCarAI(Creature* creature);

    void StartTransport(Player* prisoner, uint32 jailId);
    void OnWaypointReached(uint32 waypointId);
    void OnArrivalAtJail();

private:
    ObjectGuid m_prisonerGuid;
    uint32 m_targetJailId;
    uint32 m_pathId;
};
```

## Arrest Flow

### Phase 1: Detection & Subduing

```
Player HP < 10% OR Surrender command
           │
           ▼
Guard calls StartArrest(player)
           │
           ▼
Cast SPELL_ARREST_STUN (10s stun)
Cast SPELL_ARREST_ROOT (10s root)
           │
           ▼
Guard plays emote: "You're under arrest!"
Player forced to kneel (EMOTE_ONESHOT_KNEEL)
```

### Phase 2: Vehicle Entry

```
SpawnPoliceCar(nearPlayer, orientation)
           │
           ▼
AssignDriver(car, guardNPC)
           │
           ▼
player->EnterVehicle(car, SEAT_PRISONER)
           │
           ▼
Lock player controls:
  - Disable movement
  - Disable spells
  - Disable logout
  - Disable hearthstone
```

### Phase 3: Transport

```
car->GetMotionMaster()->MovePath(jailPathId)
           │
           ▼
[Vehicle drives along waypoints]
           │
           ▼
OnArrivalAtJail():
  - player->ExitVehicle()
  - RemoveStun()
  - TeleportToJailCell()
  - DespawnCar()
```

### Phase 4: Jail Sentence

```
TeleportToJailCell(player, jailId)
           │
           ▼
ApplyPunishment(player, wantedStars):
  - DeductGold(fineAmount)
  - ApplyDebuff(SPELL_CRIMINAL_RECORD)
  - StartTimer(jailDuration)
           │
           ▼
ClearWantedLevel(player)
           │
           ▼
[Timer expires]
           │
           ▼
TeleportToRelease(player)
SendMessage("You have served your sentence.")
```

## Database Schema

### ec_jail_locations

```sql
CREATE TABLE ec_jail_locations (
    id INT PRIMARY KEY,
    name VARCHAR(64),
    map_id INT,
    cell_x FLOAT,
    cell_y FLOAT,
    cell_z FLOAT,
    cell_o FLOAT,
    release_x FLOAT,
    release_y FLOAT,
    release_z FLOAT,
    release_o FLOAT,
    faction TINYINT,  -- 0=Alliance, 1=Horde, 2=Neutral
    default_sentence_ms INT DEFAULT 60000
);

-- Default jails
INSERT INTO ec_jail_locations VALUES
(1, 'Stormwind Stockade', 0, -8765, 846, 87, 0, -8753, 847, 96, 0, 0, 60000),
(2, 'Orgrimmar Hold', 1, 1632, -4435, 15, 0, 1650, -4420, 26, 0, 1, 60000),
(3, 'Booty Bay Brig', 0, -14438, 476, 15, 0, -14445, 464, 36, 0, 2, 30000);
```

### ec_jail_punishments

```sql
CREATE TABLE ec_jail_punishments (
    stars TINYINT PRIMARY KEY,
    jail_time_ms INT,
    gold_fine INT,
    debuff_spell_id INT,
    debuff_duration_ms INT
);

INSERT INTO ec_jail_punishments VALUES
(1, 30000,   100,  0,     0),      -- 30s, 1g
(2, 60000,   500,  0,     0),      -- 1min, 5g
(3, 120000,  2000, 80354, 300000), -- 2min, 20g, 5min debuff
(4, 300000,  5000, 80354, 600000), -- 5min, 50g, 10min debuff
(5, 600000, 10000, 80354, 1800000);-- 10min, 100g, 30min debuff
```

### ec_police_car_paths

```sql
CREATE TABLE ec_police_car_paths (
    id INT,
    point_id INT,
    map_id INT,
    x FLOAT,
    y FLOAT,
    z FLOAT,
    delay_ms INT DEFAULT 0,
    PRIMARY KEY (id, point_id)
);
```

### Vehicle Template

```sql
-- Police Car vehicle
INSERT INTO creature_template (entry, name, modelid1, vehicleId, AIName, ScriptName) VALUES
(900001, 'Police Transport', 25278, 900001, '', 'npc_ec_police_car');

-- Vehicle seats
INSERT INTO vehicle_template_accessory (entry, accessory_entry, seat_id, minion, description) VALUES
(900001, 900002, 0, 1, 'Police Driver'),  -- Seat 0: Driver NPC
(900001, 0, 1, 0, 'Prisoner Seat');       -- Seat 1: Player prisoner
```

## Spells

### Custom Spells Needed

| Spell ID | Name | Effect |
|----------|------|--------|
| 80350 | Arrest Stun | 10s stun, unbreakable |
| 80351 | Arrest Root | 10s root, unbreakable |
| 80352 | Prisoner Aura | Disables logout/HS |
| 80353 | Jail Aura | Prevents leaving area |
| 80354 | Criminal Record | -10% stats debuff |

## Configuration

### mod_ec_guard_system.conf

```conf
# Arrest System
ECGuard.ArrestEnabled = 1
ECGuard.ArrestHPThreshold = 10
ECGuard.ArrestDuration = 10000
ECGuard.PoliceCarEntry = 900001

# Jail System
ECGuard.JailEnabled = 1
ECGuard.JailDefaultDuration = 60000
ECGuard.JailGoldFineMultiplier = 1.0
ECGuard.JailDebuffEnabled = 1

# Vehicle Transport
ECGuard.VehicleTransportEnabled = 1
ECGuard.VehicleSpeed = 1.0
ECGuard.TeleportIfNoPath = 1
```

## Commands

```
.ecarrest start <player>     - Manually arrest player (GM)
.ecarrest release <player>   - Release from jail (GM)
.ecarrest status             - Show arrest/jail status
.ecjail list                 - List jail locations
.ecjail teleport <id>        - Teleport to jail
```

## Events (Bridge Integration)

```json
// Arrest started
{
  "ns": "guard",
  "type": "arrest_started",
  "data": {
    "playerGuid": 123,
    "playerName": "CriminalScum",
    "guardGuid": 456,
    "wantedStars": 3,
    "location": {"x": -8833, "y": 630, "z": 95}
  }
}

// Transport started
{
  "ns": "guard",
  "type": "transport_started",
  "data": {
    "playerGuid": 123,
    "vehicleGuid": 789,
    "destinationJail": "Stormwind Stockade"
  }
}

// Jailed
{
  "ns": "guard",
  "type": "player_jailed",
  "data": {
    "playerGuid": 123,
    "jailId": 1,
    "jailName": "Stormwind Stockade",
    "sentenceMs": 120000,
    "goldFine": 2000
  }
}

// Released
{
  "ns": "guard",
  "type": "player_released",
  "data": {
    "playerGuid": 123,
    "jailId": 1,
    "timeServed": 120000
  }
}
```

## Implementation Phases

### Phase 1: Simple (No Vehicle)
- Stun player
- Teleport to jail
- Timer-based release

### Phase 2: Fake Vehicle
- Spawn "vehicle" model near player
- Mount animation
- Screen fade → teleport
- Vehicle despawns

### Phase 3: Full System
- Real vehicle mechanics
- Waypoint driving
- Multiple jail locations
- Full punishment system

## Security Considerations

1. **Prevent logout during arrest** - Apply PRISONER aura
2. **Disable hearthstone** - Block spell during PRISONER aura
3. **Disable teleport spells** - Block mage portals, etc.
4. **Validate vehicle state** - Ensure player stays in vehicle
5. **Anti-exploit timer** - Force-jail if vehicle stuck >30s
6. **GM immunity** - GMs cannot be arrested (configurable)
