/*
 * mod-alive-world - Goal Manager Implementation
 * Copyright (C) 2025 EC
 */

#include "GoalManager.h"
#include "AliveWorldManager.h"
#include "BotMemoryManager.h"

#include "DatabaseEnv.h"
#include "DBCStores.h"
#include "QueryResult.h"

#include <algorithm>
#include <random>

namespace AliveWorld
{

// Inventory type constants
enum InventoryType
{
    INVTYPE_HEAD            = 1,
    INVTYPE_NECK            = 2,
    INVTYPE_SHOULDERS       = 3,
    INVTYPE_BODY            = 4,
    INVTYPE_CHEST           = 5,
    INVTYPE_WAIST           = 6,
    INVTYPE_LEGS            = 7,
    INVTYPE_FEET            = 8,
    INVTYPE_WRISTS          = 9,
    INVTYPE_HANDS           = 10,
    INVTYPE_FINGER          = 11,
    INVTYPE_TRINKET         = 12,
    INVTYPE_WEAPON          = 13,
    INVTYPE_SHIELD          = 14,
    INVTYPE_RANGED          = 15,
    INVTYPE_CLOAK           = 16,
    INVTYPE_2HWEAPON        = 17,
    INVTYPE_BAG             = 18,
    INVTYPE_TABARD          = 19,
    INVTYPE_ROBE            = 20,
    INVTYPE_WEAPONMAINHAND  = 21,
    INVTYPE_WEAPONOFFHAND   = 22,
    INVTYPE_HOLDABLE        = 23,
    INVTYPE_THROWN          = 25,
    INVTYPE_RANGEDRIGHT     = 26,
    INVTYPE_RELIC           = 28
};

// Item quality constants
enum ItemQuality
{
    ITEM_QUALITY_POOR       = 0,  // Gray
    ITEM_QUALITY_NORMAL     = 1,  // White
    ITEM_QUALITY_UNCOMMON   = 2,  // Green
    ITEM_QUALITY_RARE       = 3,  // Blue
    ITEM_QUALITY_EPIC       = 4,  // Purple
    ITEM_QUALITY_LEGENDARY  = 5,  // Orange
    ITEM_QUALITY_ARTIFACT   = 6   // Red
};

GoalManager::GoalManager()
    : m_nextGoalId(1)
    , m_maxGoalsPerBot(5)
    , m_completedGoals(0)
    , m_memoryManager(nullptr)
{
    std::random_device rd;
    m_rng.seed(rd());

    // Initialize profession gossip templates
    m_professionGossipTemplates = {
        "I just discovered a new recipe! Took me forever.",
        "Anyone else grinding {profession} skill? It's slow going.",
        "Finally hit 450 in {profession}. What a relief!",
        "I need more materials for my {profession}. This is expensive.",
        "Just learned a rare pattern. So excited!",
        "Inscription is such a grind. Discovery after discovery...",
        "My {profession} is coming along nicely. Almost master level.",
        "Anyone know where to get rare {profession} recipes?",
        "Crafting is the real endgame. Change my mind.",
        "I've been working on {profession} all week."
    };
}

void GoalManager::Initialize(BotMemoryManager* memoryManager)
{
    m_memoryManager = memoryManager;

    LoadItemDataFromDB();
    LoadFactionDataFromDB();
    LoadSkillDiscoveryData();
    LoadGoalTemplatesFromDB();

    ALIVE_LOG_INFO("GoalManager initialized with %u items, %u factions, %u discoverable recipes cached",
                   static_cast<uint32>(m_itemCache.size()),
                   static_cast<uint32>(m_factionCache.size()),
                   static_cast<uint32>(m_discoveries.size()));
}

void GoalManager::LoadGoalTemplatesFromDB()
{
    // Load any persisted goals from ec_bot_goals table
    QueryResult result = CharacterDatabase.Query(
        "SELECT goal_id, bot_guid, goal_type, priority, status, target_id, target_name, "
        "progress, progress_current, progress_target, created_at, updated_at, completed_at, reason "
        "FROM ec_bot_goals WHERE status IN (1, 2, 5)"  // ACTIVE, PAUSED, BLOCKED
    );

    if (!result)
    {
        ALIVE_LOG_DEBUG("No persisted goals found in database");
        return;
    }

    std::unique_lock<std::shared_mutex> lock(m_goalsMutex);
    std::unique_lock<std::shared_mutex> botLock(m_botGoalsMutex);

    uint32 loadedCount = 0;
    do
    {
        Field* fields = result->Fetch();

        BotGoal goal;
        goal.goalId = fields[0].Get<uint32>();
        goal.botGuid = fields[1].Get<uint64>();
        goal.type = static_cast<GoalType>(fields[2].Get<uint8>());
        goal.priority = static_cast<GoalPriority>(fields[3].Get<uint8>());
        goal.status = static_cast<GoalStatus>(fields[4].Get<uint8>());
        goal.targetId = fields[5].Get<uint32>();
        goal.targetName = fields[6].Get<std::string>();
        goal.progress = fields[7].Get<float>();
        goal.progressCurrent = fields[8].Get<uint32>();
        goal.progressTarget = fields[9].Get<uint32>();
        goal.createdAt = fields[10].Get<uint64>();
        goal.updatedAt = fields[11].Get<uint64>();
        goal.completedAt = fields[12].Get<uint64>();
        goal.reason = fields[13].Get<std::string>();

        m_goals[goal.goalId] = goal;
        m_botGoals[goal.botGuid].push_back(goal.goalId);

        if (goal.goalId >= m_nextGoalId)
            m_nextGoalId = goal.goalId + 1;

        loadedCount++;

    } while (result->NextRow());

    ALIVE_LOG_INFO("Loaded %u persisted goals from database", loadedCount);
}

void GoalManager::LoadItemDataFromDB()
{
    ALIVE_LOG_DEBUG("Loading item data from item_template...");

    // Load equipment items (quality >= green, level 10+)
    QueryResult result = WorldDatabase.Query(
        "SELECT entry, name, Quality, ItemLevel, RequiredLevel, InventoryType, "
        "class, subclass, SellPrice, AllowableClass "
        "FROM item_template "
        "WHERE Quality >= 2 AND RequiredLevel >= 10 AND RequiredLevel <= 80 "
        "AND InventoryType > 0 AND InventoryType <= 28 "
        "ORDER BY RequiredLevel, ItemLevel DESC "
        "LIMIT 5000"
    );

    if (!result)
    {
        ALIVE_LOG_WARN("Failed to load item data from item_template");
        return;
    }

    do
    {
        Field* fields = result->Fetch();

        ItemInfo item;
        item.itemId = fields[0].Get<uint32>();
        item.name = fields[1].Get<std::string>();
        item.quality = fields[2].Get<uint8>();
        item.itemLevel = fields[3].Get<uint32>();
        item.requiredLevel = fields[4].Get<uint8>();
        item.inventoryType = fields[5].Get<uint8>();
        item.itemClass = fields[6].Get<uint8>();
        item.itemSubClass = fields[7].Get<uint8>();
        item.sellPrice = fields[8].Get<uint32>();

        // Parse allowable classes bitmask
        int32 allowableClass = fields[9].Get<int32>();
        if (allowableClass == -1 || allowableClass == 0)
        {
            // All classes allowed
            for (uint8 c = 1; c <= 11; ++c)
                item.allowedClasses.push_back(c);
        }
        else
        {
            for (uint8 c = 1; c <= 11; ++c)
            {
                if (allowableClass & (1 << (c - 1)))
                    item.allowedClasses.push_back(c);
            }
        }

        m_itemCache[item.itemId] = item;

    } while (result->NextRow());

    ALIVE_LOG_INFO("Loaded %u items from item_template", static_cast<uint32>(m_itemCache.size()));
}

void GoalManager::LoadFactionDataFromDB()
{
    ALIVE_LOG_DEBUG("Loading faction data...");

    // Load major factions from faction_template
    // We'll use hardcoded data for the main WotLK factions
    // since faction data is in DBC files

    // Horde factions
    m_factionCache[76] = { 76, "Orgrimmar", 1, true, false, false };
    m_factionCache[68] = { 68, "Undercity", 1, true, false, false };
    m_factionCache[81] = { 81, "Thunder Bluff", 1, true, false, false };
    m_factionCache[530] = { 530, "Darkspear Trolls", 1, true, false, false };
    m_factionCache[911] = { 911, "Silvermoon City", 1, true, false, false };

    // Alliance factions
    m_factionCache[72] = { 72, "Stormwind", 1, false, true, false };
    m_factionCache[47] = { 47, "Ironforge", 1, false, true, false };
    m_factionCache[69] = { 69, "Darnassus", 1, false, true, false };
    m_factionCache[54] = { 54, "Gnomeregan Exiles", 1, false, true, false };
    m_factionCache[930] = { 930, "Exodar", 1, false, true, false };

    // Neutral factions (level-based)
    m_factionCache[21] = { 21, "Booty Bay", 30, false, false, true };
    m_factionCache[369] = { 369, "Gadgetzan", 40, false, false, true };
    m_factionCache[577] = { 577, "Everlook", 50, false, false, true };
    m_factionCache[470] = { 470, "Ratchet", 10, false, false, true };
    m_factionCache[609] = { 609, "Cenarion Circle", 55, false, false, true };
    m_factionCache[576] = { 576, "Timbermaw Hold", 50, false, false, true };
    m_factionCache[529] = { 529, "Argent Dawn", 50, false, false, true };
    m_factionCache[749] = { 749, "Hydraxian Waterlords", 55, false, false, true };
    m_factionCache[910] = { 910, "Brood of Nozdormu", 60, false, false, true };
    m_factionCache[989] = { 989, "Keepers of Time", 66, false, false, true };
    m_factionCache[935] = { 935, "The Sha'tar", 62, false, false, true };
    m_factionCache[942] = { 942, "Cenarion Expedition", 60, false, false, true };
    m_factionCache[970] = { 970, "Sporeggar", 62, false, false, true };
    m_factionCache[967] = { 967, "The Violet Eye", 70, false, false, true };
    m_factionCache[990] = { 990, "The Scale of the Sands", 70, false, false, true };
    m_factionCache[1012] = { 1012, "Ashtongue Deathsworn", 70, false, false, true };

    // WotLK factions
    m_factionCache[1037] = { 1037, "Alliance Vanguard", 68, false, true, false };
    m_factionCache[1052] = { 1052, "Horde Expedition", 68, true, false, false };
    m_factionCache[1090] = { 1090, "Kirin Tor", 68, false, false, true };
    m_factionCache[1091] = { 1091, "The Wyrmrest Accord", 68, false, false, true };
    m_factionCache[1098] = { 1098, "Knights of the Ebon Blade", 77, false, false, true };
    m_factionCache[1106] = { 1106, "Argent Crusade", 77, false, false, true };
    m_factionCache[1104] = { 1104, "Frenzyheart Tribe", 76, false, false, true };
    m_factionCache[1105] = { 1105, "The Oracles", 76, false, false, true };
    m_factionCache[1119] = { 1119, "The Sons of Hodir", 77, false, false, true };
    m_factionCache[1156] = { 1156, "The Ashen Verdict", 80, false, false, true };

    ALIVE_LOG_INFO("Loaded %u faction definitions", static_cast<uint32>(m_factionCache.size()));
}

uint32 GoalManager::GenerateGoalId()
{
    return m_nextGoalId++;
}

void GoalManager::GenerateGoalsForBot(uint64 botGuid, uint8 botLevel, uint8 botClass, uint8 botRace)
{
    if (!m_memoryManager)
        return;

    // Get bot personality
    BotMemoryData* memory = m_memoryManager->GetBotMemory(botGuid);
    if (!memory)
        return;

    const BotPersonality& personality = memory->personality;

    // Check current goal count
    uint32 currentGoals = GetGoalCountForBot(botGuid);
    if (currentGoals >= m_maxGoalsPerBot)
    {
        ALIVE_LOG_DEBUG("Bot %llu already has %u goals (max: %u)", botGuid, currentGoals, m_maxGoalsPerBot);
        return;
    }

    uint32 goalsToGenerate = m_maxGoalsPerBot - currentGoals;

    // Generate goals based on personality traits
    for (uint32 i = 0; i < goalsToGenerate; ++i)
    {
        BotGoal goal;

        // Decide goal type based on personality
        float roll = std::uniform_real_distribution<float>(0.0f, 1.0f)(m_rng);

        if (personality.greed > 0.7f && roll < 0.3f)
        {
            // Greedy bots want gold
            goal = GenerateGoldGoal(botGuid, personality);
        }
        else if (personality.aggression > 0.7f && roll < 0.5f)
        {
            // Aggressive bots want PvP or combat
            goal = GeneratePvPGoal(botGuid, personality);
        }
        else if (personality.curiosity > 0.7f && roll < 0.4f)
        {
            // Curious bots want to explore
            goal = GenerateExploreGoal(botGuid, botLevel, personality);
        }
        else if (personality.sociability > 0.7f && roll < 0.3f)
        {
            // Social bots want reputation
            goal = GenerateReputationGoal(botGuid, botLevel, personality);
        }
        else
        {
            // Default: item upgrade goal
            goal = GenerateItemGoal(botGuid, botLevel, personality);
        }

        if (goal.goalId > 0)
        {
            AddGoal(goal);
            ALIVE_LOG_DEBUG("Generated %s goal for bot %llu: %s",
                           GoalTypeToString(goal.type), botGuid, goal.targetName.c_str());
        }
    }
}

BotGoal GoalManager::GenerateItemGoal(uint64 botGuid, uint8 level, const BotPersonality& personality)
{
    BotGoal goal;
    goal.goalId = GenerateGoalId();
    goal.botGuid = botGuid;
    goal.type = GoalType::ITEM;
    goal.status = GoalStatus::ACTIVE;
    goal.createdAt = NowMs();
    goal.updatedAt = goal.createdAt;
    goal.progressCurrent = 0;
    goal.progressTarget = 1;

    // Find upgrade items for this level
    std::vector<uint32> candidates;
    for (const auto& [itemId, item] : m_itemCache)
    {
        // Items within level range
        if (item.requiredLevel <= level && item.requiredLevel >= level - 10)
        {
            // Quality preference based on personality
            uint8 minQuality = personality.greed > 0.6f ? ITEM_QUALITY_RARE : ITEM_QUALITY_UNCOMMON;
            if (item.quality >= minQuality)
            {
                candidates.push_back(itemId);
            }
        }
    }

    if (candidates.empty())
    {
        goal.goalId = 0;  // Invalid
        return goal;
    }

    // Pick random item
    std::uniform_int_distribution<size_t> dist(0, candidates.size() - 1);
    uint32 chosenItem = candidates[dist(m_rng)];
    const ItemInfo& item = m_itemCache[chosenItem];

    goal.targetId = chosenItem;
    goal.targetName = item.name;

    // Priority based on item quality
    if (item.quality >= ITEM_QUALITY_EPIC)
        goal.priority = GoalPriority::HIGH;
    else if (item.quality >= ITEM_QUALITY_RARE)
        goal.priority = GoalPriority::MEDIUM;
    else
        goal.priority = GoalPriority::LOW;

    // Reason based on personality
    if (personality.greed > 0.7f)
        goal.reason = "This would sell for a nice price";
    else if (personality.caution > 0.7f)
        goal.reason = "Better gear means better survival";
    else
        goal.reason = "Time for an upgrade";

    return goal;
}

BotGoal GoalManager::GenerateReputationGoal(uint64 botGuid, uint8 level, const BotPersonality& personality)
{
    BotGoal goal;
    goal.goalId = GenerateGoalId();
    goal.botGuid = botGuid;
    goal.type = GoalType::REPUTATION;
    goal.status = GoalStatus::ACTIVE;
    goal.createdAt = NowMs();
    goal.updatedAt = goal.createdAt;
    goal.progressCurrent = 0;
    goal.progressTarget = 21000;  // Revered

    // Find appropriate factions for level
    std::vector<uint32> candidates;
    for (const auto& [factionId, faction] : m_factionCache)
    {
        if (faction.minLevel <= level && faction.isNeutral)
        {
            candidates.push_back(factionId);
        }
    }

    if (candidates.empty())
    {
        goal.goalId = 0;
        return goal;
    }

    std::uniform_int_distribution<size_t> dist(0, candidates.size() - 1);
    uint32 chosenFaction = candidates[dist(m_rng)];
    const FactionInfo& faction = m_factionCache[chosenFaction];

    goal.targetId = chosenFaction;
    goal.targetName = faction.name;
    goal.priority = GoalPriority::MEDIUM;

    if (personality.sociability > 0.7f)
        goal.reason = "Making friends is important";
    else if (personality.loyalty > 0.7f)
        goal.reason = "I want to prove my allegiance";
    else
        goal.reason = "The rewards seem worthwhile";

    return goal;
}

BotGoal GoalManager::GenerateGoldGoal(uint64 botGuid, const BotPersonality& personality)
{
    BotGoal goal;
    goal.goalId = GenerateGoalId();
    goal.botGuid = botGuid;
    goal.type = GoalType::GOLD;
    goal.status = GoalStatus::ACTIVE;
    goal.createdAt = NowMs();
    goal.updatedAt = goal.createdAt;
    goal.progressCurrent = 0;

    // Target based on greed level
    uint32 goldTarget = 100;  // 1 gold base
    if (personality.greed > 0.8f)
        goldTarget = 10000;  // 100 gold
    else if (personality.greed > 0.6f)
        goldTarget = 5000;   // 50 gold
    else if (personality.greed > 0.4f)
        goldTarget = 1000;   // 10 gold

    goal.targetId = goldTarget;
    goal.targetName = std::to_string(goldTarget / 100) + " gold";
    goal.progressTarget = goldTarget;
    goal.priority = personality.greed > 0.7f ? GoalPriority::HIGH : GoalPriority::LOW;
    goal.reason = personality.greed > 0.7f ? "Gold is everything!" : "A little savings never hurt";

    return goal;
}

BotGoal GoalManager::GenerateExploreGoal(uint64 botGuid, uint8 level, const BotPersonality& personality)
{
    BotGoal goal;
    goal.goalId = GenerateGoalId();
    goal.botGuid = botGuid;
    goal.type = GoalType::EXPLORE;
    goal.status = GoalStatus::ACTIVE;
    goal.createdAt = NowMs();
    goal.updatedAt = goal.createdAt;
    goal.progressCurrent = 0;
    goal.progressTarget = 1;

    // Exploration zones by level
    std::vector<std::pair<uint32, std::string>> zones;
    if (level < 20)
    {
        zones = { {1, "Dun Morogh"}, {12, "Elwynn Forest"}, {14, "Durotar"}, {85, "Tirisfal Glades"} };
    }
    else if (level < 40)
    {
        zones = { {15, "Dustwallow Marsh"}, {33, "Stranglethorn Vale"}, {440, "Tanaris"}, {3, "Badlands"} };
    }
    else if (level < 60)
    {
        zones = { {1377, "Silithus"}, {618, "Winterspring"}, {139, "Eastern Plaguelands"}, {28, "Western Plaguelands"} };
    }
    else if (level < 70)
    {
        zones = { {3483, "Hellfire Peninsula"}, {3518, "Nagrand"}, {3519, "Terokkar Forest"}, {3521, "Zangarmarsh"} };
    }
    else
    {
        zones = { {3537, "Borean Tundra"}, {495, "Howling Fjord"}, {66, "Zul'Drak"}, {67, "Storm Peaks"} };
    }

    if (zones.empty())
    {
        goal.goalId = 0;
        return goal;
    }

    std::uniform_int_distribution<size_t> dist(0, zones.size() - 1);
    auto& chosen = zones[dist(m_rng)];

    goal.targetId = chosen.first;
    goal.targetName = chosen.second;
    goal.priority = personality.curiosity > 0.7f ? GoalPriority::MEDIUM : GoalPriority::LOW;
    goal.reason = personality.curiosity > 0.7f ? "I wonder what's out there..." : "Should check that place out";

    return goal;
}

BotGoal GoalManager::GeneratePvPGoal(uint64 botGuid, const BotPersonality& personality)
{
    BotGoal goal;
    goal.goalId = GenerateGoalId();
    goal.botGuid = botGuid;
    goal.type = GoalType::PVP;
    goal.status = GoalStatus::ACTIVE;
    goal.createdAt = NowMs();
    goal.updatedAt = goal.createdAt;
    goal.progressCurrent = 0;

    // Kill target based on aggression
    uint32 killTarget = 5;
    if (personality.aggression > 0.8f)
        killTarget = 25;
    else if (personality.aggression > 0.6f)
        killTarget = 15;
    else if (personality.aggression > 0.4f)
        killTarget = 10;

    goal.targetId = killTarget;
    goal.targetName = std::to_string(killTarget) + " honorable kills";
    goal.progressTarget = killTarget;
    goal.priority = personality.aggression > 0.7f ? GoalPriority::HIGH : GoalPriority::MEDIUM;
    goal.reason = personality.aggression > 0.7f ? "Blood and glory!" : "Testing my skills in battle";

    return goal;
}

uint32 GoalManager::AddGoal(const BotGoal& goal)
{
    std::unique_lock<std::shared_mutex> lock(m_goalsMutex);
    std::unique_lock<std::shared_mutex> botLock(m_botGoalsMutex);

    m_goals[goal.goalId] = goal;
    m_botGoals[goal.botGuid].push_back(goal.goalId);

    ALIVE_LOG_DEBUG("Added goal %u for bot %llu: %s (%s)",
                   goal.goalId, goal.botGuid, goal.targetName.c_str(),
                   GoalTypeToString(goal.type));

    return goal.goalId;
}

bool GoalManager::RemoveGoal(uint32 goalId)
{
    std::unique_lock<std::shared_mutex> lock(m_goalsMutex);

    auto it = m_goals.find(goalId);
    if (it == m_goals.end())
        return false;

    uint64 botGuid = it->second.botGuid;
    m_goals.erase(it);

    // Remove from bot's goal list
    std::unique_lock<std::shared_mutex> botLock(m_botGoalsMutex);
    auto& botGoalList = m_botGoals[botGuid];
    botGoalList.erase(std::remove(botGoalList.begin(), botGoalList.end(), goalId), botGoalList.end());

    return true;
}

bool GoalManager::UpdateGoalProgress(uint32 goalId, uint32 current, uint32 target)
{
    std::unique_lock<std::shared_mutex> lock(m_goalsMutex);

    auto it = m_goals.find(goalId);
    if (it == m_goals.end())
        return false;

    it->second.UpdateProgress(current, target);

    if (it->second.IsComplete())
    {
        m_completedGoals++;
        ALIVE_LOG_INFO("Goal %u completed for bot %llu: %s",
                      goalId, it->second.botGuid, it->second.targetName.c_str());
    }

    return true;
}

bool GoalManager::SetGoalStatus(uint32 goalId, GoalStatus status)
{
    std::unique_lock<std::shared_mutex> lock(m_goalsMutex);

    auto it = m_goals.find(goalId);
    if (it == m_goals.end())
        return false;

    it->second.status = status;
    it->second.updatedAt = NowMs();

    if (status == GoalStatus::COMPLETED)
    {
        it->second.completedAt = NowMs();
        m_completedGoals++;
    }

    return true;
}

void GoalManager::CleanupCompletedGoals(uint64 botGuid)
{
    std::unique_lock<std::shared_mutex> lock(m_goalsMutex);
    std::unique_lock<std::shared_mutex> botLock(m_botGoalsMutex);

    auto& goalIds = m_botGoals[botGuid];
    std::vector<uint32> toRemove;

    for (uint32 goalId : goalIds)
    {
        auto it = m_goals.find(goalId);
        if (it != m_goals.end() &&
            (it->second.status == GoalStatus::COMPLETED ||
             it->second.status == GoalStatus::ABANDONED))
        {
            toRemove.push_back(goalId);
        }
    }

    for (uint32 goalId : toRemove)
    {
        m_goals.erase(goalId);
        goalIds.erase(std::remove(goalIds.begin(), goalIds.end(), goalId), goalIds.end());
    }
}

const BotGoal* GoalManager::GetGoal(uint32 goalId) const
{
    std::shared_lock<std::shared_mutex> lock(m_goalsMutex);

    auto it = m_goals.find(goalId);
    return it != m_goals.end() ? &it->second : nullptr;
}

std::vector<BotGoal> GoalManager::GetGoalsForBot(uint64 botGuid) const
{
    std::vector<BotGoal> result;

    std::shared_lock<std::shared_mutex> botLock(m_botGoalsMutex);
    auto it = m_botGoals.find(botGuid);
    if (it == m_botGoals.end())
        return result;

    std::shared_lock<std::shared_mutex> lock(m_goalsMutex);
    for (uint32 goalId : it->second)
    {
        auto goalIt = m_goals.find(goalId);
        if (goalIt != m_goals.end())
            result.push_back(goalIt->second);
    }

    return result;
}

std::vector<BotGoal> GoalManager::GetActiveGoalsForBot(uint64 botGuid) const
{
    std::vector<BotGoal> allGoals = GetGoalsForBot(botGuid);
    std::vector<BotGoal> active;

    std::copy_if(allGoals.begin(), allGoals.end(), std::back_inserter(active),
                 [](const BotGoal& g) { return g.IsActive(); });

    return active;
}

const BotGoal* GoalManager::GetHighestPriorityGoal(uint64 botGuid) const
{
    std::vector<BotGoal> active = GetActiveGoalsForBot(botGuid);
    if (active.empty())
        return nullptr;

    // Sort by priority (lower number = higher priority)
    std::sort(active.begin(), active.end(),
              [](const BotGoal& a, const BotGoal& b) {
                  return static_cast<uint8>(a.priority) < static_cast<uint8>(b.priority);
              });

    // Store and return pointer to highest priority
    std::shared_lock<std::shared_mutex> lock(m_goalsMutex);
    auto it = m_goals.find(active.front().goalId);
    return it != m_goals.end() ? &it->second : nullptr;
}

uint32 GoalManager::GetGoalCountForBot(uint64 botGuid) const
{
    std::shared_lock<std::shared_mutex> lock(m_botGoalsMutex);

    auto it = m_botGoals.find(botGuid);
    return it != m_botGoals.end() ? static_cast<uint32>(it->second.size()) : 0;
}

uint32 GoalManager::GetActiveGoalCount() const
{
    std::shared_lock<std::shared_mutex> lock(m_goalsMutex);

    uint32 count = 0;
    for (const auto& [id, goal] : m_goals)
    {
        if (goal.IsActive())
            count++;
    }
    return count;
}

// Progress event handlers
void GoalManager::OnItemLooted(uint64 botGuid, uint32 itemId, uint32 count)
{
    std::shared_lock<std::shared_mutex> botLock(m_botGoalsMutex);

    auto it = m_botGoals.find(botGuid);
    if (it == m_botGoals.end())
        return;

    for (uint32 goalId : it->second)
    {
        const BotGoal* goal = GetGoal(goalId);
        if (goal && goal->type == GoalType::ITEM && goal->targetId == itemId && goal->IsActive())
        {
            UpdateGoalProgress(goalId, goal->progressCurrent + count, goal->progressTarget);
            break;
        }
    }
}

void GoalManager::OnReputationGained(uint64 botGuid, uint32 factionId, int32 repChange)
{
    std::shared_lock<std::shared_mutex> botLock(m_botGoalsMutex);

    auto it = m_botGoals.find(botGuid);
    if (it == m_botGoals.end())
        return;

    for (uint32 goalId : it->second)
    {
        const BotGoal* goal = GetGoal(goalId);
        if (goal && goal->type == GoalType::REPUTATION && goal->targetId == factionId && goal->IsActive())
        {
            int32 newProgress = static_cast<int32>(goal->progressCurrent) + repChange;
            if (newProgress < 0) newProgress = 0;
            UpdateGoalProgress(goalId, static_cast<uint32>(newProgress), goal->progressTarget);
            break;
        }
    }
}

void GoalManager::OnGoldChanged(uint64 botGuid, uint32 newGoldAmount)
{
    std::shared_lock<std::shared_mutex> botLock(m_botGoalsMutex);

    auto it = m_botGoals.find(botGuid);
    if (it == m_botGoals.end())
        return;

    for (uint32 goalId : it->second)
    {
        const BotGoal* goal = GetGoal(goalId);
        if (goal && goal->type == GoalType::GOLD && goal->IsActive())
        {
            UpdateGoalProgress(goalId, newGoldAmount, goal->progressTarget);
            break;
        }
    }
}

void GoalManager::OnZoneEntered(uint64 botGuid, uint32 zoneId)
{
    std::shared_lock<std::shared_mutex> botLock(m_botGoalsMutex);

    auto it = m_botGoals.find(botGuid);
    if (it == m_botGoals.end())
        return;

    for (uint32 goalId : it->second)
    {
        const BotGoal* goal = GetGoal(goalId);
        if (goal && goal->type == GoalType::EXPLORE && goal->targetId == zoneId && goal->IsActive())
        {
            UpdateGoalProgress(goalId, 1, 1);  // Complete!
            break;
        }
    }
}

void GoalManager::OnKill(uint64 botGuid, uint32 creatureEntry, bool isPvP)
{
    if (!isPvP)
        return;

    std::shared_lock<std::shared_mutex> botLock(m_botGoalsMutex);

    auto it = m_botGoals.find(botGuid);
    if (it == m_botGoals.end())
        return;

    for (uint32 goalId : it->second)
    {
        const BotGoal* goal = GetGoal(goalId);
        if (goal && goal->type == GoalType::PVP && goal->IsActive())
        {
            UpdateGoalProgress(goalId, goal->progressCurrent + 1, goal->progressTarget);
            break;
        }
    }
}

void GoalManager::OnLevelUp(uint64 botGuid, uint8 newLevel)
{
    std::shared_lock<std::shared_mutex> botLock(m_botGoalsMutex);

    auto it = m_botGoals.find(botGuid);
    if (it == m_botGoals.end())
        return;

    for (uint32 goalId : it->second)
    {
        const BotGoal* goal = GetGoal(goalId);
        if (goal && goal->type == GoalType::LEVEL && goal->IsActive())
        {
            UpdateGoalProgress(goalId, newLevel, goal->progressTarget);
            break;
        }
    }
}

void GoalManager::SaveGoalsToDB(uint64 botGuid)
{
    std::vector<BotGoal> goals = GetGoalsForBot(botGuid);

    for (const auto& goal : goals)
    {
        CharacterDatabase.Execute(
            "REPLACE INTO ec_bot_goals "
            "(goal_id, bot_guid, goal_type, priority, status, target_id, target_name, "
            "progress, progress_current, progress_target, created_at, updated_at, completed_at, reason) "
            "VALUES (%u, %llu, %u, %u, %u, %u, '%s', %f, %u, %u, %llu, %llu, %llu, '%s')",
            goal.goalId, goal.botGuid,
            static_cast<uint8>(goal.type), static_cast<uint8>(goal.priority),
            static_cast<uint8>(goal.status), goal.targetId, goal.targetName.c_str(),
            goal.progress, goal.progressCurrent, goal.progressTarget,
            goal.createdAt, goal.updatedAt, goal.completedAt, goal.reason.c_str()
        );
    }
}

void GoalManager::LoadGoalsFromDB(uint64 botGuid)
{
    QueryResult result = CharacterDatabase.Query(
        "SELECT goal_id, goal_type, priority, status, target_id, target_name, "
        "progress, progress_current, progress_target, created_at, updated_at, completed_at, reason "
        "FROM ec_bot_goals WHERE bot_guid = %llu AND status IN (1, 2, 5)",
        botGuid
    );

    if (!result)
        return;

    std::unique_lock<std::shared_mutex> lock(m_goalsMutex);
    std::unique_lock<std::shared_mutex> botLock(m_botGoalsMutex);

    do
    {
        Field* fields = result->Fetch();

        BotGoal goal;
        goal.goalId = fields[0].Get<uint32>();
        goal.botGuid = botGuid;
        goal.type = static_cast<GoalType>(fields[1].Get<uint8>());
        goal.priority = static_cast<GoalPriority>(fields[2].Get<uint8>());
        goal.status = static_cast<GoalStatus>(fields[3].Get<uint8>());
        goal.targetId = fields[4].Get<uint32>();
        goal.targetName = fields[5].Get<std::string>();
        goal.progress = fields[6].Get<float>();
        goal.progressCurrent = fields[7].Get<uint32>();
        goal.progressTarget = fields[8].Get<uint32>();
        goal.createdAt = fields[9].Get<uint64>();
        goal.updatedAt = fields[10].Get<uint64>();
        goal.completedAt = fields[11].Get<uint64>();
        goal.reason = fields[12].Get<std::string>();

        m_goals[goal.goalId] = goal;
        m_botGoals[botGuid].push_back(goal.goalId);

        if (goal.goalId >= m_nextGoalId)
            m_nextGoalId = goal.goalId + 1;

    } while (result->NextRow());
}

uint32 GoalManager::GetTotalGoalCount() const
{
    std::shared_lock<std::shared_mutex> lock(m_goalsMutex);
    return static_cast<uint32>(m_goals.size());
}

uint32 GoalManager::GetCompletedGoalCount() const
{
    return m_completedGoals;
}

float GoalManager::GetAverageProgress() const
{
    std::shared_lock<std::shared_mutex> lock(m_goalsMutex);

    if (m_goals.empty())
        return 0.0f;

    float totalProgress = 0.0f;
    for (const auto& [id, goal] : m_goals)
    {
        totalProgress += goal.progress;
    }

    return totalProgress / static_cast<float>(m_goals.size());
}

// ============================================================================
// Skill Discovery System (Issue #48)
// ============================================================================

// Skill IDs for primary professions
namespace SkillIds
{
    constexpr uint32 ALCHEMY        = 171;
    constexpr uint32 BLACKSMITHING  = 164;
    constexpr uint32 ENCHANTING     = 333;
    constexpr uint32 ENGINEERING    = 202;
    constexpr uint32 HERBALISM      = 182;
    constexpr uint32 INSCRIPTION    = 773;
    constexpr uint32 JEWELCRAFTING  = 755;
    constexpr uint32 LEATHERWORKING = 165;
    constexpr uint32 MINING         = 186;
    constexpr uint32 SKINNING       = 393;
    constexpr uint32 TAILORING      = 197;
    constexpr uint32 COOKING        = 185;
    constexpr uint32 FIRST_AID      = 129;
    constexpr uint32 FISHING        = 356;
}

// Skill ID to name mapping
static const char* GetSkillName(uint32 skillId)
{
    switch (skillId)
    {
        case SkillIds::ALCHEMY:        return "Alchemy";
        case SkillIds::BLACKSMITHING:  return "Blacksmithing";
        case SkillIds::ENCHANTING:     return "Enchanting";
        case SkillIds::ENGINEERING:    return "Engineering";
        case SkillIds::HERBALISM:      return "Herbalism";
        case SkillIds::INSCRIPTION:    return "Inscription";
        case SkillIds::JEWELCRAFTING:  return "Jewelcrafting";
        case SkillIds::LEATHERWORKING: return "Leatherworking";
        case SkillIds::MINING:         return "Mining";
        case SkillIds::SKINNING:       return "Skinning";
        case SkillIds::TAILORING:      return "Tailoring";
        case SkillIds::COOKING:        return "Cooking";
        case SkillIds::FIRST_AID:      return "First Aid";
        case SkillIds::FISHING:        return "Fishing";
        default:                       return "Unknown";
    }
}

void GoalManager::LoadSkillDiscoveryData()
{
    ALIVE_LOG_DEBUG("Loading skill discovery data from skill_discovery_template...");

    // skill_discovery_template columns: spellId, reqSpell, reqSkillValue, chance
    // Note: reqSpell can be 0 (no requirement), or it references another spell
    // chance is float (0.0-100.0)
    QueryResult result = WorldDatabase.Query(
        "SELECT spellId, reqSpell, reqSkillValue, chance "
        "FROM skill_discovery_template "
        "ORDER BY reqSkillValue ASC"
    );

    if (!result)
    {
        ALIVE_LOG_WARN("Failed to load skill discovery data - table may not exist");
        return;
    }

    m_discoveries.clear();
    m_discoveriesBySkill.clear();

    // We need to figure out which skill each discovery belongs to
    // We'll use Spell.dbc data cached in the world - for now use a secondary query
    // to get skill from skill_line_ability

    do
    {
        Field* fields = result->Fetch();

        DiscoveryInfo info;
        info.spellId = fields[0].Get<uint32>();
        info.reqSpell = fields[1].Get<uint32>();
        info.reqSkillValue = fields[2].Get<uint16>();
        info.chance = fields[3].Get<float>();

        m_discoveries.push_back(info);

    } while (result->NextRow());

    // Now map discoveries to skills via skill_line_ability
    QueryResult skillResult = WorldDatabase.Query(
        "SELECT sla.Spell, sla.SkillLine "
        "FROM skill_discovery_template sdt "
        "JOIN skill_line_ability sla ON sdt.spellId = sla.Spell"
    );

    if (skillResult)
    {
        do
        {
            Field* fields = skillResult->Fetch();
            uint32 spellId = fields[0].Get<uint32>();
            uint32 skillId = fields[1].Get<uint32>();

            m_discoveriesBySkill[skillId].push_back(spellId);

        } while (skillResult->NextRow());
    }

    ALIVE_LOG_INFO("Loaded %u skill discoveries across %u skills",
                   static_cast<uint32>(m_discoveries.size()),
                   static_cast<uint32>(m_discoveriesBySkill.size()));
}

std::vector<uint32> GoalManager::GetDiscoverableRecipes(uint32 skillId, uint16 skillValue) const
{
    std::vector<uint32> result;

    auto it = m_discoveriesBySkill.find(skillId);
    if (it == m_discoveriesBySkill.end())
        return result;

    // Filter by skill value requirement
    for (uint32 spellId : it->second)
    {
        // Find the discovery info for this spell
        for (const auto& disc : m_discoveries)
        {
            if (disc.spellId == spellId && disc.reqSkillValue <= skillValue)
            {
                result.push_back(spellId);
                break;
            }
        }
    }

    return result;
}

std::string GoalManager::GenerateProfessionGossip() const
{
    if (m_professionGossipTemplates.empty())
        return "";

    // Pick a random template
    std::uniform_int_distribution<size_t> dist(0, m_professionGossipTemplates.size() - 1);
    std::string text = m_professionGossipTemplates[dist(m_rng)];

    // Pick a random profession to mention
    static const char* professions[] = {
        "Alchemy", "Blacksmithing", "Enchanting", "Engineering",
        "Inscription", "Jewelcrafting", "Leatherworking", "Tailoring",
        "Herbalism", "Mining", "Skinning", "Cooking"
    };
    std::uniform_int_distribution<size_t> profDist(0, 11);
    std::string profession = professions[profDist(m_rng)];

    // Replace {profession} placeholder
    size_t pos = text.find("{profession}");
    if (pos != std::string::npos)
    {
        text.replace(pos, 12, profession);
    }

    return text;
}

BotGoal GoalManager::GenerateProfessionGoal(uint64 botGuid, uint8 level, const BotPersonality& personality)
{
    BotGoal goal;
    goal.goalId = GenerateGoalId();
    goal.botGuid = botGuid;
    goal.type = GoalType::PROFESSION;
    goal.status = GoalStatus::ACTIVE;
    goal.createdAt = NowMs();
    goal.updatedAt = goal.createdAt;
    goal.progressCurrent = 0;

    // Pick a random profession skill ID
    static const uint32 craftingSkills[] = {
        SkillIds::ALCHEMY, SkillIds::BLACKSMITHING, SkillIds::ENCHANTING,
        SkillIds::ENGINEERING, SkillIds::INSCRIPTION, SkillIds::JEWELCRAFTING,
        SkillIds::LEATHERWORKING, SkillIds::TAILORING
    };

    std::uniform_int_distribution<size_t> dist(0, 7);
    uint32 skillId = craftingSkills[dist(m_rng)];
    const char* skillName = GetSkillName(skillId);

    // Target skill level based on bot level
    uint16 targetSkillLevel = 75;  // Apprentice
    if (level >= 70)
        targetSkillLevel = 450;    // Grand Master
    else if (level >= 60)
        targetSkillLevel = 375;    // Master
    else if (level >= 50)
        targetSkillLevel = 300;    // Artisan
    else if (level >= 35)
        targetSkillLevel = 225;    // Expert
    else if (level >= 20)
        targetSkillLevel = 150;    // Journeyman
    else if (level >= 10)
        targetSkillLevel = 75;     // Apprentice

    goal.targetId = skillId;
    goal.targetName = std::string(skillName) + " (" + std::to_string(targetSkillLevel) + ")";
    goal.progressTarget = targetSkillLevel;

    // Priority based on personality traits
    if (personality.greed > 0.7f)
    {
        goal.priority = GoalPriority::HIGH;
        goal.reason = "Crafting is great for making gold!";
    }
    else if (personality.sociability > 0.6f)
    {
        goal.priority = GoalPriority::MEDIUM;
        goal.reason = "I can craft things for my friends.";
    }
    else if (personality.curiosity > 0.6f)
    {
        goal.priority = GoalPriority::MEDIUM;
        goal.reason = "I want to discover new recipes!";
    }
    else
    {
        goal.priority = GoalPriority::LOW;
        goal.reason = "Might as well learn a trade.";
    }

    // Check if there are discoverable recipes for this skill at a reasonable level
    std::vector<uint32> discoveries = GetDiscoverableRecipes(skillId, targetSkillLevel);
    if (!discoveries.empty())
    {
        goal.reason += " There are " + std::to_string(discoveries.size()) + " recipes to discover!";
    }

    ALIVE_LOG_DEBUG("Generated profession goal for bot %llu: %s (target: %u)",
                   botGuid, skillName, targetSkillLevel);

    return goal;
}

// ============================================================================
// Item Query Helpers (Issue #45)
// ============================================================================

std::vector<uint32> GoalManager::GetUpgradeItemsForLevel(uint8 level, uint8 playerClass, uint8 slot)
{
    std::vector<uint32> result;

    for (const auto& [itemId, item] : m_itemCache)
    {
        // Check level range (current level to 5 levels higher)
        if (item.requiredLevel > level + 5 || item.requiredLevel < level - 5)
            continue;

        // Check slot matches
        if (item.inventoryType != slot)
            continue;

        // Check class can use this item
        bool canUse = false;
        for (uint8 allowedClass : item.allowedClasses)
        {
            if (allowedClass == playerClass)
            {
                canUse = true;
                break;
            }
        }
        if (!canUse)
            continue;

        result.push_back(itemId);
    }

    // Sort by item level descending (best items first)
    std::sort(result.begin(), result.end(), [this](uint32 a, uint32 b) {
        return m_itemCache[a].itemLevel > m_itemCache[b].itemLevel;
    });

    // Limit to top 20 items
    if (result.size() > 20)
        result.resize(20);

    return result;
}

std::vector<uint32> GoalManager::GetCraftingMatsForLevel(uint8 level)
{
    std::vector<uint32> result;

    // Crafting materials are typically itemClass 7 (Trade Goods) or 9 (Recipe)
    // Query database for these since they're not in our gear cache
    QueryResult queryResult = WorldDatabase.Query(
        "SELECT entry, name, SellPrice, RequiredLevel "
        "FROM item_template "
        "WHERE class = 7 AND RequiredLevel <= %u AND RequiredLevel >= %u "
        "AND SellPrice > 0 "
        "ORDER BY SellPrice DESC "
        "LIMIT 50",
        level + 10, std::max(1, level - 20)
    );

    if (!queryResult)
        return result;

    do
    {
        Field* fields = queryResult->Fetch();
        result.push_back(fields[0].Get<uint32>());
    } while (queryResult->NextRow());

    return result;
}

std::vector<uint32> GoalManager::GetConsumablesForLevel(uint8 level)
{
    std::vector<uint32> result;

    // Consumables are itemClass 0 (Consumable)
    // SubClass: 0=Consumable, 1=Potion, 2=Elixir, 3=Flask, 4=Scroll, 5=Food & Drink
    QueryResult queryResult = WorldDatabase.Query(
        "SELECT entry, name, SellPrice, RequiredLevel "
        "FROM item_template "
        "WHERE class = 0 AND subclass IN (0, 1, 2, 3, 4, 5) "
        "AND RequiredLevel <= %u AND RequiredLevel >= %u "
        "AND Quality >= 1 "
        "ORDER BY RequiredLevel DESC, SellPrice DESC "
        "LIMIT 100",
        level, std::max(1, level - 15)
    );

    if (!queryResult)
        return result;

    do
    {
        Field* fields = queryResult->Fetch();
        result.push_back(fields[0].Get<uint32>());
    } while (queryResult->NextRow());

    return result;
}

// Additional helper: Get items by value tier for economic goals
std::vector<uint32> GoalManager::GetItemsByValueRange(uint32 minValue, uint32 maxValue) const
{
    std::vector<uint32> result;

    for (const auto& [itemId, item] : m_itemCache)
    {
        if (item.sellPrice >= minValue && item.sellPrice <= maxValue)
        {
            result.push_back(itemId);
        }
    }

    return result;
}

// Get best upgrade for a specific slot
const GoalManager::ItemInfo* GoalManager::GetBestUpgradeForSlot(uint8 level, uint8 playerClass, uint8 slot) const
{
    const ItemInfo* best = nullptr;
    uint32 bestItemLevel = 0;

    for (const auto& [itemId, item] : m_itemCache)
    {
        // Check level range
        if (item.requiredLevel > level || item.requiredLevel < level - 10)
            continue;

        // Check slot
        if (item.inventoryType != slot)
            continue;

        // Check class can use
        bool canUse = false;
        for (uint8 allowedClass : item.allowedClasses)
        {
            if (allowedClass == playerClass)
            {
                canUse = true;
                break;
            }
        }
        if (!canUse)
            continue;

        // Track best
        if (item.itemLevel > bestItemLevel)
        {
            bestItemLevel = item.itemLevel;
            best = &item;
        }
    }

    return best;
}

// Generate gear gossip
std::string GoalManager::GenerateGearGossip(uint8 level, uint8 playerClass) const
{
    static const std::vector<std::string> templates = {
        "I really need a new {slot}. Mine is getting worn out.",
        "Anyone know where to find a good {slot} for level {level}?",
        "Just got a new {item}! Best upgrade I've had in a while.",
        "Looking for {slot} upgrades. The drops here aren't great.",
        "I've been farming for a better {slot} for days.",
        "My {slot} is terrible. Time for an upgrade.",
        "Finally replaced my old {slot}. What a relief!",
        "The {item} dropped! I can't believe my luck!"
    };

    static const std::vector<std::pair<uint8, std::string>> slotNames = {
        {INVTYPE_HEAD, "helm"},
        {INVTYPE_CHEST, "chest piece"},
        {INVTYPE_SHOULDERS, "shoulders"},
        {INVTYPE_LEGS, "leggings"},
        {INVTYPE_FEET, "boots"},
        {INVTYPE_HANDS, "gloves"},
        {INVTYPE_WEAPON, "weapon"},
        {INVTYPE_SHIELD, "shield"},
        {INVTYPE_CLOAK, "cloak"}
    };

    // Pick random template
    std::uniform_int_distribution<size_t> templDist(0, templates.size() - 1);
    std::string text = templates[templDist(m_rng)];

    // Pick random slot
    std::uniform_int_distribution<size_t> slotDist(0, slotNames.size() - 1);
    auto& [slotType, slotName] = slotNames[slotDist(m_rng)];

    // Replace placeholders
    size_t pos = text.find("{slot}");
    if (pos != std::string::npos)
    {
        text.replace(pos, 6, slotName);
    }

    pos = text.find("{level}");
    if (pos != std::string::npos)
    {
        text.replace(pos, 7, std::to_string(level));
    }

    // Try to find an actual item name for {item}
    pos = text.find("{item}");
    if (pos != std::string::npos)
    {
        const ItemInfo* item = GetBestUpgradeForSlot(level, playerClass, slotType);
        if (item)
        {
            text.replace(pos, 6, item->name);
        }
        else
        {
            text.replace(pos, 6, slotName);
        }
    }

    return text;
}

} // namespace AliveWorld
