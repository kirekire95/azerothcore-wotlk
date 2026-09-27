/*
 * mod-ec-realtime-bridge - Shared Realtime Communication Bridge
 * Copyright (C) 2025 EC
 *
 * Unified type definitions for the realtime bridge protocol.
 * All EC modules use these types to communicate with the Node.js backend.
 *
 * Protocol:
 *   - Messages are newline-delimited JSON
 *   - Each message has a "ns" (namespace) field identifying the source module
 *   - The "type" field identifies the specific event/command
 */

#ifndef EC_REALTIME_BRIDGE_TYPES_H
#define EC_REALTIME_BRIDGE_TYPES_H

#include "Common.h"
#include <string>
#include <vector>
#include <functional>
#include <chrono>

namespace ECRealtimeBridge
{

// ============================================================================
// NAMESPACES - Identify which module an event belongs to
// ============================================================================

enum class EventNamespace : uint8
{
    SYSTEM = 0,      // Bridge system events (heartbeat, auth, etc.)
    ALIVE_WORLD,     // mod-alive-world (AI bots)
    GUARD_SYSTEM,    // mod-ec-guard-system (wanted levels)
    WORLD_EVENTS,    // mod-ec-world-events (raids, world bosses)
    OLLAMA_CHAT,     // mod-ollama-chat (NPC chat)
    PLAYERBOTS,      // mod-playerbots (bot management)
    LIFE_EVENTS,     // Life events from external apps (Korean Quest, etc.)
    COMBAT,          // mod-ec-combat (kill feed, arena/bg tracking)
};

inline const char* NamespaceToString(EventNamespace ns)
{
    switch (ns)
    {
        case EventNamespace::SYSTEM:       return "system";
        case EventNamespace::ALIVE_WORLD:  return "alive";
        case EventNamespace::GUARD_SYSTEM: return "guard";
        case EventNamespace::WORLD_EVENTS: return "event";
        case EventNamespace::OLLAMA_CHAT:  return "chat";
        case EventNamespace::PLAYERBOTS:   return "bots";
        case EventNamespace::LIFE_EVENTS:  return "life";
        case EventNamespace::COMBAT:       return "combat";
        default:                           return "unknown";
    }
}

inline EventNamespace StringToNamespace(const std::string& str)
{
    if (str == "system") return EventNamespace::SYSTEM;
    if (str == "alive")  return EventNamespace::ALIVE_WORLD;
    if (str == "guard")  return EventNamespace::GUARD_SYSTEM;
    if (str == "event")  return EventNamespace::WORLD_EVENTS;
    if (str == "chat")   return EventNamespace::OLLAMA_CHAT;
    if (str == "bots")   return EventNamespace::PLAYERBOTS;
    if (str == "life")   return EventNamespace::LIFE_EVENTS;
    if (str == "combat") return EventNamespace::COMBAT;
    return EventNamespace::SYSTEM;
}

// ============================================================================
// SYSTEM EVENT TYPES
// ============================================================================

enum class SystemEventType : uint8
{
    STARTUP = 0,
    SHUTDOWN = 1,
    HEARTBEAT = 2,
    AUTH_REQUEST = 3,
    AUTH_RESPONSE = 4,
    ERROR = 5,
    DEBUG = 6,
};

// ============================================================================
// GUARD SYSTEM EVENT TYPES
// ============================================================================

enum class GuardEventType : uint8
{
    // Crimes
    CRIME_COMMITTED = 0,
    CIVILIAN_KILLED = 1,
    GUARD_ATTACKED = 2,
    GUARD_KILLED = 3,

    // Wanted state
    WANTED_LEVEL_CHANGED = 10,
    WANTED_CLEARED = 11,
    WANTED_DECAYED = 12,

    // Guards
    GUARDS_ALERTED = 20,
    GUARDS_DISPATCHED = 21,
    GUARDS_SPAWNED = 22,
    GUARD_ENGAGED = 23,

    // Broadcast
    LOCAL_DEFENCE_ALERT = 30,

    // Zone threat system
    ZONE_PHASE_CHANGED = 40,        // Phase transition (QUIET/UNEASY/MOBILIZING/IMPACT/AFTERMATH)
    ZONE_STATE_CHANGED = 41,        // Alarm or control state changed
    ZONE_HEAT_CHANGED = 42,         // Heat accumulated or decayed
    ZONE_FALLEN = 43,               // Zone has fallen to attackers
    ZONE_RECOVERED = 44,            // Zone recovered from attack

    // Bounty hunters
    BOUNTY_HUNTER_SPAWNED = 50,
    BOUNTY_HUNTER_TRACKING = 51,    // Cross-zone tracking update
    BOUNTY_HUNTER_DESPAWNED = 52,

    // Elite response
    ELITE_RESPONSE_TRIGGERED = 60,
    ELITE_FORCES_ARRIVING = 61,
    ELITE_FORCES_ARRIVED = 62,
};

// ============================================================================
// WORLD EVENTS EVENT TYPES
// ============================================================================

enum class WorldEventType : uint8
{
    // Event lifecycle
    EVENT_SCHEDULED = 0,
    EVENT_STARTING = 1,
    EVENT_STARTED = 2,
    EVENT_ENDING = 3,
    EVENT_ENDED = 4,
    EVENT_CANCELLED = 5,

    // Participants
    PARTICIPANT_JOINED = 10,
    PARTICIPANT_LEFT = 11,
    PARTICIPANT_DIED = 12,
    PARTICIPANT_REWARDED = 13,

    // Combat
    BOSS_ENGAGED = 20,
    BOSS_KILLED = 21,
    OBJECTIVE_COMPLETED = 22,

    // Bots
    BOTS_RECRUITED = 30,
    BOTS_DISMISSED = 31,
};

// ============================================================================
// LIFE EVENTS EVENT TYPES (Korean Quest integration)
// ============================================================================

enum class LifeEventType : uint8
{
    // Scholarship rank changes
    SCHOLARSHIP_RANK_CHANGED = 0,       // Player promoted/demoted in league
    SCHOLARSHIP_STREAK_MILESTONE = 1,   // Study streak milestone reached
    SCHOLARSHIP_STREAK_BROKEN = 2,      // Study streak broken

    // Learning progress
    SCHOLARSHIP_LESSON_COMPLETE = 10,   // Completed a lesson
    SCHOLARSHIP_CHAPTER_COMPLETE = 11,  // Completed a chapter/unit
    SCHOLARSHIP_MILESTONE = 12,         // XP or level milestone

    // Arena (competitive learning)
    SCHOLARSHIP_ARENA_RESULT = 20,      // Won/lost arena match
    SCHOLARSHIP_ACHIEVEMENT = 21,       // Unlocked achievement

    // Snapshot update
    SCHOLARSHIP_SNAPSHOT = 30,          // Full state update
};

// ============================================================================
// COMBAT EVENT TYPES (Kill feed, Arena/BG tracking)
// ============================================================================

enum class CombatEventType : uint8
{
    // Kill Feed - PvP kills
    PVP_KILL = 0,               // Player killed another player
    PVP_DEATH = 1,              // Player was killed by another player
    CREATURE_KILL = 2,          // Player killed a creature
    KILLED_BY_CREATURE = 3,     // Player killed by a creature
    DUEL_COMPLETE = 4,          // Duel ended

    // Kill Streaks
    KILL_STREAK_STARTED = 10,   // Player started a kill streak (3+ kills)
    KILL_STREAK_MILESTONE = 11, // Player reached kill streak milestone (5, 10, 15, etc.)
    KILL_STREAK_ENDED = 12,     // Kill streak was ended by death/logout

    // Arena
    ARENA_MATCH_START = 20,     // Arena match started
    ARENA_MATCH_END = 21,       // Arena match ended
    ARENA_RATING_CHANGE = 22,   // Player's arena rating changed
    ARENA_KILL = 23,            // Kill within arena match

    // Battleground
    BG_MATCH_START = 30,        // Battleground match started
    BG_MATCH_END = 31,          // Battleground match ended
    BG_KILL = 32,               // Kill within battleground
    BG_FLAG_CAPTURE = 33,       // Flag captured (WSG, EotS)
    BG_FLAG_RETURN = 34,        // Flag returned (WSG)
    BG_OBJECTIVE_CAPTURE = 35,  // Objective captured (AB, AV, etc.)
    BG_OBJECTIVE_DEFEND = 36,   // Objective defended
    BG_HONOR_KILL = 37,         // Honorable kill in BG

    // Multi-kills (rapid successive kills)
    MULTI_KILL = 40,            // Double kill, triple kill, etc.
    FIRST_BLOOD = 41,           // First kill of the match
};

// ============================================================================
// SCHOLARSHIP STATE - Player's real-life learning progress
// ============================================================================

/**
 * ScholarshipState - Stored state of player's Korean learning progress
 *
 * This is updated from the Node.js backend via life events.
 * NPCs and game systems can reference this data.
 */
struct ScholarshipState
{
    // Scholar rank (translated from Korean Quest league)
    std::string scholarRank;        // "Novice Scholar" -> "Grand Archivist"
    std::string league;             // BRONZE, SILVER, GOLD, PLATINUM, DIAMOND, MASTER, GRANDMASTER
    uint8 tier;                     // 1-3 within league
    float percentile;               // 0-100

    // Progress
    uint32 level;
    uint32 totalXp;
    uint32 currentStreak;           // Days of consistent study
    uint32 longestStreak;

    // Mastery
    std::vector<std::string> disciplines;       // Strong areas
    std::vector<std::string> weakDisciplines;   // Areas needing work

    // Vocabulary
    uint32 vocabTotal;
    uint32 vocabMastered;
    uint32 vocabLearning;
    float weeklyAccuracy;           // 0-100%

    // Arena
    uint32 arenaWins;
    uint32 arenaLosses;
    float winRate;

    // Timestamps
    uint64 lastUpdated;             // Unix timestamp

    ScholarshipState()
        : tier(0), percentile(0), level(1), totalXp(0)
        , currentStreak(0), longestStreak(0)
        , vocabTotal(0), vocabMastered(0), vocabLearning(0), weeklyAccuracy(0)
        , arenaWins(0), arenaLosses(0), winRate(50.0f)
        , lastUpdated(0)
    {
        scholarRank = "Unknown";
        league = "UNRANKED";
    }

    // Check if player has valid scholarship data
    bool IsValid() const { return lastUpdated > 0; }

    // Get intensity factor (0.0-1.0) for effects
    float GetIntensity() const { return percentile / 100.0f; }

    // Check if player qualifies for a bonus
    bool QualifiesForBonus(const std::string& minLeague, uint32 minStreak = 0) const;
};

// ============================================================================
// BRIDGE EVENT - Outgoing message structure
// ============================================================================

struct BridgeEvent
{
    EventNamespace ns;
    std::string type;           // Event type as string
    uint64_t timestamp;         // Unix timestamp milliseconds
    std::string jsonPayload;    // Additional JSON data

    BridgeEvent()
        : ns(EventNamespace::SYSTEM)
        , type("unknown")
        , timestamp(0)
    {}

    BridgeEvent(EventNamespace _ns, const std::string& _type, const std::string& _payload = "{}")
        : ns(_ns)
        , type(_type)
        , jsonPayload(_payload)
    {
        timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()
        ).count();
    }

    // Build complete JSON message
    std::string ToJson() const;
};

// ============================================================================
// BRIDGE COMMAND - Incoming message structure
// ============================================================================

struct BridgeCommand
{
    EventNamespace ns;
    std::string type;           // Command type
    std::string requestId;      // Request ID for response matching (optional)
    std::string jsonPayload;    // Full JSON payload for module to parse

    BridgeCommand()
        : ns(EventNamespace::SYSTEM)
        , type("unknown")
        , requestId("")
    {}

    // Check if this command expects a response
    bool ExpectsResponse() const { return !requestId.empty(); }
};

// ============================================================================
// COMMAND HANDLER CALLBACK
// ============================================================================

using CommandHandler = std::function<void(const BridgeCommand&)>;

// ============================================================================
// BRIDGE STATISTICS
// ============================================================================

struct BridgeStats
{
    uint64_t eventsSent;
    uint64_t eventsDropped;     // Due to queue overflow
    uint64_t commandsReceived;
    uint64_t commandsProcessed;
    uint64_t bytesOut;
    uint64_t bytesIn;
    uint64_t reconnectCount;
    uint64_t uptimeSeconds;
    uint32_t queueSize;
    uint32_t maxQueueSize;
    bool connected;
    bool authenticated;
};

// ============================================================================
// JSON UTILITY FUNCTIONS
// ============================================================================

// Escape a string for safe JSON inclusion
std::string EscapeJsonString(const std::string& input);

// Safe extraction from JSON (returns empty/0 if not found)
std::string ExtractJsonString(const std::string& json, const std::string& key);
int64_t ExtractJsonInt(const std::string& json, const std::string& key);
double ExtractJsonDouble(const std::string& json, const std::string& key);
bool ExtractJsonBool(const std::string& json, const std::string& key);

} // namespace ECRealtimeBridge

#endif // EC_REALTIME_BRIDGE_TYPES_H
