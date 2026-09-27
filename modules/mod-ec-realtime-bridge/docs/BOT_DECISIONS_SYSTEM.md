# Bot Decisions System (PlayerbotsBridgeAdapter)

Real-time tracking of what playerbots are "thinking" - their AI decisions, strategy changes, state transitions, and action execution.

## Overview

This system bridges mod-playerbots to the Node.js backend, allowing the web UI to display bot AI decisions in real-time.

```
┌───────────────────────────────────────────────────────────────────────────┐
│                           DATA FLOW                                        │
├───────────────────────────────────────────────────────────────────────────┤
│                                                                            │
│  mod-playerbots Engine.cpp                                                 │
│         │                                                                  │
│         │ Action executed/failed                                           │
│         ▼                                                                  │
│  PlayerbotsBridgeAdapter.EmitBotActionExecuted()                          │
│         │                                                                  │
│         │ JSON payload                                                     │
│         ▼                                                                  │
│  mod-ec-realtime-bridge (TCP port 8092)                                   │
│         │                                                                  │
│         │ Newline-delimited JSON                                           │
│         ▼                                                                  │
│  Node.js RealtimeBridgeService                                            │
│         │                                                                  │
│         │ pubsub-integration.ts::routeBotsEvent()                         │
│         ▼                                                                  │
│  GraphQL PubSub (bots:actionExecuted, etc.)                               │
│         │                                                                  │
│         │ Server-Sent Events (SSE)                                         │
│         ▼                                                                  │
│  useBotDecisionsSubscription.ts (React hook)                              │
│         │                                                                  │
│         ▼                                                                  │
│  BotDecisionsPanel.tsx (UI Component)                                     │
│                                                                            │
└───────────────────────────────────────────────────────────────────────────┘
```

## Event Types

| Event Type | Description | C++ Method |
|------------|-------------|------------|
| `bot_action_executed` | Bot performed an action | `EmitBotActionExecuted()` |
| `bot_action_failed` | Action failed to execute | `EmitBotActionFailed()` |
| `bot_strategy_changed` | Strategy added/removed | `EmitBotStrategyChanged()` |
| `bot_strategies_snapshot` | Full strategy list | `EmitBotStrategiesSnapshot()` |
| `bot_state_changed` | AI state transition | `EmitBotStateChanged()` |
| `bot_target_changed` | Target selection changed | `EmitBotTargetChanged()` |
| `bot_spawned` | Bot entered world | `EmitBotSpawned()` |
| `bot_despawned` | Bot left world | `EmitBotDespawned()` |

## JSON Payload Format

All events include a standard `bot` object:

```json
{
  "bot": {
    "guid": 12345,
    "name": "Testbot",
    "level": 80,
    "class": 1,
    "race": 2,
    "zone": 1519,
    "map": 0,
    "x": -8949.95,
    "y": -130.0,
    "z": 83.5,
    "health": 15000,
    "maxHealth": 15000,
    "mana": 0,
    "maxMana": 0
  },
  "timestamp": 1706900000
}
```

### bot_action_executed

```json
{
  "bot": { ... },
  "action": "melee",
  "relevance": 0.95,
  "trigger": "target found",
  "timestamp": 1706900000
}
```

### bot_action_failed

```json
{
  "bot": { ... },
  "action": "cast fireball",
  "reason": "not enough mana",
  "timestamp": 1706900000
}
```

### bot_strategy_changed

```json
{
  "bot": { ... },
  "strategy": "combat",
  "added": true,
  "state": "combat",
  "timestamp": 1706900000
}
```

### bot_state_changed

```json
{
  "bot": { ... },
  "oldState": "non_combat",
  "newState": "combat",
  "timestamp": 1706900000
}
```

## Sampling

To prevent flooding, events are sampled at a configurable rate (default 10%):

```cpp
// Set to 50% sampling
sPlayerbotsBridge->SetActionSampleRate(0.5f);

// Monitor only specific zones
std::unordered_set<uint32_t> zones = {1519, 1537}; // Stormwind, Ironforge
sPlayerbotsBridge->SetMonitoredZones(zones);

// Monitor only specific bots
std::unordered_set<uint64_t> guids = {12345, 12346};
sPlayerbotsBridge->SetMonitoredBots(guids);
```

## C++ Integration

### Files Created

| File | Purpose |
|------|---------|
| `src/PlayerbotsBridgeAdapter.h` | Header with class definition |
| `src/PlayerbotsBridgeAdapter.cpp` | Implementation |

### Files Modified

| File | Changes |
|------|---------|
| `mod-ec-realtime-bridge.cmake` | Added `PlayerbotsBridgeAdapter.cpp` to build |
| `src/RealtimeBridgeScripts.cpp` | Added include, Initialize(), Shutdown() calls |

### mod-playerbots Engine.cpp Changes

```cpp
// At top of file
#ifdef MOD_EC_REALTIME_BRIDGE
#include "PlayerbotsBridgeAdapter.h"
#endif

// In ListenAndExecute(), after action execution (line ~246)
if (actionExecuted)
{
#ifdef MOD_EC_REALTIME_BRIDGE
    sPlayerbotsBridge->EmitBotActionExecuted(ecBot, action->getName(),
                                              relevance, event.GetSource());
#endif
}
else
{
#ifdef MOD_EC_REALTIME_BRIDGE
    sPlayerbotsBridge->EmitBotActionFailed(ecBot, action->getName(),
                                            "Execute() returned false");
#endif
}
```

## GraphQL Subscriptions

| Subscription | Channel | Description |
|--------------|---------|-------------|
| `botActionExecuted` | `bots:actionExecuted` | Action executed events |
| `botActionFailed` | `bots:actionFailed` | Action failed events |
| `botStrategyChanged` | `bots:strategyChanged` | Strategy change events |
| `botStrategiesSnapshot` | `bots:strategiesSnapshot` | Full strategy list |
| `botStateChanged` | `bots:stateChanged` | State transition events |
| `botTargetChanged` | `bots:targetChanged` | Target change events |
| `botSpawned` | `bots:spawned` | Bot spawn events |
| `botDespawned` | `bots:despawned` | Bot despawn events |
| `botEvents` | `bots:*` | All bot events (wildcard) |

### Example Query

```graphql
subscription BotActionExecuted {
  botActionExecuted {
    bot {
      guid
      name
      level
      classId
      health
      maxHealth
    }
    action
    relevance
    trigger
    timestamp
  }
}
```

## Frontend Usage

```typescript
import { useBotDecisionsSubscription } from '@/hooks/useBotDecisionsSubscription';

function MyComponent() {
  const {
    feed,             // Unified sorted feed of all events
    stats,            // Aggregated statistics
    actionEvents,     // Raw action events
    strategyEvents,   // Raw strategy events
    subscriptionError,
  } = useBotDecisionsSubscription();

  return (
    <div>
      <p>Total Actions: {stats.totalActions}</p>
      <p>Active Bots (60s): {stats.activeBots}</p>
      {feed.map(item => (
        <div key={item.id}>
          {item.bot?.name}: {item.title}
        </div>
      ))}
    </div>
  );
}
```

## Statistics

The adapter tracks:
- `GetActionsEmitted()` - Total actions emitted
- `GetStrategiesEmitted()` - Total strategy events emitted
- `GetActionsSkipped()` - Actions skipped due to sampling

Query via Node.js:
```typescript
// Send command to get stats
bridge.sendBotsCommand('get_stats', { requestId: 'stats-001' });
```

## Debugging

### GM Commands

```bash
.bridge status       # Show bridge connection status
.bridge debug on     # Enable debug logging for all bridge messages
.bridge test bots    # Send test event to bots namespace
```

### Server Logs

```bash
# In worldserver console, watch for:
[module.playerbots.bridge] PlayerbotsBridgeAdapter: Initialized
[module.playerbots.bridge] PlayerbotsBridgeAdapter: Shutdown complete
```

### Node.js Logs

```bash
# In backend terminal:
[RealtimeBridge] Received message: ns=bots type=bot_action_executed
[PubSub] Published to bots:actionExecuted
```

## Performance Considerations

1. **Sampling Rate**: Default 10% prevents flooding. Adjust for debugging.
2. **Zone Filtering**: Only monitor specific zones to reduce traffic.
3. **Bot Filtering**: Only monitor specific bots for targeted debugging.
4. **Memory**: Frontend keeps last 200 events in memory (configurable).

## Rebuild Instructions

After modifying C++ files:

```bash
cd azerothcore-wotlk/build
make -j8
make install

# Restart servers
cd ..
./scripts/azerothcore-server.sh restart
```

## Troubleshooting

### No events appearing in UI

1. Check bridge connection: `.bridge status` in game
2. Verify backend is running: `http://localhost:3011/graphql`
3. Check browser console for SSE connection errors
4. Verify sampling rate isn't 0: default is 10%

### Events appearing but incomplete data

1. Check JSON format in Node.js logs
2. Verify `bot.class` vs `bot.classId` field mapping
3. Check for null bot objects (bot may have despawned)

### High CPU/memory usage

1. Reduce sampling rate: `SetActionSampleRate(0.01f)` for 1%
2. Filter to specific zones: `SetMonitoredZones({1519})`
3. Filter to specific bots: `SetMonitoredBots({12345})`
