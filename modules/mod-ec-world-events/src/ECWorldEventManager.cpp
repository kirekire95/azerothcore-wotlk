/*
 * EC World Events - Custom World Event System for AzerothCore
 * Copyright (C) 2026 EC
 */

#include "ECWorldEventManager.h"
#include "ECWorldEventBridge.h"
#include "Config.h"
#include "DatabaseEnv.h"
#include "Log.h"
#include "Player.h"
#include "Creature.h"
#include "CreatureAI.h"
#include "GameObject.h"
#include "World.h"
#include "WorldSessionMgr.h"
#include "Chat.h"
#include "ObjectMgr.h"
#include "MapMgr.h"
#include "WorldSession.h"
#include "Item.h"
#include "Mail.h"
#include "Map.h"
#include "MotionMaster.h"
#include "TemporarySummon.h"
#include <cstdarg>
#include <algorithm>
#include <limits>
#include <sstream>
#include <random>
#include <set>
#include <unordered_map>
#include "ObjectAccessor.h"
#include "DBCStores.h"

// Playerbots integration
#ifdef MOD_PLAYERBOTS
#include "AiObjectContext.h"
#include "LFGMgr.h"
#include "NewRpgInfo.h"
#include "RandomPlayerbotMgr.h"
#include "PlayerbotMgr.h"
#include "PlayerbotAI.h"
#include "PlayerbotRepository.h"
#include "TravelMgr.h"
#endif

// AliveWorld integration
#ifdef MOD_ALIVE_WORLD
#include "AliveWorldManager.h"
#include "BotMemoryManager.h"
#include "IntelligenceTierManager.h"
#include "WorldStateManager.h"
#endif

namespace EC
{

#ifdef MOD_PLAYERBOTS
struct BotRuntimeActivitySnapshot
{
    TravelDestination* travelDestination = nullptr;
    WorldPosition* travelPosition = nullptr;
    TravelStatus travelStatus = TRAVEL_STATUS_NONE;
    uint32 travelTimeLeftMs = 0;
    bool travelGroupCopy = false;
    bool travelForced = false;
    float travelRadius = 0.0f;
    uint32 travelMoveRetries = 0;
    uint32 travelExtendRetries = 0;
    GuidPosition rpgTarget;
    NewRpgInfo rpgInfo;
    uint32 capturedAtMs = 0;
    bool holdsTravelVisitors = false;

    void HoldTravelTarget(TravelDestination* destination, WorldPosition* position)
    {
        travelDestination = destination;
        travelPosition = position;
        if (travelDestination && travelPosition)
        {
            travelDestination->addVisitor();
            travelPosition->addVisitor();
            holdsTravelVisitors = true;
        }
    }

    ~BotRuntimeActivitySnapshot()
    {
        if (holdsTravelVisitors)
        {
            travelDestination->remVisitor();
            travelPosition->remVisitor();
        }
    }
};
#endif

namespace
{
    // Fallback defaults (used if config not yet loaded)
    constexpr uint32 kDefaultBotHardCap = 80;
    constexpr uint32 kDefaultMaxCreatureSpawnsPerTick = 5;
    constexpr uint32 kDefaultDeferredSpawnStaggerMs = 500;
    constexpr uint32 kDefaultMaxBotTeleportsPerTick = 3;

#ifdef MOD_PLAYERBOTS
    constexpr uint32 kBotReturnRetrySeconds = 10;
    constexpr uint32 kBotReturnTeleportTimeoutSeconds = 30;
    constexpr uint32 kBotReturnFinalizeTimeoutSeconds = 60;

    std::string FormatBotStrategies(const std::vector<std::string>& strategies)
    {
        std::ostringstream out;
        bool first = true;
        for (const std::string& strategy : strategies)
        {
            if (strategy.empty())
                continue;

            if (!first)
                out << ',';
            out << '+' << strategy;
            first = false;
        }
        return out.str();
    }
#endif

    // Runtime combat behavior defaults for spawned event NPCs
    constexpr uint32 kEventNpcAggroScanIntervalMs = 1000;
    constexpr uint32 kEventNpcStatusLogIntervalMs = 10000;
    constexpr uint32 kEventNpcIdleWanderIntervalMs = 4000;
    constexpr float kEventNpcAggroRangeYards = 60.0f;
    constexpr float kEventNpcIdleWanderRadiusMinYards = 10.0f;
    constexpr float kEventNpcIdleWanderRadiusMaxYards = 20.0f;
    constexpr float kRaidTakeoverClearRadiusYards = 90.0f;

    bool IsConstructionWorkerName(std::string const& name)
    {
        return name.find("Peon") != std::string::npos || name.find("Worker") != std::string::npos;
    }

    bool TeamMatchesEventFaction(uint32 teamId, EventFaction faction)
    {
        switch (faction)
        {
            case EventFaction::NEUTRAL:
                return true;
            case EventFaction::ALLIANCE:
                return teamId == TEAM_ALLIANCE;
            case EventFaction::HORDE:
                return teamId == TEAM_HORDE;
            default:
                return false;
        }
    }

    bool UseRaidDefenderMix(const EventDefinition& def, bool enabled, uint32 defenderSharePct)
    {
        return def.type == EventType::RAID &&
               enabled &&
               defenderSharePct > 0 &&
               def.attackerFaction != EventFaction::NEUTRAL &&
               def.defenderFaction != EventFaction::NEUTRAL &&
               def.attackerFaction != def.defenderFaction;
    }

    bool IsBannerForFaction(uint32 gameObjectEntry, EventFaction faction)
    {
        if (faction == EventFaction::ALLIANCE)
            return gameObjectEntry == 900080;
        if (faction == EventFaction::HORDE)
            return gameObjectEntry == 900081;
        return false;
    }

    EventFaction OpposingFaction(EventFaction faction)
    {
        switch (faction)
        {
            case EventFaction::ALLIANCE:
                return EventFaction::HORDE;
            case EventFaction::HORDE:
                return EventFaction::ALLIANCE;
            default:
                return EventFaction::NEUTRAL;
        }
    }

    EventFaction GetUnitEventFaction(Unit const* unit)
    {
        if (!unit)
            return EventFaction::NEUTRAL;

        if (Player const* player = unit->ToPlayer())
            return (player->GetTeamId() == TEAM_ALLIANCE) ? EventFaction::ALLIANCE : EventFaction::HORDE;

        Creature const* creature = unit->ToCreature();
        if (!creature)
            return EventFaction::NEUTRAL;

        FactionTemplateEntry const* factionTemplate = creature->GetFactionTemplateEntry();
        if (!factionTemplate)
            return EventFaction::NEUTRAL;

        bool isAlliance = (factionTemplate->ourMask & FACTION_MASK_ALLIANCE) != 0;
        bool isHorde = (factionTemplate->ourMask & FACTION_MASK_HORDE) != 0;

        if (isAlliance && !isHorde)
            return EventFaction::ALLIANCE;
        if (isHorde && !isAlliance)
            return EventFaction::HORDE;
        return EventFaction::NEUTRAL;
    }

    bool UnitCanBeRaidTarget(Creature const* seeker, Unit* candidate)
    {
        if (!seeker || !candidate || !candidate->IsAlive())
            return false;

        if (candidate == seeker)
            return false;

        if (!candidate->isTargetableForAttack(false, seeker))
            return false;

        if (candidate->IsCreature())
        {
            Creature* candidateCreature = candidate->ToCreature();
            if (!candidateCreature || candidateCreature->IsCritter() || candidateCreature->IsPet() ||
                candidateCreature->IsTotem() || candidateCreature->IsTrigger())
            {
                return false;
            }

            if (IsConstructionWorkerName(candidateCreature->GetName()))
                return false;
        }

        return seeker->IsValidAttackTarget(candidate);
    }

    class NearbyRaidTargetCreatureSelector
    {
    public:
        NearbyRaidTargetCreatureSelector(Creature* seeker, EventFaction preferredFaction,
            bool includeNeutralTargets, float radiusYards, uint32 requiredZoneId)
            : _seeker(seeker), _preferredFaction(preferredFaction), _includeNeutralTargets(includeNeutralTargets),
              _radiusYards(radiusYards), _requiredZoneId(requiredZoneId), _best(nullptr),
              _bestDistance(std::numeric_limits<float>::max()) { }

        void Visit(std::unordered_map<ObjectGuid, Creature*>& creatureMap)
        {
            if (!_seeker)
                return;

            EventFaction seekerFaction = GetUnitEventFaction(_seeker);
            for (auto const& pair : creatureMap)
            {
                Creature* candidate = pair.second;
                if (!candidate || !candidate->IsInWorld() || candidate->IsDuringRemoveFromWorld() || !candidate->IsAlive())
                    continue;
                if (candidate->GetZoneId() != _requiredZoneId)
                    continue;
                if (!_seeker->IsWithinDistInMap(candidate, _radiusYards, true, false, false))
                    continue;
                if (!UnitCanBeRaidTarget(_seeker, candidate))
                    continue;

                EventFaction candidateFaction = GetUnitEventFaction(candidate);
                if (candidateFaction == seekerFaction)
                    continue;

                bool factionAllowed = _preferredFaction == EventFaction::NEUTRAL ||
                    candidateFaction == _preferredFaction ||
                    (_includeNeutralTargets && candidateFaction == EventFaction::NEUTRAL);
                if (!factionAllowed)
                    continue;

                float distance = _seeker->GetDistance2d(candidate);
                if (distance < _bestDistance)
                {
                    _bestDistance = distance;
                    _best = candidate;
                }
            }
        }

        template <class T>
        void Visit(std::unordered_map<ObjectGuid, T*>&) { }

        Unit* GetBest() const { return _best; }
        float GetBestDistance() const { return _bestDistance; }

    private:
        Creature* _seeker;
        EventFaction _preferredFaction;
        bool _includeNeutralTargets;
        float _radiusYards;
        uint32 _requiredZoneId;
        Unit* _best;
        float _bestDistance;
    };

    Unit* FindRaidFallbackTarget(Creature* seeker, Map* map, EventFaction preferredFaction,
        bool includeNeutralTargets, float radiusYards, uint32 requiredZoneId)
    {
        if (!seeker || !map)
            return nullptr;

        EventFaction seekerFaction = GetUnitEventFaction(seeker);
        Unit* best = nullptr;
        float bestDistance = std::numeric_limits<float>::max();

        Map::PlayerList const& players = map->GetPlayers();
        if (!players.IsEmpty())
        {
            for (Map::PlayerList::const_iterator itr = players.begin(); itr != players.end(); ++itr)
            {
                Player* player = itr->GetSource();
                if (!player || !player->IsAlive() || player->GetZoneId() != requiredZoneId)
                    continue;
                if (!seeker->IsWithinDistInMap(player, radiusYards, true, false, false))
                    continue;
                if (!UnitCanBeRaidTarget(seeker, player))
                    continue;

                EventFaction candidateFaction = GetUnitEventFaction(player);
                if (candidateFaction == seekerFaction)
                    continue;

                bool factionAllowed = preferredFaction == EventFaction::NEUTRAL ||
                    candidateFaction == preferredFaction ||
                    (includeNeutralTargets && candidateFaction == EventFaction::NEUTRAL);
                if (!factionAllowed)
                    continue;

                float distance = seeker->GetDistance2d(player);
                if (distance < bestDistance)
                {
                    bestDistance = distance;
                    best = player;
                }
            }
        }

        NearbyRaidTargetCreatureSelector creatureSelector(seeker, preferredFaction, includeNeutralTargets,
            radiusYards, requiredZoneId);
        TypeContainerVisitor<NearbyRaidTargetCreatureSelector, MapStoredObjectTypesContainer> visitor(creatureSelector);
        visitor.Visit(map->GetObjectsStore());

        Unit* creatureTarget = creatureSelector.GetBest();
        if (creatureTarget && creatureSelector.GetBestDistance() < bestDistance)
            best = creatureTarget;

        return best;
    }

    class NearbyFactionCreatureCounter
    {
    public:
        NearbyFactionCreatureCounter(const EventLocation& location, EventFaction faction, float radiusYards)
            : _location(location), _faction(faction), _radiusYards(radiusYards), _count(0) { }

        void Visit(std::unordered_map<ObjectGuid, Creature*>& creatureMap)
        {
            for (auto const& pair : creatureMap)
            {
                Creature* creature = pair.second;
                if (!creature || !creature->IsInWorld() || creature->IsDuringRemoveFromWorld() || !creature->IsAlive())
                    continue;
                if (creature->IsCritter() || creature->IsPet() || creature->IsTotem() || creature->IsTrigger())
                    continue;
                if (creature->GetMapId() != _location.mapId || creature->GetZoneId() != _location.zoneId)
                    continue;
                if (creature->GetExactDist2d(_location.x, _location.y) > _radiusYards)
                    continue;

                FactionTemplateEntry const* factionTemplate = creature->GetFactionTemplateEntry();
                if (!factionTemplate)
                    continue;

                bool matchesFaction = false;
                if (_faction == EventFaction::ALLIANCE)
                    matchesFaction = (factionTemplate->ourMask & FACTION_MASK_ALLIANCE) != 0;
                else if (_faction == EventFaction::HORDE)
                    matchesFaction = (factionTemplate->ourMask & FACTION_MASK_HORDE) != 0;
                else
                    matchesFaction = true;

                if (matchesFaction)
                    ++_count;
            }
        }

        template <class T>
        void Visit(std::unordered_map<ObjectGuid, T*>&) { }

        uint32 GetCount() const { return _count; }

    private:
        const EventLocation& _location;
        EventFaction _faction;
        float _radiusYards;
        uint32 _count;
    };

    uint32 CountAliveFactionCreaturesNearLocation(Map* map, const EventLocation& location,
        EventFaction faction, float radiusYards)
    {
        if (!map || faction == EventFaction::NEUTRAL)
            return 0;

        NearbyFactionCreatureCounter worker(location, faction, radiusYards);
        TypeContainerVisitor<NearbyFactionCreatureCounter, MapStoredObjectTypesContainer> visitor(worker);
        visitor.Visit(map->GetObjectsStore());
        return worker.GetCount();
    }

    uint32 CountAliveRaidDefendersNearObjective(Map* map, EventDefinition const& def)
    {
        if (!map || def.type != EventType::RAID || def.defenderFaction == EventFaction::NEUTRAL)
            return 0;

        return CountAliveFactionCreaturesNearLocation(
            map, def.location, def.defenderFaction, kRaidTakeoverClearRadiusYards
        );
    }

    bool IsHordeAlignedUnit(Unit const* unit)
    {
        return GetUnitEventFaction(unit) == EventFaction::HORDE;
    }
}

// =============================================================================
// ECDebug - Static Debug System
// =============================================================================

bool ECDebug::s_debugEnabled = false;
uint8 ECDebug::s_logLevel = 2;

void ECDebug::Log(ECLogLevel level, const char* func, const char* fmt, ...)
{
    // Check if we should log this level
    if (static_cast<uint8>(level) > s_logLevel && level != ECLogLevel::EC_ERROR)
        return;

    // Format the message
    char buffer[1024];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);

    // Build the full message
    std::stringstream ss;
    ss << "[EC-Events:" << LogLevelToString(level) << "] [" << func << "] " << buffer;
    std::string message = ss.str();

    // Log to server console based on level
    switch (level)
    {
        case ECLogLevel::EC_ERROR:
            LOG_ERROR("module.ec-events", "{}", message);
            break;
        case ECLogLevel::EC_WARNING:
            LOG_WARN("module.ec-events", "{}", message);
            break;
        case ECLogLevel::EC_INFO:
            LOG_INFO("module.ec-events", "{}", message);
            break;
        case ECLogLevel::EC_DEBUG:
        case ECLogLevel::EC_TRACE:
            LOG_DEBUG("module.ec-events", "{}", message);
            break;
    }

    // If debug mode is enabled, also send to game (GMs only)
    if (s_debugEnabled)
    {
        LogToGame(std::string(LogLevelToColor(level)) + message + "|r");
    }
}

void ECDebug::LogToGame(const std::string& message)
{
    // Buffer debug messages - they are flushed in batch by the manager's Update()
    // to avoid iterating all sessions on every single log call (Issue #18)
    WorldEventManager::Instance()->QueueGameLog(message);
}

void ECDebug::SetDebugEnabled(bool enabled)
{
    s_debugEnabled = enabled;
    LOG_INFO("module.ec-events", "EC World Events: Debug mode {}", enabled ? "ENABLED" : "DISABLED");
}

void ECDebug::SetLogLevel(uint8 level)
{
    s_logLevel = level;
    LOG_INFO("module.ec-events", "EC World Events: Log level set to {}", level);
}

bool ECDebug::IsDebugEnabled() { return s_debugEnabled; }
uint8 ECDebug::GetLogLevel() { return s_logLevel; }

const char* ECDebug::LogLevelToString(ECLogLevel level)
{
    switch (level)
    {
        case ECLogLevel::EC_ERROR:   return "ERROR";
        case ECLogLevel::EC_WARNING: return "WARN";
        case ECLogLevel::EC_INFO:    return "INFO";
        case ECLogLevel::EC_DEBUG:   return "DEBUG";
        case ECLogLevel::EC_TRACE:   return "TRACE";
        default:                     return "???";
    }
}

const char* ECDebug::LogLevelToColor(ECLogLevel level)
{
    switch (level)
    {
        case ECLogLevel::EC_ERROR:   return "|cffff0000";  // Red
        case ECLogLevel::EC_WARNING: return "|cffff9900";  // Orange
        case ECLogLevel::EC_INFO:    return "|cff00ff00";  // Green
        case ECLogLevel::EC_DEBUG:   return "|cff00ffff";  // Cyan
        case ECLogLevel::EC_TRACE:   return "|cff888888";  // Gray
        default:                     return "|cffffffff";  // White
    }
}

// =============================================================================
// WorldEventManager - Constructor/Destructor
// =============================================================================

WorldEventManager::WorldEventManager()
    : m_nextInstanceId(1)
    , m_enabled(false)
    , m_updateIntervalMs(1000)
    , m_maxActiveEvents(10)
    , m_eventStaggerMinutesMin(2)
    , m_eventStaggerMinutesMax(8)
    , m_raidDefenderBotsEnabled(true)
    , m_raidDefenderBotSharePct(50)
    , m_lastUpdateTime(0)
    , m_debugEnabled(false)
    , m_debugLogLevel(2)
{
    EC_LOG_TRACE("WorldEventManager constructor called");
}

WorldEventManager::~WorldEventManager()
{
    // Static singleton destructor - Shutdown() should have been called already
    // from the WorldScript hook before static destruction order issues arise.
    // This is just a safety net.
    if (!m_activeEvents.empty())
    {
        LOG_ERROR("module.ec-events", "WorldEventManager destructor called with %zu active events! "
            "Shutdown() was not called.", m_activeEvents.size());
    }
}

void WorldEventManager::Shutdown()
{
    EC_LOG_INFO("WorldEventManager shutting down...");

    // Stop all active events cleanly: log to DB, despawn everything
    for (auto& pair : m_activeEvents)
    {
        ActiveEvent& event = pair.second;
        EC_LOG_DEBUG("Cleaning up active event instance %u", pair.first);
        event.state = EventState::CANCELLED;
        LogEventToDatabase(event);
        DespawnEventBots(event);
        DespawnAllEventGameObjects(event);
        DespawnAllEventCreatures(event);
    }
    m_activeEvents.clear();

    // Durable rows intentionally remain for startup recovery. Release the
    // in-process travel/RPG snapshots now, while TravelMgr's destination and
    // position stores are still alive, instead of relying on singleton static
    // destruction order.
    m_botReturnContexts.clear();
    m_playerZones.clear();

    EC_LOG_INFO("WorldEventManager shutdown complete");
}

WorldEventManager* WorldEventManager::Instance()
{
    static WorldEventManager instance;
    return &instance;
}

// =============================================================================
// Initialization
// =============================================================================

void WorldEventManager::Initialize()
{
    EC_LOG_INFO("========================================");
    EC_LOG_INFO("EC World Events: Initializing...");
    EC_LOG_INFO("========================================");

    LoadFromConfig();

    // Return checkpoints are recovery state, not event-scheduling state. Load
    // them even when new EC world events are disabled so a config change after
    // a crash cannot strand recruited bots at the old event location.
    QueryResult maxIdResult = CharacterDatabase.Query(
        "SELECT MAX(instance_id) FROM ec_world_event_logs");
    if (maxIdResult)
    {
        Field* fields = maxIdResult->Fetch();
        if (!fields[0].IsNull())
        {
            m_nextInstanceId = fields[0].Get<uint32>() + 1;
            EC_LOG_INFO("Restored next instance ID from DB: %u", m_nextInstanceId);
        }
    }
    LoadBotReturnContexts();

    if (m_enabled)
    {
        EC_LOG_INFO("Module is ENABLED");
        EC_LOG_INFO("  Update Interval: %u ms", m_updateIntervalMs);
        EC_LOG_INFO("  Max Active Events: %u", m_maxActiveEvents);
        EC_LOG_INFO("  Debug Mode: %s", m_debugEnabled ? "ON" : "OFF");
        EC_LOG_INFO("  Debug Log Level: %u", m_debugLogLevel);

        LoadEventDefinitions();
        LoadSettlements();

        // Issue #2: Restore cooldowns from event logs + cooldown config
        // Note: ec_world_event_logs lives in acore_characters, but ec_world_events is in acore_world,
        // so we use a cross-database reference for the JOIN.
        QueryResult cooldownResult = WorldDatabase.Query(
            "SELECT e.id, IFNULL(e.cooldown_minutes, 0), UNIX_TIMESTAMP(l.end_time) "
            "FROM ec_world_events e "
            "JOIN (SELECT event_id, MAX(end_time) as end_time FROM acore_characters.ec_world_event_logs GROUP BY event_id) l "
            "ON e.id = l.event_id");
        if (cooldownResult)
        {
            auto now = std::chrono::system_clock::now();
            do
            {
                Field* fields = cooldownResult->Fetch();
                uint32 eventId = fields[0].Get<uint32>();
                uint32 perEventCooldown = fields[1].Get<uint32>();
                uint32 cooldownMins = perEventCooldown > 0 ? perEventCooldown : m_eventCooldownMinutes;
                // Parse end_time and compute cooldown expiry
                auto endTimeT = static_cast<time_t>(fields[2].Get<uint64>());
                auto endTime = std::chrono::system_clock::from_time_t(endTimeT);
                auto cooldownExpiry = endTime + std::chrono::minutes(cooldownMins);

                if (cooldownExpiry > now)
                {
                    m_eventCooldowns[eventId] = cooldownExpiry;
                    auto remaining = std::chrono::duration_cast<std::chrono::minutes>(cooldownExpiry - now);
                    EC_LOG_INFO("Restored cooldown for event %u: %lld minutes remaining",
                        eventId, remaining.count());
                }
            } while (cooldownResult->NextRow());
        }

        EC_LOG_INFO("Loaded %zu event definitions", m_eventDefinitions.size());
        EC_LOG_INFO("========================================");
    }
    else
    {
        EC_LOG_WARN("Module is DISABLED in config");
    }
}

void WorldEventManager::LoadFromConfig()
{
    EC_LOG_DEBUG("Loading configuration...");

    m_enabled = sConfigMgr->GetOption<bool>("ECWorldEvents.Enable", true);
    m_updateIntervalMs = sConfigMgr->GetOption<uint32>("ECWorldEvents.UpdateIntervalMs", 1000);
    m_maxActiveEvents = sConfigMgr->GetOption<uint32>("ECWorldEvents.MaxActiveEvents", 10);
    m_debugEnabled = sConfigMgr->GetOption<bool>("ECWorldEvents.Debug", false);
    m_debugLogLevel = sConfigMgr->GetOption<uint8>("ECWorldEvents.DebugLogLevel", 2);

    // New config features (2026-01)
    m_announceToWorld = sConfigMgr->GetOption<bool>("ECWorldEvents.AnnounceToWorld", true);
    m_minParticipationForReward = sConfigMgr->GetOption<uint32>("ECWorldEvents.MinParticipationForReward", 10);
    m_autoEscalateEnabled = sConfigMgr->GetOption<bool>("ECWorldEvents.AutoEscalateEnabled", true);
    m_autoEscalateMinutes = sConfigMgr->GetOption<uint32>("ECWorldEvents.AutoEscalateMinutes", 5);
    m_logToDatabase = sConfigMgr->GetOption<bool>("ECWorldEvents.LogToDatabase", true);
    m_eventCooldownMinutes = sConfigMgr->GetOption<uint32>("ECWorldEvents.EventCooldownMinutes", 30);

    // Event start staggering (avoid all events firing at once)
    m_eventStaggerMinutesMin = sConfigMgr->GetOption<uint32>("ECWorldEvents.EventStaggerMinutesMin", 2);
    m_eventStaggerMinutesMax = sConfigMgr->GetOption<uint32>("ECWorldEvents.EventStaggerMinutesMax", 8);
    if (m_eventStaggerMinutesMin > m_eventStaggerMinutesMax)
    {
        EC_LOG_WARN("EventStaggerMinutesMin (%u) > EventStaggerMinutesMax (%u), swapping",
            m_eventStaggerMinutesMin, m_eventStaggerMinutesMax);
        std::swap(m_eventStaggerMinutesMin, m_eventStaggerMinutesMax);
    }

    // Spawning performance settings
    m_botHardCap = sConfigMgr->GetOption<uint32>("ECWorldEvents.BotHardCap", kDefaultBotHardCap);
    m_raidDefenderBotsEnabled = sConfigMgr->GetOption<bool>("ECWorldEvents.RaidDefenderBotsEnabled", true);
    m_raidDefenderBotSharePct = sConfigMgr->GetOption<uint32>("ECWorldEvents.RaidDefenderBotSharePct", 50);
    m_maxCreatureSpawnsPerTick = sConfigMgr->GetOption<uint32>("ECWorldEvents.MaxCreatureSpawnsPerTick", kDefaultMaxCreatureSpawnsPerTick);
    m_deferredSpawnStaggerMs = sConfigMgr->GetOption<uint32>("ECWorldEvents.DeferredSpawnStaggerMs", kDefaultDeferredSpawnStaggerMs);
    m_maxBotTeleportsPerTick = sConfigMgr->GetOption<uint32>("ECWorldEvents.MaxBotTeleportsPerTick", kDefaultMaxBotTeleportsPerTick);

    // Validate config values
    if (m_updateIntervalMs < 100)
    {
        EC_LOG_WARN("UpdateIntervalMs=%u is dangerously low, clamping to 100", m_updateIntervalMs);
        m_updateIntervalMs = 100;
    }
    if (m_minParticipationForReward > 100)
    {
        EC_LOG_WARN("MinParticipationForReward=%u exceeds 100, clamping to 100", m_minParticipationForReward);
        m_minParticipationForReward = 100;
    }
    if (m_autoEscalateEnabled && m_autoEscalateMinutes == 0)
    {
        EC_LOG_WARN("AutoEscalateMinutes=0 with AutoEscalate enabled, setting to 1");
        m_autoEscalateMinutes = 1;
    }
    if (m_raidDefenderBotSharePct > 100)
    {
        EC_LOG_WARN("RaidDefenderBotSharePct=%u exceeds 100, clamping to 100", m_raidDefenderBotSharePct);
        m_raidDefenderBotSharePct = 100;
    }

    // Apply debug settings
    ECDebug::SetDebugEnabled(m_debugEnabled);
    ECDebug::SetLogLevel(m_debugLogLevel);

    EC_LOG_DEBUG("Configuration loaded:");
    EC_LOG_DEBUG("  Enable: %s", m_enabled ? "true" : "false");
    EC_LOG_DEBUG("  UpdateIntervalMs: %u", m_updateIntervalMs);
    EC_LOG_DEBUG("  MaxActiveEvents: %u", m_maxActiveEvents);
    EC_LOG_DEBUG("  Debug: %s", m_debugEnabled ? "true" : "false");
    EC_LOG_DEBUG("  DebugLogLevel: %u", m_debugLogLevel);
    EC_LOG_DEBUG("  AnnounceToWorld: %s", m_announceToWorld ? "true" : "false");
    EC_LOG_DEBUG("  MinParticipationForReward: %u%%", m_minParticipationForReward);
    EC_LOG_DEBUG("  AutoEscalateEnabled: %s", m_autoEscalateEnabled ? "true" : "false");
    EC_LOG_DEBUG("  AutoEscalateMinutes: %u", m_autoEscalateMinutes);
    EC_LOG_DEBUG("  LogToDatabase: %s", m_logToDatabase ? "true" : "false");
    EC_LOG_DEBUG("  EventCooldownMinutes: %u", m_eventCooldownMinutes);
    EC_LOG_DEBUG("  RaidDefenderBotsEnabled: %s", m_raidDefenderBotsEnabled ? "true" : "false");
    EC_LOG_DEBUG("  RaidDefenderBotSharePct: %u%%", m_raidDefenderBotSharePct);
}

void WorldEventManager::LoadEventDefinitions()
{
    EC_LOG_INFO("Loading event definitions from database...");

    m_eventDefinitions.clear();

    QueryResult result = WorldDatabase.Query(
        "SELECT id, name, description, type, attacker_faction, defender_faction, "
        "map_id, pos_x, pos_y, pos_z, orientation, zone_id, area_id, location_name, "
        "min_bots, max_bots, duration_minutes, min_level, max_level, enabled, "
        "IFNULL(preparation_minutes, 0), IFNULL(ending_minutes, 0), "
        "IFNULL(cooldown_minutes, 0) "
        "FROM ec_world_events WHERE enabled = 1"
    );

    if (!result)
    {
        EC_LOG_WARN("No event definitions found in database (ec_world_events table empty or doesn't exist)");
        return;
    }

    uint32 count = 0;
    do
    {
        Field* fields = result->Fetch();

        EventDefinition def;
        def.eventId = fields[0].Get<uint32>();
        def.name = fields[1].Get<std::string>();
        def.description = fields[2].Get<std::string>();
        // Issue #12: Validate enum ranges from DB
        uint8 typeVal = fields[3].Get<uint8>();
        uint8 attackerVal = fields[4].Get<uint8>();
        uint8 defenderVal = fields[5].Get<uint8>();
        if (typeVal > 4)
        {
            EC_LOG_WARN("Event %u has invalid type %u, skipping", fields[0].Get<uint32>(), typeVal);
            continue;
        }
        if (attackerVal > 2 || defenderVal > 2)
        {
            EC_LOG_WARN("Event %u has invalid faction (attacker=%u, defender=%u), skipping",
                fields[0].Get<uint32>(), attackerVal, defenderVal);
            continue;
        }
        def.type = static_cast<EventType>(typeVal);
        def.attackerFaction = static_cast<EventFaction>(attackerVal);
        def.defenderFaction = static_cast<EventFaction>(defenderVal);

        def.location.mapId = fields[6].Get<uint32>();
        def.location.x = fields[7].Get<float>();
        def.location.y = fields[8].Get<float>();
        def.location.z = fields[9].Get<float>();
        def.location.orientation = fields[10].Get<float>();
        def.location.zoneId = fields[11].Get<uint32>();
        def.location.areaId = fields[12].Get<uint32>();
        def.location.name = fields[13].Get<std::string>();

        def.minBots = fields[14].Get<uint32>();
        def.maxBots = fields[15].Get<uint32>();
        def.durationMinutes = fields[16].Get<uint32>();
        def.minLevel = fields[17].Get<uint32>();
        def.maxLevel = fields[18].Get<uint32>();
        def.enabled = fields[19].Get<bool>();

        // Phase timing
        def.preparationMinutes = fields[20].Get<uint32>();
        def.endingMinutes = fields[21].Get<uint32>();
        def.cooldownMinutes = fields[22].Get<uint32>();

        // Issue #13: Validate phase timing - prevent negative ACTIVE duration
        if (def.preparationMinutes + def.endingMinutes >= def.durationMinutes && def.durationMinutes > 0)
        {
            EC_LOG_WARN("Event %u '%s': preparationMinutes(%u) + endingMinutes(%u) >= durationMinutes(%u), "
                "clamping endingMinutes to 0",
                def.eventId, def.name.c_str(),
                def.preparationMinutes, def.endingMinutes, def.durationMinutes);
            def.endingMinutes = 0;
            if (def.preparationMinutes >= def.durationMinutes)
            {
                EC_LOG_WARN("  preparationMinutes also >= duration, clamping to duration-1");
                def.preparationMinutes = def.durationMinutes > 1 ? def.durationMinutes - 1 : 0;
            }
        }

        EC_LOG_DEBUG("  [%u] '%s' - %s at %s (Zone: %u, Area: %u)",
            def.eventId, def.name.c_str(),
            EventTypeToString(def.type).c_str(),
            def.location.name.c_str(),
            def.location.zoneId, def.location.areaId);

        EC_LOG_TRACE("      Map: %u, Pos: (%.2f, %.2f, %.2f)",
            def.location.mapId, def.location.x, def.location.y, def.location.z);
        EC_LOG_TRACE("      Bots: %u-%u, Duration: %u min, Level: %u-%u",
            def.minBots, def.maxBots, def.durationMinutes, def.minLevel, def.maxLevel);
        EC_LOG_TRACE("      Phases: Prep=%u min, Ending=%u min",
            def.preparationMinutes, def.endingMinutes);

        // Load rewards, objects, and creatures for this event
        LoadEventRewards(def.eventId, def);
        LoadEventObjects(def.eventId, def);
        LoadEventCreatures(def.eventId, def);

        m_eventDefinitions[def.eventId] = def;
        count++;

    } while (result->NextRow());

    EC_LOG_INFO("Successfully loaded %u event definitions", count);
}

void WorldEventManager::LoadEventRewards(uint32 eventId, EventDefinition& def)
{
    EC_LOG_TRACE("Loading rewards for event %u...", eventId);

    QueryResult result = CharacterDatabase.Query(
        "SELECT id, reward_type, value, item_count, min_kills, participation_percent, "
        "IFNULL(reward_condition, 0) "
        "FROM ec_world_event_rewards WHERE event_id = {}",
        eventId
    );

    if (!result)
    {
        EC_LOG_TRACE("  No rewards defined for event %u", eventId);
        return;
    }

    do
    {
        Field* fields = result->Fetch();

        EventReward reward;
        reward.rewardId = fields[0].Get<uint32>();
        reward.type = static_cast<RewardType>(fields[1].Get<uint8>());
        reward.value = fields[2].Get<uint32>();
        reward.itemCount = fields[3].Get<uint32>();
        reward.minKills = fields[4].Get<uint32>();
        reward.participation = fields[5].Get<float>() / 100.0f;
        reward.rewardCondition = fields[6].Get<uint8>();

        EC_LOG_TRACE("  Reward [%u]: Type=%u, Value=%u, MinKills=%u, Participation=%.1f%%, Condition=%u",
            reward.rewardId, static_cast<uint8>(reward.type), reward.value,
            reward.minKills, reward.participation * 100.0f, reward.rewardCondition);

        def.rewards.push_back(reward);

    } while (result->NextRow());

    EC_LOG_DEBUG("  Loaded %zu rewards for event %u", def.rewards.size(), eventId);
}

void WorldEventManager::LoadEventObjects(uint32 eventId, EventDefinition& def)
{
    EC_LOG_TRACE("Loading objects for event %u...", eventId);

    QueryResult result = WorldDatabase.Query(
        "SELECT id, gameobject_entry, offset_x, offset_y, offset_z, orientation, "
        "spawn_phase, despawn_phase, name, is_capturable, capture_time_seconds, "
        "points_on_capture, burning_entry, destroyed_entry, "
        "IFNULL(spawn_condition, 0), "
        "IFNULL(guard_respawn_seconds, 0), IFNULL(max_guards, 0) "
        "FROM ec_world_event_objects WHERE event_id = {}",
        eventId
    );

    if (!result)
    {
        EC_LOG_TRACE("  No objects defined for event %u", eventId);
        return;
    }

    do
    {
        Field* fields = result->Fetch();

        EventObject obj;
        obj.objectId = fields[0].Get<uint32>();
        obj.gameObjectEntry = fields[1].Get<uint32>();
        obj.offsetX = fields[2].Get<float>();
        obj.offsetY = fields[3].Get<float>();
        obj.offsetZ = fields[4].Get<float>();
        obj.orientation = fields[5].Get<float>();
        obj.spawnPhase = fields[6].Get<uint8>();
        obj.despawnPhase = fields[7].Get<uint8>();
        obj.name = fields[8].Get<std::string>();

        // Capturable building fields (AV-style towers/bunkers)
        obj.isCapturable = fields[9].Get<bool>();
        obj.captureTimeSeconds = fields[10].Get<uint32>();
        obj.pointsOnCapture = fields[11].Get<int32>();
        obj.burningEntry = fields[12].IsNull() ? 0 : fields[12].Get<uint32>();
        obj.destroyedEntry = fields[13].IsNull() ? 0 : fields[13].Get<uint32>();
        obj.spawnCondition = fields[14].Get<uint8>();
        obj.guardRespawnSeconds = fields[15].Get<uint32>();
        obj.maxGuards = fields[16].Get<uint32>();

        // Validate GameObject entry exists
        GameObjectTemplate const* goTemplate = sObjectMgr->GetGameObjectTemplate(obj.gameObjectEntry);
        if (!goTemplate)
        {
            EC_LOG_WARN("  Object %u references non-existent GameObject entry %u, skipping",
                obj.objectId, obj.gameObjectEntry);
            continue;
        }

        if (obj.isCapturable)
        {
            EC_LOG_TRACE("  Object [%u] '%s': GO Entry %u (%s), CAPTURABLE, CaptureTime=%us, Points=%d",
                obj.objectId, obj.name.c_str(), obj.gameObjectEntry, goTemplate->name.c_str(),
                obj.captureTimeSeconds, obj.pointsOnCapture);
        }
        else
        {
            EC_LOG_TRACE("  Object [%u] '%s': GO Entry %u (%s), SpawnPhase=%u, DespawnPhase=%u",
                obj.objectId, obj.name.c_str(), obj.gameObjectEntry, goTemplate->name.c_str(),
                obj.spawnPhase, obj.despawnPhase);
        }

        def.objects.push_back(obj);

    } while (result->NextRow());

    EC_LOG_DEBUG("  Loaded %zu objects for event %u", def.objects.size(), eventId);
}

void WorldEventManager::LoadEventCreatures(uint32 eventId, EventDefinition& def)
{
    EC_LOG_TRACE("Loading creatures for event %u...", eventId);

    QueryResult result = WorldDatabase.Query(
        "SELECT id, creature_entry, offset_x, offset_y, offset_z, orientation, "
        "spawn_phase, despawn_phase, is_boss, is_leader, name, "
        "assigned_building_id, patrol_radius, "
        "spawn_delay_seconds, arrival_x, arrival_y, arrival_z, arrival_method, reinforcement_wave "
        "FROM ec_world_event_creatures WHERE event_id = {}",
        eventId
    );

    if (!result)
    {
        EC_LOG_TRACE("  No creatures defined for event %u", eventId);
        return;
    }

    do
    {
        Field* fields = result->Fetch();

        EventCreature creature;
        creature.creatureId = fields[0].Get<uint32>();
        creature.creatureEntry = fields[1].Get<uint32>();
        creature.offsetX = fields[2].Get<float>();
        creature.offsetY = fields[3].Get<float>();
        creature.offsetZ = fields[4].Get<float>();
        creature.orientation = fields[5].Get<float>();
        creature.spawnPhase = fields[6].Get<uint8>();
        creature.despawnPhase = fields[7].Get<uint8>();
        creature.isBoss = fields[8].Get<bool>();
        creature.isLeader = fields[9].Get<bool>();
        creature.name = fields[10].Get<std::string>();

        // Building assignment fields
        creature.assignedBuildingId = fields[11].IsNull() ? 0 : fields[11].Get<uint32>();
        creature.patrolRadius = fields[12].Get<float>();

        // Reinforcement/Arrival fields
        creature.spawnDelaySeconds = fields[13].Get<uint32>();
        creature.hasArrivalPoint = !fields[14].IsNull();
        creature.arrivalX = fields[14].IsNull() ? 0.0f : fields[14].Get<float>();
        creature.arrivalY = fields[15].IsNull() ? 0.0f : fields[15].Get<float>();
        creature.arrivalZ = fields[16].IsNull() ? 0.0f : fields[16].Get<float>();
        creature.arrivalMethod = static_cast<ArrivalMethod>(fields[17].Get<uint8>());
        creature.reinforcementWave = fields[18].Get<uint8>();

        // Validate Creature entry exists
        CreatureTemplate const* creatureTemplate = sObjectMgr->GetCreatureTemplate(creature.creatureEntry);
        if (!creatureTemplate)
        {
            EC_LOG_WARN("  Creature %u references non-existent Creature entry %u, skipping",
                creature.creatureId, creature.creatureEntry);
            continue;
        }

        if (creature.spawnDelaySeconds > 0 || creature.hasArrivalPoint)
        {
            EC_LOG_TRACE("  Creature [%u] '%s': Entry %u, REINFORCEMENT Wave=%u, Delay=%us, Arrival=%s",
                creature.creatureId, creature.name.c_str(), creature.creatureEntry,
                creature.reinforcementWave, creature.spawnDelaySeconds,
                ArrivalMethodToString(creature.arrivalMethod).c_str());
        }
        else if (creature.assignedBuildingId > 0)
        {
            EC_LOG_TRACE("  Creature [%u] '%s': Entry %u, ASSIGNED TO BUILDING %u, PatrolRadius=%.1f",
                creature.creatureId, creature.name.c_str(), creature.creatureEntry,
                creature.assignedBuildingId, creature.patrolRadius);
        }
        else
        {
            EC_LOG_TRACE("  Creature [%u] '%s': Entry %u (%s), Boss=%s, Leader=%s, SpawnPhase=%u",
                creature.creatureId, creature.name.c_str(), creature.creatureEntry,
                creatureTemplate->Name.c_str(),
                creature.isBoss ? "YES" : "no", creature.isLeader ? "YES" : "no",
                creature.spawnPhase);
        }

        def.creatures.push_back(creature);

    } while (result->NextRow());

    EC_LOG_DEBUG("  Loaded %zu creatures for event %u", def.creatures.size(), eventId);
}

void WorldEventManager::ReloadEvents()
{
    // Issue #3: Refuse to reload while events are active - clearing definitions
    // would orphan spawned entities with no way to clean them up
    if (!m_activeEvents.empty())
    {
        EC_LOG_WARN("Cannot reload event definitions while %zu events are active! "
            "Stop all events first with .ecevent stop <id>", m_activeEvents.size());
        return;
    }

    EC_LOG_INFO("Reloading event definitions...");
    LoadEventDefinitions();
    EC_LOG_INFO("Reload complete - %zu events loaded", m_eventDefinitions.size());
}

void WorldEventManager::LoadSettlements()
{
    m_settlements.clear();

    QueryResult result = WorldDatabase.Query(
        "SELECT s.id, s.name, s.map_id, s.zone_id, s.area_id, s.center_x, s.center_y, s.center_z, "
        "s.population_radius, s.faction, s.treasury, s.development_level, s.damage_pct, "
        "IFNULL(SUM(CASE WHEN r.enabled = 1 THEN r.income_per_hour * r.health_pct / 100 ELSE 0 END), 0) "
        "FROM ec_world_settlements s LEFT JOIN ec_world_settlement_resources r ON r.settlement_id = s.id "
        "GROUP BY s.id, s.name, s.map_id, s.zone_id, s.area_id, s.center_x, s.center_y, s.center_z, "
        "s.population_radius, s.faction, s.treasury, s.development_level, s.damage_pct");

    if (!result)
    {
        EC_LOG_WARN("No settlement economy rows found; persistent settlement progression is inactive");
        return;
    }

    do
    {
        Field* fields = result->Fetch();
        SettlementState settlement;
        settlement.id = fields[0].Get<uint32>();
        settlement.name = fields[1].Get<std::string>();
        settlement.mapId = fields[2].Get<uint32>();
        settlement.zoneId = fields[3].Get<uint32>();
        settlement.areaId = fields[4].Get<uint32>();
        settlement.centerX = fields[5].Get<float>();
        settlement.centerY = fields[6].Get<float>();
        settlement.centerZ = fields[7].Get<float>();
        settlement.populationRadius = fields[8].Get<float>();
        settlement.faction = static_cast<EventFaction>(fields[9].Get<uint8>());
        settlement.treasury = fields[10].Get<uint64>();
        settlement.developmentLevel = fields[11].Get<uint16>();
        settlement.damagePct = fields[12].Get<uint8>();
        settlement.incomePerHour = fields[13].Get<uint32>();
        m_settlements[settlement.id] = settlement;
    } while (result->NextRow());

    EC_LOG_INFO("Loaded %zu persistent settlement economies", m_settlements.size());
}

void WorldEventManager::ProcessSettlementEconomy(uint32 diff)
{
    m_settlementEconomyElapsedMs += diff;
    if (m_settlementEconomyElapsedMs < m_settlementEconomyIntervalMs || m_settlements.empty())
        return;

    uint32 elapsedMs = m_settlementEconomyElapsedMs;
    m_settlementEconomyElapsedMs = 0;

    for (auto& [settlementId, settlement] : m_settlements)
    {
        uint64 generated = static_cast<uint64>(settlement.incomePerHour) * elapsedMs / 3600000;
        settlement.treasury += generated;

        if (settlement.damagePct > 0 && settlement.treasury >= 10)
        {
            uint32 repairPoints = std::min<uint32>(settlement.damagePct, settlement.treasury / 10);
            settlement.damagePct -= repairPoints;
            settlement.treasury -= static_cast<uint64>(repairPoints) * 10;
        }

        uint64 upgradeCost = 500ULL * (settlement.developmentLevel + 1);
        if (settlement.damagePct == 0 && settlement.treasury >= upgradeCost)
        {
            settlement.treasury -= upgradeCost;
            ++settlement.developmentLevel;
            EC_LOG_INFO("Settlement '%s' advanced to development level %u",
                settlement.name.c_str(), settlement.developmentLevel);
        }

        WorldDatabase.Execute(
            "UPDATE ec_world_settlements SET treasury = {}, development_level = {}, damage_pct = {}, "
            "last_economy_update = CURRENT_TIMESTAMP WHERE id = {}",
            settlement.treasury, settlement.developmentLevel, settlement.damagePct, settlementId);
    }
}

void WorldEventManager::SnapshotRaidTownPopulation(ActiveEvent& event, EventDefinition const& def)
{
    if (def.type != EventType::RAID)
        return;

    SettlementState const* settlement = nullptr;
    for (auto const& [settlementId, candidate] : m_settlements)
    {
        if (candidate.mapId != def.location.mapId || candidate.zoneId != def.location.zoneId)
            continue;

        float dx = candidate.centerX - def.location.x;
        float dy = candidate.centerY - def.location.y;
        if (dx * dx + dy * dy <= candidate.populationRadius * candidate.populationRadius)
        {
            event.settlementId = settlementId;
            settlement = &candidate;
            break;
        }
    }

    if (!settlement)
    {
        EC_LOG_WARN("RAID '%s' has no settlement definition; 80%% town attrition is unavailable", def.name.c_str());
        return;
    }

    Map* map = sMapMgr->FindBaseNonInstanceMap(settlement->mapId);
    if (!map)
        return;

    event.townPopulation.clear();
    event.townCasualtiesRecorded.clear();
    event.townCasualties = 0;
    event.casualtyReinforcementStage = 0;

    class SettlementPopulationCollector
    {
    public:
        SettlementPopulationCollector(ActiveEvent& targetEvent, SettlementState const& targetSettlement,
            EventFaction defenderFaction)
            : _event(targetEvent), _settlement(targetSettlement), _defenderFaction(defenderFaction) { }

        void Visit(std::unordered_map<ObjectGuid, Creature*>& creatures)
        {
            for (auto const& [guid, creature] : creatures)
            {
                if (!creature || !creature->IsInWorld() || !creature->IsAlive() || creature->IsCritter() ||
                    creature->IsPet() || creature->IsTotem() || creature->IsTrigger())
                    continue;

                float dx = creature->GetPositionX() - _settlement.centerX;
                float dy = creature->GetPositionY() - _settlement.centerY;
                if (dx * dx + dy * dy > _settlement.populationRadius * _settlement.populationRadius)
                    continue;

                if (GetUnitEventFaction(creature) == _defenderFaction)
                    _event.townPopulation.insert(guid);
            }
        }

        void Visit(std::unordered_map<ObjectGuid, GameObject*>&) { }
        void Visit(std::unordered_map<ObjectGuid, DynamicObject*>&) { }
        void Visit(std::unordered_map<ObjectGuid, Corpse*>&) { }

    private:
        ActiveEvent& _event;
        SettlementState const& _settlement;
        EventFaction _defenderFaction;
    };

    SettlementPopulationCollector collector(event, *settlement, def.defenderFaction);
    TypeContainerVisitor<SettlementPopulationCollector, MapStoredObjectTypesContainer> visitor(collector);
    visitor.Visit(map->GetObjectsStore());

    event.townPopulationStart = static_cast<uint32>(event.townPopulation.size());
    EC_LOG_INFO("RAID '%s' snapshot settlement '%s': %u counted town defenders; victory at 80%% casualties",
        def.name.c_str(), settlement->name.c_str(), event.townPopulationStart);
}

void WorldEventManager::ProcessRaidTownCasualty(
    ActiveEvent& event, EventDefinition const& def, Creature* creature)
{
    if (def.type != EventType::RAID || event.phase != EventPhase::ACTIVE || event.townPopulationStart == 0)
        return;

    ObjectGuid guid = creature->GetGUID();
    if (!event.townPopulation.count(guid) || !event.townCasualtiesRecorded.insert(guid).second)
        return;

    ++event.townCasualties;

    // Keep counted defenders dead for this assault. Their normal respawn timer
    // is pushed beyond the event; cleanup/recovery can restore normal behavior.
    creature->SetRespawnTime(static_cast<uint32>(
        std::max<int64>(60, std::chrono::duration_cast<std::chrono::seconds>(
            event.endTime - std::chrono::system_clock::now()).count() + 60)));

    EvaluateRaidTownAttrition(event, def);
}

void WorldEventManager::EvaluateRaidTownAttrition(ActiveEvent& event, EventDefinition const& def)
{
    if (def.type != EventType::RAID || event.phase != EventPhase::ACTIVE || event.townPopulationStart == 0)
        return;

    auto settlementIt = m_settlements.find(event.settlementId);
    if (settlementIt == m_settlements.end())
        return;

    SettlementState const& settlement = settlementIt->second;
    Map* map = sMapMgr->FindBaseNonInstanceMap(settlement.mapId);
    uint32 dynamicPlayers = 0;

    if (map)
    {
        for (auto const& playerRef : map->GetPlayers())
        {
            Player* player = playerRef.GetSource();
            if (!player || !player->IsInWorld() || player->IsDuringRemoveFromWorld() || !player->IsAlive())
                continue;
            if (!TeamMatchesEventFaction(player->GetTeamId(), def.defenderFaction))
                continue;

            float dx = player->GetPositionX() - settlement.centerX;
            float dy = player->GetPositionY() - settlement.centerY;
            if (dx * dx + dy * dy <= settlement.populationRadius * settlement.populationRadius)
                ++dynamicPlayers;
        }
    }

    uint32 aliveTownNpcs = event.townPopulationStart - std::min(event.townPopulationStart, event.townCasualties);
    uint32 currentTotal = event.townPopulationStart + dynamicPlayers;
    uint32 currentAlive = aliveTownNpcs + dynamicPlayers;
    uint32 casualtyPct = currentTotal > 0 ? 100 - (currentAlive * 100 / currentTotal) : 0;

    uint8 newStage = casualtyPct >= 50 ? 2 : (casualtyPct >= 25 ? 1 : 0);
    if (newStage > event.casualtyReinforcementStage)
    {
        event.casualtyReinforcementStage = newStage;
        uint32 reinforcementCount = newStage == 1 ? 4 : 8;
        SpawnAdditionalBots(event, reinforcementCount, true);
        EC_LOG_INFO("RAID '%s' reached %u%% effective attrition with %u live defending players: "
            "reinforcement stage %u (%u bots)",
            def.name.c_str(), casualtyPct, dynamicPlayers, newStage, reinforcementCount);
    }

    if (casualtyPct >= 80 && event.outcome == EventOutcome::PENDING)
    {
        event.outcome = EventOutcome::ATTACKER_WIN;
        EC_LOG_INFO("RAID '%s' settlement defeated: NPC casualties=%u/%u, live defending players=%u, "
            "effective attrition=%u%%",
            def.name.c_str(), event.townCasualties, event.townPopulationStart, dynamicPlayers, casualtyPct);
        TransitionToPhase(event, EventPhase::ENDING);
    }
}

// =============================================================================
// Update Loop
// =============================================================================

void WorldEventManager::QueueGameLog(const std::string& message)
{
    m_pendingGameLogs.push_back(message);
}

void WorldEventManager::Update(uint32 diff)
{
    if (!m_enabled)
    {
        // A live config reload can disable the module while events are still
        // running. Cancel those events first so their recruited bots enter the
        // normal return pipeline instead of remaining at the event forever.
        if (!m_activeEvents.empty())
        {
            std::vector<uint32> activeInstanceIds;
            activeInstanceIds.reserve(m_activeEvents.size());
            for (auto const& [instanceId, event] : m_activeEvents)
            {
                (void)event;
                activeInstanceIds.push_back(instanceId);
            }

            EC_LOG_WARN("World events were disabled with %zu active event(s); cancelling them so recruited bots can return",
                activeInstanceIds.size());
            for (uint32 instanceId : activeInstanceIds)
                StopEvent(instanceId, nullptr);
        }

        // Recovery must keep running even when administrators disable creation
        // of new events. This is intentionally throttled to the normal module
        // update interval.
        m_lastUpdateTime += diff;
        if (m_lastUpdateTime >= m_updateIntervalMs)
        {
            m_lastUpdateTime = 0;
            ProcessBotReturnContexts();
        }
        return;
    }

    // Issue #18: Flush buffered debug log messages to GMs (batched, max once per second)
    m_logToGameAccumMs += diff;
    if (m_logToGameAccumMs >= 1000 && !m_pendingGameLogs.empty())
    {
        m_logToGameAccumMs = 0;
        WorldSessionMgr::SessionMap const& sessions = sWorldSessionMgr->GetAllSessions();
        for (auto const& pair : sessions)
        {
            if (WorldSession* session = pair.second)
            {
                if (Player* player = session->GetPlayer())
                {
                    if (player->IsGameMaster() || session->GetSecurity() >= SEC_GAMEMASTER)
                    {
                        for (const std::string& msg : m_pendingGameLogs)
                            ChatHandler(session).SendSysMessage(msg.c_str());
                    }
                }
            }
        }
        m_pendingGameLogs.clear();
    }

    // Process pending scheduled defense events (from guard system)
    ProcessScheduledDefenseEvents(diff);

    m_lastUpdateTime += diff;

    if (m_lastUpdateTime < m_updateIntervalMs)
        return;

    uint32 elapsed = m_lastUpdateTime;
    EC_LOG_TRACE("Update tick (diff: %u ms, active events: %zu)", diff, m_activeEvents.size());

    m_lastUpdateTime = 0;

    // Issue #10: Throttle schedule DB query to every 30s instead of every tick
    m_lastScheduleCheckMs += elapsed;
    if (m_lastScheduleCheckMs >= m_scheduleCheckIntervalMs)
    {
        m_lastScheduleCheckMs = 0;
        CheckScheduledEvents();
    }

    // Issue #6: Age the defender count cache
    m_defenderCountCacheAge += elapsed;

    // Persistent settlement economy advances independently of active invasions.
    ProcessSettlementEconomy(elapsed);

    // Update active events
    UpdateActiveEvents(elapsed);

    // Return bots whose event has ended. This also recovers durable checkpoints
    // left behind by a server restart, including bots that log in later.
    ProcessBotReturnContexts();
}

void WorldEventManager::ProcessScheduledDefenseEvents(uint32 diff)
{
    if (m_scheduledEvents.empty())
        return;

    std::vector<uint32> toRemove;
    std::vector<ScheduledEvent> toStart;

    for (auto& [scheduleId, scheduled] : m_scheduledEvents)
    {
        if (scheduled.remainingMs <= diff)
        {
            // Time's up - start the event
            toStart.push_back(scheduled);
            toRemove.push_back(scheduleId);
        }
        else
        {
            scheduled.remainingMs -= diff;
        }
    }

    // Start triggered events
    for (const auto& scheduled : toStart)
    {
        EC_LOG_INFO("Scheduled defense event #%u triggered for zone %u (faction %u)",
            scheduled.scheduleId, scheduled.zoneId, scheduled.faction);

        // Find a defense event for this zone/faction
        uint32 eventId = FindDefenseEventForZone(scheduled.zoneId, scheduled.faction);

        if (eventId > 0)
        {
            // Start the defense event
            if (StartEvent(eventId))
            {
                EC_LOG_INFO("Started defense event %u for zone %u", eventId, scheduled.zoneId);
            }
            else
            {
                EC_LOG_WARN("Failed to start defense event %u for zone %u", eventId, scheduled.zoneId);

                // Fallback broadcast
                std::string factionName = (scheduled.faction == 1) ? "Alliance" : "Horde";
                std::stringstream ss;
                ss << "|cffff0000[EC World Events]|r Elite " << factionName
                   << " forces have arrived to reclaim the zone!";
                BroadcastEventMessage(ss.str());
            }
        }
        else
        {
            EC_LOG_WARN("No defense event found for zone %u (faction %u), broadcasting only",
                scheduled.zoneId, scheduled.faction);

            // No matching event - just broadcast
            std::string factionName = (scheduled.faction == 1) ? "Alliance" : "Horde";
            std::stringstream ss;
            ss << "|cffff0000[EC World Events]|r Elite " << factionName
               << " forces have arrived to reclaim the zone!";
            BroadcastEventMessage(ss.str());
        }
    }

    // Remove processed events
    for (uint32 id : toRemove)
    {
        m_scheduledEvents.erase(id);
    }
}

uint32 WorldEventManager::FindDefenseEventForZone(uint32 zoneId, uint8 faction) const
{
    EC_LOG_DEBUG("FindDefenseEventForZone: zoneId=%u, faction=%u", zoneId, faction);

    // Priority 1: Zone-specific defense event (zone_id matches exactly)
    for (const auto& [id, def] : m_eventDefinitions)
    {
        if (def.type == EventType::DEFENSE && def.location.zoneId == zoneId)
        {
            EC_LOG_DEBUG("  Found zone-specific event: %u (%s)", id, def.name.c_str());
            return id;
        }
    }

    // Priority 2: Faction-wide default (zone_id = 0, defender_faction matches)
    // Faction mapping: 1=Alliance, 2=Horde (matching ScheduleDefenseEvent convention)
    EventFaction targetFaction = (faction == 1) ? EventFaction::ALLIANCE : EventFaction::HORDE;

    for (const auto& [id, def] : m_eventDefinitions)
    {
        if (def.type == EventType::DEFENSE &&
            def.location.zoneId == 0 &&
            def.defenderFaction == targetFaction)
        {
            EC_LOG_DEBUG("  Found faction-wide fallback event: %u (%s)", id, def.name.c_str());
            return id;
        }
    }

    EC_LOG_DEBUG("  No matching defense event found");
    return 0;
}

void WorldEventManager::CheckScheduledEvents()
{
    EC_LOG_TRACE("Checking scheduled events...");

    // A due schedule cannot start while every event slot is occupied. Return
    // before querying or calling StartEvent, and log only on the capacity state
    // transition instead of emitting the same pair of warnings every 30s.
    if (m_activeEvents.size() >= m_maxActiveEvents)
    {
        if (!m_scheduleCapacityDeferred)
        {
            EC_LOG_INFO("Scheduled event starts deferred while all slots are occupied (%zu/%u)",
                m_activeEvents.size(), m_maxActiveEvents);
            m_scheduleCapacityDeferred = true;
        }
        return;
    }

    if (m_scheduleCapacityDeferred)
    {
        EC_LOG_INFO("Scheduled event capacity is available again (%zu/%u); resuming due schedules",
            m_activeEvents.size(), m_maxActiveEvents);
        m_scheduleCapacityDeferred = false;
    }

    const uint32 retryMinutes = std::max<uint32>(1, m_eventStaggerMinutesMin);
    auto deferSchedule = [retryMinutes](uint32 scheduleId)
    {
        WorldDatabase.Execute(Acore::StringFormat(
            "UPDATE ec_world_event_schedules SET next_run = DATE_ADD(NOW(), INTERVAL {} MINUTE) WHERE id = {}",
            retryMinutes, scheduleId));
    };

    // Query schedules that are due to run
    QueryResult result = WorldDatabase.Query(
        "SELECT s.id, s.event_id, s.schedule_type, s.interval_minutes, s.cron_expression, s.one_time_date, s.last_run "
        "FROM ec_world_event_schedules s "
        "WHERE s.enabled = 1 AND (s.next_run IS NULL OR s.next_run <= NOW())");

    if (!result)
    {
        EC_LOG_TRACE("No scheduled events due");
        return;
    }

    // Collect all due events first
    struct DueEvent
    {
        uint32 scheduleId;
        uint32 eventId;
        uint8 scheduleType;
        uint32 intervalMinutes;
    };
    std::vector<DueEvent> dueEvents;

    do
    {
        Field* fields = result->Fetch();
        DueEvent due;
        due.scheduleId = fields[0].Get<uint32>();
        due.eventId = fields[1].Get<uint32>();
        due.scheduleType = fields[2].Get<uint8>();
        due.intervalMinutes = fields[3].Get<uint32>();

        // Check if this event ID is already active (key is instanceId, not eventId)
        bool alreadyActive = false;
        for (auto const& activePair : m_activeEvents)
        {
            if (activePair.second.eventId == due.eventId &&
                activePair.second.state == EventState::ACTIVE)
            {
                alreadyActive = true;
                break;
            }
        }

        if (alreadyActive)
        {
            deferSchedule(due.scheduleId);
            EC_LOG_INFO("Event %u is already active; schedule %u deferred by %u minute(s)",
                due.eventId, due.scheduleId, retryMinutes);
            continue;
        }

        dueEvents.push_back(due);
    } while (result->NextRow());

    if (dueEvents.empty())
        return;

    // Shuffle so the same event doesn't always go first, and set up stagger RNG
    std::random_device rd;
    std::mt19937 rng(rd());

    if (dueEvents.size() > 1)
    {
        std::shuffle(dueEvents.begin(), dueEvents.end(), rng);
    }

    // Start only the FIRST event now; defer the rest with random stagger
    bool startedOne = false;
    std::uniform_int_distribution<uint32> staggerDist(m_eventStaggerMinutesMin, m_eventStaggerMinutesMax);

    for (size_t i = 0; i < dueEvents.size(); ++i)
    {
        const DueEvent& due = dueEvents[i];

        if (!startedOne)
        {
            // Start this one immediately
            if (StartEvent(due.eventId, nullptr))
            {
                EC_LOG_INFO("Scheduled event %u started (schedule %u)", due.eventId, due.scheduleId);
                startedOne = true;

                // Update last_run and calculate next_run for this event's schedule
                std::string nextRunUpdate;
                switch (due.scheduleType)
                {
                    case 0: // Interval
                        nextRunUpdate = Acore::StringFormat(
                            "UPDATE ec_world_event_schedules SET last_run = NOW(), next_run = DATE_ADD(NOW(), INTERVAL {} MINUTE) WHERE id = {}",
                            due.intervalMinutes, due.scheduleId);
                        break;
                    case 1: // Cron
                        nextRunUpdate = Acore::StringFormat(
                            "UPDATE ec_world_event_schedules SET last_run = NOW(), next_run = DATE_ADD(NOW(), INTERVAL 24 HOUR) WHERE id = {}",
                            due.scheduleId);
                        break;
                    case 2: // OneTime
                        nextRunUpdate = Acore::StringFormat(
                            "UPDATE ec_world_event_schedules SET last_run = NOW(), enabled = 0 WHERE id = {}",
                            due.scheduleId);
                        break;
                    default:
                        EC_LOG_WARN("Unknown schedule type %u for schedule %u", due.scheduleType, due.scheduleId);
                        continue;
                }
                WorldDatabase.Execute(nextRunUpdate);
            }
            else
            {
                deferSchedule(due.scheduleId);
                EC_LOG_WARN("Failed to start scheduled event %u (schedule %u); retry deferred by %u minute(s)",
                    due.eventId, due.scheduleId, retryMinutes);
            }
        }
        else
        {
            // Defer this event: push its next_run forward by a random stagger
            uint32 staggerMinutes = staggerDist(rng);
            EC_LOG_INFO("Staggering event %u (schedule %u) by %u minutes to avoid simultaneous starts",
                due.eventId, due.scheduleId, staggerMinutes);

            std::string deferUpdate = Acore::StringFormat(
                "UPDATE ec_world_event_schedules SET next_run = DATE_ADD(NOW(), INTERVAL {} MINUTE) WHERE id = {}",
                staggerMinutes, due.scheduleId);
            WorldDatabase.Execute(deferUpdate);
        }
    }
}

void WorldEventManager::UpdateActiveEvents(uint32 diff)
{
    if (m_activeEvents.empty())
        return;

    EC_LOG_TRACE("Updating %zu active events...", m_activeEvents.size());

    auto now = std::chrono::system_clock::now();
    std::vector<uint32> eventsToEnd;

    for (auto& pair : m_activeEvents)
    {
        ActiveEvent& event = pair.second;

        if (event.state != EventState::ACTIVE)
        {
            EC_LOG_TRACE("  Event %u is not active (state: %s), skipping",
                pair.first, EventStateToString(event.state).c_str());
            continue;
        }

        // Update phase timers (handles phase transitions)
        UpdatePhaseTimers(event, diff);

        // ENDING is a cleanup/celebration phase: the battle has already been
        // announced as over, so never recruit, reinforce, or respawn combatants.
        if (event.phase != EventPhase::ENDING)
        {
            // Process auto-escalate if enabled (checks defender count in zone)
            const EventDefinition* defForEscalate = GetEventDefinition(event.eventId);
            if (defForEscalate)
            {
                EvaluateRaidTownAttrition(event, *defForEscalate);
                if (event.phase != EventPhase::ENDING)
                    ProcessAutoEscalate(event, *defForEscalate, pair.first, diff);
            }

            // Update building capture timers (AV-style towers/bunkers)
            UpdateBuildingCaptures(event, diff);

            // Process reinforcement arrivals (zeppelin, boat, portal) + deferred immediate spawns
            ProcessReinforcements(event, diff);

            // Keep spawned event NPCs actively looking for enemies so they don't idle.
            ProcessEventCreatureCombat(event, diff);

            // Process deferred bot teleports (batched to avoid freezing)
            ProcessPendingBotTeleports(event);

            // Far teleports reset PlayerbotAI after worldport acknowledgement.
            // Apply temporary event strategies only after that reset completes.
            ProcessEventBotCombat(event);

            // Process building guard respawns (barracks-style)
            ProcessBuildingGuardRespawns(event, diff);
        }

        // Check if event duration has expired
        auto remaining = std::chrono::duration_cast<std::chrono::seconds>(event.endTime - now);
        EC_LOG_TRACE("  Event %u [%s]: %lld seconds remaining, %zu participants, %u kills",
            pair.first, EventPhaseToString(event.phase).c_str(),
            remaining.count(), event.participants.size(), event.totalKills);

        if (now >= event.endTime)
        {
            EC_LOG_INFO("Event %u has expired, marking for completion", pair.first);

            // Emit ending notification to realtime bridge
            const EventDefinition* expiringDef = GetEventDefinition(event.eventId);
            if (expiringDef)
            {
                sWorldEventBridge->EmitEventEnding(
                    event.eventId,
                    pair.first,
                    expiringDef->name
                );
            }

            eventsToEnd.push_back(pair.first);
        }
    }

    // End expired events
    for (uint32 instanceId : eventsToEnd)
    {
        auto it = m_activeEvents.find(instanceId);
        if (it != m_activeEvents.end())
        {
            EC_LOG_INFO("Completing event instance %u...", instanceId);

            it->second.state = EventState::COMPLETED;

            // Determine final outcome if not already set (e.g. no ENDING phase, or no early victory)
            if (it->second.outcome == EventOutcome::PENDING)
                it->second.outcome = DetermineOutcome(it->second);

            const EventDefinition* def = GetEventDefinition(it->second.eventId);
            if (def)
            {
                // Build outcome-aware completion message
                std::string outcomeMsg;
                if (it->second.outcome == EventOutcome::ATTACKER_WIN)
                    outcomeMsg = " " + EventFactionToString(def->attackerFaction) + " victory!";
                else if (it->second.outcome == EventOutcome::DEFENDER_WIN)
                    outcomeMsg = " " + EventFactionToString(def->defenderFaction) + " holds the line!";
                else
                    outcomeMsg = " The battle ends in a stalemate!";

                std::string msg = "|cff00ff00[EC World Events]|r The " + def->name + " has ended!" + outcomeMsg;
                BroadcastEventMessage(msg);
                EC_LOG_INFO("Event '%s' completed - outcome: %s, %u total kills, %zu participants",
                    def->name.c_str(), EventOutcomeToString(it->second.outcome).c_str(),
                    it->second.totalKills, it->second.participants.size());
                EC_LOG_INFO("  Scores: attacker=%d, defender=%d",
                    it->second.attackerScore, it->second.defenderScore);

                // AnnounceToWorld: Server-wide broadcast on event end
                if (m_announceToWorld)
                {
                    std::string announceMsg = "|cff00ff00[World Event]|r |cffffd700" + def->name +
                        "|r has concluded!" + outcomeMsg + " |cff00ffff" + std::to_string(it->second.totalKills) +
                        " kills|r, |cff00ffff" + std::to_string(it->second.participants.size()) + " participants|r.";
                    sWorldSessionMgr->SendServerMessage(SERVER_MSG_STRING, announceMsg.c_str());
                }
            }

            // Notify AliveWorld before cleanup with actual outcome
#ifdef MOD_ALIVE_WORLD
            {
                // For RAID: "successful" means the attackers won (the raid succeeded)
                // For DEFENSE: "successful" means the defenders held
                bool successful = false;
                if (def)
                {
                    if (def->type == EventType::RAID)
                        successful = (it->second.outcome == EventOutcome::ATTACKER_WIN);
                    else if (def->type == EventType::DEFENSE)
                        successful = (it->second.outcome == EventOutcome::DEFENDER_WIN);
                    else
                        successful = (it->second.outcome != EventOutcome::DRAW);
                }
                NotifyAliveWorldEventEnded(it->second, successful);
            }
#endif

            DistributeRewards(it->second);

            // Cleanup all spawned entities
            DespawnAllEventGameObjects(it->second);
            DespawnAllEventCreatures(it->second);
            DespawnEventBots(it->second);

            LogEventToDatabase(it->second);

            // Emit to realtime bridge
            if (def)
            {
                auto duration = std::chrono::duration_cast<std::chrono::seconds>(
                    std::chrono::system_clock::now() - it->second.startTime
                ).count();
                sWorldEventBridge->EmitEventEnded(
                    it->second.eventId,
                    instanceId,
                    def->name,
                    static_cast<uint32>(duration),
                    static_cast<uint32>(it->second.participants.size()),
                    EventOutcomeToString(it->second.outcome)
                );
            }

            // Set cooldown for this event
            {
                uint32 cooldownMin = 0;
                const EventDefinition* cooldownDef = GetEventDefinition(it->second.eventId);
                if (cooldownDef && cooldownDef->cooldownMinutes > 0)
                    cooldownMin = cooldownDef->cooldownMinutes;
                else
                    cooldownMin = m_eventCooldownMinutes;

                if (cooldownMin > 0)
                {
                    m_eventCooldowns[it->second.eventId] = std::chrono::system_clock::now()
                        + std::chrono::minutes(cooldownMin);
                    EC_LOG_INFO("Event %u placed on %u minute cooldown", it->second.eventId, cooldownMin);
                }
            }

            m_activeEvents.erase(it);
            EC_LOG_INFO("Event instance %u removed from active list", instanceId);
        }
    }
}

// =============================================================================
// Event Control
// =============================================================================

bool WorldEventManager::StartEvent(uint32 eventId, Player* initiator)
{
    EC_LOG_INFO("========================================");
    EC_LOG_INFO("StartEvent called: eventId=%u, initiator=%s",
        eventId, initiator ? initiator->GetName().c_str() : "SYSTEM");

    auto defIt = m_eventDefinitions.find(eventId);
    if (defIt == m_eventDefinitions.end())
    {
        EC_LOG_ERROR("Event %u not found in definitions!", eventId);
        return false;
    }

    if (m_activeEvents.size() >= m_maxActiveEvents)
    {
        EC_LOG_WARN("Cannot start event %u - max active events reached (%zu/%u)",
            eventId, m_activeEvents.size(), m_maxActiveEvents);
        return false;
    }

    const EventDefinition& def = defIt->second;
    EC_LOG_INFO("Event definition found: '%s'", def.name.c_str());
    EC_LOG_DEBUG("  Type: %s", EventTypeToString(def.type).c_str());
    EC_LOG_DEBUG("  Location: %s (Map %u, Zone %u, Area %u)",
        def.location.name.c_str(), def.location.mapId, def.location.zoneId, def.location.areaId);
    EC_LOG_DEBUG("  Position: (%.2f, %.2f, %.2f)", def.location.x, def.location.y, def.location.z);
    EC_LOG_DEBUG("  Bots: %u - %u", def.minBots, def.maxBots);
    EC_LOG_DEBUG("  Duration: %u minutes", def.durationMinutes);
    EC_LOG_DEBUG("  Level Range: %u - %u", def.minLevel, def.maxLevel);
    EC_LOG_DEBUG("  Attacker Faction: %s", EventFactionToString(def.attackerFaction).c_str());
    EC_LOG_DEBUG("  Defender Faction: %s", EventFactionToString(def.defenderFaction).c_str());

    // Check if this event is already running
    for (const auto& pair : m_activeEvents)
    {
        if (pair.second.eventId == eventId)
        {
            EC_LOG_WARN("Event %u is already active (instance %u)", eventId, pair.first);
            return false;
        }
    }

    // Check if this event is on cooldown
    auto cooldownIt = m_eventCooldowns.find(eventId);
    if (cooldownIt != m_eventCooldowns.end())
    {
        auto now = std::chrono::system_clock::now();
        if (now < cooldownIt->second)
        {
            auto remainingSecs = std::chrono::duration_cast<std::chrono::seconds>(
                cooldownIt->second - now).count();
            EC_LOG_WARN("Event %u is on cooldown (%lld seconds remaining)", eventId, remainingSecs);
            return false;
        }
        // Cooldown expired, remove entry
        m_eventCooldowns.erase(cooldownIt);
    }

    // Create new active event
    ActiveEvent event;
    event.instanceId = m_nextInstanceId++;
    event.eventId = eventId;
    event.state = EventState::ACTIVE;
    event.phase = EventPhase::NONE;  // Will be set by TransitionToPhase
    event.startTime = std::chrono::system_clock::now();
    event.endTime = event.startTime + std::chrono::minutes(def.durationMinutes);
    event.phaseStartTime = event.startTime;
    event.phaseEndTime = event.startTime;
    event.totalKills = 0;
    event.escalationLevel = 0;
    event.lastWarningMinutes = UINT32_MAX;
    event.attackerScore = 0;
    event.defenderScore = 0;
    event.outcome = EventOutcome::PENDING;
    event.timeWithoutDefendersMs = 0;
    event.defenderCount = 0;

    EC_LOG_INFO("Created event instance %u", event.instanceId);
    EC_LOG_DEBUG("  Total duration: %u minutes", def.durationMinutes);
    EC_LOG_DEBUG("  Preparation time: %u minutes", def.preparationMinutes);
    EC_LOG_DEBUG("  Ending phase time: %u minutes", def.endingMinutes);
    EC_LOG_DEBUG("  Objects defined: %zu", def.objects.size());
    EC_LOG_DEBUG("  Creatures defined: %zu", def.creatures.size());

    // Emit starting notification to realtime bridge
    sWorldEventBridge->EmitEventStarting(
        eventId,
        event.instanceId,
        def.name
    );

    // AnnounceToWorld: Server-wide broadcast on event start
    if (m_announceToWorld)
    {
        std::string announceMsg = "|cffff6600[World Event]|r |cffffd700" + def.name +
            "|r has begun at |cff00ff00" + def.location.name + "|r! Defenders needed!";
        sWorldSessionMgr->SendServerMessage(SERVER_MSG_STRING, announceMsg.c_str());
        EC_LOG_INFO("World announcement sent: %s", def.name.c_str());
    }

    // Store the event first (before phase transition needs it)
    m_activeEvents[event.instanceId] = event;

    // Determine starting phase based on preparation time
    EventPhase startingPhase;
    if (def.preparationMinutes > 0)
    {
        startingPhase = EventPhase::PREPARING;
        EC_LOG_INFO("Starting in PREPARING phase (%u minutes)", def.preparationMinutes);
    }
    else
    {
        startingPhase = EventPhase::ACTIVE;
        EC_LOG_INFO("No preparation phase - starting directly in ACTIVE phase");
    }

    // Transition to the starting phase (this spawns objects/creatures and broadcasts)
    TransitionToPhase(m_activeEvents[event.instanceId], startingPhase);

    // Spawn bots (only during ACTIVE phase, or during PREPARING for defenders)
    if (startingPhase == EventPhase::ACTIVE || def.type == EventType::DEFENSE)
    {
        EC_LOG_INFO("Spawning event bots...");
        SpawnEventBots(m_activeEvents[event.instanceId]);
    }
    else
    {
        EC_LOG_INFO("Bots will spawn when ACTIVE phase begins");
    }

    // Auto-join Tier 1 bots that are already in the event zone (AliveWorld integration)
#ifdef MOD_ALIVE_WORLD
    if (sAliveWorldMgr->IsEnabled())
    {
        EC_LOG_INFO("AliveWorld integration: Auto-joining Tier 1 bots in zone %u", def.location.zoneId);
        AutoJoinTier1BotsInZone(m_activeEvents[event.instanceId], def);
    }
#endif

    // Emit to realtime bridge
    sWorldEventBridge->EmitEventStarted(
        eventId,
        event.instanceId,
        def.name,
        EventTypeToString(def.type),
        def.location.mapId,
        def.location.zoneId
    );

    EC_LOG_INFO("Event '%s' (Instance %u) started successfully in %s phase!",
        def.name.c_str(), event.instanceId, EventPhaseToString(startingPhase).c_str());
    EC_LOG_INFO("========================================");

    return true;
}

bool WorldEventManager::StopEvent(uint32 instanceId, Player* initiator)
{
    EC_LOG_INFO("StopEvent called: instanceId=%u, initiator=%s",
        instanceId, initiator ? initiator->GetName().c_str() : "SYSTEM");

    auto it = m_activeEvents.find(instanceId);
    if (it == m_activeEvents.end())
    {
        EC_LOG_WARN("Event instance %u not found in active events", instanceId);
        return false;
    }

    ActiveEvent& event = it->second;
    const EventDefinition* def = GetEventDefinition(event.eventId);

    EC_LOG_INFO("Stopping event: %s (Instance %u)",
        def ? def->name.c_str() : "Unknown", instanceId);
    EC_LOG_DEBUG("  State before: %s", EventStateToString(event.state).c_str());
    EC_LOG_DEBUG("  Total kills: %u", event.totalKills);
    EC_LOG_DEBUG("  Participants: %zu", event.participants.size());
    EC_LOG_DEBUG("  Escalation level: %u", event.escalationLevel);

    event.state = EventState::CANCELLED;

    if (def)
    {
        std::string msg = "|cffff9900[EC World Events]|r " + def->name + " has been cancelled.";
        BroadcastEventMessage(msg);

        // Emit to realtime bridge
        sWorldEventBridge->EmitEventCancelled(
            event.eventId,
            instanceId,
            def->name,
            initiator ? "Cancelled by GM" : "System shutdown"
        );
    }

    // Notify AliveWorld before cleanup
#ifdef MOD_ALIVE_WORLD
    NotifyAliveWorldEventEnded(event, false);  // Cancelled = not successful
#endif

    // Cleanup all spawned entities
    EC_LOG_INFO("Despawning all spawned entities...");
    DespawnAllEventGameObjects(event);
    DespawnAllEventCreatures(event);
    DespawnEventBots(event);

    EC_LOG_INFO("Logging to database...");
    LogEventToDatabase(event);

    // Half cooldown for cancelled events (prevent spam-starting)
    {
        const EventDefinition* cooldownDef = GetEventDefinition(event.eventId);
        uint32 fullCooldown = (cooldownDef && cooldownDef->cooldownMinutes > 0)
            ? cooldownDef->cooldownMinutes : m_eventCooldownMinutes;
        uint32 cooldownMin = fullCooldown / 2;
        if (cooldownMin > 0)
        {
            m_eventCooldowns[event.eventId] = std::chrono::system_clock::now()
                + std::chrono::minutes(cooldownMin);
            EC_LOG_INFO("Cancelled event %u placed on %u minute cooldown (half of %u)",
                event.eventId, cooldownMin, fullCooldown);
        }
    }

    m_activeEvents.erase(it);
    EC_LOG_INFO("Event instance %u stopped and removed", instanceId);

    return true;
}

bool WorldEventManager::EscalateEvent(uint32 instanceId)
{
    EC_LOG_INFO("EscalateEvent called: instanceId=%u", instanceId);

    auto it = m_activeEvents.find(instanceId);
    if (it == m_activeEvents.end())
    {
        EC_LOG_WARN("Event instance %u not found", instanceId);
        return false;
    }

    ActiveEvent& event = it->second;
    const EventDefinition* def = GetEventDefinition(event.eventId);
    if (!def)
    {
        EC_LOG_ERROR("Event definition not found for instance %u (eventId: %u)",
            instanceId, event.eventId);
        return false;
    }

    uint32 oldLevel = event.escalationLevel;
    event.escalationLevel++;

    EC_LOG_INFO("Event '%s' escalated: %u -> %u", def->name.c_str(), oldLevel, event.escalationLevel);

    // Spawn additional bots based on escalation level
    uint32 additionalBots = (def->maxBots - def->minBots) / 3;
    EC_LOG_DEBUG("Spawning %u additional bots for escalation", additionalBots);

#ifdef MOD_PLAYERBOTS
    // Recruit more bots for escalation
    if (additionalBots > 0)
    {
        SpawnAdditionalBots(event, additionalBots);
    }
#else
    EC_LOG_DEBUG("MOD_PLAYERBOTS not available - cannot spawn additional bots");
#endif

    std::stringstream ss;
    ss << "|cffff0000[EC World Events]|r " << def->name << " has escalated! "
       << "Reinforcements have arrived! (Level " << event.escalationLevel << ")";
    BroadcastEventMessage(ss.str());

    return true;
}

// =============================================================================
// Scheduled Events (for Guard System Elite Response)
// =============================================================================

uint32 WorldEventManager::ScheduleDefenseEvent(uint32 zoneId, uint8 faction, uint32 delayMs)
{
    EC_LOG_INFO("ScheduleDefenseEvent called: zoneId=%u, faction=%u, delay=%ums",
        zoneId, faction, delayMs);

    // Create scheduled event
    ScheduledEvent scheduled;
    scheduled.scheduleId = m_nextScheduleId++;
    scheduled.zoneId = zoneId;
    scheduled.faction = faction;
    scheduled.remainingMs = delayMs;
    scheduled.isDefenseEvent = true;

    m_scheduledEvents[scheduled.scheduleId] = scheduled;

    // Calculate time for logging
    uint32 delayMinutes = delayMs / 60000;
    uint32 delaySeconds = (delayMs % 60000) / 1000;

    // Emit to realtime bridge
    std::string factionName = (faction == 1) ? "Alliance" : "Horde";
    uint64_t startTime = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count() + (delayMs / 1000);
    sWorldEventBridge->EmitEventScheduled(
        0,  // eventId not known yet (will be selected when triggered)
        scheduled.scheduleId,
        "Zone Defense Response",
        "DEFENSE",
        0,  // mapId not known
        zoneId,
        startTime
    );

    EC_LOG_INFO("Scheduled defense event #%u for zone %u in %u min %u sec",
        scheduled.scheduleId, zoneId, delayMinutes, delaySeconds);

    // Broadcast warning to players (reuse factionName from above)
    std::stringstream ss;
    ss << "|cffff0000[EC World Events]|r Elite " << factionName
       << " forces have been dispatched! ETA: " << delayMinutes << " minutes.";
    BroadcastEventMessage(ss.str());

    return scheduled.scheduleId;
}

void WorldEventManager::CancelScheduledEvent(uint32 scheduledId)
{
    auto it = m_scheduledEvents.find(scheduledId);
    if (it == m_scheduledEvents.end())
    {
        EC_LOG_WARN("Cannot cancel scheduled event %u - not found", scheduledId);
        return;
    }

    EC_LOG_INFO("Cancelled scheduled event #%u for zone %u",
        scheduledId, it->second.zoneId);

    m_scheduledEvents.erase(it);
}

// =============================================================================
// Bot Spawning/Despawning (Playerbots Integration)
// =============================================================================


// =============================================================================
// Event Bots, Objects, Creatures, and Reinforcement Spawning
// =============================================================================

#include "ECWorldEventManager.Spawning.inl"

// =============================================================================
// Building Capture and Event NPC Combat
// =============================================================================

#include "ECWorldEventManager.CaptureCombat.inl"

EventOutcome WorldEventManager::DetermineOutcome(const ActiveEvent& event) const
{
    // If outcome was already determined (e.g., early victory), keep it
    if (event.outcome != EventOutcome::PENDING)
        return event.outcome;

    const EventDefinition* def = GetEventDefinition(event.eventId);
    if (!def)
        return EventOutcome::DRAW;

    // No capturable buildings → fall back to score comparison
    if (event.buildings.empty())
    {
        if (event.attackerScore > event.defenderScore)
            return EventOutcome::ATTACKER_WIN;
        if (event.defenderScore > event.attackerScore)
            return EventOutcome::DEFENDER_WIN;
        // No objectives captured and no score difference:
        // For RAID/DEFENSE, timer expiring with nothing happening = defender held
        if (def->type == EventType::RAID || def->type == EventType::DEFENSE)
            return EventOutcome::DEFENDER_WIN;
        return EventOutcome::DRAW;
    }

    // Building-based outcome: compare faction scores from captures
    if (event.attackerScore > event.defenderScore)
        return EventOutcome::ATTACKER_WIN;
    if (event.defenderScore > event.attackerScore)
        return EventOutcome::DEFENDER_WIN;

    // Equal scores at timeout — defender holds (they survived the assault)
    if (def->type == EventType::RAID || def->type == EventType::DEFENSE)
        return EventOutcome::DEFENDER_WIN;

    return EventOutcome::DRAW;
}

void WorldEventManager::CheckEarlyVictory(ActiveEvent& event, uint32 instanceId)
{
    // Only check during ACTIVE phase
    if (event.phase != EventPhase::ACTIVE)
        return;

    // Already decided
    if (event.outcome != EventOutcome::PENDING)
        return;

    // Need buildings to check
    if (event.buildings.empty())
        return;

    // Check if ALL capturable buildings have been destroyed
    for (const auto& pair : event.buildings)
    {
        if (pair.second.state != BuildingState::DESTROYED)
            return;  // At least one building still standing
    }

    // All buildings destroyed — determine winner and skip to ENDING
    event.outcome = DetermineOutcome(event);
    // DetermineOutcome still returns PENDING-aware result, but since buildings are all destroyed
    // the score comparison will pick the faction that destroyed more. Force re-evaluate:
    // Since all buildings are gone, whichever side captured more points wins.
    if (event.outcome == EventOutcome::PENDING)
    {
        if (event.attackerScore >= event.defenderScore)
            event.outcome = EventOutcome::ATTACKER_WIN;
        else
            event.outcome = EventOutcome::DEFENDER_WIN;
    }

    const EventDefinition* def = GetEventDefinition(event.eventId);
    if (!def)
        return;

    std::string winnerFaction = (event.outcome == EventOutcome::ATTACKER_WIN)
        ? EventFactionToString(def->attackerFaction)
        : EventFactionToString(def->defenderFaction);

    EC_LOG_INFO("EARLY VICTORY in event '%s' (Instance %u)! All buildings destroyed. Winner: %s",
        def->name.c_str(), instanceId, winnerFaction.c_str());

    std::stringstream msg;
    msg << "|cffff0000[EC World Events]|r All objectives in " << def->name
        << " have been completed! " << winnerFaction << " is victorious!";
    BroadcastEventMessage(msg.str());

    // Skip to ENDING phase for victory celebration / cleanup
    if (def->endingMinutes > 0)
    {
        TransitionToPhase(event, EventPhase::ENDING);
    }
    // If no ending phase, event will complete on next UpdateActiveEvents cycle
}

// =============================================================================
// Phase Management
// =============================================================================

void WorldEventManager::TransitionToPhase(ActiveEvent& event, EventPhase newPhase)
{
    const EventDefinition* def = GetEventDefinition(event.eventId);
    if (!def)
        return;

    EventPhase oldPhase = event.phase;
    EC_LOG_INFO("Event '%s' (Instance %u) transitioning: %s -> %s",
        def->name.c_str(), event.instanceId,
        EventPhaseToString(oldPhase).c_str(),
        EventPhaseToString(newPhase).c_str());

    // Once an event is announced as over, remove every combat-phase entity.
    // Most legacy rows use despawn_phase=0, so selective cleanup left large
    // fortifications and burning buildings in town throughout ENDING.
    if (newPhase == EventPhase::ENDING)
    {
        DespawnAllEventGameObjects(event);
        event.buildings.clear();
        DespawnAllEventCreatures(event);
        DespawnEventBots(event);
    }
    else if (oldPhase != EventPhase::NONE)
    {
        DespawnEventGameObjects(event, oldPhase);
        DespawnEventCreatures(event, oldPhase);
    }

    // Update phase
    event.phase = newPhase;
    event.phaseStartTime = std::chrono::system_clock::now();
    event.lastWarningMinutes = UINT32_MAX;  // Reset warning tracker

    // Calculate phase end time
    switch (newPhase)
    {
        case EventPhase::PREPARING:
            event.phaseEndTime = event.phaseStartTime + std::chrono::minutes(def->preparationMinutes);
            break;
        case EventPhase::ACTIVE:
            // Active phase lasts until event.endTime minus ending phase duration
            if (def->endingMinutes > 0)
            {
                event.phaseEndTime = event.endTime - std::chrono::minutes(def->endingMinutes);
            }
            else
            {
                event.phaseEndTime = event.endTime;
            }
            break;
        case EventPhase::ENDING:
            // Early victory may enter ENDING long before the original deadline.
            // Clamp both timers so the celebration lasts only endingMinutes.
            event.phaseEndTime = event.phaseStartTime + std::chrono::minutes(def->endingMinutes);
            event.endTime = event.phaseEndTime;
            break;
        default:
            event.phaseEndTime = event.phaseStartTime;
            break;
    }

    // Log what we expect for this phase
    {
        uint8 targetPhase = static_cast<uint8>(newPhase);
        uint32 immediateCount = 0, delayedCount = 0;
        for (const EventCreature& c : def->creatures)
        {
            if (c.spawnPhase == targetPhase)
            {
                if (c.spawnDelaySeconds > 0)
                    ++delayedCount;
                else
                    ++immediateCount;
            }
        }
        EC_LOG_INFO("=== PHASE %s: Expecting %u immediate creature spawns + %u delayed reinforcements ===",
            EventPhaseToString(newPhase).c_str(), immediateCount, delayedCount);
    }

    // Determine outcome before spawning ENDING phase objects (they may be conditional on who won)
    if (newPhase == EventPhase::ENDING && event.outcome == EventOutcome::PENDING)
    {
        event.outcome = DetermineOutcome(event);
        EC_LOG_INFO("Event outcome determined: %s (attacker=%d, defender=%d)",
            EventOutcomeToString(event.outcome).c_str(), event.attackerScore, event.defenderScore);
    }

#ifdef MOD_ALIVE_WORLD
    if (newPhase == EventPhase::ENDING)
    {
        bool successful = false;
        if (def->type == EventType::RAID)
            successful = (event.outcome == EventOutcome::ATTACKER_WIN);
        else if (def->type == EventType::DEFENSE)
            successful = (event.outcome == EventOutcome::DEFENDER_WIN);
        else
            successful = (event.outcome != EventOutcome::DRAW);
        NotifyAliveWorldEventEnded(event, successful);
    }
#endif

    // Spawn objects/creatures for new phase
    SpawnEventGameObjects(event, newPhase);
    SpawnEventCreatures(event, newPhase);

    // Set up reinforcement waves (zeppelin, boat, portal arrivals)
    SetupReinforcementWaves(event, newPhase);

    if (newPhase == EventPhase::ACTIVE)
        SnapshotRaidTownPopulation(event, *def);

    // Spawn bots when entering ACTIVE phase (if not already spawned)
    if (newPhase == EventPhase::ACTIVE && event.spawnedBots.empty())
    {
        EC_LOG_INFO("ACTIVE phase started - spawning event bots");
        SpawnEventBots(event);
    }

    // Broadcast phase transition messages
    std::stringstream ss;
    switch (newPhase)
    {
        case EventPhase::PREPARING:
            ss << "|cffff9900[EC World Events]|r " << def->name << " is being prepared at " << def->location.name << "! ";
            ss << "The battle begins in " << def->preparationMinutes << " minutes!";
            break;
        case EventPhase::ACTIVE:
            ss << "|cffff0000[EC World Events]|r " << def->name << " has begun! The battle is underway!";
            break;
        case EventPhase::ENDING:
        {
            ss << "|cff00ff00[EC World Events]|r " << def->name << " is over! ";
            if (event.outcome == EventOutcome::ATTACKER_WIN)
                ss << "|cffff0000" << EventFactionToString(def->attackerFaction) << " is victorious!|r";
            else if (event.outcome == EventOutcome::DEFENDER_WIN)
                ss << "|cff00ff00" << EventFactionToString(def->defenderFaction) << " has held the line!|r";
            else
                ss << "The battle ends in a stalemate!";
            break;
        }
        default:
            break;
    }

    if (!ss.str().empty())
    {
        BroadcastEventMessage(ss.str());
    }

    // Emit to realtime bridge (could add EmitPhaseChanged event here)
    EC_LOG_INFO("Phase transition complete. Phase ends at %s",
        std::to_string(std::chrono::duration_cast<std::chrono::seconds>(
            event.phaseEndTime.time_since_epoch()).count()).c_str());
}

void WorldEventManager::UpdatePhaseTimers(ActiveEvent& event, uint32 /*diff*/)
{
    if (event.state != EventState::ACTIVE)
        return;

    const EventDefinition* def = GetEventDefinition(event.eventId);
    if (!def)
        return;

    auto now = std::chrono::system_clock::now();

    // Check if current phase should end
    if (now >= event.phaseEndTime)
    {
        switch (event.phase)
        {
            case EventPhase::PREPARING:
                // Transition to ACTIVE phase
                TransitionToPhase(event, EventPhase::ACTIVE);
                break;
            case EventPhase::ACTIVE:
                if (def->endingMinutes > 0)
                {
                    // Transition to ENDING phase
                    TransitionToPhase(event, EventPhase::ENDING);
                }
                // else: event completion handled by UpdateActiveEvents
                break;
            case EventPhase::ENDING:
                // Event completion handled by UpdateActiveEvents
                break;
            default:
                break;
        }
        return;
    }

    // Broadcast warnings during phases
    auto remaining = std::chrono::duration_cast<std::chrono::minutes>(event.phaseEndTime - now);
    uint32 remainingMinutes = static_cast<uint32>(remaining.count());

    // Broadcast at specific intervals (5, 3, 1 minute warnings)
    if (remainingMinutes != event.lastWarningMinutes)
    {
        if (remainingMinutes == 5 || remainingMinutes == 3 || remainingMinutes == 1)
        {
            BroadcastPhaseWarning(event, remainingMinutes);
        }
        event.lastWarningMinutes = remainingMinutes;
    }
}

void WorldEventManager::BroadcastPhaseWarning(ActiveEvent& event, uint32 remainingMinutes)
{
    const EventDefinition* def = GetEventDefinition(event.eventId);
    if (!def)
        return;

    std::stringstream ss;
    switch (event.phase)
    {
        case EventPhase::PREPARING:
            ss << "|cffff9900[EC World Events]|r " << def->name << " begins in "
               << remainingMinutes << " minute" << (remainingMinutes != 1 ? "s" : "") << "!";
            break;
        case EventPhase::ACTIVE:
            if (def->endingMinutes > 0)
            {
                ss << "|cffff0000[EC World Events]|r " << def->name << ": "
                   << remainingMinutes << " minute" << (remainingMinutes != 1 ? "s" : "")
                   << " until final phase!";
            }
            break;
        case EventPhase::ENDING:
            ss << "|cff00ff00[EC World Events]|r " << def->name << ": "
               << remainingMinutes << " minute" << (remainingMinutes != 1 ? "s" : "")
               << " remaining!";
            break;
        default:
            break;
    }

    if (!ss.str().empty())
    {
        BroadcastEventMessage(ss.str());
    }
}

// =============================================================================
// Auto-Escalate System
// =============================================================================

uint32 WorldEventManager::CountDefendersInZone(const ActiveEvent& event, const EventDefinition& def) const
{
    (void)event;
    uint32 count = 0;

    // Get the defender faction for this event
    EventFaction defenderFaction = def.defenderFaction;
    if (defenderFaction == EventFaction::NEUTRAL)
    {
        // No specific defenders - can't count
        return 0;
    }

    // Random playerbots do not appear in WorldSessionMgr::GetAllSessions().
    // Count the map's PlayerList so both real players and playerbots are
    // recognized, while still excluding dead or half-removed defenders.
    Map* map = sMapMgr->FindBaseNonInstanceMap(def.location.mapId);
    if (!map)
        return 0;

    Map::PlayerList const& players = map->GetPlayers();
    for (Map::PlayerList::const_iterator itr = players.begin(); itr != players.end(); ++itr)
    {
        Player* player = itr->GetSource();
        if (!player || !player->IsInWorld() || player->IsDuringRemoveFromWorld() || !player->IsAlive())
            continue;

        // Check if player is on the event map and in its zone.
        if (player->GetMapId() != def.location.mapId || player->GetZoneId() != def.location.zoneId)
            continue;

        // Check if player belongs to defender faction
        uint8 playerFaction = player->GetTeamId();  // TEAM_ALLIANCE = 0, TEAM_HORDE = 1
        bool isDefender = false;

        if (defenderFaction == EventFaction::ALLIANCE && playerFaction == TEAM_ALLIANCE)
            isDefender = true;
        else if (defenderFaction == EventFaction::HORDE && playerFaction == TEAM_HORDE)
            isDefender = true;

        if (isDefender)
            count++;
    }

    return count;
}

void WorldEventManager::ProcessAutoEscalate(ActiveEvent& event, const EventDefinition& def, uint32 instanceId, uint32 diff)
{
    if (!m_autoEscalateEnabled)
        return;

    // Only process during active phase
    if (event.phase != EventPhase::ACTIVE)
        return;

    // Count current defenders in zone
    uint32 defenderCount = CountDefendersInZone(event, def);
    event.defenderCount = defenderCount;

    if (defenderCount == 0)
    {
        // No defenders - accumulate time
        event.timeWithoutDefendersMs += diff;

        uint32 thresholdMs = m_autoEscalateMinutes * 60 * 1000;
        if (event.timeWithoutDefendersMs >= thresholdMs)
        {
            // Time to escalate!
            EC_LOG_INFO("Auto-escalate triggered for event '%s' (Instance %u) - no defenders for %u minutes",
                def.name.c_str(), instanceId, m_autoEscalateMinutes);

            // Broadcast warning
            std::string warnMsg = "|cffff6600[World Event]|r |cffffd700" + def.name +
                "|r defenders are overwhelmed! Reinforcements arriving!";
            sWorldSessionMgr->SendServerMessage(SERVER_MSG_STRING, warnMsg.c_str());

            // Emit bridge event before escalation (so we capture current level)
            sWorldEventBridge->EmitAutoEscalate(
                event.eventId,
                instanceId,
                def.name,
                event.escalationLevel + 1,  // The new level after escalation
                m_autoEscalateMinutes
            );

            // Trigger escalation
            EscalateEvent(instanceId);

            // Reset timer for next potential escalation
            event.timeWithoutDefendersMs = 0;
        }
    }
    else
    {
        // Defenders present - reset accumulator
        event.timeWithoutDefendersMs = 0;
    }
}

// =============================================================================
// Rewards
// =============================================================================


// =============================================================================
// Rewards, Messaging, and Persistence
// =============================================================================

#include "ECWorldEventManager.Rewards.inl"

void WorldEventManager::OnPlayerEnterZone(Player* player, uint32 zoneId, uint32 areaId)
{
    if (!m_enabled || !player)
        return;

    EC_LOG_TRACE("OnPlayerEnterZone: %s entered zone %u, area %u",
        player->GetName().c_str(), zoneId, areaId);

    for (auto& pair : m_activeEvents)
    {
        ActiveEvent& event = pair.second;
        if (event.state != EventState::ACTIVE)
            continue;

        const EventDefinition* def = GetEventDefinition(event.eventId);
        if (!def)
            continue;

        bool inEventZone = (def->location.zoneId == zoneId || def->location.areaId == areaId);
        EC_LOG_TRACE("  Checking event '%s': Zone match=%s, Area match=%s",
            def->name.c_str(),
            def->location.zoneId == zoneId ? "YES" : "no",
            def->location.areaId == areaId ? "YES" : "no");

        if (inEventZone)
        {
            // Check if already a participant
            if (event.participants.find(player->GetGUID()) == event.participants.end())
            {
                EC_LOG_INFO("Player %s entered event zone for '%s'!",
                    player->GetName().c_str(), def->name.c_str());

                EventParticipant participant;
                participant.playerGuid = player->GetGUID();
                participant.playerName = player->GetName();
                participant.kills = 0;
                participant.deaths = 0;
                participant.damageDealt = 0;
                participant.healingDone = 0;
                participant.joinTime = std::chrono::system_clock::now();
                participant.rewarded = false;

                event.participants[player->GetGUID()] = participant;

                // Emit to realtime bridge
                sWorldEventBridge->EmitParticipantJoined(
                    event.eventId,
                    event.instanceId,
                    player,
                    false  // isBot = false for real players
                );

                if (WorldSession* playerSession = player->GetSession())
                {
                    ChatHandler(playerSession).PSendSysMessage(
                        "|cff00ff00[EC World Events]|r You have entered the %s event zone!",
                        def->name.c_str());
                }

                EC_LOG_DEBUG("  Added as participant #%zu", event.participants.size());
            }
            else
            {
                EC_LOG_TRACE("  Already a participant in this event");
            }
        }
    }
}

void WorldEventManager::OnPlayerLeaveZone(Player* player, uint32 oldZoneId, uint32 newZoneId)
{
    if (!m_enabled || !player)
        return;

    EC_LOG_TRACE("OnPlayerLeaveZone: %s left zone %u, entered zone %u",
        player->GetName().c_str(), oldZoneId, newZoneId);

    // Check if player was participating in an event in the old zone
    for (auto& pair : m_activeEvents)
    {
        ActiveEvent& event = pair.second;
        if (event.state != EventState::ACTIVE)
            continue;

        const EventDefinition* def = GetEventDefinition(event.eventId);
        if (!def)
            continue;

        // Check if old zone was this event's zone
        bool wasInEventZone = (def->location.zoneId == oldZoneId);
        bool stillInEventZone = (def->location.zoneId == newZoneId);

        if (wasInEventZone && !stillInEventZone)
        {
            // Check if player was a participant
            auto it = event.participants.find(player->GetGUID());
            if (it != event.participants.end())
            {
                EC_LOG_INFO("Player %s left event '%s' zone (moved from zone %u to %u)",
                    player->GetName().c_str(), def->name.c_str(), oldZoneId, newZoneId);

                // Emit to realtime bridge
                sWorldEventBridge->EmitParticipantLeft(
                    event.eventId,
                    event.instanceId,
                    player,
                    false  // isBot = false for real players
                );

                // Note: We keep their participation record for rewards
                // They can rejoin by re-entering the zone
            }
        }
    }
}

void WorldEventManager::OnPlayerKillCreature(Player* player, Creature* creature)
{
    if (!m_enabled || !player || !creature)
        return;

    EC_LOG_TRACE("OnPlayerKillCreature: %s killed creature %u (%s)",
        player->GetName().c_str(), creature->GetEntry(), creature->GetName().c_str());

    for (auto& pair : m_activeEvents)
    {
        ActiveEvent& event = pair.second;
        if (event.state != EventState::ACTIVE)
            continue;

        auto it = event.participants.find(player->GetGUID());
        if (it != event.participants.end())
        {
            it->second.kills++;
            event.totalKills++;

            EC_LOG_DEBUG("  Kill counted for event %u: Player %s now has %u kills (event total: %u)",
                pair.first, player->GetName().c_str(), it->second.kills, event.totalKills);
        }
    }
}

void WorldEventManager::OnPlayerDeath(Player* player, Player* killer)
{
    if (!m_enabled || !player)
        return;

    EC_LOG_TRACE("OnPlayerDeath: %s died (killer: %s)",
        player->GetName().c_str(),
        killer ? killer->GetName().c_str() : "PvE/Environment");

    for (auto& pair : m_activeEvents)
    {
        ActiveEvent& event = pair.second;
        if (event.state != EventState::ACTIVE)
            continue;

        auto it = event.participants.find(player->GetGUID());
        if (it != event.participants.end())
        {
            it->second.deaths++;
            EC_LOG_DEBUG("  Death counted for event %u: Player %s now has %u deaths",
                pair.first, player->GetName().c_str(), it->second.deaths);

            // Emit to realtime bridge
            sWorldEventBridge->EmitParticipantDied(
                event.eventId,
                event.instanceId,
                player,
                false  // isBot = false for real players
            );
        }
    }
}

void WorldEventManager::OnCreatureDeath(Creature* creature, Player* killer)
{
    if (!creature)
        return;

    // Credit the kill to the player if one exists
    if (killer)
        OnPlayerKillCreature(killer, creature);

    // Check if this creature is a boss (rank 3 = elite worldboss, rank 4 = rare elite worldboss)
    // Also check if death occurred during an active WORLD_BOSS event in the event zone
    uint32 creatureZone = creature->GetZoneId();
    uint32 creatureRank = creature->GetCreatureTemplate()->rank;
    bool isWorldBoss = (creatureRank >= 3);  // CREATURE_ELITE_WORLDBOSS = 3

    for (auto& pair : m_activeEvents)
    {
        ActiveEvent& event = pair.second;
        if (event.state != EventState::ACTIVE)
            continue;

        const EventDefinition* def = GetEventDefinition(event.eventId);
        if (!def)
            continue;

        ProcessRaidTownCasualty(event, *def, creature);
        if (event.phase == EventPhase::ENDING)
            continue;

        // For WORLD_BOSS events, emit boss killed if creature is a world boss in the event zone
        if (def->type == EventType::WORLD_BOSS && def->location.zoneId == creatureZone && isWorldBoss)
        {
            EC_LOG_INFO("World boss '%s' (entry %u) killed during event '%s' by %s",
                creature->GetName().c_str(), creature->GetEntry(), def->name.c_str(),
                killer ? killer->GetName().c_str() : "unknown");

            sWorldEventBridge->EmitBossKilled(
                event.eventId,
                event.instanceId,
                creature,
                killer
            );
        }
        // For RAID/DEFENSE events, emit for any elite+ creature (rank >= 1) as significant kills
        else if ((def->type == EventType::RAID || def->type == EventType::DEFENSE) &&
                 def->location.zoneId == creatureZone && creatureRank >= 2)  // CREATURE_ELITE_ELITE = 2
        {
            EC_LOG_DEBUG("Elite creature '%s' (entry %u, rank %u) killed during event '%s'",
                creature->GetName().c_str(), creature->GetEntry(), creatureRank, def->name.c_str());

            // For raid/defense, treat elite+ kills as mini-boss events
            sWorldEventBridge->EmitBossKilled(
                event.eventId,
                event.instanceId,
                creature,
                killer
            );
        }
    }
}

void WorldEventManager::OnBossEngaged(Creature* boss, Player* engager)
{
    if (!m_enabled || !boss)
        return;

    // Check if this creature is a boss (rank 3 = elite worldboss, rank 4 = rare elite worldboss)
    uint32 creatureZone = boss->GetZoneId();
    uint32 creatureRank = boss->GetCreatureTemplate()->rank;
    bool isWorldBoss = (creatureRank >= 3);  // CREATURE_ELITE_WORLDBOSS = 3

    for (auto& pair : m_activeEvents)
    {
        ActiveEvent& event = pair.second;
        if (event.state != EventState::ACTIVE)
            continue;

        const EventDefinition* def = GetEventDefinition(event.eventId);
        if (!def)
            continue;

        // For WORLD_BOSS events, emit boss engaged if creature is a world boss in the event zone
        if (def->type == EventType::WORLD_BOSS && def->location.zoneId == creatureZone && isWorldBoss)
        {
            EC_LOG_INFO("World boss '%s' (entry %u) engaged during event '%s' by %s",
                boss->GetName().c_str(), boss->GetEntry(), def->name.c_str(),
                engager ? engager->GetName().c_str() : "unknown");

            sWorldEventBridge->EmitBossEngaged(
                event.eventId,
                event.instanceId,
                boss,
                engager
            );
        }
        // For RAID/DEFENSE events, emit for elite+ creatures
        else if ((def->type == EventType::RAID || def->type == EventType::DEFENSE) &&
                 def->location.zoneId == creatureZone && creatureRank >= 2)
        {
            EC_LOG_DEBUG("Elite creature '%s' (entry %u, rank %u) engaged during event '%s'",
                boss->GetName().c_str(), boss->GetEntry(), creatureRank, def->name.c_str());

            sWorldEventBridge->EmitBossEngaged(
                event.eventId,
                event.instanceId,
                boss,
                engager
            );
        }
    }
}

// NOTE: EmitObjectiveCompleted is now called from:
// - CompleteBuildingCapture() when a building is destroyed
// Future: Could add more objective types (kill X enemies, gather Y resources, etc.)

// =============================================================================
// Debug Control
// =============================================================================

void WorldEventManager::SetDebugMode(bool enabled)
{
    m_debugEnabled = enabled;
    ECDebug::SetDebugEnabled(enabled);
    EC_LOG_INFO("Debug mode %s", enabled ? "ENABLED" : "DISABLED");
}

void WorldEventManager::SetDebugLogLevel(uint8 level)
{
    m_debugLogLevel = level;
    ECDebug::SetLogLevel(level);
    EC_LOG_INFO("Debug log level set to %u", level);
}

void WorldEventManager::DebugDumpState() const
{
    EC_LOG_INFO("========== EC WORLD EVENTS STATE DUMP ==========");
    EC_LOG_INFO("Module Enabled: %s", m_enabled ? "YES" : "NO");
    EC_LOG_INFO("Debug Mode: %s", m_debugEnabled ? "ON" : "OFF");
    EC_LOG_INFO("Debug Log Level: %u", m_debugLogLevel);
    EC_LOG_INFO("Update Interval: %u ms", m_updateIntervalMs);
    EC_LOG_INFO("Max Active Events: %u", m_maxActiveEvents);
    EC_LOG_INFO("Event Definitions: %zu", m_eventDefinitions.size());
    EC_LOG_INFO("Active Events: %zu", m_activeEvents.size());
    EC_LOG_INFO("Next Instance ID: %u", m_nextInstanceId);

    if (!m_activeEvents.empty())
    {
        EC_LOG_INFO("--- Active Events ---");
        for (const auto& pair : m_activeEvents)
        {
            const EventDefinition* def = GetEventDefinition(pair.second.eventId);
            EC_LOG_INFO("  [%u] %s - State: %s, Kills: %u, Participants: %zu",
                pair.first,
                def ? def->name.c_str() : "Unknown",
                EventStateToString(pair.second.state).c_str(),
                pair.second.totalKills,
                pair.second.participants.size());
        }
    }

    EC_LOG_INFO("=================================================");
}

void WorldEventManager::DebugDumpEvent(uint32 instanceId) const
{
    auto it = m_activeEvents.find(instanceId);
    if (it == m_activeEvents.end())
    {
        EC_LOG_WARN("DebugDumpEvent: Instance %u not found", instanceId);
        return;
    }

    const ActiveEvent& event = it->second;
    const EventDefinition* def = GetEventDefinition(event.eventId);

    EC_LOG_INFO("========== EVENT DUMP: Instance %u ==========", instanceId);
    EC_LOG_INFO("Event ID: %u", event.eventId);
    EC_LOG_INFO("Name: %s", def ? def->name.c_str() : "Unknown");
    EC_LOG_INFO("State: %s", EventStateToString(event.state).c_str());
    EC_LOG_INFO("Total Kills: %u", event.totalKills);
    EC_LOG_INFO("Escalation Level: %u", event.escalationLevel);
    EC_LOG_INFO("Participants: %zu", event.participants.size());
    EC_LOG_INFO("Spawned Bots: %zu", event.spawnedBots.size());
    EC_LOG_INFO("Event NPCs: alive=%u engaged=%u", event.aliveCombatants, event.engagedCombatants);
    EC_LOG_INFO("=============================================");
}

void WorldEventManager::DebugDumpParticipants(uint32 instanceId) const
{
    auto it = m_activeEvents.find(instanceId);
    if (it == m_activeEvents.end())
    {
        EC_LOG_WARN("DebugDumpParticipants: Instance %u not found", instanceId);
        return;
    }

    const ActiveEvent& event = it->second;

    EC_LOG_INFO("========== PARTICIPANTS: Instance %u ==========", instanceId);
    for (const auto& pair : event.participants)
    {
        const EventParticipant& p = pair.second;
        EC_LOG_INFO("  %s (GUID: %u): Kills=%u, Deaths=%u, Dmg=%llu, Heal=%llu, Rewarded=%s",
            p.playerName.c_str(), p.playerGuid.GetCounter(),
            p.kills, p.deaths, p.damageDealt, p.healingDone,
            p.rewarded ? "YES" : "NO");
    }
    EC_LOG_INFO("================================================");
}

// =============================================================================
// Queries
// =============================================================================

const EventDefinition* WorldEventManager::GetEventDefinition(uint32 eventId) const
{
    auto it = m_eventDefinitions.find(eventId);
    return it != m_eventDefinitions.end() ? &it->second : nullptr;
}

const ActiveEvent* WorldEventManager::GetActiveEvent(uint32 instanceId) const
{
    auto it = m_activeEvents.find(instanceId);
    return it != m_activeEvents.end() ? &it->second : nullptr;
}

std::vector<const ActiveEvent*> WorldEventManager::GetAllActiveEvents() const
{
    std::vector<const ActiveEvent*> events;
    for (const auto& pair : m_activeEvents)
    {
        events.push_back(&pair.second);
    }
    return events;
}

std::vector<const EventDefinition*> WorldEventManager::GetAllEventDefinitions() const
{
    std::vector<const EventDefinition*> defs;
    for (const auto& pair : m_eventDefinitions)
    {
        defs.push_back(&pair.second);
    }
    return defs;
}

// =============================================================================
// Utility
// =============================================================================

std::string WorldEventManager::EventStateToString(EventState state)
{
    switch (state)
    {
        case EventState::SCHEDULED: return "Scheduled";
        case EventState::ACTIVE:    return "Active";
        case EventState::COMPLETED: return "Completed";
        case EventState::CANCELLED: return "Cancelled";
        case EventState::FAILED:    return "Failed";
        default:                    return "Unknown";
    }
}

std::string WorldEventManager::EventPhaseToString(EventPhase phase)
{
    switch (phase)
    {
        case EventPhase::NONE:      return "None";
        case EventPhase::PREPARING: return "Preparing";
        case EventPhase::ACTIVE:    return "Active";
        case EventPhase::ENDING:    return "Ending";
        default:                    return "Unknown";
    }
}

std::string WorldEventManager::EventTypeToString(EventType type)
{
    switch (type)
    {
        case EventType::RAID:       return "Raid";
        case EventType::DEFENSE:    return "Defense";
        case EventType::WORLD_BOSS: return "World Boss";
        case EventType::GATHERING:  return "Gathering";
        case EventType::PVP_ZONE:   return "PvP Zone";
        default:                    return "Unknown";
    }
}

std::string WorldEventManager::EventFactionToString(EventFaction faction)
{
    switch (faction)
    {
        case EventFaction::NEUTRAL:  return "Neutral";
        case EventFaction::ALLIANCE: return "Alliance";
        case EventFaction::HORDE:    return "Horde";
        default:                     return "Unknown";
    }
}

std::string WorldEventManager::EventOutcomeToString(EventOutcome outcome)
{
    switch (outcome)
    {
        case EventOutcome::PENDING:       return "pending";
        case EventOutcome::ATTACKER_WIN:  return "attacker_victory";
        case EventOutcome::DEFENDER_WIN:  return "defender_victory";
        case EventOutcome::DRAW:          return "draw";
        default:                          return "unknown";
    }
}

} // namespace EC
