/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>
 * Released under GNU AGPL v3 license: https://github.com/azerothcore/azerothcore-wotlk/blob/master/LICENSE-AGPL3
 *
 * EC Bot Goal System - Goal Triggers Implementation
 */

#include "GoalTriggers.h"
#include "../../Mgr/Goal/GoalMgr.h"
#include "../../Mgr/Goal/GoalRepository.h"
#include "Playerbots.h"
#include "Player.h"

bool NoActiveGoalTrigger::IsActive()
{
    if (!sGoalMgr->IsEnabled())
        return false;

    Player* bot = botAI->GetBot();
    if (!bot)
        return false;

    uint32_t botGuid = bot->GetGUID().GetCounter();

    // Don't fire if we recently failed to find any viable goals (5 min backoff)
    if (sGoalMgr->IsInGoalEvalBackoff(botGuid))
        return false;

    // Fire if bot has no active goals
    return sGoalRepository->GetActiveGoalCount(botGuid) == 0;
}

bool GoalCompletedTrigger::IsActive()
{
    if (!sGoalMgr->IsEnabled())
        return false;

    Player* bot = botAI->GetBot();
    if (!bot)
        return false;

    auto activeGoal = sGoalMgr->GetActiveGoal(bot);
    if (!activeGoal.has_value())
        return false;

    // Fire if current goal is complete
    return activeGoal->IsComplete();
}

bool GoalProgressTrigger::IsActive()
{
    if (!sGoalMgr->IsEnabled())
        return false;

    Player* bot = botAI->GetBot();
    if (!bot)
        return false;

    auto activeGoal = sGoalMgr->GetActiveGoal(bot);
    if (!activeGoal.has_value())
    {
        // Reset tracking if no goal
        m_lastProgress = 0;
        m_lastGoalId = 0;
        return false;
    }

    // Check if this is a different goal or progress changed
    bool progressChanged = false;

    if (activeGoal->id != m_lastGoalId)
    {
        // New goal, update tracking
        m_lastGoalId = activeGoal->id;
        m_lastProgress = activeGoal->progress;
        progressChanged = true; // Consider new goal as progress
    }
    else if (activeGoal->progress > m_lastProgress)
    {
        // Same goal, progress increased
        m_lastProgress = activeGoal->progress;
        progressChanged = true;
    }

    return progressChanged;
}

bool GoalTargetNearbyTrigger::IsActive()
{
    if (!sGoalMgr->IsEnabled())
        return false;

    Player* bot = botAI->GetBot();
    if (!bot)
        return false;

    auto activeGoal = sGoalMgr->GetActiveGoal(bot);
    if (!activeGoal.has_value())
        return false;

    // Check proximity based on goal type
    switch (activeGoal->type)
    {
        case GoalType::ITEM:
        {
            // Check if any creature that drops this item is nearby
            // This would require querying creature_loot_template
            // For now, simplified: check if in combat with potential dropper
            // TODO: Implement proper loot source detection
            return false;
        }

        case GoalType::REPUTATION:
        {
            // Check if faction NPCs are nearby
            // Could query for NPCs with this faction
            // TODO: Implement faction NPC detection
            return false;
        }

        case GoalType::EXPLORE:
        {
            // Check if we're near or in the target zone
            uint32_t currentZone = bot->GetZoneId();
            uint32_t currentArea = bot->GetAreaId();

            // Simple check: are we in the target zone?
            if (currentZone == activeGoal->targetId || currentArea == activeGoal->targetId)
                return true;

            // TODO: Check if we're near the zone boundary
            return false;
        }

        case GoalType::GOLD:
        {
            // Gold targets aren't "nearby" in the traditional sense
            // Could trigger when near vendor/AH if bot wants to sell
            return false;
        }

        case GoalType::PROFESSION:
        {
            // Check if trainer or materials are nearby
            // TODO: Implement trainer/node detection
            return false;
        }

        default:
            return false;
    }
}

bool NeedsNewGoalTrigger::IsActive()
{
    if (!sGoalMgr->IsEnabled())
        return false;

    Player* bot = botAI->GetBot();
    if (!bot)
        return false;

    uint32_t botGuid = bot->GetGUID().GetCounter();

    // Don't fire if we recently failed to find any viable goals (5 min backoff)
    if (sGoalMgr->IsInGoalEvalBackoff(botGuid))
        return false;

    uint32_t activeCount = sGoalRepository->GetActiveGoalCount(botGuid);
    uint8_t maxGoals = sGoalMgr->GetMaxActiveGoals();

    // Fire if we have room for more goals
    return activeCount < maxGoals;
}

bool GoalRefreshTrigger::IsActive()
{
    if (!sGoalMgr->IsEnabled())
        return false;

    Player* bot = botAI->GetBot();
    if (!bot)
        return false;

    // Always fire on check interval - the action will handle actual refresh
    // This trigger just ensures periodic re-evaluation
    uint32_t botGuid = bot->GetGUID().GetCounter();
    return sGoalRepository->GetActiveGoalCount(botGuid) > 0;
}

bool ItemGoalTrigger::IsActive()
{
    if (!sGoalMgr->IsEnabled())
        return false;

    Player* bot = botAI->GetBot();
    if (!bot)
        return false;

    auto activeGoal = sGoalMgr->GetActiveGoal(bot);
    if (!activeGoal.has_value())
        return false;

    return activeGoal->type == GoalType::ITEM;
}

bool ReputationGoalTrigger::IsActive()
{
    if (!sGoalMgr->IsEnabled())
        return false;

    Player* bot = botAI->GetBot();
    if (!bot)
        return false;

    auto activeGoal = sGoalMgr->GetActiveGoal(bot);
    if (!activeGoal.has_value())
        return false;

    return activeGoal->type == GoalType::REPUTATION;
}

bool ExploreGoalTrigger::IsActive()
{
    if (!sGoalMgr->IsEnabled())
        return false;

    Player* bot = botAI->GetBot();
    if (!bot)
        return false;

    auto activeGoal = sGoalMgr->GetActiveGoal(bot);
    if (!activeGoal.has_value())
        return false;

    return activeGoal->type == GoalType::EXPLORE;
}

bool GoldGoalTrigger::IsActive()
{
    if (!sGoalMgr->IsEnabled())
        return false;

    Player* bot = botAI->GetBot();
    if (!bot)
        return false;

    auto activeGoal = sGoalMgr->GetActiveGoal(bot);
    if (!activeGoal.has_value())
        return false;

    return activeGoal->type == GoalType::GOLD;
}

bool ProfessionGoalTrigger::IsActive()
{
    if (!sGoalMgr->IsEnabled())
        return false;

    Player* bot = botAI->GetBot();
    if (!bot)
        return false;

    auto activeGoal = sGoalMgr->GetActiveGoal(bot);
    if (!activeGoal.has_value())
        return false;

    return activeGoal->type == GoalType::PROFESSION;
}
