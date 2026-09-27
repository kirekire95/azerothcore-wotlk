/*
 * mod-alive-world - Bot State Machine Implementation
 */

#include "BotStateMachine.h"
#include "AliveWorldManager.h"
#include "Log.h"
#include "Timer.h"
#include "Player.h"
#include "SharedDefines.h"

#include <memory>
#include <random>

namespace AliveWorld
{
    // =========================================================================
    // BOT STATE MACHINE IMPLEMENTATION
    // =========================================================================

    BotStateMachine::BotStateMachine(uint64 botGuid, const std::string& botName)
        : m_botGuid(botGuid)
        , m_botName(botName)
        , m_currentState(BotBehaviorState::IDLE)
        , m_previousState(BotBehaviorState::IDLE)
        , m_stateTimeMs(0)
        , m_lastTransitionTime(getMSTime())
    {
        // Randomize initial personality
        m_personality.Randomize();
    }

    bool BotStateMachine::CanTransitionTo(BotBehaviorState newState) const
    {
        // Define valid state transitions
        switch (m_currentState)
        {
            case BotBehaviorState::IDLE:
                // Can transition to almost anything from idle
                return newState != BotBehaviorState::COMBAT_FLEEING &&
                       newState != BotBehaviorState::COMBAT_SUPPORT;

            case BotBehaviorState::TRAVELING:
                return newState == BotBehaviorState::IDLE ||
                       newState == BotBehaviorState::GRINDING ||
                       newState == BotBehaviorState::QUESTING ||
                       newState == BotBehaviorState::COMBAT_ACTIVE ||
                       newState == BotBehaviorState::SOCIALIZING ||
                       newState == BotBehaviorState::EVENT_PARTICIPANT ||
                       newState == BotBehaviorState::RESTING;

            case BotBehaviorState::GRINDING:
            case BotBehaviorState::QUESTING:
                return newState == BotBehaviorState::IDLE ||
                       newState == BotBehaviorState::TRAVELING ||
                       newState == BotBehaviorState::COMBAT_ACTIVE ||
                       newState == BotBehaviorState::RESTING ||
                       newState == BotBehaviorState::SOCIALIZING;

            case BotBehaviorState::SOCIALIZING:
                return newState == BotBehaviorState::IDLE ||
                       newState == BotBehaviorState::TRAVELING ||
                       newState == BotBehaviorState::COMBAT_ACTIVE ||
                       newState == BotBehaviorState::ROLEPLAYING;

            case BotBehaviorState::COMBAT_ACTIVE:
                return newState == BotBehaviorState::IDLE ||
                       newState == BotBehaviorState::COMBAT_FLEEING ||
                       newState == BotBehaviorState::COMBAT_SUPPORT ||
                       newState == BotBehaviorState::RESTING;  // After combat

            case BotBehaviorState::COMBAT_FLEEING:
                return newState == BotBehaviorState::IDLE ||
                       newState == BotBehaviorState::COMBAT_ACTIVE ||  // Stopped fleeing
                       newState == BotBehaviorState::RESTING;

            case BotBehaviorState::COMBAT_SUPPORT:
                return newState == BotBehaviorState::IDLE ||
                       newState == BotBehaviorState::COMBAT_ACTIVE ||
                       newState == BotBehaviorState::COMBAT_FLEEING;

            case BotBehaviorState::EVENT_PARTICIPANT:
            case BotBehaviorState::EVENT_LEADER:
            case BotBehaviorState::EVENT_SPECTATOR:
                return newState == BotBehaviorState::IDLE ||
                       newState == BotBehaviorState::COMBAT_ACTIVE ||
                       newState == BotBehaviorState::COMBAT_FLEEING;

            case BotBehaviorState::RESTING:
                return newState == BotBehaviorState::IDLE ||
                       newState == BotBehaviorState::TRAVELING ||
                       newState == BotBehaviorState::SOCIALIZING ||
                       newState == BotBehaviorState::COMBAT_ACTIVE;  // Ambushed!

            case BotBehaviorState::CRAFTING:
            case BotBehaviorState::TRADING:
                return newState == BotBehaviorState::IDLE ||
                       newState == BotBehaviorState::TRAVELING ||
                       newState == BotBehaviorState::COMBAT_ACTIVE;

            case BotBehaviorState::ROLEPLAYING:
                return newState == BotBehaviorState::IDLE ||
                       newState == BotBehaviorState::SOCIALIZING ||
                       newState == BotBehaviorState::COMBAT_ACTIVE;

            case BotBehaviorState::PATROL:
            case BotBehaviorState::GUARD_POST:
                return newState == BotBehaviorState::IDLE ||
                       newState == BotBehaviorState::PURSUIT ||
                       newState == BotBehaviorState::COMBAT_ACTIVE;

            case BotBehaviorState::PURSUIT:
                return newState == BotBehaviorState::IDLE ||
                       newState == BotBehaviorState::COMBAT_ACTIVE ||
                       newState == BotBehaviorState::PATROL ||
                       newState == BotBehaviorState::GUARD_POST;

            default:
                return true;  // Allow transition if unknown
        }
    }

    BotBehaviorState BotStateMachine::DetermineTargetState(BotStateTrigger trigger) const
    {
        switch (trigger)
        {
            // Combat triggers
            case BotStateTrigger::COMBAT_START:
                return BotBehaviorState::COMBAT_ACTIVE;

            case BotStateTrigger::COMBAT_END:
                return BotBehaviorState::IDLE;

            case BotStateTrigger::LOW_HEALTH:
                // Cautious bots flee, aggressive bots fight
                if (m_personality.caution > 0.7f)
                    return BotBehaviorState::COMBAT_FLEEING;
                return m_currentState;

            case BotStateTrigger::ALLY_IN_DANGER:
                // Loyal/helpful bots support allies
                if (m_personality.loyalty > 0.5f || m_personality.helpfulness > 0.5f)
                    return BotBehaviorState::COMBAT_SUPPORT;
                return m_currentState;

            // Social triggers
            case BotStateTrigger::RECEIVED_CHAT:
            case BotStateTrigger::RECEIVED_EMOTE:
                // Social bots engage in conversation
                if (m_personality.sociability > 0.4f && !IsInCombatState())
                    return BotBehaviorState::SOCIALIZING;
                return m_currentState;

            case BotStateTrigger::GROUP_INVITE:
                // Check if bot wants to join
                if (m_personality.sociability > 0.3f)
                    return m_currentState;  // Accept but don't change state
                return m_currentState;

            // Environment triggers
            case BotStateTrigger::PLAYER_NEARBY:
                // More active when players are around
                if (m_currentState == BotBehaviorState::IDLE && m_personality.sociability > 0.5f)
                    return BotBehaviorState::SOCIALIZING;
                return m_currentState;

            case BotStateTrigger::ENTERED_CITY:
                // Might rest or trade in cities
                if (RollTransitionChance(0.3f))
                    return BotBehaviorState::RESTING;
                if (RollTransitionChance(0.2f) && m_personality.greed > 0.5f)
                    return BotBehaviorState::TRADING;
                return m_currentState;

            // Event triggers
            case BotStateTrigger::EVENT_STARTED:
                return BotBehaviorState::EVENT_PARTICIPANT;

            case BotStateTrigger::EVENT_ENDED:
                return BotBehaviorState::IDLE;

            // Goal triggers
            case BotStateTrigger::GOAL_STARTED:
                return BotBehaviorState::QUESTING;

            case BotStateTrigger::GOAL_COMPLETED:
                return BotBehaviorState::IDLE;

            // Tier triggers (affects activity level)
            case BotStateTrigger::TIER_PROMOTED:
                // Higher tier = more active
                if (m_currentState == BotBehaviorState::IDLE)
                    return BotBehaviorState::TRAVELING;
                return m_currentState;

            case BotStateTrigger::TIER_DEMOTED:
                // Lower tier = return to idle
                if (!IsInCombatState())
                    return BotBehaviorState::IDLE;
                return m_currentState;

            // Timeout
            case BotStateTrigger::STATE_TIMEOUT:
            case BotStateTrigger::PERIODIC_CHECK:
                // Return to idle on timeout
                if (!IsInCombatState())
                    return BotBehaviorState::IDLE;
                return m_currentState;

            default:
                return m_currentState;
        }
    }

    bool BotStateMachine::TryTransition(BotStateTrigger trigger)
    {
        BotBehaviorState targetState = DetermineTargetState(trigger);

        if (targetState == m_currentState)
            return false;  // No change

        if (!CanTransitionTo(targetState))
        {
            ALIVE_LOG_DEBUG("Bot %s: Cannot transition from %s to %s (trigger: %s)",
                m_botName.c_str(),
                BotBehaviorStateToString(m_currentState),
                BotBehaviorStateToString(targetState),
                BotStateTriggerToString(trigger));
            return false;
        }

        TransitionTo(targetState);
        return true;
    }

    void BotStateMachine::ForceState(BotBehaviorState newState)
    {
        if (newState != m_currentState)
        {
            TransitionTo(newState);
        }
    }

    void BotStateMachine::TransitionTo(BotBehaviorState newState)
    {
        BotBehaviorState oldState = m_currentState;

        // Exit current state
        OnExitState(m_currentState);

        // Update state
        m_previousState = m_currentState;
        m_currentState = newState;
        m_stateTimeMs = 0;
        m_lastTransitionTime = getMSTime();

        // Record in history
        m_stateHistory.push_back(newState);
        if (m_stateHistory.size() > MAX_STATE_HISTORY)
        {
            m_stateHistory.erase(m_stateHistory.begin());
        }

        // Enter new state
        OnEnterState(newState);

        // Notify callback
        if (m_onStateChange)
        {
            m_onStateChange(m_botGuid, oldState, newState);
        }

        ALIVE_LOG_INFO("Bot %s: %s -> %s",
            m_botName.c_str(),
            BotBehaviorStateToString(oldState),
            BotBehaviorStateToString(newState));
    }

    void BotStateMachine::OnEnterState(BotBehaviorState state)
    {
        switch (state)
        {
            case BotBehaviorState::IDLE:
                ClearTarget();
                ClearDestination();
                break;

            case BotBehaviorState::COMBAT_FLEEING:
                // Clear context for fleeing
                ClearStateContext();
                break;

            case BotBehaviorState::SOCIALIZING:
                // Set mood to social
                m_personality.currentMood = "social";
                m_personality.moodTimestamp = NowMs();
                break;

            case BotBehaviorState::RESTING:
                m_personality.currentMood = "relaxed";
                m_personality.moodTimestamp = NowMs();
                break;

            default:
                break;
        }
    }

    void BotStateMachine::OnExitState(BotBehaviorState state)
    {
        switch (state)
        {
            case BotBehaviorState::COMBAT_ACTIVE:
            case BotBehaviorState::COMBAT_FLEEING:
            case BotBehaviorState::COMBAT_SUPPORT:
                // Reset combat mood
                m_personality.currentMood = "neutral";
                m_personality.moodTimestamp = NowMs();
                break;

            default:
                break;
        }
    }

    void BotStateMachine::Update(uint32 diffMs)
    {
        m_stateTimeMs += diffMs;

        // State-specific updates
        switch (m_currentState)
        {
            case BotBehaviorState::IDLE:
                UpdateIdleState(diffMs);
                break;

            case BotBehaviorState::COMBAT_ACTIVE:
            case BotBehaviorState::COMBAT_FLEEING:
            case BotBehaviorState::COMBAT_SUPPORT:
                UpdateCombatState(diffMs);
                break;

            case BotBehaviorState::SOCIALIZING:
            case BotBehaviorState::ROLEPLAYING:
                UpdateSocialState(diffMs);
                break;

            case BotBehaviorState::TRAVELING:
                UpdateTravelState(diffMs);
                break;

            default:
                break;
        }

        // Check for timeout
        if (IsStateTimedOut())
        {
            TryTransition(BotStateTrigger::STATE_TIMEOUT);
        }
    }

    void BotStateMachine::UpdateIdleState(uint32 /*diffMs*/)
    {
        // Occasionally transition to other states based on personality
        if (m_stateTimeMs > 30000)  // After 30 seconds idle
        {
            // Curious bots might start traveling
            if (RollTransitionChance(m_config.idleToTravelChance * m_personality.curiosity))
            {
                TryTransition(BotStateTrigger::PERIODIC_CHECK);
            }
        }
    }

    void BotStateMachine::UpdateCombatState(uint32 /*diffMs*/)
    {
        // Combat state updates handled by playerbots
        // This is for state-machine level monitoring
    }

    void BotStateMachine::UpdateSocialState(uint32 /*diffMs*/)
    {
        // Check if social interaction should end
        if (m_stateTimeMs > m_config.socializingTimeoutMs)
        {
            TryTransition(BotStateTrigger::STATE_TIMEOUT);
        }
    }

    void BotStateMachine::UpdateTravelState(uint32 /*diffMs*/)
    {
        // Check if travel should end
        if (m_stateTimeMs > m_config.travelingTimeoutMs)
        {
            TryTransition(BotStateTrigger::STATE_TIMEOUT);
        }
    }

    bool BotStateMachine::IsInCombatState() const
    {
        return m_currentState == BotBehaviorState::COMBAT_ACTIVE ||
               m_currentState == BotBehaviorState::COMBAT_FLEEING ||
               m_currentState == BotBehaviorState::COMBAT_SUPPORT ||
               m_currentState == BotBehaviorState::PURSUIT;
    }

    bool BotStateMachine::IsInSocialState() const
    {
        return m_currentState == BotBehaviorState::SOCIALIZING ||
               m_currentState == BotBehaviorState::ROLEPLAYING;
    }

    bool BotStateMachine::IsInEventState() const
    {
        return m_currentState == BotBehaviorState::EVENT_PARTICIPANT ||
               m_currentState == BotBehaviorState::EVENT_LEADER ||
               m_currentState == BotBehaviorState::EVENT_SPECTATOR;
    }

    bool BotStateMachine::IsBusy() const
    {
        return IsInCombatState() ||
               IsInEventState() ||
               m_currentState == BotBehaviorState::CRAFTING ||
               m_currentState == BotBehaviorState::TRADING;
    }

    bool BotStateMachine::IsStateTimedOut() const
    {
        switch (m_currentState)
        {
            case BotBehaviorState::IDLE:
                return m_stateTimeMs > m_config.idleTimeoutMs;
            case BotBehaviorState::TRAVELING:
                return m_stateTimeMs > m_config.travelingTimeoutMs;
            case BotBehaviorState::GRINDING:
                return m_stateTimeMs > m_config.grindingTimeoutMs;
            case BotBehaviorState::SOCIALIZING:
                return m_stateTimeMs > m_config.socializingTimeoutMs;
            default:
                return false;  // No timeout for other states
        }
    }

    void BotStateMachine::SetStateContext(const std::string& key, const std::string& value)
    {
        m_stateContext[key] = value;
    }

    std::string BotStateMachine::GetStateContext(const std::string& key) const
    {
        auto it = m_stateContext.find(key);
        return (it != m_stateContext.end()) ? it->second : "";
    }

    void BotStateMachine::ClearStateContext()
    {
        m_stateContext.clear();
    }

    void BotStateMachine::SetDestination(float x, float y, float z, uint32 mapId)
    {
        m_destX = x;
        m_destY = y;
        m_destZ = z;
        m_destMapId = mapId;
        m_hasDestination = true;
    }

    bool BotStateMachine::RollTransitionChance(float baseChance) const
    {
        static std::random_device rd;
        static std::mt19937 gen(rd());
        std::uniform_real_distribution<float> dis(0.0f, 1.0f);
        return dis(gen) < baseChance;
    }

    const char* BotStateMachine::GetStateString() const
    {
        return BotBehaviorStateToString(m_currentState);
    }

    void BotStateMachine::DumpState() const
    {
        LOG_INFO("module", "=== Bot State: %s (GUID: %llu) ===", m_botName.c_str(), m_botGuid);
        LOG_INFO("module", "  Current State: %s", GetStateString());
        LOG_INFO("module", "  Previous State: %s", BotBehaviorStateToString(m_previousState));
        LOG_INFO("module", "  Time in State: %u ms", m_stateTimeMs);
        LOG_INFO("module", "  Has Target: %s", m_targetGuid ? "Yes" : "No");
        LOG_INFO("module", "  Has Destination: %s", m_hasDestination ? "Yes" : "No");
        LOG_INFO("module", "  Personality: aggro=%.2f, caution=%.2f, social=%.2f",
            m_personality.aggression, m_personality.caution, m_personality.sociability);
    }

    // =========================================================================
    // ZONE ACTIVITY STATE IMPLEMENTATION
    // =========================================================================

    void ZoneActivityState::Update(uint32 diffMs)
    {
        timeInLevelMs += diffMs;
        RecalculateLevel();
    }

    void ZoneActivityState::OnPlayerEnter()
    {
        playerCount++;
        lastPlayerEnterTime = NowMs();
        RecalculateLevel();
    }

    void ZoneActivityState::OnPlayerLeave()
    {
        if (playerCount > 0)
            playerCount--;
        lastPlayerLeaveTime = NowMs();
        RecalculateLevel();
    }

    void ZoneActivityState::OnCombatEvent()
    {
        recentCombatEvents++;
        RecalculateLevel();
    }

    void ZoneActivityState::OnChatMessage()
    {
        recentChatMessages++;
    }

    void ZoneActivityState::OnDeath()
    {
        recentDeaths++;
        RecalculateLevel();
    }

    void ZoneActivityState::RecalculateLevel()
    {
        ZoneActivityLevel oldLevel = level;

        // Check for active event first
        if (activeEventId > 0)
        {
            level = ZoneActivityLevel::EVENT;
        }
        // Check for PvP activity
        else if (recentCombatEvents > 10 && allianceCount > 0 && hordeCount > 0)
        {
            level = ZoneActivityLevel::CONTESTED;
        }
        // Population-based levels
        else if (playerCount == 0)
        {
            level = ZoneActivityLevel::DORMANT;
        }
        else if (playerCount <= 2)
        {
            level = ZoneActivityLevel::QUIET;
        }
        else if (playerCount <= 10)
        {
            level = ZoneActivityLevel::ACTIVE;
        }
        else
        {
            level = ZoneActivityLevel::BUSY;
        }

        // Reset time if level changed
        if (level != oldLevel)
        {
            timeInLevelMs = 0;
        }
    }

    // =========================================================================
    // ZONE ACTIVITY MANAGER IMPLEMENTATION
    // =========================================================================

    ZoneActivityManager* ZoneActivityManager::Instance()
    {
        static ZoneActivityManager instance;
        return &instance;
    }

    void ZoneActivityManager::Initialize()
    {
        LOG_INFO("module", "AliveWorld: ZoneActivityManager initialized");
    }

    void ZoneActivityManager::Update(uint32 diffMs)
    {
        for (auto& [zoneId, state] : m_zoneStates)
        {
            state->Update(diffMs);
        }
    }

    ZoneActivityState* ZoneActivityManager::GetZoneState(uint32 zoneId)
    {
        auto it = m_zoneStates.find(zoneId);
        return (it != m_zoneStates.end()) ? it->second.get() : nullptr;
    }

    ZoneActivityState* ZoneActivityManager::GetOrCreateZoneState(uint32 zoneId, uint32 mapId)
    {
        auto it = m_zoneStates.find(zoneId);
        if (it != m_zoneStates.end())
        {
            return it->second.get();
        }

        auto state = std::make_unique<ZoneActivityState>();
        state->zoneId = zoneId;
        state->mapId = mapId;

        ZoneActivityState* ptr = state.get();
        m_zoneStates[zoneId] = std::move(state);

        return ptr;
    }

    void ZoneActivityManager::OnPlayerEnterZone(Player* player, uint32 zoneId)
    {
        if (!player)
            return;

        // Get map ID from player
        uint32 mapId = player->GetMapId();
        ZoneActivityState* state = GetOrCreateZoneState(zoneId, mapId);

        state->OnPlayerEnter();

        // Track faction
        if (player->GetTeamId() == TEAM_ALLIANCE)
            state->allianceCount++;
        else
            state->hordeCount++;
    }

    void ZoneActivityManager::OnPlayerLeaveZone(Player* player, uint32 zoneId)
    {
        ZoneActivityState* state = GetZoneState(zoneId);
        if (!state || !player)
            return;

        state->OnPlayerLeave();

        // Track faction
        if (player->GetTeamId() == TEAM_ALLIANCE && state->allianceCount > 0)
            state->allianceCount--;
        else if (state->hordeCount > 0)
            state->hordeCount--;
    }

    void ZoneActivityManager::OnBotEnterZone(uint64 /*botGuid*/, uint32 zoneId)
    {
        ZoneActivityState* state = GetZoneState(zoneId);
        if (state)
        {
            state->botCount++;
        }
    }

    void ZoneActivityManager::OnBotLeaveZone(uint64 /*botGuid*/, uint32 zoneId)
    {
        ZoneActivityState* state = GetZoneState(zoneId);
        if (state && state->botCount > 0)
        {
            state->botCount--;
        }
    }

    void ZoneActivityManager::OnWorldEventStart(uint32 zoneId, uint32 eventId, const std::string& eventName)
    {
        ZoneActivityState* state = GetOrCreateZoneState(zoneId, 0);
        state->activeEventId = eventId;
        state->activeEventName = eventName;
        state->RecalculateLevel();
    }

    void ZoneActivityManager::OnWorldEventEnd(uint32 zoneId, uint32 eventId)
    {
        ZoneActivityState* state = GetZoneState(zoneId);
        if (state && state->activeEventId == eventId)
        {
            state->activeEventId = 0;
            state->activeEventName.clear();
            state->RecalculateLevel();
        }
    }

    std::vector<uint32> ZoneActivityManager::GetActiveZones() const
    {
        std::vector<uint32> result;
        for (const auto& [zoneId, state] : m_zoneStates)
        {
            if (state->IsActive())
            {
                result.push_back(zoneId);
            }
        }
        return result;
    }

    std::vector<uint32> ZoneActivityManager::GetDormantZones() const
    {
        std::vector<uint32> result;
        for (const auto& [zoneId, state] : m_zoneStates)
        {
            if (state->IsDormant())
            {
                result.push_back(zoneId);
            }
        }
        return result;
    }

    uint32 ZoneActivityManager::GetBusiestZone() const
    {
        uint32 busiestZone = 0;
        uint32 maxPop = 0;

        for (const auto& [zoneId, state] : m_zoneStates)
        {
            if (state->GetTotalPopulation() > maxPop)
            {
                maxPop = state->GetTotalPopulation();
                busiestZone = zoneId;
            }
        }

        return busiestZone;
    }

    uint32 ZoneActivityManager::GetTotalActiveZones() const
    {
        uint32 count = 0;
        for (const auto& [zoneId, state] : m_zoneStates)
        {
            if (!state->IsDormant())
            {
                ++count;
            }
        }
        return count;
    }

    uint32 ZoneActivityManager::GetTotalPlayers() const
    {
        uint32 total = 0;
        for (const auto& [zoneId, state] : m_zoneStates)
        {
            total += state->playerCount;
        }
        return total;
    }

    uint32 ZoneActivityManager::GetTotalBots() const
    {
        uint32 total = 0;
        for (const auto& [zoneId, state] : m_zoneStates)
        {
            total += state->botCount;
        }
        return total;
    }

} // namespace AliveWorld
