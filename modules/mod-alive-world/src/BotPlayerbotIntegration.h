/*
 * mod-alive-world - Bot State Machine to Playerbots Integration
 * Copyright (C) 2025 EC
 *
 * Bridges the high-level BotStateMachine (behavioral state: "what am I doing")
 * with the low-level playerbots AI (combat rotations, movement, spell casts).
 *
 * BotStateMachine states map to playerbots strategy combinations:
 *   IDLE        -> rpg, stay
 *   TRAVELING   -> travel, follow (if grouped)
 *   GRINDING    -> grind, combat
 *   QUESTING    -> quest, combat
 *   SOCIALIZING -> rpg, emote, stay
 *   COMBAT_*    -> combat, tank/dps/heal based on spec
 *   EVENT_*     -> raid, combat
 *   RESTING     -> rpg, food, stay
 *   CRAFTING    -> stay
 *   TRADING     -> stay, vendor
 *   PATROL      -> travel, guard
 *   GUARD_POST  -> stay, guard
 *   PURSUIT     -> chase, combat
 */

#ifndef BOT_PLAYERBOT_INTEGRATION_H
#define BOT_PLAYERBOT_INTEGRATION_H

#include "BotStateMachine.h"
#include <functional>
#include <map>
#include <string>

class Player;
class PlayerbotAI;

namespace AliveWorld
{

// ============================================================================
// STRATEGY PROFILE
// ============================================================================

// A strategy profile defines what playerbots strategies to enable/disable
struct StrategyProfile
{
    std::string combatStrategies;      // Strategies for BOT_STATE_COMBAT
    std::string nonCombatStrategies;   // Strategies for BOT_STATE_NON_COMBAT

    // Strategy modifiers (prefix with + to add, - to remove)
    // Example: "+rpg +stay -grind"
};

// ============================================================================
// INTEGRATION MANAGER
// ============================================================================

class BotPlayerbotIntegration
{
public:
    static BotPlayerbotIntegration* Instance();

    // Initialize strategy mappings
    void Initialize();

    // Get strategy profile for a behavioral state
    const StrategyProfile& GetStrategyProfile(BotBehaviorState state) const;

    // Apply a behavioral state to a bot's playerbots AI
    // This changes the active strategies based on the state
    void ApplyStateToBot(Player* bot, BotBehaviorState state);

    // Convenience: Apply state machine's current state
    void ApplyStateMachineToBot(Player* bot, const BotStateMachine& stateMachine);

    // Hook into state machine callbacks
    // Call this when registering a bot with AliveWorld
    void RegisterBotStateMachineCallback(BotStateMachine* stateMachine, Player* bot);

    // =========================================================================
    // STRATEGY MAPPINGS
    // =========================================================================

    // Get/set strategy mappings for customization
    void SetStrategyProfile(BotBehaviorState state, const StrategyProfile& profile);

    // Check if a bot has playerbots AI
    static bool HasPlayerbotAI(Player* player);

    // Get playerbots AI for a bot (may be null)
    static PlayerbotAI* GetPlayerbotAI(Player* player);

private:
    BotPlayerbotIntegration();
    ~BotPlayerbotIntegration() = default;

    // Default strategy profiles per behavioral state
    void InitializeDefaultProfiles();

    // Apply strategy changes to playerbots engine
    void ApplyStrategies(PlayerbotAI* ai, const StrategyProfile& profile);

    // State -> Strategy profile mapping
    std::map<BotBehaviorState, StrategyProfile> m_stateProfiles;

    // Default profile for unmapped states
    StrategyProfile m_defaultProfile;

    bool m_initialized = false;
};

#define sBotPlayerbotIntegration AliveWorld::BotPlayerbotIntegration::Instance()

// ============================================================================
// DEFAULT STRATEGY PROFILES
// ============================================================================

// These define the default strategy combinations for each behavioral state.
// Can be overridden via config or at runtime.

namespace DefaultStrategies
{
    // IDLE: Bot is standing around, may respond to nearby activity
    constexpr const char* IDLE_COMBAT = "";
    constexpr const char* IDLE_NONCOMBAT = "+rpg +stay -grind -travel";

    // TRAVELING: Bot is moving between locations
    constexpr const char* TRAVELING_COMBAT = "";
    constexpr const char* TRAVELING_NONCOMBAT = "+travel -stay -rpg";

    // GRINDING: Bot is farming mobs
    constexpr const char* GRINDING_COMBAT = "+dps";
    constexpr const char* GRINDING_NONCOMBAT = "+grind -stay -rpg";

    // QUESTING: Bot is following quest objectives
    constexpr const char* QUESTING_COMBAT = "+dps";
    constexpr const char* QUESTING_NONCOMBAT = "+quest -stay";

    // SOCIALIZING: Bot is interacting with others
    constexpr const char* SOCIALIZING_COMBAT = "";
    constexpr const char* SOCIALIZING_NONCOMBAT = "+rpg +emote +stay -grind";

    // COMBAT_ACTIVE: Bot is in active combat
    constexpr const char* COMBAT_ACTIVE_COMBAT = "+threat";
    constexpr const char* COMBAT_ACTIVE_NONCOMBAT = "";

    // COMBAT_FLEEING: Bot is running away
    constexpr const char* COMBAT_FLEEING_COMBAT = "+flee";
    constexpr const char* COMBAT_FLEEING_NONCOMBAT = "";

    // COMBAT_SUPPORT: Bot is supporting allies
    constexpr const char* COMBAT_SUPPORT_COMBAT = "+assist";
    constexpr const char* COMBAT_SUPPORT_NONCOMBAT = "";

    // EVENT_PARTICIPANT: In a world event
    constexpr const char* EVENT_PARTICIPANT_COMBAT = "+raid";
    constexpr const char* EVENT_PARTICIPANT_NONCOMBAT = "+group -stay";

    // EVENT_LEADER: Leading an event
    constexpr const char* EVENT_LEADER_COMBAT = "+raid +tank assist";
    constexpr const char* EVENT_LEADER_NONCOMBAT = "+group +rti -stay";

    // RESTING: In inn/city resting
    constexpr const char* RESTING_COMBAT = "";
    constexpr const char* RESTING_NONCOMBAT = "+rpg +food +stay -travel";

    // CRAFTING: Using professions
    constexpr const char* CRAFTING_COMBAT = "";
    constexpr const char* CRAFTING_NONCOMBAT = "+stay -travel -rpg";

    // TRADING: At vendor/AH
    constexpr const char* TRADING_COMBAT = "";
    constexpr const char* TRADING_NONCOMBAT = "+stay -travel";

    // ROLEPLAYING: RP interaction mode
    constexpr const char* ROLEPLAYING_COMBAT = "";
    constexpr const char* ROLEPLAYING_NONCOMBAT = "+rpg +emote +stay -grind -travel";

    // PATROL: Walking a patrol route
    constexpr const char* PATROL_COMBAT = "+guard";
    constexpr const char* PATROL_NONCOMBAT = "+travel +guard -stay -rpg";

    // GUARD_POST: Standing guard
    constexpr const char* GUARD_POST_COMBAT = "+guard";
    constexpr const char* GUARD_POST_NONCOMBAT = "+stay +guard -travel -rpg";

    // PURSUIT: Chasing a target
    constexpr const char* PURSUIT_COMBAT = "+chase";
    constexpr const char* PURSUIT_NONCOMBAT = "+follow +chase -stay";
}

// ============================================================================
// HELPER: BOT STATE TO STRING FOR LOGGING
// ============================================================================

inline const char* BotStateToPlayerbotStateStr(BotBehaviorState state)
{
    switch (state)
    {
        case BotBehaviorState::IDLE:            return "idle";
        case BotBehaviorState::TRAVELING:       return "traveling";
        case BotBehaviorState::GRINDING:        return "grinding";
        case BotBehaviorState::QUESTING:        return "questing";
        case BotBehaviorState::SOCIALIZING:     return "socializing";
        case BotBehaviorState::COMBAT_ACTIVE:   return "combat";
        case BotBehaviorState::COMBAT_FLEEING:  return "fleeing";
        case BotBehaviorState::COMBAT_SUPPORT:  return "support";
        case BotBehaviorState::EVENT_PARTICIPANT: return "event";
        case BotBehaviorState::EVENT_LEADER:    return "event_leader";
        case BotBehaviorState::EVENT_SPECTATOR: return "spectator";
        case BotBehaviorState::RESTING:         return "resting";
        case BotBehaviorState::CRAFTING:        return "crafting";
        case BotBehaviorState::TRADING:         return "trading";
        case BotBehaviorState::ROLEPLAYING:     return "roleplaying";
        case BotBehaviorState::PATROL:          return "patrol";
        case BotBehaviorState::GUARD_POST:      return "guard";
        case BotBehaviorState::PURSUIT:         return "pursuit";
        default: return "unknown";
    }
}

} // namespace AliveWorld

#endif // BOT_PLAYERBOT_INTEGRATION_H
