/*
 * mod-alive-world - Bot Memory Manager Implementation
 * Copyright (C) 2025 EC
 */

#include "BotMemoryManager.h"
#include "AliveWorldBridgeAdapter.h"
#include "AliveWorldManager.h"
#include "WorldStateManager.h"
#include "Config.h"
#include "DatabaseEnv.h"
#include "ObjectAccessor.h"
#include "Player.h"

#include <algorithm>
#include <random>
#include <sstream>

namespace AliveWorld
{

// Static random generator for personality initialization
static std::random_device s_rd;
static std::mt19937 s_gen(s_rd());
static std::uniform_real_distribution<float> s_normalDist(0.3f, 0.7f);    // Normal personality range
static std::uniform_real_distribution<float> s_extremeLowDist(0.0f, 0.25f);  // Extreme low end
static std::uniform_real_distribution<float> s_extremeHighDist(0.75f, 1.0f); // Extreme high end
static std::uniform_int_distribution<int> s_percentDist(1, 100);          // For chance rolls
static std::uniform_int_distribution<int> s_coinFlip(0, 1);               // High or low extreme

static float PercentToTrait(uint8 percent)
{
    return std::clamp(static_cast<float>(percent) / 100.0f, 0.0f, 1.0f);
}

static uint8 TraitToPercent(float trait)
{
    float clamped = std::clamp(trait, 0.0f, 1.0f);
    return static_cast<uint8>(clamped * 100.0f);
}

// Get a personality trait value with chance of being extreme
static float GetRandomizedTrait(int extremeChancePercent)
{
    // Roll for extreme personality
    if (s_percentDist(s_gen) <= extremeChancePercent)
    {
        // 50/50 chance of extreme high or extreme low
        if (s_coinFlip(s_gen) == 0)
            return s_extremeLowDist(s_gen);
        else
            return s_extremeHighDist(s_gen);
    }
    // Normal personality
    return s_normalDist(s_gen);
}

void BotPersonality::Randomize()
{
    // Get extreme personality chance from config (default 10%)
    // This means 10% of bots will have at least one extreme trait
    int extremeChance = sConfigMgr->GetOption<int32>("AliveWorld.ExtremePersonalityChance", 10);

    // Randomize all personality traits
    // Each trait has an independent chance to be extreme
    aggression = GetRandomizedTrait(extremeChance);
    caution = GetRandomizedTrait(extremeChance);
    sociability = GetRandomizedTrait(extremeChance);
    loyalty = GetRandomizedTrait(extremeChance);
    curiosity = GetRandomizedTrait(extremeChance);
    vengefulness = GetRandomizedTrait(extremeChance);
    helpfulness = GetRandomizedTrait(extremeChance);
    greed = GetRandomizedTrait(extremeChance);
    currentMood = "neutral";
    moodTimestamp = NowMs();
}

BotMemoryManager::BotMemoryManager()
    : m_enabled(true)
    , m_maxCombatLog(5)
    , m_maxSocialConnections(20)
    , m_persistIntervalSeconds(300)
    , m_personalityEvolution(true)
    , m_worldStateManager(nullptr)
    , m_persistTimer(0)
    , m_lastPersistTime(0)
{
}

BotMemoryManager::~BotMemoryManager()
{
    // Save all dirty bots before shutdown
    SaveToDatabase();

    std::unique_lock<std::shared_mutex> lock(m_memoryMutex);
    m_botMemory.clear();
}

void BotMemoryManager::Initialize(WorldStateManager* worldStateManager)
{
    ALIVE_LOG_INFO("Bot Memory Manager initializing...");

    m_worldStateManager = worldStateManager;
    LoadConfig();

    if (m_enabled)
    {
        ALIVE_LOG_INFO("Bot Memory Manager enabled");
        ALIVE_LOG_INFO("  Max Combat Log: %u", m_maxCombatLog);
        ALIVE_LOG_INFO("  Max Social Connections: %u", m_maxSocialConnections);
        ALIVE_LOG_INFO("  Persist Interval: %u seconds", m_persistIntervalSeconds);
        ALIVE_LOG_INFO("  Personality Evolution: %s", m_personalityEvolution ? "ON" : "OFF");

        LoadPersonalityOverrides();
        LoadFromDatabase();
        EnforcePersonalityOverrides();
    }
    else
    {
        ALIVE_LOG_WARN("Bot Memory Manager disabled in config");
    }
}

void BotMemoryManager::LoadConfig()
{
    m_enabled = sConfigMgr->GetOption<bool>("AliveWorld.BotMemory.Enable", true);
    m_maxCombatLog = sConfigMgr->GetOption<uint32>("AliveWorld.BotMemory.MaxCombatLog", 5);
    m_maxSocialConnections = sConfigMgr->GetOption<uint32>("AliveWorld.BotMemory.MaxSocialConnections", 20);
    m_persistIntervalSeconds = sConfigMgr->GetOption<uint32>("AliveWorld.BotMemory.PersistIntervalSeconds", 300);
    m_personalityEvolution = sConfigMgr->GetOption<bool>("AliveWorld.BotMemory.PersonalityEvolution", true);
}

void BotMemoryManager::InitializeRandomPersonality(BotPersonality& personality)
{
    personality.Randomize();
}

// =========================================================================
// CLASS/RACE-BASED PERSONALITY (Data Goldmine Feature)
// =========================================================================

// WoW 3.3.5 Class IDs
enum WoWClass : uint8
{
    CLASS_WARRIOR       = 1,
    CLASS_PALADIN       = 2,
    CLASS_HUNTER        = 3,
    CLASS_ROGUE         = 4,
    CLASS_PRIEST        = 5,
    CLASS_DEATH_KNIGHT  = 6,
    CLASS_SHAMAN        = 7,
    CLASS_MAGE          = 8,
    CLASS_WARLOCK       = 9,
    CLASS_DRUID         = 11
};

// WoW 3.3.5 Race IDs
enum WoWRace : uint8
{
    RACE_HUMAN          = 1,
    RACE_ORC            = 2,
    RACE_DWARF          = 3,
    RACE_NIGHT_ELF      = 4,
    RACE_UNDEAD         = 5,
    RACE_TAUREN         = 6,
    RACE_GNOME          = 7,
    RACE_TROLL          = 8,
    RACE_BLOOD_ELF      = 10,
    RACE_DRAENEI        = 11
};

// Get a trait value within a range with slight randomization
static float GetTraitInRange(float min, float max)
{
    std::uniform_real_distribution<float> dist(min, max);
    return dist(s_gen);
}

void BotMemoryManager::InitializePersonalityFromClassRace(BotPersonality& personality, uint8 playerClass, uint8 playerRace)
{
    // Start with neutral/balanced personality
    personality.aggression = 0.5f;
    personality.caution = 0.5f;
    personality.sociability = 0.5f;
    personality.loyalty = 0.5f;
    personality.curiosity = 0.5f;
    personality.vengefulness = 0.3f;
    personality.helpfulness = 0.5f;
    personality.greed = 0.5f;

    // ==== CLASS-BASED PERSONALITY ====
    // Each class has distinct personality traits reflecting their playstyle and lore
    switch (playerClass)
    {
        case CLASS_WARRIOR:
            // Warriors: Fierce, straightforward fighters. High aggression, moderate loyalty
            personality.aggression = GetTraitInRange(0.70f, 0.90f);
            personality.caution = GetTraitInRange(0.20f, 0.40f);
            personality.loyalty = GetTraitInRange(0.50f, 0.70f);
            personality.greed = GetTraitInRange(0.30f, 0.50f);
            personality.sociability = GetTraitInRange(0.35f, 0.55f);
            personality.helpfulness = GetTraitInRange(0.40f, 0.60f);
            break;

        case CLASS_PALADIN:
            // Paladins: Holy warriors, protectors of the weak. Very helpful and loyal
            personality.helpfulness = GetTraitInRange(0.70f, 0.90f);
            personality.loyalty = GetTraitInRange(0.70f, 0.90f);
            personality.aggression = GetTraitInRange(0.40f, 0.60f);
            personality.greed = GetTraitInRange(0.15f, 0.35f);
            personality.vengefulness = GetTraitInRange(0.20f, 0.40f);
            personality.sociability = GetTraitInRange(0.55f, 0.75f);
            break;

        case CLASS_HUNTER:
            // Hunters: Independent, self-reliant. Curious explorers, less social
            personality.curiosity = GetTraitInRange(0.65f, 0.85f);
            personality.caution = GetTraitInRange(0.45f, 0.65f);
            personality.sociability = GetTraitInRange(0.25f, 0.45f);
            personality.greed = GetTraitInRange(0.45f, 0.65f);
            personality.loyalty = GetTraitInRange(0.40f, 0.60f);
            personality.aggression = GetTraitInRange(0.35f, 0.55f);
            break;

        case CLASS_ROGUE:
            // Rogues: Shadowy opportunists. High greed and curiosity, lower loyalty
            personality.greed = GetTraitInRange(0.70f, 0.90f);
            personality.curiosity = GetTraitInRange(0.60f, 0.80f);
            personality.caution = GetTraitInRange(0.55f, 0.75f);
            personality.loyalty = GetTraitInRange(0.20f, 0.45f);
            personality.helpfulness = GetTraitInRange(0.20f, 0.40f);
            personality.sociability = GetTraitInRange(0.30f, 0.50f);
            break;

        case CLASS_PRIEST:
            // Priests: Spiritual healers. Very helpful, avoid violence
            personality.helpfulness = GetTraitInRange(0.75f, 0.95f);
            personality.aggression = GetTraitInRange(0.10f, 0.30f);
            personality.sociability = GetTraitInRange(0.60f, 0.80f);
            personality.vengefulness = GetTraitInRange(0.10f, 0.25f);
            personality.caution = GetTraitInRange(0.55f, 0.75f);
            personality.loyalty = GetTraitInRange(0.55f, 0.75f);
            break;

        case CLASS_DEATH_KNIGHT:
            // Death Knights: Dark, brooding warriors. High aggression and vengefulness
            personality.aggression = GetTraitInRange(0.65f, 0.85f);
            personality.vengefulness = GetTraitInRange(0.60f, 0.85f);
            personality.helpfulness = GetTraitInRange(0.15f, 0.35f);
            personality.sociability = GetTraitInRange(0.20f, 0.40f);
            personality.caution = GetTraitInRange(0.30f, 0.50f);
            personality.loyalty = GetTraitInRange(0.35f, 0.55f);
            break;

        case CLASS_SHAMAN:
            // Shamans: Spiritual guides, balanced. Connected to elements
            personality.helpfulness = GetTraitInRange(0.50f, 0.70f);
            personality.curiosity = GetTraitInRange(0.50f, 0.70f);
            personality.sociability = GetTraitInRange(0.45f, 0.65f);
            personality.loyalty = GetTraitInRange(0.55f, 0.75f);
            personality.aggression = GetTraitInRange(0.40f, 0.60f);
            personality.caution = GetTraitInRange(0.40f, 0.60f);
            break;

        case CLASS_MAGE:
            // Mages: Scholarly intellectuals. High curiosity, cautious in combat
            personality.curiosity = GetTraitInRange(0.70f, 0.90f);
            personality.caution = GetTraitInRange(0.50f, 0.70f);
            personality.aggression = GetTraitInRange(0.20f, 0.40f);
            personality.sociability = GetTraitInRange(0.40f, 0.60f);
            personality.greed = GetTraitInRange(0.35f, 0.55f);
            personality.helpfulness = GetTraitInRange(0.45f, 0.65f);
            break;

        case CLASS_WARLOCK:
            // Warlocks: Dark scholars dealing with demons. Curious but self-serving
            personality.curiosity = GetTraitInRange(0.60f, 0.80f);
            personality.greed = GetTraitInRange(0.55f, 0.75f);
            personality.vengefulness = GetTraitInRange(0.45f, 0.65f);
            personality.helpfulness = GetTraitInRange(0.20f, 0.40f);
            personality.loyalty = GetTraitInRange(0.30f, 0.50f);
            personality.sociability = GetTraitInRange(0.25f, 0.45f);
            break;

        case CLASS_DRUID:
            // Druids: Nature guardians, balanced and adaptable. Helpful protectors
            personality.helpfulness = GetTraitInRange(0.55f, 0.75f);
            personality.curiosity = GetTraitInRange(0.50f, 0.70f);
            personality.loyalty = GetTraitInRange(0.50f, 0.70f);
            personality.caution = GetTraitInRange(0.45f, 0.65f);
            personality.aggression = GetTraitInRange(0.35f, 0.55f);
            personality.sociability = GetTraitInRange(0.40f, 0.60f);
            break;

        default:
            // Unknown class - use pure random
            personality.Randomize();
            break;
    }

    // ==== RACIAL MODIFIERS ====
    // Each race has cultural traits that slightly modify personality
    switch (playerRace)
    {
        case RACE_HUMAN:
            // Humans: Diplomatic, adaptable. +sociability, +loyalty
            personality.sociability = std::min(1.0f, personality.sociability + 0.10f);
            personality.loyalty = std::min(1.0f, personality.loyalty + 0.05f);
            break;

        case RACE_ORC:
            // Orcs: Proud warriors. +aggression, +loyalty (to Horde)
            personality.aggression = std::min(1.0f, personality.aggression + 0.15f);
            personality.loyalty = std::min(1.0f, personality.loyalty + 0.10f);
            break;

        case RACE_DWARF:
            // Dwarves: Treasure-loving, loyal friends. +greed, +loyalty
            personality.greed = std::min(1.0f, personality.greed + 0.12f);
            personality.loyalty = std::min(1.0f, personality.loyalty + 0.10f);
            break;

        case RACE_NIGHT_ELF:
            // Night Elves: Ancient, mysterious. +curiosity, -sociability
            personality.curiosity = std::min(1.0f, personality.curiosity + 0.10f);
            personality.sociability = std::max(0.0f, personality.sociability - 0.05f);
            personality.caution = std::min(1.0f, personality.caution + 0.05f);
            break;

        case RACE_UNDEAD:
            // Forsaken: Bitter, vengeful. +vengefulness, -helpfulness
            personality.vengefulness = std::min(1.0f, personality.vengefulness + 0.15f);
            personality.helpfulness = std::max(0.0f, personality.helpfulness - 0.10f);
            personality.sociability = std::max(0.0f, personality.sociability - 0.05f);
            break;

        case RACE_TAUREN:
            // Tauren: Peaceful, wise. +helpfulness, +caution
            personality.helpfulness = std::min(1.0f, personality.helpfulness + 0.12f);
            personality.caution = std::min(1.0f, personality.caution + 0.08f);
            personality.aggression = std::max(0.0f, personality.aggression - 0.05f);
            break;

        case RACE_GNOME:
            // Gnomes: Curious inventors, treasure seekers. +curiosity, +greed
            personality.curiosity = std::min(1.0f, personality.curiosity + 0.15f);
            personality.greed = std::min(1.0f, personality.greed + 0.08f);
            break;

        case RACE_TROLL:
            // Trolls: Fierce, mystical. +vengefulness, +curiosity
            personality.vengefulness = std::min(1.0f, personality.vengefulness + 0.10f);
            personality.curiosity = std::min(1.0f, personality.curiosity + 0.05f);
            personality.aggression = std::min(1.0f, personality.aggression + 0.05f);
            break;

        case RACE_BLOOD_ELF:
            // Blood Elves: Proud, magic-addicted. +greed (for power), +curiosity
            personality.greed = std::min(1.0f, personality.greed + 0.10f);
            personality.curiosity = std::min(1.0f, personality.curiosity + 0.08f);
            personality.sociability = std::min(1.0f, personality.sociability + 0.05f);
            break;

        case RACE_DRAENEI:
            // Draenei: Noble exiles. +helpfulness, +loyalty
            personality.helpfulness = std::min(1.0f, personality.helpfulness + 0.12f);
            personality.loyalty = std::min(1.0f, personality.loyalty + 0.10f);
            personality.vengefulness = std::max(0.0f, personality.vengefulness - 0.05f);
            break;

        default:
            // Unknown race - no modifier
            break;
    }

    // Set initial mood based on personality
    if (personality.aggression > 0.7f)
        personality.currentMood = "fierce";
    else if (personality.helpfulness > 0.7f)
        personality.currentMood = "friendly";
    else if (personality.curiosity > 0.7f)
        personality.currentMood = "curious";
    else if (personality.greed > 0.7f)
        personality.currentMood = "ambitious";
    else
        personality.currentMood = "neutral";

    personality.moodTimestamp = NowMs();

    ALIVE_LOG_DEBUG("Initialized class/race personality: class=%u, race=%u, aggr=%.2f, help=%.2f, curio=%.2f",
        playerClass, playerRace, personality.aggression, personality.helpfulness, personality.curiosity);
}

BotMemoryData* BotMemoryManager::GetBotMemory(uint64 botGuid)
{
    std::shared_lock<std::shared_mutex> lock(m_memoryMutex);

    auto it = m_botMemory.find(botGuid);
    return it != m_botMemory.end() ? &it->second : nullptr;
}

const BotMemoryData* BotMemoryManager::GetBotMemory(uint64 botGuid) const
{
    std::shared_lock<std::shared_mutex> lock(m_memoryMutex);

    auto it = m_botMemory.find(botGuid);
    return it != m_botMemory.end() ? &it->second : nullptr;
}

BotMemoryData& BotMemoryManager::GetOrCreateBotMemory(uint64 botGuid, const std::string& botName)
{
    std::unique_lock<std::shared_mutex> lock(m_memoryMutex);

    auto it = m_botMemory.find(botGuid);
    if (it != m_botMemory.end())
        return it->second;

    // Create new memory
    BotMemoryData newMemory;
    newMemory.botGuid = botGuid;
    newMemory.botName = botName;
    newMemory.currentTier = BotTier::TIER_3_PASSIVE;

    // Get class/race from the live Player object instead of a DB query.
    // The bot is already loaded in memory — querying the DB here would block
    // the world thread during mass bot logins (80+ bots) and risk deadlock
    // from the lock.unlock()/lock.lock() dance.
    uint8 playerClass = 0;
    uint8 playerRace = 0;

    Player* botPlayer = ObjectAccessor::FindPlayer(ObjectGuid::Create<HighGuid::Player>(botGuid));
    if (botPlayer)
    {
        playerClass = botPlayer->getClass();
        playerRace = botPlayer->getRace();
    }

    // Initialize personality based on class/race if available, otherwise random
    if (playerClass > 0 && playerRace > 0)
    {
        InitializePersonalityFromClassRace(newMemory.personality, playerClass, playerRace);
        ALIVE_LOG_DEBUG("Created class/race-based memory for bot %s (%llu): class=%u, race=%u",
            botName.c_str(), botGuid, playerClass, playerRace);
    }
    else
    {
        // Fallback to random personality if we couldn't get class/race
        InitializeRandomPersonality(newMemory.personality);
        ALIVE_LOG_DEBUG("Created random memory for bot %s (%llu) - no class/race data found",
            botName.c_str(), botGuid);
    }

    ApplyPersonalityOverride(botGuid, newMemory);

    m_botMemory[botGuid] = newMemory;

    // Mark as dirty for persistence
    {
        std::lock_guard<std::mutex> dirtyLock(m_dirtyMutex);
        m_dirtyBots.insert(botGuid);
    }

    return m_botMemory[botGuid];
}

bool BotMemoryManager::HasBotMemory(uint64 botGuid) const
{
    std::shared_lock<std::shared_mutex> lock(m_memoryMutex);
    return m_botMemory.find(botGuid) != m_botMemory.end();
}

void BotMemoryManager::RemoveBotMemory(uint64 botGuid)
{
    std::unique_lock<std::shared_mutex> lock(m_memoryMutex);
    m_botMemory.erase(botGuid);

    std::lock_guard<std::mutex> dirtyLock(m_dirtyMutex);
    m_dirtyBots.erase(botGuid);

    std::lock_guard<std::mutex> tierLock(m_tierMutex);
    m_tier1Bots.erase(botGuid);
    m_tier2Bots.erase(botGuid);
}

// =========================================================================
// COMBAT MEMORY
// =========================================================================

void BotMemoryManager::RecordCombat(uint64 botGuid, const CombatMemory& combat)
{
    std::unique_lock<std::shared_mutex> lock(m_memoryMutex);

    auto it = m_botMemory.find(botGuid);
    if (it == m_botMemory.end())
        return;

    BotMemoryData& memory = it->second;

    // Add combat to log
    memory.combatLog.push_back(combat);

    // Trim to max size
    while (memory.combatLog.size() > m_maxCombatLog)
    {
        memory.combatLog.pop_front();
    }

    // Mark dirty
    {
        std::lock_guard<std::mutex> dirtyLock(m_dirtyMutex);
        m_dirtyBots.insert(botGuid);
    }

    // Personality evolution based on combat
    if (m_personalityEvolution)
    {
        if (combat.opponentType == "player")
        {
            if (combat.wasVictorious)
                memory.personality.aggression = std::min(1.0f, memory.personality.aggression + 0.05f);
            else
                memory.personality.aggression = std::max(0.0f, memory.personality.aggression - 0.03f);
        }
    }

    ApplyPersonalityOverride(botGuid, memory);

    ALIVE_LOG_TRACE("Recorded combat for bot %llu vs %s (%s)",
        botGuid, combat.opponentName.c_str(), combat.wasVictorious ? "WIN" : "LOSS");
}

std::vector<CombatMemory> BotMemoryManager::GetRecentCombat(uint64 botGuid, uint32 count) const
{
    std::shared_lock<std::shared_mutex> lock(m_memoryMutex);

    auto it = m_botMemory.find(botGuid);
    if (it == m_botMemory.end())
        return {};

    const auto& combatLog = it->second.combatLog;
    std::vector<CombatMemory> result;

    uint32 toGet = std::min<uint32>(count, static_cast<uint32>(combatLog.size()));
    for (auto rit = combatLog.rbegin(); rit != combatLog.rend() && result.size() < toGet; ++rit)
    {
        result.push_back(*rit);
    }

    return result;
}

float BotMemoryManager::GetCombatWinRate(uint64 botGuid) const
{
    std::shared_lock<std::shared_mutex> lock(m_memoryMutex);

    auto it = m_botMemory.find(botGuid);
    if (it == m_botMemory.end() || it->second.combatLog.empty())
        return 0.5f;  // Default to 50%

    const auto& combatLog = it->second.combatLog;
    uint32 wins = 0;
    for (const auto& combat : combatLog)
    {
        if (combat.wasVictorious)
            wins++;
    }

    return static_cast<float>(wins) / combatLog.size();
}

// =========================================================================
// SOCIAL MEMORY
// =========================================================================

void BotMemoryManager::RecordSocialInteraction(uint64 botGuid, uint64 playerGuid,
                                               const std::string& playerName,
                                               const std::string& interactionType,
                                               float sentimentDelta)
{
    std::unique_lock<std::shared_mutex> lock(m_memoryMutex);

    auto it = m_botMemory.find(botGuid);
    if (it == m_botMemory.end())
        return;

    BotMemoryData& memory = it->second;

    // Find or create social connection
    auto socialIt = memory.socialConnections.find(playerGuid);
    if (socialIt == memory.socialConnections.end())
    {
        // Check if at max connections
        if (memory.socialConnections.size() >= m_maxSocialConnections)
        {
            // Remove oldest/least interacted connection
            uint64 toRemove = 0;
            uint32 minInteractions = UINT32_MAX;
            for (const auto& pair : memory.socialConnections)
            {
                if (pair.second.interactionCount < minInteractions)
                {
                    minInteractions = pair.second.interactionCount;
                    toRemove = pair.first;
                }
            }
            if (toRemove > 0)
                memory.socialConnections.erase(toRemove);
        }

        SocialMemory newSocial;
        newSocial.playerGuid = ObjectGuid::Create<HighGuid::Player>(playerGuid);
        newSocial.playerName = playerName;
        newSocial.sentimentScore = 0.0f;
        newSocial.interactionCount = 0;
        memory.socialConnections[playerGuid] = newSocial;
        socialIt = memory.socialConnections.find(playerGuid);
    }

    SocialMemory& social = socialIt->second;
    social.interactionCount++;
    social.lastInteractionTime = NowMs();
    social.lastInteractionType = interactionType;
    social.sentimentScore = std::clamp(social.sentimentScore + sentimentDelta, -1.0f, 1.0f);

    // Mark dirty
    {
        std::lock_guard<std::mutex> dirtyLock(m_dirtyMutex);
        m_dirtyBots.insert(botGuid);
    }

    // Update world state social graph
    if (m_worldStateManager)
    {
        m_worldStateManager->RecordSocialConnection(botGuid, playerGuid, interactionType);
    }

    // Personality evolution
    if (m_personalityEvolution)
    {
        if (interactionType == "group" || interactionType == "help")
        {
            memory.personality.helpfulness = std::min(1.0f, memory.personality.helpfulness + 0.02f);
        }
        else if (interactionType == "chat")
        {
            memory.personality.sociability = std::min(1.0f, memory.personality.sociability + 0.01f);
        }
    }

    ApplyPersonalityOverride(botGuid, memory);

    ALIVE_LOG_TRACE("Social interaction: bot %llu with %s (type: %s, sentiment delta: %.2f)",
        botGuid, playerName.c_str(), interactionType.c_str(), sentimentDelta);
}

SocialMemory* BotMemoryManager::GetSocialMemory(uint64 botGuid, uint64 playerGuid)
{
    std::shared_lock<std::shared_mutex> lock(m_memoryMutex);

    auto it = m_botMemory.find(botGuid);
    if (it == m_botMemory.end())
        return nullptr;

    auto socialIt = it->second.socialConnections.find(playerGuid);
    return socialIt != it->second.socialConnections.end() ? &socialIt->second : nullptr;
}

float BotMemoryManager::GetSentiment(uint64 botGuid, uint64 playerGuid) const
{
    std::shared_lock<std::shared_mutex> lock(m_memoryMutex);

    auto it = m_botMemory.find(botGuid);
    if (it == m_botMemory.end())
        return 0.0f;

    auto socialIt = it->second.socialConnections.find(playerGuid);
    return socialIt != it->second.socialConnections.end() ? socialIt->second.sentimentScore : 0.0f;
}

void BotMemoryManager::UpdateSentiment(uint64 botGuid, uint64 playerGuid, float delta)
{
    std::unique_lock<std::shared_mutex> lock(m_memoryMutex);

    auto it = m_botMemory.find(botGuid);
    if (it == m_botMemory.end())
        return;

    auto socialIt = it->second.socialConnections.find(playerGuid);
    if (socialIt != it->second.socialConnections.end())
    {
        socialIt->second.sentimentScore = std::clamp(socialIt->second.sentimentScore + delta, -1.0f, 1.0f);

        std::lock_guard<std::mutex> dirtyLock(m_dirtyMutex);
        m_dirtyBots.insert(botGuid);
    }
}

std::vector<SocialMemory> BotMemoryManager::GetFriends(uint64 botGuid, float minSentiment) const
{
    std::shared_lock<std::shared_mutex> lock(m_memoryMutex);

    std::vector<SocialMemory> result;
    auto it = m_botMemory.find(botGuid);
    if (it == m_botMemory.end())
        return result;

    for (const auto& pair : it->second.socialConnections)
    {
        if (pair.second.sentimentScore >= minSentiment)
            result.push_back(pair.second);
    }

    return result;
}

std::vector<SocialMemory> BotMemoryManager::GetEnemies(uint64 botGuid, float maxSentiment) const
{
    std::shared_lock<std::shared_mutex> lock(m_memoryMutex);

    std::vector<SocialMemory> result;
    auto it = m_botMemory.find(botGuid);
    if (it == m_botMemory.end())
        return result;

    for (const auto& pair : it->second.socialConnections)
    {
        if (pair.second.sentimentScore <= maxSentiment)
            result.push_back(pair.second);
    }

    return result;
}

// =========================================================================
// LOCATION MEMORY
// =========================================================================

void BotMemoryManager::RecordZoneVisit(uint64 botGuid, uint32 zoneId, uint32 durationMinutes)
{
    std::unique_lock<std::shared_mutex> lock(m_memoryMutex);

    auto it = m_botMemory.find(botGuid);
    if (it == m_botMemory.end())
        return;

    BotMemoryData& memory = it->second;
    memory.zoneTimeSpent[zoneId] += durationMinutes;

    // Update favorite zone
    uint32 maxTime = 0;
    for (const auto& pair : memory.zoneTimeSpent)
    {
        if (pair.second > maxTime)
        {
            maxTime = pair.second;
            memory.favoriteZoneId = pair.first;
        }
    }

    std::lock_guard<std::mutex> dirtyLock(m_dirtyMutex);
    m_dirtyBots.insert(botGuid);
}

uint32 BotMemoryManager::GetFavoriteZone(uint64 botGuid) const
{
    std::shared_lock<std::shared_mutex> lock(m_memoryMutex);

    auto it = m_botMemory.find(botGuid);
    return it != m_botMemory.end() ? it->second.favoriteZoneId : 0;
}

uint32 BotMemoryManager::GetZoneTimeSpent(uint64 botGuid, uint32 zoneId) const
{
    std::shared_lock<std::shared_mutex> lock(m_memoryMutex);

    auto it = m_botMemory.find(botGuid);
    if (it == m_botMemory.end())
        return 0;

    auto zoneIt = it->second.zoneTimeSpent.find(zoneId);
    return zoneIt != it->second.zoneTimeSpent.end() ? zoneIt->second : 0;
}

void BotMemoryManager::SetCurrentZone(uint64 botGuid, uint32 zoneId)
{
    std::unique_lock<std::shared_mutex> lock(m_memoryMutex);

    auto it = m_botMemory.find(botGuid);
    if (it != m_botMemory.end())
    {
        it->second.currentZoneId = zoneId;
    }
}

// =========================================================================
// PERSONALITY
// =========================================================================

BotPersonality& BotMemoryManager::GetPersonality(uint64 botGuid)
{
    static BotPersonality s_defaultPersonality;

    std::shared_lock<std::shared_mutex> lock(m_memoryMutex);

    auto it = m_botMemory.find(botGuid);
    return it != m_botMemory.end() ? it->second.personality : s_defaultPersonality;
}

void BotMemoryManager::EvolvePersonality(uint64 botGuid, const std::string& eventType, float intensity)
{
    if (!m_personalityEvolution)
        return;

    std::unique_lock<std::shared_mutex> lock(m_memoryMutex);

    auto it = m_botMemory.find(botGuid);
    if (it == m_botMemory.end())
        return;

    BotPersonality& personality = it->second.personality;

    if (eventType == "pvp_win")
        personality.aggression = std::min(1.0f, personality.aggression + intensity);
    else if (eventType == "pvp_loss")
        personality.aggression = std::max(0.0f, personality.aggression - intensity);
    else if (eventType == "group_success")
        personality.helpfulness = std::min(1.0f, personality.helpfulness + intensity);
    else if (eventType == "exploration")
        personality.curiosity = std::min(1.0f, personality.curiosity + intensity);
    else if (eventType == "loot")
        personality.greed = std::min(1.0f, personality.greed + intensity);

    ApplyPersonalityOverride(botGuid, it->second);

    std::lock_guard<std::mutex> dirtyLock(m_dirtyMutex);
    m_dirtyBots.insert(botGuid);
}

void BotMemoryManager::SetMood(uint64 botGuid, const std::string& mood)
{
    bool updated = false;
    std::unique_lock<std::shared_mutex> lock(m_memoryMutex);

    auto it = m_botMemory.find(botGuid);
    if (it != m_botMemory.end())
    {
        it->second.personality.currentMood = mood;
        it->second.personality.moodTimestamp = NowMs();
        updated = true;
        ApplyPersonalityOverride(botGuid, it->second);
    }

    if (updated)
    {
        std::lock_guard<std::mutex> dirtyLock(m_dirtyMutex);
        m_dirtyBots.insert(botGuid);
    }
}

std::string BotMemoryManager::GetMood(uint64 botGuid) const
{
    std::shared_lock<std::shared_mutex> lock(m_memoryMutex);

    auto it = m_botMemory.find(botGuid);
    return it != m_botMemory.end() ? it->second.personality.currentMood : "neutral";
}

bool BotMemoryManager::HasPersonalityOverride(uint64 botGuid) const
{
    std::shared_lock<std::shared_mutex> lock(m_memoryMutex);
    auto it = m_personalityOverrides.find(botGuid);
    return it != m_personalityOverrides.end() && it->second.enabled;
}

bool BotMemoryManager::GetPersonalityOverride(uint64 botGuid, BotPersonalityOverride& outOverride) const
{
    std::shared_lock<std::shared_mutex> lock(m_memoryMutex);
    auto it = m_personalityOverrides.find(botGuid);
    if (it == m_personalityOverrides.end())
        return false;

    outOverride = it->second;
    return true;
}

std::vector<BotPersonalityOverride> BotMemoryManager::GetRelentlessHunterProfiles() const
{
    std::vector<BotPersonalityOverride> hunters;
    std::shared_lock<std::shared_mutex> lock(m_memoryMutex);

    for (auto const& [botGuid, profile] : m_personalityOverrides)
    {
        if (profile.IsRelentlessHunter())
            hunters.push_back(profile);
    }

    return hunters;
}

void BotMemoryManager::ReloadPersonalityOverrides()
{
    LoadPersonalityOverrides();
    EnforcePersonalityOverrides();
}

// =========================================================================
// GOSSIP KNOWLEDGE
// =========================================================================

void BotMemoryManager::LearnGossip(uint64 botGuid, uint32 gossipId)
{
    std::unique_lock<std::shared_mutex> lock(m_memoryMutex);

    auto it = m_botMemory.find(botGuid);
    if (it == m_botMemory.end())
        return;

    auto& knownGossip = it->second.knownGossipIds;
    if (std::find(knownGossip.begin(), knownGossip.end(), gossipId) == knownGossip.end())
    {
        knownGossip.push_back(gossipId);

        // Keep list reasonable size
        if (knownGossip.size() > 50)
            knownGossip.erase(knownGossip.begin());
    }

    // Also mark in world state
    if (m_worldStateManager)
    {
        m_worldStateManager->MarkGossipKnown(gossipId, botGuid);
    }
}

bool BotMemoryManager::KnowsGossip(uint64 botGuid, uint32 gossipId) const
{
    std::shared_lock<std::shared_mutex> lock(m_memoryMutex);

    auto it = m_botMemory.find(botGuid);
    if (it == m_botMemory.end())
        return false;

    const auto& knownGossip = it->second.knownGossipIds;
    return std::find(knownGossip.begin(), knownGossip.end(), gossipId) != knownGossip.end();
}

std::vector<uint32> BotMemoryManager::GetKnownGossip(uint64 botGuid) const
{
    std::shared_lock<std::shared_mutex> lock(m_memoryMutex);

    auto it = m_botMemory.find(botGuid);
    return it != m_botMemory.end() ? it->second.knownGossipIds : std::vector<uint32>();
}

// =========================================================================
// WORLD EVENT PARTICIPATION (mod-ec-world-events integration)
// =========================================================================

void BotMemoryManager::RecordEventJoin(uint64 botGuid, uint32 eventId, uint32 instanceId,
                                       const std::string& eventName, uint8 eventType, uint32 zoneId,
                                       bool voluntary, const std::string& role)
{
    std::unique_lock<std::shared_mutex> lock(m_memoryMutex);

    auto it = m_botMemory.find(botGuid);
    if (it == m_botMemory.end())
        return;

    // Create new participation record
    EventParticipation participation;
    participation.eventId = eventId;
    participation.instanceId = instanceId;
    participation.eventName = eventName;
    participation.eventType = eventType;
    participation.zoneId = zoneId;
    participation.joinTime = NowMs();
    participation.leaveTime = 0;
    participation.wasVoluntary = voluntary;
    participation.role = role;

    // Add to history and set as current event
    it->second.eventHistory.push_back(participation);
    it->second.currentEventInstanceId = instanceId;

    // Keep history reasonable size (max 10)
    while (it->second.eventHistory.size() > 10)
        it->second.eventHistory.pop_front();

    ALIVE_LOG_DEBUG("Bot %s joined event '%s' (instance %u) as %s",
        it->second.botName.c_str(), eventName.c_str(), instanceId, role.c_str());
}

void BotMemoryManager::RecordEventLeave(uint64 botGuid, uint32 instanceId, bool successful)
{
    std::unique_lock<std::shared_mutex> lock(m_memoryMutex);

    auto it = m_botMemory.find(botGuid);
    if (it == m_botMemory.end())
        return;

    // Find and update the participation record
    for (auto& event : it->second.eventHistory)
    {
        if (event.instanceId == instanceId && event.leaveTime == 0)
        {
            event.leaveTime = NowMs();
            event.wasSuccessful = successful;

            ALIVE_LOG_DEBUG("Bot %s left event '%s' (instance %u) - %s",
                it->second.botName.c_str(), event.eventName.c_str(),
                instanceId, successful ? "SUCCESS" : "FAILED");
            break;
        }
    }

    // Clear current event if it matches
    if (it->second.currentEventInstanceId == instanceId)
        it->second.currentEventInstanceId = 0;
}

void BotMemoryManager::UpdateEventStats(uint64 botGuid, uint32 instanceId,
                                        uint32 kills, uint32 deaths,
                                        uint64 damageDealt, uint64 healingDone)
{
    std::unique_lock<std::shared_mutex> lock(m_memoryMutex);

    auto it = m_botMemory.find(botGuid);
    if (it == m_botMemory.end())
        return;

    // Find and update the participation record
    for (auto& event : it->second.eventHistory)
    {
        if (event.instanceId == instanceId)
        {
            event.kills = kills;
            event.deaths = deaths;
            event.damageDealt = damageDealt;
            event.healingDone = healingDone;
            break;
        }
    }
}

bool BotMemoryManager::IsInEvent(uint64 botGuid) const
{
    std::shared_lock<std::shared_mutex> lock(m_memoryMutex);

    auto it = m_botMemory.find(botGuid);
    return it != m_botMemory.end() && it->second.currentEventInstanceId != 0;
}

uint32 BotMemoryManager::GetCurrentEventInstance(uint64 botGuid) const
{
    std::shared_lock<std::shared_mutex> lock(m_memoryMutex);

    auto it = m_botMemory.find(botGuid);
    return it != m_botMemory.end() ? it->second.currentEventInstanceId : 0;
}

std::vector<EventParticipation> BotMemoryManager::GetEventHistory(uint64 botGuid, uint32 count) const
{
    std::shared_lock<std::shared_mutex> lock(m_memoryMutex);

    auto it = m_botMemory.find(botGuid);
    if (it == m_botMemory.end())
        return {};

    std::vector<EventParticipation> result;
    const auto& history = it->second.eventHistory;

    // Return most recent events (from back of deque)
    uint32 startIdx = history.size() > count ? history.size() - count : 0;
    for (uint32 i = startIdx; i < history.size(); ++i)
        result.push_back(history[i]);

    return result;
}

uint32 BotMemoryManager::GetEventParticipationCount(uint64 botGuid) const
{
    std::shared_lock<std::shared_mutex> lock(m_memoryMutex);

    auto it = m_botMemory.find(botGuid);
    return it != m_botMemory.end() ? static_cast<uint32>(it->second.eventHistory.size()) : 0;
}

float BotMemoryManager::GetEventSuccessRate(uint64 botGuid) const
{
    std::shared_lock<std::shared_mutex> lock(m_memoryMutex);

    auto it = m_botMemory.find(botGuid);
    if (it == m_botMemory.end() || it->second.eventHistory.empty())
        return 0.0f;

    uint32 successful = 0;
    uint32 completed = 0;

    for (const auto& event : it->second.eventHistory)
    {
        if (event.leaveTime > 0)  // Event is finished
        {
            ++completed;
            if (event.wasSuccessful)
                ++successful;
        }
    }

    return completed > 0 ? static_cast<float>(successful) / static_cast<float>(completed) : 0.0f;
}

// =========================================================================
// INTELLIGENCE TIER
// =========================================================================

void BotMemoryManager::SetBotTier(uint64 botGuid, BotTier tier)
{
    BotTier oldTier = BotTier::TIER_3_PASSIVE;
    std::string botName;

    {
        std::unique_lock<std::shared_mutex> memLock(m_memoryMutex);
        auto it = m_botMemory.find(botGuid);
        if (it != m_botMemory.end())
        {
            oldTier = it->second.currentTier;
            botName = it->second.botName;
            it->second.currentTier = tier;
            it->second.lastTierUpdateTime = NowMs();
        }
    }

    {
        std::lock_guard<std::mutex> tierLock(m_tierMutex);

        // Remove from all tier sets first
        m_tier1Bots.erase(botGuid);
        m_tier2Bots.erase(botGuid);

        // Add to appropriate tier
        switch (tier)
        {
            case BotTier::TIER_1_ACTIVE:
                m_tier1Bots.insert(botGuid);
                break;
            case BotTier::TIER_2_AWARE:
                m_tier2Bots.insert(botGuid);
                break;
            default:
                break;  // Tier 3 is implicit (not in tier1 or tier2)
        }
    }

    // Emit tier change event if tier actually changed
    // Note: Lower tier number = higher priority (Tier 1 > Tier 2 > Tier 3)
    if (oldTier != tier)
    {
        std::string reason = "Tier evaluation update";
        if (static_cast<int>(oldTier) > static_cast<int>(tier))
            reason = "Promoted: player proximity or interaction";
        else
            reason = "Demoted: no player activity nearby";

        sAliveWorldBridge->EmitBotTierChanged(static_cast<uint32>(botGuid), oldTier, tier, reason);
    }
}

BotTier BotMemoryManager::GetBotTier(uint64 botGuid) const
{
    std::shared_lock<std::shared_mutex> lock(m_memoryMutex);

    auto it = m_botMemory.find(botGuid);
    return it != m_botMemory.end() ? it->second.currentTier : BotTier::TIER_3_PASSIVE;
}

std::vector<uint64> BotMemoryManager::GetBotsByTier(BotTier tier) const
{
    std::lock_guard<std::mutex> lock(m_tierMutex);

    switch (tier)
    {
        case BotTier::TIER_1_ACTIVE:
            return std::vector<uint64>(m_tier1Bots.begin(), m_tier1Bots.end());
        case BotTier::TIER_2_AWARE:
            return std::vector<uint64>(m_tier2Bots.begin(), m_tier2Bots.end());
        default:
            // For Tier 3, we'd need to return all bots not in tier1/tier2
            return {};
    }
}

uint32 BotMemoryManager::GetTierCount(BotTier tier) const
{
    std::lock_guard<std::mutex> lock(m_tierMutex);

    switch (tier)
    {
        case BotTier::TIER_1_ACTIVE:
            return static_cast<uint32>(m_tier1Bots.size());
        case BotTier::TIER_2_AWARE:
            return static_cast<uint32>(m_tier2Bots.size());
        default:
        {
            std::shared_lock<std::shared_mutex> memLock(m_memoryMutex);
            return static_cast<uint32>(m_botMemory.size()) -
                   static_cast<uint32>(m_tier1Bots.size()) -
                   static_cast<uint32>(m_tier2Bots.size());
        }
    }
}

// =========================================================================
// AI STATE
// =========================================================================

void BotMemoryManager::SetAIProcessing(uint64 botGuid, bool processing)
{
    std::unique_lock<std::shared_mutex> lock(m_memoryMutex);

    auto it = m_botMemory.find(botGuid);
    if (it != m_botMemory.end())
    {
        it->second.isProcessingAI = processing;
    }
}

bool BotMemoryManager::IsAIProcessing(uint64 botGuid) const
{
    std::shared_lock<std::shared_mutex> lock(m_memoryMutex);

    auto it = m_botMemory.find(botGuid);
    return it != m_botMemory.end() ? it->second.isProcessingAI : false;
}

void BotMemoryManager::SetLastAIRequest(uint64 botGuid, uint64 timestamp)
{
    std::unique_lock<std::shared_mutex> lock(m_memoryMutex);

    auto it = m_botMemory.find(botGuid);
    if (it != m_botMemory.end())
    {
        it->second.lastAIRequestTime = timestamp;
    }
}

uint64 BotMemoryManager::GetLastAIRequest(uint64 botGuid) const
{
    std::shared_lock<std::shared_mutex> lock(m_memoryMutex);

    auto it = m_botMemory.find(botGuid);
    return it != m_botMemory.end() ? it->second.lastAIRequestTime : 0;
}

void BotMemoryManager::RecordAIDecision(uint64 botGuid, const std::string& decision)
{
    std::unique_lock<std::shared_mutex> lock(m_memoryMutex);

    auto it = m_botMemory.find(botGuid);
    if (it != m_botMemory.end())
    {
        it->second.lastAIDecision = decision;
        it->second.lastAIDecisionTime = NowMs();
        ALIVE_LOG_TRACE("Bot %llu AI decision: %s", botGuid, decision.c_str());
    }
}

std::string BotMemoryManager::GetLastAIDecision(uint64 botGuid) const
{
    std::shared_lock<std::shared_mutex> lock(m_memoryMutex);

    auto it = m_botMemory.find(botGuid);
    return it != m_botMemory.end() ? it->second.lastAIDecision : "";
}

// =========================================================================
// PERSISTENCE
// =========================================================================

std::string BotMemoryManager::SerializeMemory(const BotMemoryData& memory) const
{
    // Simple JSON serialization for backward compatibility
    std::ostringstream ss;
    ss << "{";
    ss << "\"personality\":{";
    ss << "\"aggression\":" << memory.personality.aggression << ",";
    ss << "\"helpfulness\":" << memory.personality.helpfulness << ",";
    ss << "\"sociability\":" << memory.personality.sociability << ",";
    ss << "\"curiosity\":" << memory.personality.curiosity << ",";
    ss << "\"greed\":" << memory.personality.greed << ",";
    ss << "\"mood\":\"" << memory.personality.currentMood << "\"";
    ss << "},";
    ss << "\"favoriteZone\":" << memory.favoriteZoneId << ",";
    ss << "\"combatCount\":" << memory.combatLog.size() << ",";
    ss << "\"socialCount\":" << memory.socialConnections.size();
    ss << "}";
    return ss.str();
}

// =============================================================================
// JSON PARSING HELPERS
// =============================================================================
// Lightweight JSON parsing without external library dependencies.
// These helpers handle the specific JSON formats used for bot memory persistence.
// =============================================================================

// Helper: Skip whitespace in JSON string
static size_t SkipWhitespace(const std::string& json, size_t pos)
{
    while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t' ||
           json[pos] == '\n' || json[pos] == '\r'))
        pos++;
    return pos;
}

// Helper: Parse a quoted string value from JSON
static bool ParseJsonString(const std::string& json, size_t& pos, std::string& result)
{
    pos = SkipWhitespace(json, pos);
    if (pos >= json.size() || json[pos] != '"')
        return false;

    pos++;  // Skip opening quote
    result.clear();

    while (pos < json.size())
    {
        char c = json[pos];
        if (c == '"')
        {
            pos++;  // Skip closing quote
            return true;
        }
        if (c == '\\' && pos + 1 < json.size())
        {
            pos++;
            char escaped = json[pos];
            switch (escaped)
            {
                case '"': result += '"'; break;
                case '\\': result += '\\'; break;
                case 'n': result += '\n'; break;
                case 't': result += '\t'; break;
                case 'r': result += '\r'; break;
                default: result += escaped; break;
            }
        }
        else
        {
            result += c;
        }
        pos++;
    }
    return false;  // Unterminated string
}

// Helper: Parse a numeric value (int or float)
static bool ParseJsonNumber(const std::string& json, size_t& pos, double& result)
{
    pos = SkipWhitespace(json, pos);
    if (pos >= json.size())
        return false;

    size_t startPos = pos;
    bool hasDecimal = false;

    // Handle negative numbers
    if (json[pos] == '-')
        pos++;

    while (pos < json.size())
    {
        char c = json[pos];
        if (c >= '0' && c <= '9')
        {
            pos++;
        }
        else if (c == '.' && !hasDecimal)
        {
            hasDecimal = true;
            pos++;
        }
        else
        {
            break;
        }
    }

    if (pos == startPos)
        return false;

    try
    {
        result = std::stod(json.substr(startPos, pos - startPos));
        return true;
    }
    catch (...)
    {
        return false;
    }
}

// Helper: Parse a boolean value
static bool ParseJsonBool(const std::string& json, size_t& pos, bool& result)
{
    pos = SkipWhitespace(json, pos);
    if (pos + 4 <= json.size() && json.substr(pos, 4) == "true")
    {
        result = true;
        pos += 4;
        return true;
    }
    if (pos + 5 <= json.size() && json.substr(pos, 5) == "false")
    {
        result = false;
        pos += 5;
        return true;
    }
    return false;
}

// Helper: Find a key in a JSON object and return position after the colon
static size_t FindJsonKey(const std::string& json, const std::string& key, size_t startPos = 0)
{
    // Look for "key":
    std::string pattern = "\"" + key + "\"";
    size_t pos = json.find(pattern, startPos);
    if (pos == std::string::npos)
        return std::string::npos;

    pos += pattern.length();
    pos = SkipWhitespace(json, pos);

    if (pos >= json.size() || json[pos] != ':')
        return std::string::npos;

    return pos + 1;  // Position after the colon
}

// =============================================================================
// DeserializeMemory - Parse JSON into BotMemoryData structure
// =============================================================================
// This is the inverse of SerializeMemory. It parses a JSON string containing
// bot memory data and populates the BotMemoryData structure.
//
// The JSON format is:
// {
//   "personality": {
//     "aggression": 0.5,
//     "helpfulness": 0.5,
//     "sociability": 0.5,
//     "curiosity": 0.5,
//     "greed": 0.5,
//     "mood": "neutral"
//   },
//   "favoriteZone": 12,
//   "combatCount": 5,
//   "socialCount": 3
// }
//
// Note: This is a lightweight implementation. Complex nested structures
// (combat log, social connections) are parsed from separate columns in the
// database, not from this single JSON string.
// =============================================================================
bool BotMemoryManager::DeserializeMemory(const std::string& json, BotMemoryData& memory)
{
    if (json.empty() || json[0] != '{')
    {
        ALIVE_LOG_WARN("DeserializeMemory: Invalid JSON format (empty or not an object)");
        return false;
    }

    // Parse personality object
    size_t personalityPos = FindJsonKey(json, "personality");
    if (personalityPos != std::string::npos)
    {
        // Skip to opening brace of personality object
        personalityPos = SkipWhitespace(json, personalityPos);
        if (personalityPos < json.size() && json[personalityPos] == '{')
        {
            // Find the closing brace to limit our search
            size_t braceDepth = 1;
            size_t endPos = personalityPos + 1;
            while (endPos < json.size() && braceDepth > 0)
            {
                if (json[endPos] == '{') braceDepth++;
                else if (json[endPos] == '}') braceDepth--;
                endPos++;
            }
            std::string personalityJson = json.substr(personalityPos, endPos - personalityPos);

            // Parse individual personality traits
            double value;
            size_t pos;

            pos = FindJsonKey(personalityJson, "aggression");
            if (pos != std::string::npos && ParseJsonNumber(personalityJson, pos, value))
                memory.personality.aggression = static_cast<float>(value);

            pos = FindJsonKey(personalityJson, "helpfulness");
            if (pos != std::string::npos && ParseJsonNumber(personalityJson, pos, value))
                memory.personality.helpfulness = static_cast<float>(value);

            pos = FindJsonKey(personalityJson, "sociability");
            if (pos != std::string::npos && ParseJsonNumber(personalityJson, pos, value))
                memory.personality.sociability = static_cast<float>(value);

            pos = FindJsonKey(personalityJson, "curiosity");
            if (pos != std::string::npos && ParseJsonNumber(personalityJson, pos, value))
                memory.personality.curiosity = static_cast<float>(value);

            pos = FindJsonKey(personalityJson, "greed");
            if (pos != std::string::npos && ParseJsonNumber(personalityJson, pos, value))
                memory.personality.greed = static_cast<float>(value);

            // Additional personality traits that may be serialized
            pos = FindJsonKey(personalityJson, "caution");
            if (pos != std::string::npos && ParseJsonNumber(personalityJson, pos, value))
                memory.personality.caution = static_cast<float>(value);

            pos = FindJsonKey(personalityJson, "loyalty");
            if (pos != std::string::npos && ParseJsonNumber(personalityJson, pos, value))
                memory.personality.loyalty = static_cast<float>(value);

            pos = FindJsonKey(personalityJson, "vengefulness");
            if (pos != std::string::npos && ParseJsonNumber(personalityJson, pos, value))
                memory.personality.vengefulness = static_cast<float>(value);

            // Parse mood string
            std::string mood;
            pos = FindJsonKey(personalityJson, "mood");
            if (pos != std::string::npos && ParseJsonString(personalityJson, pos, mood))
                memory.personality.currentMood = mood;
        }
    }

    // Parse favorite zone
    size_t pos = FindJsonKey(json, "favoriteZone");
    if (pos != std::string::npos)
    {
        double value;
        if (ParseJsonNumber(json, pos, value))
            memory.favoriteZoneId = static_cast<uint32>(value);
    }

    // Parse current zone (if present)
    pos = FindJsonKey(json, "currentZone");
    if (pos != std::string::npos)
    {
        double value;
        if (ParseJsonNumber(json, pos, value))
            memory.currentZoneId = static_cast<uint32>(value);
    }

    // Parse tier (if present)
    pos = FindJsonKey(json, "tier");
    if (pos != std::string::npos)
    {
        double value;
        if (ParseJsonNumber(json, pos, value))
            memory.currentTier = static_cast<BotTier>(static_cast<uint8>(value));
    }

    // Note: combatCount and socialCount are informational only and don't need
    // to be deserialized since the actual data comes from separate JSON columns

    memory.personality.moodTimestamp = NowMs();

    ALIVE_LOG_TRACE("DeserializeMemory: Successfully parsed bot memory (zone=%u, mood=%s)",
        memory.favoriteZoneId, memory.personality.currentMood.c_str());

    return true;
}

std::string SerializeCombatLog(const std::deque<CombatMemory>& combatLog)
{
    if (combatLog.empty())
        return "[]";

    std::ostringstream ss;
    ss << "[";
    bool first = true;
    for (const auto& combat : combatLog)
    {
        if (!first) ss << ",";
        first = false;
        ss << "{";
        ss << "\"type\":\"" << combat.opponentType << "\",";
        ss << "\"guid\":" << combat.opponentGuid.GetRawValue() << ",";
        ss << "\"name\":\"" << combat.opponentName << "\",";
        ss << "\"level\":" << combat.opponentLevel << ",";
        ss << "\"won\":" << (combat.wasVictorious ? "true" : "false") << ",";
        ss << "\"dmgDealt\":" << combat.damageDealt << ",";
        ss << "\"dmgTaken\":" << combat.damageTaken << ",";
        ss << "\"zone\":" << combat.zoneId << ",";
        ss << "\"time\":" << combat.timestamp;
        ss << "}";
    }
    ss << "]";
    return ss.str();
}

std::string SerializeSocialConnections(const std::map<uint64, SocialMemory>& connections)
{
    if (connections.empty())
        return "{}";

    std::ostringstream ss;
    ss << "{";
    bool first = true;
    for (const auto& pair : connections)
    {
        if (!first) ss << ",";
        first = false;
        const SocialMemory& social = pair.second;
        ss << "\"" << pair.first << "\":{";
        ss << "\"name\":\"" << social.playerName << "\",";
        ss << "\"sentiment\":" << social.sentimentScore << ",";
        ss << "\"interactions\":" << social.interactionCount << ",";
        ss << "\"lastType\":\"" << social.lastInteractionType << "\",";
        ss << "\"lastTime\":" << social.lastInteractionTime;
        ss << "}";
    }
    ss << "}";
    return ss.str();
}

std::string SerializeZoneTime(const std::map<uint32, uint32>& zoneTimeSpent)
{
    if (zoneTimeSpent.empty())
        return "{}";

    std::ostringstream ss;
    ss << "{";
    bool first = true;
    for (const auto& pair : zoneTimeSpent)
    {
        if (!first) ss << ",";
        first = false;
        ss << "\"" << pair.first << "\":" << pair.second;
    }
    ss << "}";
    return ss.str();
}

std::string SerializeKnownGossip(const std::vector<uint32>& gossipIds)
{
    if (gossipIds.empty())
        return "[]";

    std::ostringstream ss;
    ss << "[";
    bool first = true;
    for (uint32 id : gossipIds)
    {
        if (!first) ss << ",";
        first = false;
        ss << id;
    }
    ss << "]";
    return ss.str();
}

void BotMemoryManager::LoadPersonalityOverrides()
{
    std::unordered_map<uint64, BotPersonalityOverride> loadedOverrides;

    // Check table existence first to avoid SQL errors on older installs.
    QueryResult tableExists = CharacterDatabase.Query(
        "SELECT 1 FROM information_schema.tables "
        "WHERE table_schema = DATABASE() "
        "AND table_name = 'alive_world_bot_personality_overrides' "
        "LIMIT 1"
    );

    if (!tableExists)
    {
        std::unique_lock<std::shared_mutex> lock(m_memoryMutex);
        m_personalityOverrides.clear();
        ALIVE_LOG_DEBUG("No personality override table found (alive_world_bot_personality_overrides)");
        return;
    }

    QueryResult result = CharacterDatabase.Query(
        "SELECT bot_guid, is_enabled, "
        "lock_aggression, aggression, lock_caution, caution, "
        "lock_sociability, sociability, lock_loyalty, loyalty, "
        "lock_curiosity, curiosity, lock_vengefulness, vengefulness, "
        "lock_helpfulness, helpfulness, lock_greed, greed, "
        "lock_mood, fixed_mood, "
        "relentless_hunter, relentless_target_guid, "
        "hunter_pursuit_radius, hunter_engage_radius, hunter_reengage_cooldown_ms "
        "FROM alive_world_bot_personality_overrides"
    );

    if (result)
    {
        do
        {
            Field* fields = result->Fetch();

            BotPersonalityOverride profile;
            profile.botGuid = fields[0].Get<uint64>();
            profile.enabled = fields[1].Get<uint8>() != 0;

            profile.lockAggression = fields[2].Get<uint8>() != 0;
            profile.aggression = PercentToTrait(fields[3].Get<uint8>());
            profile.lockCaution = fields[4].Get<uint8>() != 0;
            profile.caution = PercentToTrait(fields[5].Get<uint8>());
            profile.lockSociability = fields[6].Get<uint8>() != 0;
            profile.sociability = PercentToTrait(fields[7].Get<uint8>());
            profile.lockLoyalty = fields[8].Get<uint8>() != 0;
            profile.loyalty = PercentToTrait(fields[9].Get<uint8>());
            profile.lockCuriosity = fields[10].Get<uint8>() != 0;
            profile.curiosity = PercentToTrait(fields[11].Get<uint8>());
            profile.lockVengefulness = fields[12].Get<uint8>() != 0;
            profile.vengefulness = PercentToTrait(fields[13].Get<uint8>());
            profile.lockHelpfulness = fields[14].Get<uint8>() != 0;
            profile.helpfulness = PercentToTrait(fields[15].Get<uint8>());
            profile.lockGreed = fields[16].Get<uint8>() != 0;
            profile.greed = PercentToTrait(fields[17].Get<uint8>());

            profile.lockMood = fields[18].Get<uint8>() != 0;
            profile.fixedMood = fields[19].Get<std::string>();
            if (profile.fixedMood.empty())
                profile.fixedMood = "neutral";

            profile.relentlessHunter = fields[20].Get<uint8>() != 0;
            profile.relentlessTargetGuid = fields[21].Get<uint64>();
            profile.hunterPursuitRadius = fields[22].Get<float>();
            profile.hunterEngageRadius = fields[23].Get<float>();
            profile.hunterReengageCooldownMs = fields[24].Get<uint32>();

            // Keep sane bounds even if someone enters odd values.
            profile.hunterPursuitRadius = std::clamp(profile.hunterPursuitRadius, 5.0f, 500.0f);
            profile.hunterEngageRadius = std::clamp(profile.hunterEngageRadius, 5.0f, profile.hunterPursuitRadius);
            profile.hunterReengageCooldownMs = std::clamp(profile.hunterReengageCooldownMs, 250u, 60000u);

            loadedOverrides[profile.botGuid] = profile;
        }
        while (result->NextRow());
    }

    uint32 relentlessCount = 0;
    for (auto const& [guid, profile] : loadedOverrides)
    {
        if (profile.IsRelentlessHunter())
            ++relentlessCount;
    }

    size_t totalOverrides = loadedOverrides.size();
    {
        std::unique_lock<std::shared_mutex> lock(m_memoryMutex);
        m_personalityOverrides = std::move(loadedOverrides);
    }

    ALIVE_LOG_INFO("Loaded %zu personality overrides (%u relentless hunters)",
        totalOverrides, relentlessCount);
}

bool BotMemoryManager::ApplyPersonalityOverride(uint64 botGuid, BotMemoryData& memory)
{
    auto it = m_personalityOverrides.find(botGuid);
    if (it == m_personalityOverrides.end() || !it->second.enabled)
        return false;

    const BotPersonalityOverride& profile = it->second;
    bool changed = false;

    auto applyTrait = [&changed](bool shouldLock, float& trait, float value)
    {
        if (!shouldLock)
            return;

        float clamped = std::clamp(value, 0.0f, 1.0f);
        if (trait != clamped)
        {
            trait = clamped;
            changed = true;
        }
    };

    applyTrait(profile.lockAggression, memory.personality.aggression, profile.aggression);
    applyTrait(profile.lockCaution, memory.personality.caution, profile.caution);
    applyTrait(profile.lockSociability, memory.personality.sociability, profile.sociability);
    applyTrait(profile.lockLoyalty, memory.personality.loyalty, profile.loyalty);
    applyTrait(profile.lockCuriosity, memory.personality.curiosity, profile.curiosity);
    applyTrait(profile.lockVengefulness, memory.personality.vengefulness, profile.vengefulness);
    applyTrait(profile.lockHelpfulness, memory.personality.helpfulness, profile.helpfulness);
    applyTrait(profile.lockGreed, memory.personality.greed, profile.greed);

    if (profile.lockMood)
    {
        const std::string lockedMood = profile.fixedMood.empty() ? "neutral" : profile.fixedMood;
        if (memory.personality.currentMood != lockedMood)
        {
            memory.personality.currentMood = lockedMood;
            changed = true;
        }
    }

    if (changed)
        memory.personality.moodTimestamp = NowMs();

    return changed;
}

void BotMemoryManager::EnforcePersonalityOverrides()
{
    std::vector<uint64> changedBots;

    {
        std::unique_lock<std::shared_mutex> lock(m_memoryMutex);
        for (auto& [botGuid, memory] : m_botMemory)
        {
            if (ApplyPersonalityOverride(botGuid, memory))
                changedBots.push_back(botGuid);
        }
    }

    if (changedBots.empty())
        return;

    std::lock_guard<std::mutex> dirtyLock(m_dirtyMutex);
    for (uint64 botGuid : changedBots)
        m_dirtyBots.insert(botGuid);
}

void BotMemoryManager::LoadFromDatabase()
{
    ALIVE_LOG_INFO("Loading bot memories from database...");

    // Use the new comprehensive table schema
    QueryResult result = CharacterDatabase.Query(
        "SELECT bot_guid, bot_name, "
        "personality_aggression, personality_caution, personality_sociability, "
        "personality_loyalty, personality_curiosity, personality_vengefulness, "
        "personality_helpfulness, personality_greed, current_mood, "
        "favorite_zone_id, current_zone_id, current_tier, "
        "combat_log_json, social_connections_json, zone_time_json, known_gossip_json "
        "FROM alive_world_bot_memory"
    );

    if (!result)
    {
        // Try legacy table as fallback, joining with characters for class/race data
        result = CharacterDatabase.Query(
            "SELECT abm.bot_guid, abm.bot_name, abm.personality_json, abm.preferred_zone_id, "
            "COALESCE(c.class, 0) AS class, COALESCE(c.race, 0) AS race "
            "FROM alive_bot_memory abm "
            "LEFT JOIN characters c ON abm.bot_guid = c.guid"
        );

        if (!result)
        {
            ALIVE_LOG_INFO("No existing bot memories found in database");
            return;
        }

        // Load from legacy format with class/race-based personality
        uint32 count = 0;
        uint32 classBasedCount = 0;
        do
        {
            Field* fields = result->Fetch();
            uint64 botGuid = fields[0].Get<uint64>();
            std::string botName = fields[1].Get<std::string>();
            uint32 favoriteZone = fields[3].Get<uint32>();
            uint8 playerClass = fields[4].Get<uint8>();
            uint8 playerRace = fields[5].Get<uint8>();

            BotMemoryData memory;
            memory.botGuid = botGuid;
            memory.botName = botName;
            memory.favoriteZoneId = favoriteZone;
            memory.currentTier = BotTier::TIER_3_PASSIVE;

            // Use class/race-based personality if available
            if (playerClass > 0 && playerRace > 0)
            {
                InitializePersonalityFromClassRace(memory.personality, playerClass, playerRace);
                classBasedCount++;
            }
            else
            {
                InitializeRandomPersonality(memory.personality);
            }

            std::unique_lock<std::shared_mutex> lock(m_memoryMutex);
            ApplyPersonalityOverride(botGuid, memory);
            m_botMemory[botGuid] = memory;
            count++;
        } while (result->NextRow());

        ALIVE_LOG_INFO("Loaded %u bot memories from legacy database (%u with class/race personality)",
            count, classBasedCount);
        return;
    }

    // Load from new comprehensive schema
    uint32 count = 0;
    do
    {
        Field* fields = result->Fetch();

        BotMemoryData memory;
        memory.botGuid = fields[0].Get<uint64>();
        memory.botName = fields[1].Get<std::string>();

        // Personality (stored as 0-100, convert to 0.0-1.0)
        memory.personality.aggression = PercentToTrait(fields[2].Get<uint8>());
        memory.personality.caution = PercentToTrait(fields[3].Get<uint8>());
        memory.personality.sociability = PercentToTrait(fields[4].Get<uint8>());
        memory.personality.loyalty = PercentToTrait(fields[5].Get<uint8>());
        memory.personality.curiosity = PercentToTrait(fields[6].Get<uint8>());
        memory.personality.vengefulness = PercentToTrait(fields[7].Get<uint8>());
        memory.personality.helpfulness = PercentToTrait(fields[8].Get<uint8>());
        memory.personality.greed = PercentToTrait(fields[9].Get<uint8>());
        memory.personality.currentMood = fields[10].Get<std::string>();
        memory.personality.moodTimestamp = NowMs();

        memory.favoriteZoneId = fields[11].Get<uint32>();
        memory.currentZoneId = fields[12].Get<uint32>();
        memory.currentTier = static_cast<BotTier>(fields[13].Get<uint8>());

        // JSON columns - stored but not parsed (would need JSON parser)
        // For now, we just log that we have them but don't restore complex data
        std::string combatJson = fields[14].Get<std::string>();
        std::string socialJson = fields[15].Get<std::string>();
        std::string zoneTimeJson = fields[16].Get<std::string>();
        std::string gossipJson = fields[17].Get<std::string>();

        // Note: Full JSON parsing would require a JSON library
        // For now, complex data starts fresh but personality is preserved
        ALIVE_LOG_TRACE("Bot %s: combat=%zu bytes, social=%zu bytes, zones=%zu bytes",
            memory.botName.c_str(), combatJson.length(), socialJson.length(), zoneTimeJson.length());

        std::unique_lock<std::shared_mutex> lock(m_memoryMutex);
        ApplyPersonalityOverride(memory.botGuid, memory);
        m_botMemory[memory.botGuid] = memory;
        count++;

    } while (result->NextRow());

    ALIVE_LOG_INFO("Loaded %u bot memories from database", count);
}

void BotMemoryManager::SaveToDatabase()
{
    std::set<uint64> toSave;
    {
        std::lock_guard<std::mutex> lock(m_dirtyMutex);
        toSave = m_dirtyBots;
        m_dirtyBots.clear();
    }

    if (toSave.empty())
        return;

    ALIVE_LOG_DEBUG("Saving %zu bot memories to database", toSave.size());

    std::shared_lock<std::shared_mutex> lock(m_memoryMutex);

    CharacterDatabaseTransaction trans = CharacterDatabase.BeginTransaction();

    for (uint64 botGuid : toSave)
    {
        auto it = m_botMemory.find(botGuid);
        if (it == m_botMemory.end())
            continue;

        const BotMemoryData& memory = it->second;

        // Serialize complex data to JSON
        std::string combatJson = SerializeCombatLog(memory.combatLog);
        std::string socialJson = SerializeSocialConnections(memory.socialConnections);
        std::string zoneTimeJson = SerializeZoneTime(memory.zoneTimeSpent);
        std::string gossipJson = SerializeKnownGossip(memory.knownGossipIds);

        // Convert personality floats (0.0-1.0) to integers (0-100)
        uint8 aggression = TraitToPercent(memory.personality.aggression);
        uint8 caution = TraitToPercent(memory.personality.caution);
        uint8 sociability = TraitToPercent(memory.personality.sociability);
        uint8 loyalty = TraitToPercent(memory.personality.loyalty);
        uint8 curiosity = TraitToPercent(memory.personality.curiosity);
        uint8 vengefulness = TraitToPercent(memory.personality.vengefulness);
        uint8 helpfulness = TraitToPercent(memory.personality.helpfulness);
        uint8 greed = TraitToPercent(memory.personality.greed);

        // Create mutable copies for EscapeString (which modifies in-place)
        std::string botNameEsc = memory.botName;
        std::string moodEsc = memory.personality.currentMood;
        CharacterDatabase.EscapeString(botNameEsc);
        CharacterDatabase.EscapeString(moodEsc);
        CharacterDatabase.EscapeString(combatJson);
        CharacterDatabase.EscapeString(socialJson);
        CharacterDatabase.EscapeString(zoneTimeJson);
        CharacterDatabase.EscapeString(gossipJson);

        std::string sql = Acore::StringFormat(
            "REPLACE INTO alive_world_bot_memory "
            "(bot_guid, bot_name, "
            "personality_aggression, personality_caution, personality_sociability, "
            "personality_loyalty, personality_curiosity, personality_vengefulness, "
            "personality_helpfulness, personality_greed, current_mood, "
            "favorite_zone_id, current_zone_id, current_tier, "
            "combat_log_json, social_connections_json, zone_time_json, known_gossip_json) "
            "VALUES ({}, '{}', {}, {}, {}, {}, {}, {}, {}, {}, '{}', {}, {}, {}, '{}', '{}', '{}', '{}')",
            memory.botGuid,
            botNameEsc,
            aggression, caution, sociability, loyalty, curiosity, vengefulness, helpfulness, greed,
            moodEsc,
            memory.favoriteZoneId,
            memory.currentZoneId,
            static_cast<uint8>(memory.currentTier),
            combatJson,
            socialJson,
            zoneTimeJson,
            gossipJson
        );

        trans->Append(sql.c_str());
    }

    CharacterDatabase.CommitTransaction(trans);

    m_lastPersistTime = NowMs();
    ALIVE_LOG_DEBUG("Bot memories saved to database");
}

void BotMemoryManager::SaveBotMemory(uint64 botGuid)
{
    std::lock_guard<std::mutex> lock(m_dirtyMutex);
    m_dirtyBots.insert(botGuid);
}

void BotMemoryManager::LoadBotMemory(uint64 botGuid)
{
    QueryResult result = CharacterDatabase.Query(
        "SELECT bot_name, personality_json, preferred_zone_id FROM alive_bot_memory WHERE bot_guid = {}",
        botGuid
    );

    if (!result)
        return;

    Field* fields = result->Fetch();
    std::string botName = fields[0].Get<std::string>();
    uint32 favoriteZone = fields[2].Get<uint32>();

    // Query class/race for class-based personality
    uint8 playerClass = 0;
    uint8 playerRace = 0;

    QueryResult charResult = CharacterDatabase.Query(
        "SELECT class, race FROM characters WHERE guid = {}",
        botGuid
    );

    if (charResult)
    {
        Field* charFields = charResult->Fetch();
        playerClass = charFields[0].Get<uint8>();
        playerRace = charFields[1].Get<uint8>();
    }

    std::unique_lock<std::shared_mutex> lock(m_memoryMutex);

    if (m_botMemory.find(botGuid) == m_botMemory.end())
    {
        BotMemoryData memory;
        memory.botGuid = botGuid;
        memory.botName = botName;
        memory.favoriteZoneId = favoriteZone;
        memory.currentTier = BotTier::TIER_3_PASSIVE;

        // Use class/race-based personality if available
        if (playerClass > 0 && playerRace > 0)
            InitializePersonalityFromClassRace(memory.personality, playerClass, playerRace);
        else
            InitializeRandomPersonality(memory.personality);

        ApplyPersonalityOverride(botGuid, memory);
        m_botMemory[botGuid] = memory;
    }
}

// =========================================================================
// UPDATE
// =========================================================================

void BotMemoryManager::Update(uint32 diff)
{
    if (!m_enabled)
        return;

    EnforcePersonalityOverrides();

    m_persistTimer += diff;
    if (m_persistTimer >= m_persistIntervalSeconds * 1000)
    {
        m_persistTimer = 0;
        SaveToDatabase();
    }
}

// =========================================================================
// STATS
// =========================================================================

uint32 BotMemoryManager::GetTotalBots() const
{
    std::shared_lock<std::shared_mutex> lock(m_memoryMutex);
    return static_cast<uint32>(m_botMemory.size());
}

uint32 BotMemoryManager::GetLoadedBots() const
{
    return GetTotalBots();
}

} // namespace AliveWorld
