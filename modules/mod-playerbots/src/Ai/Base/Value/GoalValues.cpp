/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>
 * Released under GNU AGPL v3 license: https://github.com/azerothcore/azerothcore-wotlk/blob/master/LICENSE-AGPL3
 *
 * EC Bot Goal System - Goal Values Implementation
 */

#include "GoalValues.h"
#include "../../Mgr/Goal/GoalMgr.h"
#include "../../Mgr/Goal/GoalRepository.h"
#include "Playerbots.h"
#include "Player.h"

std::optional<Goal> CurrentGoalValue::Calculate()
{
    if (!sGoalMgr->IsEnabled())
        return std::nullopt;

    Player* bot = botAI->GetBot();
    if (!bot)
        return std::nullopt;

    return sGoalMgr->GetActiveGoal(bot);
}

std::vector<Goal> ActiveGoalsValue::Calculate()
{
    if (!sGoalMgr->IsEnabled())
        return {};

    Player* bot = botAI->GetBot();
    if (!bot)
        return {};

    return sGoalMgr->GetActiveGoals(bot);
}

std::vector<Goal> AvailableGoalsValue::Calculate()
{
    if (!sGoalMgr->IsEnabled())
        return {};

    Player* bot = botAI->GetBot();
    if (!bot)
        return {};

    uint32_t botGuid = bot->GetGUID().GetCounter();

    // Get pending goals from repository
    return sGoalRepository->LoadGoalsByStatus(botGuid, GoalStatus::PENDING);
}

float GoalProgressValue::Calculate()
{
    if (!sGoalMgr->IsEnabled())
        return 0.0f;

    Player* bot = botAI->GetBot();
    if (!bot)
        return 0.0f;

    auto activeGoal = sGoalMgr->GetActiveGoal(bot);
    if (!activeGoal.has_value())
        return 0.0f;

    return activeGoal->GetProgressPercent();
}

bool HasActiveGoalValue::Calculate()
{
    if (!sGoalMgr->IsEnabled())
        return false;

    Player* bot = botAI->GetBot();
    if (!bot)
        return false;

    uint32_t botGuid = bot->GetGUID().GetCounter();
    return sGoalRepository->GetActiveGoalCount(botGuid) > 0;
}

uint8_t GoalCountValue::Calculate()
{
    if (!sGoalMgr->IsEnabled())
        return 0;

    Player* bot = botAI->GetBot();
    if (!bot)
        return 0;

    uint32_t botGuid = bot->GetGUID().GetCounter();
    return static_cast<uint8_t>(sGoalRepository->GetActiveGoalCount(botGuid));
}

GoalType GoalTypeValue::Calculate()
{
    if (!sGoalMgr->IsEnabled())
        return GoalType::MAX_GOAL_TYPE;

    Player* bot = botAI->GetBot();
    if (!bot)
        return GoalType::MAX_GOAL_TYPE;

    auto activeGoal = sGoalMgr->GetActiveGoal(bot);
    if (!activeGoal.has_value())
        return GoalType::MAX_GOAL_TYPE;

    return activeGoal->type;
}

uint32_t GoalTargetIdValue::Calculate()
{
    if (!sGoalMgr->IsEnabled())
        return 0;

    Player* bot = botAI->GetBot();
    if (!bot)
        return 0;

    auto activeGoal = sGoalMgr->GetActiveGoal(bot);
    if (!activeGoal.has_value())
        return 0;

    return activeGoal->targetId;
}

std::string GoalTargetNameValue::Calculate()
{
    if (!sGoalMgr->IsEnabled())
        return "";

    Player* bot = botAI->GetBot();
    if (!bot)
        return "";

    auto activeGoal = sGoalMgr->GetActiveGoal(bot);
    if (!activeGoal.has_value())
        return "";

    // If targetName is already populated, return it
    if (!activeGoal->targetName.empty())
        return activeGoal->targetName;

    // Otherwise resolve from game data
    switch (activeGoal->type)
    {
        case GoalType::ITEM:
            return sGoalMgr->GetItemName(activeGoal->targetId);
        case GoalType::REPUTATION:
            return sGoalMgr->GetFactionName(activeGoal->targetId);
        case GoalType::EXPLORE:
            return sGoalMgr->GetZoneName(activeGoal->targetId);
        case GoalType::PROFESSION:
            return sGoalMgr->GetSkillName(activeGoal->targetId);
        case GoalType::GOLD:
            return "Gold Target";
        case GoalType::ACHIEVEMENT:
            return "Achievement";
        case GoalType::MAX_GOAL_TYPE:
        default:
            return "Unknown";
    }
}
