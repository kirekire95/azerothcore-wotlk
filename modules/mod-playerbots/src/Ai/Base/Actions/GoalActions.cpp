/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>
 * Released under GNU AGPL v3 license: https://github.com/azerothcore/azerothcore-wotlk/blob/master/LICENSE-AGPL3
 *
 * EC Bot Goal System - Goal Actions Implementation
 */

#include "GoalActions.h"
#include "../../Mgr/Goal/GoalMgr.h"
#include "../../Mgr/Goal/GoalRepository.h"
#include "PlayerbotAI.h"
#include "Player.h"
#include "ChatHelper.h"
#include "Log.h"
#include <sstream>

// ============================================================================
// SelectGoalAction
// ============================================================================

bool SelectGoalAction::Execute(Event event)
{
    Player* bot = botAI->GetBot();
    if (!bot)
        return false;

    // Let GoalMgr handle goal selection
    sGoalMgr->SelectGoalsForBot(bot, sGoalMgr->GetMaxActiveGoals());

    LOG_DEBUG("playerbots", "GoalActions: Bot {} selected new goals",
        bot->GetGUID().GetCounter());

    return true;
}

bool SelectGoalAction::isPossible()
{
    return sGoalMgr->IsEnabled() && botAI->GetBot() != nullptr;
}

bool SelectGoalAction::isUseful()
{
    if (!sGoalMgr->IsEnabled())
        return false;

    Player* bot = botAI->GetBot();
    if (!bot)
        return false;

    uint32_t botGuid = bot->GetGUID().GetCounter();
    uint32_t activeCount = sGoalRepository->GetActiveGoalCount(botGuid);

    // Useful if we have room for more goals
    return activeCount < sGoalMgr->GetMaxActiveGoals();
}

// ============================================================================
// RefreshGoalsAction
// ============================================================================

bool RefreshGoalsAction::Execute(Event event)
{
    Player* bot = botAI->GetBot();
    if (!bot)
        return false;

    sGoalMgr->RefreshGoals(bot);

    LOG_DEBUG("playerbots", "GoalActions: Bot {} refreshed goals",
        bot->GetGUID().GetCounter());

    return true;
}

bool RefreshGoalsAction::isPossible()
{
    return sGoalMgr->IsEnabled() && botAI->GetBot() != nullptr;
}

bool RefreshGoalsAction::isUseful()
{
    if (!sGoalMgr->IsEnabled())
        return false;

    Player* bot = botAI->GetBot();
    if (!bot)
        return false;

    uint32_t botGuid = bot->GetGUID().GetCounter();

    // Useful if we have active goals to refresh
    return sGoalRepository->GetActiveGoalCount(botGuid) > 0;
}

// ============================================================================
// CompleteGoalAction
// ============================================================================

bool CompleteGoalAction::Execute(Event event)
{
    Player* bot = botAI->GetBot();
    if (!bot)
        return false;

    auto activeGoal = sGoalMgr->GetActiveGoal(bot);
    if (!activeGoal.has_value() || !activeGoal->IsComplete())
        return false;

    // Mark as completed
    sGoalMgr->CompleteGoal(activeGoal->id);

    // Log completion
    LOG_INFO("playerbots", "GoalActions: Bot {} completed goal: {} - {}",
        bot->GetGUID().GetCounter(),
        GoalTypeToString(activeGoal->type),
        activeGoal->targetName);

    // Optional: Emote or say something
    // botAI->DoSpecificAction("emote cheer");

    return true;
}

bool CompleteGoalAction::isPossible()
{
    return sGoalMgr->IsEnabled() && botAI->GetBot() != nullptr;
}

bool CompleteGoalAction::isUseful()
{
    if (!sGoalMgr->IsEnabled())
        return false;

    Player* bot = botAI->GetBot();
    if (!bot)
        return false;

    auto activeGoal = sGoalMgr->GetActiveGoal(bot);
    return activeGoal.has_value() && activeGoal->IsComplete();
}

// ============================================================================
// AbandonGoalAction
// ============================================================================

bool AbandonGoalAction::Execute(Event event)
{
    Player* bot = botAI->GetBot();
    if (!bot)
        return false;

    auto activeGoal = sGoalMgr->GetActiveGoal(bot);
    if (!activeGoal.has_value())
        return false;

    sGoalMgr->AbandonGoal(activeGoal->id);

    LOG_DEBUG("playerbots", "GoalActions: Bot {} abandoned goal: {}",
        bot->GetGUID().GetCounter(), activeGoal->targetName);

    return true;
}

bool AbandonGoalAction::isPossible()
{
    return sGoalMgr->IsEnabled() && botAI->GetBot() != nullptr;
}

bool AbandonGoalAction::isUseful()
{
    if (!sGoalMgr->IsEnabled())
        return false;

    Player* bot = botAI->GetBot();
    if (!bot)
        return false;

    return sGoalMgr->GetActiveGoal(bot).has_value();
}

// ============================================================================
// PursueGoalAction
// ============================================================================

bool PursueGoalAction::Execute(Event event)
{
    Player* bot = botAI->GetBot();
    if (!bot)
        return false;

    auto activeGoal = sGoalMgr->GetActiveGoal(bot);
    if (!activeGoal.has_value())
        return false;

    // Delegate to type-specific action
    switch (activeGoal->type)
    {
        case GoalType::ITEM:
            return botAI->DoSpecificAction("pursue item goal");

        case GoalType::REPUTATION:
            return botAI->DoSpecificAction("pursue reputation goal");

        case GoalType::EXPLORE:
            return botAI->DoSpecificAction("pursue explore goal");

        case GoalType::GOLD:
            return botAI->DoSpecificAction("pursue gold goal");

        case GoalType::PROFESSION:
            return botAI->DoSpecificAction("pursue profession goal");

        default:
            return false;
    }
}

bool PursueGoalAction::isPossible()
{
    return sGoalMgr->IsEnabled() && botAI->GetBot() != nullptr;
}

bool PursueGoalAction::isUseful()
{
    if (!sGoalMgr->IsEnabled())
        return false;

    Player* bot = botAI->GetBot();
    if (!bot)
        return false;

    auto activeGoal = sGoalMgr->GetActiveGoal(bot);
    return activeGoal.has_value() && !activeGoal->IsComplete();
}

// ============================================================================
// PursueItemGoalAction
// ============================================================================

bool PursueItemGoalAction::Execute(Event event)
{
    Player* bot = botAI->GetBot();
    if (!bot)
        return false;

    auto activeGoal = sGoalMgr->GetActiveGoal(bot);
    if (!activeGoal.has_value() || activeGoal->type != GoalType::ITEM)
        return false;

    uint32_t itemId = activeGoal->targetId;

    // Strategy 1: Check if we already have the item
    if (bot->HasItemCount(itemId, 1))
    {
        // Item acquired! Let the event system handle completion
        sGoalMgr->OnItemLooted(bot, itemId);
        return true;
    }

    // Strategy 2: Try to buy from vendor
    // This would integrate with existing buy actions
    // botAI->DoSpecificAction("buy", itemId);

    // Strategy 3: Look for mobs that drop this item
    // This would integrate with existing grind/attack logic
    // For now, just continue normal combat behavior

    LOG_DEBUG("playerbots", "GoalActions: Bot {} pursuing item goal: {} (id={})",
        bot->GetGUID().GetCounter(), activeGoal->targetName, itemId);

    return true;
}

bool PursueItemGoalAction::isPossible()
{
    return sGoalMgr->IsEnabled() && botAI->GetBot() != nullptr;
}

bool PursueItemGoalAction::isUseful()
{
    if (!sGoalMgr->IsEnabled())
        return false;

    Player* bot = botAI->GetBot();
    if (!bot)
        return false;

    auto activeGoal = sGoalMgr->GetActiveGoal(bot);
    return activeGoal.has_value() && activeGoal->type == GoalType::ITEM && !activeGoal->IsComplete();
}

// ============================================================================
// PursueReputationGoalAction
// ============================================================================

bool PursueReputationGoalAction::Execute(Event event)
{
    Player* bot = botAI->GetBot();
    if (!bot)
        return false;

    auto activeGoal = sGoalMgr->GetActiveGoal(bot);
    if (!activeGoal.has_value() || activeGoal->type != GoalType::REPUTATION)
        return false;

    uint32_t factionId = activeGoal->targetId;

    // Strategy: Find and complete faction quests
    // This would integrate with existing quest system

    // Strategy: Kill faction enemies
    // This would integrate with existing combat system

    LOG_DEBUG("playerbots", "GoalActions: Bot {} pursuing reputation goal: {} (faction={})",
        bot->GetGUID().GetCounter(), activeGoal->targetName, factionId);

    return true;
}

bool PursueReputationGoalAction::isPossible()
{
    return sGoalMgr->IsEnabled() && botAI->GetBot() != nullptr;
}

bool PursueReputationGoalAction::isUseful()
{
    if (!sGoalMgr->IsEnabled())
        return false;

    Player* bot = botAI->GetBot();
    if (!bot)
        return false;

    auto activeGoal = sGoalMgr->GetActiveGoal(bot);
    return activeGoal.has_value() && activeGoal->type == GoalType::REPUTATION && !activeGoal->IsComplete();
}

// ============================================================================
// PursueExploreGoalAction
// ============================================================================

bool PursueExploreGoalAction::Execute(Event event)
{
    Player* bot = botAI->GetBot();
    if (!bot)
        return false;

    auto activeGoal = sGoalMgr->GetActiveGoal(bot);
    if (!activeGoal.has_value() || activeGoal->type != GoalType::EXPLORE)
        return false;

    uint32_t targetZone = activeGoal->targetId;
    uint32_t currentZone = bot->GetZoneId();

    // Check if we're already in the target zone
    if (currentZone == targetZone)
    {
        // We're here! Mark as complete
        sGoalMgr->OnZoneDiscovered(bot, targetZone);
        return true;
    }

    // Strategy: Travel to the target zone
    // This would integrate with existing travel/movement system
    // botAI->DoSpecificAction("travel to zone", targetZone);

    LOG_DEBUG("playerbots", "GoalActions: Bot {} pursuing explore goal: {} (zone={})",
        bot->GetGUID().GetCounter(), activeGoal->targetName, targetZone);

    return true;
}

bool PursueExploreGoalAction::isPossible()
{
    return sGoalMgr->IsEnabled() && botAI->GetBot() != nullptr;
}

bool PursueExploreGoalAction::isUseful()
{
    if (!sGoalMgr->IsEnabled())
        return false;

    Player* bot = botAI->GetBot();
    if (!bot)
        return false;

    auto activeGoal = sGoalMgr->GetActiveGoal(bot);
    return activeGoal.has_value() && activeGoal->type == GoalType::EXPLORE && !activeGoal->IsComplete();
}

// ============================================================================
// PursueGoldGoalAction
// ============================================================================

bool PursueGoldGoalAction::Execute(Event event)
{
    Player* bot = botAI->GetBot();
    if (!bot)
        return false;

    auto activeGoal = sGoalMgr->GetActiveGoal(bot);
    if (!activeGoal.has_value() || activeGoal->type != GoalType::GOLD)
        return false;

    uint32_t currentGold = bot->GetMoney();
    uint32_t targetGold = activeGoal->targetProgress;

    // Update progress
    if (currentGold != activeGoal->progress)
    {
        sGoalRepository->UpdateProgress(activeGoal->id, currentGold);
    }

    // Check completion
    if (currentGold >= targetGold)
    {
        sGoalMgr->OnGoldChange(bot, currentGold - activeGoal->progress);
        return true;
    }

    // Strategy: Grind, quest, AH
    // This integrates with existing economy behaviors

    LOG_DEBUG("playerbots", "GoalActions: Bot {} pursuing gold goal: {}/{} copper",
        bot->GetGUID().GetCounter(), currentGold, targetGold);

    return true;
}

bool PursueGoldGoalAction::isPossible()
{
    return sGoalMgr->IsEnabled() && botAI->GetBot() != nullptr;
}

bool PursueGoldGoalAction::isUseful()
{
    if (!sGoalMgr->IsEnabled())
        return false;

    Player* bot = botAI->GetBot();
    if (!bot)
        return false;

    auto activeGoal = sGoalMgr->GetActiveGoal(bot);
    return activeGoal.has_value() && activeGoal->type == GoalType::GOLD && !activeGoal->IsComplete();
}

// ============================================================================
// PursueProfessionGoalAction
// ============================================================================

bool PursueProfessionGoalAction::Execute(Event event)
{
    Player* bot = botAI->GetBot();
    if (!bot)
        return false;

    auto activeGoal = sGoalMgr->GetActiveGoal(bot);
    if (!activeGoal.has_value() || activeGoal->type != GoalType::PROFESSION)
        return false;

    uint32_t skillId = activeGoal->targetId;
    uint32_t currentSkill = bot->GetSkillValue(skillId);
    uint32_t targetSkill = activeGoal->targetValue;

    // Update progress
    if (currentSkill != activeGoal->progress)
    {
        sGoalMgr->OnSkillChange(bot, skillId, currentSkill);
    }

    // Check completion
    if (currentSkill >= targetSkill)
    {
        return true;
    }

    // Strategy: Craft items, gather materials
    // This integrates with existing profession behaviors

    LOG_DEBUG("playerbots", "GoalActions: Bot {} pursuing profession goal: skill {} at {}/{}",
        bot->GetGUID().GetCounter(), skillId, currentSkill, targetSkill);

    return true;
}

bool PursueProfessionGoalAction::isPossible()
{
    return sGoalMgr->IsEnabled() && botAI->GetBot() != nullptr;
}

bool PursueProfessionGoalAction::isUseful()
{
    if (!sGoalMgr->IsEnabled())
        return false;

    Player* bot = botAI->GetBot();
    if (!bot)
        return false;

    auto activeGoal = sGoalMgr->GetActiveGoal(bot);
    return activeGoal.has_value() && activeGoal->type == GoalType::PROFESSION && !activeGoal->IsComplete();
}

// ============================================================================
// ReportGoalProgressAction
// ============================================================================

bool ReportGoalProgressAction::Execute(Event event)
{
    Player* bot = botAI->GetBot();
    if (!bot)
        return false;

    auto goals = sGoalMgr->GetActiveGoals(bot);

    if (goals.empty())
    {
        botAI->TellMaster("I have no active goals.");
        return true;
    }

    std::ostringstream out;
    out << "My goals:\n";

    for (const auto& goal : goals)
    {
        out << "- " << GoalTypeToString(goal.type) << ": "
            << goal.targetName << " ("
            << static_cast<int>(goal.GetProgressPercent()) << "% complete, "
            << "priority " << static_cast<int>(goal.priority) << ")\n";
    }

    botAI->TellMaster(out.str());
    return true;
}
