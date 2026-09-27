/*
 * mod-alive-world - Economy System Implementation
 * Copyright (C) 2025 EC
 */

#include "EconomySystem.h"
#include "BotMemoryManager.h"
#include "GoalSystem.h"
#include "GossipManager.h"
#include "WorldStateManager.h"

#include "Config.h"
#include "DatabaseEnv.h"
#include "ItemTemplate.h"
#include "Log.h"
#include "ObjectMgr.h"
#include "Player.h"

#include <algorithm>
#include <cmath>
#include <random>
#include <sstream>

namespace AliveWorld
{
    // =========================================================================
    // CONSTRUCTOR / DESTRUCTOR
    // =========================================================================

    EconomySystem::EconomySystem()
        : m_enabled(true)
        , m_marketUpdateIntervalMs(300000)      // 5 minutes
        , m_gossipGenerationIntervalMs(600000)  // 10 minutes
        , m_gossipExpirationMs(3600000)         // 1 hour
        , m_priceVolatility(0.15f)              // 15% max fluctuation
        , m_minValuablePrice(10000)             // 1g minimum to track
        , m_nextGossipId(1)
        , m_marketUpdateTimer(0)
        , m_gossipGenerationTimer(0)
        , m_cleanupTimer(0)
        , m_memoryManager(nullptr)
        , m_goalSystem(nullptr)
        , m_gossipManager(nullptr)
        , m_worldStateManager(nullptr)
        , m_dataLoaded(false)
    {
        // Initialize gossip templates
        m_priceRisingTemplates = {
            "Have you seen {item} prices lately? They're going through the roof!",
            "{item} is selling for crazy amounts right now.",
            "I heard traders are paying top coin for {item}.",
            "Better stock up on {item} before prices go higher!",
            "The market for {item} is on fire!",
            "{item}? Good luck finding any cheap ones.",
            "Word is {item} demand is surging.",
            "Smart folks are hoarding {item} right now."
        };

        m_priceFallingTemplates = {
            "{item} prices are crashing, good time to buy!",
            "Nobody wants {item} anymore, prices are way down.",
            "I got {item} for half what I paid last week.",
            "The {item} market is flooded right now.",
            "Sellers are desperate to unload {item}.",
            "{item}? You can barely give those away now.",
            "I'd hold off selling any {item} if I were you.",
            "The bottom fell out of the {item} market."
        };

        m_demandHighTemplates = {
            "Everyone's looking for {item} these days.",
            "{item} is the hot commodity right now.",
            "Seems like every crafter needs {item}.",
            "I've had three people ask me about {item} today.",
            "If you've got {item}, now's the time to sell.",
            "{item} goes fast at the auction house."
        };

        m_demandLowTemplates = {
            "Nobody's buying {item} lately.",
            "{item}? Might as well vendor it.",
            "The market's dried up for {item}.",
            "Can't even give {item} away these days.",
            "I've been trying to sell {item} for a week.",
            "{item} just sits in my bags collecting dust."
        };

        m_farmingGossipTemplates = {
            "{item} drops from {creature} if you're looking to farm some.",
            "I heard {creature} in {zone} drops {item}.",
            "Best place to farm {item}? Try killing {creature}.",
            "Just got {item} from a {creature}. Pretty good drop rate.",
            "If you need {item}, head to {zone} and hunt {creature}.",
            "{creature} has been dropping {item} for me all day.",
            "Pro tip: {item} drops off {creature}. Not a lot of people know that.",
            "Spent all morning farming {creature} for {item}.",
            "Those {creature} in {zone}? They drop {item} sometimes."
        };

        m_vendorGossipTemplates = {
            "{vendor} has the best prices on {item_type}.",
            "If you need {item_type}, try {vendor}.",
            "I always buy my {item_type} from {vendor}.",
            "That vendor {vendor}? Great selection of {item_type}.",
            "Need to repair? {vendor} can help you out.",
            "{vendor} sells reagents if you're a crafter.",
            "Head to {vendor} for food and drink.",
            "Best place to stock up is {vendor}.",
            "I just restocked at {vendor}. Good prices.",
            "{vendor} saved me a trip to the city for {item_type}."
        };
    }

    EconomySystem::~EconomySystem()
    {
    }

    // =========================================================================
    // INITIALIZATION
    // =========================================================================

    void EconomySystem::Initialize(BotMemoryManager* memoryManager, GoalSystem* goalSystem,
                                    GossipManager* gossipManager, WorldStateManager* worldStateManager)
    {
        m_memoryManager = memoryManager;
        m_goalSystem = goalSystem;
        m_gossipManager = gossipManager;
        m_worldStateManager = worldStateManager;

        LoadConfig();

        if (m_enabled)
        {
            LOG_INFO("module.alive-world", "EconomySystem: Initializing...");

            LoadValuableItems();
            LoadFarmingLocations();
            LoadVendorData();

            m_dataLoaded = true;

            LOG_INFO("module.alive-world", "EconomySystem: Loaded {} valuable items, {} farming locations, {} vendors",
                     GetValuableItemCount(), GetFarmingLocationCount(), GetVendorCount());
        }
        else
        {
            LOG_INFO("module.alive-world", "EconomySystem: Disabled by configuration");
        }
    }

    void EconomySystem::LoadConfig()
    {
        m_enabled = sConfigMgr->GetOption<bool>("AliveWorld.Economy.Enabled", true);
        m_marketUpdateIntervalMs = sConfigMgr->GetOption<uint32>("AliveWorld.Economy.MarketUpdateInterval", 300000);
        m_gossipGenerationIntervalMs = sConfigMgr->GetOption<uint32>("AliveWorld.Economy.GossipGenerationInterval", 600000);
        m_gossipExpirationMs = sConfigMgr->GetOption<uint32>("AliveWorld.Economy.GossipExpiration", 3600000);
        m_priceVolatility = sConfigMgr->GetOption<float>("AliveWorld.Economy.PriceVolatility", 0.15f);
        m_minValuablePrice = sConfigMgr->GetOption<uint32>("AliveWorld.Economy.MinValuablePrice", 10000);
    }

    // =========================================================================
    // DATABASE LOADING
    // =========================================================================

    void EconomySystem::LoadValuableItems()
    {
        // Load valuable items from item_template
        // Criteria: sell price >= 1g, or quality >= green, or used in crafting

        QueryResult result = WorldDatabase.Query(R"(
            SELECT entry, name, class, subclass, Quality, SellPrice, BuyPrice,
                   RequiredLevel, bonding
            FROM item_template
            WHERE SellPrice >= {} OR Quality >= 2
            ORDER BY SellPrice DESC
            LIMIT 10000
        )", m_minValuablePrice);

        if (!result)
        {
            LOG_WARN("module.alive-world", "EconomySystem: No valuable items found in item_template");
            return;
        }

        std::unique_lock<std::shared_mutex> lock(m_itemsMutex);
        m_valuableItems.clear();

        do
        {
            Field* fields = result->Fetch();

            ValuableItem item;
            item.itemId = fields[0].Get<uint32>();
            item.name = fields[1].Get<std::string>();

            uint32 itemClass = fields[2].Get<uint32>();
            uint32 itemSubClass = fields[3].Get<uint32>();

            item.quality = fields[4].Get<uint8>();
            item.sellPrice = fields[5].Get<uint32>();
            item.buyPrice = fields[6].Get<uint32>();
            item.requiredLevel = fields[7].Get<uint32>();

            uint32 bonding = fields[8].Get<uint32>();
            item.isBOP = (bonding == 1); // BIND_WHEN_PICKED_UP

            // Classify the item
            item.valueTier = GetItemValueTier(item.sellPrice);
            item.category = ClassifyItem(item.itemId, itemClass, itemSubClass);

            // Check if it's a crafting material (trade goods class)
            item.isCraftingMat = (itemClass == 7); // ITEM_CLASS_TRADE_GOODS

            // Initial market price is ~2x vendor price for tradeable items
            item.estimatedMarketPrice = item.isBOP ? 0 : (item.sellPrice * 2);
            item.demandMultiplier = 1.0f;

            m_valuableItems[item.itemId] = item;

        } while (result->NextRow());

        LOG_DEBUG("module.alive-world", "EconomySystem: Loaded {} valuable items", m_valuableItems.size());
    }

    void EconomySystem::LoadFarmingLocations()
    {
        // Find creatures that drop valuable items and their spawn locations
        // This builds a list of recommended farming spots

        // Query creature_loot_template for valuable drops
        // Note: Reference > 0 means indirect drop via reference table (we filter those out)
        QueryResult result = WorldDatabase.Query(R"(
            SELECT DISTINCT
                c.zone, c.area, ct.name, ct.entry, ct.minlevel, ct.maxlevel,
                clt.Item, clt.Chance
            FROM creature c
            JOIN creature_template ct ON c.id1 = ct.entry
            JOIN creature_loot_template clt ON ct.lootid = clt.Entry
            WHERE clt.Item IN (
                SELECT entry FROM item_template
                WHERE SellPrice >= {} OR Quality >= 2
            )
            AND clt.Chance > 1
            AND clt.Reference = 0
            AND ct.rank = 0
            ORDER BY clt.Chance DESC
            LIMIT 5000
        )", m_minValuablePrice);

        if (!result)
        {
            // Fallback: just load some common farming locations
            LOG_WARN("module.alive-world", "EconomySystem: Could not load farming locations from database, using defaults");
            return;
        }

        std::unique_lock<std::shared_mutex> lock(m_locationsMutex);
        m_farmingLocations.clear();

        // Map to aggregate drops per creature/zone
        std::unordered_map<std::string, FarmingLocation> locationMap;

        do
        {
            Field* fields = result->Fetch();

            uint32 zoneId = fields[0].Get<uint32>();
            uint32 areaId = fields[1].Get<uint32>();
            std::string creatureName = fields[2].Get<std::string>();
            uint32 creatureEntry = fields[3].Get<uint32>();
            uint8 minLevel = fields[4].Get<uint8>();
            uint8 maxLevel = fields[5].Get<uint8>();
            uint32 itemId = fields[6].Get<uint32>();
            float dropChance = fields[7].Get<float>();

            // Zone name lookup would require AreaTableEntry DBC - use zone ID for now
            std::string zoneName = "Zone " + std::to_string(zoneId);

            // Create key for this location
            std::string key = std::to_string(zoneId) + "_" + std::to_string(creatureEntry);

            auto it = locationMap.find(key);
            if (it == locationMap.end())
            {
                FarmingLocation loc;
                loc.zoneId = zoneId;
                loc.areaId = areaId;
                loc.zoneName = zoneName;
                loc.creatureEntry = creatureEntry;
                loc.creatureName = creatureName;
                loc.creatureLevel = (minLevel + maxLevel) / 2;
                loc.recommendedLevel = loc.creatureLevel;
                loc.dangerLevel = 1;
                loc.requiresGroup = false;
                loc.dropRateAverage = 0.0f;

                locationMap[key] = loc;
                it = locationMap.find(key);
            }

            // Add this item to the location's drops
            it->second.valuableDrops.push_back(itemId);
            it->second.dropRateAverage += dropChance;

        } while (result->NextRow());

        // Convert map to vector and calculate gold/hour
        for (auto& pair : locationMap)
        {
            FarmingLocation& loc = pair.second;

            // Average the drop rate
            if (!loc.valuableDrops.empty())
            {
                loc.dropRateAverage /= loc.valuableDrops.size();
            }

            // Calculate estimated gold per hour
            loc.goldPerHour = CalculateGoldPerHour(loc);

            m_farmingLocations.push_back(loc);
        }

        // Sort by gold per hour
        std::sort(m_farmingLocations.begin(), m_farmingLocations.end(),
            [](const FarmingLocation& a, const FarmingLocation& b) {
                return a.goldPerHour > b.goldPerHour;
            });

        LOG_DEBUG("module.alive-world", "EconomySystem: Loaded {} farming locations", m_farmingLocations.size());

        // Also build the creature drops mapping
        LoadCreatureDropData();
    }

    void EconomySystem::LoadCreatureDropData()
    {
        // Build lookup tables for creature -> items and item -> creatures
        // This uses the farming locations data we already have, plus a direct query

        QueryResult result = WorldDatabase.Query(R"(
            SELECT DISTINCT
                ct.entry, ct.name, clt.Item, it.name as item_name
            FROM creature_template ct
            JOIN creature_loot_template clt ON ct.lootid = clt.Entry
            JOIN item_template it ON clt.Item = it.entry
            WHERE clt.Chance > 1
            AND clt.Reference = 0
            AND it.Quality >= 2
            ORDER BY ct.entry
            LIMIT 50000
        )");

        if (!result)
        {
            LOG_WARN("module.alive-world", "EconomySystem: Could not load creature drop data");
            return;
        }

        std::unique_lock<std::shared_mutex> lock(m_dropsMutex);
        m_creatureDrops.clear();
        m_itemSources.clear();
        m_creatureNames.clear();

        do
        {
            Field* fields = result->Fetch();

            uint32 creatureEntry = fields[0].Get<uint32>();
            std::string creatureName = fields[1].Get<std::string>();
            uint32 itemId = fields[2].Get<uint32>();

            // Store creature name
            m_creatureNames[creatureEntry] = creatureName;

            // Add to creature -> items mapping
            m_creatureDrops[creatureEntry].push_back(itemId);

            // Add to item -> creatures mapping
            m_itemSources[itemId].push_back(creatureEntry);

        } while (result->NextRow());

        LOG_DEBUG("module.alive-world", "EconomySystem: Cached drops for {} creatures, {} items",
            m_creatureDrops.size(), m_itemSources.size());
    }

    std::vector<uint32> EconomySystem::GetDropsFromCreature(uint32 creatureEntry) const
    {
        std::shared_lock<std::shared_mutex> lock(m_dropsMutex);
        auto it = m_creatureDrops.find(creatureEntry);
        return (it != m_creatureDrops.end()) ? it->second : std::vector<uint32>{};
    }

    std::vector<uint32> EconomySystem::GetCreaturesThatDrop(uint32 itemId) const
    {
        std::shared_lock<std::shared_mutex> lock(m_dropsMutex);
        auto it = m_itemSources.find(itemId);
        return (it != m_itemSources.end()) ? it->second : std::vector<uint32>{};
    }

    std::string EconomySystem::GetDropInfo(uint32 itemId) const
    {
        std::shared_lock<std::shared_mutex> lock(m_dropsMutex);

        auto it = m_itemSources.find(itemId);
        if (it == m_itemSources.end() || it->second.empty())
            return "";

        // Get item name
        std::string itemName = "item";
        {
            std::shared_lock<std::shared_mutex> itemLock(m_itemsMutex);
            auto itemIt = m_valuableItems.find(itemId);
            if (itemIt != m_valuableItems.end())
                itemName = itemIt->second.name;
        }

        // Get first creature name
        uint32 creatureEntry = it->second[0];
        auto nameIt = m_creatureNames.find(creatureEntry);
        std::string creatureName = (nameIt != m_creatureNames.end()) ? nameIt->second : "unknown creature";

        return itemName + " drops from " + creatureName;
    }

    std::string EconomySystem::GenerateFarmingGossip(uint32 itemId) const
    {
        if (m_farmingGossipTemplates.empty())
            return "";

        std::shared_lock<std::shared_mutex> lock(m_dropsMutex);

        auto it = m_itemSources.find(itemId);
        if (it == m_itemSources.end() || it->second.empty())
            return "";

        // Get random template
        static std::mt19937 rng(std::random_device{}());
        std::uniform_int_distribution<size_t> dist(0, m_farmingGossipTemplates.size() - 1);
        std::string tmpl = m_farmingGossipTemplates[dist(rng)];

        // Get item name
        std::string itemName = "item";
        {
            std::shared_lock<std::shared_mutex> itemLock(m_itemsMutex);
            auto itemIt = m_valuableItems.find(itemId);
            if (itemIt != m_valuableItems.end())
                itemName = itemIt->second.name;
        }

        // Get random creature that drops it
        std::uniform_int_distribution<size_t> creatureDist(0, it->second.size() - 1);
        uint32 creatureEntry = it->second[creatureDist(rng)];

        auto nameIt = m_creatureNames.find(creatureEntry);
        std::string creatureName = (nameIt != m_creatureNames.end()) ? nameIt->second : "creatures";

        // Find zone from farming locations
        std::string zoneName = "the wilds";
        {
            std::shared_lock<std::shared_mutex> locLock(m_locationsMutex);
            for (const auto& loc : m_farmingLocations)
            {
                if (loc.creatureEntry == creatureEntry)
                {
                    zoneName = loc.zoneName;
                    break;
                }
            }
        }

        // Replace placeholders
        size_t pos;
        while ((pos = tmpl.find("{item}")) != std::string::npos)
            tmpl.replace(pos, 6, itemName);
        while ((pos = tmpl.find("{creature}")) != std::string::npos)
            tmpl.replace(pos, 10, creatureName);
        while ((pos = tmpl.find("{zone}")) != std::string::npos)
            tmpl.replace(pos, 6, zoneName);

        return tmpl;
    }

    std::string EconomySystem::GenerateFarmingGossip(const FarmingLocation& location) const
    {
        if (m_farmingGossipTemplates.empty() || location.valuableDrops.empty())
            return "";

        static std::mt19937 rng(std::random_device{}());
        std::uniform_int_distribution<size_t> dist(0, m_farmingGossipTemplates.size() - 1);
        std::string tmpl = m_farmingGossipTemplates[dist(rng)];

        // Pick a random valuable drop from this location
        std::uniform_int_distribution<size_t> itemDist(0, location.valuableDrops.size() - 1);
        uint32 itemId = location.valuableDrops[itemDist(rng)];

        // Get item name
        std::string itemName = "valuables";
        {
            std::shared_lock<std::shared_mutex> lock(m_itemsMutex);
            auto it = m_valuableItems.find(itemId);
            if (it != m_valuableItems.end())
                itemName = it->second.name;
        }

        // Replace placeholders
        size_t pos;
        while ((pos = tmpl.find("{item}")) != std::string::npos)
            tmpl.replace(pos, 6, itemName);
        while ((pos = tmpl.find("{creature}")) != std::string::npos)
            tmpl.replace(pos, 10, location.creatureName);
        while ((pos = tmpl.find("{zone}")) != std::string::npos)
            tmpl.replace(pos, 6, location.zoneName);

        return tmpl;
    }

    void EconomySystem::OnBotLootedValuable(uint64 botGuid, uint32 itemId, uint32 creatureEntry)
    {
        // Generate farming gossip when a bot loots a valuable item
        if (!m_gossipManager)
            return;

        // Only generate gossip sometimes
        static std::mt19937 rng(std::random_device{}());
        if (std::uniform_real_distribution<float>(0, 1)(rng) > 0.15f)  // 15% chance
            return;

        std::string gossip = GenerateFarmingGossip(itemId);
        if (gossip.empty())
            return;

        // Create gossip item for the gossip manager
        GossipItem item;
        item.gossipId = GenerateGossipId();
        item.type = static_cast<uint8>(WorldEventType::LOOT_ITEM);
        item.headline = gossip;
        item.details = "Farming tip from personal experience";
        item.timestamp = NowMs();
        item.expiresAt = item.timestamp + m_gossipExpirationMs;
        item.importance = 3;  // Low-medium importance
        item.spreadRadius = 2;

        // Add bot to known list
        item.knownByBots.insert(botGuid);

        // This would add to gossip manager - need to expose a method there
        LOG_DEBUG("module.alive-world", "EconomySystem: Bot {} generated farming gossip: {}",
            botGuid, gossip);
    }

    void EconomySystem::LoadVendorData()
    {
        // Load vendor information from npc_vendor and creature spawns

        QueryResult result = WorldDatabase.Query(R"(
            SELECT DISTINCT
                ct.entry, ct.name, ct.npcflag,
                c.position_x, c.position_y, c.position_z, c.map,
                c.zone, c.area
            FROM creature c
            JOIN creature_template ct ON c.id1 = ct.entry
            WHERE ct.npcflag & 128
            LIMIT 5000
        )");

        if (!result)
        {
            LOG_WARN("module.alive-world", "EconomySystem: No vendors found in database");
            return;
        }

        std::unique_lock<std::shared_mutex> lock(m_vendorsMutex);
        m_vendors.clear();

        do
        {
            Field* fields = result->Fetch();

            VendorInfo vendor;
            vendor.creatureEntry = fields[0].Get<uint32>();
            vendor.vendorName = fields[1].Get<std::string>();

            uint32 npcFlags = fields[2].Get<uint32>();

            vendor.posX = fields[3].Get<float>();
            vendor.posY = fields[4].Get<float>();
            vendor.posZ = fields[5].Get<float>();
            vendor.mapId = fields[6].Get<uint32>();
            vendor.zoneId = fields[7].Get<uint32>();
            vendor.areaId = fields[8].Get<uint32>();

            // Check for repair capability (UNIT_NPC_FLAG_REPAIR = 4096)
            vendor.sellsRepairServices = (npcFlags & 4096) != 0;

            // Check for food/drink vendor (we'll detect this by vendor items later)
            vendor.sellsReagents = false;
            vendor.sellsFoodDrink = false;

            m_vendors[vendor.creatureEntry] = vendor;

        } while (result->NextRow());

        // Now load vendor items
        QueryResult itemResult = WorldDatabase.Query(R"(
            SELECT entry, item FROM npc_vendor
            WHERE entry IN (SELECT entry FROM creature_template WHERE npcflag & 128)
        )");

        if (itemResult)
        {
            do
            {
                Field* fields = itemResult->Fetch();
                uint32 vendorEntry = fields[0].Get<uint32>();
                uint32 itemId = fields[1].Get<uint32>();

                auto it = m_vendors.find(vendorEntry);
                if (it != m_vendors.end())
                {
                    it->second.sellsItems.push_back(itemId);

                    // Check if item is food/drink or reagent
                    const ItemTemplate* itemTemplate = sObjectMgr->GetItemTemplate(itemId);
                    if (itemTemplate)
                    {
                        if (itemTemplate->Class == 0) // ITEM_CLASS_CONSUMABLE
                        {
                            if (itemTemplate->SubClass == 0 || itemTemplate->SubClass == 5)
                            {
                                it->second.sellsFoodDrink = true;
                            }
                        }
                        else if (itemTemplate->Class == 7 && itemTemplate->SubClass == 0)
                        {
                            // Trade goods - often reagents
                            it->second.sellsReagents = true;
                        }
                    }
                }

            } while (itemResult->NextRow());
        }

        LOG_DEBUG("module.alive-world", "EconomySystem: Loaded {} vendors", m_vendors.size());
    }

    // =========================================================================
    // ITEM QUERIES
    // =========================================================================

    std::optional<ValuableItem> EconomySystem::GetItem(uint32 itemId) const
    {
        std::shared_lock<std::shared_mutex> lock(m_itemsMutex);
        auto it = m_valuableItems.find(itemId);
        if (it != m_valuableItems.end())
        {
            return it->second;
        }
        return std::nullopt;
    }

    std::vector<ValuableItem> EconomySystem::GetItemsByCategory(ItemCategory category) const
    {
        std::vector<ValuableItem> result;
        std::shared_lock<std::shared_mutex> lock(m_itemsMutex);

        for (const auto& pair : m_valuableItems)
        {
            if (pair.second.category == category)
            {
                result.push_back(pair.second);
            }
        }

        return result;
    }

    std::vector<ValuableItem> EconomySystem::GetItemsByValueTier(ItemValueTier tier) const
    {
        std::vector<ValuableItem> result;
        std::shared_lock<std::shared_mutex> lock(m_itemsMutex);

        for (const auto& pair : m_valuableItems)
        {
            if (pair.second.valueTier == tier)
            {
                result.push_back(pair.second);
            }
        }

        return result;
    }

    std::vector<ValuableItem> EconomySystem::GetTopValueItems(uint32 count) const
    {
        std::vector<ValuableItem> result;
        std::shared_lock<std::shared_mutex> lock(m_itemsMutex);

        for (const auto& pair : m_valuableItems)
        {
            result.push_back(pair.second);
        }

        // Sort by estimated market price
        std::sort(result.begin(), result.end(),
            [](const ValuableItem& a, const ValuableItem& b) {
                return a.estimatedMarketPrice > b.estimatedMarketPrice;
            });

        if (result.size() > count)
        {
            result.resize(count);
        }

        return result;
    }

    ItemValueTier EconomySystem::GetItemValueTier(uint32 sellPrice) const
    {
        if (sellPrice >= TIER_LEGENDARY_MIN) return ItemValueTier::LEGENDARY;
        if (sellPrice >= TIER_PRECIOUS_MIN) return ItemValueTier::PRECIOUS;
        if (sellPrice >= TIER_VALUABLE_MIN) return ItemValueTier::VALUABLE;
        if (sellPrice >= TIER_UNCOMMON_MIN) return ItemValueTier::UNCOMMON;
        if (sellPrice >= TIER_COMMON_MIN) return ItemValueTier::COMMON;
        return ItemValueTier::TRASH;
    }

    ItemCategory EconomySystem::ClassifyItem(uint32 itemId, uint32 itemClass, uint32 itemSubClass) const
    {
        // Classify based on item class and subclass
        switch (itemClass)
        {
            case 0: // ITEM_CLASS_CONSUMABLE
                return ItemCategory::CONSUMABLE;

            case 2: // ITEM_CLASS_WEAPON
            case 4: // ITEM_CLASS_ARMOR
                return ItemCategory::EQUIPMENT;

            case 7: // ITEM_CLASS_TRADE_GOODS
                switch (itemSubClass)
                {
                    case 0:  return ItemCategory::TRADE_GOODS;  // Trade Goods
                    case 5:  return ItemCategory::CLOTH;        // Cloth
                    case 6:  return ItemCategory::LEATHER;      // Leather
                    case 7:  return ItemCategory::ORE;          // Metal & Stone
                    case 8:  return ItemCategory::ORE;          // Meat (use ORE for now)
                    case 9:  return ItemCategory::HERB;         // Herb
                    case 10: return ItemCategory::ELEMENTAL;    // Elemental
                    case 12: return ItemCategory::ENCHANTING;   // Enchanting
                    case 13: return ItemCategory::GEM;          // Materials (often gems)
                    default: return ItemCategory::TRADE_GOODS;
                }

            case 3: // ITEM_CLASS_GEM
                return ItemCategory::GEM;

            case 9: // ITEM_CLASS_RECIPE
                return ItemCategory::RECIPE;

            case 12: // ITEM_CLASS_QUEST
                return ItemCategory::QUEST_ITEM;

            default:
                return ItemCategory::UNKNOWN;
        }
    }

    // =========================================================================
    // FARMING LOCATION QUERIES
    // =========================================================================

    std::vector<FarmingLocation> EconomySystem::GetFarmingLocationsForItem(uint32 itemId) const
    {
        std::vector<FarmingLocation> result;
        std::shared_lock<std::shared_mutex> lock(m_locationsMutex);

        for (const auto& loc : m_farmingLocations)
        {
            for (uint32 dropId : loc.valuableDrops)
            {
                if (dropId == itemId)
                {
                    result.push_back(loc);
                    break;
                }
            }
        }

        return result;
    }

    std::vector<FarmingLocation> EconomySystem::GetFarmingLocationsForCategory(ItemCategory category) const
    {
        std::vector<FarmingLocation> result;
        std::shared_lock<std::shared_mutex> lock(m_locationsMutex);

        for (const auto& loc : m_farmingLocations)
        {
            for (uint32 itemId : loc.valuableDrops)
            {
                auto itemOpt = GetItem(itemId);
                if (itemOpt && itemOpt->category == category)
                {
                    result.push_back(loc);
                    break;
                }
            }
        }

        return result;
    }

    std::vector<FarmingLocation> EconomySystem::GetFarmingLocationsForLevel(uint8 playerLevel) const
    {
        std::vector<FarmingLocation> result;
        std::shared_lock<std::shared_mutex> lock(m_locationsMutex);

        for (const auto& loc : m_farmingLocations)
        {
            // Location is suitable if recommended level is within 10 levels
            if (loc.recommendedLevel <= playerLevel &&
                loc.recommendedLevel >= (playerLevel > 10 ? playerLevel - 10 : 1))
            {
                result.push_back(loc);
            }
        }

        // Sort by gold per hour
        std::sort(result.begin(), result.end(),
            [](const FarmingLocation& a, const FarmingLocation& b) {
                return a.goldPerHour > b.goldPerHour;
            });

        return result;
    }

    std::optional<FarmingLocation> EconomySystem::GetBestFarmingLocation(uint64 botGuid, Player* bot)
    {
        if (!bot)
            return std::nullopt;

        uint8 level = bot->GetLevel();
        ItemCategory preferred = GetBotPreferredCategory(bot);

        std::shared_lock<std::shared_mutex> lock(m_locationsMutex);

        FarmingLocation* best = nullptr;
        float bestScore = 0.0f;

        for (auto& loc : m_farmingLocations)
        {
            // Skip if too high level
            if (loc.recommendedLevel > level + 3)
                continue;

            // Skip if too low level (inefficient)
            if (loc.recommendedLevel < level - 15)
                continue;

            float score = loc.goldPerHour;

            // Bonus for preferred category
            for (uint32 itemId : loc.valuableDrops)
            {
                auto itemOpt = GetItem(itemId);
                if (itemOpt && itemOpt->category == preferred)
                {
                    score *= 1.5f;
                    break;
                }
            }

            // Penalty for danger
            score *= (1.0f - (loc.dangerLevel * 0.05f));

            if (score > bestScore)
            {
                bestScore = score;
                best = const_cast<FarmingLocation*>(&loc);
            }
        }

        if (best)
        {
            return *best;
        }

        return std::nullopt;
    }

    // =========================================================================
    // VENDOR QUERIES
    // =========================================================================

    std::vector<VendorInfo> EconomySystem::GetVendorsInZone(uint32 zoneId) const
    {
        std::vector<VendorInfo> result;
        std::shared_lock<std::shared_mutex> lock(m_vendorsMutex);

        for (const auto& pair : m_vendors)
        {
            if (pair.second.zoneId == zoneId)
            {
                result.push_back(pair.second);
            }
        }

        return result;
    }

    std::optional<VendorInfo> EconomySystem::GetNearestVendor(uint32 mapId, float x, float y, float z) const
    {
        std::shared_lock<std::shared_mutex> lock(m_vendorsMutex);

        const VendorInfo* nearest = nullptr;
        float nearestDist = std::numeric_limits<float>::max();

        for (const auto& pair : m_vendors)
        {
            const VendorInfo& vendor = pair.second;
            if (vendor.mapId != mapId)
                continue;

            float dx = vendor.posX - x;
            float dy = vendor.posY - y;
            float dz = vendor.posZ - z;
            float dist = dx*dx + dy*dy + dz*dz;

            if (dist < nearestDist)
            {
                nearestDist = dist;
                nearest = &vendor;
            }
        }

        if (nearest)
        {
            return *nearest;
        }

        return std::nullopt;
    }

    std::optional<VendorInfo> EconomySystem::GetNearestRepairVendor(uint32 mapId, float x, float y, float z) const
    {
        std::shared_lock<std::shared_mutex> lock(m_vendorsMutex);

        const VendorInfo* nearest = nullptr;
        float nearestDist = std::numeric_limits<float>::max();

        for (const auto& pair : m_vendors)
        {
            const VendorInfo& vendor = pair.second;
            if (vendor.mapId != mapId || !vendor.sellsRepairServices)
                continue;

            float dx = vendor.posX - x;
            float dy = vendor.posY - y;
            float dz = vendor.posZ - z;
            float dist = dx*dx + dy*dy + dz*dz;

            if (dist < nearestDist)
            {
                nearestDist = dist;
                nearest = &vendor;
            }
        }

        if (nearest)
        {
            return *nearest;
        }

        return std::nullopt;
    }

    std::vector<VendorInfo> EconomySystem::GetVendorsSellingItem(uint32 itemId) const
    {
        std::vector<VendorInfo> result;
        std::shared_lock<std::shared_mutex> lock(m_vendorsMutex);

        for (const auto& pair : m_vendors)
        {
            for (uint32 vendorItemId : pair.second.sellsItems)
            {
                if (vendorItemId == itemId)
                {
                    result.push_back(pair.second);
                    break;
                }
            }
        }

        return result;
    }

    std::optional<VendorInfo> EconomySystem::GetNearestFoodVendor(uint32 mapId, float x, float y, float z) const
    {
        std::shared_lock<std::shared_mutex> lock(m_vendorsMutex);

        const VendorInfo* nearest = nullptr;
        float nearestDist = std::numeric_limits<float>::max();

        for (const auto& pair : m_vendors)
        {
            const VendorInfo& vendor = pair.second;
            if (vendor.mapId != mapId || !vendor.sellsFoodDrink)
                continue;

            float dx = vendor.posX - x;
            float dy = vendor.posY - y;
            float dz = vendor.posZ - z;
            float dist = dx*dx + dy*dy + dz*dz;

            if (dist < nearestDist)
            {
                nearestDist = dist;
                nearest = &vendor;
            }
        }

        return nearest ? std::make_optional(*nearest) : std::nullopt;
    }

    std::optional<VendorInfo> EconomySystem::GetNearestReagentVendor(uint32 mapId, float x, float y, float z) const
    {
        std::shared_lock<std::shared_mutex> lock(m_vendorsMutex);

        const VendorInfo* nearest = nullptr;
        float nearestDist = std::numeric_limits<float>::max();

        for (const auto& pair : m_vendors)
        {
            const VendorInfo& vendor = pair.second;
            if (vendor.mapId != mapId || !vendor.sellsReagents)
                continue;

            float dx = vendor.posX - x;
            float dy = vendor.posY - y;
            float dz = vendor.posZ - z;
            float dist = dx*dx + dy*dy + dz*dz;

            if (dist < nearestDist)
            {
                nearestDist = dist;
                nearest = &vendor;
            }
        }

        return nearest ? std::make_optional(*nearest) : std::nullopt;
    }

    std::vector<VendorInfo> EconomySystem::GetFoodVendors() const
    {
        std::vector<VendorInfo> result;
        std::shared_lock<std::shared_mutex> lock(m_vendorsMutex);

        for (const auto& pair : m_vendors)
        {
            if (pair.second.sellsFoodDrink)
            {
                result.push_back(pair.second);
            }
        }

        return result;
    }

    std::vector<VendorInfo> EconomySystem::GetReagentVendors() const
    {
        std::vector<VendorInfo> result;
        std::shared_lock<std::shared_mutex> lock(m_vendorsMutex);

        for (const auto& pair : m_vendors)
        {
            if (pair.second.sellsReagents)
            {
                result.push_back(pair.second);
            }
        }

        return result;
    }

    std::vector<VendorInfo> EconomySystem::GetRepairVendors() const
    {
        std::vector<VendorInfo> result;
        std::shared_lock<std::shared_mutex> lock(m_vendorsMutex);

        for (const auto& pair : m_vendors)
        {
            if (pair.second.sellsRepairServices)
            {
                result.push_back(pair.second);
            }
        }

        return result;
    }

    std::string EconomySystem::GenerateVendorGossip() const
    {
        if (m_vendorGossipTemplates.empty())
            return "";

        std::shared_lock<std::shared_mutex> lock(m_vendorsMutex);
        if (m_vendors.empty())
            return "";

        // Pick a random vendor
        static std::mt19937 rng(std::random_device{}());
        std::uniform_int_distribution<size_t> vendorDist(0, m_vendors.size() - 1);

        auto it = m_vendors.begin();
        std::advance(it, vendorDist(rng));
        const VendorInfo& vendor = it->second;

        return GenerateVendorGossip(vendor);
    }

    std::string EconomySystem::GenerateVendorGossip(const VendorInfo& vendor) const
    {
        if (m_vendorGossipTemplates.empty())
            return "";

        static std::mt19937 rng(std::random_device{}());
        std::uniform_int_distribution<size_t> dist(0, m_vendorGossipTemplates.size() - 1);
        std::string tmpl = m_vendorGossipTemplates[dist(rng)];

        // Determine what type of items this vendor sells
        std::string itemType = "supplies";
        if (vendor.sellsFoodDrink)
            itemType = "food and drink";
        else if (vendor.sellsReagents)
            itemType = "reagents";
        else if (vendor.sellsRepairServices)
            itemType = "repair services";
        else if (!vendor.sellsItems.empty())
            itemType = "trade goods";

        // Replace placeholders
        size_t pos;
        while ((pos = tmpl.find("{vendor}")) != std::string::npos)
            tmpl.replace(pos, 8, vendor.vendorName);
        while ((pos = tmpl.find("{item_type}")) != std::string::npos)
            tmpl.replace(pos, 11, itemType);

        return tmpl;
    }

    // =========================================================================
    // MARKET SIMULATION
    // =========================================================================

    void EconomySystem::UpdateMarketPrices()
    {
        if (!m_enabled)
            return;

        std::unique_lock<std::shared_mutex> lock(m_itemsMutex);

        static std::random_device rd;
        static std::mt19937 gen(rd());
        std::uniform_real_distribution<float> changeDist(-m_priceVolatility, m_priceVolatility);
        std::uniform_int_distribution<uint32> itemPicker(0, std::max(1u, static_cast<uint32>(m_valuableItems.size())) - 1);

        // Update a random subset of items
        uint32 updateCount = std::min(100u, static_cast<uint32>(m_valuableItems.size() / 10));

        auto it = m_valuableItems.begin();
        for (uint32 i = 0; i < updateCount && it != m_valuableItems.end(); ++i)
        {
            // Advance to random position
            std::advance(it, itemPicker(gen) % std::max(1, static_cast<int>(std::distance(it, m_valuableItems.end()))));
            if (it == m_valuableItems.end())
                break;

            ValuableItem& item = it->second;

            // Skip BoP items
            if (item.isBOP)
            {
                ++it;
                continue;
            }

            // Apply random price change
            float change = changeDist(gen);
            item.demandMultiplier += change * 0.1f;

            // Clamp demand multiplier
            item.demandMultiplier = std::clamp(item.demandMultiplier, 0.5f, 2.0f);

            // Update estimated market price
            uint32 basePrice = item.sellPrice * 2;
            item.estimatedMarketPrice = static_cast<uint32>(basePrice * item.demandMultiplier);

            // Generate gossip for significant changes
            if (std::abs(change) > 0.1f)
            {
                // Release lock before creating gossip to avoid deadlock
                lock.unlock();
                CreatePriceGossip(item, change);
                lock.lock();
                // Iterator might be invalidated, restart
                it = m_valuableItems.begin();
            }
            else
            {
                ++it;
            }
        }
    }

    uint32 EconomySystem::GetEstimatedMarketPrice(uint32 itemId) const
    {
        auto itemOpt = GetItem(itemId);
        if (itemOpt)
        {
            return itemOpt->estimatedMarketPrice;
        }
        return 0;
    }

    float EconomySystem::GetItemDemand(uint32 itemId) const
    {
        auto itemOpt = GetItem(itemId);
        if (itemOpt)
        {
            return itemOpt->demandMultiplier;
        }
        return 1.0f;
    }

    void EconomySystem::SimulatePriceFluctuation(uint32 itemId, float changePercent)
    {
        std::unique_lock<std::shared_mutex> lock(m_itemsMutex);

        auto it = m_valuableItems.find(itemId);
        if (it != m_valuableItems.end())
        {
            ValuableItem& item = it->second;
            item.demandMultiplier *= (1.0f + changePercent);
            item.demandMultiplier = std::clamp(item.demandMultiplier, 0.5f, 2.0f);

            uint32 basePrice = item.sellPrice * 2;
            item.estimatedMarketPrice = static_cast<uint32>(basePrice * item.demandMultiplier);

            lock.unlock();

            if (std::abs(changePercent) > 0.1f)
            {
                CreatePriceGossip(item, changePercent);
            }
        }
    }

    // =========================================================================
    // ECONOMIC GOSSIP
    // =========================================================================

    std::vector<EconomicGossip> EconomySystem::GetActiveGossip() const
    {
        std::lock_guard<std::mutex> lock(m_gossipMutex);
        return m_activeGossip;
    }

    void EconomySystem::GenerateMarketGossip()
    {
        if (!m_enabled)
            return;

        // Get items with significant demand changes
        std::vector<ValuableItem> highDemand;
        std::vector<ValuableItem> lowDemand;

        {
            std::shared_lock<std::shared_mutex> lock(m_itemsMutex);
            for (const auto& pair : m_valuableItems)
            {
                if (pair.second.demandMultiplier > 1.3f)
                {
                    highDemand.push_back(pair.second);
                }
                else if (pair.second.demandMultiplier < 0.7f)
                {
                    lowDemand.push_back(pair.second);
                }
            }
        }

        // Generate gossip for high demand items
        static std::random_device rd;
        static std::mt19937 gen(rd());

        if (!highDemand.empty())
        {
            std::uniform_int_distribution<size_t> itemDist(0, highDemand.size() - 1);
            std::uniform_int_distribution<size_t> templateDist(0, m_demandHighTemplates.size() - 1);

            const ValuableItem& item = highDemand[itemDist(gen)];
            std::string templateStr = m_demandHighTemplates[templateDist(gen)];

            // Replace {item} placeholder
            size_t pos = templateStr.find("{item}");
            if (pos != std::string::npos)
            {
                templateStr.replace(pos, 6, item.name);
            }

            EconomicGossip gossip;
            gossip.gossipId = GenerateGossipId();
            gossip.itemId = item.itemId;
            gossip.itemName = item.name;
            gossip.message = templateStr;
            gossip.priceChange = (item.demandMultiplier - 1.0f) * 100.0f;
            gossip.timestamp = NowMs();
            gossip.expiresAt = gossip.timestamp + m_gossipExpirationMs;
            gossip.reliability = 7;

            std::lock_guard<std::mutex> lock(m_gossipMutex);
            m_activeGossip.push_back(gossip);
        }

        // Generate gossip for low demand items
        if (!lowDemand.empty())
        {
            std::uniform_int_distribution<size_t> itemDist(0, lowDemand.size() - 1);
            std::uniform_int_distribution<size_t> templateDist(0, m_demandLowTemplates.size() - 1);

            const ValuableItem& item = lowDemand[itemDist(gen)];
            std::string templateStr = m_demandLowTemplates[templateDist(gen)];

            // Replace {item} placeholder
            size_t pos = templateStr.find("{item}");
            if (pos != std::string::npos)
            {
                templateStr.replace(pos, 6, item.name);
            }

            EconomicGossip gossip;
            gossip.gossipId = GenerateGossipId();
            gossip.itemId = item.itemId;
            gossip.itemName = item.name;
            gossip.message = templateStr;
            gossip.priceChange = (item.demandMultiplier - 1.0f) * 100.0f;
            gossip.timestamp = NowMs();
            gossip.expiresAt = gossip.timestamp + m_gossipExpirationMs;
            gossip.reliability = 6;

            std::lock_guard<std::mutex> lock(m_gossipMutex);
            m_activeGossip.push_back(gossip);
        }
    }

    std::string EconomySystem::GetRandomEconomicGossip() const
    {
        std::lock_guard<std::mutex> lock(m_gossipMutex);

        if (m_activeGossip.empty())
        {
            return "Business is slow these days.";
        }

        static std::random_device rd;
        static std::mt19937 gen(rd());
        std::uniform_int_distribution<size_t> dist(0, m_activeGossip.size() - 1);

        return m_activeGossip[dist(gen)].message;
    }

    void EconomySystem::CreatePriceGossip(const ValuableItem& item, float priceChange)
    {
        static std::random_device rd;
        static std::mt19937 gen(rd());

        const std::vector<std::string>& templates = priceChange > 0 ?
            m_priceRisingTemplates : m_priceFallingTemplates;

        if (templates.empty())
            return;

        std::uniform_int_distribution<size_t> dist(0, templates.size() - 1);
        std::string templateStr = templates[dist(gen)];

        // Replace {item} placeholder
        size_t pos = templateStr.find("{item}");
        if (pos != std::string::npos)
        {
            templateStr.replace(pos, 6, item.name);
        }

        EconomicGossip gossip;
        gossip.gossipId = GenerateGossipId();
        gossip.itemId = item.itemId;
        gossip.itemName = item.name;
        gossip.message = templateStr;
        gossip.priceChange = priceChange * 100.0f;
        gossip.timestamp = NowMs();
        gossip.expiresAt = gossip.timestamp + m_gossipExpirationMs;
        gossip.reliability = 8;

        std::lock_guard<std::mutex> lock(m_gossipMutex);
        m_activeGossip.push_back(gossip);
    }

    // =========================================================================
    // MARKET ACTIVITY SIMULATION
    // =========================================================================
    // Simulates background economic activity in the game world. This creates
    // the illusion of a living economy by periodically adjusting demand
    // multipliers based on various simulated factors:
    //
    // 1. Crafting Demand - Crafting materials have cyclical demand based on
    //    simulated crafting activity
    // 2. Seasonal Effects - Some items are more valuable at different times
    // 3. Zone Activity - Items from active zones see more trading
    // 4. Random Events - Occasional spikes in demand for specific items
    //
    // This is purely cosmetic - it doesn't affect actual item drops or prices.
    // It just makes bots talk about market conditions realistically.
    // =========================================================================

    void EconomySystem::SimulateMarketActivity()
    {
        if (!m_enabled || !m_dataLoaded)
            return;

        LOG_DEBUG("module.alive-world", "EconomySystem: Simulating market activity...");

        static std::random_device rd;
        static std::mt19937 gen(rd());

        // =====================================================================
        // Phase 1: Crafting Material Demand Cycles
        // =====================================================================
        // Simulate crafters needing materials - creates demand waves
        // Different material types have different cycle patterns

        std::vector<ItemCategory> craftingCategories = {
            ItemCategory::ORE, ItemCategory::HERB, ItemCategory::LEATHER,
            ItemCategory::CLOTH, ItemCategory::ELEMENTAL, ItemCategory::ENCHANTING
        };

        std::uniform_int_distribution<size_t> categoryDist(0, craftingCategories.size() - 1);
        std::uniform_real_distribution<float> demandShift(-0.08f, 0.12f);  // Slight upward bias

        // Pick 2-3 random categories to have demand changes
        uint32 categoriesAffected = std::uniform_int_distribution<uint32>(2, 3)(gen);

        for (uint32 i = 0; i < categoriesAffected; ++i)
        {
            ItemCategory category = craftingCategories[categoryDist(gen)];
            float shift = demandShift(gen);

            // Apply to items in this category
            {
                std::unique_lock<std::shared_mutex> lock(m_itemsMutex);
                for (auto& pair : m_valuableItems)
                {
                    if (pair.second.category == category && !pair.second.isBOP)
                    {
                        pair.second.demandMultiplier += shift * 0.5f;  // Dampen the effect
                        pair.second.demandMultiplier = std::clamp(
                            pair.second.demandMultiplier, 0.5f, 2.0f);

                        // Update market price
                        uint32 basePrice = pair.second.sellPrice * 2;
                        pair.second.estimatedMarketPrice = static_cast<uint32>(
                            basePrice * pair.second.demandMultiplier);
                    }
                }
            }

            LOG_DEBUG("module.alive-world", "EconomySystem: %s demand shifted by %.1f%%",
                ItemCategoryToString(category), shift * 100.0f);
        }

        // =====================================================================
        // Phase 2: Zone-Based Activity
        // =====================================================================
        // Items that drop in zones with more player/bot activity see more trading
        // Use world state manager to get zone activity if available

        if (m_worldStateManager)
        {
            std::shared_lock<std::shared_mutex> locLock(m_locationsMutex);

            for (const auto& loc : m_farmingLocations)
            {
                // Check if this zone is active (has players)
                uint32 zonePopulation = 0;  // Would get from world state
                // For now, simulate random zone activity
                bool zoneIsActive = std::uniform_int_distribution<int>(0, 3)(gen) == 0;

                if (zoneIsActive)
                {
                    // Boost demand for items from this zone
                    float boost = std::uniform_real_distribution<float>(0.01f, 0.05f)(gen);

                    std::unique_lock<std::shared_mutex> itemLock(m_itemsMutex);
                    for (uint32 itemId : loc.valuableDrops)
                    {
                        auto it = m_valuableItems.find(itemId);
                        if (it != m_valuableItems.end() && !it->second.isBOP)
                        {
                            it->second.demandMultiplier += boost;
                            it->second.demandMultiplier = std::clamp(
                                it->second.demandMultiplier, 0.5f, 2.0f);

                            uint32 basePrice = it->second.sellPrice * 2;
                            it->second.estimatedMarketPrice = static_cast<uint32>(
                                basePrice * it->second.demandMultiplier);
                        }
                    }
                }
            }
        }

        // =====================================================================
        // Phase 3: Random Demand Spikes
        // =====================================================================
        // Occasionally, a random valuable item has a sudden demand spike
        // This creates "hot" items that bots will gossip about

        std::uniform_int_distribution<int> spikeChance(1, 20);  // 5% chance
        if (spikeChance(gen) == 1)
        {
            std::unique_lock<std::shared_mutex> lock(m_itemsMutex);

            if (!m_valuableItems.empty())
            {
                // Pick a random item
                std::uniform_int_distribution<size_t> itemDist(0, m_valuableItems.size() - 1);
                auto it = m_valuableItems.begin();
                std::advance(it, itemDist(gen));

                ValuableItem& item = it->second;

                if (!item.isBOP)
                {
                    // Apply significant demand spike
                    float spike = std::uniform_real_distribution<float>(0.15f, 0.30f)(gen);
                    float oldDemand = item.demandMultiplier;
                    item.demandMultiplier = std::min(2.0f, item.demandMultiplier + spike);

                    uint32 basePrice = item.sellPrice * 2;
                    item.estimatedMarketPrice = static_cast<uint32>(
                        basePrice * item.demandMultiplier);

                    LOG_INFO("module.alive-world",
                        "EconomySystem: Demand spike for %s! (%.1f%% -> %.1f%%)",
                        item.name.c_str(), oldDemand * 100.0f, item.demandMultiplier * 100.0f);

                    // Generate gossip about the spike
                    lock.unlock();  // Avoid deadlock
                    CreatePriceGossip(item, spike);
                }
            }
        }

        // =====================================================================
        // Phase 4: Mean Reversion
        // =====================================================================
        // Prices naturally drift back toward baseline over time
        // This prevents runaway price inflation/deflation

        {
            std::unique_lock<std::shared_mutex> lock(m_itemsMutex);
            for (auto& pair : m_valuableItems)
            {
                ValuableItem& item = pair.second;
                if (item.isBOP)
                    continue;

                // Slowly pull demand back toward 1.0 (baseline)
                float diff = 1.0f - item.demandMultiplier;
                item.demandMultiplier += diff * 0.02f;  // 2% reversion per cycle

                // Update price
                uint32 basePrice = item.sellPrice * 2;
                item.estimatedMarketPrice = static_cast<uint32>(
                    basePrice * item.demandMultiplier);
            }
        }

        // =====================================================================
        // Phase 5: Simulated Bot-to-Bot Trades
        // =====================================================================
        // Occasionally simulate bots trading with each other
        // This is purely cosmetic but adds to the "living economy" feel

        std::uniform_int_distribution<int> tradeChance(1, 10);  // 10% chance
        if (tradeChance(gen) == 1)
        {
            // Pick a random valuable item for the trade
            std::shared_lock<std::shared_mutex> lock(m_itemsMutex);
            if (!m_valuableItems.empty())
            {
                std::uniform_int_distribution<size_t> itemDist(0, m_valuableItems.size() - 1);
                auto it = m_valuableItems.begin();
                std::advance(it, itemDist(gen));

                if (!it->second.isBOP)
                {
                    // Generate trade gossip
                    lock.unlock();
                    GenerateTradeGossip(0, 0, it->second.itemId, it->second.estimatedMarketPrice);

                    LOG_DEBUG("module.alive-world",
                        "EconomySystem: Simulated trade for %s at %ug",
                        it->second.name.c_str(), it->second.estimatedMarketPrice / 10000);
                }
            }
        }

        LOG_DEBUG("module.alive-world", "EconomySystem: Market activity simulation complete");
    }

    void EconomySystem::PruneExpiredGossip()
    {
        std::lock_guard<std::mutex> lock(m_gossipMutex);

        uint64 now = NowMs();
        m_activeGossip.erase(
            std::remove_if(m_activeGossip.begin(), m_activeGossip.end(),
                [now](const EconomicGossip& g) { return g.expiresAt < now; }),
            m_activeGossip.end()
        );
    }

    // =========================================================================
    // BOT ECONOMIC STATE
    // =========================================================================

    void EconomySystem::UpdateBotEconomicState(uint64 botGuid, Player* bot)
    {
        if (!bot)
            return;

        std::unique_lock<std::shared_mutex> lock(m_statesMutex);

        BotEconomicState& state = m_botStates[botGuid];
        state.botGuid = botGuid;
        state.currentGold = bot->GetMoney();

        // Get preferred category based on professions
        state.preferredFarmCategory = GetBotPreferredCategory(bot);

        // Get greed factor from personality if available
        if (m_memoryManager)
        {
            // BotMemoryManager should have personality data
            // For now, use default
            state.greedFactor = 1.0f;
        }

        // Set gold goal based on level and greed
        uint32 baseGoldGoal = bot->GetLevel() * 100000; // 10g per level
        state.targetGold = static_cast<uint32>(baseGoldGoal * state.greedFactor);
    }

    std::optional<BotEconomicState> EconomySystem::GetBotEconomicState(uint64 botGuid) const
    {
        std::shared_lock<std::shared_mutex> lock(m_statesMutex);

        auto it = m_botStates.find(botGuid);
        if (it != m_botStates.end())
        {
            return it->second;
        }

        return std::nullopt;
    }

    void EconomySystem::SetBotGoldGoal(uint64 botGuid, uint32 targetGold)
    {
        std::unique_lock<std::shared_mutex> lock(m_statesMutex);

        auto it = m_botStates.find(botGuid);
        if (it != m_botStates.end())
        {
            it->second.targetGold = targetGold;
        }
    }

    // =========================================================================
    // GOAL INTEGRATION
    // =========================================================================

    void EconomySystem::UpdateGoals(uint64 botGuid, Player* bot)
    {
        if (!m_goalSystem || !bot)
            return;

        // Generate economic goals if bot doesn't have many
        auto goals = GenerateEconomicGoals(botGuid, bot);
        for (const auto& goal : goals)
        {
            m_goalSystem->AddGoal(botGuid, goal);
        }
    }

    std::vector<BotGoal> EconomySystem::GenerateEconomicGoals(uint64 botGuid, Player* bot)
    {
        std::vector<BotGoal> goals;

        if (!bot)
            return goals;

        UpdateBotEconomicState(botGuid, bot);
        auto stateOpt = GetBotEconomicState(botGuid);
        if (!stateOpt)
            return goals;

        const BotEconomicState& state = *stateOpt;

        // Gold accumulation goal
        if (state.currentGold < state.targetGold)
        {
            BotGoal goldGoal;
            goldGoal.goalId = 0; // Will be assigned by GoalSystem
            goldGoal.botGuid = botGuid;
            goldGoal.type = GoalType::GOLD;
            goldGoal.priority = state.currentGold < (state.targetGold / 4) ?
                GoalPriority::HIGH : GoalPriority::MEDIUM;
            goldGoal.status = GoalStatus::ACTIVE;
            goldGoal.targetId = 0;
            goldGoal.targetName = "Gold savings";
            goldGoal.progressCurrent = state.currentGold / 10000; // In gold
            goldGoal.progressTarget = state.targetGold / 10000;
            goldGoal.progress = static_cast<float>(state.currentGold) / static_cast<float>(state.targetGold);
            goldGoal.createdAt = NowMs();
            goldGoal.updatedAt = goldGoal.createdAt;
            goldGoal.reason = "Need gold for repairs and supplies";

            goals.push_back(goldGoal);
        }

        // Farming goal based on preferred category
        if (state.preferredFarmCategory != ItemCategory::UNKNOWN)
        {
            auto locations = GetFarmingLocationsForCategory(state.preferredFarmCategory);
            if (!locations.empty())
            {
                // Find a suitable location for bot's level
                for (const auto& loc : locations)
                {
                    if (loc.recommendedLevel <= bot->GetLevel())
                    {
                        BotGoal farmGoal;
                        farmGoal.goalId = 0;
                        farmGoal.botGuid = botGuid;
                        farmGoal.type = GoalType::ITEM;
                        farmGoal.priority = GoalPriority::LOW;
                        farmGoal.status = GoalStatus::ACTIVE;
                        farmGoal.targetId = loc.zoneId;
                        farmGoal.targetName = "Farm " + loc.zoneName;
                        farmGoal.progressCurrent = 0;
                        farmGoal.progressTarget = 10; // 10 items
                        farmGoal.progress = 0.0f;
                        farmGoal.createdAt = NowMs();
                        farmGoal.updatedAt = farmGoal.createdAt;
                        farmGoal.reason = "Good farming spot for my professions";
                        farmGoal.notes = "Creature: " + loc.creatureName;

                        goals.push_back(farmGoal);
                        break;
                    }
                }
            }
        }

        return goals;
    }

    // =========================================================================
    // COSMETIC TRADING
    // =========================================================================

    void EconomySystem::SimulateBotTrade(uint64 buyer, uint64 seller, uint32 itemId)
    {
        // This is purely cosmetic - bots don't actually trade
        // Just generates gossip about trading activity

        auto itemOpt = GetItem(itemId);
        if (!itemOpt)
            return;

        // Slightly increase demand for traded items
        SimulatePriceFluctuation(itemId, 0.02f);

        // Generate trade gossip
        GenerateTradeGossip(buyer, seller, itemId, itemOpt->estimatedMarketPrice);
    }

    void EconomySystem::GenerateTradeGossip(uint64 buyer, uint64 seller, uint32 itemId, uint32 price)
    {
        auto itemOpt = GetItem(itemId);
        if (!itemOpt)
            return;

        // Create a gossip about the trade
        std::ostringstream ss;
        ss << "Heard someone paid " << (price / 10000) << "g for " << itemOpt->name << ".";

        EconomicGossip gossip;
        gossip.gossipId = GenerateGossipId();
        gossip.itemId = itemId;
        gossip.itemName = itemOpt->name;
        gossip.message = ss.str();
        gossip.priceChange = 0.0f;
        gossip.timestamp = NowMs();
        gossip.expiresAt = gossip.timestamp + (m_gossipExpirationMs / 2);
        gossip.reliability = 5;

        std::lock_guard<std::mutex> lock(m_gossipMutex);
        m_activeGossip.push_back(gossip);
    }

    // =========================================================================
    // UPDATE
    // =========================================================================

    void EconomySystem::Update(uint32 diff)
    {
        if (!m_enabled)
            return;

        // Market price updates
        m_marketUpdateTimer += diff;
        if (m_marketUpdateTimer >= m_marketUpdateIntervalMs)
        {
            m_marketUpdateTimer = 0;
            UpdateMarketPrices();
        }

        // Gossip generation
        m_gossipGenerationTimer += diff;
        if (m_gossipGenerationTimer >= m_gossipGenerationIntervalMs)
        {
            m_gossipGenerationTimer = 0;
            GenerateMarketGossip();
        }

        // Cleanup
        m_cleanupTimer += diff;
        if (m_cleanupTimer >= 60000) // Every minute
        {
            m_cleanupTimer = 0;
            PruneExpiredGossip();
        }
    }

    // =========================================================================
    // STATS
    // =========================================================================

    uint32 EconomySystem::GetValuableItemCount() const
    {
        std::shared_lock<std::shared_mutex> lock(m_itemsMutex);
        return static_cast<uint32>(m_valuableItems.size());
    }

    uint32 EconomySystem::GetFarmingLocationCount() const
    {
        std::shared_lock<std::shared_mutex> lock(m_locationsMutex);
        return static_cast<uint32>(m_farmingLocations.size());
    }

    uint32 EconomySystem::GetVendorCount() const
    {
        std::shared_lock<std::shared_mutex> lock(m_vendorsMutex);
        return static_cast<uint32>(m_vendors.size());
    }

    uint32 EconomySystem::GetActiveGossipCount() const
    {
        std::lock_guard<std::mutex> lock(m_gossipMutex);
        return static_cast<uint32>(m_activeGossip.size());
    }

    // =========================================================================
    // PRIVATE HELPERS
    // =========================================================================

    float EconomySystem::CalculateGoldPerHour(const FarmingLocation& location) const
    {
        float totalValue = 0.0f;

        for (uint32 itemId : location.valuableDrops)
        {
            auto itemOpt = GetItem(itemId);
            if (itemOpt)
            {
                // Estimate kills per hour based on level
                float killsPerHour = 60.0f; // ~1 kill per minute average
                float dropChance = location.dropRateAverage / 100.0f;
                float itemsPerHour = killsPerHour * dropChance;

                // Value in gold (divide by 10000 to convert copper to gold)
                totalValue += (itemsPerHour * itemOpt->estimatedMarketPrice) / 10000.0f;
            }
        }

        return totalValue;
    }

    ItemCategory EconomySystem::GetBotPreferredCategory(Player* bot) const
    {
        if (!bot)
            return ItemCategory::UNKNOWN;

        // Check professions to determine preferred farming category
        // This is a simplified version - real implementation would check
        // the bot's actual professions

        // For now, return a category based on class
        uint8 botClass = bot->getClass();

        switch (botClass)
        {
            case 1:  // Warrior
            case 2:  // Paladin
            case 6:  // Death Knight
                return ItemCategory::ORE; // Likely has mining

            case 3:  // Hunter
            case 4:  // Rogue
            case 11: // Druid
                return ItemCategory::LEATHER; // Likely has skinning

            case 5:  // Priest
            case 8:  // Mage
            case 9:  // Warlock
                return ItemCategory::CLOTH; // Uses cloth

            case 7:  // Shaman
                return ItemCategory::HERB; // Often herbalism

            default:
                return ItemCategory::TRADE_GOODS;
        }
    }

    uint32 EconomySystem::GenerateGossipId()
    {
        return m_nextGossipId.fetch_add(1, std::memory_order_relaxed);
    }

} // namespace AliveWorld
