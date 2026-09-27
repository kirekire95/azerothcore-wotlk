/*
 * mod-alive-world - Bot State Machine to Playerbots Integration
 * Copyright (C) 2025 EC
 */

#include "BotPlayerbotIntegration.h"
#include "Log.h"
#include "Player.h"

#ifdef MOD_PLAYERBOTS
#include "PlayerbotAI.h"
#include "PlayerbotMgr.h"
#endif

namespace AliveWorld
{

// ============================================================================
// SINGLETON
// ============================================================================

BotPlayerbotIntegration* BotPlayerbotIntegration::Instance()
{
    static BotPlayerbotIntegration instance;
    return &instance;
}

BotPlayerbotIntegration::BotPlayerbotIntegration()
{
    // Default profile for unmapped states
    m_defaultProfile.combatStrategies = "";
    m_defaultProfile.nonCombatStrategies = "+rpg";
}

// ============================================================================
// INITIALIZATION
// ============================================================================

void BotPlayerbotIntegration::Initialize()
{
    if (m_initialized)
        return;

    InitializeDefaultProfiles();
    m_initialized = true;

    LOG_INFO("module", "AliveWorld: BotPlayerbotIntegration initialized with %zu state profiles",
        m_stateProfiles.size());
}

void BotPlayerbotIntegration::InitializeDefaultProfiles()
{
    using namespace DefaultStrategies;

    // Map each behavioral state to its strategy profile
    m_stateProfiles[BotBehaviorState::IDLE] = {
        IDLE_COMBAT, IDLE_NONCOMBAT
    };

    m_stateProfiles[BotBehaviorState::TRAVELING] = {
        TRAVELING_COMBAT, TRAVELING_NONCOMBAT
    };

    m_stateProfiles[BotBehaviorState::GRINDING] = {
        GRINDING_COMBAT, GRINDING_NONCOMBAT
    };

    m_stateProfiles[BotBehaviorState::QUESTING] = {
        QUESTING_COMBAT, QUESTING_NONCOMBAT
    };

    m_stateProfiles[BotBehaviorState::SOCIALIZING] = {
        SOCIALIZING_COMBAT, SOCIALIZING_NONCOMBAT
    };

    m_stateProfiles[BotBehaviorState::COMBAT_ACTIVE] = {
        COMBAT_ACTIVE_COMBAT, COMBAT_ACTIVE_NONCOMBAT
    };

    m_stateProfiles[BotBehaviorState::COMBAT_FLEEING] = {
        COMBAT_FLEEING_COMBAT, COMBAT_FLEEING_NONCOMBAT
    };

    m_stateProfiles[BotBehaviorState::COMBAT_SUPPORT] = {
        COMBAT_SUPPORT_COMBAT, COMBAT_SUPPORT_NONCOMBAT
    };

    m_stateProfiles[BotBehaviorState::EVENT_PARTICIPANT] = {
        EVENT_PARTICIPANT_COMBAT, EVENT_PARTICIPANT_NONCOMBAT
    };

    m_stateProfiles[BotBehaviorState::EVENT_LEADER] = {
        EVENT_LEADER_COMBAT, EVENT_LEADER_NONCOMBAT
    };

    m_stateProfiles[BotBehaviorState::EVENT_SPECTATOR] = {
        "", "+stay -travel"  // Spectators just watch
    };

    m_stateProfiles[BotBehaviorState::RESTING] = {
        RESTING_COMBAT, RESTING_NONCOMBAT
    };

    m_stateProfiles[BotBehaviorState::CRAFTING] = {
        CRAFTING_COMBAT, CRAFTING_NONCOMBAT
    };

    m_stateProfiles[BotBehaviorState::TRADING] = {
        TRADING_COMBAT, TRADING_NONCOMBAT
    };

    m_stateProfiles[BotBehaviorState::ROLEPLAYING] = {
        ROLEPLAYING_COMBAT, ROLEPLAYING_NONCOMBAT
    };

    m_stateProfiles[BotBehaviorState::PATROL] = {
        PATROL_COMBAT, PATROL_NONCOMBAT
    };

    m_stateProfiles[BotBehaviorState::GUARD_POST] = {
        GUARD_POST_COMBAT, GUARD_POST_NONCOMBAT
    };

    m_stateProfiles[BotBehaviorState::PURSUIT] = {
        PURSUIT_COMBAT, PURSUIT_NONCOMBAT
    };
}

// ============================================================================
// STRATEGY PROFILE ACCESS
// ============================================================================

const StrategyProfile& BotPlayerbotIntegration::GetStrategyProfile(BotBehaviorState state) const
{
    auto it = m_stateProfiles.find(state);
    if (it != m_stateProfiles.end())
        return it->second;

    return m_defaultProfile;
}

void BotPlayerbotIntegration::SetStrategyProfile(BotBehaviorState state, const StrategyProfile& profile)
{
    m_stateProfiles[state] = profile;
}

// ============================================================================
// APPLY STATE TO BOT
// ============================================================================

void BotPlayerbotIntegration::ApplyStateToBot(Player* bot, BotBehaviorState state)
{
    if (!bot)
        return;

#ifdef MOD_PLAYERBOTS
    PlayerbotAI* ai = GetPlayerbotAI(bot);
    if (!ai)
    {
        // Not a playerbot, nothing to do
        return;
    }

    const StrategyProfile& profile = GetStrategyProfile(state);
    ApplyStrategies(ai, profile);

    LOG_DEBUG("module", "AliveWorld: Applied state '%s' strategies to bot %s",
        BotStateToPlayerbotStateStr(state), bot->GetName().c_str());
#else
    // Playerbots module not available
    (void)state;  // Suppress unused warning
#endif
}

void BotPlayerbotIntegration::ApplyStateMachineToBot(Player* bot, const BotStateMachine& stateMachine)
{
    ApplyStateToBot(bot, stateMachine.GetState());
}

void BotPlayerbotIntegration::ApplyStrategies(PlayerbotAI* ai, const StrategyProfile& profile)
{
#ifdef MOD_PLAYERBOTS
    if (!ai)
        return;

    // Apply combat strategies
    if (!profile.combatStrategies.empty())
    {
        ai->ChangeStrategy(profile.combatStrategies, BOT_STATE_COMBAT);
    }

    // Apply non-combat strategies
    if (!profile.nonCombatStrategies.empty())
    {
        ai->ChangeStrategy(profile.nonCombatStrategies, BOT_STATE_NON_COMBAT);
    }
#else
    (void)ai;
    (void)profile;
#endif
}

// ============================================================================
// CALLBACK REGISTRATION
// ============================================================================

void BotPlayerbotIntegration::RegisterBotStateMachineCallback(BotStateMachine* stateMachine, Player* bot)
{
    if (!stateMachine || !bot)
        return;

#ifdef MOD_PLAYERBOTS
    // Capture bot's GUID for the callback (safer than raw pointer)
    ObjectGuid botGuid = bot->GetGUID();

    // Set callback to apply strategy changes when state changes
    stateMachine->SetStateChangeCallback(
        [botGuid](uint64 /*botGuidLow*/, BotBehaviorState oldState, BotBehaviorState newState)
        {
            // Find the player by GUID
            Player* player = ObjectAccessor::FindPlayer(botGuid);
            if (!player)
                return;

            LOG_DEBUG("module", "AliveWorld: Bot %s state changed: %s -> %s",
                player->GetName().c_str(),
                BotBehaviorStateToString(oldState),
                BotBehaviorStateToString(newState));

            // Apply the new state's strategies
            sBotPlayerbotIntegration->ApplyStateToBot(player, newState);
        });

    LOG_DEBUG("module", "AliveWorld: Registered playerbots callback for bot %s",
        bot->GetName().c_str());
#else
    (void)stateMachine;
    (void)bot;
#endif
}

// ============================================================================
// STATIC HELPERS
// ============================================================================

bool BotPlayerbotIntegration::HasPlayerbotAI(Player* player)
{
#ifdef MOD_PLAYERBOTS
    return player && sPlayerbotsMgr.GetPlayerbotAI(player) != nullptr;
#else
    (void)player;
    return false;
#endif
}

PlayerbotAI* BotPlayerbotIntegration::GetPlayerbotAI(Player* player)
{
#ifdef MOD_PLAYERBOTS
    return player ? sPlayerbotsMgr.GetPlayerbotAI(player) : nullptr;
#else
    (void)player;
    return nullptr;
#endif
}

} // namespace AliveWorld
