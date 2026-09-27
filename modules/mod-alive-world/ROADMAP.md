# Alive World System - Implementation Roadmap

> Making the game world feel truly alive with intelligent, reactive, and evolving bot behaviors.

## Vision

Create a living, breathing game world where 500+ bots:
- **React** to events around them based on personality
- **Remember** their experiences and relationships
- **Gossip** about interesting happenings
- **Adapt** their strategies over time
- **Pursue goals** like gathering, crafting, and trading
- **Coordinate** with each other for group activities

---

## Architecture Overview

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                           mod-alive-world (C++)                              │
├─────────────────────────────────────────────────────────────────────────────┤
│                                                                              │
│  ┌─────────────┐  ┌─────────────┐  ┌─────────────┐  ┌─────────────┐        │
│  │   Event     │  │   World     │  │ Intelligence│  │    Bot      │        │
│  │ Collector   │─▶│   State     │─▶│    Tier     │─▶│   Memory    │        │
│  │             │  │  Manager    │  │  Manager    │  │  Manager    │        │
│  └─────────────┘  └─────────────┘  └─────────────┘  └─────────────┘        │
│         │                                                   │               │
│         │              ┌─────────────┐                     │               │
│         │              │     AI      │◀────────────────────┘               │
│         │              │  Decision   │                                      │
│         │              │   Engine    │                                      │
│         │              └──────┬──────┘                                      │
│         │                     │                                             │
│         ▼                     ▼                                             │
│  ┌─────────────┐  ┌─────────────┐  ┌─────────────┐                        │
│  │   Gossip    │  │  Behavior   │  │    Goal     │                        │
│  │  Manager    │◀─│  Executor   │◀─│   System    │                        │
│  │             │  │             │  │             │                        │
│  └─────────────┘  └─────────────┘  └─────────────┘                        │
│                           │                                                 │
└───────────────────────────┼─────────────────────────────────────────────────┘
                            │
                   mod-ec-realtime-bridge
                            │
                            ▼
            ┌───────────────────────────────┐
            │  AliveWorldService (Node.js)  │
            │  - Dashboard & Analytics      │
            │  - Event visualization        │
            │  - Admin commands             │
            └───────────────────────────────┘
```

---

## Implementation Phases

### Phase 1: Foundation (DONE)
**Status:** ✅ Complete

| Component | Status | Description |
|-----------|--------|-------------|
| EventCollector | ✅ | Captures all game events |
| WorldStateManager | ✅ | Tracks zone states, activity |
| IntelligenceTierManager | ✅ | Tier 1/2/3 bot management |
| BotMemoryManager | ✅ | Per-bot memory with personality |
| AIDecisionEngine | ✅ | LLM queue with caching |
| AliveWorldBridgeAdapter | ✅ | Event emission to bridge |
| mod-ec-realtime-bridge | ✅ | TCP communication layer |

### Phase 2: Actions & Behaviors (NEW)
**Status:** ✅ Complete

| Component | Status | Description |
|-----------|--------|-------------|
| BehaviorExecutor.h | ✅ | Behavior types and API |
| BehaviorExecutor.cpp | ✅ | Execute chat, movement, combat, economic, social |
| AliveWorldEventHooks.cpp | ✅ | Unified event capture (replaces mod-ollama-chat) |
| GossipManager.h | ✅ | Gossip types and spreading logic |
| GossipManager.cpp | ✅ | Template-based gossip creation/spreading |
| GoalSystem.cpp | ✅ | Multi-step goals (gather→craft→sell) |
| GoalManager.cpp | ✅ | Goal tracking and progress updates |
| StrategyParser.cpp | ✅ | Parse AI responses into actions (Jan 2025) |

### Phase 3: Intelligence Expansion
**Status:** 🔄 In Progress

| Feature | Status | Description |
|---------|--------|-------------|
| Personality Evolution | ✅ | Traits change based on experiences (BotMemoryManager) |
| Social Graph | ✅ | Track relationships, friend/foe (SocialMemory) |
| Economic Behavior | ✅ | Gather, craft, trade, AH (EconomySystem.cpp) |
| Combat Adaptation | ✅ | Change strategy based on outcomes (CombatMemory) |
| Zone Migration | ⏳ | Bots move to "interesting" areas |
| Faction Warfare | ⏳ | Coordinate faction-based activities |

### Phase 4: Polish & Scale (TODO)
**Status:** 📋 Planned

| Feature | Priority | Description |
|---------|----------|-------------|
| Performance Tuning | High | Handle 500+ bots efficiently |
| Response Caching | High | Reduce LLM calls with smart caching |
| Dashboard Enhancements | Medium | Better visualization, controls |
| Configuration UI | Medium | In-game GM commands for tuning |
| Documentation | Medium | Full API reference, guides |

---

## Behavior Categories

### 1. Chat Behaviors
```
CHAT_SAY         - Say to nearby players
CHAT_YELL        - Yell (wider range)
CHAT_EMOTE       - Perform emote (wave, dance, etc.)
CHAT_WHISPER     - Private message to player
CHAT_PARTY       - Party chat
CHAT_GUILD       - Guild chat
GOSSIP_SPREAD    - Share gossip with nearby bot
```

### 2. Movement Behaviors
```
MOVE_TO_ZONE     - Travel to different zone
MOVE_TO_POSITION - Walk to specific coordinates
MOVE_NEAR_ENTITY - Move toward player/NPC
MOVE_FLEE        - Run from danger
MOVE_PATROL      - Wander in area
MOVE_FOLLOW      - Follow target
```

### 3. Combat Behaviors
```
COMBAT_SET_STYLE   - Aggressive/defensive/passive/berserker/cautious
COMBAT_SET_TARGET  - Priority target selection
COMBAT_USE_ABILITY - Cast specific spell
COMBAT_FLEE        - Disengage and run
COMBAT_CALL_HELP   - Alert nearby allies
COMBAT_TAUNT       - Draw aggro
```

### 4. Economic Behaviors
```
ECON_GATHER      - Harvest mining/herb node
ECON_CRAFT       - Create item
ECON_TRADE_OFFER - Initiate trade
ECON_AH_LIST     - List item on AH
ECON_AH_BUY      - Buy from AH
ECON_VENDOR_SELL - Sell to vendor
```

### 5. Social Behaviors
```
SOCIAL_REQUEST_GROUP  - Ask to join group
SOCIAL_INVITE_GROUP   - Invite to group
SOCIAL_LEAVE_GROUP    - Leave group
SOCIAL_DUEL_REQUEST   - Challenge to duel
SOCIAL_GUILD_CHAT     - Guild discussions
```

### 6. Meta Behaviors
```
META_CHANGE_GOAL        - Update current objective
META_ADAPT_PERSONALITY  - Shift personality traits
META_REMEMBER_EVENT     - Store important memory
```

---

## Intelligence Tiers

| Tier | Name | Max Bots | AI Access | Behaviors |
|------|------|----------|-----------|-----------|
| 1 | Active | 5-10 | Full LLM | All behaviors, proactive |
| 2 | Aware | 50-100 | Queued LLM | Reactions, simple goals |
| 3 | Passive | 400+ | Templates only | Background, logging only |

### Tier Promotion Criteria
- **1 → Active**: Near player (<50y), in combat with player, whispered
- **2 → Aware**: Same zone as player, near interesting events
- **3 → Passive**: No players nearby, no recent activity

### Tier Demotion Criteria
- **Active → 2**: No player nearby for 30 seconds
- **Aware → 3**: No players in zone for 2 minutes

---

## Gossip System

### Gossip Types
```
CRIME        - Guard system crimes, wanted levels
BOSS_KILL    - World boss / raid boss defeats
RARE_SPAWN   - Rare creature appearances
ACHIEVEMENT  - Notable player achievements
HOLIDAY      - In-game events and holidays
ZONE_EVENT   - Raid, defense, world events
GENERAL      - Player activities, interesting happenings
WARNING      - Danger alerts, enemy presence
REACTION     - Emotional responses to events
```

### Spreading Mechanics
1. Bot learns gossip (witnesses event or hears from another bot)
2. Periodically checks for nearby bots who don't know the gossip
3. Roll spread chance (based on importance, age, spread count)
4. On success: Bot says the gossip text, target learns it
5. Gossip expires after TTL (default 1 hour)

### Spread Chance Formula
```
base_chance = importance * 5%   // 5-50% base
age_modifier = age < 1min ? 1.5 : 1.0
spread_penalty = max(0.1, 1 - spread_count * 0.1)
final_chance = min(base_chance * age_modifier * spread_penalty, 75%)
```

---

## Goal System (Planned)

### Goal Types
```go
GOAL_GATHER_RESOURCE  // Collect X of item Y
GOAL_CRAFT_ITEM       // Create item from materials
GOAL_SELL_ITEM        // Sell item at AH or vendor
GOAL_REACH_LOCATION   // Travel to specific place
GOAL_JOIN_GROUP       // Find and join a group
GOAL_COMPLETE_QUEST   // Turn in a quest
GOAL_LEVEL_UP         // Gain experience
GOAL_LEARN_SKILL      // Train a profession/skill
```

### Goal Structure
```cpp
struct Goal {
    GoalType type;
    uint32 targetId;      // Item ID, zone ID, etc.
    uint32 quantity;      // How many to collect
    uint32 progress;      // Current progress
    BehaviorPriority priority;
    std::vector<Goal> subgoals;  // Decomposed steps
};
```

### Example: "Make money from mining"
```
GOAL_SELL_ITEM (copper ore)
  └─ GOAL_GATHER_RESOURCE (copper ore, 20)
       └─ GOAL_REACH_LOCATION (Elwynn Forest mining area)
```

---

## Strategy Parsing (✅ IMPLEMENTED)

The AI returns text responses that need to be parsed into actions.
**Implementation**: `StrategyParser.cpp` (Jan 2025)

### Response Format (LLM Output)
```
[STRATEGY]
style: cautious
target: nearest_enemy
action: flee_if_health_low
[CHAT]
"This doesn't look good... maybe we should retreat?"
[EMOTE]
worried
```

### Parsing Rules
1. Extract [STRATEGY] block → apply combat/movement changes
2. Extract [CHAT] block → queue Say behavior
3. Extract [EMOTE] block → queue Emote behavior
4. Fall back to simple text response if no blocks found

---

## Database Tables

### ec_alive_bot_memory (characters DB)
```sql
CREATE TABLE ec_alive_bot_memory (
    guid BIGINT PRIMARY KEY,
    name VARCHAR(64),
    -- Personality (0-100 scale)
    aggression TINYINT DEFAULT 50,
    caution TINYINT DEFAULT 50,
    sociability TINYINT DEFAULT 50,
    loyalty TINYINT DEFAULT 50,
    curiosity TINYINT DEFAULT 50,
    vengefulness TINYINT DEFAULT 50,
    -- Combat stats
    total_kills INT DEFAULT 0,
    total_deaths INT DEFAULT 0,
    pvp_kills INT DEFAULT 0,
    pvp_deaths INT DEFAULT 0,
    -- Activity
    last_seen BIGINT,
    current_zone INT,
    current_goal TEXT,
    -- Serialized data
    combat_log TEXT,
    social_connections TEXT,
    witnessed_events TEXT
);
```

### ec_gossip_pool (characters DB)
```sql
CREATE TABLE ec_gossip_pool (
    id INT PRIMARY KEY AUTO_INCREMENT,
    type TINYINT,
    zone_id INT,
    text TEXT,
    subject VARCHAR(128),
    importance TINYINT,
    created_at BIGINT,
    expires_at BIGINT,
    spread_count INT DEFAULT 0,
    source_guid BIGINT,
    source_name VARCHAR(64)
);
```

### ec_gossip_templates (world DB)
```sql
CREATE TABLE ec_gossip_templates (
    id INT PRIMARY KEY AUTO_INCREMENT,
    type TINYINT,
    pattern TEXT,
    probability INT DEFAULT 100,
    faction_id INT DEFAULT 0,
    enabled TINYINT DEFAULT 1
);
```

---

## Performance Budget

### Memory
| Component | Per Bot | 500 Bots |
|-----------|---------|----------|
| Memory Object | 2 KB | 1 MB |
| Combat Log (5 entries) | 500 B | 250 KB |
| Social Connections | 1 KB | 500 KB |
| Gossip Knowledge | 200 B | 100 KB |
| **Total** | ~4 KB | ~2 MB |

### CPU (per tick, 100ms)
| Component | Time | Notes |
|-----------|------|-------|
| Tier Updates | 5 ms | Check distances |
| Behavior Queue | 10 ms | Process up to 50 |
| Gossip Spreading | 2 ms | Periodic, not every tick |
| Event Processing | 5 ms | Queue, don't block |
| **Total** | ~22 ms | Well under 100ms budget |

### LLM Requests
| Tier | Bots | Requests/min | Tokens/min |
|------|------|--------------|------------|
| 1 | 10 | 20 | 4000 |
| 2 | 100 | 10 | 2000 |
| 3 | 400 | 0 | 0 |
| **Total** | 510 | 30 | 6000 |

At 30 requests/min, with 2-5 second response times, we need ~2-3 concurrent requests max.

---

## Configuration

### mod_alive_world.conf.dist
```ini
# =============================================================================
# ALIVE WORLD CONFIGURATION
# =============================================================================

# Enable the system
AliveWorld.Enable = 1

# Debug logging
AliveWorld.Debug = 0
AliveWorld.Debug.Level = 1  # 0=off, 1=basic, 2=verbose, 3=trace

# Intelligence Tiers
AliveWorld.Tier1.MaxBots = 10
AliveWorld.Tier1.PlayerDistance = 50.0
AliveWorld.Tier2.MaxBots = 100
AliveWorld.Tier2.ZoneCheck = 1
AliveWorld.TierUpdateIntervalMs = 5000

# Behavior Executor
AliveWorld.BehaviorExecutor.Enable = 1
AliveWorld.BehaviorExecutor.MaxPerTick = 50
AliveWorld.BehaviorExecutor.CooldownMs = 100

# Gossip System
AliveWorld.Gossip.Enable = 1
AliveWorld.Gossip.MaxPoolSize = 500
AliveWorld.Gossip.SpreadIntervalMs = 10000
AliveWorld.Gossip.SpreadRange = 50.0
AliveWorld.Gossip.DefaultTTLSeconds = 3600

# AI Decision Engine
AliveWorld.AI.Enable = 1
AliveWorld.AI.MaxConcurrent = 5
AliveWorld.AI.TimeoutSeconds = 30
AliveWorld.AI.CacheEnable = 1
AliveWorld.AI.CacheTTLSeconds = 1800

# Ollama Connection
AliveWorld.Ollama.Host = "127.0.0.1"
AliveWorld.Ollama.Port = 11434
AliveWorld.Ollama.Model = "mistral"

# Bridge Connection
AliveWorld.Bridge.Host = "127.0.0.1"
AliveWorld.Bridge.Port = 8092
```

---

## GM Commands

```
.aw status              - Show system status
.aw debug [on/off]      - Toggle debug mode
.aw debug level <0-3>   - Set debug verbosity
.aw reload              - Reload configuration
.aw tier <bot> <1-3>    - Force bot to tier
.aw gossip add <text>   - Create test gossip
.aw gossip list         - List active gossip
.aw gossip clear        - Clear all gossip
.aw bot memory <name>   - Show bot's memory
.aw bot goal <name> <goal> - Set bot's current goal
.aw behavior <name> <type> - Force bot behavior
```

---

## Integration with Other Modules

### mod-ec-guard-system
```cpp
// On crime committed
sAliveWorldMgr->ProcessWorldEvent({
    .type = WorldEventType::CRIME_COMMITTED,
    .actorGuid = criminal->GetGUID(),
    .zoneId = zoneId,
    .data = {{"stars", "3"}, {"crimeType", "murder"}}
});
```

### mod-ec-world-events
```cpp
// On world event started
sAliveWorldMgr->ProcessWorldEvent({
    .type = WorldEventType::WORLD_EVENT_START,
    .zoneId = zoneId,
    .data = {{"eventName", "Stormwind Raid"}, {"eventType", "raid"}}
});
```

### mod-playerbots
```cpp
// Get PlayerbotAI for behavior execution
PlayerbotAI* ai = sPlayerbotsMgr->GetPlayerbotAI(bot);
ai->Say("Hello!");
ai->ChangeStrategy("+aggressive", BOT_STATE_COMBAT);
```

---

## Testing Checklist

### Unit Tests
- [ ] BehaviorExecutor queuing and execution
- [ ] GossipManager creation and spreading
- [ ] IntelligenceTierManager tier changes
- [ ] BotMemoryManager persistence

### Integration Tests
- [ ] Event hooks trigger correctly
- [ ] Bridge communication works
- [ ] LLM requests complete
- [ ] Response caching works

### Load Tests
- [ ] 500 bots with tier system
- [ ] 30 LLM requests/minute
- [ ] 500 gossip items
- [ ] Memory stays under 50MB

### Manual Tests
- [ ] Bot reacts to player kill
- [ ] Bot spreads gossip to another bot
- [ ] Bot changes combat style
- [ ] Bot flees from danger
- [ ] Dashboard shows real-time events

---

## Next Steps

1. ✅ ~~Update cmake to include new files~~ (Done)
2. ✅ ~~Implement GoalSystem for multi-step behaviors~~ (Done - GoalSystem.cpp, GoalManager.cpp)
3. ✅ ~~Implement StrategyParser for AI response handling~~ (Done - StrategyParser.cpp, Jan 2025)
4. **Now**: Test StrategyParser with live server, tune performance
5. **Next**: Implement Zone Migration for bot movement to active areas
6. **Ongoing**: Add more gossip templates, behavior variations

---

## References

- mod-playerbots API: See PlayerbotAI.h
- mod-ollama-chat for LLM patterns: See mod-ollama-chat_api.cpp
- AzerothCore event hooks: See ScriptMgr.h
- Bridge protocol: See EC_MODULES.md
