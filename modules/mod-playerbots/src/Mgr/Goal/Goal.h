/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>
 * Released under GNU AGPL v3 license: https://github.com/azerothcore/azerothcore-wotlk/blob/master/LICENSE-AGPL3
 *
 * EC Bot Goal System - Goal Data Structures
 *
 * Defines goal types, statuses, and the Goal struct for personality-driven bot behavior.
 */

#ifndef _EC_BOT_GOAL_H
#define _EC_BOT_GOAL_H

#include <cstdint>
#include <string>

/**
 * GoalType - Categories of goals bots can pursue
 *
 * Each type has different evaluation and progress tracking logic:
 * - ITEM: Track by item acquisition (loot, buy, craft)
 * - REPUTATION: Track by faction standing changes
 * - ACHIEVEMENT: Track by achievement criteria progress
 * - EXPLORE: Track by zone/area discovery
 * - PROFESSION: Track by skill level or recipe learning
 * - GOLD: Track by gold accumulation
 */
enum class GoalType : uint8_t
{
    ITEM        = 0,  // Acquire specific item (gear upgrade, consumable, material)
    REPUTATION  = 1,  // Reach reputation level with faction
    ACHIEVEMENT = 2,  // Complete achievement (future: not currently tracked by bots)
    EXPLORE     = 3,  // Discover zone/area
    PROFESSION  = 4,  // Learn recipe or reach skill level
    GOLD        = 5,  // Accumulate gold amount
    MAX_GOAL_TYPE
};

/**
 * GoalStatus - Lifecycle states for a goal
 */
enum class GoalStatus : uint8_t
{
    PENDING   = 0,  // Created but not yet started
    ACTIVE    = 1,  // Currently being pursued
    COMPLETED = 2,  // Successfully finished
    FAILED    = 3,  // Cannot complete (item removed, faction at war, etc.)
    ABANDONED = 4,  // Bot chose to abandon (better goal available, personality shift)
    MAX_GOAL_STATUS
};

/**
 * Goal - A single goal a bot is pursuing
 *
 * Goals are created by GoalMgr based on game data and bot personality,
 * stored in ec_bot_goals table, and tracked by GoalValues/GoalTriggers.
 */
struct Goal
{
    uint32_t id;            // Database ID (0 if not persisted)
    uint32_t botGuid;       // Character GUID of the bot
    GoalType type;          // Category of goal
    uint32_t targetId;      // Primary target (itemId, factionId, zoneId, skillId, gold amount)
    uint32_t targetValue;   // Secondary target (rep level, skill level, etc.)
    uint8_t priority;       // 0-100, higher = more important
    uint32_t progress;      // Current progress (items acquired, rep gained, etc.)
    uint32_t targetProgress;// Required for completion
    GoalStatus status;      // Current lifecycle state
    uint32_t createdAt;     // Unix timestamp
    uint32_t updatedAt;     // Unix timestamp

    // Computed/cached fields (not persisted)
    std::string targetName;     // Human-readable name (item name, faction name, etc.)
    float personalityWeight;    // Multiplier from bot personality (0.1 - 2.0)

    Goal()
        : id(0), botGuid(0), type(GoalType::ITEM), targetId(0), targetValue(0),
          priority(50), progress(0), targetProgress(1), status(GoalStatus::PENDING),
          createdAt(0), updatedAt(0), personalityWeight(1.0f) {}

    // Check if goal is complete
    bool IsComplete() const { return progress >= targetProgress; }

    // Get progress as percentage (0-100)
    float GetProgressPercent() const
    {
        return targetProgress > 0 ? (static_cast<float>(progress) / targetProgress) * 100.0f : 0.0f;
    }

    // Check if goal is actionable (can make progress)
    bool IsActive() const { return status == GoalStatus::ACTIVE; }

    // Check if goal has ended (completed, failed, or abandoned)
    bool IsEnded() const
    {
        return status == GoalStatus::COMPLETED ||
               status == GoalStatus::FAILED ||
               status == GoalStatus::ABANDONED;
    }

    // Get weighted priority (base priority * personality weight)
    float GetWeightedPriority() const
    {
        return static_cast<float>(priority) * personalityWeight;
    }
};

// Helper functions for enum conversion
inline const char* GoalTypeToString(GoalType type)
{
    switch (type)
    {
        case GoalType::ITEM:        return "Item";
        case GoalType::REPUTATION:  return "Reputation";
        case GoalType::ACHIEVEMENT: return "Achievement";
        case GoalType::EXPLORE:     return "Explore";
        case GoalType::PROFESSION:  return "Profession";
        case GoalType::GOLD:        return "Gold";
        default:                    return "Unknown";
    }
}

inline const char* GoalStatusToString(GoalStatus status)
{
    switch (status)
    {
        case GoalStatus::PENDING:   return "Pending";
        case GoalStatus::ACTIVE:    return "Active";
        case GoalStatus::COMPLETED: return "Completed";
        case GoalStatus::FAILED:    return "Failed";
        case GoalStatus::ABANDONED: return "Abandoned";
        default:                    return "Unknown";
    }
}

#endif // _EC_BOT_GOAL_H
