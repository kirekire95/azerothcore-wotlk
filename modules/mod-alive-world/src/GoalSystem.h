/*
 * mod-alive-world - Goal System
 * Copyright (C) 2025 EC
 *
 * Manages bot goals - what they want to achieve based on personality,
 * game data (items, reputation, achievements), and current state.
 *
 * Bots pursue goals intelligently:
 *   - Gear upgrades based on item_template.ItemLevel
 *   - Profession recipes from skill_discovery_template
 *   - Reputation targets via faction_dbc + npc_vendor
 *   - Achievement progress from achievement_criteria_data
 *   - Personality-weighted selection (greedy bots prioritize gold)
 */

#ifndef ALIVE_WORLD_GOAL_SYSTEM_H
#define ALIVE_WORLD_GOAL_SYSTEM_H

#include "AliveWorldTypes.h"

#include <atomic>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <unordered_map>
#include <vector>

class Player;

namespace AliveWorld
{
    class BotMemoryManager;
    class WorldStateManager;

    // Goal selection weights based on personality traits
    struct GoalWeights
    {
        float itemWeight;           // Influenced by greed
        float reputationWeight;     // Influenced by loyalty, sociability
        float achievementWeight;    // Influenced by curiosity
        float exploreWeight;        // Influenced by curiosity
        float professionWeight;     // Influenced by greed, caution
        float goldWeight;           // Influenced by greed
        float levelWeight;          // Base weight, always relevant
        float pvpWeight;            // Influenced by aggression
        float socialWeight;         // Influenced by sociability

        GoalWeights()
            : itemWeight(1.0f), reputationWeight(0.5f), achievementWeight(0.3f)
            , exploreWeight(0.4f), professionWeight(0.4f), goldWeight(0.6f)
            , levelWeight(0.8f), pvpWeight(0.3f), socialWeight(0.4f)
        {}
    };

    // Item upgrade candidate
    struct ItemUpgradeCandidate
    {
        uint32 itemId;
        std::string itemName;
        uint32 itemLevel;
        uint32 currentItemLevel;    // What bot currently has in that slot
        uint8 slot;                 // Equipment slot
        float upgradeScore;         // How much of an upgrade this is

        ItemUpgradeCandidate()
            : itemId(0), itemLevel(0), currentItemLevel(0), slot(0), upgradeScore(0.0f)
        {}
    };

    // Reputation target
    struct ReputationTarget
    {
        uint32 factionId;
        std::string factionName;
        uint32 currentStanding;     // Current reputation value
        uint32 targetStanding;      // Target reputation value
        std::string currentRank;    // "Friendly", "Honored", etc.
        std::string targetRank;     // What rank to reach
        float priority;             // How important this faction is

        ReputationTarget()
            : factionId(0), currentStanding(0), targetStanding(0), priority(0.0f)
        {}
    };

    class GoalSystem
    {
    public:
        static GoalSystem* Instance()
        {
            static GoalSystem instance;
            return &instance;
        }

        GoalSystem();
        ~GoalSystem();

        // Initialization
        void Initialize(BotMemoryManager* memoryManager, WorldStateManager* worldStateManager);
        void LoadConfig();

        // Goal selection - main entry point
        std::vector<BotGoal> SelectGoalsForBot(uint64 botGuid, Player* bot);

        // Specific goal generators
        std::vector<BotGoal> GenerateItemGoals(uint64 botGuid, Player* bot, const GoalWeights& weights);
        std::vector<BotGoal> GenerateReputationGoals(uint64 botGuid, Player* bot, const GoalWeights& weights);
        std::vector<BotGoal> GenerateAchievementGoals(uint64 botGuid, Player* bot, const GoalWeights& weights);
        std::vector<BotGoal> GenerateExploreGoals(uint64 botGuid, Player* bot, const GoalWeights& weights);
        std::vector<BotGoal> GenerateProfessionGoals(uint64 botGuid, Player* bot, const GoalWeights& weights);
        std::vector<BotGoal> GenerateGoldGoals(uint64 botGuid, Player* bot, const GoalWeights& weights);
        std::vector<BotGoal> GenerateLevelGoals(uint64 botGuid, Player* bot, const GoalWeights& weights);
        std::vector<BotGoal> GeneratePvPGoals(uint64 botGuid, Player* bot, const GoalWeights& weights);
        std::vector<BotGoal> GenerateSocialGoals(uint64 botGuid, Player* bot, const GoalWeights& weights);

        // Goal management
        void AddGoal(uint64 botGuid, const BotGoal& goal);
        void RemoveGoal(uint64 botGuid, uint32 goalId);
        void UpdateGoalProgress(uint64 botGuid, uint32 goalId, uint32 current, uint32 target);
        void CompleteGoal(uint64 botGuid, uint32 goalId);
        void AbandonGoal(uint64 botGuid, uint32 goalId);
        void PauseGoal(uint64 botGuid, uint32 goalId);
        void ResumeGoal(uint64 botGuid, uint32 goalId);

        // Goal queries (return by value to avoid dangling pointers)
        std::vector<BotGoal> GetBotGoals(uint64 botGuid) const;
        std::vector<BotGoal> GetActiveGoals(uint64 botGuid) const;
        std::optional<BotGoal> GetGoal(uint64 botGuid, uint32 goalId) const;
        std::optional<BotGoal> GetHighestPriorityGoal(uint64 botGuid) const;
        uint32 GetGoalCount(uint64 botGuid) const;
        uint32 GetActiveGoalCount(uint64 botGuid) const;

        // Personality-based weight calculation
        GoalWeights CalculateWeightsFromPersonality(const BotPersonality& personality);

        // Item analysis
        std::vector<ItemUpgradeCandidate> FindItemUpgrades(Player* bot, uint32 maxResults = 10);
        float CalculateItemUpgradeScore(Player* bot, uint32 itemId, uint8 slot);

        // Reputation analysis
        std::vector<ReputationTarget> FindReputationTargets(Player* bot, uint32 maxResults = 5);
        bool HasVendorRewards(uint32 factionId, uint32 minReputation);

        // Persistence
        void LoadFromDatabase();
        void SaveToDatabase();
        void SaveBotGoals(uint64 botGuid);
        void LoadBotGoals(uint64 botGuid);

        // Update (called periodically)
        void Update(uint32 diff);

        // Stats
        uint32 GetTotalGoals() const;
        uint32 GetActiveGoalsCount() const;
        uint32 GetCompletedGoalsCount() const;

        // Settings
        bool IsEnabled() const { return m_enabled; }
        void SetEnabled(bool enabled) { m_enabled = enabled; }

    private:
        // Generate unique goal ID
        uint32 GenerateGoalId();

        // Calculate priority based on various factors
        GoalPriority CalculatePriority(GoalType type, float upgradeScore, const BotPersonality& personality);

        // Check if bot already has a similar goal
        bool HasSimilarGoal(uint64 botGuid, GoalType type, uint32 targetId) const;

        // Prune old/completed goals
        void PruneGoals(uint64 botGuid);

        // Serialize/deserialize for database
        std::string SerializeGoals(const std::vector<BotGoal>& goals) const;
        std::vector<BotGoal> DeserializeGoals(const std::string& json);

        // Configuration
        bool m_enabled;
        uint32 m_maxGoalsPerBot;
        uint32 m_maxActiveGoals;
        uint32 m_goalRefreshIntervalMs;
        uint32 m_persistIntervalMs;
        bool m_autoSelectGoals;

        // Goal storage: botGuid -> list of goals
        std::unordered_map<uint64, std::vector<BotGoal>> m_botGoals;
        mutable std::shared_mutex m_goalsMutex;

        // Goal ID counter
        std::atomic<uint32> m_nextGoalId;

        // Dirty tracking for persistence
        std::set<uint64> m_dirtyBots;
        std::mutex m_dirtyMutex;

        // References
        BotMemoryManager* m_memoryManager;
        WorldStateManager* m_worldStateManager;

        // Timing
        uint32 m_updateTimer;
        uint32 m_persistTimer;
        uint64 m_lastUpdateTime;
    };

} // namespace AliveWorld

// Global accessor
#define sGoalSystem AliveWorld::GoalSystem::Instance()

#endif // ALIVE_WORLD_GOAL_SYSTEM_H
