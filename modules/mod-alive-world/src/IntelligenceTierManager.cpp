/*
 * mod-alive-world - Intelligence Tier Manager Implementation
 * Copyright (C) 2025 EC
 */

#include "IntelligenceTierManager.h"
#include "AliveWorldManager.h"
#include "BotMemoryManager.h"
#include "WorldStateManager.h"
#include "Config.h"
#include "Group.h"
#include "Map.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "World.h"
#include "WorldSession.h"
#include "WorldSessionMgr.h"

#ifdef MOD_PLAYERBOTS
#include "PlayerbotAI.h"
#include "RandomPlayerbotMgr.h"
#endif

namespace AliveWorld
{

IntelligenceTierManager::IntelligenceTierManager()
    : m_maxTier1(10)
    , m_maxTier2(100)
    , m_tier1Distance(100.0f)
    , m_tierUpdateIntervalMs(5000)
    , m_memoryManager(nullptr)
    , m_worldStateManager(nullptr)
    , m_updateTimer(0)
{
}

IntelligenceTierManager::~IntelligenceTierManager()
{
    std::lock_guard<std::mutex> lock(m_tierMutex);
    m_tier1Bots.clear();
    m_tier2Bots.clear();
}

void IntelligenceTierManager::Initialize(BotMemoryManager* memoryManager, WorldStateManager* worldStateManager)
{
    ALIVE_LOG_INFO("Intelligence Tier Manager initializing...");

    m_memoryManager = memoryManager;
    m_worldStateManager = worldStateManager;
    LoadConfig();

    ALIVE_LOG_INFO("Intelligence Tier Manager configured:");
    ALIVE_LOG_INFO("  Max Tier 1: %u bots", m_maxTier1);
    ALIVE_LOG_INFO("  Max Tier 2: %u bots", m_maxTier2);
    ALIVE_LOG_INFO("  Tier 1 Distance: %.1f yards", m_tier1Distance);
    ALIVE_LOG_INFO("  Update Interval: %u ms", m_tierUpdateIntervalMs);
}

void IntelligenceTierManager::LoadConfig()
{
    m_maxTier1 = sConfigMgr->GetOption<uint32>("AliveWorld.MaxTier1Bots", 10);
    m_maxTier2 = sConfigMgr->GetOption<uint32>("AliveWorld.MaxTier2Bots", 100);
    m_tier1Distance = sConfigMgr->GetOption<float>("AliveWorld.Tier1Distance", 100.0f);
    m_tierUpdateIntervalMs = sConfigMgr->GetOption<uint32>("AliveWorld.TierUpdateIntervalMs", 5000);
}

bool IntelligenceTierManager::IsRealPlayer(Player* player) const
{
    if (!player)
        return false;

#ifdef MOD_PLAYERBOTS
    if (sPlayerbotsMgr.GetPlayerbotAI(player))
        return false;
    if (sRandomPlayerbotMgr.IsRandomBot(player))
        return false;
#endif

    return true;
}

std::vector<Player*> IntelligenceTierManager::GetRealPlayers() const
{
    std::vector<Player*> result;

    WorldSessionMgr::SessionMap const& sessions = sWorldSessionMgr->GetAllSessions();
    for (auto const& pair : sessions)
    {
        WorldSession* session = pair.second;
        if (!session)
            continue;

        Player* player = session->GetPlayer();
        if (player && player->IsInWorld() && IsRealPlayer(player))
        {
            result.push_back(player);
        }
    }

    return result;
}

float IntelligenceTierManager::GetDistanceToNearestPlayer(Player* bot) const
{
    if (!bot || !bot->IsInWorld())
        return FLT_MAX;

    float minDistance = FLT_MAX;
    std::vector<Player*> realPlayers = GetRealPlayers();

    for (Player* player : realPlayers)
    {
        if (!player->IsInWorld())
            continue;

        if (player->GetMapId() != bot->GetMapId())
            continue;

        float dist = bot->GetDistance(player);
        if (dist < minDistance)
            minDistance = dist;
    }

    return minDistance;
}

TierCriteria IntelligenceTierManager::EvaluateCriteria(uint64 botGuid, Player* bot)
{
    TierCriteria criteria;

    if (!bot || !bot->IsInWorld())
        return criteria;

    // Distance to nearest real player
    criteria.distanceToNearestPlayer = GetDistanceToNearestPlayer(bot);
    criteria.nearRealPlayer = criteria.distanceToNearestPlayer <= m_tier1Distance;

    // Check if same zone as real players
    uint32 botZone = bot->GetZoneId();
    if (m_worldStateManager)
    {
        ZoneState state = m_worldStateManager->GetZoneState(botZone);
        criteria.sameZoneAsPlayer = state.GetTotalPlayers() > 0;
    }

    // Check recent interaction
    {
        std::lock_guard<std::mutex> lock(m_interactionMutex);

        auto interactionIt = m_lastPlayerInteraction.find(botGuid);
        if (interactionIt != m_lastPlayerInteraction.end())
        {
            criteria.lastPlayerInteractionTime = interactionIt->second;
            uint64 timeSince = NowMs() - interactionIt->second;
            criteria.recentlySpokenTo = timeSince < RECENT_INTERACTION_THRESHOLD_MS;
        }

        criteria.inCombatWithPlayer = m_botsInCombatWithPlayer.find(botGuid) != m_botsInCombatWithPlayer.end();
        criteria.involvedInEvent = m_botsInWorldEvent.find(botGuid) != m_botsInWorldEvent.end();
    }

    // Check if in party with Tier 1 bot
    if (bot->GetGroup())
    {
        std::lock_guard<std::mutex> tierLock(m_tierMutex);
        for (auto it = bot->GetGroup()->GetMemberSlots().begin();
             it != bot->GetGroup()->GetMemberSlots().end(); ++it)
        {
            if (m_tier1Bots.find(it->guid.GetRawValue()) != m_tier1Bots.end())
            {
                criteria.inPartyWithTier1 = true;
                break;
            }
        }
    }

    return criteria;
}

float IntelligenceTierManager::CalculateTierScore(const TierCriteria& criteria)
{
    float score = 0.0f;

    // Immediate Tier 1 qualifiers
    if (criteria.inCombatWithPlayer)
        score += 100.0f;

    if (criteria.recentlySpokenTo)
        score += 80.0f;

    if (criteria.involvedInEvent)
        score += 60.0f;

    // Distance-based scoring
    if (criteria.nearRealPlayer)
    {
        // Closer = higher score (inverse relationship)
        float distanceScore = 50.0f * (1.0f - (criteria.distanceToNearestPlayer / m_tier1Distance));
        score += std::max(0.0f, distanceScore);
    }

    // Tier 2 qualifiers
    if (criteria.sameZoneAsPlayer)
        score += 20.0f;

    if (criteria.inPartyWithTier1)
        score += 30.0f;

    if (criteria.nearWorldEvent)
        score += 15.0f;

    return score;
}

BotTier IntelligenceTierManager::CalculateBotTier(uint64 botGuid, Player* bot)
{
    TierCriteria criteria = EvaluateCriteria(botGuid, bot);

    // Cache criteria for debugging
    {
        std::lock_guard<std::mutex> lock(m_criteriaMutex);
        m_criteriaCache[botGuid] = criteria;
    }

    float score = CalculateTierScore(criteria);

    // Tier 1: Score >= 50 (near player, in combat, recently spoken to, etc.)
    if (score >= 50.0f)
        return BotTier::TIER_1_ACTIVE;

    // Tier 2: Score >= 10 (same zone as player, in party with Tier 1, etc.)
    if (score >= 10.0f)
        return BotTier::TIER_2_AWARE;

    // Default: Tier 3
    return BotTier::TIER_3_PASSIVE;
}

void IntelligenceTierManager::UpdateAllTiers()
{
    ALIVE_LOG_TRACE("Updating all bot tiers...");

#ifdef MOD_PLAYERBOTS
    // Get all bots
    PlayerBotMap allBots = sRandomPlayerbotMgr.GetAllBots();

    if (allBots.empty())
        return;

    // Calculate tier for each bot and collect scores
    struct BotScore
    {
        uint64 guid;
        Player* bot;
        BotTier desiredTier;
        float score;
    };
    std::vector<BotScore> botScores;

    for (auto& pair : allBots)
    {
        Player* bot = pair.second;
        if (!bot || !bot->IsInWorld())
            continue;

        uint64 guid = bot->GetGUID().GetRawValue();
        TierCriteria criteria = EvaluateCriteria(guid, bot);
        float score = CalculateTierScore(criteria);
        BotTier desiredTier = CalculateBotTier(guid, bot);

        botScores.push_back({guid, bot, desiredTier, score});
    }

    // Sort by score (highest first)
    std::sort(botScores.begin(), botScores.end(),
        [](const BotScore& a, const BotScore& b) { return a.score > b.score; });

    // Assign tiers respecting limits
    uint32 tier1Count = 0;
    uint32 tier2Count = 0;

    std::set<uint64> newTier1;
    std::set<uint64> newTier2;

    for (const auto& bs : botScores)
    {
        BotTier finalTier = BotTier::TIER_3_PASSIVE;

        if (bs.desiredTier == BotTier::TIER_1_ACTIVE && tier1Count < m_maxTier1)
        {
            finalTier = BotTier::TIER_1_ACTIVE;
            newTier1.insert(bs.guid);
            tier1Count++;
        }
        else if ((bs.desiredTier == BotTier::TIER_1_ACTIVE || bs.desiredTier == BotTier::TIER_2_AWARE)
                 && tier2Count < m_maxTier2)
        {
            // Overflow from Tier 1 goes to Tier 2
            if (bs.desiredTier == BotTier::TIER_1_ACTIVE)
                finalTier = BotTier::TIER_2_AWARE;
            else
                finalTier = BotTier::TIER_2_AWARE;

            newTier2.insert(bs.guid);
            tier2Count++;
        }

        // Update memory manager
        if (m_memoryManager)
        {
            m_memoryManager->SetBotTier(bs.guid, finalTier);
        }
    }

    // Update our tier sets
    {
        std::lock_guard<std::mutex> lock(m_tierMutex);
        m_tier1Bots = newTier1;
        m_tier2Bots = newTier2;
    }

    ALIVE_LOG_DEBUG("Tier update complete: T1=%u/%u, T2=%u/%u, T3=%zu",
        tier1Count, m_maxTier1, tier2Count, m_maxTier2,
        botScores.size() - tier1Count - tier2Count);

#endif
}

void IntelligenceTierManager::PromoteToTier1(uint64 botGuid)
{
    std::lock_guard<std::mutex> lock(m_tierMutex);

    if (m_tier1Bots.size() >= m_maxTier1)
    {
        ALIVE_LOG_DEBUG("Cannot promote to Tier 1 - at capacity (%u/%u)", m_tier1Bots.size(), m_maxTier1);
        return;
    }

    m_tier2Bots.erase(botGuid);
    m_tier1Bots.insert(botGuid);

    if (m_memoryManager)
        m_memoryManager->SetBotTier(botGuid, BotTier::TIER_1_ACTIVE);

    ALIVE_LOG_DEBUG("Bot %llu promoted to Tier 1", botGuid);
}

void IntelligenceTierManager::DemoteFromTier1(uint64 botGuid)
{
    std::lock_guard<std::mutex> lock(m_tierMutex);

    if (m_tier1Bots.erase(botGuid) > 0)
    {
        // Move to Tier 2 if under limit
        if (m_tier2Bots.size() < m_maxTier2)
        {
            m_tier2Bots.insert(botGuid);
            if (m_memoryManager)
                m_memoryManager->SetBotTier(botGuid, BotTier::TIER_2_AWARE);
        }
        else
        {
            if (m_memoryManager)
                m_memoryManager->SetBotTier(botGuid, BotTier::TIER_3_PASSIVE);
        }

        ALIVE_LOG_DEBUG("Bot %llu demoted from Tier 1", botGuid);
    }
}

void IntelligenceTierManager::PromoteToTier2(uint64 botGuid)
{
    std::lock_guard<std::mutex> lock(m_tierMutex);

    if (m_tier2Bots.size() >= m_maxTier2)
    {
        ALIVE_LOG_DEBUG("Cannot promote to Tier 2 - at capacity (%u/%u)", m_tier2Bots.size(), m_maxTier2);
        return;
    }

    m_tier2Bots.insert(botGuid);

    if (m_memoryManager)
        m_memoryManager->SetBotTier(botGuid, BotTier::TIER_2_AWARE);

    ALIVE_LOG_DEBUG("Bot %llu promoted to Tier 2", botGuid);
}

void IntelligenceTierManager::DemoteToTier3(uint64 botGuid)
{
    std::lock_guard<std::mutex> lock(m_tierMutex);

    m_tier1Bots.erase(botGuid);
    m_tier2Bots.erase(botGuid);

    if (m_memoryManager)
        m_memoryManager->SetBotTier(botGuid, BotTier::TIER_3_PASSIVE);

    ALIVE_LOG_DEBUG("Bot %llu demoted to Tier 3", botGuid);
}

BotTier IntelligenceTierManager::GetBotTier(uint64 botGuid) const
{
    std::lock_guard<std::mutex> lock(m_tierMutex);

    if (m_tier1Bots.find(botGuid) != m_tier1Bots.end())
        return BotTier::TIER_1_ACTIVE;

    if (m_tier2Bots.find(botGuid) != m_tier2Bots.end())
        return BotTier::TIER_2_AWARE;

    return BotTier::TIER_3_PASSIVE;
}

std::vector<uint64> IntelligenceTierManager::GetTier1Bots() const
{
    std::lock_guard<std::mutex> lock(m_tierMutex);
    return std::vector<uint64>(m_tier1Bots.begin(), m_tier1Bots.end());
}

std::vector<uint64> IntelligenceTierManager::GetTier2Bots() const
{
    std::lock_guard<std::mutex> lock(m_tierMutex);
    return std::vector<uint64>(m_tier2Bots.begin(), m_tier2Bots.end());
}

uint32 IntelligenceTierManager::GetTier1Count() const
{
    std::lock_guard<std::mutex> lock(m_tierMutex);
    return static_cast<uint32>(m_tier1Bots.size());
}

uint32 IntelligenceTierManager::GetTier2Count() const
{
    std::lock_guard<std::mutex> lock(m_tierMutex);
    return static_cast<uint32>(m_tier2Bots.size());
}

uint32 IntelligenceTierManager::GetTier3Count() const
{
    if (!m_memoryManager)
        return 0;

    return m_memoryManager->GetTotalBots() - GetTier1Count() - GetTier2Count();
}

bool IntelligenceTierManager::IsTier1Full() const
{
    std::lock_guard<std::mutex> lock(m_tierMutex);
    return m_tier1Bots.size() >= m_maxTier1;
}

bool IntelligenceTierManager::IsTier2Full() const
{
    std::lock_guard<std::mutex> lock(m_tierMutex);
    return m_tier2Bots.size() >= m_maxTier2;
}

void IntelligenceTierManager::OnPlayerSpoke(uint64 botGuid, uint64 playerGuid)
{
    std::lock_guard<std::mutex> lock(m_interactionMutex);
    m_lastPlayerInteraction[botGuid] = NowMs();

    ALIVE_LOG_TRACE("Player interaction recorded for bot %llu", botGuid);
}

void IntelligenceTierManager::OnCombatStart(uint64 botGuid, uint64 opponentGuid, bool isPlayer)
{
    if (!isPlayer)
        return;

    std::lock_guard<std::mutex> lock(m_interactionMutex);
    m_botsInCombatWithPlayer.insert(botGuid);

    ALIVE_LOG_TRACE("Bot %llu entered combat with player %llu", botGuid, opponentGuid);
}

void IntelligenceTierManager::OnCombatEnd(uint64 botGuid)
{
    std::lock_guard<std::mutex> lock(m_interactionMutex);
    m_botsInCombatWithPlayer.erase(botGuid);

    ALIVE_LOG_TRACE("Bot %llu combat ended", botGuid);
}

void IntelligenceTierManager::OnWorldEventJoin(uint64 botGuid, uint32 eventId)
{
    std::lock_guard<std::mutex> lock(m_interactionMutex);
    m_botsInWorldEvent.insert(botGuid);

    ALIVE_LOG_TRACE("Bot %llu joined world event %u", botGuid, eventId);
}

void IntelligenceTierManager::OnWorldEventLeave(uint64 botGuid, uint32 eventId)
{
    std::lock_guard<std::mutex> lock(m_interactionMutex);
    m_botsInWorldEvent.erase(botGuid);

    ALIVE_LOG_TRACE("Bot %llu left world event %u", botGuid, eventId);
}

std::map<BotTier, uint32> IntelligenceTierManager::GetTierDistribution() const
{
    std::map<BotTier, uint32> distribution;
    distribution[BotTier::TIER_1_ACTIVE] = GetTier1Count();
    distribution[BotTier::TIER_2_AWARE] = GetTier2Count();
    distribution[BotTier::TIER_3_PASSIVE] = GetTier3Count();
    return distribution;
}

void IntelligenceTierManager::Update(uint32 diff)
{
    m_updateTimer += diff;
    if (m_updateTimer >= m_tierUpdateIntervalMs)
    {
        m_updateTimer = 0;
        UpdateAllTiers();
    }
}

// =============================================================================
// RebalanceTiers - Incremental tier adjustment
// =============================================================================
// This is a lighter-weight operation than UpdateAllTiers() designed to handle
// immediate tier changes based on triggering events (combat start, player
// interaction, world event join). It only moves bots that have had significant
// criteria changes rather than recalculating all bots.
//
// The algorithm works as follows:
// 1. Check bots in combat with players - these get immediate Tier 1 promotion
// 2. Check bots recently spoken to by players - promote to Tier 1 if room
// 3. Check bots in world events - promote to Tier 1/2
// 4. Demote Tier 1 bots that no longer meet criteria (not near players)
// 5. Demote Tier 2 bots that are no longer in player zones
//
// This is called after specific events trigger, not on a timer like UpdateAllTiers.
// =============================================================================
void IntelligenceTierManager::RebalanceTiers()
{
    ALIVE_LOG_TRACE("Rebalancing tiers (incremental)...");

#ifdef MOD_PLAYERBOTS
    uint64 currentTime = NowMs();

    // =========================================================================
    // Phase 1: Promote bots that need immediate Tier 1 access
    // =========================================================================

    // Collect bots in combat with players - they need Tier 1 immediately
    std::vector<uint64> combatPromotions;
    {
        std::lock_guard<std::mutex> lock(m_interactionMutex);
        for (uint64 botGuid : m_botsInCombatWithPlayer)
        {
            if (GetBotTier(botGuid) != BotTier::TIER_1_ACTIVE)
                combatPromotions.push_back(botGuid);
        }
    }

    // Promote combat bots to Tier 1 (they get priority - demote least active)
    for (uint64 botGuid : combatPromotions)
    {
        if (!IsTier1Full())
        {
            PromoteToTier1(botGuid);
        }
        else
        {
            // Find a Tier 1 bot not in combat to demote
            uint64 demoteCandidate = 0;
            float lowestScore = FLT_MAX;

            std::vector<uint64> tier1List = GetTier1Bots();
            for (uint64 existingGuid : tier1List)
            {
                // Skip bots in combat - they keep priority
                {
                    std::lock_guard<std::mutex> lock(m_interactionMutex);
                    if (m_botsInCombatWithPlayer.find(existingGuid) != m_botsInCombatWithPlayer.end())
                        continue;
                }

                // Get cached criteria score
                float score = 0.0f;
                {
                    std::lock_guard<std::mutex> lock(m_criteriaMutex);
                    auto it = m_criteriaCache.find(existingGuid);
                    if (it != m_criteriaCache.end())
                        score = CalculateTierScore(it->second);
                }

                if (score < lowestScore)
                {
                    lowestScore = score;
                    demoteCandidate = existingGuid;
                }
            }

            if (demoteCandidate != 0)
            {
                DemoteFromTier1(demoteCandidate);
                PromoteToTier1(botGuid);
            }
            // else: all Tier 1 bots are in combat, keep as is
        }
    }

    // =========================================================================
    // Phase 2: Check recently interacted bots for promotion
    // =========================================================================
    std::vector<uint64> recentInteractions;
    {
        std::lock_guard<std::mutex> lock(m_interactionMutex);
        for (const auto& pair : m_lastPlayerInteraction)
        {
            uint64 timeSince = currentTime - pair.second;
            if (timeSince < RECENT_INTERACTION_THRESHOLD_MS)
            {
                if (GetBotTier(pair.first) == BotTier::TIER_3_PASSIVE)
                    recentInteractions.push_back(pair.first);
            }
        }
    }

    // Promote recently-interacted bots to at least Tier 2
    for (uint64 botGuid : recentInteractions)
    {
        if (!IsTier2Full())
        {
            PromoteToTier2(botGuid);
        }
    }

    // =========================================================================
    // Phase 3: Check world event participants
    // =========================================================================
    std::vector<uint64> eventParticipants;
    {
        std::lock_guard<std::mutex> lock(m_interactionMutex);
        for (uint64 botGuid : m_botsInWorldEvent)
        {
            BotTier currentTier = GetBotTier(botGuid);
            if (currentTier == BotTier::TIER_3_PASSIVE)
                eventParticipants.push_back(botGuid);
        }
    }

    // World event participants go to Tier 2 at minimum
    for (uint64 botGuid : eventParticipants)
    {
        if (!IsTier2Full())
        {
            PromoteToTier2(botGuid);
        }
    }

    // =========================================================================
    // Phase 4: Demote Tier 1 bots that no longer meet criteria
    // =========================================================================
    std::vector<uint64> tier1Demotions;
    {
        std::lock_guard<std::mutex> tierLock(m_tierMutex);
        std::lock_guard<std::mutex> interactionLock(m_interactionMutex);

        for (uint64 botGuid : m_tier1Bots)
        {
            // Skip if in combat
            if (m_botsInCombatWithPlayer.find(botGuid) != m_botsInCombatWithPlayer.end())
                continue;

            // Check if recently interacted
            auto interactionIt = m_lastPlayerInteraction.find(botGuid);
            bool recentlyInteracted = interactionIt != m_lastPlayerInteraction.end() &&
                (currentTime - interactionIt->second) < RECENT_INTERACTION_THRESHOLD_MS;
            if (recentlyInteracted)
                continue;

            // Check cached criteria - if score dropped below Tier 1 threshold
            {
                std::lock_guard<std::mutex> criteriaLock(m_criteriaMutex);
                auto it = m_criteriaCache.find(botGuid);
                if (it != m_criteriaCache.end())
                {
                    float score = CalculateTierScore(it->second);
                    // Score < 50 means no longer Tier 1 eligible
                    if (score < 40.0f)  // Use hysteresis (40 instead of 50) to prevent thrashing
                    {
                        tier1Demotions.push_back(botGuid);
                    }
                }
            }
        }
    }

    for (uint64 botGuid : tier1Demotions)
    {
        DemoteFromTier1(botGuid);
    }

    // =========================================================================
    // Phase 5: Clean up stale interactions
    // =========================================================================
    {
        std::lock_guard<std::mutex> lock(m_interactionMutex);

        // Remove very old interactions (2x threshold)
        auto it = m_lastPlayerInteraction.begin();
        while (it != m_lastPlayerInteraction.end())
        {
            if (currentTime - it->second > RECENT_INTERACTION_THRESHOLD_MS * 2)
                it = m_lastPlayerInteraction.erase(it);
            else
                ++it;
        }
    }

    ALIVE_LOG_TRACE("Rebalance complete: promoted %zu combat bots, %zu recent interactions, %zu event bots; demoted %zu",
        combatPromotions.size(), recentInteractions.size(), eventParticipants.size(), tier1Demotions.size());

#endif
}

} // namespace AliveWorld
