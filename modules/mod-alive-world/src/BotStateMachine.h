/*
 * mod-alive-world - Bot State Machine
 * Inspired by Battleground/Boss scripted state patterns
 *
 * Each bot has a behavioral state that affects AI decisions and interactions.
 * States flow based on game events, player proximity, and AI tier.
 */

#ifndef BOT_STATE_MACHINE_H
#define BOT_STATE_MACHINE_H

#include "AliveWorldTypes.h"
#include "Common.h"
#include "ObjectGuid.h"

#include <chrono>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

class Player;

namespace AliveWorld
{
    // =========================================================================
    // BOT BEHAVIORAL STATE (like BattlegroundStatus)
    // =========================================================================

    enum class BotBehaviorState : uint8
    {
        // Core states
        IDLE            = 0,  // Standing around, low activity
        TRAVELING       = 1,  // Moving between locations
        GRINDING        = 2,  // Farming mobs
        QUESTING        = 3,  // Following quest objectives
        SOCIALIZING     = 4,  // Interacting with other players/bots

        // Combat states
        COMBAT_ACTIVE   = 10, // In active combat
        COMBAT_FLEEING  = 11, // Running from combat
        COMBAT_SUPPORT  = 12, // Supporting allies in combat

        // Event states
        EVENT_PARTICIPANT = 20, // Participating in world event
        EVENT_LEADER      = 21, // Leading a group event
        EVENT_SPECTATOR   = 22, // Watching an event

        // Special states
        RESTING         = 30, // In inn/city, resting
        CRAFTING        = 31, // Using profession
        TRADING         = 32, // At vendor/AH
        ROLEPLAYING     = 33, // RP interaction mode

        // Patrol/Guard (for guard-like bots)
        PATROL          = 40, // Walking a patrol route
        GUARD_POST      = 41, // Standing guard at position
        PURSUIT         = 42  // Chasing a target
    };

    // =========================================================================
    // STATE TRANSITION TRIGGERS
    // =========================================================================

    enum class BotStateTrigger : uint8
    {
        // Environmental
        PLAYER_NEARBY       = 1,   // Real player entered range
        PLAYER_LEFT         = 2,   // Real player left range
        ENTERED_CITY        = 3,   // Entered major city
        LEFT_CITY           = 4,   // Left major city
        ENTERED_DANGER_ZONE = 5,   // Entered PvP/high-level area
        TIME_TRANSITION     = 6,   // Day/night change

        // Combat
        COMBAT_START        = 10,  // Entered combat
        COMBAT_END          = 11,  // Left combat
        LOW_HEALTH          = 12,  // Health dropped below threshold
        ALLY_IN_DANGER      = 13,  // Nearby ally needs help
        ENEMY_SPOTTED       = 14,  // Hostile detected

        // Social
        RECEIVED_CHAT       = 20,  // Someone chatted at/near bot
        RECEIVED_EMOTE      = 21,  // Someone emoted at bot
        GROUP_INVITE        = 22,  // Invited to group
        GROUP_DISBANDED     = 23,  // Group ended

        // Goals/Events
        GOAL_STARTED        = 30,  // Started pursuing a goal
        GOAL_COMPLETED      = 31,  // Completed a goal
        GOAL_BLOCKED        = 32,  // Goal cannot progress
        EVENT_STARTED       = 33,  // World event began nearby
        EVENT_ENDED         = 34,  // World event ended

        // Tier changes
        TIER_PROMOTED       = 40,  // Promoted to higher tier
        TIER_DEMOTED        = 41,  // Demoted to lower tier

        // Timers
        STATE_TIMEOUT       = 50,  // Been in state too long
        PERIODIC_CHECK      = 51   // Regular state evaluation
    };

    // =========================================================================
    // STATE CONFIGURATION
    // =========================================================================

    struct BotStateConfig
    {
        // Timeouts per state (ms)
        uint32 idleTimeoutMs        = 300000;   // 5 min max idle
        uint32 travelingTimeoutMs   = 600000;   // 10 min max travel
        uint32 grindingTimeoutMs    = 1800000;  // 30 min max grind
        uint32 socializingTimeoutMs = 180000;   // 3 min max chat

        // Combat thresholds
        float fleeHealthPercent     = 20.0f;    // Flee below 20% HP
        float supportRangeYards     = 40.0f;    // Range to support allies

        // Proximity ranges
        float playerProximityRange  = 50.0f;    // Range to detect players
        float socialRange           = 15.0f;    // Range for social interaction

        // Transition probabilities
        float idleToTravelChance    = 0.3f;     // 30% chance to start traveling
        float travelToGrindChance   = 0.5f;     // 50% chance to grind at destination
    };

    // =========================================================================
    // BOT STATE MACHINE
    // =========================================================================

    class BotStateMachine
    {
    public:
        BotStateMachine(uint64 botGuid, const std::string& botName);
        ~BotStateMachine() = default;

        // =====================================================================
        // STATE MACHINE CORE
        // =====================================================================

        // Get current state
        BotBehaviorState GetState() const { return m_currentState; }
        BotBehaviorState GetPreviousState() const { return m_previousState; }

        // Check if transition is valid
        bool CanTransitionTo(BotBehaviorState newState) const;

        // Attempt state transition
        bool TryTransition(BotStateTrigger trigger);

        // Force state change (bypasses validation)
        void ForceState(BotBehaviorState newState);

        // Update state machine (called periodically)
        void Update(uint32 diffMs);

        // =====================================================================
        // STATE QUERIES
        // =====================================================================

        bool IsInCombatState() const;
        bool IsInSocialState() const;
        bool IsInEventState() const;
        bool IsIdle() const { return m_currentState == BotBehaviorState::IDLE; }
        bool IsBusy() const;  // In a state that shouldn't be interrupted

        // State timing
        uint32 GetTimeInStateMs() const { return m_stateTimeMs; }
        uint32 GetTimeInStateSec() const { return m_stateTimeMs / 1000; }
        bool IsStateTimedOut() const;

        // =====================================================================
        // CONTEXT MANAGEMENT
        // =====================================================================

        // Set/get context for current state
        void SetStateContext(const std::string& key, const std::string& value);
        std::string GetStateContext(const std::string& key) const;
        void ClearStateContext();

        // Target tracking
        void SetTarget(ObjectGuid target) { m_targetGuid = target; }
        ObjectGuid GetTarget() const { return m_targetGuid; }
        void ClearTarget() { m_targetGuid = ObjectGuid::Empty; }

        // Location tracking
        void SetDestination(float x, float y, float z, uint32 mapId);
        bool HasDestination() const { return m_hasDestination; }
        void ClearDestination() { m_hasDestination = false; }

        // =====================================================================
        // CALLBACKS
        // =====================================================================

        using StateChangeCallback = std::function<void(uint64 botGuid, BotBehaviorState oldState, BotBehaviorState newState)>;
        void SetStateChangeCallback(StateChangeCallback callback) { m_onStateChange = callback; }

        // =====================================================================
        // PERSONALITY INTEGRATION
        // =====================================================================

        // Personality affects state transitions
        void SetPersonality(const BotPersonality& personality) { m_personality = personality; }
        const BotPersonality& GetPersonality() const { return m_personality; }

        // =====================================================================
        // CONFIGURATION
        // =====================================================================

        void SetConfig(const BotStateConfig& config) { m_config = config; }
        const BotStateConfig& GetConfig() const { return m_config; }

        // =====================================================================
        // DEBUG
        // =====================================================================

        const char* GetStateString() const;
        void DumpState() const;

        // =====================================================================
        // ACCESSORS
        // =====================================================================

        uint64 GetBotGuid() const { return m_botGuid; }
        const std::string& GetBotName() const { return m_botName; }

    private:
        // Transition logic
        void TransitionTo(BotBehaviorState newState);
        void OnEnterState(BotBehaviorState state);
        void OnExitState(BotBehaviorState state);

        // State-specific update logic
        void UpdateIdleState(uint32 diffMs);
        void UpdateCombatState(uint32 diffMs);
        void UpdateSocialState(uint32 diffMs);
        void UpdateTravelState(uint32 diffMs);

        // Determine target state from trigger
        BotBehaviorState DetermineTargetState(BotStateTrigger trigger) const;

        // Personality-based randomization
        bool RollTransitionChance(float baseChance) const;

        // Bot identity
        uint64 m_botGuid;
        std::string m_botName;

        // State machine
        BotBehaviorState m_currentState = BotBehaviorState::IDLE;
        BotBehaviorState m_previousState = BotBehaviorState::IDLE;
        uint32 m_stateTimeMs = 0;
        uint64 m_lastTransitionTime = 0;

        // State history (for pattern detection)
        std::vector<BotBehaviorState> m_stateHistory;
        static constexpr size_t MAX_STATE_HISTORY = 10;

        // Context
        std::map<std::string, std::string> m_stateContext;
        ObjectGuid m_targetGuid;

        // Destination
        float m_destX = 0, m_destY = 0, m_destZ = 0;
        uint32 m_destMapId = 0;
        bool m_hasDestination = false;

        // Personality and config
        BotPersonality m_personality;
        BotStateConfig m_config;

        // Callbacks
        StateChangeCallback m_onStateChange;
    };

    // =========================================================================
    // ZONE ACTIVITY STATE (like BG status for zones)
    // =========================================================================

    enum class ZoneActivityLevel : uint8
    {
        DORMANT     = 0,  // No players, minimal bot activity
        QUIET       = 1,  // Few players, bots in passive mode
        ACTIVE      = 2,  // Normal activity level
        BUSY        = 3,  // High activity, lots of players
        CONTESTED   = 4,  // PvP activity, tension high
        EVENT       = 5   // World event in progress
    };

    struct ZoneActivityState
    {
        uint32 zoneId;
        uint32 mapId;
        ZoneActivityLevel level = ZoneActivityLevel::DORMANT;

        // Population tracking
        uint32 playerCount = 0;
        uint32 botCount = 0;
        uint32 allianceCount = 0;
        uint32 hordeCount = 0;

        // Activity metrics
        uint32 recentCombatEvents = 0;
        uint32 recentChatMessages = 0;
        uint32 recentDeaths = 0;

        // Timing
        uint32 timeInLevelMs = 0;
        uint64 lastUpdateTime = 0;
        uint64 lastPlayerEnterTime = 0;
        uint64 lastPlayerLeaveTime = 0;

        // World event tracking
        uint32 activeEventId = 0;
        std::string activeEventName;

        // Methods
        void Update(uint32 diffMs);
        void OnPlayerEnter();
        void OnPlayerLeave();
        void OnCombatEvent();
        void OnChatMessage();
        void OnDeath();
        void RecalculateLevel();

        bool IsDormant() const { return level == ZoneActivityLevel::DORMANT; }
        bool IsActive() const { return level >= ZoneActivityLevel::ACTIVE; }
        bool IsBusy() const { return level >= ZoneActivityLevel::BUSY; }
        bool HasEvent() const { return level == ZoneActivityLevel::EVENT; }
        uint32 GetTotalPopulation() const { return playerCount + botCount; }
    };

    // =========================================================================
    // ZONE ACTIVITY MANAGER
    // =========================================================================

    class ZoneActivityManager
    {
    public:
        static ZoneActivityManager* Instance();

        void Initialize();
        void Update(uint32 diffMs);

        ZoneActivityState* GetZoneState(uint32 zoneId);
        ZoneActivityState* GetOrCreateZoneState(uint32 zoneId, uint32 mapId);

        // Events
        void OnPlayerEnterZone(Player* player, uint32 zoneId);
        void OnPlayerLeaveZone(Player* player, uint32 zoneId);
        void OnBotEnterZone(uint64 botGuid, uint32 zoneId);
        void OnBotLeaveZone(uint64 botGuid, uint32 zoneId);
        void OnWorldEventStart(uint32 zoneId, uint32 eventId, const std::string& eventName);
        void OnWorldEventEnd(uint32 zoneId, uint32 eventId);

        // Queries
        std::vector<uint32> GetActiveZones() const;
        std::vector<uint32> GetDormantZones() const;
        uint32 GetBusiestZone() const;

        // Statistics
        uint32 GetTotalActiveZones() const;
        uint32 GetTotalPlayers() const;
        uint32 GetTotalBots() const;

    private:
        ZoneActivityManager() = default;
        ~ZoneActivityManager() = default;

        std::map<uint32, std::unique_ptr<ZoneActivityState>> m_zoneStates;
    };

    // =========================================================================
    // HELPER FUNCTIONS
    // =========================================================================

    inline const char* BotBehaviorStateToString(BotBehaviorState state)
    {
        switch (state)
        {
            case BotBehaviorState::IDLE:            return "Idle";
            case BotBehaviorState::TRAVELING:       return "Traveling";
            case BotBehaviorState::GRINDING:        return "Grinding";
            case BotBehaviorState::QUESTING:        return "Questing";
            case BotBehaviorState::SOCIALIZING:     return "Socializing";
            case BotBehaviorState::COMBAT_ACTIVE:   return "Combat";
            case BotBehaviorState::COMBAT_FLEEING:  return "Fleeing";
            case BotBehaviorState::COMBAT_SUPPORT:  return "Support";
            case BotBehaviorState::EVENT_PARTICIPANT: return "Event Participant";
            case BotBehaviorState::EVENT_LEADER:    return "Event Leader";
            case BotBehaviorState::EVENT_SPECTATOR: return "Event Spectator";
            case BotBehaviorState::RESTING:         return "Resting";
            case BotBehaviorState::CRAFTING:        return "Crafting";
            case BotBehaviorState::TRADING:         return "Trading";
            case BotBehaviorState::ROLEPLAYING:     return "Roleplaying";
            case BotBehaviorState::PATROL:          return "Patrol";
            case BotBehaviorState::GUARD_POST:      return "Guard Post";
            case BotBehaviorState::PURSUIT:         return "Pursuit";
            default: return "Unknown";
        }
    }

    inline const char* BotStateTriggerToString(BotStateTrigger trigger)
    {
        switch (trigger)
        {
            case BotStateTrigger::PLAYER_NEARBY:       return "Player Nearby";
            case BotStateTrigger::PLAYER_LEFT:         return "Player Left";
            case BotStateTrigger::ENTERED_CITY:        return "Entered City";
            case BotStateTrigger::LEFT_CITY:           return "Left City";
            case BotStateTrigger::ENTERED_DANGER_ZONE: return "Entered Danger Zone";
            case BotStateTrigger::TIME_TRANSITION:     return "Time Transition";
            case BotStateTrigger::COMBAT_START:        return "Combat Start";
            case BotStateTrigger::COMBAT_END:          return "Combat End";
            case BotStateTrigger::LOW_HEALTH:          return "Low Health";
            case BotStateTrigger::ALLY_IN_DANGER:      return "Ally In Danger";
            case BotStateTrigger::ENEMY_SPOTTED:       return "Enemy Spotted";
            case BotStateTrigger::RECEIVED_CHAT:       return "Received Chat";
            case BotStateTrigger::RECEIVED_EMOTE:      return "Received Emote";
            case BotStateTrigger::GROUP_INVITE:        return "Group Invite";
            case BotStateTrigger::GROUP_DISBANDED:     return "Group Disbanded";
            case BotStateTrigger::GOAL_STARTED:        return "Goal Started";
            case BotStateTrigger::GOAL_COMPLETED:      return "Goal Completed";
            case BotStateTrigger::GOAL_BLOCKED:        return "Goal Blocked";
            case BotStateTrigger::EVENT_STARTED:       return "Event Started";
            case BotStateTrigger::EVENT_ENDED:         return "Event Ended";
            case BotStateTrigger::TIER_PROMOTED:       return "Tier Promoted";
            case BotStateTrigger::TIER_DEMOTED:        return "Tier Demoted";
            case BotStateTrigger::STATE_TIMEOUT:       return "State Timeout";
            case BotStateTrigger::PERIODIC_CHECK:      return "Periodic Check";
            default: return "Unknown";
        }
    }

    inline const char* ZoneActivityLevelToString(ZoneActivityLevel level)
    {
        switch (level)
        {
            case ZoneActivityLevel::DORMANT:   return "Dormant";
            case ZoneActivityLevel::QUIET:     return "Quiet";
            case ZoneActivityLevel::ACTIVE:    return "Active";
            case ZoneActivityLevel::BUSY:      return "Busy";
            case ZoneActivityLevel::CONTESTED: return "Contested";
            case ZoneActivityLevel::EVENT:     return "Event";
            default: return "Unknown";
        }
    }

    #define sZoneActivityMgr AliveWorld::ZoneActivityManager::Instance()

} // namespace AliveWorld

#endif // BOT_STATE_MACHINE_H
