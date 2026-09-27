# mod-gtapolice v2

GTA-style police pursuit, arrest, and jail system for AzerothCore 3.3.5.

## MSI runtime status

`mod-gtapolice` is compiled but intentionally disabled on `msi-z590` as of 2026-09-01. Police-car creature `90011` has `VehicleId = 0` because the required custom `Vehicle.dbc`/`VehicleSeat.dbc` row `9001` is not deployed to every 3.3.5a client; referencing the missing row has frozen clients. The old no-vehicle fallback directly jailed players without an arresting officer or transport and is prohibited.

```ini
GTAPolice.Enable = 0
GTAPolice.AffectPlayerbots = 0
GTAPolice.AllowDirectBookingWithoutTransport = 0
```

Do not re-enable this module until the server and every client have validated matching vehicle DBC data and a visible officer, crew, transport, arrival, and release flow passes end-to-end testing. Direct booking must remain disabled even after transport is restored.

## Features Overview

### Core Pursuit System
- **Police Officers** - Scan for wanted players (configurable range/interval)
- **Police Cars** - Vehicle pursuit with driver + crew officers
- **Crew Officers** - Exit vehicle, approach on foot, perform arrest
- **Transport** - Car drives arrested player to jail
- **Jail System** - Faction-based jail locations with time/gold punishment

### Multi-Car Escalation
- **Star-based dispatch** - More cars at higher wanted levels
- **Reinforcements** - Additional cars spawn during long pursuits
- **Configurable scaling** - 1-4 cars based on wanted stars

### Star Decay System
- **Grace period** - No decay while actively committing crimes
- **Evasion timer** - Stars decay after evading pursuit
- **Configurable rates** - Customize decay interval and cooldowns

### Zone Heat System
- **Heat accumulation** - Crimes increase zone heat
- **Heat decay** - Zones cool down over time
- **Heat levels** - Cold (0-29), Warm (30-79), Hot (80+)
- **Officer behavior** - Hotter zones = more aggressive response

### Escape Options
- **Bribe System** - Pay gold to reduce wanted level (cooldown-based)
- **Pay-n-Spray** - Visit shady NPCs to clear wanted status for gold

### Integration
- **mod-ec-guard-system** - Uses guard wanted stars instead of spell aura
- **mod-ec-realtime-bridge** - Emits events for GraphQL subscriptions
- **Witness System** - Witness events affect zone heat

## NPC Entries

| Entry | Name | ScriptName | Purpose |
|-------|------|------------|---------|
| 90010 | Police Officer | `npc_gtapolice_officer` | Scans for wanted, dispatches cars |
| 90011 | Police Car | `npc_gtapolice_car` | Vehicle with 4 seats (VehicleId 9001) |
| 90012 | Police Officer | `npc_gtapolice_crew` | Crew that exits car and arrests on foot |
| 90015 | Shifty Pete | `npc_gtapolice_paynspray` | Pay-n-Spray gossip vendor |

## Vehicle Seats (VehicleId 9001)

| Seat | Entry | Role | Notes |
|------|-------|------|-------|
| 0 | 90010 | Driver | Crew officer with vehicle control |
| 1 | 90011 | Passenger | Crew officer, no control |
| 2 | 90012 | Prisoner | Arrested player, cannot exit voluntarily |
| 3 | - | Reserved | Future use |

## GM Commands

| Command | Description |
|---------|-------------|
| `.police wanted <stars>` | Set target's wanted level (1-5) |
| `.police wanted off` | Clear wanted status |
| `.police pursue` | Start pursuit on target |
| `.police arrest` | Immediate arrest (skip chase) |
| `.police status` | Show pursuit statistics |
| `.police clear` | End all active pursuits |
| `.police bribe` | Force bribe attempt on target |
| `.police heat` | Show current zone heat status |
| `.police reload` | Reload configuration at runtime |

## Configuration

All settings are in `conf/mod_gtapolice.conf.dist`. Copy to `mod_gtapolice.conf` and customize.

### General Settings

```conf
GTAPolice.Enable = 1
GTAPolice.DebugLog = 0
```

### Creature Entries

```conf
GTAPolice.OfficerEntry = 90010
GTAPolice.CarEntry = 90011
GTAPolice.CrewOfficerEntry = 90012
GTAPolice.PaynSprayNPCEntry = 90015
```

### Vehicle Seats

```conf
GTAPolice.Vehicle.SeatDriver = 0
GTAPolice.Vehicle.SeatCrewPassenger = 1
GTAPolice.Vehicle.SeatPrisoner = 2
```

### Crew System

```conf
GTAPolice.Crew.Size = 2
GTAPolice.Crew.ExitRange = 15.0
GTAPolice.Crew.ArrestRange = 3.0
GTAPolice.Crew.EscapeRange = 50.0
GTAPolice.Crew.ExitDelayMs = 500
GTAPolice.Crew.LoadingTimeoutMs = 10000
GTAPolice.Crew.RunSpeed = 1.5
GTAPolice.Crew.DespawnNonDriverOnExit = 1
```

### Spells

```conf
GTAPolice.Spell.ArrestStun = 853
GTAPolice.Spell.ArrestRoot = 122
GTAPolice.Spell.SirenAura = 0
```

### Officer Behavior

```conf
GTAPolice.Officer.ScanRange = 60.0
GTAPolice.Officer.ScanIntervalMs = 1500
```

### Pursuit Settings

```conf
GTAPolice.Pursuit.CarSpawnDistance = 18.0
GTAPolice.Pursuit.ChaseRepathDistance = 2.0
GTAPolice.Pursuit.ArrestRange = 6.0
GTAPolice.Pursuit.ChaseTimeoutMs = 45000
GTAPolice.Pursuit.EscapeDistance = 100.0
```

### Transport Settings

```conf
GTAPolice.Transport.FailsafeDurationMs = 20000
GTAPolice.Transport.ArrivalDistance = 8.0
```

### Jail Settings

```conf
GTAPolice.Jail.DefaultMap = 0
GTAPolice.Jail.DefaultX = -8823.0
GTAPolice.Jail.DefaultY = 622.0
GTAPolice.Jail.DefaultZ = 95.0
GTAPolice.Jail.DefaultO = 0.0
```

### Guard System Integration

```conf
GTAPolice.GuardSystem.UseGuardSystemWanted = 1
GTAPolice.GuardSystem.MinWantedStarsForPursuit = 2
GTAPolice.GuardSystem.MinWantedStarsForArrest = 3
GTAPolice.GuardSystem.ClearWantedOnJail = 1
```

### Bridge Events

```conf
GTAPolice.Bridge.EmitBridgeEvents = 1
```

### Star Decay

```conf
GTAPolice.StarDecay.Enabled = 1
GTAPolice.StarDecay.IntervalMs = 60000
GTAPolice.StarDecay.GracePeriodMs = 30000
GTAPolice.StarDecay.EvasionRequiredMs = 120000
```

### Multi-Car Pursuit

```conf
GTAPolice.MultiCar.MaxCars = 4
GTAPolice.MultiCar.CarsAtStar1 = 1
GTAPolice.MultiCar.CarsAtStar2 = 1
GTAPolice.MultiCar.CarsAtStar3 = 2
GTAPolice.MultiCar.CarsAtStar4 = 3
GTAPolice.MultiCar.CarsAtStar5 = 4
GTAPolice.MultiCar.ReinforceIntervalMs = 30000
GTAPolice.MultiCar.SpawnMinDistance = 30.0
GTAPolice.MultiCar.SpawnMaxDistance = 50.0
```

### Radio Chatter

```conf
GTAPolice.RadioChatter.Enabled = 1
GTAPolice.RadioChatter.IntervalMs = 15000
```

### Bribe System

```conf
GTAPolice.Bribe.Enabled = 1
GTAPolice.Bribe.CostPerStar = 50000
GTAPolice.Bribe.CooldownMs = 300000
GTAPolice.Bribe.MaxStars = 3
```

### Pay-n-Spray

```conf
GTAPolice.PaynSpray.Enabled = 1
GTAPolice.PaynSpray.CostPerStar = 100000
GTAPolice.PaynSpray.CooldownMs = 600000
```

### Zone Heat

```conf
GTAPolice.ZoneHeat.Enabled = 1
GTAPolice.ZoneHeat.DecayIntervalMs = 60000
GTAPolice.ZoneHeat.DecayAmount = 5
GTAPolice.ZoneHeat.PerCrime = 10
GTAPolice.ZoneHeat.ThresholdLow = 30
GTAPolice.ZoneHeat.ThresholdHigh = 80
```

### Witness Integration

```conf
GTAPolice.Witness.AlertsEnabled = 1
GTAPolice.Witness.DetectedHeat = 5
GTAPolice.Witness.ConfirmedHeatBonus = 15
GTAPolice.Witness.SuppressedHeatReduction = 10
```

## Database Setup

### Using ac-cli

```bash
ac police sql-install
```

### Using ec-sql

```bash
./scripts/ec-sql/ec-sql.sh install-ec world
./scripts/ec-sql/ec-sql.sh install-ec characters
```

### Manual Installation

```sql
-- World database
SOURCE data/sql/world/base/01_creature_templates.sql;
SOURCE data/sql/world/base/02_jail_locations.sql;

-- Characters database
SOURCE data/sql/characters/base/01_wanted_state.sql;
```

## Database Tables

### acore_world

| Table | Purpose |
|-------|---------|
| `creature_template` | NPC entries 90010-90015 |
| `creature_template_model` | Display models |
| `creature` | Officer/Pay-n-Spray spawn locations |
| `ec_jail_locations` | Jail coordinates by faction |
| `ec_jail_punishments` | Time/gold by wanted stars |
| `ec_police_car_paths` | Optional waypoints |

### acore_characters

| Table | Purpose |
|-------|---------|
| `ec_police_wanted_state` | Player wanted persistence |

## Spawn Locations

### Police Officers (24 spawns)

**Alliance Cities:**
- Stormwind (4): Gates, Trade District, Cathedral Square, Old Town
- Ironforge (3): The Commons, Military Ward, Mystic Ward
- Darnassus (2): Temple of the Moon, Tradesman's Terrace
- Exodar (1): Seat of the Naaru

**Horde Cities:**
- Orgrimmar (3): Valley of Strength, The Drag, Valley of Honor
- Undercity (2): Trade Quarter, War Quarter
- Thunder Bluff (2): High Rise, Spirit Rise
- Silvermoon City (1): Bazaar

**Neutral Cities:**
- Dalaran (2): Magus Commerce Exchange, Runeweaver Square
- Shattrath (2): Lower City, Terrace of Light
- Booty Bay (1): Docks
- Gadgetzan (1): Center

### Pay-n-Spray NPCs (8 spawns)

Hidden in shady locations:
- Stormwind: Cut-Throat Alley
- Orgrimmar: Cleft of Shadow
- Undercity: Rogues' Quarter
- Ironforge: Forlorn Cavern
- Booty Bay: Back alley behind inn
- Dalaran: Sewers (Underbelly)
- Shattrath: Lower City corner
- Gadgetzan: Behind the inn

## Pursuit Flow

```
                    ┌─────────────────────────────────────┐
                    │  Officer scans (60y, every 1.5s)    │
                    └──────────────────┬──────────────────┘
                                       ▼
                    ┌─────────────────────────────────────┐
                    │  Finds player with wanted stars     │
                    │  (≥2 for pursuit, ≥3 for arrest)    │
                    └──────────────────┬──────────────────┘
                                       ▼
                    ┌─────────────────────────────────────┐
                    │  Spawns Police Car(s) based on      │
                    │  wanted level (1-4 cars)            │
                    └──────────────────┬──────────────────┘
                                       ▼
                    ┌─────────────────────────────────────┐
                    │  Car starts MoveChase() pursuit     │
                    │  State: CHASE                       │
                    └──────────────────┬──────────────────┘
                                       ▼
              ┌────────────────────────┴────────────────────────┐
              ▼                                                  ▼
┌─────────────────────────┐                      ┌─────────────────────────┐
│  Distance ≤ 15y         │                      │  Chase timeout (45s)    │
│  Crew exits vehicle     │                      │  OR escape (100y)       │
│  State: APPROACH        │                      │  → Car despawns         │
└───────────┬─────────────┘                      └─────────────────────────┘
            ▼
┌─────────────────────────┐
│  Crew approaches on foot│
│  (run speed 1.5x)       │
└───────────┬─────────────┘
            ▼
┌─────────────────────────┐
│  Distance ≤ 3y          │
│  Stun + Root applied    │
│  State: ARRESTING       │
└───────────┬─────────────┘
            ▼
┌─────────────────────────┐
│  Player forced into     │
│  seat 2 (prisoner)      │
│  State: LOADING         │
└───────────┬─────────────┘
            ▼
┌─────────────────────────┐
│  Crew re-boards car     │
│  Car drives to jail     │
│  State: TRANSPORT       │
└───────────┬─────────────┘
            ▼
┌─────────────────────────┐
│  Arrival at jail        │
│  Player exits + teleport│
│  Punishment applied     │
│  Car despawns           │
└─────────────────────────┘
```

## Bridge Events

When `mod-ec-realtime-bridge` is present, these events are emitted:

| Event | Namespace | Description |
|-------|-----------|-------------|
| `pursuit_started` | police | New pursuit initiated |
| `pursuit_ended` | police | Pursuit concluded (arrest/escape) |
| `car_spawned` | police | Police car spawned |
| `car_despawned` | police | Police car despawned |
| `crew_exited` | police | Crew officers left vehicle |
| `arrest_started` | police | Arrest in progress |
| `arrest_complete` | police | Player successfully arrested |
| `player_jailed` | police | Player teleported to jail |
| `player_escaped` | police | Player evaded pursuit |
| `bribe_attempted` | police | Bribe attempt made |
| `bribe_success` | police | Bribe accepted |
| `bribe_failed` | police | Bribe rejected |
| `paynspray_used` | police | Pay-n-Spray service used |
| `zone_heat_changed` | police | Zone heat level changed |
| `wanted_changed` | police | Player wanted level changed |

## Troubleshooting

### Pursuit does nothing
- Check `GTAPolice.Enable = 1`
- Check player has enough wanted stars (`MinWantedStarsForPursuit`)
- Check `VehicleId` is valid in `creature_template` for entry 90011

### Player not entering vehicle
- Vehicle seat configuration wrong
- Player already in vehicle/flight/combat
- Check seat 2 exists in vehicle_template

### Car not moving
- Check jail coordinates are valid for the map
- Check server logs for `[mod-gtapolice]` errors

### Crew not exiting
- Check `CrewOfficerEntry` matches SQL (90012)
- Check `Crew.Size` is at least 1

### Stars not decaying
- Check `StarDecay.Enabled = 1`
- Player must not be in active pursuit
- Grace period applies after crimes

### Zone heat not working
- Check `ZoneHeat.Enabled = 1`
- Check `Witness.AlertsEnabled = 1` for witness integration

### Bridge events not emitting
- Check `mod-ec-realtime-bridge` is installed and enabled
- Check `Bridge.EmitBridgeEvents = 1`

## Dependencies

### Required
- AzerothCore 3.3.5 (master branch)
- Custom Vehicle.dbc entry 9001 (ec-custom-content)

### Optional
- **mod-ec-guard-system** - Uses guard wanted stars instead of spell aura
- **mod-ec-realtime-bridge** - Enables GraphQL event subscriptions
- **mod-playerbots** - Bot integration for testing

## Build

The module auto-detects optional dependencies via CMake:

```
AUTOLOAD: mod-gtapolice
  mod-gtapolice: mod-ec-realtime-bridge detected, enabling bridge events
  mod-gtapolice: mod-ec-guard-system detected, enabling wanted level integration
```

## CLI Commands

Use `ac police` for management:

```bash
ac police status       # Check module installation
ac police config       # Show current configuration
ac police config-copy  # Copy .dist template to active
ac police sql-install  # Install police database tables
ac police ra <cmd>     # Send police GM command via RA
```

## File Structure

```
mod-gtapolice/
├── mod-gtapolice.cmake           # CMake build config
├── README.md                     # This file
├── conf/
│   └── mod_gtapolice.conf.dist   # Configuration template
├── data/sql/
│   ├── world/base/
│   │   ├── 01_creature_templates.sql  # NPCs + spawns
│   │   └── 02_jail_locations.sql      # Jail tables
│   └── characters/base/
│       └── 01_wanted_state.sql        # Player state
└── src/
    ├── mod_gtapolice.cpp         # Registration + scripts
    ├── GTAPoliceConfig.h/.cpp    # Configuration loader
    ├── GTAPoliceManager.h/.cpp   # Main pursuit manager
    ├── GTAPoliceBridge.h/.cpp    # Bridge event adapter
    ├── GTAPoliceCarAI.h/.cpp     # Vehicle AI
    ├── GTAPoliceOfficerAI.h/.cpp # Scanner officer AI
    ├── GTAPoliceCrewAI.h/.cpp    # Crew officer AI
    ├── GTAPoliceCommands.h/.cpp  # GM commands
    ├── GTAPolicePaynSpray.h/.cpp # Pay-n-Spray NPC
    └── GTAPoliceHelpers.h        # Utility functions
```
