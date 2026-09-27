/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>
 * Released under GNU AGPL v3 license: https://github.com/azerothcore/azerothcore-wotlk/blob/master/LICENSE-AGPL3
 *
 * EC Bot Goal System - Goal Strategy
 *
 * Strategy that binds goal triggers to goal actions.
 */

#ifndef _EC_BOT_GOAL_STRATEGY_H
#define _EC_BOT_GOAL_STRATEGY_H

#include "Strategy.h"
#include "Action.h"

class PlayerbotAI;

/**
 * GoalStrategy - Main strategy for goal-driven behavior
 *
 * Binds goal triggers to appropriate actions:
 * - no active goal → select goal (low priority, background)
 * - goal completed → complete goal (high priority, immediate)
 * - goal refresh → refresh goals (low priority, periodic)
 * - needs new goal → select goal (low priority, fills slots)
 */
class GoalStrategy : public Strategy
{
public:
    GoalStrategy(PlayerbotAI* ai) : Strategy(ai) {}

    std::string const getName() override { return "goal"; }
    uint32 GetType() const override { return STRATEGY_TYPE_GENERIC; }

    void InitTriggers(std::vector<TriggerNode*>& triggers) override;
    void InitMultipliers(std::vector<Multiplier*>& multipliers) override;
};

/**
 * GoalPursuitStrategy - Strategy for actively pursuing goals
 *
 * Lower priority than combat/survival but higher than idle behaviors.
 * Enables goal-specific pursuit actions based on goal type.
 */
class GoalPursuitStrategy : public Strategy
{
public:
    GoalPursuitStrategy(PlayerbotAI* ai) : Strategy(ai) {}

    std::string const getName() override { return "goal pursuit"; }
    uint32 GetType() const override { return STRATEGY_TYPE_GENERIC; }

    void InitTriggers(std::vector<TriggerNode*>& triggers) override;
};

/**
 * ItemGoalStrategy - Strategy for pursuing item goals
 *
 * Enables item-seeking behaviors when an item goal is active.
 */
class ItemGoalStrategy : public Strategy
{
public:
    ItemGoalStrategy(PlayerbotAI* ai) : Strategy(ai) {}

    std::string const getName() override { return "item goal"; }
    uint32 GetType() const override { return STRATEGY_TYPE_GENERIC; }

    void InitTriggers(std::vector<TriggerNode*>& triggers) override;
};

/**
 * ReputationGoalStrategy - Strategy for pursuing reputation goals
 *
 * Enables faction grinding behaviors when a reputation goal is active.
 */
class ReputationGoalStrategy : public Strategy
{
public:
    ReputationGoalStrategy(PlayerbotAI* ai) : Strategy(ai) {}

    std::string const getName() override { return "reputation goal"; }
    uint32 GetType() const override { return STRATEGY_TYPE_GENERIC; }

    void InitTriggers(std::vector<TriggerNode*>& triggers) override;
};

/**
 * ExploreGoalStrategy - Strategy for pursuing exploration goals
 *
 * Enables travel/exploration behaviors when an explore goal is active.
 */
class ExploreGoalStrategy : public Strategy
{
public:
    ExploreGoalStrategy(PlayerbotAI* ai) : Strategy(ai) {}

    std::string const getName() override { return "explore goal"; }
    uint32 GetType() const override { return STRATEGY_TYPE_GENERIC; }

    void InitTriggers(std::vector<TriggerNode*>& triggers) override;
};

/**
 * GoldGoalStrategy - Strategy for pursuing gold goals
 *
 * Enables gold-farming behaviors when a gold goal is active.
 */
class GoldGoalStrategy : public Strategy
{
public:
    GoldGoalStrategy(PlayerbotAI* ai) : Strategy(ai) {}

    std::string const getName() override { return "gold goal"; }
    uint32 GetType() const override { return STRATEGY_TYPE_GENERIC; }

    void InitTriggers(std::vector<TriggerNode*>& triggers) override;
};

/**
 * ProfessionGoalStrategy - Strategy for pursuing profession goals
 *
 * Enables crafting/gathering behaviors when a profession goal is active.
 */
class ProfessionGoalStrategy : public Strategy
{
public:
    ProfessionGoalStrategy(PlayerbotAI* ai) : Strategy(ai) {}

    std::string const getName() override { return "profession goal"; }
    uint32 GetType() const override { return STRATEGY_TYPE_GENERIC; }

    void InitTriggers(std::vector<TriggerNode*>& triggers) override;
};

#endif // _EC_BOT_GOAL_STRATEGY_H
