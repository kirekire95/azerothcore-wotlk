# Player Proximity Spawning (EC Custom Feature)

**Author:** EC
**Date:** 2025-01-25
**Status:** Implemented

## Overview

Player Proximity Spawning is a custom feature that makes the game world feel more alive by spawning bots in zones where real players are located. Instead of bots randomly spawning across the entire world, they preferentially teleport to zones with real player activity, respecting faction rules.

## Problem Solved

By default, playerbots spawn randomly across all level-appropriate zones. This means:
- You might be in Stranglethorn Vale with 0 other players/bots around
- Meanwhile, 50 bots are idling in Barrens with no one to interact with
- The world feels empty despite having 500 bots online

With Player Proximity Spawning:
- Bots gravitate toward zones where real players are
- You'll encounter more "players" as you explore
- The world feels populated and alive
- Faction rules ensure immersion (Alliance bots in Alliance zones, etc.)

## How It Works

### Zone Tracking

Every 30 seconds, the system scans all real players and records:
- Which zone they're in
- Their faction (Alliance/Horde)
- Their level (min/max in zone)
- The zone's faction type (Alliance/Horde/Neutral)

### Teleport Decision

When a bot needs to teleport:

1. **Roll the dice** - Check if we should try player zones (configurable %)
2. **Find valid zones** - Filter zones by:
   - Bot faction compatibility
   - Bot level appropriateness
3. **Get locations** - Find spawn points in those zones
4. **Teleport** - Move bot to a player-occupied zone
5. **Fallback** - If no valid zones, use normal random teleport

### Faction Rules (Mode 1 - Default)

| Zone Type | Player Faction | Bots That Can Spawn |
|-----------|----------------|---------------------|
| Neutral (STV, Tanaris) | Any | Both Horde and Alliance |
| Alliance (Elwynn, Westfall) | Alliance | Alliance only |
| Horde (Durotar, Barrens) | Horde | Horde only |

## Configuration

Add these options to `playerbots.conf`:

```conf
# Enable player proximity spawning
# Default: 1 (enabled)
AiPlayerbot.SpawnNearPlayersEnabled = 1

# Percentage chance (0-100) to try spawning in player zones
# Higher = more bots near players, Lower = more random
# Default: 50
AiPlayerbot.SpawnNearPlayersChance = 50

# Minimum distance from real players when spawning (in yards)
# Prevents bots from popping in right next to you
# Default: 300
AiPlayerbot.SpawnNearPlayersMinDistance = 300

# Faction behavior mode
#   0 = Same faction only (Horde player = only Horde bots)
#   1 = Faction-aware (respects zone ownership) [DEFAULT]
#   2 = Both factions everywhere (PvP chaos)
# Default: 1
AiPlayerbot.SpawnNearPlayersFactionMode = 1
```

## Example Scenarios

### Scenario 1: Horde Player in Stranglethorn Vale (Neutral)
- Zone faction: Neutral (0)
- Player faction: Horde
- Result: **Both Horde AND Alliance bots** can spawn (faction mode 1)

### Scenario 2: Horde Player in Durotar (Horde Zone)
- Zone faction: Horde (4)
- Player faction: Horde
- Result: **Only Horde bots** can spawn

### Scenario 3: Alliance Player in Elwynn Forest (Alliance Zone)
- Zone faction: Alliance (2)
- Player faction: Alliance
- Result: **Only Alliance bots** can spawn

### Scenario 4: Horde Player in Elwynn Forest (Alliance Zone, Faction Mode 1)
- Zone faction: Alliance (2)
- Player faction: Horde
- Result: **Only Alliance bots** can spawn (zone ownership takes precedence)

## Technical Implementation

### Files Modified

| File | Changes |
|------|---------|
| `PlayerbotAIConfig.h` | Added config variables |
| `PlayerbotAIConfig.cpp` | Added config loading |
| `playerbots.conf.dist` | Added config documentation |
| `RandomPlayerbotMgr.h` | Added PlayerZoneInfo struct and methods |
| `RandomPlayerbotMgr.cpp` | Added zone tracking and teleport logic |

### Key Functions

```cpp
// Update which zones have real players (called every 30s)
void UpdatePlayerZones();

// Get list of zones with players
std::vector<PlayerZoneInfo> GetZonesWithPlayers() const;

// Check if bot faction can spawn in zone
bool CanBotSpawnInPlayerZone(TeamId botTeam, const PlayerZoneInfo& zoneInfo) const;

// Get spawn locations in player-occupied zones
std::vector<WorldLocation> GetPlayerZoneLocations(Player* bot) const;
```

### Data Structures

```cpp
struct PlayerZoneInfo {
    uint32 zoneId;              // Zone ID (e.g., 33 = Stranglethorn Vale)
    uint32 mapId;               // Map ID (0 = Eastern Kingdoms, etc.)
    uint32 zoneFaction;         // 0 = neutral, 2 = Alliance, 4 = Horde
    bool hasAlliancePlayers;    // At least one Alliance player
    bool hasHordePlayers;       // At least one Horde player
    uint8 minPlayerLevel;       // Lowest level player in zone
    uint8 maxPlayerLevel;       // Highest level player in zone
};
```

## Debugging

Enable debug logging to see the feature in action:

```conf
# In worldserver.conf
Logger.playerbots=4,Console Server
```

Look for log messages starting with `PlayerProximitySpawning:`:
- "Updated X zones with real players"
- "Bot X teleporting to player zone (Y locations available)"
- "No suitable player zones for bot X"

## Performance Considerations

- Zone cache updates every 30 seconds (configurable overhead)
- Location filtering is done in-memory using existing caches
- No additional database queries
- Minimal impact on server performance

## Future Enhancements

Potential improvements:
- [ ] Weight zones by player count (more players = more bots)
- [ ] Time-of-day awareness (more bots during peak hours)
- [ ] Quest zone prioritization
- [ ] Dungeon entrance clustering
- [ ] City overflow prevention

## Related Configuration

This feature works alongside existing teleport options:
- `AiPlayerbot.RandomBotMaps` - Which maps bots can spawn on
- `AiPlayerbot.ProbTeleToBankers` - Chance to teleport to cities
- `AiPlayerbot.EnableNewRpgStrategy` - Use faction-specific starter zones
- `AiPlayerbot.RandomBotTeleLowerLevel` / `RandomBotTeleHigherLevel` - Level tolerance

## Changelog

### 2025-01-25
- Initial implementation
- Added 4 config options
- Implemented zone tracking with 30-second cache
- Added faction-aware spawning logic
- Integrated with RandomTeleportForLevel and RandomTeleportGrindForLevel
