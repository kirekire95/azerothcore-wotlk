/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>
 * Released under GNU AGPL v3 license: https://github.com/azerothcore/azerothcore-wotlk/blob/master/LICENSE-AGPL3
 *
 * EC Bot Goal System - Goal Actions
 *
 * Action implementations for goal-related behaviors.
 */

#ifndef _EC_BOT_GOAL_ACTIONS_H
#define _EC_BOT_GOAL_ACTIONS_H

#include "Action.h"
#include "../../Mgr/Goal/Goal.h"

class PlayerbotAI;

/**
 * SelectGoalAction - Select and activate a new goal
 *
 * Called when bot needs a new goal. Evaluates available goals
 * and selects the best one based on personality and priorities.
 */
class SelectGoalAction : public Action
{
public:
    SelectGoalAction(PlayerbotAI* ai, std::string name = "select goal")
        : Action(ai, name)
    {}

    bool Execute(Event event) override;
    bool isPossible() override;
    bool isUseful() override;
};

/**
 * RefreshGoalsAction - Re-evaluate and reprioritize existing goals
 *
 * Called periodically to adjust goal priorities based on
 * changed circumstances (level up, new gear, etc.)
 */
class RefreshGoalsAction : public Action
{
public:
    RefreshGoalsAction(PlayerbotAI* ai, std::string name = "refresh goals")
        : Action(ai, name)
    {}

    bool Execute(Event event) override;
    bool isPossible() override;
    bool isUseful() override;
};

/**
 * CompleteGoalAction - Mark goal as completed and handle rewards
 *
 * Called when a goal reaches 100% progress.
 * Handles celebration, logging, and cleanup.
 */
class CompleteGoalAction : public Action
{
public:
    CompleteGoalAction(PlayerbotAI* ai, std::string name = "complete goal")
        : Action(ai, name)
    {}

    bool Execute(Event event) override;
    bool isPossible() override;
    bool isUseful() override;
};

/**
 * AbandonGoalAction - Abandon current goal
 *
 * Called when bot decides to stop pursuing a goal
 * (e.g., better goal available, goal no longer valid).
 */
class AbandonGoalAction : public Action
{
public:
    AbandonGoalAction(PlayerbotAI* ai, std::string name = "abandon goal")
        : Action(ai, name)
    {}

    bool Execute(Event event) override;
    bool isPossible() override;
    bool isUseful() override;
};

/**
 * PursueGoalAction - Take action toward completing current goal
 *
 * Base action that delegates to goal-type-specific pursuit.
 * This is the main "work toward goal" action.
 */
class PursueGoalAction : public Action
{
public:
    PursueGoalAction(PlayerbotAI* ai, std::string name = "pursue goal")
        : Action(ai, name)
    {}

    bool Execute(Event event) override;
    bool isPossible() override;
    bool isUseful() override;
};

/**
 * PursueItemGoalAction - Pursue an item acquisition goal
 *
 * Actions include:
 * - Kill mobs that drop the item
 * - Buy from vendor/AH
 * - Complete quests that reward it
 */
class PursueItemGoalAction : public Action
{
public:
    PursueItemGoalAction(PlayerbotAI* ai, std::string name = "pursue item goal")
        : Action(ai, name)
    {}

    bool Execute(Event event) override;
    bool isPossible() override;
    bool isUseful() override;
};

/**
 * PursueReputationGoalAction - Pursue a reputation goal
 *
 * Actions include:
 * - Kill faction enemies
 * - Complete faction quests
 * - Turn in repeatable items
 */
class PursueReputationGoalAction : public Action
{
public:
    PursueReputationGoalAction(PlayerbotAI* ai, std::string name = "pursue reputation goal")
        : Action(ai, name)
    {}

    bool Execute(Event event) override;
    bool isPossible() override;
    bool isUseful() override;
};

/**
 * PursueExploreGoalAction - Pursue an exploration goal
 *
 * Actions include:
 * - Travel to unexplored zone
 * - Explore all sub-areas
 */
class PursueExploreGoalAction : public Action
{
public:
    PursueExploreGoalAction(PlayerbotAI* ai, std::string name = "pursue explore goal")
        : Action(ai, name)
    {}

    bool Execute(Event event) override;
    bool isPossible() override;
    bool isUseful() override;
};

/**
 * PursueGoldGoalAction - Pursue a gold accumulation goal
 *
 * Actions include:
 * - Farm valuable items
 * - Sell on AH
 * - Do profitable quests
 */
class PursueGoldGoalAction : public Action
{
public:
    PursueGoldGoalAction(PlayerbotAI* ai, std::string name = "pursue gold goal")
        : Action(ai, name)
    {}

    bool Execute(Event event) override;
    bool isPossible() override;
    bool isUseful() override;
};

/**
 * PursueProfessionGoalAction - Pursue a profession skill goal
 *
 * Actions include:
 * - Gather materials
 * - Craft items
 * - Learn recipes from trainer
 */
class PursueProfessionGoalAction : public Action
{
public:
    PursueProfessionGoalAction(PlayerbotAI* ai, std::string name = "pursue profession goal")
        : Action(ai, name)
    {}

    bool Execute(Event event) override;
    bool isPossible() override;
    bool isUseful() override;
};

/**
 * ReportGoalProgressAction - Report goal progress to chat
 *
 * Used for debugging/testing. Reports current goal status.
 */
class ReportGoalProgressAction : public Action
{
public:
    ReportGoalProgressAction(PlayerbotAI* ai, std::string name = "report goal progress")
        : Action(ai, name)
    {}

    bool Execute(Event event) override;
    bool isPossible() override { return true; }
    bool isUseful() override { return true; }
};

#endif // _EC_BOT_GOAL_ACTIONS_H
