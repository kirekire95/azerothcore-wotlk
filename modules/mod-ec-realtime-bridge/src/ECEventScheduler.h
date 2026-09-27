/*
 * EC Event Scheduler - Shared Task Scheduling System
 * Inspired by AzerothCore Boss Script TaskScheduler pattern
 *
 * Used by: mod-alive-world, mod-ec-guard-system, mod-ec-world-events
 *
 * Features:
 * - One-shot and repeating events
 * - Event grouping (cancel all events in a group)
 * - Priority-based execution
 * - Async callback support
 */

#ifndef EC_EVENT_SCHEDULER_H
#define EC_EVENT_SCHEDULER_H

#include "Common.h"
#include <chrono>
#include <functional>
#include <map>
#include <memory>
#include <queue>
#include <set>
#include <string>
#include <vector>

namespace ECShared
{
    // =========================================================================
    // EVENT TYPES (shared across modules)
    // =========================================================================

    enum class ScheduledEventType : uint16
    {
        // Generic
        NONE                    = 0,
        CUSTOM                  = 1,

        // AliveWorld Events (100-199)
        AW_GOSSIP_SPREAD        = 100,  // Periodic gossip propagation
        AW_ZONE_UPDATE          = 101,  // Update zone activity levels
        AW_BOT_TIER_CHECK       = 102,  // Re-evaluate bot tiers
        AW_AI_QUEUE_PROCESS     = 103,  // Process AI request queue
        AW_MEMORY_CLEANUP       = 104,  // Clean old memories
        AW_WEATHER_GOSSIP       = 105,  // Generate weather gossip
        AW_TIME_CHECK           = 106,  // Check time transitions
        AW_GOAL_PROGRESS        = 107,  // Update goal progress
        AW_SOCIAL_DECAY         = 108,  // Decay social relationships

        // Guard System Events (200-299)
        GS_HEAT_DECAY           = 200,  // Decay zone heat levels
        GS_WANTED_DECAY         = 201,  // Decay player wanted levels
        GS_GUARD_PATROL         = 202,  // Update guard patrol routes
        GS_DISPATCH_CHECK       = 203,  // Check if guards need dispatch
        GS_ZONE_PHASE_CHECK     = 204,  // Check zone phase transitions
        GS_GUARD_DESPAWN        = 205,  // Check for guard despawns
        GS_BROADCAST_UPDATE     = 206,  // LocalDefense broadcasts

        // World Events (300-399)
        WE_EVENT_UPDATE         = 300,  // Update event progress
        WE_PARTICIPANT_CHECK    = 301,  // Check participant eligibility
        WE_REWARD_DISTRIBUTION  = 302,  // Distribute event rewards
        WE_SPAWN_WAVE           = 303,  // Spawn next wave of creatures
        WE_PHASE_TRANSITION     = 304,  // Event phase transition
        WE_BOSS_ABILITY         = 305,  // Boss ability timer

        // Bridge Events (400-499)
        BR_HEARTBEAT            = 400,  // Bridge heartbeat
        BR_STATE_SYNC           = 401,  // Sync state to Node.js
        BR_QUEUE_FLUSH          = 402   // Flush message queue
    };

    // =========================================================================
    // EVENT PRIORITY
    // =========================================================================

    enum class EventPriority : uint8
    {
        CRITICAL    = 0,    // Execute immediately (phase transitions)
        HIGH        = 1,    // High priority (combat decisions)
        NORMAL      = 2,    // Normal priority (most events)
        LOW         = 3,    // Low priority (cleanup, decay)
        BACKGROUND  = 4     // Background tasks
    };

    // =========================================================================
    // EVENT GROUP (for batch cancellation)
    // =========================================================================

    enum class EventGroup : uint8
    {
        NONE            = 0,
        ALIVE_WORLD     = 1,
        GUARD_SYSTEM    = 2,
        WORLD_EVENTS    = 3,
        BRIDGE          = 4,
        COMBAT          = 5,
        ZONE_SPECIFIC   = 6,
        PLAYER_SPECIFIC = 7
    };

    // =========================================================================
    // SCHEDULED EVENT
    // =========================================================================

    struct ScheduledEvent
    {
        uint32 eventId;                     // Unique event ID
        ScheduledEventType type;
        EventPriority priority;
        EventGroup group;

        uint64 executeTime;                 // When to execute (game time ms)
        uint32 repeatInterval;              // 0 = one-shot, >0 = repeat interval in ms
        uint32 repeatCount;                 // 0 = infinite, >0 = max repeats
        uint32 currentRepeat;               // Current repeat counter

        std::function<void()> callback;     // What to execute
        std::string debugName;              // For logging

        // Context data (optional)
        uint32 contextId;                   // Zone ID, Player GUID low, etc.
        uint64 contextGuid;                 // Full GUID if needed

        // State
        bool cancelled = false;
        bool executing = false;

        ScheduledEvent()
            : eventId(0)
            , type(ScheduledEventType::NONE)
            , priority(EventPriority::NORMAL)
            , group(EventGroup::NONE)
            , executeTime(0)
            , repeatInterval(0)
            , repeatCount(0)
            , currentRepeat(0)
            , contextId(0)
            , contextGuid(0)
        {}

        // For priority queue ordering (earliest first, then by priority)
        bool operator>(const ScheduledEvent& other) const
        {
            if (executeTime != other.executeTime)
                return executeTime > other.executeTime;
            return static_cast<uint8>(priority) > static_cast<uint8>(other.priority);
        }
    };

    // =========================================================================
    // EVENT SCHEDULER
    // =========================================================================

    class EventScheduler
    {
    public:
        EventScheduler();
        ~EventScheduler() = default;

        // =====================================================================
        // SCHEDULING API
        // =====================================================================

        // Schedule one-shot event
        uint32 Schedule(uint32 delayMs, ScheduledEventType type, std::function<void()> callback);

        // Schedule one-shot with priority and group
        uint32 Schedule(uint32 delayMs, ScheduledEventType type, EventPriority priority,
                        EventGroup group, std::function<void()> callback);

        // Schedule repeating event
        uint32 ScheduleRepeating(uint32 intervalMs, ScheduledEventType type,
                                  std::function<void()> callback, uint32 maxRepeats = 0);

        // Schedule repeating with full options
        uint32 ScheduleRepeating(uint32 initialDelayMs, uint32 intervalMs,
                                  ScheduledEventType type, EventPriority priority,
                                  EventGroup group, std::function<void()> callback,
                                  uint32 maxRepeats = 0);

        // Schedule with context (for zone/player specific events)
        uint32 ScheduleWithContext(uint32 delayMs, ScheduledEventType type,
                                    uint32 contextId, std::function<void()> callback);

        // =====================================================================
        // CANCELLATION API
        // =====================================================================

        // Cancel specific event by ID
        bool Cancel(uint32 eventId);

        // Cancel all events of a type
        uint32 CancelByType(ScheduledEventType type);

        // Cancel all events in a group
        uint32 CancelByGroup(EventGroup group);

        // Cancel all events with context
        uint32 CancelByContext(uint32 contextId);

        // Cancel all events
        void CancelAll();

        // =====================================================================
        // QUERY API
        // =====================================================================

        // Check if event exists and is pending
        bool IsScheduled(uint32 eventId) const;

        // Get time until event executes (0 if not found or past due)
        uint32 GetTimeUntil(uint32 eventId) const;

        // Count events
        uint32 GetPendingCount() const;
        uint32 GetPendingCountByType(ScheduledEventType type) const;
        uint32 GetPendingCountByGroup(EventGroup group) const;

        // =====================================================================
        // EXECUTION
        // =====================================================================

        // Process due events (call from Update loop)
        void Update(uint32 diffMs);

        // Process all events immediately (for shutdown)
        void ProcessAll();

        // =====================================================================
        // CONFIGURATION
        // =====================================================================

        // Set max events to process per update (0 = unlimited)
        void SetMaxEventsPerUpdate(uint32 max) { m_maxEventsPerUpdate = max; }

        // Enable/disable debug logging
        void SetDebugLogging(bool enabled) { m_debugLogging = enabled; }

        // =====================================================================
        // STATISTICS
        // =====================================================================

        struct Stats
        {
            uint64 totalScheduled = 0;
            uint64 totalExecuted = 0;
            uint64 totalCancelled = 0;
            uint64 totalRepeats = 0;
            uint32 currentPending = 0;
            uint32 peakPending = 0;
            uint64 lastUpdateTime = 0;
            uint32 eventsLastUpdate = 0;
        };

        const Stats& GetStats() const { return m_stats; }
        void ResetStats();

    private:
        // Generate unique event ID
        uint32 GenerateEventId();

        // Add event to queue
        void AddEvent(ScheduledEvent&& event);

        // Priority queue (min-heap by executeTime)
        std::priority_queue<ScheduledEvent, std::vector<ScheduledEvent>,
                            std::greater<ScheduledEvent>> m_eventQueue;

        // Event lookup by ID (for cancellation)
        std::map<uint32, ScheduledEvent*> m_eventById;

        // Event tracking by group
        std::map<EventGroup, std::set<uint32>> m_eventsByGroup;

        // Event tracking by type
        std::map<ScheduledEventType, std::set<uint32>> m_eventsByType;

        // Event tracking by context
        std::map<uint32, std::set<uint32>> m_eventsByContext;

        // Current game time
        uint64 m_currentTime = 0;

        // Event ID counter
        uint32 m_nextEventId = 1;

        // Configuration
        uint32 m_maxEventsPerUpdate = 100;
        bool m_debugLogging = false;

        // Statistics
        Stats m_stats;

        // Rebuild queue after cancellations
        void RebuildQueueIfNeeded();
        bool m_needsRebuild = false;
        uint32 m_cancelledCount = 0;
        static constexpr uint32 REBUILD_THRESHOLD = 50;
    };

    // =========================================================================
    // GLOBAL SCHEDULER INSTANCE
    // =========================================================================

    EventScheduler* GetGlobalScheduler();

    // =========================================================================
    // HELPER FUNCTIONS
    // =========================================================================

    inline const char* EventTypeToString(ScheduledEventType type)
    {
        switch (type)
        {
            case ScheduledEventType::NONE:              return "None";
            case ScheduledEventType::CUSTOM:            return "Custom";

            // AliveWorld
            case ScheduledEventType::AW_GOSSIP_SPREAD:  return "AW_GossipSpread";
            case ScheduledEventType::AW_ZONE_UPDATE:    return "AW_ZoneUpdate";
            case ScheduledEventType::AW_BOT_TIER_CHECK: return "AW_BotTierCheck";
            case ScheduledEventType::AW_AI_QUEUE_PROCESS: return "AW_AIQueueProcess";
            case ScheduledEventType::AW_MEMORY_CLEANUP: return "AW_MemoryCleanup";
            case ScheduledEventType::AW_WEATHER_GOSSIP: return "AW_WeatherGossip";
            case ScheduledEventType::AW_TIME_CHECK:     return "AW_TimeCheck";
            case ScheduledEventType::AW_GOAL_PROGRESS:  return "AW_GoalProgress";
            case ScheduledEventType::AW_SOCIAL_DECAY:   return "AW_SocialDecay";

            // Guard System
            case ScheduledEventType::GS_HEAT_DECAY:     return "GS_HeatDecay";
            case ScheduledEventType::GS_WANTED_DECAY:   return "GS_WantedDecay";
            case ScheduledEventType::GS_GUARD_PATROL:   return "GS_GuardPatrol";
            case ScheduledEventType::GS_DISPATCH_CHECK: return "GS_DispatchCheck";
            case ScheduledEventType::GS_ZONE_PHASE_CHECK: return "GS_ZonePhaseCheck";
            case ScheduledEventType::GS_GUARD_DESPAWN:  return "GS_GuardDespawn";
            case ScheduledEventType::GS_BROADCAST_UPDATE: return "GS_BroadcastUpdate";

            // World Events
            case ScheduledEventType::WE_EVENT_UPDATE:   return "WE_EventUpdate";
            case ScheduledEventType::WE_PARTICIPANT_CHECK: return "WE_ParticipantCheck";
            case ScheduledEventType::WE_REWARD_DISTRIBUTION: return "WE_RewardDistribution";
            case ScheduledEventType::WE_SPAWN_WAVE:     return "WE_SpawnWave";
            case ScheduledEventType::WE_PHASE_TRANSITION: return "WE_PhaseTransition";
            case ScheduledEventType::WE_BOSS_ABILITY:   return "WE_BossAbility";

            // Bridge
            case ScheduledEventType::BR_HEARTBEAT:      return "BR_Heartbeat";
            case ScheduledEventType::BR_STATE_SYNC:     return "BR_StateSync";
            case ScheduledEventType::BR_QUEUE_FLUSH:    return "BR_QueueFlush";

            default: return "Unknown";
        }
    }

    inline const char* EventGroupToString(EventGroup group)
    {
        switch (group)
        {
            case EventGroup::NONE:            return "None";
            case EventGroup::ALIVE_WORLD:     return "AliveWorld";
            case EventGroup::GUARD_SYSTEM:    return "GuardSystem";
            case EventGroup::WORLD_EVENTS:    return "WorldEvents";
            case EventGroup::BRIDGE:          return "Bridge";
            case EventGroup::COMBAT:          return "Combat";
            case EventGroup::ZONE_SPECIFIC:   return "ZoneSpecific";
            case EventGroup::PLAYER_SPECIFIC: return "PlayerSpecific";
            default: return "Unknown";
        }
    }

    inline const char* EventPriorityToString(EventPriority priority)
    {
        switch (priority)
        {
            case EventPriority::CRITICAL:   return "Critical";
            case EventPriority::HIGH:       return "High";
            case EventPriority::NORMAL:     return "Normal";
            case EventPriority::LOW:        return "Low";
            case EventPriority::BACKGROUND: return "Background";
            default: return "Unknown";
        }
    }

} // namespace ECShared

// Convenience macro for accessing global scheduler
#define sECScheduler ECShared::GetGlobalScheduler()

#endif // EC_EVENT_SCHEDULER_H
