/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>
 * Released under GNU AGPL v3 license: https://github.com/azerothcore/azerothcore-wotlk/blob/master/LICENSE-AGPL3
 *
 * EC Bot Goal System - Goal Repository
 *
 * Database access layer for bot goals. Handles CRUD operations on ec_bot_goals table.
 */

#ifndef _EC_BOT_GOAL_REPOSITORY_H
#define _EC_BOT_GOAL_REPOSITORY_H

#include "Goal.h"
#include <vector>
#include <memory>

// Forward declarations
class Field;

class GoalRepository
{
public:
    static GoalRepository* instance();

    // Initialize repository (ensure table exists)
    void Initialize();

    // ========================================================================
    // CRUD Operations
    // ========================================================================

    /**
     * Load all goals for a bot
     * @param botGuid Character GUID
     * @return Vector of all goals (any status)
     */
    std::vector<Goal> LoadGoalsForBot(uint32_t botGuid);

    /**
     * Load only active goals for a bot
     * @param botGuid Character GUID
     * @return Vector of goals with status = ACTIVE, sorted by priority DESC
     */
    std::vector<Goal> LoadActiveGoals(uint32_t botGuid);

    /**
     * Load goals by status
     * @param botGuid Character GUID
     * @param status Goal status to filter by
     * @return Vector of matching goals
     */
    std::vector<Goal> LoadGoalsByStatus(uint32_t botGuid, GoalStatus status);

    /**
     * Load a single goal by ID
     * @param goalId Database ID
     * @return Goal if found, nullptr otherwise
     */
    std::unique_ptr<Goal> LoadGoal(uint32_t goalId);

    /**
     * Save a new goal to database
     * @param goal Goal to save (id will be updated with generated ID)
     * @return Generated goal ID, or 0 on failure
     */
    uint32_t SaveGoal(Goal& goal);

    /**
     * Update an existing goal
     * @param goal Goal with updated fields
     * @return true if updated successfully
     */
    bool UpdateGoal(const Goal& goal);

    /**
     * Delete a goal by ID
     * @param goalId Database ID
     * @return true if deleted
     */
    bool DeleteGoal(uint32_t goalId);

    // ========================================================================
    // Bulk Operations
    // ========================================================================

    /**
     * Update only the progress field
     * @param goalId Database ID
     * @param progress New progress value
     */
    void UpdateProgress(uint32_t goalId, uint32_t progress);

    /**
     * Update only the status field
     * @param goalId Database ID
     * @param status New status
     */
    void SetStatus(uint32_t goalId, GoalStatus status);

    /**
     * Mark all active goals as abandoned for a bot
     * @param botGuid Character GUID
     */
    void AbandonAllGoals(uint32_t botGuid);

    /**
     * Delete all completed/failed/abandoned goals older than threshold
     * @param maxAgeSeconds Goals older than this will be deleted
     * @return Number of goals deleted
     */
    uint32_t CleanupOldGoals(uint32_t maxAgeSeconds);

    /**
     * Check if bot has an active goal of a specific type targeting a specific ID
     * @param botGuid Character GUID
     * @param type Goal type
     * @param targetId Target ID to check
     * @return true if such goal exists
     */
    bool HasGoalForTarget(uint32_t botGuid, GoalType type, uint32_t targetId);

    /**
     * Get count of active goals for a bot
     * @param botGuid Character GUID
     * @return Number of active goals
     */
    uint32_t GetActiveGoalCount(uint32_t botGuid);

private:
    GoalRepository() = default;
    ~GoalRepository() = default;
    GoalRepository(const GoalRepository&) = delete;
    GoalRepository& operator=(const GoalRepository&) = delete;

    // Helper to parse a database row into Goal struct
    Goal ParseGoalFromRow(Field* fields);
};

#define sGoalRepository GoalRepository::instance()

#endif // _EC_BOT_GOAL_REPOSITORY_H
