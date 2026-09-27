/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>
 * Released under GNU AGPL v3 license: https://github.com/azerothcore/azerothcore-wotlk/blob/master/LICENSE-AGPL3
 *
 * EC Bot Goal System - Goal Triggers
 *
 * Trigger implementations that fire based on goal state changes.
 */

#ifndef _EC_BOT_GOAL_TRIGGERS_H
#define _EC_BOT_GOAL_TRIGGERS_H

#include "Trigger.h"
#include "../../Mgr/Goal/Goal.h"

class PlayerbotAI;

/**
 * NoActiveGoalTrigger - Fires when bot has no active goal but could have one
 *
 * Used to trigger goal selection when bot is idle.
 * Check interval: 30 seconds (goal selection is not urgent)
 */
class NoActiveGoalTrigger : public Trigger
{
public:
    NoActiveGoalTrigger(PlayerbotAI* ai, std::string name = "no active goal")
        : Trigger(ai, name, 30) // Check every 30 seconds
    {}

    bool IsActive() override;
    std::string const GetTargetName() override { return "self target"; }
};

/**
 * GoalCompletedTrigger - Fires when current goal reaches completion
 *
 * Used to trigger goal completion actions (celebration, reward claim, etc.)
 * Check interval: 5 seconds (want responsive completion)
 */
class GoalCompletedTrigger : public Trigger
{
public:
    GoalCompletedTrigger(PlayerbotAI* ai, std::string name = "goal completed")
        : Trigger(ai, name, 5) // Check every 5 seconds
    {}

    bool IsActive() override;
    std::string const GetTargetName() override { return "self target"; }
};

/**
 * GoalProgressTrigger - Fires when goal makes progress
 *
 * Used to trigger progress notifications or behavior adjustments.
 * Stores last known progress to detect changes.
 * Check interval: 10 seconds
 */
class GoalProgressTrigger : public Trigger
{
public:
    GoalProgressTrigger(PlayerbotAI* ai, std::string name = "goal progress")
        : Trigger(ai, name, 10)
        , m_lastProgress(0)
        , m_lastGoalId(0)
    {}

    bool IsActive() override;
    std::string const GetTargetName() override { return "self target"; }

private:
    uint32_t m_lastProgress;
    uint32_t m_lastGoalId;
};

/**
 * GoalTargetNearbyTrigger - Fires when goal target is within range
 *
 * Used to trigger pursuit actions when:
 * - ITEM goal: mob that drops item is nearby
 * - REPUTATION goal: faction NPC is nearby
 * - EXPLORE goal: target zone boundary is nearby
 * Check interval: 15 seconds
 */
class GoalTargetNearbyTrigger : public Trigger
{
public:
    GoalTargetNearbyTrigger(PlayerbotAI* ai, std::string name = "goal target nearby")
        : Trigger(ai, name, 15)
    {}

    bool IsActive() override;
    std::string const GetTargetName() override { return "goal target"; }
};

/**
 * NeedsNewGoalTrigger - Fires when bot has slots for more goals
 *
 * Similar to NoActiveGoalTrigger but fires even with active goals
 * if bot has room for more parallel goals.
 * Check interval: 60 seconds (not urgent)
 */
class NeedsNewGoalTrigger : public Trigger
{
public:
    NeedsNewGoalTrigger(PlayerbotAI* ai, std::string name = "needs new goal")
        : Trigger(ai, name, 60) // Check every 60 seconds
    {}

    bool IsActive() override;
    std::string const GetTargetName() override { return "self target"; }
};

/**
 * GoalRefreshTrigger - Fires periodically to refresh goal priorities
 *
 * Used to trigger goal re-evaluation based on changed circumstances.
 * Check interval: 300 seconds (5 minutes)
 */
class GoalRefreshTrigger : public Trigger
{
public:
    GoalRefreshTrigger(PlayerbotAI* ai, std::string name = "goal refresh")
        : Trigger(ai, name, 300) // Check every 5 minutes
    {}

    bool IsActive() override;
    std::string const GetTargetName() override { return "self target"; }
};

/**
 * ItemGoalTrigger - Fires when bot has an active ITEM type goal
 *
 * Used to enable item-seeking behaviors.
 * Check interval: 10 seconds
 */
class ItemGoalTrigger : public Trigger
{
public:
    ItemGoalTrigger(PlayerbotAI* ai, std::string name = "item goal")
        : Trigger(ai, name, 10)
    {}

    bool IsActive() override;
    std::string const GetTargetName() override { return "self target"; }
};

/**
 * ReputationGoalTrigger - Fires when bot has an active REPUTATION type goal
 *
 * Used to enable reputation-grinding behaviors.
 * Check interval: 10 seconds
 */
class ReputationGoalTrigger : public Trigger
{
public:
    ReputationGoalTrigger(PlayerbotAI* ai, std::string name = "reputation goal")
        : Trigger(ai, name, 10)
    {}

    bool IsActive() override;
    std::string const GetTargetName() override { return "self target"; }
};

/**
 * ExploreGoalTrigger - Fires when bot has an active EXPLORE type goal
 *
 * Used to enable exploration behaviors.
 * Check interval: 10 seconds
 */
class ExploreGoalTrigger : public Trigger
{
public:
    ExploreGoalTrigger(PlayerbotAI* ai, std::string name = "explore goal")
        : Trigger(ai, name, 10)
    {}

    bool IsActive() override;
    std::string const GetTargetName() override { return "self target"; }
};

/**
 * GoldGoalTrigger - Fires when bot has an active GOLD type goal
 *
 * Used to enable gold-farming behaviors.
 * Check interval: 10 seconds
 */
class GoldGoalTrigger : public Trigger
{
public:
    GoldGoalTrigger(PlayerbotAI* ai, std::string name = "gold goal")
        : Trigger(ai, name, 10)
    {}

    bool IsActive() override;
    std::string const GetTargetName() override { return "self target"; }
};

/**
 * ProfessionGoalTrigger - Fires when bot has an active PROFESSION type goal
 *
 * Used to enable profession leveling behaviors.
 * Check interval: 10 seconds
 */
class ProfessionGoalTrigger : public Trigger
{
public:
    ProfessionGoalTrigger(PlayerbotAI* ai, std::string name = "profession goal")
        : Trigger(ai, name, 10)
    {}

    bool IsActive() override;
    std::string const GetTargetName() override { return "self target"; }
};

#endif // _EC_BOT_GOAL_TRIGGERS_H
