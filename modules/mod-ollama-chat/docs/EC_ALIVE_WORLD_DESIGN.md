# EC Alive World System - Design Document

**Author:** EC
**Date:** 2025-01-25
**Status:** Design Phase

## Vision

Transform the WoW private server from a static world with simple bots into a **living, breathing ecosystem** where:
- Bots perceive, remember, and react to world events
- Information spreads through gossip and rumors
- Bots evolve over time based on experiences
- AI is used intelligently without overwhelming the system

## Core Problem: 500 Bots Can't All Think at Once

With 500 bots, we can't give each one a full-time AI brain. The solution:

### Intelligence Tiering

```
┌─────────────────────────────────────────────────────────────────┐
│                     BOT INTELLIGENCE TIERS                       │
├─────────────────────────────────────────────────────────────────┤
│                                                                  │
│  TIER 1: ACTIVE BRAIN (5-10 bots max at any time)               │
│  ┌─────────────────────────────────────────────────────────┐    │
│  │ • Near real players (within 100 yards)                   │    │
│  │ • In combat with real players                            │    │
│  │ • Involved in active events                              │    │
│  │ • Recently spoken to                                     │    │
│  │ → Full LLM access, complex decision making               │    │
│  └─────────────────────────────────────────────────────────┘    │
│                                                                  │
│  TIER 2: AWARE (50-100 bots)                                    │
│  ┌─────────────────────────────────────────────────────────┐    │
│  │ • Same zone as real players                              │    │
│  │ • In party/raid with Tier 1 bots                         │    │
│  │ • Near world events                                      │    │
│  │ → Event logging, basic reactions, queued LLM access      │    │
│  └─────────────────────────────────────────────────────────┘    │
│                                                                  │
│  TIER 3: PASSIVE (400+ bots)                                    │
│  ┌─────────────────────────────────────────────────────────┐    │
│  │ • No players nearby                                      │    │
│  │ • Background activities                                  │    │
│  │ → Minimal logging, scripted behavior only                │    │
│  └─────────────────────────────────────────────────────────┘    │
│                                                                  │
└─────────────────────────────────────────────────────────────────┘
```

## Architecture: AI Bridge System

```
┌────────────────────────────────────────────────────────────────────────┐
│                         ALIVE WORLD ARCHITECTURE                        │
├────────────────────────────────────────────────────────────────────────┤
│                                                                         │
│  ┌─────────────┐     ┌──────────────┐     ┌─────────────────────────┐ │
│  │ AzerothCore │     │ mod-playerbots│     │ mod-ollama-chat         │ │
│  │   Server    │────▶│   (Actions)   │────▶│ (Current: Chat Only)    │ │
│  └─────────────┘     └──────────────┘     └─────────────────────────┘ │
│         │                   │                         │                │
│         │                   │                         │                │
│         ▼                   ▼                         ▼                │
│  ┌─────────────────────────────────────────────────────────────────┐  │
│  │                   mod-alive-world (NEW)                          │  │
│  │  ┌─────────────────────────────────────────────────────────────┐│  │
│  │  │                    EVENT COLLECTOR                          ││  │
│  │  │  • Combat events (kills, deaths, damage)                    ││  │
│  │  │  • Social events (chat, emotes, trades)                     ││  │
│  │  │  • World events (rare spawns, bosses, festivals)            ││  │
│  │  │  • Zone events (players entering/leaving)                   ││  │
│  │  └─────────────────────────────────────────────────────────────┘│  │
│  │                            │                                     │  │
│  │                            ▼                                     │  │
│  │  ┌─────────────────────────────────────────────────────────────┐│  │
│  │  │                    WORLD STATE MANAGER                      ││  │
│  │  │  • Zone heat maps (where players are)                       ││  │
│  │  │  • Recent events buffer (last 10 min)                       ││  │
│  │  │  • Gossip pool (shareable news)                             ││  │
│  │  │  • Bot social graph (who knows who)                         ││  │
│  │  └─────────────────────────────────────────────────────────────┘│  │
│  │                            │                                     │  │
│  │                            ▼                                     │  │
│  │  ┌─────────────────────────────────────────────────────────────┐│  │
│  │  │                    BOT MEMORY SYSTEM                        ││  │
│  │  │  Per-bot lightweight memory:                                 ││  │
│  │  │  • Combat summary (last 5 fights)                           ││  │
│  │  │  • Social log (recent conversations)                        ││  │
│  │  │  • Location history (zones visited)                         ││  │
│  │  │  • Personality traits (evolving)                            ││  │
│  │  │  • Reputation with players                                  ││  │
│  │  └─────────────────────────────────────────────────────────────┘│  │
│  │                            │                                     │  │
│  │                            ▼                                     │  │
│  │  ┌─────────────────────────────────────────────────────────────┐│  │
│  │  │                    AI DECISION ENGINE                       ││  │
│  │  │  • Intelligence tier manager                                 ││  │
│  │  │  • LLM request queue (max 5 concurrent)                     ││  │
│  │  │  • Strategy adapter (changes bot behavior)                  ││  │
│  │  │  • Response cache (similar situations → cached response)    ││  │
│  │  └─────────────────────────────────────────────────────────────┘│  │
│  └─────────────────────────────────────────────────────────────────┘  │
│                            │                                          │
│                            ▼                                          │
│  ┌─────────────────────────────────────────────────────────────────┐  │
│  │                       OLLAMA LLM                                 │  │
│  │  • Queued requests (rate limited)                               │  │
│  │  • Response caching                                              │  │
│  │  • Priority system (Tier 1 > Tier 2)                            │  │
│  └─────────────────────────────────────────────────────────────────┘  │
│                                                                         │
└────────────────────────────────────────────────────────────────────────┘
```

## Component Details

### 1. Event Collector

Captures everything happening in the world (lightweight, always-on):

```cpp
enum class WorldEventType {
    // Combat
    PLAYER_KILLED_MOB,
    PLAYER_KILLED_PLAYER,
    BOT_KILLED_MOB,
    BOSS_ENGAGED,
    BOSS_KILLED,
    WIPE,

    // Social
    PLAYER_CHAT,
    PLAYER_EMOTE,
    TRADE_COMPLETED,
    GUILD_INVITE,
    GROUP_FORMED,

    // World
    RARE_SPAWN,
    WORLD_BOSS_SPAWN,
    FESTIVAL_START,
    GUARD_CALLED,      // Your guard system!

    // Zone
    PLAYER_ENTERED_ZONE,
    PLAYER_LEFT_ZONE,
    ZONE_BECAME_EMPTY,
    ZONE_BECAME_ACTIVE
};

struct WorldEvent {
    WorldEventType type;
    uint32 timestamp;
    uint32 zoneId;
    uint32 mapId;
    ObjectGuid primaryActor;    // Who did it
    ObjectGuid secondaryActor;  // To whom
    std::string details;        // "Onyxia killed by <Legends>"
    uint8 importance;           // 1-10 scale
};
```

### 2. World State Manager

Maintains a shared "world knowledge" that any bot can access:

```cpp
struct ZoneState {
    uint32 zoneId;
    uint32 alliancePlayerCount;
    uint32 hordePlayerCount;
    uint32 botCount;
    std::vector<WorldEvent> recentEvents;  // Last 10 minutes
    uint8 dangerLevel;      // PvP activity
    uint8 activityLevel;    // Overall activity
};

struct GossipItem {
    std::string headline;   // "Onyxia was slain last night!"
    std::string details;    // "The guild <Legends> took her down..."
    uint32 timestamp;
    uint32 zoneId;          // Where it happened
    uint8 spreadRadius;     // How far the gossip has spread
    uint8 excitement;       // How exciting is this news
};

class WorldStateManager {
    std::map<uint32, ZoneState> zoneStates;
    std::deque<GossipItem> gossipPool;     // Max 50 items
    std::map<uint64, std::set<uint64>> socialGraph;  // Who knows who

    // Called frequently
    void UpdateZoneStates();
    void PropagateGossip();

    // Bot queries
    std::vector<GossipItem> GetGossipForBot(Player* bot);
    ZoneState GetZoneState(uint32 zoneId);
    bool IsHotZone(uint32 zoneId);
};
```

### 3. Bot Memory System

Lightweight per-bot memory (stored in DB, cached in memory):

```cpp
struct BotMemory {
    uint64 botGuid;

    // Combat summary (last 5 encounters)
    struct CombatMemory {
        uint32 timestamp;
        bool wasVictorious;
        std::string opponentType;  // "player", "elite", "boss"
        std::string opponentName;
        uint32 damageDealt;
        uint32 damageTaken;
        uint32 healingDone;
    };
    std::deque<CombatMemory> combatLog;  // Max 5

    // Social memory
    struct SocialMemory {
        uint64 playerGuid;
        std::string playerName;
        float sentimentScore;      // -1.0 to 1.0
        uint32 interactionCount;
        std::string lastTopic;
    };
    std::map<uint64, SocialMemory> knownPlayers;

    // Location memory
    std::map<uint32, uint32> zoneTimeSpent;  // zoneId → minutes
    uint32 favoriteZone;

    // Evolving personality
    float aggression;       // 0-1, increases with PvP
    float helpfulness;      // 0-1, increases with group play
    float chattiness;       // 0-1, varies
    float curiosity;        // 0-1, affects exploration
    std::string currentMood;  // "excited", "bored", "angry", etc.
};
```

### 4. AI Decision Engine

The brain that decides when and how to use AI:

```cpp
class AIDecisionEngine {
    // Tier management
    std::set<uint64> tier1Bots;  // Active brain (max 10)
    std::set<uint64> tier2Bots;  // Aware (max 100)
    // All others are Tier 3 (passive)

    // LLM queue
    struct LLMRequest {
        uint64 botGuid;
        std::string prompt;
        uint8 priority;  // 1 = urgent, 5 = low
        std::function<void(std::string)> callback;
    };
    std::priority_queue<LLMRequest> llmQueue;
    uint8 concurrentRequests = 0;
    const uint8 MAX_CONCURRENT = 5;

    // Response cache
    std::map<size_t, std::string> responseCache;  // hash(context) → response

    // Main loop (called every second)
    void Update() {
        UpdateTiers();           // Reassign bots to tiers
        ProcessLLMQueue();       // Send queued requests
        PropagateDecisions();    // Send actions to bots
    }

    // Decision making
    void OnBotNeedsDecision(Player* bot, DecisionContext context) {
        uint8 tier = GetBotTier(bot->GetGUID());

        if (tier == 1) {
            // Full AI - queue LLM request
            QueueLLMRequest(bot, context, PRIORITY_HIGH);
        } else if (tier == 2) {
            // Check cache first, then queue with lower priority
            if (auto cached = CheckCache(context)) {
                ApplyDecision(bot, cached);
            } else {
                QueueLLMRequest(bot, context, PRIORITY_LOW);
            }
        } else {
            // Tier 3 - scripted response only
            ApplyScriptedDecision(bot, context);
        }
    }
};
```

## Gossip System

Bots share information like real players would:

```cpp
// When a bot hears gossip or witnesses an event
void Bot::LearnGossip(const GossipItem& gossip) {
    memory.knownGossip.push_back(gossip);

    // Chance to share with nearby bots
    if (roll_chance(30)) {
        for (auto* nearbyBot : GetNearbyBots(50.0f)) {
            nearbyBot->LearnGossip(gossip);
        }
    }
}

// When bot becomes active and wants to chat
std::string Bot::GenerateGossipChat() {
    auto gossip = memory.knownGossip.front();

    // Prompt for LLM
    return fmt::format(
        "You are {} the {}. You just heard: '{}'. "
        "Share this news naturally in 1-2 sentences as if gossiping.",
        GetName(), GetClassName(), gossip.headline
    );
}
```

### Example Gossip Scenarios

1. **World Boss Kill**
   - Event: `<Legends>` kills Onyxia
   - Gossip: "Did you hear? Onyxia was slain last night!"
   - Spread: Bots in Stormwind start talking about it
   - Evolution: Day later, "I heard the loot was incredible..."

2. **Player Ganking**
   - Event: Rogue kills 5 lowbies in Redridge
   - Gossip: "Watch out in Redridge, some Rogue is on a rampage!"
   - Spread: Alliance bots warn each other
   - Evolution: Guards called, "Justice was served!"

3. **Rare Spawn**
   - Event: Time-Lost Proto Drake spawns
   - Gossip: "Someone spotted the Time-Lost Drake in Storm Peaks!"
   - Spread: Bots might head that direction
   - Action: Some bots actually go hunting

## Strategy Adaptation

Bots change their behavior based on events and memory:

```cpp
void Bot::AdaptStrategy() {
    // Based on recent combat
    if (memory.combatLog.back().wasVictorious == false &&
        memory.combatLog.back().opponentType == "player") {
        // Lost to a player - become more cautious
        personality.aggression -= 0.1f;
        SetStrategy("defensive");
    }

    // Based on zone activity
    auto zoneState = worldState.GetZoneState(GetZoneId());
    if (zoneState.dangerLevel > 7 && GetTeamId() == TEAM_HORDE) {
        // Danger! Either flee or call for backup
        if (personality.aggression > 0.7f) {
            CallForBackup();  // Aggressive bots fight
        } else {
            FleeToSafeZone(); // Passive bots run
        }
    }

    // Based on player reputation
    if (auto* nearbyPlayer = GetNearbyRealPlayer()) {
        auto sentiment = memory.GetSentiment(nearbyPlayer);
        if (sentiment > 0.8f) {
            // Friend! Be helpful
            SetStrategy("follow_friend");
            if (roll_chance(20)) {
                Say("Hey! Good to see you again!");
            }
        } else if (sentiment < -0.5f) {
            // Enemy! Be hostile
            if (personality.aggression > 0.6f) {
                SetStrategy("pvp");
            } else {
                AvoidPlayer(nearbyPlayer);
            }
        }
    }
}
```

## Database Schema

```sql
-- Bot persistent memory
CREATE TABLE bot_memory (
    bot_guid BIGINT PRIMARY KEY,
    combat_log JSON,           -- Last 5 combats
    social_memory JSON,        -- Known players + sentiment
    zone_history JSON,         -- Time spent per zone
    personality JSON,          -- Traits (aggression, helpfulness, etc.)
    known_gossip JSON,         -- Gossip they've heard
    last_updated TIMESTAMP
);

-- World gossip pool (shared)
CREATE TABLE world_gossip (
    id INT AUTO_INCREMENT PRIMARY KEY,
    headline VARCHAR(255),
    details TEXT,
    zone_id INT,
    importance TINYINT,
    created_at TIMESTAMP,
    expires_at TIMESTAMP
);

-- World events log (for history/analytics)
CREATE TABLE world_events (
    id BIGINT AUTO_INCREMENT PRIMARY KEY,
    event_type VARCHAR(50),
    zone_id INT,
    primary_actor BIGINT,
    secondary_actor BIGINT,
    details TEXT,
    timestamp TIMESTAMP,
    INDEX idx_zone_time (zone_id, timestamp),
    INDEX idx_type_time (event_type, timestamp)
);
```

## Performance Budget

| Component | Max Memory | Max CPU/tick | DB Writes/min |
|-----------|------------|--------------|---------------|
| Event Collector | 10 MB | 1 ms | 0 (buffered) |
| World State | 50 MB | 5 ms | 0 (in-memory) |
| Bot Memory (500 bots) | 100 MB | 10 ms | 10 (batched) |
| AI Decision Engine | 20 MB | 20 ms | 0 |
| LLM Queue | 5 MB | varies | 0 |
| **Total** | **185 MB** | **36 ms** | **10** |

## LLM Request Budget

| Scenario | Requests/min | Priority |
|----------|--------------|----------|
| Tier 1 bot chat | 10-20 | HIGH |
| Tier 2 bot reactions | 5-10 | MEDIUM |
| Strategy decisions | 2-5 | LOW |
| Gossip generation | 1-2 | LOW |
| **Total** | **~30/min** | - |

With 5 concurrent requests and ~2 second response time:
- Throughput: ~150 requests/min capacity
- We're using ~30/min = **20% capacity** = safe!

## Implementation Phases

### Phase 1: Foundation (Week 1-2)
- [ ] Create mod-alive-world module skeleton
- [ ] Event Collector with basic hooks
- [ ] Zone State tracking
- [ ] Bot tier system

### Phase 2: Memory (Week 2-3)
- [ ] Bot Memory system
- [ ] Database schema
- [ ] Memory persistence

### Phase 3: Gossip (Week 3-4)
- [ ] Gossip pool
- [ ] Gossip spreading logic
- [ ] LLM gossip generation

### Phase 4: Strategy (Week 4-5)
- [ ] Strategy adaptation
- [ ] AI Decision Engine
- [ ] LLM queue system

### Phase 5: Polish (Week 5-6)
- [ ] Response caching
- [ ] Performance tuning
- [ ] Testing at scale

## Integration with Existing Systems

### mod-ollama-chat
- Keep for chat generation
- Add hooks to receive decisions from AI Engine
- Share personality/memory data

### mod-playerbots
- Add event hooks for combat, movement, etc.
- Expose strategy change API
- Share position/zone data

### Player Proximity Spawning (new feature)
- Feed zone heat maps from World State
- Prioritize spawning in active zones

### mod-ec-world-events
- World events feed into Event Collector
- Bots can participate in events based on interest

## Example: A Day in the Life of a Bot

```
08:00 - Bot "Thralok" wakes up in Orgrimmar (Tier 3: Passive)
        → Running basic idle behavior, logging zone

08:15 - Player enters Orgrimmar, Thralok is nearby (Tier 2: Aware)
        → Starts logging events, might wave or /nod

08:20 - Player says "Hey, anyone want to run dungeons?"
        → Thralok promoted to Tier 1: Active
        → LLM generates: "I'm down! Tank or DPS?"

08:25 - Grouped with player, heading to dungeon (Tier 1: Active)
        → Full AI, making combat decisions, chatting

09:00 - Dungeon complete, player logs off (Tier 3: Passive)
        → Memory updated: "Ran dungeon with PlayerX, got loot"
        → Personality shift: helpfulness +0.05

09:30 - Thralok travels to Barrens, passes another bot
        → Shares gossip: "Hey, did a dungeon run this morning!"
        → Other bot learns about PlayerX being friendly

10:00 - Rare spawn appears nearby (Tier 2: Aware)
        → Thralok logs it, maybe heads toward it
        → If player nearby, might mention it: "Whoa, is that a rare?"
```

## Conclusion

This design gives us:
1. **Scalable AI** - Only 5-10 bots thinking at once
2. **Living world** - Events propagate, gossip spreads
3. **Evolving bots** - Personalities change over time
4. **Efficient resources** - ~20% LLM capacity, <200MB RAM
5. **Modular architecture** - Clean separation of concerns

The world becomes alive not because every bot is an AI genius, but because information flows naturally and bots react to their environment in believable ways.
