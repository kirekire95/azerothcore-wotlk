/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>
 * Released under GNU AGPL v3 license: https://github.com/azerothcore/azerothcore-wotlk/blob/master/LICENSE-AGPL3
 *
 * EC Bot Goal System - Goal Manager Implementation
 */

#include "GoalMgr.h"
#include "DatabaseEnv.h"
#include "GoalRepository.h"
#include "Player.h"
#include "Item.h"
#include "ItemTemplate.h"
#include "ObjectMgr.h"
#include "DBCStores.h"
#include "ReputationMgr.h"
#include "SpellMgr.h"
#include "Log.h"
#include "Config.h"
#include "PlayerbotAIConfig.h"
#include "BotPersonality.h"
#include <algorithm>
#include <random>

GoalMgr* GoalMgr::instance()
{
    static GoalMgr instance;
    return &instance;
}

GoalMgr::GoalMgr()
    : m_enabled(false)
    , m_maxActiveGoals(3)
    , m_refreshIntervalSeconds(300)
    , m_itemUpgradeThreshold(1.1f)
    , m_personalityWeightEnabled(true)
    , m_goalRetentionSeconds(604800)  // 7 days default
    , m_cleanupTimer(0)
{
}

void GoalMgr::Initialize()
{
    LOG_INFO("playerbots", "GoalMgr: Initializing...");

    LoadConfig();

    if (!m_enabled)
    {
        LOG_INFO("playerbots", "GoalMgr: Disabled by config");
        return;
    }

    sGoalRepository->Initialize();

    LOG_INFO("playerbots", "GoalMgr: Initialized (maxGoals={}, refreshInterval={}s)",
        m_maxActiveGoals, m_refreshIntervalSeconds);
}

void GoalMgr::LoadConfig()
{
    m_enabled = sConfigMgr->GetOption<bool>("AiPlayerbot.GoalSystem.Enabled", true);
    m_maxActiveGoals = sConfigMgr->GetOption<uint8_t>("AiPlayerbot.GoalSystem.MaxActiveGoals", 3);
    m_refreshIntervalSeconds = sConfigMgr->GetOption<uint32_t>("AiPlayerbot.GoalSystem.RefreshIntervalSeconds", 300);
    m_itemUpgradeThreshold = sConfigMgr->GetOption<float>("AiPlayerbot.GoalSystem.ItemUpgradeThreshold", 1.1f);
    m_personalityWeightEnabled = sConfigMgr->GetOption<bool>("AiPlayerbot.GoalSystem.PersonalityWeightEnabled", true);
    m_goalRetentionSeconds = sConfigMgr->GetOption<uint32_t>("AiPlayerbot.GoalSystem.GoalRetentionSeconds", 604800); // 7 days default
}

void GoalMgr::Update(uint32_t diff)
{
    if (!m_enabled)
        return;

    // Saturating add to prevent overflow
    if (m_cleanupTimer > CLEANUP_INTERVAL - diff)
        m_cleanupTimer = CLEANUP_INTERVAL;
    else
        m_cleanupTimer += diff;

    if (m_cleanupTimer >= CLEANUP_INTERVAL)
    {
        m_cleanupTimer = 0;

        // Clean up old completed/failed goals based on config
        uint32_t cleaned = sGoalRepository->CleanupOldGoals(m_goalRetentionSeconds);
        if (cleaned > 0)
            LOG_DEBUG("playerbots", "GoalMgr: Cleaned up {} old goals", cleaned);

        // Clear stale cache entries
        std::lock_guard<std::mutex> lock(m_cacheMutex);
        m_activeGoalsCache.clear();
        m_cacheAccessOrder.clear();
    }
}

// ============================================================================
// Goal Selection
// ============================================================================

void GoalMgr::SelectGoalsForBot(Player* bot, uint8_t maxGoals)
{
    if (!m_enabled || !bot)
        return;

    uint32_t botGuid = bot->GetGUID().GetCounter();

    // Check how many active goals bot already has
    uint32_t activeCount = sGoalRepository->GetActiveGoalCount(botGuid);
    if (activeCount >= maxGoals)
        return;

    uint8_t slotsAvailable = maxGoals - static_cast<uint8_t>(activeCount);

    // Gather potential goals from all evaluation functions
    std::vector<Goal> candidates;

    auto itemGoals = EvaluateItemGoals(bot);
    auto repGoals = EvaluateReputationGoals(bot);
    auto profGoals = EvaluateProfessionGoals(bot);
    auto exploreGoals = EvaluateExploreGoals(bot);
    auto goldGoals = EvaluateGoldGoals(bot);

    candidates.insert(candidates.end(), itemGoals.begin(), itemGoals.end());
    candidates.insert(candidates.end(), repGoals.begin(), repGoals.end());
    candidates.insert(candidates.end(), profGoals.begin(), profGoals.end());
    candidates.insert(candidates.end(), exploreGoals.begin(), exploreGoals.end());
    candidates.insert(candidates.end(), goldGoals.begin(), goldGoals.end());

    if (candidates.empty())
    {
        // No viable goals found — enter backoff to stop trigger spam
        m_emptyEvalBackoff[botGuid] = time(nullptr);
        return;
    }

    // Clear backoff on success
    m_emptyEvalBackoff.erase(botGuid);

    // Apply personality weighting
    BotPersonality personality = GetBotPersonality(bot);
    for (auto& goal : candidates)
    {
        goal.personalityWeight = CalculatePersonalityWeight(goal, personality);
    }

    // Sort by weighted priority (highest first)
    std::sort(candidates.begin(), candidates.end(),
        [](const Goal& a, const Goal& b) {
            return a.GetWeightedPriority() > b.GetWeightedPriority();
        });

    // Select top goals up to available slots
    uint8_t goalsCreated = 0;
    for (const auto& candidate : candidates)
    {
        if (goalsCreated >= slotsAvailable)
            break;

        // Skip if bot already has a goal for this target
        if (sGoalRepository->HasGoalForTarget(botGuid, candidate.type, candidate.targetId))
            continue;

        // Create and save the goal
        Goal newGoal = candidate;
        newGoal.botGuid = botGuid;
        newGoal.status = GoalStatus::ACTIVE;

        uint32_t goalId = sGoalRepository->SaveGoal(newGoal);
        if (goalId > 0)
        {
            LOG_DEBUG("playerbots", "GoalMgr: Bot {} selected goal: {} {} (priority={:.1f})",
                botGuid, GoalTypeToString(newGoal.type), newGoal.targetName,
                newGoal.GetWeightedPriority());
            goalsCreated++;
        }
    }

    // Update cache
    if (goalsCreated > 0)
    {
        std::lock_guard<std::mutex> lock(m_cacheMutex);
        m_activeGoalsCache.erase(botGuid); // Invalidate cache
    }
}

void GoalMgr::RefreshGoals(Player* bot)
{
    if (!m_enabled || !bot)
        return;

    uint32_t botGuid = bot->GetGUID().GetCounter();

    // Load current active goals
    auto goals = sGoalRepository->LoadActiveGoals(botGuid);
    if (goals.empty())
        return;

    BotPersonality personality = GetBotPersonality(bot);

    // Re-evaluate each goal
    for (auto& goal : goals)
    {
        // Check if goal is still valid
        if (!IsGoalValid(goal))
        {
            FailGoal(goal.id, "Target no longer valid");
            continue;
        }

        // Update personality weight
        goal.personalityWeight = CalculatePersonalityWeight(goal, personality);

        // Recalculate priority based on progress
        // Goals closer to completion get higher priority
        float progressBonus = goal.GetProgressPercent() * 0.2f; // Up to +20% for near-complete
        uint8_t newPriority = std::min(100, static_cast<int>(goal.priority + progressBonus));

        if (newPriority != goal.priority)
        {
            goal.priority = newPriority;
            sGoalRepository->UpdateGoal(goal);
        }
    }

    // Invalidate cache
    std::lock_guard<std::mutex> lock(m_cacheMutex);
    m_activeGoalsCache.erase(botGuid);
}

// ============================================================================
// Goal Evaluation
// ============================================================================

std::vector<Goal> GoalMgr::EvaluateItemGoals(Player* bot)
{
    std::vector<Goal> goals;

    if (!m_enabled || !bot)
        return goals;

    // Get bot's level and class for filtering
    uint8_t level = bot->GetLevel();
    uint8_t playerClass = bot->getClass();

    // Query potential upgrade items from item_template
    // Focus on dungeon drops and quest rewards in level-appropriate range
    QueryResult result = WorldDatabase.Query(
        "SELECT entry, name, ItemLevel, Quality, InventoryType, AllowableClass "
        "FROM item_template "
        "WHERE RequiredLevel <= {} AND RequiredLevel >= {} "
        "AND Quality >= 2 AND Quality <= 4 "  // Uncommon to Epic
        "AND (AllowableClass = -1 OR AllowableClass & {} != 0) "
        "ORDER BY ItemLevel DESC LIMIT 50",
        level, std::max(1, level - 10), (1 << (playerClass - 1)));

    if (!result)
        return goals;

    do
    {
        Field* fields = result->Fetch();
        uint32_t itemId = fields[0].Get<uint32_t>();
        std::string itemName = fields[1].Get<std::string>();
        uint32_t itemLevel = fields[2].Get<uint32_t>();
        uint32_t quality = fields[3].Get<uint32_t>();

        // Check if this is actually an upgrade
        if (!IsItemUpgrade(bot, itemId))
            continue;

        float upgradeScore = GetItemUpgradeScore(bot, itemId);
        if (upgradeScore < m_itemUpgradeThreshold)
            continue;

        Goal goal;
        goal.type = GoalType::ITEM;
        goal.targetId = itemId;
        goal.targetName = itemName;
        goal.priority = static_cast<uint8_t>(std::min(100.0f, 30.0f + upgradeScore * 10.0f + quality * 10.0f));
        goal.progress = 0;
        goal.targetProgress = 1; // Need 1 item
        goal.status = GoalStatus::PENDING;

        goals.push_back(goal);

    } while (result->NextRow());

    return goals;
}

std::vector<Goal> GoalMgr::EvaluateReputationGoals(Player* bot)
{
    std::vector<Goal> goals;

    if (!bot)
        return goals;

    // Check factions with valuable vendor rewards
    // Common valuable factions with gear/recipes
    static const std::vector<std::pair<uint32_t, std::string>> valuableFactions = {
        {529, "Argent Dawn"},
        {576, "Timbermaw Hold"},
        {609, "Cenarion Circle"},
        {749, "Hydraxian Waterlords"},
        {910, "Brood of Nozdormu"},
        {932, "The Aldor"},
        {934, "The Scryers"},
        {935, "The Sha'tar"},
        {942, "Cenarion Expedition"},
        {946, "Honor Hold"},
        {947, "Thrallmar"},
        {978, "Kurenai"},
        {941, "The Mag'har"},
        {989, "Keepers of Time"},
        {990, "The Scale of the Sands"},
        {1011, "Lower City"},
        {1012, "Ashtongue Deathsworn"},
        {1015, "Netherwing"},
        {1031, "Sha'tari Skyguard"},
        {1038, "Ogri'la"},
        {1077, "Shattered Sun Offensive"},
        {1090, "Kirin Tor"},
        {1091, "The Wyrmrest Accord"},
        {1098, "Knights of the Ebon Blade"},
        {1104, "Frenzyheart Tribe"},
        {1105, "The Oracles"},
        {1106, "Argent Crusade"},
        {1119, "The Sons of Hodir"},
    };

    for (const auto& [factionId, factionName] : valuableFactions)
    {
        FactionEntry const* factionEntry = sFactionStore.LookupEntry(factionId);
        if (!factionEntry)
            continue;

        // Check if bot can gain rep with this faction
        if (!bot->GetReputationMgr().GetState(factionEntry))
            continue;

        ReputationRank currentRank = bot->GetReputationMgr().GetRank(factionEntry);

        // Skip if already Exalted
        if (currentRank >= REP_EXALTED)
            continue;

        // Create goal to reach next rank
        ReputationRank targetRank = static_cast<ReputationRank>(currentRank + 1);

        Goal goal;
        goal.type = GoalType::REPUTATION;
        goal.targetId = factionId;
        goal.targetValue = static_cast<uint32_t>(targetRank);
        goal.targetName = factionName + " (" + GetRankName(targetRank) + ")";

        // Priority based on current rank (closer to exalted = higher priority)
        goal.priority = static_cast<uint8_t>(30 + currentRank * 8);
        goal.progress = bot->GetReputationMgr().GetReputation(factionEntry);
        goal.targetProgress = GetReputationForRank(targetRank);
        goal.status = GoalStatus::PENDING;

        goals.push_back(goal);
    }

    return goals;
}

std::vector<Goal> GoalMgr::EvaluateProfessionGoals(Player* bot)
{
    std::vector<Goal> goals;

    if (!bot)
        return goals;

    // Check each profession the bot has
    static const std::vector<uint32_t> professionSkills = {
        164,  // Blacksmithing
        165,  // Leatherworking
        171,  // Alchemy
        182,  // Herbalism
        185,  // Cooking
        186,  // Mining
        197,  // Tailoring
        202,  // Engineering
        333,  // Enchanting
        393,  // Skinning
        755,  // Jewelcrafting
        773,  // Inscription
    };

    for (uint32_t skillId : professionSkills)
    {
        uint16_t skillValue = bot->GetSkillValue(skillId);
        if (skillValue == 0)
            continue; // Bot doesn't have this profession

        uint16_t maxSkill = bot->GetMaxSkillValue(skillId);

        // If not at max, create goal to level up
        if (skillValue < maxSkill)
        {
            Goal goal;
            goal.type = GoalType::PROFESSION;
            goal.targetId = skillId;
            goal.targetValue = std::min(static_cast<uint32_t>(maxSkill), static_cast<uint32_t>(skillValue + 50));
            goal.targetName = GetSkillName(skillId) + " skill";
            goal.priority = static_cast<uint8_t>(40 + (skillValue / 75) * 5); // Higher skill = higher priority
            goal.progress = skillValue;
            goal.targetProgress = goal.targetValue;
            goal.status = GoalStatus::PENDING;

            goals.push_back(goal);
        }
    }

    return goals;
}

std::vector<Goal> GoalMgr::EvaluateExploreGoals(Player* bot)
{
    std::vector<Goal> goals;

    if (!bot)
        return goals;

    uint8_t level = bot->GetLevel();

    // Level-appropriate zones to explore
    // Format: {zoneId, zoneName, minLevel, maxLevel}
    static const std::vector<std::tuple<uint32_t, std::string, uint8_t, uint8_t>> explorationZones = {
        // Eastern Kingdoms
        {12, "Elwynn Forest", 1, 10},
        {40, "Westfall", 10, 20},
        {44, "Redridge Mountains", 15, 25},
        {47, "The Hinterlands", 40, 50},
        {51, "Searing Gorge", 45, 50},
        {46, "Burning Steppes", 50, 58},
        {139, "Eastern Plaguelands", 55, 60},
        // Kalimdor
        {14, "Durotar", 1, 10},
        {17, "The Barrens", 10, 25},
        {400, "Thousand Needles", 25, 35},
        {357, "Feralas", 40, 50},
        {490, "Un'Goro Crater", 48, 55},
        {1377, "Silithus", 55, 60},
        // Outland
        {3483, "Hellfire Peninsula", 58, 63},
        {3518, "Nagrand", 64, 67},
        {3519, "Terokkar Forest", 62, 65},
        {3521, "Zangarmarsh", 60, 64},
        {3522, "Blade's Edge Mountains", 65, 68},
        {3523, "Netherstorm", 67, 70},
        {3520, "Shadowmoon Valley", 67, 70},
        // Northrend
        {3537, "Borean Tundra", 68, 72},
        {65, "Dragonblight", 71, 74},
        {394, "Grizzly Hills", 73, 75},
        {66, "Zul'Drak", 74, 77},
        {67, "The Storm Peaks", 77, 80},
        {210, "Icecrown", 77, 80},
    };

    for (const auto& [zoneId, zoneName, minLevel, maxLevel] : explorationZones)
    {
        // Check if zone is level-appropriate
        if (level < minLevel || level > maxLevel + 5)
            continue;

        // Check if zone is already explored (simplified check)
        // In reality, would need to check exploration flags
        // For now, create goal for any level-appropriate zone

        Goal goal;
        goal.type = GoalType::EXPLORE;
        goal.targetId = zoneId;
        goal.targetName = zoneName;
        goal.priority = static_cast<uint8_t>(25 + (80 - std::abs(static_cast<int>(level) - static_cast<int>((minLevel + maxLevel) / 2)))); // Closer to optimal level = higher priority
        goal.progress = 0;
        goal.targetProgress = 1;
        goal.status = GoalStatus::PENDING;

        goals.push_back(goal);
    }

    return goals;
}

std::vector<Goal> GoalMgr::EvaluateGoldGoals(Player* bot)
{
    std::vector<Goal> goals;

    if (!bot)
        return goals;

    uint32_t currentGold = bot->GetMoney() / 10000; // Convert copper to gold
    uint8_t level = bot->GetLevel();

    // Set gold targets based on level
    std::vector<std::pair<uint32_t, std::string>> goldTargets;

    if (level >= 20 && currentGold < 5)
        goldTargets.push_back({5, "5 gold (first mount)"});
    if (level >= 40 && currentGold < 100)
        goldTargets.push_back({100, "100 gold (epic mount)"});
    if (level >= 60 && currentGold < 1000)
        goldTargets.push_back({1000, "1000 gold (flying mount)"});
    if (level >= 70 && currentGold < 5000)
        goldTargets.push_back({5000, "5000 gold (epic flying)"});
    if (level >= 77 && currentGold < 1000)
        goldTargets.push_back({1000, "1000 gold (cold weather flying)"});

    for (const auto& [targetGold, description] : goldTargets)
    {
        Goal goal;
        goal.type = GoalType::GOLD;
        goal.targetId = targetGold * 10000; // Store in copper
        goal.targetName = description;
        goal.priority = static_cast<uint8_t>(50 + level / 4); // Higher level = gold more important
        goal.progress = bot->GetMoney();
        goal.targetProgress = targetGold * 10000;
        goal.status = GoalStatus::PENDING;

        goals.push_back(goal);
    }

    return goals;
}

// ============================================================================
// Personality Weighting
// ============================================================================

float GoalMgr::CalculatePersonalityWeight(const Goal& goal, const BotPersonality& personality)
{
    if (!m_personalityWeightEnabled)
        return 1.0f;

    float weight = 1.0f;

    switch (goal.type)
    {
        case GoalType::ITEM:
            // Greedy bots want valuable items
            weight += personality.greed * 0.3f;
            // Aggressive bots want combat gear
            weight += personality.aggression * 0.2f;
            break;

        case GoalType::GOLD:
            // Greedy bots strongly want gold
            weight += personality.greed * 0.5f;
            // Less helpful bots prefer hoarding
            weight += (1.0f - personality.helpfulness) * 0.2f;
            break;

        case GoalType::REPUTATION:
            // Helpful bots care about standing
            weight += personality.helpfulness * 0.3f;
            // Curious bots explore faction stories
            weight += personality.curiosity * 0.1f;
            break;

        case GoalType::EXPLORE:
            // Curious bots explore more
            weight += personality.curiosity * 0.4f;
            // Less aggressive bots prefer exploration over combat
            weight += (1.0f - personality.aggression) * 0.2f;
            break;

        case GoalType::PROFESSION:
            // Curious bots want to learn
            weight += personality.curiosity * 0.2f;
            // Non-aggressive bots like crafting
            weight += (1.0f - personality.aggression) * 0.2f;
            // Greedy bots want profitable crafts
            weight += personality.greed * 0.1f;
            break;

        case GoalType::ACHIEVEMENT:
            // Curious bots pursue achievements
            weight += personality.curiosity * 0.3f;
            break;

        default:
            break;
    }

    return std::clamp(weight, 0.1f, 2.0f);
}

BotPersonality GoalMgr::GetBotPersonality(Player* bot)
{
    BotPersonality personality;
    if (!bot)
        return personality;

    uint32_t botGuid = bot->GetGUID().GetCounter();

    // Check cache first
    {
        std::lock_guard<std::mutex> lock(m_personalityCacheMutex);
        auto it = m_personalityCache.find(botGuid);
        if (it != m_personalityCache.end())
        {
            // Update LRU order
            m_personalityCacheOrder.remove(botGuid);
            m_personalityCacheOrder.push_front(botGuid);
            return it->second;
        }
    }

    // Try loading from database
    personality = LoadPersonalityFromDB(botGuid);

    // Not in DB — generate from GUID seed (deterministic, same values as before) and persist
    if (personality.aggression < 0.0f)
    {
        std::mt19937 rng(botGuid);
        std::uniform_real_distribution<float> dist(0.2f, 0.8f);

        personality.aggression = dist(rng);
        personality.helpfulness = dist(rng);
        personality.chattiness = dist(rng);
        personality.curiosity = dist(rng);
        personality.greed = dist(rng);

        SavePersonalityToDB(botGuid, personality);
    }

    // Update cache
    {
        std::lock_guard<std::mutex> lock(m_personalityCacheMutex);
        m_personalityCache[botGuid] = personality;
        m_personalityCacheOrder.push_front(botGuid);

        // Evict oldest entries if over capacity
        while (m_personalityCache.size() > MAX_PERSONALITY_CACHE_SIZE && !m_personalityCacheOrder.empty())
        {
            uint32_t oldest = m_personalityCacheOrder.back();
            m_personalityCacheOrder.pop_back();
            m_personalityCache.erase(oldest);
        }
    }

    return personality;
}

BotPersonality GoalMgr::LoadPersonalityFromDB(uint32_t botGuid)
{
    BotPersonality p;
    p.aggression = -1.0f;  // Sentinel: not found

    QueryResult result = CharacterDatabase.Query(
        "SELECT aggression, helpfulness, chattiness, curiosity, greed "
        "FROM ec_bot_personality WHERE bot_guid = {}",
        botGuid);

    if (result)
    {
        Field* fields = result->Fetch();
        p.aggression = fields[0].Get<float>();
        p.helpfulness = fields[1].Get<float>();
        p.chattiness = fields[2].Get<float>();
        p.curiosity = fields[3].Get<float>();
        p.greed = fields[4].Get<float>();
    }

    return p;
}

void GoalMgr::SavePersonalityToDB(uint32_t botGuid, const BotPersonality& p)
{
    CharacterDatabase.Execute(
        "REPLACE INTO ec_bot_personality (bot_guid, aggression, helpfulness, chattiness, curiosity, greed, created_at) "
        "VALUES ({}, {}, {}, {}, {}, {}, UNIX_TIMESTAMP())",
        botGuid, p.aggression, p.helpfulness, p.chattiness, p.curiosity, p.greed);
}

// ============================================================================
// Progress Tracking (Event Hooks)
// ============================================================================

void GoalMgr::OnItemLooted(Player* bot, uint32_t itemId)
{
    if (!m_enabled || !bot)
        return;

    uint32_t botGuid = bot->GetGUID().GetCounter();

    // Get goals from cache first
    std::vector<Goal> goals;
    {
        std::lock_guard<std::mutex> lock(m_cacheMutex);
        auto it = m_activeGoalsCache.find(botGuid);
        if (it != m_activeGoalsCache.end())
            goals = it->second;
    }

    // Only query DB if cache miss
    if (goals.empty())
        goals = sGoalRepository->LoadActiveGoals(botGuid);

    bool cacheUpdated = false;
    for (auto& goal : goals)
    {
        if (goal.type == GoalType::ITEM && goal.targetId == itemId)
        {
            goal.progress++;
            cacheUpdated = true;
            if (goal.IsComplete())
            {
                CompleteGoal(goal.id);
                LOG_DEBUG("playerbots", "GoalMgr: Bot {} completed item goal: {}",
                    botGuid, goal.targetName);
            }
            else
            {
                sGoalRepository->UpdateProgress(goal.id, goal.progress);
            }
        }
    }

    // Update cache with modified goals
    if (cacheUpdated)
    {
        std::lock_guard<std::mutex> lock(m_cacheMutex);
        m_activeGoalsCache[botGuid] = std::move(goals);
        UpdateCacheAccess(botGuid);
    }
}

void GoalMgr::OnReputationChange(Player* bot, uint32_t factionId, int32_t change)
{
    if (!m_enabled || !bot || change <= 0)
        return;

    uint32_t botGuid = bot->GetGUID().GetCounter();

    // Get goals from cache first
    std::vector<Goal> goals;
    {
        std::lock_guard<std::mutex> lock(m_cacheMutex);
        auto it = m_activeGoalsCache.find(botGuid);
        if (it != m_activeGoalsCache.end())
            goals = it->second;
    }

    // Only query DB if cache miss
    if (goals.empty())
        goals = sGoalRepository->LoadActiveGoals(botGuid);

    bool cacheUpdated = false;
    for (auto& goal : goals)
    {
        if (goal.type == GoalType::REPUTATION && goal.targetId == factionId)
        {
            FactionEntry const* factionEntry = sFactionStore.LookupEntry(factionId);
            if (factionEntry)
            {
                int32_t currentRep = bot->GetReputationMgr().GetReputation(factionEntry);
                goal.progress = static_cast<uint32_t>(std::max(0, currentRep));
                cacheUpdated = true;

                if (goal.progress >= goal.targetProgress)
                {
                    CompleteGoal(goal.id);
                    LOG_DEBUG("playerbots", "GoalMgr: Bot {} completed reputation goal: {}",
                        botGuid, goal.targetName);
                }
                else
                {
                    sGoalRepository->UpdateProgress(goal.id, goal.progress);
                }
            }
        }
    }

    // Update cache with modified goals
    if (cacheUpdated)
    {
        std::lock_guard<std::mutex> lock(m_cacheMutex);
        m_activeGoalsCache[botGuid] = std::move(goals);
        UpdateCacheAccess(botGuid);
    }
}

void GoalMgr::OnZoneDiscovered(Player* bot, uint32_t zoneId)
{
    if (!m_enabled || !bot)
        return;

    uint32_t botGuid = bot->GetGUID().GetCounter();

    // Get goals from cache first
    std::vector<Goal> goals;
    {
        std::lock_guard<std::mutex> lock(m_cacheMutex);
        auto it = m_activeGoalsCache.find(botGuid);
        if (it != m_activeGoalsCache.end())
            goals = it->second;
    }

    // Only query DB if cache miss
    if (goals.empty())
        goals = sGoalRepository->LoadActiveGoals(botGuid);

    bool cacheUpdated = false;
    for (auto& goal : goals)
    {
        if (goal.type == GoalType::EXPLORE && goal.targetId == zoneId)
        {
            goal.progress = 1;
            cacheUpdated = true;
            CompleteGoal(goal.id);
            LOG_DEBUG("playerbots", "GoalMgr: Bot {} completed explore goal: {}",
                botGuid, goal.targetName);
        }
    }

    // Update cache with modified goals
    if (cacheUpdated)
    {
        std::lock_guard<std::mutex> lock(m_cacheMutex);
        m_activeGoalsCache[botGuid] = std::move(goals);
        UpdateCacheAccess(botGuid);
    }
}

void GoalMgr::OnGoldChange(Player* bot, int32_t delta)
{
    if (!m_enabled || !bot || delta <= 0)
        return;

    uint32_t botGuid = bot->GetGUID().GetCounter();
    uint32_t currentMoney = bot->GetMoney();

    // Get goals from cache first
    std::vector<Goal> goals;
    {
        std::lock_guard<std::mutex> lock(m_cacheMutex);
        auto it = m_activeGoalsCache.find(botGuid);
        if (it != m_activeGoalsCache.end())
            goals = it->second;
    }

    // Only query DB if cache miss
    if (goals.empty())
        goals = sGoalRepository->LoadActiveGoals(botGuid);

    bool cacheUpdated = false;
    for (auto& goal : goals)
    {
        if (goal.type == GoalType::GOLD)
        {
            goal.progress = currentMoney;
            cacheUpdated = true;

            if (goal.progress >= goal.targetProgress)
            {
                CompleteGoal(goal.id);
                LOG_DEBUG("playerbots", "GoalMgr: Bot {} completed gold goal: {}",
                    botGuid, goal.targetName);
            }
            else
            {
                sGoalRepository->UpdateProgress(goal.id, goal.progress);
            }
        }
    }

    // Update cache with modified goals
    if (cacheUpdated)
    {
        std::lock_guard<std::mutex> lock(m_cacheMutex);
        m_activeGoalsCache[botGuid] = std::move(goals);
        UpdateCacheAccess(botGuid);
    }
}

void GoalMgr::OnSkillChange(Player* bot, uint32_t skillId, uint32_t newValue)
{
    if (!m_enabled || !bot)
        return;

    uint32_t botGuid = bot->GetGUID().GetCounter();

    // Get goals from cache first
    std::vector<Goal> goals;
    {
        std::lock_guard<std::mutex> lock(m_cacheMutex);
        auto it = m_activeGoalsCache.find(botGuid);
        if (it != m_activeGoalsCache.end())
            goals = it->second;
    }

    // Only query DB if cache miss
    if (goals.empty())
        goals = sGoalRepository->LoadActiveGoals(botGuid);

    bool cacheUpdated = false;
    for (auto& goal : goals)
    {
        if (goal.type == GoalType::PROFESSION && goal.targetId == skillId)
        {
            goal.progress = newValue;
            cacheUpdated = true;

            if (goal.progress >= goal.targetProgress)
            {
                CompleteGoal(goal.id);
                LOG_DEBUG("playerbots", "GoalMgr: Bot {} completed profession goal: {}",
                    botGuid, goal.targetName);
            }
            else
            {
                sGoalRepository->UpdateProgress(goal.id, goal.progress);
            }
        }
    }

    // Update cache with modified goals
    if (cacheUpdated)
    {
        std::lock_guard<std::mutex> lock(m_cacheMutex);
        m_activeGoalsCache[botGuid] = std::move(goals);
        UpdateCacheAccess(botGuid);
    }
}

// ============================================================================
// Goal Lifecycle
// ============================================================================

std::optional<Goal> GoalMgr::GetActiveGoal(Player* bot)
{
    if (!m_enabled || !bot)
        return std::nullopt;

    uint32_t botGuid = bot->GetGUID().GetCounter();

    // Check cache first
    {
        std::lock_guard<std::mutex> lock(m_cacheMutex);
        auto it = m_activeGoalsCache.find(botGuid);
        if (it != m_activeGoalsCache.end() && !it->second.empty())
        {
            // Return copy of highest priority goal (thread-safe)
            return it->second[0];
        }
    }

    // Load from database
    auto goals = sGoalRepository->LoadActiveGoals(botGuid);
    if (goals.empty())
        return std::nullopt;

    // Update cache and return copy
    {
        std::lock_guard<std::mutex> lock(m_cacheMutex);
        m_activeGoalsCache[botGuid] = goals;
        UpdateCacheAccess(botGuid);
        return goals[0];
    }
}

std::vector<Goal> GoalMgr::GetActiveGoals(Player* bot)
{
    if (!m_enabled || !bot)
        return {};

    uint32_t botGuid = bot->GetGUID().GetCounter();

    // Check cache first
    {
        std::lock_guard<std::mutex> lock(m_cacheMutex);
        auto it = m_activeGoalsCache.find(botGuid);
        if (it != m_activeGoalsCache.end())
            return it->second;
    }

    // Load from database
    auto goals = sGoalRepository->LoadActiveGoals(botGuid);

    // Update cache
    {
        std::lock_guard<std::mutex> lock(m_cacheMutex);
        m_activeGoalsCache[botGuid] = goals;
    }

    return goals;
}

void GoalMgr::ActivateGoal(uint32_t goalId)
{
    sGoalRepository->SetStatus(goalId, GoalStatus::ACTIVE);
}

void GoalMgr::CompleteGoal(uint32_t goalId)
{
    sGoalRepository->SetStatus(goalId, GoalStatus::COMPLETED);
}

void GoalMgr::FailGoal(uint32_t goalId, const std::string& reason)
{
    LOG_DEBUG("playerbots", "GoalMgr: Goal {} failed: {}", goalId, reason);
    sGoalRepository->SetStatus(goalId, GoalStatus::FAILED);
}

void GoalMgr::AbandonGoal(uint32_t goalId)
{
    sGoalRepository->SetStatus(goalId, GoalStatus::ABANDONED);
}

// ============================================================================
// Helpers
// ============================================================================

bool GoalMgr::IsItemUpgrade(Player* bot, uint32_t itemId)
{
    if (!bot)
        return false;

    ItemTemplate const* itemTemplate = sObjectMgr->GetItemTemplate(itemId);
    if (!itemTemplate)
        return false;

    // Check if this item can be equipped
    if (itemTemplate->InventoryType == INVTYPE_NON_EQUIP)
        return false;

    // Check class/race requirements
    if (itemTemplate->AllowableClass > 0 && !(itemTemplate->AllowableClass & bot->getClassMask()))
        return false;
    if (itemTemplate->AllowableRace > 0 && !(itemTemplate->AllowableRace & bot->getRaceMask()))
        return false;

    // Simple item level comparison
    uint8_t slot = bot->FindEquipSlot(itemTemplate, 0, true);
    if (slot == NULL_SLOT)
        return false;

    Item* currentItem = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, slot);
    if (!currentItem)
        return true; // Empty slot = upgrade

    ItemTemplate const* currentTemplate = currentItem->GetTemplate();
    return itemTemplate->ItemLevel > currentTemplate->ItemLevel;
}

float GoalMgr::GetItemUpgradeScore(Player* bot, uint32_t itemId)
{
    if (!bot)
        return 0.0f;

    ItemTemplate const* itemTemplate = sObjectMgr->GetItemTemplate(itemId);
    if (!itemTemplate)
        return 0.0f;

    uint8_t slot = bot->FindEquipSlot(itemTemplate, 0, true);
    if (slot == NULL_SLOT)
        return 0.0f;

    Item* currentItem = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, slot);
    if (!currentItem)
        return 2.0f; // Empty slot = big upgrade

    ItemTemplate const* currentTemplate = currentItem->GetTemplate();
    if (currentTemplate->ItemLevel == 0)
        return 2.0f;

    return static_cast<float>(itemTemplate->ItemLevel) / static_cast<float>(currentTemplate->ItemLevel);
}

std::string GoalMgr::GetItemName(uint32_t itemId)
{
    ItemTemplate const* itemTemplate = sObjectMgr->GetItemTemplate(itemId);
    if (!itemTemplate)
        return "Unknown Item";
    return itemTemplate->Name1;
}

std::string GoalMgr::GetFactionName(uint32_t factionId)
{
    FactionEntry const* factionEntry = sFactionStore.LookupEntry(factionId);
    if (!factionEntry)
        return "Unknown Faction";
    return factionEntry->name[0]; // Default locale
}

std::string GoalMgr::GetZoneName(uint32_t zoneId)
{
    AreaTableEntry const* areaEntry = sAreaTableStore.LookupEntry(zoneId);
    if (!areaEntry)
        return "Unknown Zone";
    return areaEntry->area_name[0]; // Default locale
}

// LRU cache eviction helper - call while holding m_cacheMutex
void GoalMgr::UpdateCacheAccess(uint32_t botGuid)
{
    // Remove from current position if exists
    m_cacheAccessOrder.remove(botGuid);
    // Add to front (most recently used)
    m_cacheAccessOrder.push_front(botGuid);

    // Evict oldest if over limit
    while (m_activeGoalsCache.size() > MAX_CACHE_SIZE && !m_cacheAccessOrder.empty())
    {
        uint32_t oldest = m_cacheAccessOrder.back();
        m_cacheAccessOrder.pop_back();
        m_activeGoalsCache.erase(oldest);
    }
}

bool GoalMgr::IsGoalValid(const Goal& goal)
{
    switch (goal.type)
    {
        case GoalType::ITEM:
            // Check if item still exists in database
            return sObjectMgr->GetItemTemplate(goal.targetId) != nullptr;

        case GoalType::REPUTATION:
            // Check if faction still exists
            return sFactionStore.LookupEntry(goal.targetId) != nullptr;

        case GoalType::EXPLORE:
            // Check if zone still exists
            return sAreaTableStore.LookupEntry(goal.targetId) != nullptr;

        case GoalType::PROFESSION:
            // Skill IDs are generally static
            return true;

        case GoalType::GOLD:
            // Gold goals are always valid
            return true;

        case GoalType::ACHIEVEMENT:
            // Achievement validation would go here
            return true;

        default:
            return false;
    }
}

// Helper function for reputation rank names
const char* GoalMgr::GetRankName(ReputationRank rank)
{
    switch (rank)
    {
        case REP_HATED:      return "Hated";
        case REP_HOSTILE:    return "Hostile";
        case REP_UNFRIENDLY: return "Unfriendly";
        case REP_NEUTRAL:    return "Neutral";
        case REP_FRIENDLY:   return "Friendly";
        case REP_HONORED:    return "Honored";
        case REP_REVERED:    return "Revered";
        case REP_EXALTED:    return "Exalted";
        default:             return "Unknown";
    }
}

int32_t GoalMgr::GetReputationForRank(ReputationRank rank)
{
    // Standard WoW reputation thresholds
    switch (rank)
    {
        case REP_HATED:      return -42000;
        case REP_HOSTILE:    return -6000;
        case REP_UNFRIENDLY: return -3000;
        case REP_NEUTRAL:    return 0;
        case REP_FRIENDLY:   return 3000;
        case REP_HONORED:    return 9000;
        case REP_REVERED:    return 21000;
        case REP_EXALTED:    return 42000;
        default:             return 0;
    }
}

std::string GoalMgr::GetSkillName(uint32_t skillId)
{
    SkillLineEntry const* skillEntry = sSkillLineStore.LookupEntry(skillId);
    if (!skillEntry)
        return "Unknown Skill";
    return skillEntry->name[0]; // Default locale
}

bool GoalMgr::IsInGoalEvalBackoff(uint32_t botGuid) const
{
    auto it = m_emptyEvalBackoff.find(botGuid);
    if (it == m_emptyEvalBackoff.end())
        return false;

    return (time(nullptr) - it->second) < EMPTY_EVAL_BACKOFF_SECONDS;
}
