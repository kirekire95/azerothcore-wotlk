/*
 * mod-alive-world - World State Manager Implementation
 * Copyright (C) 2025 EC
 */

#include "WorldStateManager.h"
#include "AliveWorldBridgeAdapter.h"
#include "AliveWorldManager.h"
#include "BotMemoryManager.h"
#include "Config.h"
#include "DatabaseEnv.h"
#include "EventCollector.h"
#include "GossipTemplateManager.h"
#include "Map.h"
#include "MapMgr.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "World.h"
#include "WorldSession.h"
#include "WorldSessionMgr.h"
#include "GameTime.h"
#include "Timer.h"

#ifdef MOD_PLAYERBOTS
#include "PlayerbotAI.h"
#include "RandomPlayerbotMgr.h"
#endif

namespace AliveWorld
{

WorldStateManager::WorldStateManager()
    : m_enabled(true)
    , m_updateIntervalMs(30000)
    , m_hotZoneThreshold(3)
    , m_maxGossipPoolSize(50)
    , m_gossipTTLSeconds(3600)
    , m_gossipSpreadChance(30)
    , m_gossipSpreadDistance(50.0f)
    , m_minGossipImportance(3)
    , m_gossipPersistEnabled(true)
    , m_gossipPersistIntervalMs(DEFAULT_GOSSIP_PERSIST_INTERVAL_MS)
    , m_gossipPersistBatchSize(DEFAULT_GOSSIP_PERSIST_BATCH_SIZE)
    , m_nextGossipId(1)
    , m_eventCollector(nullptr)
    , m_lastProcessedEventId(0)
    , m_templateManager(nullptr)
    , m_memoryManager(nullptr)
    , m_zoneUpdateTimer(0)
    , m_gossipPropagateTimer(0)
    , m_gossipCleanupTimer(0)
    , m_gossipPersistTimer(0)
    , m_lastTrackedHour(255)  // Invalid hour to force initial update
{
}

WorldStateManager::~WorldStateManager()
{
    // Flush any pending gossip to database before shutdown
    if (m_gossipPersistEnabled)
    {
        PersistGossipToDatabase();
    }

    std::unique_lock<std::shared_mutex> zoneLock(m_zoneMutex);
    m_zoneStates.clear();

    std::unique_lock<std::shared_mutex> gossipLock(m_gossipMutex);
    m_gossipPool.clear();
}

void WorldStateManager::Initialize(EventCollector* eventCollector,
                                    GossipTemplateManager* templateManager)
{
    ALIVE_LOG_INFO("World State Manager initializing...");

    m_eventCollector = eventCollector;
    m_templateManager = templateManager;
    LoadConfig();

    if (m_enabled)
    {
        ALIVE_LOG_INFO("World State Manager enabled");
        ALIVE_LOG_INFO("  Update Interval: %u ms", m_updateIntervalMs);
        ALIVE_LOG_INFO("  Hot Zone Threshold: %u", m_hotZoneThreshold);
        ALIVE_LOG_INFO("  Max Gossip Pool: %u", m_maxGossipPoolSize);
        ALIVE_LOG_INFO("  Gossip TTL: %u seconds", m_gossipTTLSeconds);
        ALIVE_LOG_INFO("  Gossip Persist: %s", m_gossipPersistEnabled ? "ON" : "OFF");
        if (m_templateManager && m_templateManager->IsLoaded())
        {
            ALIVE_LOG_INFO("  Gossip Templates: %u loaded", m_templateManager->GetTemplateCount());
        }
        else
        {
            ALIVE_LOG_INFO("  Gossip Templates: Using hardcoded defaults");
        }

        // Load gossip from database if persistence is enabled
        if (m_gossipPersistEnabled)
        {
            LoadGossipFromDatabase();
        }

        // Initial zone state update
        UpdateZoneStates();
    }
    else
    {
        ALIVE_LOG_WARN("World State Manager disabled in config");
    }
}

void WorldStateManager::LoadConfig()
{
    m_enabled = sConfigMgr->GetOption<bool>("AliveWorld.WorldState.Enable", true);
    m_updateIntervalMs = sConfigMgr->GetOption<uint32>("AliveWorld.WorldState.UpdateIntervalMs", 30000);
    m_hotZoneThreshold = sConfigMgr->GetOption<uint8>("AliveWorld.WorldState.HotZoneThreshold", 3);
    m_maxGossipPoolSize = sConfigMgr->GetOption<uint32>("AliveWorld.Gossip.MaxPoolSize", 50);
    m_gossipTTLSeconds = sConfigMgr->GetOption<uint32>("AliveWorld.Gossip.ItemTTLSeconds", 3600);
    m_gossipSpreadChance = sConfigMgr->GetOption<uint32>("AliveWorld.Gossip.SpreadChance", 30);
    m_gossipSpreadDistance = sConfigMgr->GetOption<float>("AliveWorld.Gossip.SpreadDistance", 50.0f);
    m_minGossipImportance = sConfigMgr->GetOption<uint8>("AliveWorld.Gossip.MinImportance", 3);

    // Gossip persistence settings
    m_gossipPersistEnabled = sConfigMgr->GetOption<bool>("AliveWorld.Gossip.Persist.Enable", true);
    m_gossipPersistIntervalMs = sConfigMgr->GetOption<uint32>("AliveWorld.Gossip.Persist.IntervalMs", DEFAULT_GOSSIP_PERSIST_INTERVAL_MS);
    m_gossipPersistBatchSize = sConfigMgr->GetOption<uint32>("AliveWorld.Gossip.Persist.BatchSize", DEFAULT_GOSSIP_PERSIST_BATCH_SIZE);
}

void WorldStateManager::UpdateZoneStates()
{
    std::unique_lock<std::shared_mutex> lock(m_zoneMutex);
    std::unique_lock<std::shared_mutex> trackLock(m_trackingMutex);

    // Clear tracking data
    m_playersInZone.clear();
    m_botsInZone.clear();

    uint64 now = NowMs();

    // Iterate all online sessions
    WorldSessionMgr::SessionMap const& sessions = sWorldSessionMgr->GetAllSessions();
    for (auto const& pair : sessions)
    {
        WorldSession* session = pair.second;
        if (!session)
            continue;

        Player* player = session->GetPlayer();
        if (!player || !player->IsInWorld())
            continue;

        uint32 zoneId = player->GetZoneId();
        uint32 mapId = player->GetMapId();
        bool isAlliance = (player->GetTeamId() == TEAM_ALLIANCE);

        // Check if this is a bot
        bool isBot = false;
#ifdef MOD_PLAYERBOTS
        isBot = (sPlayerbotsMgr.GetPlayerbotAI(player) != nullptr) ||
                sRandomPlayerbotMgr.IsRandomBot(player);
#endif

        // Initialize zone state if new
        if (m_zoneStates.find(zoneId) == m_zoneStates.end())
        {
            ZoneState newState;
            newState.zoneId = zoneId;
            newState.mapId = mapId;
            m_zoneStates[zoneId] = newState;
        }

        ZoneState& state = m_zoneStates[zoneId];
        state.mapId = mapId;
        state.lastUpdateTime = now;

        if (isBot)
        {
            if (isAlliance)
                state.allianceBotCount++;
            else
                state.hordeBotCount++;

            m_botsInZone[zoneId].insert(player->GetGUID().GetRawValue());
        }
        else
        {
            if (isAlliance)
                state.alliancePlayerCount++;
            else
                state.hordePlayerCount++;

            m_playersInZone[zoneId].insert(player->GetGUID().GetRawValue());
        }
    }

    // Recalculate metrics for each zone and emit updates
    for (auto& pair : m_zoneStates)
    {
        RecalculateZoneMetrics(pair.first);

        // Emit zone state update for zones with population
        const ZoneState& state = pair.second;
        if (state.GetTotalPopulation() > 0)
        {
            sAliveWorldBridge->EmitZoneStateUpdated(
                state.zoneId, state.mapId,
                state.alliancePlayerCount, state.hordePlayerCount,
                state.allianceBotCount, state.hordeBotCount,
                state.dangerLevel, state.activityLevel);
        }
    }

    ALIVE_LOG_TRACE("Zone states updated: %zu zones tracked", m_zoneStates.size());
}

void WorldStateManager::RecalculateZoneMetrics(uint32 zoneId)
{
    auto it = m_zoneStates.find(zoneId);
    if (it == m_zoneStates.end())
        return;

    ZoneState& state = it->second;

    // Activity level based on total population
    uint32 totalPop = state.GetTotalPopulation();
    state.activityLevel = std::min<uint8>(10, totalPop / 5);  // 50 chars = max activity

    // Danger level based on PvP potential (both factions present)
    uint32 allianceTotal = state.alliancePlayerCount + state.allianceBotCount;
    uint32 hordeTotal = state.hordePlayerCount + state.hordeBotCount;

    if (allianceTotal > 0 && hordeTotal > 0)
    {
        // PvP potential - both factions present
        uint32 minFaction = std::min(allianceTotal, hordeTotal);
        state.dangerLevel = std::min<uint8>(10, 3 + (minFaction * 2));
    }
    else
    {
        state.dangerLevel = 0;
    }

    // Get recent events for this zone
    if (m_eventCollector)
    {
        auto events = m_eventCollector->GetEventsByZone(zoneId, 10);
        state.recentEventIds.clear();
        for (const auto& event : events)
        {
            state.recentEventIds.push_back(event.eventId);
        }
    }
}

ZoneState WorldStateManager::GetZoneState(uint32 zoneId) const
{
    std::shared_lock<std::shared_mutex> lock(m_zoneMutex);

    auto it = m_zoneStates.find(zoneId);
    if (it != m_zoneStates.end())
        return it->second;

    return ZoneState();
}

std::vector<ZoneState> WorldStateManager::GetAllZoneStates() const
{
    std::shared_lock<std::shared_mutex> lock(m_zoneMutex);

    std::vector<ZoneState> result;
    result.reserve(m_zoneStates.size());
    for (const auto& pair : m_zoneStates)
    {
        result.push_back(pair.second);
    }
    return result;
}

std::vector<ZoneState> WorldStateManager::GetHotZones() const
{
    std::shared_lock<std::shared_mutex> lock(m_zoneMutex);

    std::vector<ZoneState> result;
    for (const auto& pair : m_zoneStates)
    {
        if (pair.second.IsHotZone(m_hotZoneThreshold))
            result.push_back(pair.second);
    }
    return result;
}

std::vector<ZoneState> WorldStateManager::GetZonesWithPlayers() const
{
    std::shared_lock<std::shared_mutex> lock(m_zoneMutex);

    std::vector<ZoneState> result;
    for (const auto& pair : m_zoneStates)
    {
        if (pair.second.GetTotalPlayers() > 0)
            result.push_back(pair.second);
    }
    return result;
}

bool WorldStateManager::IsHotZone(uint32 zoneId) const
{
    std::shared_lock<std::shared_mutex> lock(m_zoneMutex);

    auto it = m_zoneStates.find(zoneId);
    return it != m_zoneStates.end() && it->second.IsHotZone(m_hotZoneThreshold);
}

uint8 WorldStateManager::GetZoneDangerLevel(uint32 zoneId) const
{
    std::shared_lock<std::shared_mutex> lock(m_zoneMutex);

    auto it = m_zoneStates.find(zoneId);
    return it != m_zoneStates.end() ? it->second.dangerLevel : 0;
}

uint8 WorldStateManager::GetZoneActivityLevel(uint32 zoneId) const
{
    std::shared_lock<std::shared_mutex> lock(m_zoneMutex);

    auto it = m_zoneStates.find(zoneId);
    return it != m_zoneStates.end() ? it->second.activityLevel : 0;
}

void WorldStateManager::OnPlayerEnterZone(uint64 guid, uint32 zoneId, bool isAlliance, bool isBot)
{
    std::unique_lock<std::shared_mutex> trackLock(m_trackingMutex);
    std::unique_lock<std::shared_mutex> zoneLock(m_zoneMutex);

    // Track the player/bot
    if (isBot)
        m_botsInZone[zoneId].insert(guid);
    else
        m_playersInZone[zoneId].insert(guid);

    // Update zone state
    if (m_zoneStates.find(zoneId) == m_zoneStates.end())
    {
        ZoneState newState;
        newState.zoneId = zoneId;
        newState.lastUpdateTime = NowMs();
        m_zoneStates[zoneId] = newState;
    }

    ZoneState& state = m_zoneStates[zoneId];
    if (isBot)
    {
        if (isAlliance) state.allianceBotCount++;
        else state.hordeBotCount++;
    }
    else
    {
        if (isAlliance) state.alliancePlayerCount++;
        else state.hordePlayerCount++;
    }

    RecalculateZoneMetrics(zoneId);
}

void WorldStateManager::OnPlayerLeaveZone(uint64 guid, uint32 zoneId, bool isAlliance, bool isBot)
{
    std::unique_lock<std::shared_mutex> trackLock(m_trackingMutex);
    std::unique_lock<std::shared_mutex> zoneLock(m_zoneMutex);

    // Remove from tracking
    if (isBot)
        m_botsInZone[zoneId].erase(guid);
    else
        m_playersInZone[zoneId].erase(guid);

    // Update zone state
    auto it = m_zoneStates.find(zoneId);
    if (it != m_zoneStates.end())
    {
        ZoneState& state = it->second;
        if (isBot)
        {
            if (isAlliance && state.allianceBotCount > 0) state.allianceBotCount--;
            else if (!isAlliance && state.hordeBotCount > 0) state.hordeBotCount--;
        }
        else
        {
            if (isAlliance && state.alliancePlayerCount > 0) state.alliancePlayerCount--;
            else if (!isAlliance && state.hordePlayerCount > 0) state.hordePlayerCount--;
        }

        RecalculateZoneMetrics(zoneId);
    }
}

uint32 WorldStateManager::GetPlayersInZone(uint32 zoneId, bool includeBots) const
{
    std::shared_lock<std::shared_mutex> lock(m_zoneMutex);

    auto it = m_zoneStates.find(zoneId);
    if (it == m_zoneStates.end())
        return 0;

    if (includeBots)
        return it->second.GetTotalPopulation();
    else
        return it->second.GetTotalPlayers();
}

uint32 WorldStateManager::GetBotsInZone(uint32 zoneId) const
{
    std::shared_lock<std::shared_mutex> lock(m_zoneMutex);

    auto it = m_zoneStates.find(zoneId);
    return it != m_zoneStates.end() ? it->second.GetTotalBots() : 0;
}

// =========================================================================
// GOSSIP SYSTEM
// =========================================================================

void WorldStateManager::AddGossip(const GossipItem& gossip)
{
    GossipItem newGossip = gossip;
    newGossip.gossipId = m_nextGossipId++;
    newGossip.timestamp = NowMs();
    newGossip.expiresAt = newGossip.timestamp + (m_gossipTTLSeconds * 1000);

    {
        std::unique_lock<std::shared_mutex> lock(m_gossipMutex);

        // Remove oldest if pool is full
        while (m_gossipPool.size() >= m_maxGossipPoolSize && !m_gossipPool.empty())
        {
            m_gossipIndex.erase(m_gossipPool.front().gossipId);
            m_gossipPool.pop_front();
        }

        // Add new gossip to pool
        m_gossipPool.push_back(newGossip);
        m_gossipIndex[newGossip.gossipId] = m_gossipPool.size() - 1;
    }

    // Queue for database persistence (separate lock)
    if (m_gossipPersistEnabled)
    {
        std::lock_guard<std::mutex> persistLock(m_gossipPersistMutex);
        m_gossipPersistQueue.push(newGossip);
    }

    ALIVE_LOG_DEBUG("Gossip added [%u]: %s (Importance: %u)",
        newGossip.gossipId, newGossip.headline.c_str(), newGossip.importance);
}

GossipItem WorldStateManager::CreateGossipFromEvent(const WorldEvent& event)
{
    GossipItem gossip;
    gossip.headline = GenerateHeadline(event);
    gossip.details = GenerateDetails(event);
    gossip.originZoneId = event.zoneId;
    gossip.importance = event.importance;
    gossip.spreadRadius = 0;
    gossip.sourceEventId = event.eventId;

    return gossip;
}

std::string WorldStateManager::GenerateHeadline(const WorldEvent& event)
{
    // Use template manager if available
    if (m_templateManager && m_templateManager->IsLoaded())
    {
        std::string headline = m_templateManager->GenerateHeadline(event);
        if (!headline.empty())
            return headline;
    }

    // Fallback to hardcoded templates
    switch (event.type)
    {
        case WorldEventType::BOSS_KILLED:
            return event.secondaryName + " has been defeated!";

        case WorldEventType::PLAYER_KILLED_PLAYER:
            return event.primaryName + " slew " + event.secondaryName + " in battle!";

        case WorldEventType::RARE_SPAWN:
            return "Rare creature spotted: " + event.primaryName + "!";

        case WorldEventType::WORLD_BOSS_SPAWN:
            return "World boss " + event.primaryName + " has appeared!";

        case WorldEventType::GUARD_CALLED:
            return "Guards pursue " + event.primaryName + "!";

        case WorldEventType::ZONE_BECAME_ACTIVE:
            return "Activity reported in the area!";

        default:
            return event.details.length() > 50 ?
                event.details.substr(0, 50) + "..." : event.details;
    }
}

std::string WorldStateManager::GenerateDetails(const WorldEvent& event)
{
    return event.details;
}

std::vector<GossipItem> WorldStateManager::GetGossipForBot(uint64 botGuid, uint32 maxCount) const
{
    std::shared_lock<std::shared_mutex> lock(m_gossipMutex);

    std::vector<GossipItem> result;
    for (const auto& gossip : m_gossipPool)
    {
        // Skip if bot already knows this gossip
        if (gossip.knownByBots.find(botGuid) != gossip.knownByBots.end())
            continue;

        result.push_back(gossip);
        if (result.size() >= maxCount)
            break;
    }

    return result;
}

std::vector<GossipItem> WorldStateManager::GetRecentGossip(uint32 count) const
{
    std::shared_lock<std::shared_mutex> lock(m_gossipMutex);

    std::vector<GossipItem> result;
    uint32 toGet = std::min<uint32>(count, static_cast<uint32>(m_gossipPool.size()));

    for (auto it = m_gossipPool.rbegin(); it != m_gossipPool.rend() && result.size() < toGet; ++it)
    {
        result.push_back(*it);
    }

    return result;
}

std::vector<GossipItem> WorldStateManager::GetGossipByZone(uint32 zoneId) const
{
    std::shared_lock<std::shared_mutex> lock(m_gossipMutex);

    std::vector<GossipItem> result;
    for (const auto& gossip : m_gossipPool)
    {
        if (gossip.originZoneId == zoneId)
            result.push_back(gossip);
    }

    return result;
}

void WorldStateManager::MarkGossipKnown(uint32 gossipId, uint64 botGuid)
{
    std::unique_lock<std::shared_mutex> lock(m_gossipMutex);

    auto it = m_gossipIndex.find(gossipId);
    if (it != m_gossipIndex.end() && it->second < m_gossipPool.size())
    {
        m_gossipPool[it->second].knownByBots.insert(botGuid);
    }
}

void WorldStateManager::SpreadGossip(uint64 sourceBotGuid, uint64 targetBotGuid, uint32 gossipId)
{
    std::unique_lock<std::shared_mutex> lock(m_gossipMutex);

    auto it = m_gossipIndex.find(gossipId);
    if (it == m_gossipIndex.end() || it->second >= m_gossipPool.size())
        return;

    GossipItem& gossip = m_gossipPool[it->second];

    // Source must know it
    if (gossip.knownByBots.find(sourceBotGuid) == gossip.knownByBots.end())
        return;

    // Target learns it
    gossip.knownByBots.insert(targetBotGuid);
    gossip.spreadRadius++;

    ALIVE_LOG_TRACE("Gossip [%u] spread from %llu to %llu (radius: %u)",
        gossipId, sourceBotGuid, targetBotGuid, gossip.spreadRadius);
}

void WorldStateManager::PropagateGossip()
{
    // This is called periodically to simulate natural gossip spreading
    // Bots near each other have a chance to share gossip based on personality

    // Get all bots that are near real players (they're more likely to share)
    std::vector<ZoneState> activeZones = GetZonesWithPlayers();

    uint32 totalSpreads = 0;

    for (const auto& zone : activeZones)
    {
        std::shared_lock<std::shared_mutex> trackLock(m_trackingMutex);

        auto botIt = m_botsInZone.find(zone.zoneId);
        if (botIt == m_botsInZone.end() || botIt->second.size() < 2)
            continue;

        // Convert set to vector for random access
        std::vector<uint64> botsInZone(botIt->second.begin(), botIt->second.end());
        trackLock.unlock();

        // Each bot in the zone has a chance to spread gossip
        for (size_t i = 0; i < botsInZone.size() && i < 20; ++i)
        {
            uint64 sourceBot = botsInZone[i];

            // Get personality for spread chance calculation
            float sociability = 0.5f;  // Default
            if (m_memoryManager)
            {
                BotPersonality& personality = m_memoryManager->GetPersonality(sourceBot);
                sociability = personality.sociability;
            }

            // Spread chance = base chance * (0.5 + sociability)
            // Sociability 0.0 = 50% of base, 1.0 = 150% of base
            float adjustedChance = m_gossipSpreadChance * (0.5f + sociability);
            if (rand() % 100 >= static_cast<int>(adjustedChance))
                continue;

            // Get nearby bots (within gossip spread distance)
            std::vector<uint64> nearbyBots = GetNearbyBots(sourceBot, m_gossipSpreadDistance);
            if (nearbyBots.empty())
                continue;

            // Pick a random nearby bot
            uint64 targetBot = nearbyBots[rand() % nearbyBots.size()];

            // Find gossip to spread - prioritize higher importance
            std::shared_lock<std::shared_mutex> gossipLock(m_gossipMutex);

            // Sort gossip by importance (descending) for this iteration
            std::vector<const GossipItem*> spreadableGossip;
            for (const auto& gossip : m_gossipPool)
            {
                if (gossip.knownByBots.find(sourceBot) != gossip.knownByBots.end() &&
                    gossip.knownByBots.find(targetBot) == gossip.knownByBots.end())
                {
                    spreadableGossip.push_back(&gossip);
                }
            }

            if (spreadableGossip.empty())
                continue;

            // Select gossip - higher importance = higher chance of being selected
            // Simple weighted selection: importance acts as weight
            uint32 totalWeight = 0;
            for (const auto* g : spreadableGossip)
                totalWeight += g->importance;

            if (totalWeight == 0)
                continue;

            uint32 roll = rand() % totalWeight;
            uint32 cumulative = 0;
            const GossipItem* selected = spreadableGossip[0];

            for (const auto* g : spreadableGossip)
            {
                cumulative += g->importance;
                if (roll < cumulative)
                {
                    selected = g;
                    break;
                }
            }

            uint32 gossipId = selected->gossipId;
            gossipLock.unlock();

            SpreadGossip(sourceBot, targetBot, gossipId);
            totalSpreads++;

            // Limit spreads per cycle to prevent performance issues
            if (totalSpreads >= 50)
            {
                ALIVE_LOG_DEBUG("Gossip propagation limit reached (%u spreads)", totalSpreads);
                return;
            }
        }
    }

    if (totalSpreads > 0)
    {
        ALIVE_LOG_DEBUG("Gossip propagation: %u items spread across %zu zones",
            totalSpreads, activeZones.size());
    }
}

void WorldStateManager::CleanupExpiredGossip()
{
    std::unique_lock<std::shared_mutex> lock(m_gossipMutex);

    uint64 now = NowMs();
    uint32 removed = 0;

    while (!m_gossipPool.empty() && m_gossipPool.front().expiresAt < now)
    {
        m_gossipIndex.erase(m_gossipPool.front().gossipId);
        m_gossipPool.pop_front();
        removed++;
    }

    // Rebuild index
    m_gossipIndex.clear();
    for (size_t i = 0; i < m_gossipPool.size(); ++i)
    {
        m_gossipIndex[m_gossipPool[i].gossipId] = i;
    }

    if (removed > 0)
    {
        ALIVE_LOG_DEBUG("Cleaned up %u expired gossip items", removed);
    }
}

uint32 WorldStateManager::GetGossipPoolSize() const
{
    std::shared_lock<std::shared_mutex> lock(m_gossipMutex);
    return static_cast<uint32>(m_gossipPool.size());
}

// =========================================================================
// SOCIAL GRAPH
// =========================================================================

void WorldStateManager::RecordSocialConnection(uint64 botGuid, uint64 otherGuid, const std::string& type)
{
    std::unique_lock<std::shared_mutex> lock(m_socialMutex);

    m_socialGraph[botGuid].insert(otherGuid);
    m_socialGraph[otherGuid].insert(botGuid);  // Bidirectional

    ALIVE_LOG_TRACE("Social connection: %llu <-> %llu (type: %s)",
        botGuid, otherGuid, type.c_str());
}

std::set<uint64> WorldStateManager::GetBotConnections(uint64 botGuid) const
{
    std::shared_lock<std::shared_mutex> lock(m_socialMutex);

    auto it = m_socialGraph.find(botGuid);
    return it != m_socialGraph.end() ? it->second : std::set<uint64>();
}

std::vector<uint64> WorldStateManager::GetNearbyBots(uint64 botGuid, float distance) const
{
    std::vector<uint64> result;

    Player* bot = ObjectAccessor::FindPlayer(ObjectGuid::Create<HighGuid::Player>(botGuid));
    if (!bot || !bot->IsInWorld())
        return result;

    uint32 zoneId = bot->GetZoneId();

    std::shared_lock<std::shared_mutex> lock(m_trackingMutex);
    auto it = m_botsInZone.find(zoneId);
    if (it == m_botsInZone.end())
        return result;

    for (uint64 otherGuid : it->second)
    {
        if (otherGuid == botGuid)
            continue;

        Player* other = ObjectAccessor::FindPlayer(ObjectGuid::Create<HighGuid::Player>(otherGuid));
        if (!other || !other->IsInWorld())
            continue;

        if (bot->GetDistance(other) <= distance)
            result.push_back(otherGuid);
    }

    return result;
}

bool WorldStateManager::AreSociallyConnected(uint64 guid1, uint64 guid2) const
{
    std::shared_lock<std::shared_mutex> lock(m_socialMutex);

    auto it = m_socialGraph.find(guid1);
    return it != m_socialGraph.end() && it->second.find(guid2) != it->second.end();
}

// =========================================================================
// EVENT INTEGRATION
// =========================================================================

void WorldStateManager::ProcessNewEvents()
{
    if (!m_eventCollector)
        return;

    auto events = m_eventCollector->GetUnprocessedEvents();
    for (const auto& event : events)
    {
        if (event.eventId <= m_lastProcessedEventId)
            continue;

        // Create gossip from high-importance events
        if (event.importance >= m_minGossipImportance)
        {
            GossipItem gossip = CreateGossipFromEvent(event);
            AddGossip(gossip);
        }

        m_lastProcessedEventId = event.eventId;
        m_eventCollector->MarkEventProcessed(event.eventId);
    }
}

// =========================================================================
// UPDATE LOOP
// =========================================================================

void WorldStateManager::Update(uint32 diff)
{
    if (!m_enabled)
        return;

    // Zone state update
    m_zoneUpdateTimer += diff;
    if (m_zoneUpdateTimer >= m_updateIntervalMs)
    {
        m_zoneUpdateTimer = 0;
        UpdateZoneStates();
    }

    // Process new events
    ProcessNewEvents();

    // Gossip propagation
    m_gossipPropagateTimer += diff;
    if (m_gossipPropagateTimer >= GOSSIP_PROPAGATE_INTERVAL_MS)
    {
        m_gossipPropagateTimer = 0;
        PropagateGossip();
    }

    // Gossip cleanup
    m_gossipCleanupTimer += diff;
    if (m_gossipCleanupTimer >= GOSSIP_CLEANUP_INTERVAL_MS)
    {
        m_gossipCleanupTimer = 0;
        CleanupExpiredGossip();
    }

    // Gossip persistence
    if (m_gossipPersistEnabled)
    {
        m_gossipPersistTimer += diff;
        if (m_gossipPersistTimer >= m_gossipPersistIntervalMs)
        {
            m_gossipPersistTimer = 0;
            PersistGossipToDatabase();
        }
    }
}

// =========================================================================
// STATS
// =========================================================================

APIStats WorldStateManager::GetStats() const
{
    APIStats stats;

    {
        std::shared_lock<std::shared_mutex> lock(m_zoneMutex);
        stats.activeZones = 0;
        for (const auto& pair : m_zoneStates)
        {
            if (pair.second.GetTotalPopulation() > 0)
                stats.activeZones++;
        }
    }

    {
        std::shared_lock<std::shared_mutex> lock(m_gossipMutex);
        stats.gossipPoolSize = static_cast<uint32>(m_gossipPool.size());
    }

    if (m_eventCollector)
    {
        stats.eventBufferSize = m_eventCollector->GetEventCount();
    }

    return stats;
}

// =========================================================================
// GOSSIP PERSISTENCE
// =========================================================================

void WorldStateManager::LoadGossipFromDatabase()
{
    if (!m_gossipPersistEnabled)
        return;

    ALIVE_LOG_INFO("Loading gossip from database...");

    uint64 now = NowMs();

    // Load active gossip that hasn't expired
    QueryResult result = CharacterDatabase.Query(
        "SELECT gossip_id, gossip_type, headline, details, source_event_id, "
        "origin_zone_id, origin_bot_guid, subject_name, importance, "
        "spread_count, spread_radius, is_active, "
        "UNIX_TIMESTAMP(created_at) * 1000 as timestamp, "
        "UNIX_TIMESTAMP(expires_at) * 1000 as expires_at "
        "FROM alive_world_gossip "
        "WHERE is_active = 1 AND expires_at > NOW() "
        "ORDER BY created_at ASC "
        "LIMIT {}",
        m_maxGossipPoolSize);

    if (!result)
    {
        ALIVE_LOG_INFO("No active gossip in database");
        return;
    }

    std::unique_lock<std::shared_mutex> lock(m_gossipMutex);
    uint32 count = 0;

    do
    {
        Field* fields = result->Fetch();

        GossipItem gossip;
        gossip.gossipId = fields[0].Get<uint32>();
        // gossip_type is stored but we reconstruct from headline/details
        gossip.headline = fields[2].Get<std::string>();
        gossip.details = fields[3].Get<std::string>();
        gossip.sourceEventId = fields[4].Get<uint32>();
        gossip.originZoneId = fields[5].Get<uint32>();
        // origin_bot_guid (fields[6]) - not used directly in GossipItem
        // subject_name (fields[7]) - not used directly in GossipItem
        gossip.importance = fields[8].Get<uint8>();
        gossip.spreadRadius = fields[9].Get<uint8>();  // Using spread_count as spreadRadius
        // spread_radius from DB (fields[10])
        // is_active (fields[11])
        gossip.timestamp = fields[12].Get<uint64>();
        gossip.expiresAt = fields[13].Get<uint64>();

        m_gossipPool.push_back(gossip);
        m_gossipIndex[gossip.gossipId] = m_gossipPool.size() - 1;

        // Track highest gossip ID
        if (gossip.gossipId >= m_nextGossipId)
            m_nextGossipId = gossip.gossipId + 1;

        count++;
    }
    while (result->NextRow());

    ALIVE_LOG_INFO("Loaded %u gossip items from database, next gossip ID: %u", count, m_nextGossipId.load());
}

void WorldStateManager::PersistGossipToDatabase()
{
    if (!m_gossipPersistEnabled)
        return;

    std::vector<GossipItem> gossipToPersist;

    // Grab batch from queue
    {
        std::lock_guard<std::mutex> lock(m_gossipPersistMutex);
        while (!m_gossipPersistQueue.empty() && gossipToPersist.size() < m_gossipPersistBatchSize)
        {
            gossipToPersist.push_back(m_gossipPersistQueue.front());
            m_gossipPersistQueue.pop();
        }
    }

    if (gossipToPersist.empty())
        return;

    // Build batch insert
    CharacterDatabaseTransaction trans = CharacterDatabase.BeginTransaction();

    for (const auto& gossip : gossipToPersist)
    {
        // Convert timestamp to MySQL datetime
        time_t createdTime = gossip.timestamp / 1000;
        time_t expiresTime = gossip.expiresAt / 1000;

        // Create mutable copies for EscapeString
        std::string gossipType = "event";
        std::string headlineEsc = gossip.headline;
        std::string detailsEsc = gossip.details;
        CharacterDatabase.EscapeString(gossipType);
        CharacterDatabase.EscapeString(headlineEsc);
        CharacterDatabase.EscapeString(detailsEsc);

        std::string sql = Acore::StringFormat(
            "INSERT IGNORE INTO alive_world_gossip "
            "(gossip_id, gossip_type, headline, details, source_event_id, "
            "origin_zone_id, importance, spread_count, spread_radius, is_active, "
            "created_at, expires_at) VALUES "
            "({}, '{}', '{}', '{}', {}, {}, {}, {}, {}, 1, FROM_UNIXTIME({}), FROM_UNIXTIME({}))",
            gossip.gossipId,
            gossipType,
            headlineEsc,
            detailsEsc,
            gossip.sourceEventId,
            gossip.originZoneId,
            gossip.importance,
            gossip.spreadRadius,  // spread_count
            gossip.spreadRadius,  // spread_radius
            createdTime,
            expiresTime
        );

        trans->Append(sql.c_str());
    }

    CharacterDatabase.CommitTransaction(trans);

    ALIVE_LOG_DEBUG("Persisted %zu gossip items to database", gossipToPersist.size());
}

// ============================================================================
// WEATHER TRACKING
// ============================================================================

void WorldStateManager::UpdateZoneWeather(uint32 zoneId, uint8 weatherType, float grade)
{
    std::unique_lock<std::shared_mutex> lock(m_weatherMutex);

    ZoneWeatherState& state = m_zoneWeather[zoneId];
    bool wasStormy = state.IsStormy();

    state.zoneId = zoneId;
    state.grade = grade;
    state.lastChangeTime = NowMs();

    // Convert AzerothCore WeatherType to our WeatherCategory
    switch (weatherType)
    {
        case 0:  state.category = WeatherCategory::FINE; break;
        case 1:  state.category = WeatherCategory::RAIN; break;
        case 2:  state.category = WeatherCategory::SNOW; break;
        case 3:  state.category = WeatherCategory::STORM; break;
        case 86: state.category = WeatherCategory::THUNDERS; break;
        case 90: state.category = WeatherCategory::BLACKRAIN; break;
        default: state.category = WeatherCategory::FINE; break;
    }

    state.intensity = GradeToIntensity(grade);

    // Track when weather state began (for duration-based gossip)
    bool isFine = state.category == WeatherCategory::FINE || state.intensity == WeatherIntensity::NONE;
    if (isFine != (wasStormy || m_zoneWeather[zoneId].category == WeatherCategory::FINE))
    {
        state.stateStartTime = NowMs();
    }

    ALIVE_LOG_DEBUG("Zone %u weather updated: %s %s (grade: %.2f)",
                   zoneId,
                   WeatherIntensityToString(state.intensity),
                   WeatherCategoryToString(state.category),
                   grade);
}

ZoneWeatherState WorldStateManager::GetZoneWeather(uint32 zoneId) const
{
    std::shared_lock<std::shared_mutex> lock(m_weatherMutex);

    auto it = m_zoneWeather.find(zoneId);
    if (it != m_zoneWeather.end())
    {
        return it->second;
    }

    // Return default clear weather
    ZoneWeatherState defaultState;
    defaultState.zoneId = zoneId;
    return defaultState;
}

std::vector<uint32> WorldStateManager::GetZonesWithBadWeather() const
{
    std::vector<uint32> result;
    std::shared_lock<std::shared_mutex> lock(m_weatherMutex);

    for (const auto& [zoneId, state] : m_zoneWeather)
    {
        if (state.IsBadWeather())
        {
            result.push_back(zoneId);
        }
    }

    return result;
}

bool WorldStateManager::HasBadWeather(uint32 zoneId) const
{
    std::shared_lock<std::shared_mutex> lock(m_weatherMutex);

    auto it = m_zoneWeather.find(zoneId);
    return it != m_zoneWeather.end() && it->second.IsBadWeather();
}

// ============================================================================
// TIME TRACKING
// ============================================================================

void WorldStateManager::UpdateTimeState()
{
    // Use AzerothCore's time utilities
    Seconds gameTime = GameTime::GetGameTime();

    m_timeState.currentHour = Acore::Time::GetHours(gameTime);
    m_timeState.currentMinute = Acore::Time::GetMinutes(gameTime);
    m_timeState.dayOfWeek = Acore::Time::GetDayInWeek(gameTime);
    m_timeState.dayOfYear = Acore::Time::GetDayInYear(gameTime);

    // Calculate season (same as Weather.cpp)
    // Day 78-168: Spring, Day 168-259: Summer, Day 259-350: Fall, Day 350-78: Winter
    uint32 day = m_timeState.dayOfYear;
    if (day >= 78 && day < 168)
        m_timeState.currentSeason = Season::SPRING;
    else if (day >= 168 && day < 259)
        m_timeState.currentSeason = Season::SUMMER;
    else if (day >= 259 && day < 350)
        m_timeState.currentSeason = Season::FALL;
    else
        m_timeState.currentSeason = Season::WINTER;

    // Calculate time period
    uint8 hour = m_timeState.currentHour;
    if (hour >= 5 && hour < 7)
        m_timeState.currentPeriod = TimeTransitionType::DAWN;
    else if (hour >= 7 && hour < 12)
        m_timeState.currentPeriod = TimeTransitionType::MORNING;
    else if (hour == 12)
        m_timeState.currentPeriod = TimeTransitionType::NOON;
    else if (hour > 12 && hour < 18)
        m_timeState.currentPeriod = TimeTransitionType::AFTERNOON;
    else if (hour >= 18 && hour < 20)
        m_timeState.currentPeriod = TimeTransitionType::DUSK;
    else if (hour >= 20 && hour < 22)
        m_timeState.currentPeriod = TimeTransitionType::EVENING;
    else if (hour == 0)
        m_timeState.currentPeriod = TimeTransitionType::MIDNIGHT;
    else
        m_timeState.currentPeriod = TimeTransitionType::NIGHT;
}

bool WorldStateManager::CheckTimeTransition()
{
    // Get current hour
    Seconds gameTime = GameTime::GetGameTime();
    uint8 currentHour = Acore::Time::GetHours(gameTime);

    // Check if hour changed
    if (currentHour == m_lastTrackedHour)
    {
        return false;
    }

    uint8 previousHour = m_lastTrackedHour;
    m_lastTrackedHour = currentHour;

    // Update full time state
    UpdateTimeState();
    m_timeState.lastTransitionTime = NowMs();

    // Log significant transitions
    if (currentHour == 0)
    {
        ALIVE_LOG_DEBUG("Time transition: Midnight");
    }
    else if (currentHour == 5)
    {
        ALIVE_LOG_DEBUG("Time transition: Dawn (hour %u)", currentHour);
    }
    else if (currentHour == 7)
    {
        ALIVE_LOG_DEBUG("Time transition: Morning (hour %u)", currentHour);
    }
    else if (currentHour == 12)
    {
        ALIVE_LOG_DEBUG("Time transition: Noon (hour %u)", currentHour);
    }
    else if (currentHour == 18)
    {
        ALIVE_LOG_DEBUG("Time transition: Dusk (hour %u)", currentHour);
    }
    else if (currentHour == 22)
    {
        ALIVE_LOG_DEBUG("Time transition: Night (hour %u)", currentHour);
    }

    // Return true if this was a significant transition hour
    return (currentHour == 0 || currentHour == 5 || currentHour == 7 ||
            currentHour == 12 || currentHour == 18 || currentHour == 22);
}

} // namespace AliveWorld
