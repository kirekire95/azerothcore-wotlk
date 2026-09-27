/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "PassiveMultiplier.h"
#include "Action.h"

// EC Bot Behavior Controller integration
#ifdef MOD_ALIVE_WORLD
#include "BotBehaviorController.h"
#include "Playerbots.h"
#endif

std::vector<std::string> PassiveMultiplier::allowedActions;
std::vector<std::string> PassiveMultiplier::allowedParts;

PassiveMultiplier::PassiveMultiplier(PlayerbotAI* botAI) : Multiplier(botAI, "passive")
{
    if (allowedActions.empty())
    {
        allowedActions.push_back("co");
        allowedActions.push_back("nc");
        allowedActions.push_back("reset botAI");
        allowedActions.push_back("check mount state");
        allowedActions.push_back("lfg");
    }

    if (allowedParts.empty())
    {
        allowedParts.push_back("follow");
        allowedParts.push_back("move from group");
        allowedParts.push_back("stay");
        allowedParts.push_back("chat shortcut");
    }
}

float PassiveMultiplier::GetValue(Action* action)
{
    if (!action)
        return 1.0f;

    std::string const name = action->getName();

#ifdef MOD_ALIVE_WORLD
    // EC Bot Behavior Controller: if the bot has a non-passive stance from the
    // behavior controller, allow combat actions even when the playerbots "passive"
    // strategy is active. This lets the behavior controller override playerbots' passive mode.
    if (Player* bot = botAI->GetBot())
    {
        uint64 botGuid = bot->GetGUID().GetCounter();
        if (sBotBehaviorController->HasProfile(botGuid))
        {
            auto stance = sBotBehaviorController->GetStance(botGuid);
            if (stance != AliveWorld::BotStance::PASSIVE)
            {
                // Allow attack-related actions for defensive/aggressive stances
                if (name == "attack my target" ||
                    name == "attack anything" ||
                    name.find("dps assist") != std::string::npos ||
                    name.find("tank assist") != std::string::npos ||
                    name.find("attack") != std::string::npos ||
                    name == "melee" ||
                    name == "reach melee" ||
                    name == "reach spell")
                {
                    return 1.0f;
                }
            }
        }
    }
#endif

    for (std::vector<std::string>::iterator i = allowedActions.begin(); i != allowedActions.end(); i++)
    {
        if (name == *i)
            return 1.0f;
    }

    for (std::vector<std::string>::iterator i = allowedParts.begin(); i != allowedParts.end(); i++)
    {
        if (name.find(*i) != std::string::npos)
            return 1.0f;
    }

    return 0;
}
