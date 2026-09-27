/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>
 * Released under GNU AGPL v3 license: https://github.com/azerothcore/azerothcore-wotlk/blob/master/LICENSE-AGPL3
 *
 * EC Bot Goal System - Goal Strategy Implementation
 */

#include "GoalStrategy.h"
#include "Playerbots.h"

// ============================================================================
// GoalStrategy - Main goal management strategy
// ============================================================================

void GoalStrategy::InitTriggers(std::vector<TriggerNode*>& triggers)
{
    // When bot has no active goals, try to select new ones
    // Priority 5.0 - low, background task
    triggers.push_back(new TriggerNode(
        "no active goal",
        { NextAction("select goal", 5.0f) }));

    // When a goal is completed, handle it immediately
    // Priority 80.0 - high, want to process completions quickly
    triggers.push_back(new TriggerNode(
        "goal completed",
        { NextAction("complete goal", 80.0f) }));

    // Periodic goal refresh to re-evaluate priorities
    // Priority 3.0 - very low, just maintenance
    triggers.push_back(new TriggerNode(
        "goal refresh",
        { NextAction("refresh goals", 3.0f) }));

    // When we have room for more goals, try to add them
    // Priority 4.0 - low, opportunistic
    triggers.push_back(new TriggerNode(
        "needs new goal",
        { NextAction("select goal", 4.0f) }));
}

void GoalStrategy::InitMultipliers(std::vector<Multiplier*>& multipliers)
{
    // No multipliers needed for base goal strategy
}

// ============================================================================
// GoalPursuitStrategy - Active goal pursuit
// ============================================================================

void GoalPursuitStrategy::InitTriggers(std::vector<TriggerNode*>& triggers)
{
    // When goal target is nearby, pursue more aggressively
    // Priority 30.0 - moderate, higher than idle but lower than combat
    triggers.push_back(new TriggerNode(
        "goal target nearby",
        { NextAction("pursue goal", 30.0f) }));

    // When goal makes progress, might want to continue
    // Priority 25.0 - slightly lower than target nearby
    triggers.push_back(new TriggerNode(
        "goal progress",
        { NextAction("pursue goal", 25.0f) }));
}

// ============================================================================
// ItemGoalStrategy
// ============================================================================

void ItemGoalStrategy::InitTriggers(std::vector<TriggerNode*>& triggers)
{
    // When item goal is active, enable item pursuit
    // Priority 20.0 - background activity
    triggers.push_back(new TriggerNode(
        "item goal",
        { NextAction("pursue item goal", 20.0f) }));
}

// ============================================================================
// ReputationGoalStrategy
// ============================================================================

void ReputationGoalStrategy::InitTriggers(std::vector<TriggerNode*>& triggers)
{
    // When reputation goal is active, enable rep grinding
    // Priority 20.0 - background activity
    triggers.push_back(new TriggerNode(
        "reputation goal",
        { NextAction("pursue reputation goal", 20.0f) }));
}

// ============================================================================
// ExploreGoalStrategy
// ============================================================================

void ExploreGoalStrategy::InitTriggers(std::vector<TriggerNode*>& triggers)
{
    // When explore goal is active, enable exploration
    // Priority 22.0 - slightly higher than other goals (exploration is safer)
    triggers.push_back(new TriggerNode(
        "explore goal",
        { NextAction("pursue explore goal", 22.0f) }));
}

// ============================================================================
// GoldGoalStrategy
// ============================================================================

void GoldGoalStrategy::InitTriggers(std::vector<TriggerNode*>& triggers)
{
    // When gold goal is active, enable gold farming
    // Priority 18.0 - slightly lower (gold is accumulated passively too)
    triggers.push_back(new TriggerNode(
        "gold goal",
        { NextAction("pursue gold goal", 18.0f) }));
}

// ============================================================================
// ProfessionGoalStrategy
// ============================================================================

void ProfessionGoalStrategy::InitTriggers(std::vector<TriggerNode*>& triggers)
{
    // When profession goal is active, enable crafting/gathering
    // Priority 19.0 - similar to gold, can be done during downtime
    triggers.push_back(new TriggerNode(
        "profession goal",
        { NextAction("pursue profession goal", 19.0f) }));
}
