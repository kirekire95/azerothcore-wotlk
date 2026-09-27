# GTA-Style Wanted Star Decay System

## Overview

Implemented automatic wanted star decay for mod-gtapolice. In GTA games, if you evade police long enough, your wanted stars decrease over time. This system replicates that behavior.

## How It Works

1. **Crime Recording**: When a player commits a crime (wanted level increases), the system records the time
2. **Evasion Start**: When a pursuit ends without arrest (player escapes), evasion timer starts
3. **Decay Conditions**: Stars decay only if:
   - Player is NOT in active pursuit
   - Grace period has passed since last crime (default: 30 seconds)
   - Player has evaded police for required time (default: 30 seconds)
   - Decay interval has elapsed (default: 60 seconds per star)
4. **Decay Action**: One star is removed at a time
5. **Bridge Event**: Emits `star_decay` event to realtime bridge for UI updates

## Configuration

Added to `mod_gtapolice.conf.dist`:

```conf
GTAPolice.StarDecayEnabled = 1                      # Enable/disable system
GTAPolice.StarDecayIntervalMs = 60000               # 1 minute per star
GTAPolice.StarDecayGracePeriodMs = 30000            # 30 seconds after crime
GTAPolice.StarDecayRequiredEvasionMs = 30000        # 30 seconds of evasion
```

## Integration Points

### 1. Crime Recording
**Location**: `GTAPoliceManager::OnWantedLevelChanged()`
- Called when guard system increases wanted level
- Records crime timestamp
- Resets evasion timer

### 2. Evasion Start
**Location**: `GTAPoliceManager::EndPursuit()`
- Called when pursuit ends without arrest (player escapes)
- Starts evasion timer if player still has wanted stars

### 3. Decay Processing
**Location**: `GTAPoliceManager::Update()`
- Called every world update tick
- Checks all players with decay states
- Accumulates decay timers
- Triggers decay when conditions are met

### 4. Star Reduction
**Location**: `GTAPoliceManager::DecayPlayerStars()`
- Reduces wanted level by 1 star
- Uses guard system API if available
- Falls back to aura removal
- Emits bridge event
- Notifies player in chat

## Bridge Event Schema

Event: `star_decay`
Namespace: `police` (via `GUARD_SYSTEM` namespace)

```json
{
  "type": "star_decay",
  "player": {
    "guid": 123,
    "name": "PlayerName",
    "level": 80,
    "class": 1,
    "race": 1,
    "x": 1234.56,
    "y": 7890.12,
    "z": 345.67,
    "mapId": 0,
    "zoneId": 12
  },
  "oldStars": 3,
  "newStars": 2,
  "zoneId": 12,
  "timestamp": 1706548800
}
```

## Files Modified

### Configuration
- `GTAPoliceConfig.h` - Added 4 new config fields
- `GTAPoliceConfig.cpp` - Added config loading + log output
- `conf/mod_gtapolice.conf.dist` - Added configuration documentation

### Manager
- `GTAPoliceManager.h` - Added decay state struct, public methods, private methods
- `GTAPoliceManager.cpp` - Implemented full decay system:
  - `RecordCrime()` - Records crime timestamp
  - `StartEvasion()` - Starts evasion timer
  - `UpdateStarDecay()` - Processes decay for all players
  - `DecayPlayerStars()` - Reduces wanted level by 1 star
  - Modified `Shutdown()` - Cleanup decay states
  - Modified `Update()` - Call UpdateStarDecay()
  - Modified `OnWantedLevelChanged()` - Record crime on wanted increase
  - Modified `EndPursuit()` - Start evasion on escape

### Bridge
- `GTAPoliceBridge.h` - Added `EmitStarDecay()` declaration
- `GTAPoliceBridge.cpp` - Implemented `EmitStarDecay()` with JSON builder

## Testing Checklist

- [ ] Compile AzerothCore with mod-gtapolice
- [ ] Verify configuration loads correctly on server start
- [ ] Commit a crime to get wanted stars
- [ ] Escape pursuit (don't get arrested)
- [ ] Wait 30 seconds (evasion required time)
- [ ] Wait 60 seconds (decay interval)
- [ ] Verify star decreases by 1
- [ ] Check player receives chat notification
- [ ] Verify bridge event is emitted
- [ ] Test with multiple wanted levels (decay multiple times)
- [ ] Verify stars don't decay during active pursuit
- [ ] Verify stars don't decay if you commit another crime during grace period

## Future Enhancements

1. **Variable Decay Rates**: Higher stars decay slower (5 stars takes longer than 2 stars)
2. **Zone-Specific Decay**: Decay faster in wilderness, slower in cities
3. **Activity-Based Decay**: Decay faster if player is idle/hiding
4. **Witness System**: If guards see you, reset evasion timer
5. **Pursuit Intensity**: More aggressive police response prevents decay

## Implementation Notes

- **Thread Safety**: All decay state access is mutex-protected
- **Performance**: Minimal overhead - only processes players with active decay states
- **Guard System Integration**: Uses `SetPlayerWantedStars()` API when available
- **Fallback Mode**: Works with aura-based wanted system if guard system is disabled
- **Player Notification**: Uses colored chat message (green for good news)
- **Cleanup**: Automatically removes decay state when stars reach 0 or player disconnects
