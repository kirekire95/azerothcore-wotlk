/*
 * mod-alive-world - Goal Manager
 * Copyright (C) 2025 EC
 *
 * Manages bot goals - what they want to achieve (items, reputation, gold, etc.)
 * Goals are personality-driven and use WoW database tables for realistic targets.
 */

#ifndef ALIVE_WORLD_GOAL_MANAGER_H
#define ALIVE_WORLD_GOAL_MANAGER_H

#include "AliveWorldTypes.h"

#include <atomic>
#include <map>
#include <mutex>
#include <random>
#include <shared_mutex>
#include <unordered_map>
#include <vector>

namespace AliveWorld
{
    class BotMemoryManager;

    /**
     * Manages goals for all bots in the system.
     * Goals are generated based on personality, class, and level.
     * Progress is tracked and goals influence bot behavior.
     */
    class GoalManager
    {
    public:
        GoalManager();
        ~GoalManager() = default;

        // Initialization
        void Initialize(BotMemoryManager* memoryManager);
        void LoadGoalTemplatesFromDB();
        void LoadItemDataFromDB();
        void LoadFactionDataFromDB();

        // Goal Generation (personality-driven)
        void GenerateGoalsForBot(uint64 botGuid, uint8 botLevel, uint8 botClass, uint8 botRace);
        BotGoal GenerateItemGoal(uint64 botGuid, uint8 level, const BotPersonality& personality);
        BotGoal GenerateReputationGoal(uint64 botGuid, uint8 level, const BotPersonality& personality);
        BotGoal GenerateGoldGoal(uint64 botGuid, const BotPersonality& personality);
        BotGoal GenerateExploreGoal(uint64 botGuid, uint8 level, const BotPersonality& personality);
        BotGoal GeneratePvPGoal(uint64 botGuid, const BotPersonality& personality);
        BotGoal GenerateProfessionGoal(uint64 botGuid, uint8 level, const BotPersonality& personality);

        // Profession discovery data
        void LoadSkillDiscoveryData();
        std::vector<uint32> GetDiscoverableRecipes(uint32 skillId, uint16 skillValue) const;
        std::string GenerateProfessionGossip() const;

        // Goal Management
        uint32 AddGoal(const BotGoal& goal);
        bool RemoveGoal(uint32 goalId);
        bool UpdateGoalProgress(uint32 goalId, uint32 current, uint32 target);
        bool SetGoalStatus(uint32 goalId, GoalStatus status);
        void CleanupCompletedGoals(uint64 botGuid);

        // Goal Queries
        const BotGoal* GetGoal(uint32 goalId) const;
        std::vector<BotGoal> GetGoalsForBot(uint64 botGuid) const;
        std::vector<BotGoal> GetActiveGoalsForBot(uint64 botGuid) const;
        const BotGoal* GetHighestPriorityGoal(uint64 botGuid) const;
        uint32 GetGoalCountForBot(uint64 botGuid) const;
        uint32 GetActiveGoalCount() const;

        // Goal Progress Events
        void OnItemLooted(uint64 botGuid, uint32 itemId, uint32 count);
        void OnReputationGained(uint64 botGuid, uint32 factionId, int32 repChange);
        void OnGoldChanged(uint64 botGuid, uint32 newGoldAmount);
        void OnZoneEntered(uint64 botGuid, uint32 zoneId);
        void OnKill(uint64 botGuid, uint32 creatureEntry, bool isPvP);
        void OnLevelUp(uint64 botGuid, uint8 newLevel);

        // Database persistence
        void SaveGoalsToDB(uint64 botGuid);
        void LoadGoalsFromDB(uint64 botGuid);

        // Stats
        uint32 GetTotalGoalCount() const;
        uint32 GetCompletedGoalCount() const;
        float GetAverageProgress() const;

        // Configuration
        void SetMaxGoalsPerBot(uint32 max) { m_maxGoalsPerBot = max; }
        uint32 GetMaxGoalsPerBot() const { return m_maxGoalsPerBot; }

    private:
        // Generate a unique goal ID
        uint32 GenerateGoalId();

        // Helper: Get appropriate items for a level range
        std::vector<uint32> GetUpgradeItemsForLevel(uint8 level, uint8 playerClass, uint8 slot);
        std::vector<uint32> GetCraftingMatsForLevel(uint8 level);
        std::vector<uint32> GetConsumablesForLevel(uint8 level);
        std::vector<uint32> GetItemsByValueRange(uint32 minValue, uint32 maxValue) const;
        std::string GenerateGearGossip(uint8 level, uint8 playerClass) const;

        // Helper: Get appropriate factions for a level range
        std::vector<uint32> GetFactionsForLevel(uint8 level, uint8 playerRace);

        // Helper: Get exploration zones for level
        std::vector<uint32> GetExplorationZonesForLevel(uint8 level);

        // Cached item data (from item_template)
        struct ItemInfo
        {
            uint32 itemId;
            std::string name;
            uint8 quality;              // 0-6
            uint32 itemLevel;
            uint8 requiredLevel;
            uint8 inventoryType;        // Slot
            uint8 itemClass;            // Weapon, Armor, etc.
            uint8 itemSubClass;
            uint32 sellPrice;
            std::vector<uint8> allowedClasses;  // Classes that can use
        };
        std::unordered_map<uint32, ItemInfo> m_itemCache;

        // Get best upgrade item for a slot (uses m_itemCache)
        const ItemInfo* GetBestUpgradeForSlot(uint8 level, uint8 playerClass, uint8 slot) const;

        // Cached faction data (from Faction.dbc via faction_template)
        struct FactionInfo
        {
            uint32 factionId;
            std::string name;
            uint8 minLevel;             // Minimum level to care about this faction
            bool isHorde;
            bool isAlliance;
            bool isNeutral;
        };
        std::unordered_map<uint32, FactionInfo> m_factionCache;

        // Cached zone data (for exploration goals)
        struct ZoneInfo
        {
            uint32 zoneId;
            std::string name;
            uint8 minLevel;
            uint8 maxLevel;
            uint32 mapId;
        };
        std::unordered_map<uint32, ZoneInfo> m_zoneCache;

        // Skill discovery data (from skill_discovery_template)
        struct DiscoveryInfo
        {
            uint32 spellId;         // Discoverable spell/recipe
            uint32 reqSpell;        // Required base spell
            uint16 reqSkillValue;   // Skill level required
            float chance;           // Discovery chance
        };
        std::vector<DiscoveryInfo> m_discoveries;
        std::unordered_map<uint32, std::vector<uint32>> m_discoveriesBySkill;  // skillId -> spellIds

        // Profession gossip templates
        std::vector<std::string> m_professionGossipTemplates;

        // All active goals indexed by goalId
        std::unordered_map<uint32, BotGoal> m_goals;
        mutable std::shared_mutex m_goalsMutex;

        // Goals indexed by bot GUID for fast lookup
        std::unordered_map<uint64, std::vector<uint32>> m_botGoals;  // botGuid -> goalIds
        mutable std::shared_mutex m_botGoalsMutex;

        // Goal ID generator
        std::atomic<uint32> m_nextGoalId;

        // Configuration
        uint32 m_maxGoalsPerBot;

        // Statistics
        std::atomic<uint32> m_completedGoals;

        // Reference to memory manager for personality data
        BotMemoryManager* m_memoryManager;

        // Random number generator
        mutable std::mt19937 m_rng;
    };

} // namespace AliveWorld

#endif // ALIVE_WORLD_GOAL_MANAGER_H
