/*
 * mod-alive-world - Alive World System for AzerothCore
 * Copyright (C) 2025 EC
 *
 * Common types, enums, and data structures used across the module.
 */

#ifndef ALIVE_WORLD_TYPES_H
#define ALIVE_WORLD_TYPES_H

#include "Common.h"
#include "ObjectGuid.h"

#include <chrono>
#include <deque>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace AliveWorld
{
    // =========================================================================
    // LOGGING
    // =========================================================================

    // Named AWLogLevel to avoid collision with AzerothCore's global LogLevel enum
    enum class AWLogLevel : uint8
    {
        AW_ERROR   = 0,
        AW_WARNING = 1,
        AW_INFO    = 2,
        AW_DEBUG   = 3,
        AW_TRACE   = 4
    };

    // =========================================================================
    // UTILITY FUNCTION (forward declaration - needed before structs)
    // =========================================================================

    inline uint64 NowMs()
    {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()
        ).count();
    }

    // =========================================================================
    // INTELLIGENCE TIERS
    // =========================================================================

    enum class BotTier : uint8
    {
        TIER_1_ACTIVE  = 1,  // Full AI - near players, in combat, recently spoken to
        TIER_2_AWARE   = 2,  // Aware - same zone as players, queued AI
        TIER_3_PASSIVE = 3   // Passive - no players nearby, scripted only
    };

    // =========================================================================
    // WORLD EVENTS
    // =========================================================================

    enum class WorldEventType : uint8
    {
        // Combat Events
        PLAYER_KILLED_MOB       = 1,
        PLAYER_KILLED_PLAYER    = 2,
        BOT_KILLED_MOB          = 3,
        BOT_KILLED_PLAYER       = 4,
        BOSS_ENGAGED            = 5,
        BOSS_KILLED             = 6,
        WIPE                    = 7,
        PLAYER_DIED             = 8,
        BOT_DIED                = 9,
        COMBAT_KILL             = 10,   // Generic combat kill (creature)
        COMBAT_PVP_KILL         = 11,   // Generic PvP kill

        // Social Events
        PLAYER_CHAT             = 20,
        BOT_CHAT                = 21,
        PLAYER_EMOTE            = 22,
        TRADE_COMPLETED         = 23,
        GUILD_INVITE            = 24,
        GROUP_FORMED            = 25,
        GROUP_DISBANDED         = 26,
        DUEL_STARTED            = 27,
        DUEL_ENDED              = 28,
        DUEL_REQUEST            = 29,   // Duel requested
        DUEL_START              = 30,   // Alias for DUEL_STARTED
        DUEL_END                = 31,   // Alias for DUEL_ENDED
        CHAT_MESSAGE            = 32,   // Generic chat message

        // World Events
        RARE_SPAWN              = 40,
        WORLD_BOSS_SPAWN        = 41,
        FESTIVAL_START          = 42,
        FESTIVAL_END            = 43,
        GUARD_CALLED            = 44,   // Integration with guard system
        WORLD_EVENT_START       = 45,   // Integration with mod-ec-world-events
        WORLD_EVENT_END         = 46,

        // Zone Events
        PLAYER_ENTERED_ZONE     = 60,
        PLAYER_LEFT_ZONE        = 61,
        BOT_ENTERED_ZONE        = 62,
        BOT_LEFT_ZONE           = 63,
        ZONE_BECAME_ACTIVE      = 64,   // First player entered
        ZONE_BECAME_EMPTY       = 65,   // Last player left
        ZONE_ENTER              = 66,   // Generic zone enter

        // Progress Events
        LOOT_ITEM               = 70,   // Player looted an item
        QUEST_COMPLETE          = 71,   // Quest completed
        LEVEL_UP                = 72,   // Level gained
        ACHIEVEMENT             = 73,   // Achievement earned

        // Economic Events
        AUCTION_CREATED         = 80,
        AUCTION_SOLD            = 81,
        MAIL_SENT               = 82,

        // Extended World Events
        FACTION_CONFLICT        = 100,  // PvP tension in an area
        MERCHANT_EVENT          = 101,  // Trading opportunity
        WEATHER_CHANGE          = 102,  // Weather system change
        HOLIDAY_EVENT           = 103,  // In-game holiday
        BOT_ACHIEVEMENT         = 104,  // Bot achieved something notable
        GUILD_EVENT             = 105,  // Guild-related event

        // Generic Gossip (from broadcast_text extraction)
        GOSSIP                  = 106,  // Generic chatter/hearsay patterns

        // Guild Events (specific)
        GUILD_JOIN              = 110,  // Player/bot joined guild
        GUILD_LEAVE             = 111   // Player/bot left guild
    };

    struct WorldEvent
    {
        uint32 eventId;                 // Unique identifier
        WorldEventType type;
        uint64 timestamp;               // Unix timestamp in milliseconds
        uint32 zoneId;
        uint32 areaId;
        uint32 mapId;
        ObjectGuid primaryActor;        // Who initiated (player/bot/creature)
        ObjectGuid secondaryActor;      // Target (if applicable)
        std::string primaryName;        // Name cache for primary actor
        std::string secondaryName;      // Name cache for secondary
        std::string details;            // Human-readable description
        uint8 importance;               // 1-10 scale (used for gossip creation)
        bool processed;                 // Has this event been processed by bots?

        // Alternative field names (for compatibility with hook code)
        uint64 actorGuid;               // Raw GUID of actor (same as primaryActor raw value)
        std::string actorName;          // Name of actor (alias for primaryName)
        uint64 targetGuid;              // Raw GUID of target (same as secondaryActor raw value)
        std::string targetName;         // Name of target (alias for secondaryName)
        std::map<std::string, std::string> data;  // Additional event-specific data

        WorldEvent()
            : eventId(0), type(WorldEventType::PLAYER_CHAT), timestamp(0)
            , zoneId(0), areaId(0), mapId(0), importance(1), processed(false)
            , actorGuid(0), targetGuid(0)
        {}
    };

    // =========================================================================
    // WEATHER & TIME SYSTEM
    // =========================================================================

    /**
     * Time transition types for bot reactions.
     * These represent significant moments in the day/night cycle.
     */
    enum class TimeTransitionType : uint8
    {
        NONE        = 0,
        DAWN        = 1,    // 05:00-07:00 - Sun rising
        MORNING     = 2,    // 07:00-12:00 - Morning activities
        NOON        = 3,    // 12:00 - Midday
        AFTERNOON   = 4,    // 12:00-18:00 - Afternoon
        DUSK        = 5,    // 18:00-20:00 - Sun setting
        EVENING     = 6,    // 20:00-22:00 - Early night
        NIGHT       = 7,    // 22:00-05:00 - Deep night
        MIDNIGHT    = 8     // 00:00 - Midnight moment
    };

    /**
     * Seasons for weather probability and gossip context.
     */
    enum class Season : uint8
    {
        SPRING  = 0,    // Day 78-168
        SUMMER  = 1,    // Day 168-259
        FALL    = 2,    // Day 259-350 (Autumn)
        WINTER  = 3     // Day 350-78
    };

    /**
     * Weather intensity levels for gossip generation.
     */
    enum class WeatherIntensity : uint8
    {
        NONE    = 0,    // Clear/Fine weather
        LIGHT   = 1,    // Grade 0.0-0.333
        MEDIUM  = 2,    // Grade 0.333-0.667
        HEAVY   = 3     // Grade 0.667-1.0
    };

    /**
     * Weather type categories (matches AzerothCore WeatherType).
     */
    enum class WeatherCategory : uint8
    {
        FINE        = 0,    // Clear skies
        RAIN        = 1,    // Rain
        SNOW        = 2,    // Snow
        STORM       = 3,    // Sandstorm
        THUNDERS    = 86,   // Thunder
        BLACKRAIN   = 90    // Dark/corrupted rain
    };

    /**
     * Current weather state for a zone.
     */
    struct ZoneWeatherState
    {
        uint32 zoneId;
        WeatherCategory category;
        WeatherIntensity intensity;
        float grade;                    // Raw grade 0.0-1.0
        uint64 lastChangeTime;
        uint64 stateStartTime;          // When this weather state began

        ZoneWeatherState()
            : zoneId(0), category(WeatherCategory::FINE), intensity(WeatherIntensity::NONE)
            , grade(0.0f), lastChangeTime(0), stateStartTime(0)
        {}

        bool IsBadWeather() const { return category != WeatherCategory::FINE && intensity >= WeatherIntensity::MEDIUM; }
        bool IsStormy() const { return category == WeatherCategory::THUNDERS || intensity == WeatherIntensity::HEAVY; }
        bool IsCold() const { return category == WeatherCategory::SNOW; }
    };

    /**
     * Current time state for the world.
     */
    struct TimeState
    {
        uint8 currentHour;              // 0-23
        uint8 currentMinute;            // 0-59
        Season currentSeason;
        TimeTransitionType currentPeriod;
        uint8 dayOfWeek;                // 0-6 (Sunday=0)
        uint16 dayOfYear;               // 0-365
        uint64 lastTransitionTime;      // When period last changed

        TimeState()
            : currentHour(12), currentMinute(0), currentSeason(Season::SUMMER)
            , currentPeriod(TimeTransitionType::AFTERNOON), dayOfWeek(0)
            , dayOfYear(180), lastTransitionTime(0)
        {}

        bool IsNight() const { return currentHour >= 22 || currentHour < 5; }
        bool IsDawn() const { return currentHour >= 5 && currentHour < 7; }
        bool IsDusk() const { return currentHour >= 18 && currentHour < 20; }
        bool IsDaytime() const { return currentHour >= 7 && currentHour < 18; }
    };

    // Helper functions for weather/time
    inline const char* TimeTransitionToString(TimeTransitionType type)
    {
        switch (type)
        {
            case TimeTransitionType::DAWN:      return "Dawn";
            case TimeTransitionType::MORNING:   return "Morning";
            case TimeTransitionType::NOON:      return "Noon";
            case TimeTransitionType::AFTERNOON: return "Afternoon";
            case TimeTransitionType::DUSK:      return "Dusk";
            case TimeTransitionType::EVENING:   return "Evening";
            case TimeTransitionType::NIGHT:     return "Night";
            case TimeTransitionType::MIDNIGHT:  return "Midnight";
            default: return "Unknown";
        }
    }

    inline const char* SeasonToString(Season season)
    {
        switch (season)
        {
            case Season::SPRING: return "Spring";
            case Season::SUMMER: return "Summer";
            case Season::FALL:   return "Fall";
            case Season::WINTER: return "Winter";
            default: return "Unknown";
        }
    }

    inline const char* WeatherCategoryToString(WeatherCategory cat)
    {
        switch (cat)
        {
            case WeatherCategory::FINE:      return "Clear";
            case WeatherCategory::RAIN:      return "Rain";
            case WeatherCategory::SNOW:      return "Snow";
            case WeatherCategory::STORM:     return "Sandstorm";
            case WeatherCategory::THUNDERS:  return "Thunder";
            case WeatherCategory::BLACKRAIN: return "Dark Rain";
            default: return "Unknown";
        }
    }

    inline const char* WeatherIntensityToString(WeatherIntensity intensity)
    {
        switch (intensity)
        {
            case WeatherIntensity::NONE:   return "None";
            case WeatherIntensity::LIGHT:  return "Light";
            case WeatherIntensity::MEDIUM: return "Moderate";
            case WeatherIntensity::HEAVY:  return "Heavy";
            default: return "Unknown";
        }
    }

    inline WeatherIntensity GradeToIntensity(float grade)
    {
        if (grade < 0.01f) return WeatherIntensity::NONE;
        if (grade < 0.333f) return WeatherIntensity::LIGHT;
        if (grade < 0.667f) return WeatherIntensity::MEDIUM;
        return WeatherIntensity::HEAVY;
    }

    // =========================================================================
    // ZONE STATE
    // =========================================================================

    struct ZoneState
    {
        uint32 zoneId;
        uint32 mapId;
        uint32 alliancePlayerCount;
        uint32 hordePlayerCount;
        uint32 allianceBotCount;
        uint32 hordeBotCount;
        uint8 activityLevel;            // 0-10
        uint8 dangerLevel;              // 0-10 (PvP activity)
        uint64 lastUpdateTime;
        std::vector<uint32> recentEventIds;  // IDs of recent events in this zone

        ZoneState()
            : zoneId(0), mapId(0), alliancePlayerCount(0), hordePlayerCount(0)
            , allianceBotCount(0), hordeBotCount(0), activityLevel(0)
            , dangerLevel(0), lastUpdateTime(0)
        {}

        uint32 GetTotalPlayers() const { return alliancePlayerCount + hordePlayerCount; }
        uint32 GetTotalBots() const { return allianceBotCount + hordeBotCount; }
        uint32 GetTotalPopulation() const { return GetTotalPlayers() + GetTotalBots(); }
        bool IsHotZone(uint8 threshold) const { return activityLevel >= threshold; }
    };

    // =========================================================================
    // GOSSIP
    // =========================================================================

    struct GossipItem
    {
        uint32 gossipId;
        uint8 type;                     // GossipType enum value (CRIME, BOSS_KILL, etc.)
        std::string headline;           // Short summary: "Onyxia was slain!"
        std::string details;            // Full story: "The guild <Legends>..."
        uint64 timestamp;               // When it was created
        uint64 expiresAt;               // When it should be removed
        uint32 originZoneId;            // Where it happened
        uint8 importance;               // 1-10 scale
        uint8 spreadRadius;             // How many "hops" it has spread
        uint32 sourceEventId;           // Original event that created this gossip
        std::set<uint64> knownByBots;   // GUIDs of bots who know this gossip

        GossipItem()
            : gossipId(0), type(0), timestamp(0), expiresAt(0), originZoneId(0)
            , importance(1), spreadRadius(0), sourceEventId(0)
        {}
    };

    // =========================================================================
    // BOT MEMORY
    // =========================================================================

    struct CombatMemory
    {
        uint64 timestamp;
        bool wasVictorious;
        std::string opponentType;       // "player", "elite", "boss", "mob"
        std::string opponentName;
        ObjectGuid opponentGuid;
        uint32 opponentLevel;           // Level of the opponent
        uint64 damageDealt;
        uint64 damageTaken;
        uint64 healingDone;
        uint32 zoneId;

        CombatMemory()
            : timestamp(0), wasVictorious(false), opponentLevel(0), damageDealt(0)
            , damageTaken(0), healingDone(0), zoneId(0)
        {}
    };

    struct SocialMemory
    {
        ObjectGuid playerGuid;
        std::string playerName;
        float sentimentScore;           // -1.0 (enemy) to 1.0 (friend)
        uint32 interactionCount;
        uint64 lastInteractionTime;
        std::string lastInteractionType;  // "chat", "trade", "combat", "group"
        std::string lastTopic;            // Last conversation topic

        SocialMemory()
            : sentimentScore(0.0f), interactionCount(0), lastInteractionTime(0)
        {}
    };

    /**
     * Event participation memory - tracks bot involvement in world events.
     * Used for gossip generation ("I fought in the Goldshire raid!") and
     * personality development (bots who survive events become more confident).
     */
    struct EventParticipation
    {
        uint32 eventId;                 // Event definition ID
        uint32 instanceId;              // Unique instance of this event
        std::string eventName;          // Human-readable name
        uint8 eventType;                // 0=Raid, 1=Defense, 2=WorldBoss, 3=Gathering, 4=PvPZone
        uint64 joinTime;                // When bot joined
        uint64 leaveTime;               // When bot left (or event ended)
        uint32 kills;                   // Kills during event
        uint32 deaths;                  // Deaths during event
        uint64 damageDealt;             // Total damage dealt
        uint64 healingDone;             // Total healing done
        bool wasSuccessful;             // Did the event complete successfully?
        bool wasVoluntary;              // Did bot join willingly (vs auto-recruited)?
        std::string role;               // "attacker", "defender", "healer", "support"
        uint32 zoneId;                  // Zone where event took place

        EventParticipation()
            : eventId(0), instanceId(0), eventType(0), joinTime(0), leaveTime(0)
            , kills(0), deaths(0), damageDealt(0), healingDone(0)
            , wasSuccessful(false), wasVoluntary(false), zoneId(0)
        {}

        uint64 GetDurationMs() const { return leaveTime > joinTime ? leaveTime - joinTime : 0; }
        float GetParticipationScore() const
        {
            // Score based on activity during event
            float score = 0.0f;
            score += kills * 10.0f;
            score += static_cast<float>(damageDealt) / 1000.0f;
            score += static_cast<float>(healingDone) / 500.0f;
            score -= deaths * 5.0f;
            return std::max(0.0f, score);
        }
    };

    inline const char* EventParticipationTypeToString(uint8 eventType)
    {
        switch (eventType)
        {
            case 0: return "Raid";
            case 1: return "Defense";
            case 2: return "World Boss";
            case 3: return "Gathering";
            case 4: return "PvP Zone";
            default: return "Unknown";
        }
    }

    struct BotPersonality
    {
        // Core personality traits (0.0 - 1.0 scale)
        float aggression;               // 0-1: tendency to engage in combat/PvP
        float caution;                  // 0-1: risk aversion, careful decision making
        float sociability;              // 0-1: frequency of social interaction
        float loyalty;                  // 0-1: dedication to group/allies
        float curiosity;                // 0-1: exploration tendency
        float vengefulness;             // 0-1: tendency to seek revenge
        float helpfulness;              // 0-1: willingness to help others
        float greed;                    // 0-1: focus on gold/loot
        std::string currentMood;        // "neutral", "excited", "angry", "bored", etc.
        uint64 moodTimestamp;           // When mood last changed

        BotPersonality()
            : aggression(0.5f), caution(0.5f), sociability(0.5f), loyalty(0.5f)
            , curiosity(0.5f), vengefulness(0.3f), helpfulness(0.5f), greed(0.5f)
            , currentMood("neutral"), moodTimestamp(0)
        {}

        // Initialize with random values for variety
        void Randomize();
    };

    /**
     * Per-bot personality override profile.
     *
     * These overrides let specific bots keep fixed traits (no drift from
     * personality evolution) and optionally run a relentless hunt profile
     * against a specific target.
     */
    struct BotPersonalityOverride
    {
        uint64 botGuid = 0;
        bool enabled = false;

        // Locked trait overrides (values are 0.0 - 1.0)
        bool lockAggression = false;
        float aggression = 0.5f;

        bool lockCaution = false;
        float caution = 0.5f;

        bool lockSociability = false;
        float sociability = 0.5f;

        bool lockLoyalty = false;
        float loyalty = 0.5f;

        bool lockCuriosity = false;
        float curiosity = 0.5f;

        bool lockVengefulness = false;
        float vengefulness = 0.3f;

        bool lockHelpfulness = false;
        float helpfulness = 0.5f;

        bool lockGreed = false;
        float greed = 0.5f;

        bool lockMood = false;
        std::string fixedMood = "neutral";

        // Optional relentless hunter behavior
        bool relentlessHunter = false;
        uint64 relentlessTargetGuid = 0;
        float hunterPursuitRadius = 120.0f;      // Track target within this distance
        float hunterEngageRadius = 35.0f;        // Start attack within this distance
        uint32 hunterReengageCooldownMs = 2000;  // Minimum delay between hunt actions

        bool HasAnyLockedTrait() const
        {
            return lockAggression || lockCaution || lockSociability || lockLoyalty ||
                   lockCuriosity || lockVengefulness || lockHelpfulness || lockGreed || lockMood;
        }

        bool IsRelentlessHunter() const
        {
            return enabled && relentlessHunter && botGuid != 0 && relentlessTargetGuid != 0;
        }
    };

    struct BotMemoryData
    {
        uint64 botGuid;
        std::string botName;
        BotTier currentTier;
        BotPersonality personality;

        // Combat history
        std::deque<CombatMemory> combatLog;      // Max 5

        // Social connections
        std::map<uint64, SocialMemory> socialConnections;  // playerGuid -> memory

        // Location history
        std::map<uint32, uint32> zoneTimeSpent;  // zoneId -> minutes
        uint32 favoriteZoneId;
        uint32 currentZoneId;

        // Known gossip
        std::vector<uint32> knownGossipIds;

        // World event participation history (mod-ec-world-events integration)
        std::deque<EventParticipation> eventHistory;  // Max 10 events
        uint32 currentEventInstanceId;                // Currently participating in this event (0 if none)

        // AI state
        uint64 lastAIRequestTime;
        uint64 lastTierUpdateTime;
        uint64 lastAIDecisionTime;
        std::string lastAIDecision;
        bool isProcessingAI;

        // Combat statistics
        uint32 totalKills;              // Total creatures/players killed
        uint32 totalDeaths;             // Total times this bot died
        uint32 pvpKills;                // PvP-specific kills
        uint32 pvpDeaths;               // PvP-specific deaths

        BotMemoryData()
            : botGuid(0), currentTier(BotTier::TIER_3_PASSIVE), favoriteZoneId(0)
            , currentZoneId(0), currentEventInstanceId(0), lastAIRequestTime(0)
            , lastTierUpdateTime(0), lastAIDecisionTime(0), isProcessingAI(false)
            , totalKills(0), totalDeaths(0), pvpKills(0), pvpDeaths(0)
        {}
    };

    // =========================================================================
    // GOAL SYSTEM
    // =========================================================================

    enum class GoalType : uint8
    {
        ITEM        = 1,    // Acquire specific item (gear upgrade, quest item, crafting mat)
        REPUTATION  = 2,    // Gain reputation with a faction
        ACHIEVEMENT = 3,    // Complete an achievement
        EXPLORE     = 4,    // Explore/visit a specific zone or area
        PROFESSION  = 5,    // Learn a recipe or level up a profession
        GOLD        = 6,    // Accumulate gold
        LEVEL       = 7,    // Gain experience/level up
        PVP         = 8,    // PvP-related goals (honor, kills)
        SOCIAL      = 9     // Social goals (make friends, join guild)
    };

    enum class GoalPriority : uint8
    {
        CRITICAL = 1,   // Must do immediately (survival, urgent needs)
        HIGH     = 2,   // Very important (big upgrade, major milestone)
        MEDIUM   = 3,   // Normal priority (gradual improvement)
        LOW      = 4,   // Nice to have (optional, long-term)
        PASSIVE  = 5    // Background goal (always active but low effort)
    };

    enum class GoalStatus : uint8
    {
        ACTIVE      = 1,    // Currently pursuing
        PAUSED      = 2,    // Temporarily paused
        COMPLETED   = 3,    // Successfully completed
        ABANDONED   = 4,    // Given up on
        BLOCKED     = 5     // Cannot progress (missing prerequisite)
    };

    struct BotGoal
    {
        uint32 goalId;              // Unique identifier
        uint64 botGuid;             // Bot pursuing this goal
        GoalType type;
        GoalPriority priority;
        GoalStatus status;
        uint32 targetId;            // ItemID, FactionID, AchievementID, ZoneID, etc.
        std::string targetName;     // Human-readable target name
        float progress;             // 0.0 - 1.0 completion
        uint32 progressCurrent;     // Current value (e.g., rep gained, items collected)
        uint32 progressTarget;      // Target value needed for completion
        uint64 createdAt;           // When goal was created
        uint64 updatedAt;           // Last progress update
        uint64 completedAt;         // When completed (0 if not)
        std::string reason;         // Why bot chose this goal (personality-driven)
        std::string notes;          // Additional context

        BotGoal()
            : goalId(0), botGuid(0), type(GoalType::ITEM), priority(GoalPriority::MEDIUM)
            , status(GoalStatus::ACTIVE), targetId(0), progress(0.0f)
            , progressCurrent(0), progressTarget(1), createdAt(0), updatedAt(0)
            , completedAt(0)
        {}

        bool IsComplete() const { return status == GoalStatus::COMPLETED || progress >= 1.0f; }
        bool IsActive() const { return status == GoalStatus::ACTIVE; }
        void UpdateProgress(uint32 current, uint32 target)
        {
            progressCurrent = current;
            progressTarget = target;
            progress = target > 0 ? static_cast<float>(current) / static_cast<float>(target) : 0.0f;
            updatedAt = NowMs();
            if (progress >= 1.0f)
            {
                status = GoalStatus::COMPLETED;
                completedAt = NowMs();
            }
        }
    };

    inline const char* GoalTypeToString(GoalType type)
    {
        switch (type)
        {
            case GoalType::ITEM:        return "Item";
            case GoalType::REPUTATION:  return "Reputation";
            case GoalType::ACHIEVEMENT: return "Achievement";
            case GoalType::EXPLORE:     return "Explore";
            case GoalType::PROFESSION:  return "Profession";
            case GoalType::GOLD:        return "Gold";
            case GoalType::LEVEL:       return "Level";
            case GoalType::PVP:         return "PvP";
            case GoalType::SOCIAL:      return "Social";
            default: return "Unknown";
        }
    }

    inline const char* GoalPriorityToString(GoalPriority priority)
    {
        switch (priority)
        {
            case GoalPriority::CRITICAL: return "Critical";
            case GoalPriority::HIGH:     return "High";
            case GoalPriority::MEDIUM:   return "Medium";
            case GoalPriority::LOW:      return "Low";
            case GoalPriority::PASSIVE:  return "Passive";
            default: return "Unknown";
        }
    }

    inline const char* GoalStatusToString(GoalStatus status)
    {
        switch (status)
        {
            case GoalStatus::ACTIVE:    return "Active";
            case GoalStatus::PAUSED:    return "Paused";
            case GoalStatus::COMPLETED: return "Completed";
            case GoalStatus::ABANDONED: return "Abandoned";
            case GoalStatus::BLOCKED:   return "Blocked";
            default: return "Unknown";
        }
    }

    // =========================================================================
    // ECONOMY SYSTEM
    // =========================================================================

    // Item value tier classification
    enum class ItemValueTier : uint8
    {
        TRASH       = 0,    // < 1g sell price
        COMMON      = 1,    // 1-10g
        UNCOMMON    = 2,    // 10-50g
        VALUABLE    = 3,    // 50-200g
        PRECIOUS    = 4,    // 200-1000g
        LEGENDARY   = 5     // > 1000g
    };

    // Item category for farming preferences
    enum class ItemCategory : uint8
    {
        UNKNOWN         = 0,
        CLOTH           = 1,    // Cloth drops
        LEATHER         = 2,    // Skinning materials
        ORE             = 3,    // Mining materials
        HERB            = 4,    // Herbalism materials
        ELEMENTAL       = 5,    // Elemental crafting mats (Primal Fire, etc.)
        ENCHANTING      = 6,    // Enchanting materials
        GEM             = 7,    // Gems (raw and cut)
        RECIPE          = 8,    // Recipes and patterns
        EQUIPMENT       = 9,    // Weapons and armor
        CONSUMABLE      = 10,   // Potions, food, etc.
        TRADE_GOODS     = 11,   // General trade goods
        QUEST_ITEM      = 12    // Quest-related items
    };

    // Valuable item info (cached from database)
    struct ValuableItem
    {
        uint32 itemId;
        std::string name;
        ItemValueTier valueTier;
        ItemCategory category;
        uint32 sellPrice;           // Vendor sell price in copper
        uint32 buyPrice;            // Vendor buy price in copper
        uint32 estimatedMarketPrice; // Simulated AH price
        uint8 quality;              // 0=gray, 1=white, 2=green, etc.
        uint32 requiredLevel;
        bool isBOP;                 // Bind on Pickup
        bool isCraftingMat;         // Used in professions
        float demandMultiplier;     // Simulated demand (1.0 = normal)

        ValuableItem()
            : itemId(0), valueTier(ItemValueTier::TRASH), category(ItemCategory::UNKNOWN)
            , sellPrice(0), buyPrice(0), estimatedMarketPrice(0), quality(0)
            , requiredLevel(0), isBOP(false), isCraftingMat(false), demandMultiplier(1.0f)
        {}
    };

    // Farming location (where to get valuable items)
    struct FarmingLocation
    {
        uint32 zoneId;
        uint32 areaId;
        std::string zoneName;
        uint32 creatureEntry;       // Primary creature to farm
        std::string creatureName;
        uint8 creatureLevel;
        std::vector<uint32> valuableDrops;  // Item IDs that drop here
        float dropRateAverage;      // Average drop rate for valuables
        float goldPerHour;          // Estimated gold/hour
        uint8 dangerLevel;          // 1-10 based on elites, PvP, etc.
        bool requiresGroup;         // Need party to farm
        uint32 recommendedLevel;    // Minimum level to farm efficiently

        FarmingLocation()
            : zoneId(0), areaId(0), creatureEntry(0), creatureLevel(0)
            , dropRateAverage(0.0f), goldPerHour(0.0f), dangerLevel(1)
            , requiresGroup(false), recommendedLevel(0)
        {}
    };

    // Vendor info (where to buy/sell)
    struct VendorInfo
    {
        uint32 creatureEntry;
        std::string vendorName;
        uint32 zoneId;
        uint32 areaId;
        float posX, posY, posZ;
        uint32 mapId;
        std::vector<uint32> sellsItems;     // Items this vendor sells
        bool sellsReagents;
        bool sellsFoodDrink;
        bool sellsRepairServices;
        uint32 requiredReputation;          // Faction ID if rep required
        uint32 requiredReputationRank;      // Standing required (Friendly, etc.)

        VendorInfo()
            : creatureEntry(0), zoneId(0), areaId(0)
            , posX(0), posY(0), posZ(0), mapId(0)
            , sellsReagents(false), sellsFoodDrink(false), sellsRepairServices(false)
            , requiredReputation(0), requiredReputationRank(0)
        {}
    };

    // Economic gossip (price rumors, market news)
    struct EconomicGossip
    {
        uint32 gossipId;
        uint32 itemId;              // 0 if general gossip
        std::string itemName;
        std::string message;        // "Eternal Fire prices are crazy right now!"
        float priceChange;          // % change (positive = rising)
        uint64 timestamp;
        uint64 expiresAt;
        uint8 reliability;          // 1-10 how accurate

        EconomicGossip()
            : gossipId(0), itemId(0), priceChange(0.0f)
            , timestamp(0), expiresAt(0), reliability(5)
        {}
    };

    // Bot's economic state
    struct BotEconomicState
    {
        uint64 botGuid;
        uint32 currentGold;         // In copper
        uint32 targetGold;          // Savings goal
        ItemCategory preferredFarmCategory;  // Based on professions
        std::vector<uint32> watchedItems;    // Items bot is tracking prices for
        std::vector<uint32> knownVendorEntries;  // Vendors bot knows about
        uint32 lastFarmZoneId;
        uint64 lastSellTime;
        uint64 lastBuyTime;
        float greedFactor;          // From personality, affects gold goals

        BotEconomicState()
            : botGuid(0), currentGold(0), targetGold(0)
            , preferredFarmCategory(ItemCategory::UNKNOWN)
            , lastFarmZoneId(0), lastSellTime(0), lastBuyTime(0), greedFactor(1.0f)
        {}
    };

    inline const char* ItemValueTierToString(ItemValueTier tier)
    {
        switch (tier)
        {
            case ItemValueTier::TRASH:      return "Trash";
            case ItemValueTier::COMMON:     return "Common";
            case ItemValueTier::UNCOMMON:   return "Uncommon";
            case ItemValueTier::VALUABLE:   return "Valuable";
            case ItemValueTier::PRECIOUS:   return "Precious";
            case ItemValueTier::LEGENDARY:  return "Legendary";
            default: return "Unknown";
        }
    }

    inline const char* ItemCategoryToString(ItemCategory cat)
    {
        switch (cat)
        {
            case ItemCategory::CLOTH:       return "Cloth";
            case ItemCategory::LEATHER:     return "Leather";
            case ItemCategory::ORE:         return "Ore";
            case ItemCategory::HERB:        return "Herb";
            case ItemCategory::ELEMENTAL:   return "Elemental";
            case ItemCategory::ENCHANTING:  return "Enchanting";
            case ItemCategory::GEM:         return "Gem";
            case ItemCategory::RECIPE:      return "Recipe";
            case ItemCategory::EQUIPMENT:   return "Equipment";
            case ItemCategory::CONSUMABLE:  return "Consumable";
            case ItemCategory::TRADE_GOODS: return "Trade Goods";
            case ItemCategory::QUEST_ITEM:  return "Quest Item";
            default: return "Unknown";
        }
    }

    // =========================================================================
    // SPEECH PATTERN SYSTEM
    // =========================================================================

    // Context for NPC/bot speech (extracted from creature_text comments)
    enum class SpeechContext : uint8
    {
        AGGRO       = 1,    // "on Aggro" - entering combat
        DEATH       = 2,    // "on Death" - dying
        KILL        = 3,    // "on Kill" - killing an enemy
        IDLE        = 4,    // Random idle chatter
        GREETING    = 5,    // Greeting players/NPCs
        THREAT      = 6,    // Threatening enemies
        CHALLENGE   = 7,    // Combat challenge/taunt
        FEAR        = 8,    // Expressing fear
        BOAST       = 9,    // Boasting about prowess
        FACTION     = 10,   // Faction pride/loyalty
        COMBAT      = 11,   // General combat speech
        EVENT       = 12,   // Scripted event speech
        EMOTE       = 13    // Emote text (%s format)
    };

    // Chat type values from creature_text.Type
    enum class CreatureTextType : uint8
    {
        SAY                 = 12,   // CHAT_MSG_MONSTER_SAY
        YELL                = 14,   // CHAT_MSG_MONSTER_YELL
        EMOTE               = 16,   // CHAT_MSG_MONSTER_EMOTE
        BOSS_EMOTE          = 41,   // CHAT_MSG_RAID_BOSS_EMOTE
        WHISPER             = 15,   // CHAT_MSG_MONSTER_WHISPER
        BOSS_WHISPER        = 42    // CHAT_MSG_RAID_BOSS_WHISPER
    };

    // Creature type classification for speech patterns
    enum class CreatureCategory : uint8
    {
        GENERIC     = 0,
        HUMANOID    = 1,    // Humans, orcs, etc.
        BEAST       = 2,    // Animals with growls
        UNDEAD      = 3,    // Scourge, ghosts
        DEMON       = 4,    // Burning Legion
        ELEMENTAL   = 5,    // Fire, water, etc.
        DRAGON      = 6,    // Dragons and dragonkin
        GIANT       = 7,    // Giants, ogres
        MECHANICAL  = 8,    // Robots, constructs
        ABERRATION  = 9     // Old God minions, etc.
    };

    struct SpeechPattern
    {
        uint32 patternId;           // Unique identifier
        std::string text;           // The speech text (with placeholders)
        SpeechContext context;      // When to use this speech
        CreatureTextType textType;  // SAY, YELL, EMOTE, etc.
        CreatureCategory category;  // What type of creature uses this
        uint32 emoteId;             // Associated emote (0 if none)
        uint8 languageId;           // In-game language
        uint32 creatureEntry;       // Original creature (0 = generic)
        std::string creatureName;   // For attribution
        std::string comment;        // Original comment from DB
        float probability;          // Usage probability

        SpeechPattern()
            : patternId(0), context(SpeechContext::IDLE)
            , textType(CreatureTextType::SAY), category(CreatureCategory::GENERIC)
            , emoteId(0), languageId(0), creatureEntry(0), probability(100.0f)
        {}
    };

    inline const char* SpeechContextToString(SpeechContext ctx)
    {
        switch (ctx)
        {
            case SpeechContext::AGGRO:      return "Aggro";
            case SpeechContext::DEATH:      return "Death";
            case SpeechContext::KILL:       return "Kill";
            case SpeechContext::IDLE:       return "Idle";
            case SpeechContext::GREETING:   return "Greeting";
            case SpeechContext::THREAT:     return "Threat";
            case SpeechContext::CHALLENGE:  return "Challenge";
            case SpeechContext::FEAR:       return "Fear";
            case SpeechContext::BOAST:      return "Boast";
            case SpeechContext::FACTION:    return "Faction";
            case SpeechContext::COMBAT:     return "Combat";
            case SpeechContext::EVENT:      return "Event";
            case SpeechContext::EMOTE:      return "Emote";
            default: return "Unknown";
        }
    }

    inline const char* CreatureCategoryToString(CreatureCategory cat)
    {
        switch (cat)
        {
            case CreatureCategory::GENERIC:     return "Generic";
            case CreatureCategory::HUMANOID:    return "Humanoid";
            case CreatureCategory::BEAST:       return "Beast";
            case CreatureCategory::UNDEAD:      return "Undead";
            case CreatureCategory::DEMON:       return "Demon";
            case CreatureCategory::ELEMENTAL:   return "Elemental";
            case CreatureCategory::DRAGON:      return "Dragon";
            case CreatureCategory::GIANT:       return "Giant";
            case CreatureCategory::MECHANICAL:  return "Mechanical";
            case CreatureCategory::ABERRATION:  return "Aberration";
            default: return "Unknown";
        }
    }

    // =========================================================================
    // AI DECISION ENGINE
    // =========================================================================

    enum class AIRequestPriority : uint8
    {
        URGENT = 1,     // Tier 1 bots, immediate needs
        HIGH   = 2,     // Tier 1 bots, general decisions
        MEDIUM = 3,     // Tier 2 bots
        LOW    = 4      // Background tasks, gossip generation
    };

    enum class AIRequestType : uint8
    {
        CHAT_RESPONSE   = 1,    // Generate chat response
        COMBAT_DECISION = 2,    // Combat strategy decision
        STRATEGY_ADAPT  = 3,    // Adapt overall strategy
        GOSSIP_GENERATE = 4,    // Generate gossip from event
        SOCIAL_DECISION = 5,    // Social interaction decision
        GOSSIP_TWEAK    = 6     // Tweak/vary a gossip template using AI
    };

    struct AIRequest
    {
        uint32 requestId;
        uint64 botGuid;
        AIRequestType type;
        AIRequestPriority priority;
        std::string prompt;
        std::string context;             // Additional context (memory, events, etc.)
        uint64 createdAt;
        uint64 timeoutAt;
        std::function<void(const std::string&, bool)> callback;  // (response, success)

        // For comparison in priority queue
        bool operator<(const AIRequest& other) const
        {
            // Higher priority first (lower number = higher priority)
            return priority > other.priority;
        }
    };

    struct AIResponse
    {
        uint32 requestId;
        std::string response;
        bool success;
        std::string errorMessage;
        uint64 processingTimeMs;
    };

    // Response cache entry
    struct CachedResponse
    {
        size_t contextHash;
        std::string response;
        uint64 createdAt;
        uint64 expiresAt;
        uint32 hitCount;
    };

    // =========================================================================
    // HTTP API TYPES
    // =========================================================================

    struct APIStats
    {
        uint32 totalBots;
        uint32 tier1Count;
        uint32 tier2Count;
        uint32 tier3Count;
        uint32 activeZones;
        uint32 gossipPoolSize;
        uint32 eventBufferSize;
        uint32 pendingAIRequests;
        uint32 cacheHitRate;
        uint64 uptimeSeconds;
    };

    // =========================================================================
    // UTILITY FUNCTIONS
    // =========================================================================

    inline const char* TierToString(BotTier tier)
    {
        switch (tier)
        {
            case BotTier::TIER_1_ACTIVE:  return "Tier 1 (Active)";
            case BotTier::TIER_2_AWARE:   return "Tier 2 (Aware)";
            case BotTier::TIER_3_PASSIVE: return "Tier 3 (Passive)";
            default: return "Unknown";
        }
    }

    inline const char* EventTypeToString(WorldEventType type)
    {
        switch (type)
        {
            case WorldEventType::PLAYER_KILLED_MOB:     return "Player killed mob";
            case WorldEventType::PLAYER_KILLED_PLAYER:  return "Player killed player";
            case WorldEventType::BOT_KILLED_MOB:        return "Bot killed mob";
            case WorldEventType::BOT_KILLED_PLAYER:     return "Bot killed player";
            case WorldEventType::BOSS_ENGAGED:          return "Boss engaged";
            case WorldEventType::BOSS_KILLED:           return "Boss killed";
            case WorldEventType::WIPE:                  return "Wipe";
            case WorldEventType::PLAYER_DIED:           return "Player died";
            case WorldEventType::BOT_DIED:              return "Bot died";
            case WorldEventType::COMBAT_KILL:           return "Combat kill";
            case WorldEventType::COMBAT_PVP_KILL:       return "Combat PvP kill";
            case WorldEventType::PLAYER_CHAT:           return "Player chat";
            case WorldEventType::BOT_CHAT:              return "Bot chat";
            case WorldEventType::PLAYER_EMOTE:          return "Player emote";
            case WorldEventType::TRADE_COMPLETED:       return "Trade completed";
            case WorldEventType::GUILD_INVITE:          return "Guild invite";
            case WorldEventType::GROUP_FORMED:          return "Group formed";
            case WorldEventType::GROUP_DISBANDED:       return "Group disbanded";
            case WorldEventType::DUEL_STARTED:          return "Duel started";
            case WorldEventType::DUEL_ENDED:            return "Duel ended";
            case WorldEventType::DUEL_REQUEST:          return "Duel requested";
            case WorldEventType::DUEL_START:            return "Duel started";
            case WorldEventType::DUEL_END:              return "Duel ended";
            case WorldEventType::CHAT_MESSAGE:          return "Chat message";
            case WorldEventType::RARE_SPAWN:            return "Rare spawn";
            case WorldEventType::WORLD_BOSS_SPAWN:      return "World boss spawn";
            case WorldEventType::FESTIVAL_START:        return "Festival start";
            case WorldEventType::FESTIVAL_END:          return "Festival end";
            case WorldEventType::GUARD_CALLED:          return "Guard called";
            case WorldEventType::WORLD_EVENT_START:     return "World event start";
            case WorldEventType::WORLD_EVENT_END:       return "World event end";
            case WorldEventType::PLAYER_ENTERED_ZONE:   return "Player entered zone";
            case WorldEventType::PLAYER_LEFT_ZONE:      return "Player left zone";
            case WorldEventType::BOT_ENTERED_ZONE:      return "Bot entered zone";
            case WorldEventType::BOT_LEFT_ZONE:         return "Bot left zone";
            case WorldEventType::ZONE_BECAME_ACTIVE:    return "Zone active";
            case WorldEventType::ZONE_BECAME_EMPTY:     return "Zone empty";
            case WorldEventType::ZONE_ENTER:            return "Zone enter";
            case WorldEventType::LOOT_ITEM:             return "Loot item";
            case WorldEventType::QUEST_COMPLETE:        return "Quest complete";
            case WorldEventType::LEVEL_UP:              return "Level up";
            case WorldEventType::ACHIEVEMENT:           return "Achievement";
            case WorldEventType::AUCTION_CREATED:       return "Auction created";
            case WorldEventType::AUCTION_SOLD:          return "Auction sold";
            case WorldEventType::MAIL_SENT:             return "Mail sent";
            case WorldEventType::FACTION_CONFLICT:      return "Faction conflict";
            case WorldEventType::MERCHANT_EVENT:        return "Merchant event";
            case WorldEventType::WEATHER_CHANGE:        return "Weather change";
            case WorldEventType::HOLIDAY_EVENT:         return "Holiday event";
            case WorldEventType::BOT_ACHIEVEMENT:       return "Bot achievement";
            case WorldEventType::GUILD_EVENT:           return "Guild event";
            case WorldEventType::GOSSIP:                return "Gossip";
            case WorldEventType::GUILD_JOIN:            return "Guild join";
            case WorldEventType::GUILD_LEAVE:           return "Guild leave";
            default: return "Unknown";
        }
    }

    inline const char* AWLogLevelToString(AWLogLevel level)
    {
        switch (level)
        {
            case AWLogLevel::AW_ERROR:   return "ERROR";
            case AWLogLevel::AW_WARNING: return "WARN";
            case AWLogLevel::AW_INFO:    return "INFO";
            case AWLogLevel::AW_DEBUG:   return "DEBUG";
            case AWLogLevel::AW_TRACE:   return "TRACE";
            default: return "???";
        }
    }

    // =========================================================================
    // SOCKET COMMAND SYSTEM
    // =========================================================================
    // Commands that can be received from external systems (console, daemon, bridge)
    // to control bot behavior. These map to BehaviorExecutor actions.
    // =========================================================================

    enum class SocketCommandType : uint8
    {
        // Chat commands
        BOT_SAY         = 1,    // Make bot say something
        BOT_YELL        = 2,    // Make bot yell
        BOT_WHISPER     = 3,    // Make bot whisper to target
        BOT_EMOTE       = 4,    // Make bot perform emote

        // Consumable commands
        BOT_DRINK       = 10,   // Make bot drink
        BOT_EAT         = 11,   // Make bot eat

        // Movement commands
        BOT_FOLLOW      = 20,   // Make bot follow target
        BOT_STAY        = 21,   // Make bot stay in place
        BOT_MOVE_TO     = 22,   // Make bot move to position

        // Combat commands
        BOT_ATTACK      = 30,   // Make bot attack target
        BOT_FLEE        = 31,   // Make bot flee
        BOT_ASSIST      = 32,   // Make bot assist target
        BOT_STOP_COMBAT = 33,   // Make bot disengage

        // Strategy commands
        BOT_SET_ROLE     = 40,   // Set bot role (tank, healer, dps)
        BOT_SET_STRATEGY = 41,   // Set bot strategy string

        // System commands
        REQUEST_STATE   = 50,   // Request bot state
        SET_DEBUG       = 51,   // Enable/disable debug
        AUTH            = 52,   // Authentication handshake
        PING            = 53,   // Keep-alive ping

        // Goal commands
        SET_GOAL        = 60,   // Set a new goal for bot
        CANCEL_GOAL     = 61,   // Cancel current goal
        QUERY_GOALS     = 62,   // Query bot's current goals

        // Behavior controller commands
        BOT_SET_STANCE           = 70,  // Set behavior stance (passive/defensive/aggressive)
        BOT_SET_BEHAVIOR_PROFILE = 71,  // Set full behavior profile
        BOT_TOGGLE_DEBUG_THOUGHTS = 72, // Toggle debug thought broadcasting
        BOT_GET_BEHAVIOR         = 73   // Query bot's current behavior profile
    };

    /**
     * SocketCommand - A command received from external systems.
     *
     * This struct represents an action to be performed on a bot, received via
     * the realtime bridge, HTTP API, or console. Commands are processed by
     * AliveWorldManager::HandleSocketCommand() and translated to BehaviorExecutor
     * actions.
     */
    struct SocketCommand
    {
        SocketCommandType type;
        uint64 botGuid;          // Target bot GUID
        uint64 targetGuid;       // Secondary target (for whisper, follow, attack)
        std::string message;     // Text content (for say, yell, emote, strategy)
        uint32 numParam;         // Numeric parameter (emote ID, spell ID, etc.)
        float x, y, z;           // Position parameters (for move_to)

        SocketCommand()
            : type(SocketCommandType::BOT_SAY)
            , botGuid(0)
            , targetGuid(0)
            , numParam(0)
            , x(0), y(0), z(0)
        {}

        // Convenience constructors for common commands
        static SocketCommand Say(uint64 guid, const std::string& text)
        {
            SocketCommand cmd;
            cmd.type = SocketCommandType::BOT_SAY;
            cmd.botGuid = guid;
            cmd.message = text;
            return cmd;
        }

        static SocketCommand Emote(uint64 guid, uint32 emoteId)
        {
            SocketCommand cmd;
            cmd.type = SocketCommandType::BOT_EMOTE;
            cmd.botGuid = guid;
            cmd.numParam = emoteId;
            return cmd;
        }

        static SocketCommand Attack(uint64 botGuid, uint64 targetGuid)
        {
            SocketCommand cmd;
            cmd.type = SocketCommandType::BOT_ATTACK;
            cmd.botGuid = botGuid;
            cmd.targetGuid = targetGuid;
            return cmd;
        }

        static SocketCommand Follow(uint64 botGuid, uint64 targetGuid)
        {
            SocketCommand cmd;
            cmd.type = SocketCommandType::BOT_FOLLOW;
            cmd.botGuid = botGuid;
            cmd.targetGuid = targetGuid;
            return cmd;
        }
    };

    inline const char* SocketCommandTypeToString(SocketCommandType type)
    {
        switch (type)
        {
            case SocketCommandType::BOT_SAY:        return "Say";
            case SocketCommandType::BOT_YELL:       return "Yell";
            case SocketCommandType::BOT_WHISPER:    return "Whisper";
            case SocketCommandType::BOT_EMOTE:      return "Emote";
            case SocketCommandType::BOT_DRINK:      return "Drink";
            case SocketCommandType::BOT_EAT:        return "Eat";
            case SocketCommandType::BOT_FOLLOW:     return "Follow";
            case SocketCommandType::BOT_STAY:       return "Stay";
            case SocketCommandType::BOT_MOVE_TO:    return "MoveTo";
            case SocketCommandType::BOT_ATTACK:     return "Attack";
            case SocketCommandType::BOT_FLEE:       return "Flee";
            case SocketCommandType::BOT_ASSIST:     return "Assist";
            case SocketCommandType::BOT_STOP_COMBAT: return "StopCombat";
            case SocketCommandType::BOT_SET_ROLE:   return "SetRole";
            case SocketCommandType::BOT_SET_STRATEGY: return "SetStrategy";
            case SocketCommandType::REQUEST_STATE:  return "RequestState";
            case SocketCommandType::SET_DEBUG:      return "SetDebug";
            case SocketCommandType::AUTH:           return "Auth";
            case SocketCommandType::PING:           return "Ping";
            case SocketCommandType::SET_GOAL:       return "SetGoal";
            case SocketCommandType::CANCEL_GOAL:    return "CancelGoal";
            case SocketCommandType::QUERY_GOALS:    return "QueryGoals";
            case SocketCommandType::BOT_SET_STANCE:           return "SetStance";
            case SocketCommandType::BOT_SET_BEHAVIOR_PROFILE: return "SetBehaviorProfile";
            case SocketCommandType::BOT_TOGGLE_DEBUG_THOUGHTS: return "ToggleDebugThoughts";
            case SocketCommandType::BOT_GET_BEHAVIOR:         return "GetBehavior";
            default: return "Unknown";
        }
    }

} // namespace AliveWorld

#endif // ALIVE_WORLD_TYPES_H
