/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>
 * Released under GNU AGPL v3 license: https://github.com/azerothcore/azerothcore-wotlk/blob/master/LICENSE-AGPL3
 *
 * EC Bot Goal System - Goal Manager
 *
 * Core logic for goal selection, evaluation, personality weighting, and progress tracking.
 */

#ifndef _EC_BOT_GOAL_MGR_H
#define _EC_BOT_GOAL_MGR_H

#include "Goal.h"
#include "GoalRepository.h"
#include "BotPersonality.h"
#include "SharedDefines.h"
#include <vector>
#include <map>
#include <mutex>
#include <list>
#include <optional>

class Player;
class ItemTemplate;

/**
 * GoalMgr - Singleton manager for bot goals
 *
 * Responsibilities:
 * - Evaluate potential goals based on game data
 * - Apply personality weighting to goals
 * - Track goal progress via event hooks
 * - Manage goal lifecycle (create, complete, fail, abandon)
 */
class GoalMgr
{
public:
    static GoalMgr* instance();

    // Initialize manager (called at server startup)
    void Initialize();

    // Periodic cleanup of old completed goals
    void Update(uint32_t diff);

    // ========================================================================
    // Goal Selection
    // ========================================================================

    /**
     * Select new goals for a bot based on current state and personality
     * @param bot Player pointer
     * @param maxGoals Maximum number of goals to create
     */
    void SelectGoalsForBot(Player* bot, uint8_t maxGoals = 3);

    /**
     * Re-evaluate and re-prioritize existing goals
     * @param bot Player pointer
     */
    void RefreshGoals(Player* bot);

    // ========================================================================
    // Goal Evaluation (find potential goals)
    // ========================================================================

    /**
     * Find gear upgrade goals (items better than currently equipped)
     * @param bot Player pointer
     * @return Vector of potential item goals
     */
    std::vector<Goal> EvaluateItemGoals(Player* bot);

    /**
     * Find reputation goals (factions with valuable rewards)
     * @param bot Player pointer
     * @return Vector of potential reputation goals
     */
    std::vector<Goal> EvaluateReputationGoals(Player* bot);

    /**
     * Find profession goals (recipes to learn, skills to level)
     * @param bot Player pointer
     * @return Vector of potential profession goals
     */
    std::vector<Goal> EvaluateProfessionGoals(Player* bot);

    /**
     * Find exploration goals (zones to discover)
     * @param bot Player pointer
     * @return Vector of potential explore goals
     */
    std::vector<Goal> EvaluateExploreGoals(Player* bot);

    /**
     * Find gold accumulation goals
     * @param bot Player pointer
     * @return Vector of potential gold goals
     */
    std::vector<Goal> EvaluateGoldGoals(Player* bot);

    // ========================================================================
    // Personality Weighting
    // ========================================================================

    /**
     * Calculate personality weight for a goal
     * @param goal Goal to weight
     * @param personality Bot's personality traits
     * @return Weight multiplier (0.1 - 2.0)
     */
    float CalculatePersonalityWeight(const Goal& goal, const BotPersonality& personality);

    /**
     * Get personality for a bot (from BotMemoryManager)
     * @param bot Player pointer
     * @return BotPersonality struct
     */
    BotPersonality GetBotPersonality(Player* bot);

    // ========================================================================
    // Progress Tracking (event hooks)
    // ========================================================================

    /**
     * Called when bot loots or acquires an item
     * @param bot Player pointer
     * @param itemId Item entry ID
     */
    void OnItemLooted(Player* bot, uint32_t itemId);

    /**
     * Called when bot's reputation changes
     * @param bot Player pointer
     * @param factionId Faction ID
     * @param change Amount of reputation gained/lost
     */
    void OnReputationChange(Player* bot, uint32_t factionId, int32_t change);

    /**
     * Called when bot discovers a new zone
     * @param bot Player pointer
     * @param zoneId Zone ID
     */
    void OnZoneDiscovered(Player* bot, uint32_t zoneId);

    /**
     * Called when bot's gold changes
     * @param bot Player pointer
     * @param delta Amount of gold gained/lost (in copper)
     */
    void OnGoldChange(Player* bot, int32_t delta);

    /**
     * Called when bot learns a skill or recipe
     * @param bot Player pointer
     * @param skillId Skill ID
     * @param newValue New skill value
     */
    void OnSkillChange(Player* bot, uint32_t skillId, uint32_t newValue);

    // ========================================================================
    // Goal Lifecycle
    // ========================================================================

    /**
     * Get the current active goal for a bot (highest priority)
     * @param bot Player pointer
     * @return Active goal or std::nullopt if none
     */
    std::optional<Goal> GetActiveGoal(Player* bot);

    /**
     * Get all active goals for a bot
     * @param bot Player pointer
     * @return Vector of active goals
     */
    std::vector<Goal> GetActiveGoals(Player* bot);

    /**
     * Activate a pending goal
     * @param goalId Goal database ID
     */
    void ActivateGoal(uint32_t goalId);

    /**
     * Mark goal as completed
     * @param goalId Goal database ID
     */
    void CompleteGoal(uint32_t goalId);

    /**
     * Mark goal as failed
     * @param goalId Goal database ID
     * @param reason Failure reason for logging
     */
    void FailGoal(uint32_t goalId, const std::string& reason);

    /**
     * Abandon a goal (bot chose to stop pursuing)
     * @param goalId Goal database ID
     */
    void AbandonGoal(uint32_t goalId);

    // ========================================================================
    // Helpers
    // ========================================================================

    /**
     * Check if an item is an upgrade for the bot
     * @param bot Player pointer
     * @param itemId Item entry ID
     * @return true if item is better than current gear
     */
    bool IsItemUpgrade(Player* bot, uint32_t itemId);

    /**
     * Get upgrade score for an item
     * @param bot Player pointer
     * @param itemId Item entry ID
     * @return Score (higher = better upgrade)
     */
    float GetItemUpgradeScore(Player* bot, uint32_t itemId);

    /**
     * Get item name for display
     * @param itemId Item entry ID
     * @return Item name or "Unknown"
     */
    std::string GetItemName(uint32_t itemId);

    /**
     * Get faction name for display
     * @param factionId Faction ID
     * @return Faction name or "Unknown"
     */
    std::string GetFactionName(uint32_t factionId);

    /**
     * Get zone name for display
     * @param zoneId Zone ID
     * @return Zone name or "Unknown"
     */
    std::string GetZoneName(uint32_t zoneId);

    /**
     * Get skill name for display
     * @param skillId Skill ID
     * @return Skill name or "Unknown"
     */
    std::string GetSkillName(uint32_t skillId);

    /**
     * Check if goal system is enabled in config
     * @return true if enabled
     */
    bool IsEnabled() const { return m_enabled; }

    /**
     * Get config value for max active goals
     * @return Max goals per bot
     */
    uint8_t GetMaxActiveGoals() const { return m_maxActiveGoals; }

private:
    GoalMgr();
    ~GoalMgr() = default;
    GoalMgr(const GoalMgr&) = delete;
    GoalMgr& operator=(const GoalMgr&) = delete;

    // Load config values
    void LoadConfig();

    // Check if goal target is still valid
    bool IsGoalValid(const Goal& goal);

    // Reputation helpers
    const char* GetRankName(ReputationRank rank);
    int32_t GetReputationForRank(ReputationRank rank);

    // LRU cache management
    void UpdateCacheAccess(uint32_t botGuid);

    // In-memory cache of active goals per bot (for fast lookups)
    std::map<uint32_t, std::vector<Goal>> m_activeGoalsCache;
    std::list<uint32_t> m_cacheAccessOrder;  // For LRU eviction
    std::mutex m_cacheMutex;
    static const size_t MAX_CACHE_SIZE = 1000;  // Max bots to cache

    // Config values
    bool m_enabled;
    uint8_t m_maxActiveGoals;
    uint32_t m_refreshIntervalSeconds;
    float m_itemUpgradeThreshold;
    bool m_personalityWeightEnabled;
    uint32_t m_goalRetentionSeconds;  // How long to keep completed/failed goals

    // Update timer
    uint32_t m_cleanupTimer;
    static const uint32_t CLEANUP_INTERVAL = 60 * 60 * 1000; // 1 hour

    // Personality persistence (LRU cache, same pattern as goals cache)
    BotPersonality LoadPersonalityFromDB(uint32_t botGuid);
    void SavePersonalityToDB(uint32_t botGuid, const BotPersonality& personality);

    std::map<uint32_t, BotPersonality> m_personalityCache;
    std::list<uint32_t> m_personalityCacheOrder;
    std::mutex m_personalityCacheMutex;
    static const size_t MAX_PERSONALITY_CACHE_SIZE = 2000;

    // Goal evaluation backoff — prevents spamming SelectGoalsForBot when no goals are viable
    std::map<uint32_t, time_t> m_emptyEvalBackoff;  // botGuid → timestamp of last empty eval
    static const int EMPTY_EVAL_BACKOFF_SECONDS = 300;  // 5 minutes

public:
    bool IsInGoalEvalBackoff(uint32_t botGuid) const;
};

#define sGoalMgr GoalMgr::instance()

#endif // _EC_BOT_GOAL_MGR_H
