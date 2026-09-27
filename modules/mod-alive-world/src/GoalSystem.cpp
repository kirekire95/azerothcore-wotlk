/*
 * mod-alive-world - Goal System Implementation
 * Copyright (C) 2025 EC
 */

#include "GoalSystem.h"
#include "AliveWorldBridgeAdapter.h"
#include "AliveWorldManager.h"
#include "BotMemoryManager.h"
#include "WorldStateManager.h"

#include "Config.h"
#include "DatabaseEnv.h"
#include "Log.h"
#include "Player.h"
#include "Item.h"
#include "ItemTemplate.h"
#include "ObjectMgr.h"
#include "ReputationMgr.h"
#include "AchievementMgr.h"
#include "SpellMgr.h"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>

namespace AliveWorld
{
    // Logging macros - use LOG_* directly, log level controlled by config
    #define GOAL_LOG_ERROR(fmt, ...) \
        LOG_ERROR("module.alive-world", "[GoalSystem] " fmt, ##__VA_ARGS__)

    #define GOAL_LOG_INFO(fmt, ...) \
        LOG_INFO("module.alive-world", "[GoalSystem] " fmt, ##__VA_ARGS__)

    #define GOAL_LOG_DEBUG(fmt, ...) \
        LOG_DEBUG("module.alive-world", "[GoalSystem] " fmt, ##__VA_ARGS__)

    GoalSystem::GoalSystem()
        : m_enabled(true)
        , m_maxGoalsPerBot(10)
        , m_maxActiveGoals(3)
        , m_goalRefreshIntervalMs(300000)  // 5 minutes
        , m_persistIntervalMs(60000)       // 1 minute
        , m_autoSelectGoals(true)
        , m_nextGoalId(1)
        , m_memoryManager(nullptr)
        , m_worldStateManager(nullptr)
        , m_updateTimer(0)
        , m_persistTimer(0)
        , m_lastUpdateTime(0)
    {
    }

    GoalSystem::~GoalSystem()
    {
        SaveToDatabase();
    }

    void GoalSystem::Initialize(BotMemoryManager* memoryManager, WorldStateManager* worldStateManager)
    {
        m_memoryManager = memoryManager;
        m_worldStateManager = worldStateManager;
        LoadConfig();
        LoadFromDatabase();
        GOAL_LOG_INFO("Goal System initialized with %u goals loaded", GetTotalGoals());
    }

    void GoalSystem::LoadConfig()
    {
        m_enabled = sConfigMgr->GetOption<bool>("AliveWorld.Goals.Enable", true);
        m_maxGoalsPerBot = sConfigMgr->GetOption<uint32>("AliveWorld.Goals.MaxPerBot", 10);
        m_maxActiveGoals = sConfigMgr->GetOption<uint32>("AliveWorld.Goals.MaxActive", 3);
        m_goalRefreshIntervalMs = sConfigMgr->GetOption<uint32>("AliveWorld.Goals.RefreshIntervalMs", 300000);
        m_persistIntervalMs = sConfigMgr->GetOption<uint32>("AliveWorld.Goals.PersistIntervalMs", 60000);
        m_autoSelectGoals = sConfigMgr->GetOption<bool>("AliveWorld.Goals.AutoSelect", true);
    }

    // =========================================================================
    // GOAL SELECTION
    // =========================================================================

    std::vector<BotGoal> GoalSystem::SelectGoalsForBot(uint64 botGuid, Player* bot)
    {
        if (!m_enabled || !bot || !m_memoryManager)
            return {};

        // Get bot's personality for weighted selection
        BotPersonality personality = m_memoryManager->GetPersonality(botGuid);
        GoalWeights weights = CalculateWeightsFromPersonality(personality);

        std::vector<BotGoal> candidateGoals;

        // Generate goals from each category
        auto itemGoals = GenerateItemGoals(botGuid, bot, weights);
        auto repGoals = GenerateReputationGoals(botGuid, bot, weights);
        auto achieveGoals = GenerateAchievementGoals(botGuid, bot, weights);
        auto exploreGoals = GenerateExploreGoals(botGuid, bot, weights);
        auto profGoals = GenerateProfessionGoals(botGuid, bot, weights);
        auto goldGoals = GenerateGoldGoals(botGuid, bot, weights);
        auto levelGoals = GenerateLevelGoals(botGuid, bot, weights);
        auto pvpGoals = GeneratePvPGoals(botGuid, bot, weights);
        auto socialGoals = GenerateSocialGoals(botGuid, bot, weights);

        // Combine all candidate goals
        candidateGoals.insert(candidateGoals.end(), itemGoals.begin(), itemGoals.end());
        candidateGoals.insert(candidateGoals.end(), repGoals.begin(), repGoals.end());
        candidateGoals.insert(candidateGoals.end(), achieveGoals.begin(), achieveGoals.end());
        candidateGoals.insert(candidateGoals.end(), exploreGoals.begin(), exploreGoals.end());
        candidateGoals.insert(candidateGoals.end(), profGoals.begin(), profGoals.end());
        candidateGoals.insert(candidateGoals.end(), goldGoals.begin(), goldGoals.end());
        candidateGoals.insert(candidateGoals.end(), levelGoals.begin(), levelGoals.end());
        candidateGoals.insert(candidateGoals.end(), pvpGoals.begin(), pvpGoals.end());
        candidateGoals.insert(candidateGoals.end(), socialGoals.begin(), socialGoals.end());

        // Sort by priority (lower enum value = higher priority)
        std::sort(candidateGoals.begin(), candidateGoals.end(),
            [](const BotGoal& a, const BotGoal& b) {
                return static_cast<uint8>(a.priority) < static_cast<uint8>(b.priority);
            });

        // Take top N goals
        std::vector<BotGoal> selectedGoals;
        uint32 activeCount = 0;
        for (const auto& goal : candidateGoals)
        {
            if (selectedGoals.size() >= m_maxGoalsPerBot)
                break;

            // Check if we already have a similar goal
            if (HasSimilarGoal(botGuid, goal.type, goal.targetId))
                continue;

            // Limit active goals
            if (goal.status == GoalStatus::ACTIVE && activeCount >= m_maxActiveGoals)
            {
                BotGoal pausedGoal = goal;
                pausedGoal.status = GoalStatus::PAUSED;
                selectedGoals.push_back(pausedGoal);
            }
            else
            {
                selectedGoals.push_back(goal);
                if (goal.status == GoalStatus::ACTIVE)
                    activeCount++;
            }
        }

        GOAL_LOG_DEBUG("Selected %zu goals for bot %s (guid: %llu)",
            selectedGoals.size(), bot->GetName().c_str(), botGuid);

        return selectedGoals;
    }

    GoalWeights GoalSystem::CalculateWeightsFromPersonality(const BotPersonality& personality)
    {
        GoalWeights weights;

        // Item goals - influenced by greed
        weights.itemWeight = 0.5f + (personality.greed * 0.5f);

        // Reputation - influenced by loyalty and sociability
        weights.reputationWeight = 0.3f + (personality.loyalty * 0.3f) + (personality.sociability * 0.2f);

        // Achievement - influenced by curiosity
        weights.achievementWeight = 0.2f + (personality.curiosity * 0.6f);

        // Explore - heavily influenced by curiosity
        weights.exploreWeight = 0.2f + (personality.curiosity * 0.7f);

        // Profession - influenced by greed and caution (careful planners like professions)
        weights.professionWeight = 0.3f + (personality.greed * 0.3f) + (personality.caution * 0.2f);

        // Gold - heavily influenced by greed
        weights.goldWeight = 0.4f + (personality.greed * 0.6f);

        // Level - always somewhat important, slightly higher for aggressive bots
        weights.levelWeight = 0.7f + (personality.aggression * 0.2f);

        // PvP - heavily influenced by aggression and vengefulness
        weights.pvpWeight = 0.1f + (personality.aggression * 0.5f) + (personality.vengefulness * 0.3f);

        // Social - influenced by sociability and helpfulness
        weights.socialWeight = 0.2f + (personality.sociability * 0.5f) + (personality.helpfulness * 0.2f);

        return weights;
    }

    // =========================================================================
    // SPECIFIC GOAL GENERATORS
    // =========================================================================

    std::vector<BotGoal> GoalSystem::GenerateItemGoals(uint64 botGuid, Player* bot, const GoalWeights& weights)
    {
        std::vector<BotGoal> goals;
        if (weights.itemWeight < 0.3f)
            return goals;  // Bot not interested in items

        auto upgrades = FindItemUpgrades(bot, 5);

        for (const auto& upgrade : upgrades)
        {
            BotGoal goal;
            goal.goalId = GenerateGoalId();
            goal.botGuid = botGuid;
            goal.type = GoalType::ITEM;
            goal.targetId = upgrade.itemId;
            goal.targetName = upgrade.itemName;
            goal.progress = 0.0f;
            goal.progressCurrent = 0;
            goal.progressTarget = 1;
            goal.createdAt = NowMs();
            goal.updatedAt = NowMs();

            // Priority based on upgrade magnitude
            if (upgrade.upgradeScore > 0.5f)
                goal.priority = GoalPriority::HIGH;
            else if (upgrade.upgradeScore > 0.3f)
                goal.priority = GoalPriority::MEDIUM;
            else
                goal.priority = GoalPriority::LOW;

            std::ostringstream reason;
            reason << "Need better gear in slot " << (int)upgrade.slot
                   << " (current iLvl: " << upgrade.currentItemLevel
                   << ", target iLvl: " << upgrade.itemLevel << ")";
            goal.reason = reason.str();

            goals.push_back(goal);
        }

        return goals;
    }

    std::vector<BotGoal> GoalSystem::GenerateReputationGoals(uint64 botGuid, Player* bot, const GoalWeights& weights)
    {
        std::vector<BotGoal> goals;
        if (weights.reputationWeight < 0.3f)
            return goals;

        auto repTargets = FindReputationTargets(bot, 3);

        for (const auto& target : repTargets)
        {
            BotGoal goal;
            goal.goalId = GenerateGoalId();
            goal.botGuid = botGuid;
            goal.type = GoalType::REPUTATION;
            goal.targetId = target.factionId;
            goal.targetName = target.factionName;
            goal.progressCurrent = target.currentStanding;
            goal.progressTarget = target.targetStanding;
            goal.progress = target.targetStanding > 0 ?
                static_cast<float>(target.currentStanding) / static_cast<float>(target.targetStanding) : 0.0f;
            goal.createdAt = NowMs();
            goal.updatedAt = NowMs();

            // Priority based on faction importance
            if (target.priority > 0.7f)
                goal.priority = GoalPriority::HIGH;
            else if (target.priority > 0.4f)
                goal.priority = GoalPriority::MEDIUM;
            else
                goal.priority = GoalPriority::LOW;

            std::ostringstream reason;
            reason << "Want to reach " << target.targetRank << " with " << target.factionName
                   << " (currently " << target.currentRank << ")";
            goal.reason = reason.str();

            goals.push_back(goal);
        }

        return goals;
    }

    std::vector<BotGoal> GoalSystem::GenerateAchievementGoals(uint64 botGuid, Player* bot, const GoalWeights& weights)
    {
        std::vector<BotGoal> goals;
        if (weights.achievementWeight < 0.3f)
            return goals;

        // Achievement goals are complex - for now, create generic exploration achievements
        // Full implementation would scan achievement_criteria_data for near-complete achievements

        uint32 botLevel = bot->GetLevel();

        // If bot is high level, create achievement goal for exploration
        if (botLevel >= 60)
        {
            BotGoal goal;
            goal.goalId = GenerateGoalId();
            goal.botGuid = botGuid;
            goal.type = GoalType::ACHIEVEMENT;
            goal.targetId = 0;  // Generic exploration achievement
            goal.targetName = "World Explorer";
            goal.progress = 0.0f;
            goal.progressCurrent = 0;
            goal.progressTarget = 100;  // Placeholder
            goal.priority = GoalPriority::LOW;
            goal.createdAt = NowMs();
            goal.updatedAt = NowMs();
            goal.reason = "Curious about the world, want to see everything";

            goals.push_back(goal);
        }

        return goals;
    }

    std::vector<BotGoal> GoalSystem::GenerateExploreGoals(uint64 botGuid, Player* bot, const GoalWeights& weights)
    {
        std::vector<BotGoal> goals;
        if (weights.exploreWeight < 0.3f)
            return goals;

        // Get zones bot hasn't visited much
        if (!m_memoryManager)
            return goals;

        uint32 currentZone = bot->GetZoneId();
        uint32 botLevel = bot->GetLevel();

        // Suggest exploring a level-appropriate zone
        // This would ideally check zone level ranges from areaentry_dbc
        std::vector<uint32> suggestedZones;

        if (botLevel < 20)
            suggestedZones = {1, 12, 14, 141, 215};  // Starting zones
        else if (botLevel < 40)
            suggestedZones = {440, 267, 15, 33};     // Mid-level zones
        else if (botLevel < 60)
            suggestedZones = {1377, 2017, 357};      // Higher level zones
        else
            suggestedZones = {3483, 3518, 3523};    // Outland/Northrend

        for (uint32 zoneId : suggestedZones)
        {
            if (zoneId == currentZone)
                continue;

            // Check if bot has spent little time here
            uint32 timeSpent = m_memoryManager->GetZoneTimeSpent(botGuid, zoneId);
            if (timeSpent > 30)  // Already spent 30+ minutes
                continue;

            BotGoal goal;
            goal.goalId = GenerateGoalId();
            goal.botGuid = botGuid;
            goal.type = GoalType::EXPLORE;
            goal.targetId = zoneId;
            goal.targetName = "Zone " + std::to_string(zoneId);  // Would get actual name from AreaTableEntry
            goal.progress = timeSpent > 0 ? std::min(1.0f, timeSpent / 30.0f) : 0.0f;
            goal.progressCurrent = timeSpent;
            goal.progressTarget = 30;  // 30 minutes in zone
            goal.priority = GoalPriority::LOW;
            goal.createdAt = NowMs();
            goal.updatedAt = NowMs();
            goal.reason = "Curious about this unexplored area";

            goals.push_back(goal);

            if (goals.size() >= 2)
                break;  // Max 2 explore goals
        }

        return goals;
    }

    std::vector<BotGoal> GoalSystem::GenerateProfessionGoals(uint64 botGuid, Player* bot, const GoalWeights& weights)
    {
        std::vector<BotGoal> goals;
        if (weights.professionWeight < 0.3f)
            return goals;

        // Check bot's professions
        // This would scan the bot's skills and find recipes to learn

        // Simplified: Create generic profession leveling goal
        uint32 botLevel = bot->GetLevel();
        if (botLevel >= 10)
        {
            BotGoal goal;
            goal.goalId = GenerateGoalId();
            goal.botGuid = botGuid;
            goal.type = GoalType::PROFESSION;
            goal.targetId = 0;  // Generic profession
            goal.targetName = "Level Profession";
            goal.progress = 0.0f;
            goal.progressCurrent = 0;
            goal.progressTarget = 75;  // Next tier
            goal.priority = GoalPriority::PASSIVE;
            goal.createdAt = NowMs();
            goal.updatedAt = NowMs();
            goal.reason = "Want to improve crafting skills for profit";

            goals.push_back(goal);
        }

        return goals;
    }

    std::vector<BotGoal> GoalSystem::GenerateGoldGoals(uint64 botGuid, Player* bot, const GoalWeights& weights)
    {
        std::vector<BotGoal> goals;
        if (weights.goldWeight < 0.4f)
            return goals;

        uint32 currentGold = bot->GetMoney() / 10000;  // Convert to gold
        uint32 botLevel = bot->GetLevel();

        // Set gold target based on level
        uint32 targetGold = botLevel * 10;  // Simple formula: 10g per level

        if (currentGold < targetGold)
        {
            BotGoal goal;
            goal.goalId = GenerateGoalId();
            goal.botGuid = botGuid;
            goal.type = GoalType::GOLD;
            goal.targetId = targetGold;
            goal.targetName = std::to_string(targetGold) + " Gold";
            goal.progressCurrent = currentGold;
            goal.progressTarget = targetGold;
            goal.progress = static_cast<float>(currentGold) / static_cast<float>(targetGold);
            goal.priority = weights.goldWeight > 0.7f ? GoalPriority::HIGH : GoalPriority::MEDIUM;
            goal.createdAt = NowMs();
            goal.updatedAt = NowMs();
            goal.reason = "Need more gold for gear and consumables";

            goals.push_back(goal);
        }

        return goals;
    }

    std::vector<BotGoal> GoalSystem::GenerateLevelGoals(uint64 botGuid, Player* bot, const GoalWeights& weights)
    {
        std::vector<BotGoal> goals;

        uint32 currentLevel = bot->GetLevel();
        if (currentLevel >= 80)  // Max level
            return goals;

        uint32 targetLevel = std::min(currentLevel + 5, 80u);  // Aim for 5 levels ahead

        BotGoal goal;
        goal.goalId = GenerateGoalId();
        goal.botGuid = botGuid;
        goal.type = GoalType::LEVEL;
        goal.targetId = targetLevel;
        goal.targetName = "Level " + std::to_string(targetLevel);
        goal.progressCurrent = currentLevel;
        goal.progressTarget = targetLevel;
        goal.progress = static_cast<float>(currentLevel - (targetLevel - 5)) / 5.0f;
        goal.priority = GoalPriority::HIGH;  // Leveling is always important
        goal.createdAt = NowMs();
        goal.updatedAt = NowMs();
        goal.reason = "Get stronger to face tougher challenges";

        goals.push_back(goal);

        return goals;
    }

    std::vector<BotGoal> GoalSystem::GeneratePvPGoals(uint64 botGuid, Player* bot, const GoalWeights& weights)
    {
        std::vector<BotGoal> goals;
        if (weights.pvpWeight < 0.4f)
            return goals;

        // Only for bots that are interested in PvP
        if (!m_memoryManager)
            return goals;

        BotPersonality& personality = m_memoryManager->GetPersonality(botGuid);
        if (personality.aggression < 0.5f)
            return goals;

        // Create honor goal
        BotGoal goal;
        goal.goalId = GenerateGoalId();
        goal.botGuid = botGuid;
        goal.type = GoalType::PVP;
        goal.targetId = 1000;  // Target honor kills
        goal.targetName = "1000 Honor Kills";
        goal.progressCurrent = 0;  // Would get from player stats
        goal.progressTarget = 1000;
        goal.progress = 0.0f;
        goal.priority = GoalPriority::MEDIUM;
        goal.createdAt = NowMs();
        goal.updatedAt = NowMs();
        goal.reason = "Prove combat prowess against other players";

        goals.push_back(goal);

        return goals;
    }

    std::vector<BotGoal> GoalSystem::GenerateSocialGoals(uint64 botGuid, Player* bot, const GoalWeights& weights)
    {
        std::vector<BotGoal> goals;
        if (weights.socialWeight < 0.4f)
            return goals;

        // Social goals: make friends, join guild, help others
        if (!m_memoryManager)
            return goals;

        auto friends = m_memoryManager->GetFriends(botGuid, 0.5f);
        if (friends.size() < 3)
        {
            BotGoal goal;
            goal.goalId = GenerateGoalId();
            goal.botGuid = botGuid;
            goal.type = GoalType::SOCIAL;
            goal.targetId = 5;  // Target 5 friends
            goal.targetName = "Make 5 Friends";
            goal.progressCurrent = friends.size();
            goal.progressTarget = 5;
            goal.progress = static_cast<float>(friends.size()) / 5.0f;
            goal.priority = GoalPriority::LOW;
            goal.createdAt = NowMs();
            goal.updatedAt = NowMs();
            goal.reason = "Looking for companions to adventure with";

            goals.push_back(goal);
        }

        return goals;
    }

    // =========================================================================
    // GOAL MANAGEMENT
    // =========================================================================

    void GoalSystem::AddGoal(uint64 botGuid, const BotGoal& goal)
    {
        std::unique_lock<std::shared_mutex> lock(m_goalsMutex);

        auto& botGoals = m_botGoals[botGuid];

        // Check limit
        if (botGoals.size() >= m_maxGoalsPerBot)
        {
            PruneGoals(botGuid);
            if (botGoals.size() >= m_maxGoalsPerBot)
            {
                GOAL_LOG_DEBUG("Cannot add goal for bot %llu - at max capacity", botGuid);
                return;
            }
        }

        botGoals.push_back(goal);

        // Emit to bridge
        sAliveWorldBridge->EmitGoalCreated(
            botGuid, goal.goalId,
            static_cast<uint8>(goal.type),
            goal.targetName,
            static_cast<uint8>(goal.priority));

        // Mark dirty for persistence
        std::lock_guard<std::mutex> dirtyLock(m_dirtyMutex);
        m_dirtyBots.insert(botGuid);
    }

    void GoalSystem::RemoveGoal(uint64 botGuid, uint32 goalId)
    {
        std::unique_lock<std::shared_mutex> lock(m_goalsMutex);

        auto it = m_botGoals.find(botGuid);
        if (it == m_botGoals.end())
            return;

        auto& goals = it->second;
        goals.erase(
            std::remove_if(goals.begin(), goals.end(),
                [goalId](const BotGoal& g) { return g.goalId == goalId; }),
            goals.end()
        );

        std::lock_guard<std::mutex> dirtyLock(m_dirtyMutex);
        m_dirtyBots.insert(botGuid);
    }

    void GoalSystem::UpdateGoalProgress(uint64 botGuid, uint32 goalId, uint32 current, uint32 target)
    {
        std::unique_lock<std::shared_mutex> lock(m_goalsMutex);

        auto it = m_botGoals.find(botGuid);
        if (it == m_botGoals.end())
            return;

        for (auto& goal : it->second)
        {
            if (goal.goalId == goalId)
            {
                goal.UpdateProgress(current, target);

                // Emit progress update
                sAliveWorldBridge->EmitGoalProgress(
                    botGuid, goal.goalId,
                    goal.progress, goal.progressCurrent, goal.progressTarget);
                break;
            }
        }

        std::lock_guard<std::mutex> dirtyLock(m_dirtyMutex);
        m_dirtyBots.insert(botGuid);
    }

    void GoalSystem::CompleteGoal(uint64 botGuid, uint32 goalId)
    {
        std::unique_lock<std::shared_mutex> lock(m_goalsMutex);

        auto it = m_botGoals.find(botGuid);
        if (it == m_botGoals.end())
            return;

        for (auto& goal : it->second)
        {
            if (goal.goalId == goalId)
            {
                goal.status = GoalStatus::COMPLETED;
                goal.progress = 1.0f;
                goal.completedAt = NowMs();
                GOAL_LOG_INFO("Bot %llu completed goal: %s", botGuid, goal.targetName.c_str());

                // Emit goal completion
                sAliveWorldBridge->EmitGoalCompleted(botGuid, goal.goalId, goal.targetName);
                break;
            }
        }

        std::lock_guard<std::mutex> dirtyLock(m_dirtyMutex);
        m_dirtyBots.insert(botGuid);
    }

    void GoalSystem::AbandonGoal(uint64 botGuid, uint32 goalId)
    {
        std::unique_lock<std::shared_mutex> lock(m_goalsMutex);

        auto it = m_botGoals.find(botGuid);
        if (it == m_botGoals.end())
            return;

        for (auto& goal : it->second)
        {
            if (goal.goalId == goalId)
            {
                goal.status = GoalStatus::ABANDONED;
                goal.updatedAt = NowMs();

                // Emit goal abandoned
                sAliveWorldBridge->EmitGoalAbandoned(botGuid, goal.goalId, "Goal abandoned by bot");
                break;
            }
        }

        std::lock_guard<std::mutex> dirtyLock(m_dirtyMutex);
        m_dirtyBots.insert(botGuid);
    }

    void GoalSystem::PauseGoal(uint64 botGuid, uint32 goalId)
    {
        std::unique_lock<std::shared_mutex> lock(m_goalsMutex);

        auto it = m_botGoals.find(botGuid);
        if (it == m_botGoals.end())
            return;

        for (auto& goal : it->second)
        {
            if (goal.goalId == goalId && goal.status == GoalStatus::ACTIVE)
            {
                goal.status = GoalStatus::PAUSED;
                goal.updatedAt = NowMs();
                break;
            }
        }

        std::lock_guard<std::mutex> dirtyLock(m_dirtyMutex);
        m_dirtyBots.insert(botGuid);
    }

    void GoalSystem::ResumeGoal(uint64 botGuid, uint32 goalId)
    {
        std::unique_lock<std::shared_mutex> lock(m_goalsMutex);

        auto it = m_botGoals.find(botGuid);
        if (it == m_botGoals.end())
            return;

        for (auto& goal : it->second)
        {
            if (goal.goalId == goalId && goal.status == GoalStatus::PAUSED)
            {
                goal.status = GoalStatus::ACTIVE;
                goal.updatedAt = NowMs();
                break;
            }
        }

        std::lock_guard<std::mutex> dirtyLock(m_dirtyMutex);
        m_dirtyBots.insert(botGuid);
    }

    // =========================================================================
    // GOAL QUERIES
    // =========================================================================

    std::vector<BotGoal> GoalSystem::GetBotGoals(uint64 botGuid) const
    {
        std::shared_lock<std::shared_mutex> lock(m_goalsMutex);

        auto it = m_botGoals.find(botGuid);
        if (it == m_botGoals.end())
            return {};

        return it->second;
    }

    std::vector<BotGoal> GoalSystem::GetActiveGoals(uint64 botGuid) const
    {
        std::shared_lock<std::shared_mutex> lock(m_goalsMutex);

        auto it = m_botGoals.find(botGuid);
        if (it == m_botGoals.end())
            return {};

        std::vector<BotGoal> activeGoals;
        for (const auto& goal : it->second)
        {
            if (goal.status == GoalStatus::ACTIVE)
                activeGoals.push_back(goal);
        }

        return activeGoals;
    }

    std::optional<BotGoal> GoalSystem::GetGoal(uint64 botGuid, uint32 goalId) const
    {
        std::shared_lock<std::shared_mutex> lock(m_goalsMutex);

        auto it = m_botGoals.find(botGuid);
        if (it == m_botGoals.end())
            return std::nullopt;

        for (const auto& goal : it->second)
        {
            if (goal.goalId == goalId)
                return goal;  // Return copy
        }

        return std::nullopt;
    }

    std::optional<BotGoal> GoalSystem::GetHighestPriorityGoal(uint64 botGuid) const
    {
        std::shared_lock<std::shared_mutex> lock(m_goalsMutex);

        auto it = m_botGoals.find(botGuid);
        if (it == m_botGoals.end())
            return std::nullopt;

        const BotGoal* highest = nullptr;
        for (const auto& goal : it->second)
        {
            if (goal.status != GoalStatus::ACTIVE)
                continue;

            if (!highest || static_cast<uint8>(goal.priority) < static_cast<uint8>(highest->priority))
                highest = &goal;
        }

        if (highest)
            return *highest;  // Return copy

        return std::nullopt;
    }

    uint32 GoalSystem::GetGoalCount(uint64 botGuid) const
    {
        std::shared_lock<std::shared_mutex> lock(m_goalsMutex);

        auto it = m_botGoals.find(botGuid);
        if (it == m_botGoals.end())
            return 0;

        return it->second.size();
    }

    uint32 GoalSystem::GetActiveGoalCount(uint64 botGuid) const
    {
        std::shared_lock<std::shared_mutex> lock(m_goalsMutex);

        auto it = m_botGoals.find(botGuid);
        if (it == m_botGoals.end())
            return 0;

        uint32 count = 0;
        for (const auto& goal : it->second)
        {
            if (goal.status == GoalStatus::ACTIVE)
                count++;
        }

        return count;
    }

    // =========================================================================
    // ITEM ANALYSIS
    // =========================================================================

    std::vector<ItemUpgradeCandidate> GoalSystem::FindItemUpgrades(Player* bot, uint32 maxResults)
    {
        std::vector<ItemUpgradeCandidate> upgrades;

        if (!bot)
            return upgrades;

        uint32 botLevel = bot->GetLevel();

        // Check each equipment slot
        for (uint8 slot = EQUIPMENT_SLOT_HEAD; slot < EQUIPMENT_SLOT_END; ++slot)
        {
            Item* currentItem = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, slot);
            uint32 currentILevel = 0;

            if (currentItem)
            {
                const ItemTemplate* tmpl = currentItem->GetTemplate();
                if (tmpl)
                    currentILevel = tmpl->ItemLevel;
            }

            // Find upgrade - this is simplified, full implementation would query item_template
            // For now, suggest generic upgrades based on level
            uint32 targetILevel = botLevel + 5;  // Simple formula

            if (targetILevel > currentILevel + 5)  // At least 5 iLevel upgrade
            {
                ItemUpgradeCandidate upgrade;
                upgrade.slot = slot;
                upgrade.currentItemLevel = currentILevel;
                upgrade.itemLevel = targetILevel;
                upgrade.itemName = "Upgrade for slot " + std::to_string(slot);
                upgrade.upgradeScore = (targetILevel - currentILevel) / 20.0f;  // Normalize

                upgrades.push_back(upgrade);
            }

            if (upgrades.size() >= maxResults)
                break;
        }

        // Sort by upgrade score
        std::sort(upgrades.begin(), upgrades.end(),
            [](const ItemUpgradeCandidate& a, const ItemUpgradeCandidate& b) {
                return a.upgradeScore > b.upgradeScore;
            });

        return upgrades;
    }

    float GoalSystem::CalculateItemUpgradeScore(Player* bot, uint32 itemId, uint8 slot)
    {
        if (!bot)
            return 0.0f;

        const ItemTemplate* newItem = sObjectMgr->GetItemTemplate(itemId);
        if (!newItem)
            return 0.0f;

        Item* currentItem = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, slot);
        uint32 currentILevel = 0;

        if (currentItem)
        {
            const ItemTemplate* currentTmpl = currentItem->GetTemplate();
            if (currentTmpl)
                currentILevel = currentTmpl->ItemLevel;
        }

        float score = (newItem->ItemLevel - currentILevel) / 20.0f;
        return std::max(0.0f, std::min(1.0f, score));
    }

    // =========================================================================
    // REPUTATION ANALYSIS
    // =========================================================================

    std::vector<ReputationTarget> GoalSystem::FindReputationTargets(Player* bot, uint32 maxResults)
    {
        std::vector<ReputationTarget> targets;

        if (!bot)
            return targets;

        // Get important factions based on bot level and faction
        // This is simplified - full implementation would check npc_vendor for faction rewards

        std::vector<std::pair<uint32, std::string>> importantFactions;

        if (bot->GetTeamId() == TEAM_ALLIANCE)
        {
            importantFactions = {
                {72, "Stormwind"},
                {47, "Ironforge"},
                {69, "Darnassus"},
                {930, "Exodar"},
                {1037, "Alliance Vanguard"}
            };
        }
        else
        {
            importantFactions = {
                {76, "Orgrimmar"},
                {68, "Undercity"},
                {81, "Thunder Bluff"},
                {911, "Silvermoon City"},
                {1052, "Horde Expedition"}
            };
        }

        for (const auto& [factionId, factionName] : importantFactions)
        {
            ReputationTarget target;
            target.factionId = factionId;
            target.factionName = factionName;
            target.currentStanding = 0;  // Would get from ReputationMgr
            target.targetStanding = 42000;  // Exalted
            target.currentRank = "Neutral";
            target.targetRank = "Exalted";
            target.priority = 0.5f;

            targets.push_back(target);

            if (targets.size() >= maxResults)
                break;
        }

        return targets;
    }

    bool GoalSystem::HasVendorRewards(uint32 factionId, uint32 minReputation)
    {
        // Would query npc_vendor joined with faction requirements
        // Simplified: assume all factions have rewards
        return true;
    }

    // =========================================================================
    // PERSISTENCE
    // =========================================================================

    void GoalSystem::LoadFromDatabase()
    {
        QueryResult result = CharacterDatabase.Query(
            "SELECT bot_guid, goals_json FROM alive_bot_goals"
        );

        if (!result)
            return;

        std::unique_lock<std::shared_mutex> lock(m_goalsMutex);

        do
        {
            Field* fields = result->Fetch();
            uint64 botGuid = fields[0].Get<uint64>();
            std::string goalsJson = fields[1].Get<std::string>();

            auto goals = DeserializeGoals(goalsJson);
            if (!goals.empty())
            {
                m_botGoals[botGuid] = goals;

                // Update next goal ID
                for (const auto& goal : goals)
                {
                    if (goal.goalId >= m_nextGoalId)
                        m_nextGoalId = goal.goalId + 1;
                }
            }
        }
        while (result->NextRow());

        GOAL_LOG_INFO("Loaded goals for %zu bots from database", m_botGoals.size());
    }

    void GoalSystem::SaveToDatabase()
    {
        std::set<uint64> botsToSave;
        {
            std::lock_guard<std::mutex> lock(m_dirtyMutex);
            botsToSave = std::move(m_dirtyBots);
            m_dirtyBots.clear();
        }

        if (botsToSave.empty())
            return;

        CharacterDatabaseTransaction trans = CharacterDatabase.BeginTransaction();

        for (uint64 botGuid : botsToSave)
        {
            std::vector<BotGoal> goals;
            {
                std::shared_lock<std::shared_mutex> lock(m_goalsMutex);
                auto it = m_botGoals.find(botGuid);
                if (it != m_botGoals.end())
                    goals = it->second;
            }

            std::string goalsJson = SerializeGoals(goals);

            // Use direct query since we can't add custom prepared statements to core
            std::string escapedJson = goalsJson;
            // Simple escape for single quotes
            size_t pos = 0;
            while ((pos = escapedJson.find('\'', pos)) != std::string::npos)
            {
                escapedJson.replace(pos, 1, "''");
                pos += 2;
            }

            trans->Append(Acore::StringFormat(
                "REPLACE INTO alive_bot_goals (bot_guid, goals_json) VALUES ({}, '{}')",
                botGuid, escapedJson));
        }

        CharacterDatabase.CommitTransaction(trans);
        GOAL_LOG_DEBUG("Saved goals for %zu bots to database", botsToSave.size());
    }

    void GoalSystem::SaveBotGoals(uint64 botGuid)
    {
        std::lock_guard<std::mutex> lock(m_dirtyMutex);
        m_dirtyBots.insert(botGuid);
    }

    void GoalSystem::LoadBotGoals(uint64 botGuid)
    {
        QueryResult result = CharacterDatabase.Query(
            "SELECT goals_json FROM alive_bot_goals WHERE bot_guid = {}",
            botGuid
        );

        if (!result)
            return;

        Field* fields = result->Fetch();
        std::string goalsJson = fields[0].Get<std::string>();

        auto goals = DeserializeGoals(goalsJson);
        if (!goals.empty())
        {
            std::unique_lock<std::shared_mutex> lock(m_goalsMutex);
            m_botGoals[botGuid] = goals;
        }
    }

    // =========================================================================
    // UPDATE
    // =========================================================================

    void GoalSystem::Update(uint32 diff)
    {
        if (!m_enabled)
            return;

        m_updateTimer += diff;
        m_persistTimer += diff;

        // Periodic persistence
        if (m_persistTimer >= m_persistIntervalMs)
        {
            m_persistTimer = 0;
            SaveToDatabase();
        }
    }

    // =========================================================================
    // STATS
    // =========================================================================

    uint32 GoalSystem::GetTotalGoals() const
    {
        std::shared_lock<std::shared_mutex> lock(m_goalsMutex);

        uint32 total = 0;
        for (const auto& [guid, goals] : m_botGoals)
            total += goals.size();

        return total;
    }

    uint32 GoalSystem::GetActiveGoalsCount() const
    {
        std::shared_lock<std::shared_mutex> lock(m_goalsMutex);

        uint32 count = 0;
        for (const auto& [guid, goals] : m_botGoals)
        {
            for (const auto& goal : goals)
            {
                if (goal.status == GoalStatus::ACTIVE)
                    count++;
            }
        }

        return count;
    }

    uint32 GoalSystem::GetCompletedGoalsCount() const
    {
        std::shared_lock<std::shared_mutex> lock(m_goalsMutex);

        uint32 count = 0;
        for (const auto& [guid, goals] : m_botGoals)
        {
            for (const auto& goal : goals)
            {
                if (goal.status == GoalStatus::COMPLETED)
                    count++;
            }
        }

        return count;
    }

    // =========================================================================
    // PRIVATE HELPERS
    // =========================================================================

    uint32 GoalSystem::GenerateGoalId()
    {
        return m_nextGoalId++;
    }

    GoalPriority GoalSystem::CalculatePriority(GoalType type, float upgradeScore, const BotPersonality& personality)
    {
        float priorityScore = upgradeScore;

        // Adjust based on personality
        switch (type)
        {
            case GoalType::ITEM:
                priorityScore += personality.greed * 0.2f;
                break;
            case GoalType::PVP:
                priorityScore += personality.aggression * 0.3f;
                break;
            case GoalType::SOCIAL:
                priorityScore += personality.sociability * 0.2f;
                break;
            default:
                break;
        }

        if (priorityScore > 0.8f)
            return GoalPriority::CRITICAL;
        if (priorityScore > 0.6f)
            return GoalPriority::HIGH;
        if (priorityScore > 0.4f)
            return GoalPriority::MEDIUM;
        if (priorityScore > 0.2f)
            return GoalPriority::LOW;

        return GoalPriority::PASSIVE;
    }

    bool GoalSystem::HasSimilarGoal(uint64 botGuid, GoalType type, uint32 targetId) const
    {
        std::shared_lock<std::shared_mutex> lock(m_goalsMutex);

        auto it = m_botGoals.find(botGuid);
        if (it == m_botGoals.end())
            return false;

        for (const auto& goal : it->second)
        {
            if (goal.type == type && goal.targetId == targetId &&
                (goal.status == GoalStatus::ACTIVE || goal.status == GoalStatus::PAUSED))
                return true;
        }

        return false;
    }

    void GoalSystem::PruneGoals(uint64 botGuid)
    {
        // Note: Caller must hold write lock
        auto it = m_botGoals.find(botGuid);
        if (it == m_botGoals.end())
            return;

        auto& goals = it->second;

        // Remove completed/abandoned goals older than 24 hours
        uint64 cutoffTime = NowMs() - (24 * 60 * 60 * 1000);

        goals.erase(
            std::remove_if(goals.begin(), goals.end(),
                [cutoffTime](const BotGoal& g) {
                    return (g.status == GoalStatus::COMPLETED || g.status == GoalStatus::ABANDONED) &&
                           g.updatedAt < cutoffTime;
                }),
            goals.end()
        );
    }

    // Helper to escape JSON strings
    static std::string EscapeJsonString(const std::string& input)
    {
        std::ostringstream ss;
        for (char c : input)
        {
            switch (c)
            {
                case '"':  ss << "\\\""; break;
                case '\\': ss << "\\\\"; break;
                case '\b': ss << "\\b"; break;
                case '\f': ss << "\\f"; break;
                case '\n': ss << "\\n"; break;
                case '\r': ss << "\\r"; break;
                case '\t': ss << "\\t"; break;
                default:
                    if (static_cast<unsigned char>(c) < 0x20)
                        ss << "\\u" << std::hex << std::setw(4) << std::setfill('0') << static_cast<int>(c);
                    else
                        ss << c;
            }
        }
        return ss.str();
    }

    std::string GoalSystem::SerializeGoals(const std::vector<BotGoal>& goals) const
    {
        std::ostringstream ss;
        ss << "[";

        bool first = true;
        for (const auto& goal : goals)
        {
            if (!first) ss << ",";
            first = false;

            ss << "{"
               << "\"id\":" << goal.goalId << ","
               << "\"type\":" << static_cast<int>(goal.type) << ","
               << "\"priority\":" << static_cast<int>(goal.priority) << ","
               << "\"status\":" << static_cast<int>(goal.status) << ","
               << "\"targetId\":" << goal.targetId << ","
               << "\"targetName\":\"" << EscapeJsonString(goal.targetName) << "\","
               << "\"progress\":" << goal.progress << ","
               << "\"progressCurrent\":" << goal.progressCurrent << ","
               << "\"progressTarget\":" << goal.progressTarget << ","
               << "\"createdAt\":" << goal.createdAt << ","
               << "\"updatedAt\":" << goal.updatedAt << ","
               << "\"completedAt\":" << goal.completedAt << ","
               << "\"reason\":\"" << EscapeJsonString(goal.reason) << "\""
               << "}";
        }

        ss << "]";
        return ss.str();
    }

    // Simple JSON number parser
    static uint64 ParseJsonNumber(const std::string& json, size_t& pos)
    {
        size_t start = pos;
        while (pos < json.size() && (std::isdigit(json[pos]) || json[pos] == '-' || json[pos] == '.'))
            pos++;
        return std::stoull(json.substr(start, pos - start));
    }

    // Simple JSON string parser
    static std::string ParseJsonString(const std::string& json, size_t& pos)
    {
        if (pos >= json.size() || json[pos] != '"')
            return "";
        pos++;  // Skip opening quote

        std::string result;
        while (pos < json.size() && json[pos] != '"')
        {
            if (json[pos] == '\\' && pos + 1 < json.size())
            {
                pos++;
                switch (json[pos])
                {
                    case '"':  result += '"'; break;
                    case '\\': result += '\\'; break;
                    case 'b':  result += '\b'; break;
                    case 'f':  result += '\f'; break;
                    case 'n':  result += '\n'; break;
                    case 'r':  result += '\r'; break;
                    case 't':  result += '\t'; break;
                    default:   result += json[pos]; break;
                }
            }
            else
            {
                result += json[pos];
            }
            pos++;
        }
        if (pos < json.size()) pos++;  // Skip closing quote
        return result;
    }

    // Skip whitespace
    static void SkipWhitespace(const std::string& json, size_t& pos)
    {
        while (pos < json.size() && std::isspace(json[pos]))
            pos++;
    }

    std::vector<BotGoal> GoalSystem::DeserializeGoals(const std::string& json)
    {
        std::vector<BotGoal> goals;

        if (json.empty() || json[0] != '[')
            return goals;

        size_t pos = 1;  // Skip '['
        SkipWhitespace(json, pos);

        while (pos < json.size() && json[pos] != ']')
        {
            if (json[pos] == ',')
            {
                pos++;
                SkipWhitespace(json, pos);
                continue;
            }

            if (json[pos] != '{')
                break;

            pos++;  // Skip '{'
            BotGoal goal;

            while (pos < json.size() && json[pos] != '}')
            {
                SkipWhitespace(json, pos);
                if (json[pos] == ',') { pos++; continue; }
                if (json[pos] == '}') break;

                // Parse key
                std::string key = ParseJsonString(json, pos);
                SkipWhitespace(json, pos);
                if (pos < json.size() && json[pos] == ':') pos++;
                SkipWhitespace(json, pos);

                // Parse value based on key
                if (key == "id") goal.goalId = static_cast<uint32>(ParseJsonNumber(json, pos));
                else if (key == "type") goal.type = static_cast<GoalType>(ParseJsonNumber(json, pos));
                else if (key == "priority") goal.priority = static_cast<GoalPriority>(ParseJsonNumber(json, pos));
                else if (key == "status") goal.status = static_cast<GoalStatus>(ParseJsonNumber(json, pos));
                else if (key == "targetId") goal.targetId = static_cast<uint32>(ParseJsonNumber(json, pos));
                else if (key == "targetName") goal.targetName = ParseJsonString(json, pos);
                else if (key == "progress") { ParseJsonNumber(json, pos); }  // Skip float parsing for now
                else if (key == "progressCurrent") goal.progressCurrent = static_cast<uint32>(ParseJsonNumber(json, pos));
                else if (key == "progressTarget") goal.progressTarget = static_cast<uint32>(ParseJsonNumber(json, pos));
                else if (key == "createdAt") goal.createdAt = ParseJsonNumber(json, pos);
                else if (key == "updatedAt") goal.updatedAt = ParseJsonNumber(json, pos);
                else if (key == "completedAt") goal.completedAt = ParseJsonNumber(json, pos);
                else if (key == "reason") goal.reason = ParseJsonString(json, pos);
                else
                {
                    // Skip unknown value
                    if (json[pos] == '"') ParseJsonString(json, pos);
                    else ParseJsonNumber(json, pos);
                }

                SkipWhitespace(json, pos);
            }

            if (pos < json.size() && json[pos] == '}') pos++;
            SkipWhitespace(json, pos);

            // Recalculate progress from current/target
            if (goal.progressTarget > 0)
                goal.progress = static_cast<float>(goal.progressCurrent) / static_cast<float>(goal.progressTarget);

            goals.push_back(goal);
        }

        return goals;
    }

} // namespace AliveWorld
