/*
 * EC Bot Behavior Controller - Whisper Command Actions
 * Copyright (C) 2025 EC
 *
 * Actions triggered when a player whispers stance commands to a party bot:
 *   "passive"        -> SetBehaviorStancePassiveAction
 *   "defensive"      -> SetBehaviorStanceDefensiveAction
 *   "aggressive"     -> SetBehaviorStanceAggressiveAction
 *   "attack neutrals"-> ToggleAttackNeutralsAction
 *   "debug thoughts" -> ToggleDebugThoughtsAction
 */

#ifndef SET_BEHAVIOR_STANCE_ACTIONS_H
#define SET_BEHAVIOR_STANCE_ACTIONS_H

#ifdef MOD_ALIVE_WORLD

#include "Action.h"
#include "BotBehaviorController.h"
#include "Playerbots.h"

// =========================================================================
// BASE CLASS
// =========================================================================

class SetBehaviorStanceActionBase : public Action
{
public:
    SetBehaviorStanceActionBase(PlayerbotAI* botAI, std::string const name)
        : Action(botAI, name) {}

protected:
    void ReportStance(AliveWorld::BotStance stance)
    {
        std::string stanceName = AliveWorld::BotStanceToString(stance);
        botAI->TellMaster("Behavior stance set to: " + stanceName);
    }

    void ReportToggle(const std::string& feature, bool enabled)
    {
        botAI->TellMaster(feature + (enabled ? " enabled." : " disabled."));
    }
};

// =========================================================================
// STANCE ACTIONS
// =========================================================================

class SetBehaviorStancePassiveAction : public SetBehaviorStanceActionBase
{
public:
    SetBehaviorStancePassiveAction(PlayerbotAI* botAI)
        : SetBehaviorStanceActionBase(botAI, "set stance passive") {}

    bool Execute(Event /* event */) override
    {
        uint64 guid = bot->GetGUID().GetCounter();
        sBotBehaviorController->SetStance(guid, AliveWorld::BotStance::PASSIVE, "whisper");
        ReportStance(AliveWorld::BotStance::PASSIVE);
        sBotBehaviorController->BroadcastThought(bot, "Entering passive mode. I won't attack anything.", "stance_change");
        return true;
    }
};

class SetBehaviorStanceDefensiveAction : public SetBehaviorStanceActionBase
{
public:
    SetBehaviorStanceDefensiveAction(PlayerbotAI* botAI)
        : SetBehaviorStanceActionBase(botAI, "set stance defensive") {}

    bool Execute(Event /* event */) override
    {
        uint64 guid = bot->GetGUID().GetCounter();
        sBotBehaviorController->SetStance(guid, AliveWorld::BotStance::DEFENSIVE, "whisper");
        ReportStance(AliveWorld::BotStance::DEFENSIVE);
        sBotBehaviorController->BroadcastThought(bot, "Switching to defensive. I'll fight back if we're attacked.", "stance_change");
        return true;
    }
};

class SetBehaviorStanceAggressiveAction : public SetBehaviorStanceActionBase
{
public:
    SetBehaviorStanceAggressiveAction(PlayerbotAI* botAI)
        : SetBehaviorStanceActionBase(botAI, "set stance aggressive") {}

    bool Execute(Event /* event */) override
    {
        uint64 guid = bot->GetGUID().GetCounter();
        sBotBehaviorController->SetStance(guid, AliveWorld::BotStance::AGGRESSIVE, "whisper");
        ReportStance(AliveWorld::BotStance::AGGRESSIVE);
        sBotBehaviorController->BroadcastThought(bot, "Going aggressive! I'll attack anything hostile in range.", "stance_change");
        return true;
    }
};

// =========================================================================
// TOGGLE ACTIONS
// =========================================================================

class ToggleAttackNeutralsAction : public SetBehaviorStanceActionBase
{
public:
    ToggleAttackNeutralsAction(PlayerbotAI* botAI)
        : SetBehaviorStanceActionBase(botAI, "toggle attack neutrals") {}

    bool Execute(Event /* event */) override
    {
        uint64 guid = bot->GetGUID().GetCounter();
        auto profile = sBotBehaviorController->GetProfile(guid);
        profile.attackNeutrals = !profile.attackNeutrals;
        sBotBehaviorController->SetProfile(guid, profile, "whisper");
        ReportToggle("Attack neutrals", profile.attackNeutrals);

        if (profile.attackNeutrals)
            sBotBehaviorController->BroadcastThought(bot, "I'll now attack neutral targets too.", "stance_change");
        else
            sBotBehaviorController->BroadcastThought(bot, "I'll leave neutral targets alone.", "stance_change");

        return true;
    }
};

class ToggleDebugThoughtsAction : public SetBehaviorStanceActionBase
{
public:
    ToggleDebugThoughtsAction(PlayerbotAI* botAI)
        : SetBehaviorStanceActionBase(botAI, "toggle debug thoughts") {}

    bool Execute(Event /* event */) override
    {
        uint64 guid = bot->GetGUID().GetCounter();
        bool current = sBotBehaviorController->HasDebugThoughts(guid);
        sBotBehaviorController->SetDebugThoughts(guid, !current);
        ReportToggle("Debug thoughts", !current);
        return true;
    }
};

#endif // MOD_ALIVE_WORLD
#endif // SET_BEHAVIOR_STANCE_ACTIONS_H
