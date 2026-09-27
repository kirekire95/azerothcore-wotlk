# mod-alive-world

**Alive World System** - Makes your WoW bots perceive, remember, and react to world events with AI-powered decision making.

## Overview

This module creates a living, breathing game world where bots:
- **Perceive** events (combat, chat, zone changes, boss kills)
- **Remember** experiences (combat history, social connections, personality evolution)
- **React** intelligently (using LLM for complex decisions, deterministic rules for common situations)
- **Gossip** with each other (news spreads naturally between bots)

## Architecture

```
┌─────────────────────────────────────────────────────────────────┐
│                    mod-alive-world (C++)                         │
│  EventCollector → WorldStateManager → BotMemoryManager          │
│  IntelligenceTierManager → AliveWorldBridgeAdapter              │
│                    ↑ Bridge to shared socket                    │
└──────────────────────────┬──────────────────────────────────────┘
                           │ Events via mod-ec-realtime-bridge
                           ▼
┌─────────────────────────────────────────────────────────────────┐
│              mod-ec-realtime-bridge (C++)                        │
│  - Shared TCP socket for all EC modules                         │
│  - Unified JSON event stream (namespace-based routing)          │
│  - Port 8091 by default (configurable)                          │
└──────────────────────────┬──────────────────────────────────────┘
                           │ TCP Socket (bidirectional JSON)
                           ▼
┌─────────────────────────────────────────────────────────────────┐
│              wow-control-backend (Node.js)                       │
│  - Receives events from bridge, makes AI decisions              │
│  - Calls Ollama for LLM responses                               │
│  - Sends commands back (.bot say, .bot drink, etc.)             │
│  - Exposes GraphQL API for dashboard                            │
└──────────────────────────┬──────────────────────────────────────┘
                           │ GraphQL + WebSocket
                           ▼
┌─────────────────────────────────────────────────────────────────┐
│              Azeroth Command Console (Dashboard)                 │
│  Real-time bot stats, zone map, gossip feed, AI queue           │
└─────────────────────────────────────────────────────────────────┘
```

**Note:** As of 2025, mod-alive-world uses the shared `mod-ec-realtime-bridge` instead of a dedicated socket. This allows all EC modules (alive-world, guard-system, world-events) to share a single TCP connection.

## Communication Protocol

All communication uses a unified TCP socket via `mod-ec-realtime-bridge` with JSON messages.

### Message Format

```json
{
  "ns": "alive",           // Namespace: alive, guard, event, system
  "type": "event_type",    // Message type
  "requestId": "uuid",     // For request/response matching (queries only)
  "data": { ... }          // Payload
}
```

### Communication Patterns

#### 1. Events (C++ → Node.js)

Fire-and-forget messages pushed when game events occur:

| Event Type | Description | Payload Fields |
|------------|-------------|----------------|
| `player_login` | Player entered world | guid, name, level, race, class, zone |
| `player_logout` | Player left world | guid, name |
| `player_chat` | Player spoke | guid, message, chatType |
| `bot_tier_changed` | Bot intelligence tier changed | botGuid, oldTier, newTier, reason |
| `bot_near_player` | Bot entered player proximity | botGuid, playerGuid, distance |
| `combat_start` | Combat began | botGuid, targetGuid, targetName |
| `combat_end` | Combat ended | botGuid, targetGuid |
| `creature_killed` | NPC was killed | killerGuid, creatureEntry, creatureName |
| `player_killed` | Player was killed | killerGuid, victimGuid |
| `gossip_spread` | Gossip was shared | gossipType, content, zoneId |
| `goal_created` | Bot set a goal | botGuid, goalId, goalType, target |
| `goal_progress` | Goal progress updated | botGuid, goalId, progress, current, total |
| `goal_completed` | Goal achieved | botGuid, goalId, targetName |
| `zone_state_updated` | Zone stats changed | zoneId, alliancePlayers, hordePlayers, dangerLevel |

#### 2. Queries (Node.js → C++)

Request/response messages for fetching data:

```
Request:  { "ns": "alive", "type": "get_bots", "requestId": "abc123" }
Response: { "ns": "alive", "type": "response", "requestId": "abc123", "data": {...} }
```

| Query Type | Description | Response Data |
|------------|-------------|---------------|
| `get_stats` | System statistics | uptimeSeconds, totalBots, tier counts, gossipCount |
| `get_zones` | All zone states | Array of zone objects with player/bot counts |
| `get_zone` | Single zone by ID | Zone object with detailed stats |
| `get_gossip` | Recent gossip items | Array of gossip with content, importance, TTL |
| `get_gossip_item` | Single gossip by ID | Full gossip details |
| `get_events` | Recent world events | Array of event objects |
| `get_bots` | Bot summary | Array with guid, name, tier, zone |
| `get_bot` | Full bot details | Memory, personality, combat history, goals |
| `get_tiers` | Tier distribution | tier1, tier2, tier3 counts |
| `get_ai_queue` | AI engine status | pending, processing, cacheHitRate |

#### 3. Commands (Node.js → C++)

Action execution messages:

| Command Type | Description | Parameters |
|--------------|-------------|------------|
| `bot_say` | Make bot speak | botGuid, message |
| `bot_emote` | Make bot emote | botGuid, emote |
| `bot_whisper` | Private message | botGuid, targetGuid, message |
| `bot_follow` | Follow a target | botGuid, targetGuid |
| `bot_stay` | Stop movement | botGuid |
| `bot_move_to` | Move to position | botGuid, x, y, z |
| `bot_attack` | Attack target | botGuid, targetGuid |
| `bot_flee` | Run away | botGuid |
| `set_goal` | Assign a goal | botGuid, goalType, target, priority |
| `cancel_goal` | Cancel a goal | botGuid, goalId |
| `set_debug` | Toggle debug | enabled |

### Example Flow

```
1. C++ detects player login
   → C++ sends: { "ns": "alive", "type": "player_login", "data": { "guid": 123, "name": "TestPlayer" } }

2. Node.js wants bot list
   → Node sends: { "ns": "alive", "type": "get_bots", "requestId": "req-001" }
   → C++ responds: { "ns": "alive", "type": "response", "requestId": "req-001", "data": { "bots": [...] } }

3. Node.js commands a bot
   → Node sends: { "ns": "alive", "type": "bot_say", "botGuid": 456, "message": "Hello world!" }
   → C++ responds: { "ns": "alive", "type": "response", "requestId": "...", "data": { "success": true } }
```

## Intelligence Tiering

Not all 500+ bots can use full AI at once. The tiering system manages this:

| Tier | Count | AI Access | Criteria |
|------|-------|-----------|----------|
| **Tier 1 (Active)** | 5-10 | Immediate LLM | Near players, in combat, recently spoken to |
| **Tier 2 (Aware)** | 50-100 | Queued LLM | Same zone as players |
| **Tier 3 (Passive)** | Rest | None | No players nearby, scripted behavior only |

Tiers are recalculated every 5 seconds based on:
- Distance to nearest real player
- Recent interaction (chat, combat)
- Combat engagement
- Zone activity level

## Features

### Event Collection
- Player login/logout, chat, emotes
- Combat start/end, creature/player kills
- Zone changes, rare spawns, boss kills
- Group formation, duels, trades

### Bot Memory System
- Per-bot personality traits (aggression, caution, sociability, etc.)
- Combat history (last 5-10 encounters)
- Social connections with sentiment tracking
- Personality evolution based on experiences

### Gossip System
- World events become gossip that spreads between bots
- Importance-weighted spreading
- TTL-based expiration
- Bots react to gossip based on personality

### AI Decision Engine
- Priority-based request queue
- Response caching to reduce LLM calls
- Deterministic fallbacks for common situations
- Graceful degradation when daemon is down

## Installation

### 1. Build the Module

```bash
cd azerothcore-wotlk

# If building locally
cd build
cmake .. -DMODULES=static
make -j$(nproc)

# If using Docker
docker compose build ac-worldserver
```

### 2. Install Database Schema

```bash
# World database (zone configs, personality presets)
mysql -u acore -p acore_world < modules/mod-alive-world/data/sql/db-world/alive_world_config.sql

# Characters database (bot memory, combat logs)
mysql -u acore -p acore_characters < modules/mod-alive-world/data/sql/db-characters/alive_world_characters.sql
```

### 3. Configure the Module

Copy the config file:
```bash
cp modules/mod-alive-world/conf/mod_alive_world.conf.dist env/dist/etc/mod_alive_world.conf
```

Edit key settings:
```ini
# Enable the module
AliveWorld.Enable = 1

# Tier limits
AliveWorld.MaxTier1Bots = 10
AliveWorld.MaxTier2Bots = 100

# Daemon connection (Node.js)
AliveWorld.Daemon.Enable = 1
AliveWorld.Daemon.Host = 127.0.0.1
AliveWorld.Daemon.Port = 8091
```

### 4. Start the Node.js Daemon

```bash
cd client/apps/wow-control-backend
pnpm dev  # Runs on port 3011, connects to C++ on 8091
```

### 5. Restart Worldserver

The module initializes on startup and connects to the daemon.

## Commands

| Command | Description | Level |
|---------|-------------|-------|
| `.aw status` | Show system status | GM |
| `.aw dump` | Dump state to server log | GM |
| `.aw personality [botName\|botGuid]` | Show live traits, locks, hunter target | GM |
| `.aw debug [on\|off\|level]` | Toggle debug mode | Admin |
| `.aw reload` | Reload configuration | Admin |

### Status Output

```
=== Alive World Status ===
Uptime: 3600 seconds
Total Bots: 523
  Tier 1 (Active): 8
  Tier 2 (Aware): 87
  Tier 3 (Passive): 428
Active Zones: 12
Gossip Pool: 23 items
Event Buffer: 156 events
Pending AI Requests: 2
Cache Hit Rate: 73%
```

## Database Tables

### Characters Database

| Table | Purpose |
|-------|---------|
| `alive_bot_memory` | Bot personality, stats, activity |
| `alive_world_bot_personality_overrides` | Fixed personality locks + relentless hunter profiles |
| `alive_bot_combat_log` | Combat encounter history |
| `alive_bot_social_connections` | Relationships between entities |
| `alive_world_gossip` | Gossip/rumor storage |
| `alive_zone_activity_history` | Zone activity tracking |
| `alive_ai_response_cache` | LLM response caching |

### World Database

| Table | Purpose |
|-------|---------|
| `alive_zone_config` | Zone danger levels, faction control |
| `alive_personality_presets` | Personality templates |
| `alive_ai_prompt_templates` | LLM prompt templates |

## Status Script

```bash
# View all stats
./modules/mod-alive-world/scripts/alive_status.sh

# View specific sections
./alive_status.sh bots       # Bot memory stats
./alive_status.sh zones      # Zone activity
./alive_status.sh gossip     # Gossip pool
./alive_status.sh cache      # AI response cache
./alive_status.sh connections # Social connections
./alive_status.sh presets    # Personality presets
```

## GraphQL API

The Node.js daemon exposes a GraphQL API on port 3011:

### Queries

```graphql
# Get daemon stats
query { aliveWorldStats { connected, tier1Bots, tier2Bots, tier3Bots } }

# Get zones sorted by danger
query { aliveWorldZones(minDanger: 0.5) { zoneId, zoneName, dangerLevel } }

# Get bot detail
query { aliveWorldBot(guid: 1234) { name, personality { aggression }, recentCombat { enemyName } } }

# Get gossip feed
query { aliveWorldGossip(limit: 10) { content, importance, spreadCount } }

# Get AI queue
query { aliveWorldAIQueue { id, botGuid, status, prompt } }
```

### Mutations

```graphql
# Make a bot say something
mutation { aliveWorldBotSay(botGuid: 1234, message: "Hello!") { success } }

# Send a command
mutation { aliveWorldSendCommand(input: { botGuid: 1234, type: "drink" }) { success } }

# Inject test event
mutation { aliveWorldInjectEvent(input: { eventType: "combat_start", botGuid: 1234 }) { success } }
```

## Fixed Personality + Hunter Example

Use `alive_world_bot_personality_overrides` to pin traits and force a bot to
continually pursue a specific target:

```sql
REPLACE INTO alive_world_bot_personality_overrides (
  bot_guid, is_enabled,
  lock_aggression, aggression,
  lock_vengefulness, vengefulness,
  relentless_hunter, relentless_target_guid,
  hunter_pursuit_radius, hunter_engage_radius, hunter_reengage_cooldown_ms
) VALUES (
  12345, 1,
  1, 95,
  1, 90,
  1, 67890,
  140, 35, 1500
);
```

Inspect personalities directly from SQL:

```sql
SELECT *
FROM v_alive_world_bot_personality
WHERE bot_name = 'YourBotName';
```

Hunter range mode is configurable in `mod_alive_world.conf`:

```ini
AliveWorld.Hunter.CrossZoneEnable = 1
AliveWorld.Hunter.CrossMapTeleportEnable = 1
AliveWorld.Hunter.CrossMapTeleportCooldownMs = 30000
```

### Subscriptions

```graphql
# Real-time event stream
subscription { aliveWorldEvents { eventType, botGuid, timestamp } }

# Zone updates
subscription { aliveWorldZoneUpdates { zoneId, dangerLevel, playerCount } }

# AI queue status
subscription { aliveWorldAIQueue { id, status, processingTimeMs } }
```

## Fallback Behavior

When the Node.js daemon is disconnected:

1. **Graceful Degradation**: Module continues running with deterministic behavior
2. **No AI Decisions**: Tier 1 bots fall back to standard playerbot AI
3. **Event Buffering**: Events are still collected (up to limit)
4. **Auto-Reconnect**: Attempts reconnection every 5 seconds
5. **State Preservation**: Bot memory persists in database

## Configuration Reference

See `conf/mod_alive_world.conf.dist` for all options:

- **General**: Enable, update interval, debug
- **Intelligence Tiering**: Max bots per tier, distances, update frequency
- **Event Collector**: Buffer size, TTL
- **World State**: Zone update interval, hot zone threshold
- **Gossip**: Pool size, spread chance, TTL
- **Bot Memory**: Combat log size, persistence interval
- **Daemon**: Host, port, reconnect interval, heartbeat
- **Strategy**: Personality evolution rates

## Uninstallation

```bash
# Remove tables
mysql -u acore -p acore_world < modules/mod-alive-world/data/sql/db-world/alive_world_uninstall.sql
mysql -u acore -p acore_characters < modules/mod-alive-world/data/sql/db-characters/alive_world_uninstall.sql

# Remove config
rm env/dist/etc/mod_alive_world.conf

# Rebuild without module
cmake .. -DMODULES=static
make -j$(nproc)
```

## Dependencies

- **mod-playerbots**: Required for bot control
- **mod-ec-realtime-bridge**: Required, provides shared TCP socket for event streaming
- **mod-ollama-chat**: Optional, provides LLM integration
- **mod-ec-world-events**: Optional, integrates with world events
- **mod-ec-guard-system**: Optional, provides crime event reactions

## License

Copyright (C) 2025 EC
