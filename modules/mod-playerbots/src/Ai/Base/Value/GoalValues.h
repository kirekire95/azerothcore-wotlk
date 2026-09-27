/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>
 * Released under GNU AGPL v3 license: https://github.com/azerothcore/azerothcore-wotlk/blob/master/LICENSE-AGPL3
 *
 * EC Bot Goal System - Goal Values (per-bot state)
 *
 * CalculatedValue implementations for accessing goal state from the AI system.
 */

#ifndef _EC_BOT_GOAL_VALUES_H
#define _EC_BOT_GOAL_VALUES_H

#include "Value.h"
#include "../../Mgr/Goal/Goal.h"
#include <vector>
#include <optional>

class PlayerbotAI;

/**
 * CurrentGoalValue - Get the current active goal for this bot
 *
 * Returns the highest priority active goal, or empty optional if no active goals.
 * Cached for 10 seconds to avoid frequent database queries.
 */
class CurrentGoalValue : public CalculatedValue<std::optional<Goal>>
{
public:
    CurrentGoalValue(PlayerbotAI* ai, std::string name = "current goal")
        : CalculatedValue<std::optional<Goal>>(ai, name, 10) // 10 second cache
    {}

    std::optional<Goal> Calculate() override;
};

/**
 * ActiveGoalsValue - Get all active goals for this bot
 *
 * Returns vector of all active goals sorted by priority.
 * Cached for 30 seconds.
 */
class ActiveGoalsValue : public CalculatedValue<std::vector<Goal>>
{
public:
    ActiveGoalsValue(PlayerbotAI* ai, std::string name = "active goals")
        : CalculatedValue<std::vector<Goal>>(ai, name, 30) // 30 second cache
    {}

    std::vector<Goal> Calculate() override;
};

/**
 * AvailableGoalsValue - Get potential goals that could be selected
 *
 * Returns vector of pending goals that haven't been activated yet.
 * Useful for UI display or decision making.
 * Cached for 60 seconds (goal evaluation is expensive).
 */
class AvailableGoalsValue : public CalculatedValue<std::vector<Goal>>
{
public:
    AvailableGoalsValue(PlayerbotAI* ai, std::string name = "available goals")
        : CalculatedValue<std::vector<Goal>>(ai, name, 60) // 60 second cache
    {}

    std::vector<Goal> Calculate() override;
};

/**
 * GoalProgressValue - Get progress percentage of current goal
 *
 * Returns 0-100 float representing completion percentage.
 * Returns 0 if no active goal.
 * Cached for 5 seconds.
 */
class GoalProgressValue : public CalculatedValue<float>
{
public:
    GoalProgressValue(PlayerbotAI* ai, std::string name = "goal progress")
        : CalculatedValue<float>(ai, name, 5) // 5 second cache
    {}

    float Calculate() override;
};

/**
 * HasActiveGoalValue - Check if bot has any active goal
 *
 * Simple boolean check, useful for trigger conditions.
 * Cached for 5 seconds.
 */
class HasActiveGoalValue : public CalculatedValue<bool>
{
public:
    HasActiveGoalValue(PlayerbotAI* ai, std::string name = "has active goal")
        : CalculatedValue<bool>(ai, name, 5) // 5 second cache
    {}

    bool Calculate() override;
};

/**
 * GoalCountValue - Get number of active goals
 *
 * Returns count of active goals for this bot.
 * Cached for 10 seconds.
 */
class GoalCountValue : public CalculatedValue<uint8_t>
{
public:
    GoalCountValue(PlayerbotAI* ai, std::string name = "goal count")
        : CalculatedValue<uint8_t>(ai, name, 10) // 10 second cache
    {}

    uint8_t Calculate() override;
};

/**
 * GoalTypeValue - Get type of current active goal
 *
 * Returns GoalType enum of highest priority active goal.
 * Returns MAX_GOAL_TYPE if no active goal.
 * Cached for 5 seconds.
 */
class GoalTypeValue : public CalculatedValue<GoalType>
{
public:
    GoalTypeValue(PlayerbotAI* ai, std::string name = "goal type")
        : CalculatedValue<GoalType>(ai, name, 5) // 5 second cache
    {}

    GoalType Calculate() override;
};

/**
 * GoalTargetIdValue - Get target ID of current active goal
 *
 * Returns targetId (itemId, factionId, zoneId, etc.) of current goal.
 * Returns 0 if no active goal.
 * Cached for 5 seconds.
 */
class GoalTargetIdValue : public CalculatedValue<uint32_t>
{
public:
    GoalTargetIdValue(PlayerbotAI* ai, std::string name = "goal target id")
        : CalculatedValue<uint32_t>(ai, name, 5) // 5 second cache
    {}

    uint32_t Calculate() override;
};

/**
 * GoalTargetNameValue - Get human-readable target name of current goal
 *
 * Returns target name string (item name, faction name, zone name, etc.)
 * Returns empty string if no active goal.
 * Cached for 30 seconds (names don't change).
 */
class GoalTargetNameValue : public CalculatedValue<std::string>
{
public:
    GoalTargetNameValue(PlayerbotAI* ai, std::string name = "goal target name")
        : CalculatedValue<std::string>(ai, name, 30) // 30 second cache
    {}

    std::string Calculate() override;
};

#endif // _EC_BOT_GOAL_VALUES_H
