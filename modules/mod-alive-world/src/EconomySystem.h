/*
 * mod-alive-world - Economy System
 * Copyright (C) 2025 EC
 *
 * Manages bot economic awareness - understanding item values, farming
 * locations, vendor positions, and market dynamics.
 *
 * Features:
 *   - Valuable items index from item_template + creature_loot_template
 *   - Farming location recommendations by item type and bot level
 *   - Vendor location knowledge from npc_vendor + creature spawns
 *   - Simulated market dynamics with price fluctuations
 *   - Economic gossip generation ("Eternal Fire prices are crazy!")
 *   - Integration with GoalSystem for economic goals
 */

#ifndef ALIVE_WORLD_ECONOMY_SYSTEM_H
#define ALIVE_WORLD_ECONOMY_SYSTEM_H

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
    class GoalSystem;
    class GossipManager;
    class WorldStateManager;

    class EconomySystem
    {
    public:
        static EconomySystem* Instance()
        {
            static EconomySystem instance;
            return &instance;
        }

        EconomySystem();
        ~EconomySystem();

        // Initialization
        void Initialize(BotMemoryManager* memoryManager, GoalSystem* goalSystem,
                        GossipManager* gossipManager, WorldStateManager* worldStateManager);
        void LoadConfig();

        // Database loading (called during server startup)
        void LoadValuableItems();
        void LoadFarmingLocations();
        void LoadVendorData();

        // Item queries
        std::optional<ValuableItem> GetItem(uint32 itemId) const;
        std::vector<ValuableItem> GetItemsByCategory(ItemCategory category) const;
        std::vector<ValuableItem> GetItemsByValueTier(ItemValueTier tier) const;
        std::vector<ValuableItem> GetTopValueItems(uint32 count) const;
        ItemValueTier GetItemValueTier(uint32 sellPrice) const;
        ItemCategory ClassifyItem(uint32 itemId, uint32 itemClass, uint32 itemSubClass) const;

        // Farming location queries
        std::vector<FarmingLocation> GetFarmingLocationsForItem(uint32 itemId) const;
        std::vector<FarmingLocation> GetFarmingLocationsForCategory(ItemCategory category) const;
        std::vector<FarmingLocation> GetFarmingLocationsForLevel(uint8 playerLevel) const;
        std::optional<FarmingLocation> GetBestFarmingLocation(uint64 botGuid, Player* bot);

        // Creature drop queries (from creature_loot_template)
        std::vector<uint32> GetDropsFromCreature(uint32 creatureEntry) const;
        std::vector<uint32> GetCreaturesThatDrop(uint32 itemId) const;
        std::string GetDropInfo(uint32 itemId) const;  // "Eternal Fire drops from Flame Revenants"

        // Farming gossip (specific to loot knowledge)
        std::string GenerateFarmingGossip(uint32 itemId) const;
        std::string GenerateFarmingGossip(const FarmingLocation& location) const;
        void OnBotLootedValuable(uint64 botGuid, uint32 itemId, uint32 creatureEntry);

        // Vendor queries
        std::vector<VendorInfo> GetVendorsInZone(uint32 zoneId) const;
        std::optional<VendorInfo> GetNearestVendor(uint32 mapId, float x, float y, float z) const;
        std::optional<VendorInfo> GetNearestRepairVendor(uint32 mapId, float x, float y, float z) const;
        std::optional<VendorInfo> GetNearestFoodVendor(uint32 mapId, float x, float y, float z) const;
        std::optional<VendorInfo> GetNearestReagentVendor(uint32 mapId, float x, float y, float z) const;
        std::vector<VendorInfo> GetVendorsSellingItem(uint32 itemId) const;
        std::vector<VendorInfo> GetFoodVendors() const;
        std::vector<VendorInfo> GetReagentVendors() const;
        std::vector<VendorInfo> GetRepairVendors() const;

        // Vendor gossip
        std::string GenerateVendorGossip() const;
        std::string GenerateVendorGossip(const VendorInfo& vendor) const;

        // Market simulation
        void UpdateMarketPrices();
        uint32 GetEstimatedMarketPrice(uint32 itemId) const;
        float GetItemDemand(uint32 itemId) const;
        void SimulatePriceFluctuation(uint32 itemId, float changePercent);

        // Economic gossip
        std::vector<EconomicGossip> GetActiveGossip() const;
        void GenerateMarketGossip();
        std::string GetRandomEconomicGossip() const;

        // Bot economic state
        void UpdateBotEconomicState(uint64 botGuid, Player* bot);
        std::optional<BotEconomicState> GetBotEconomicState(uint64 botGuid) const;
        void SetBotGoldGoal(uint64 botGuid, uint32 targetGold);

        // Goal integration
        void UpdateGoals(uint64 botGuid, Player* bot);
        std::vector<BotGoal> GenerateEconomicGoals(uint64 botGuid, Player* bot);

        // Cosmetic trading (for immersion)
        void SimulateBotTrade(uint64 buyer, uint64 seller, uint32 itemId);
        void GenerateTradeGossip(uint64 buyer, uint64 seller, uint32 itemId, uint32 price);

        // Update (called periodically)
        void Update(uint32 diff);

        // Stats
        uint32 GetValuableItemCount() const;
        uint32 GetFarmingLocationCount() const;
        uint32 GetVendorCount() const;
        uint32 GetActiveGossipCount() const;

        // Settings
        bool IsEnabled() const { return m_enabled; }
        void SetEnabled(bool enabled) { m_enabled = enabled; }

    private:
        // Price tier thresholds (in copper)
        static constexpr uint32 TIER_COMMON_MIN    = 10000;      // 1g
        static constexpr uint32 TIER_UNCOMMON_MIN  = 100000;     // 10g
        static constexpr uint32 TIER_VALUABLE_MIN  = 500000;     // 50g
        static constexpr uint32 TIER_PRECIOUS_MIN  = 2000000;    // 200g
        static constexpr uint32 TIER_LEGENDARY_MIN = 10000000;   // 1000g

        // Generate gossip from price change
        void CreatePriceGossip(const ValuableItem& item, float priceChange);

        // Update item demand based on "market activity"
        void SimulateMarketActivity();

        // Prune expired gossip
        void PruneExpiredGossip();

        // Calculate gold per hour for a farming location
        float CalculateGoldPerHour(const FarmingLocation& location) const;

        // Load creature -> drops mappings
        void LoadCreatureDropData();

        // Get bot's preferred farming category based on professions
        ItemCategory GetBotPreferredCategory(Player* bot) const;

        // Helper to generate unique gossip ID
        uint32 GenerateGossipId();

        // Configuration
        bool m_enabled;
        uint32 m_marketUpdateIntervalMs;
        uint32 m_gossipGenerationIntervalMs;
        uint32 m_gossipExpirationMs;
        float m_priceVolatility;            // How much prices can fluctuate
        uint32 m_minValuablePrice;          // Minimum sell price to track

        // Data storage
        std::unordered_map<uint32, ValuableItem> m_valuableItems;  // itemId -> item
        mutable std::shared_mutex m_itemsMutex;

        std::vector<FarmingLocation> m_farmingLocations;
        mutable std::shared_mutex m_locationsMutex;

        // Creature drop mapping (from creature_loot_template)
        std::unordered_map<uint32, std::vector<uint32>> m_creatureDrops;  // creatureEntry -> itemIds
        std::unordered_map<uint32, std::vector<uint32>> m_itemSources;    // itemId -> creatureEntries
        std::unordered_map<uint32, std::string> m_creatureNames;          // creatureEntry -> name
        mutable std::shared_mutex m_dropsMutex;

        std::unordered_map<uint32, VendorInfo> m_vendors;  // creatureEntry -> vendor
        mutable std::shared_mutex m_vendorsMutex;

        std::vector<EconomicGossip> m_activeGossip;
        mutable std::mutex m_gossipMutex;

        std::unordered_map<uint64, BotEconomicState> m_botStates;  // botGuid -> state
        mutable std::shared_mutex m_statesMutex;

        // Gossip templates
        std::vector<std::string> m_priceRisingTemplates;
        std::vector<std::string> m_priceFallingTemplates;
        std::vector<std::string> m_demandHighTemplates;
        std::vector<std::string> m_demandLowTemplates;
        std::vector<std::string> m_farmingGossipTemplates;
        std::vector<std::string> m_vendorGossipTemplates;

        // ID counters
        std::atomic<uint32> m_nextGossipId;

        // Timers
        uint32 m_marketUpdateTimer;
        uint32 m_gossipGenerationTimer;
        uint32 m_cleanupTimer;

        // References
        BotMemoryManager* m_memoryManager;
        GoalSystem* m_goalSystem;
        GossipManager* m_gossipManager;
        WorldStateManager* m_worldStateManager;

        // Loaded flag
        bool m_dataLoaded;
    };

} // namespace AliveWorld

// Global accessor
#define sEconomySystem AliveWorld::EconomySystem::Instance()

#endif // ALIVE_WORLD_ECONOMY_SYSTEM_H
