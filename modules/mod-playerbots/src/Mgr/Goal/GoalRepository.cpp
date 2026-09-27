/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>
 * Released under GNU AGPL v3 license: https://github.com/azerothcore/azerothcore-wotlk/blob/master/LICENSE-AGPL3
 *
 * EC Bot Goal System - Goal Repository Implementation
 */

#include "GoalRepository.h"
#include "DatabaseEnv.h"
#include "Field.h"
#include "QueryResult.h"
#include "Log.h"
#include <ctime>

GoalRepository* GoalRepository::instance()
{
    static GoalRepository instance;
    return &instance;
}

void GoalRepository::Initialize()
{
    LOG_INFO("playerbots", "GoalRepository: Initializing...");

    // Table should be created by SQL migration, but verify it exists
    QueryResult result = CharacterDatabase.Query("SHOW TABLES LIKE 'ec_bot_goals'");
    if (!result)
    {
        LOG_ERROR("playerbots", "GoalRepository: Table 'ec_bot_goals' not found! Run the SQL migration.");
        return;
    }

    // Get count for logging
    result = CharacterDatabase.Query("SELECT COUNT(*) FROM ec_bot_goals WHERE status = 1");
    if (result)
    {
        Field* fields = result->Fetch();
        uint32_t activeCount = fields[0].Get<uint32_t>();
        LOG_INFO("playerbots", "GoalRepository: Initialized with {} active goals", activeCount);
    }
}

Goal GoalRepository::ParseGoalFromRow(Field* fields)
{
    Goal goal;
    goal.id = fields[0].Get<uint32_t>();          // goal_id
    goal.botGuid = fields[1].Get<uint32_t>();      // bot_guid
    goal.type = static_cast<GoalType>(fields[2].Get<uint8_t>());  // goal_type
    goal.targetId = fields[3].Get<uint32_t>();     // target_id
    goal.priority = fields[4].Get<uint8_t>();      // priority
    goal.progress = fields[5].Get<uint32_t>();     // progress_current
    goal.targetProgress = fields[6].Get<uint32_t>(); // progress_target
    goal.status = static_cast<GoalStatus>(fields[7].Get<uint8_t>()); // status
    goal.createdAt = fields[8].Get<uint32_t>();    // created_at
    goal.updatedAt = fields[9].Get<uint32_t>();    // updated_at
    goal.targetValue = 0; // Not stored in DB
    goal.personalityWeight = 1.0f; // Will be calculated by GoalMgr
    return goal;
}

std::vector<Goal> GoalRepository::LoadGoalsForBot(uint32_t botGuid)
{
    std::vector<Goal> goals;

    QueryResult result = CharacterDatabase.Query(
        "SELECT goal_id, bot_guid, goal_type, target_id, priority, progress_current, "
        "progress_target, status, created_at, updated_at "
        "FROM ec_bot_goals WHERE bot_guid = {} ORDER BY priority DESC",
        botGuid);

    if (!result)
        return goals;

    do
    {
        Field* fields = result->Fetch();
        goals.push_back(ParseGoalFromRow(fields));
    } while (result->NextRow());

    return goals;
}

std::vector<Goal> GoalRepository::LoadActiveGoals(uint32_t botGuid)
{
    return LoadGoalsByStatus(botGuid, GoalStatus::ACTIVE);
}

std::vector<Goal> GoalRepository::LoadGoalsByStatus(uint32_t botGuid, GoalStatus status)
{
    std::vector<Goal> goals;

    QueryResult result = CharacterDatabase.Query(
        "SELECT goal_id, bot_guid, goal_type, target_id, priority, progress_current, "
        "progress_target, status, created_at, updated_at "
        "FROM ec_bot_goals WHERE bot_guid = {} AND status = {} ORDER BY priority DESC",
        botGuid, static_cast<uint8_t>(status));

    if (!result)
        return goals;

    do
    {
        Field* fields = result->Fetch();
        goals.push_back(ParseGoalFromRow(fields));
    } while (result->NextRow());

    return goals;
}

std::unique_ptr<Goal> GoalRepository::LoadGoal(uint32_t goalId)
{
    QueryResult result = CharacterDatabase.Query(
        "SELECT goal_id, bot_guid, goal_type, target_id, priority, progress_current, "
        "progress_target, status, created_at, updated_at "
        "FROM ec_bot_goals WHERE goal_id = {}",
        goalId);

    if (!result)
        return nullptr;

    Field* fields = result->Fetch();
    return std::make_unique<Goal>(ParseGoalFromRow(fields));
}

uint32_t GoalRepository::SaveGoal(Goal& goal)
{
    uint32_t now = static_cast<uint32_t>(std::time(nullptr));
    goal.createdAt = now;
    goal.updatedAt = now;

    // Use DirectExecute + Query in same connection context for atomicity
    CharacterDatabase.DirectExecute(
        "INSERT INTO ec_bot_goals (bot_guid, goal_type, target_id, priority, "
        "progress_current, progress_target, status, created_at, updated_at) "
        "VALUES ({}, {}, {}, {}, {}, {}, {}, {}, {})",
        goal.botGuid,
        static_cast<uint8_t>(goal.type),
        goal.targetId,
        goal.priority,
        goal.progress,
        goal.targetProgress,
        static_cast<uint8_t>(goal.status),
        goal.createdAt,
        goal.updatedAt);

    // Get the auto-generated ID (same connection context after DirectExecute)
    QueryResult result = CharacterDatabase.Query("SELECT LAST_INSERT_ID()");
    if (result)
    {
        Field* fields = result->Fetch();
        goal.id = fields[0].Get<uint32_t>();
        return goal.id;
    }

    return 0;
}

bool GoalRepository::UpdateGoal(const Goal& goal)
{
    uint32_t now = static_cast<uint32_t>(std::time(nullptr));

    CharacterDatabase.Execute(
        "UPDATE ec_bot_goals SET "
        "goal_type = {}, target_id = {}, priority = {}, "
        "progress_current = {}, progress_target = {}, status = {}, updated_at = {} "
        "WHERE goal_id = {}",
        static_cast<uint8_t>(goal.type),
        goal.targetId,
        goal.priority,
        goal.progress,
        goal.targetProgress,
        static_cast<uint8_t>(goal.status),
        now,
        goal.id);

    return true;
}

bool GoalRepository::DeleteGoal(uint32_t goalId)
{
    CharacterDatabase.Execute("DELETE FROM ec_bot_goals WHERE goal_id = {}", goalId);
    return true;
}

void GoalRepository::UpdateProgress(uint32_t goalId, uint32_t progress)
{
    uint32_t now = static_cast<uint32_t>(std::time(nullptr));
    CharacterDatabase.Execute(
        "UPDATE ec_bot_goals SET progress_current = {}, updated_at = {} WHERE goal_id = {}",
        progress, now, goalId);
}

void GoalRepository::SetStatus(uint32_t goalId, GoalStatus status)
{
    uint32_t now = static_cast<uint32_t>(std::time(nullptr));
    CharacterDatabase.Execute(
        "UPDATE ec_bot_goals SET status = {}, updated_at = {} WHERE goal_id = {}",
        static_cast<uint8_t>(status), now, goalId);
}

void GoalRepository::AbandonAllGoals(uint32_t botGuid)
{
    uint32_t now = static_cast<uint32_t>(std::time(nullptr));
    CharacterDatabase.Execute(
        "UPDATE ec_bot_goals SET status = {}, updated_at = {} "
        "WHERE bot_guid = {} AND status IN ({}, {})",
        static_cast<uint8_t>(GoalStatus::ABANDONED),
        now,
        botGuid,
        static_cast<uint8_t>(GoalStatus::PENDING),
        static_cast<uint8_t>(GoalStatus::ACTIVE));
}

uint32_t GoalRepository::CleanupOldGoals(uint32_t maxAgeSeconds)
{
    uint32_t cutoff = static_cast<uint32_t>(std::time(nullptr)) - maxAgeSeconds;

    // First get count
    QueryResult result = CharacterDatabase.Query(
        "SELECT COUNT(*) FROM ec_bot_goals WHERE status IN ({}, {}, {}) AND updated_at < {}",
        static_cast<uint8_t>(GoalStatus::COMPLETED),
        static_cast<uint8_t>(GoalStatus::FAILED),
        static_cast<uint8_t>(GoalStatus::ABANDONED),
        cutoff);

    uint32_t count = 0;
    if (result)
    {
        Field* fields = result->Fetch();
        count = fields[0].Get<uint32_t>();
    }

    // Then delete
    CharacterDatabase.Execute(
        "DELETE FROM ec_bot_goals WHERE status IN ({}, {}, {}) AND updated_at < {}",
        static_cast<uint8_t>(GoalStatus::COMPLETED),
        static_cast<uint8_t>(GoalStatus::FAILED),
        static_cast<uint8_t>(GoalStatus::ABANDONED),
        cutoff);

    if (count > 0)
        LOG_INFO("playerbots", "GoalRepository: Cleaned up {} old goals", count);

    return count;
}

bool GoalRepository::HasGoalForTarget(uint32_t botGuid, GoalType type, uint32_t targetId)
{
    QueryResult result = CharacterDatabase.Query(
        "SELECT 1 FROM ec_bot_goals WHERE bot_guid = {} AND goal_type = {} "
        "AND target_id = {} AND status IN ({}, {}) LIMIT 1",
        botGuid,
        static_cast<uint8_t>(type),
        targetId,
        static_cast<uint8_t>(GoalStatus::PENDING),
        static_cast<uint8_t>(GoalStatus::ACTIVE));

    return result != nullptr;
}

uint32_t GoalRepository::GetActiveGoalCount(uint32_t botGuid)
{
    QueryResult result = CharacterDatabase.Query(
        "SELECT COUNT(*) FROM ec_bot_goals WHERE bot_guid = {} AND status = {}",
        botGuid, static_cast<uint8_t>(GoalStatus::ACTIVE));

    if (result)
    {
        Field* fields = result->Fetch();
        return fields[0].Get<uint32_t>();
    }

    return 0;
}
