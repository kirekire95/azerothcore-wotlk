/*
 * mod-alive-world - Event Collector Implementation
 * Copyright (C) 2025 EC
 */

#include "EventCollector.h"
#include "AliveWorldManager.h"
#include "Config.h"
#include "Creature.h"
#include "DatabaseEnv.h"
#include "Map.h"
#include "Player.h"

#ifdef MOD_PLAYERBOTS
#include "PlayerbotAI.h"
#include "RandomPlayerbotMgr.h"
#endif

namespace AliveWorld
{

EventCollector::EventCollector()
    : m_enabled(true)
    , m_persistEnabled(true)
    , m_maxBufferSize(500)
    , m_eventTTLSeconds(600)
    , m_persistIntervalMs(DEFAULT_PERSIST_INTERVAL_MS)
    , m_persistBatchSize(DEFAULT_PERSIST_BATCH_SIZE)
    , m_nextEventId(1)
    , m_cleanupTimer(0)
    , m_persistTimer(0)
{
}

EventCollector::~EventCollector()
{
    // Flush any pending events to database before shutdown
    if (m_persistEnabled)
    {
        PersistEventsToDatabase();
    }

    std::lock_guard<std::mutex> lock(m_bufferMutex);
    m_eventBuffer.clear();
    m_eventIndex.clear();
}

void EventCollector::Initialize()
{
    ALIVE_LOG_INFO("Event Collector initializing...");
    LoadConfig();

    if (m_enabled)
    {
        ALIVE_LOG_INFO("Event Collector enabled - Buffer size: %u, TTL: %us, Persist: %s",
            m_maxBufferSize, m_eventTTLSeconds, m_persistEnabled ? "ON" : "OFF");

        if (m_persistEnabled)
        {
            // Load recent events from database for continuity
            LoadEventsFromDatabase();
            ALIVE_LOG_INFO("Event Collector persistence interval: %ums, batch size: %u",
                m_persistIntervalMs, m_persistBatchSize);
        }
    }
    else
    {
        ALIVE_LOG_WARN("Event Collector disabled in config");
    }
}

void EventCollector::LoadConfig()
{
    m_enabled = sConfigMgr->GetOption<bool>("AliveWorld.EventCollector.Enable", true);
    m_maxBufferSize = sConfigMgr->GetOption<uint32>("AliveWorld.EventCollector.MaxBufferSize", 500);
    m_eventTTLSeconds = sConfigMgr->GetOption<uint32>("AliveWorld.EventCollector.EventTTLSeconds", 600);

    // Persistence settings
    m_persistEnabled = sConfigMgr->GetOption<bool>("AliveWorld.EventCollector.Persist.Enable", true);
    m_persistIntervalMs = sConfigMgr->GetOption<uint32>("AliveWorld.EventCollector.Persist.IntervalMs", DEFAULT_PERSIST_INTERVAL_MS);
    m_persistBatchSize = sConfigMgr->GetOption<uint32>("AliveWorld.EventCollector.Persist.BatchSize", DEFAULT_PERSIST_BATCH_SIZE);
}

void EventCollector::AddEvent(WorldEvent event)
{
    if (!m_enabled)
        return;

    event.eventId = m_nextEventId++;
    event.timestamp = NowMs();

    {
        std::lock_guard<std::mutex> lock(m_bufferMutex);

        // Remove oldest events if buffer is full
        bool evicted = false;
        while (m_eventBuffer.size() >= m_maxBufferSize && !m_eventBuffer.empty())
        {
            uint32 oldId = m_eventBuffer.front().eventId;
            m_eventIndex.erase(oldId);
            m_eventBuffer.pop_front();
            evicted = true;
        }

        // Add new event
        m_eventBuffer.push_back(event);

        // Rebuild index if events were evicted (pop_front invalidates all indices)
        if (evicted)
        {
            m_eventIndex.clear();
            for (size_t i = 0; i < m_eventBuffer.size(); ++i)
            {
                m_eventIndex[m_eventBuffer[i].eventId] = i;
            }
        }
        else
        {
            m_eventIndex[event.eventId] = m_eventBuffer.size() - 1;
        }
    }

    // Queue for database persistence (separate lock)
    if (m_persistEnabled)
    {
        std::lock_guard<std::mutex> persistLock(m_persistMutex);
        m_persistQueue.push(event);
    }

    ALIVE_LOG_DEBUG("Event recorded [%u]: %s - %s (Zone: %u, Importance: %u)",
        event.eventId,
        EventTypeToString(event.type),
        event.details.c_str(),
        event.zoneId,
        event.importance);
}

bool EventCollector::IsBot(Unit* unit) const
{
    if (!unit || !unit->IsPlayer())
        return false;

#ifdef MOD_PLAYERBOTS
    Player* player = unit->ToPlayer();
    if (player && sPlayerbotsMgr.GetPlayerbotAI(player))
        return true;
    if (sRandomPlayerbotMgr.IsRandomBot(player))
        return true;
#endif

    return false;
}

void EventCollector::GetZoneAndArea(Unit* unit, uint32& zoneId, uint32& areaId, uint32& mapId)
{
    if (!unit)
    {
        zoneId = 0;
        areaId = 0;
        mapId = 0;
        return;
    }

    mapId = unit->GetMapId();
    unit->GetZoneAndAreaId(zoneId, areaId);
}

uint8 EventCollector::CalculateImportance(WorldEventType type, Unit* primary, Unit* secondary)
{
    uint8 baseImportance = 1;

    switch (type)
    {
        // High importance events
        case WorldEventType::BOSS_KILLED:
        case WorldEventType::WORLD_BOSS_SPAWN:
        case WorldEventType::RARE_SPAWN:
        case WorldEventType::WIPE:
            baseImportance = 9;
            break;

        case WorldEventType::BOSS_ENGAGED:
        case WorldEventType::PLAYER_KILLED_PLAYER:
        case WorldEventType::WORLD_EVENT_START:
            baseImportance = 7;
            break;

        // Medium importance
        case WorldEventType::GUARD_CALLED:
        case WorldEventType::GROUP_FORMED:
        case WorldEventType::DUEL_ENDED:
        case WorldEventType::ZONE_BECAME_ACTIVE:
            baseImportance = 5;
            break;

        // Lower importance
        case WorldEventType::PLAYER_KILLED_MOB:
        case WorldEventType::BOT_KILLED_MOB:
        case WorldEventType::PLAYER_CHAT:
        case WorldEventType::TRADE_COMPLETED:
            baseImportance = 3;
            break;

        // Routine events
        case WorldEventType::PLAYER_ENTERED_ZONE:
        case WorldEventType::PLAYER_LEFT_ZONE:
        case WorldEventType::BOT_ENTERED_ZONE:
        case WorldEventType::BOT_LEFT_ZONE:
            baseImportance = 1;
            break;

        default:
            baseImportance = 3;
            break;
    }

    // Bonus for real player involvement
    if (primary && primary->IsPlayer() && !IsBot(primary))
        baseImportance = std::min<uint8>(10, baseImportance + 1);

    if (secondary && secondary->IsPlayer() && !IsBot(secondary))
        baseImportance = std::min<uint8>(10, baseImportance + 1);

    return baseImportance;
}

void EventCollector::RecordCombatKill(Unit* killer, Unit* victim, uint32 zoneId, uint32 areaId)
{
    if (!killer || !victim)
        return;

    WorldEvent event;
    GetZoneAndArea(killer, event.zoneId, event.areaId, event.mapId);
    if (zoneId > 0) event.zoneId = zoneId;
    if (areaId > 0) event.areaId = areaId;

    event.primaryActor = killer->GetGUID();
    event.secondaryActor = victim->GetGUID();
    event.primaryName = killer->GetName();
    event.secondaryName = victim->GetName();

    bool killerIsBot = IsBot(killer);
    bool victimIsPlayer = victim->IsPlayer();
    bool victimIsBot = IsBot(victim);

    if (victimIsPlayer && !victimIsBot)
    {
        // Real player killed
        event.type = killerIsBot ? WorldEventType::BOT_KILLED_PLAYER : WorldEventType::PLAYER_KILLED_PLAYER;
        event.details = event.primaryName + " slew " + event.secondaryName + " in combat!";
    }
    else if (victimIsPlayer && victimIsBot)
    {
        // Bot killed
        event.type = killerIsBot ? WorldEventType::BOT_KILLED_MOB : WorldEventType::PLAYER_KILLED_MOB;
        event.details = event.primaryName + " defeated " + event.secondaryName;
    }
    else
    {
        // Creature killed
        event.type = killerIsBot ? WorldEventType::BOT_KILLED_MOB : WorldEventType::PLAYER_KILLED_MOB;
        event.details = event.primaryName + " killed " + event.secondaryName;
    }

    event.importance = CalculateImportance(event.type, killer, victim);
    AddEvent(event);
}

void EventCollector::RecordPlayerDeath(Player* player, Unit* killer)
{
    if (!player)
        return;

    WorldEvent event;
    GetZoneAndArea(player, event.zoneId, event.areaId, event.mapId);
    event.primaryActor = player->GetGUID();
    event.primaryName = player->GetName();

    if (killer)
    {
        event.secondaryActor = killer->GetGUID();
        event.secondaryName = killer->GetName();
        event.details = event.primaryName + " was slain by " + event.secondaryName;
    }
    else
    {
        event.details = event.primaryName + " died";
    }

    event.type = IsBot(player) ? WorldEventType::BOT_DIED : WorldEventType::PLAYER_DIED;
    event.importance = CalculateImportance(event.type, player, killer);
    AddEvent(event);
}

void EventCollector::RecordBossEngaged(Creature* boss, Player* engager)
{
    if (!boss || !engager)
        return;

    WorldEvent event;
    GetZoneAndArea(boss, event.zoneId, event.areaId, event.mapId);
    event.type = WorldEventType::BOSS_ENGAGED;
    event.primaryActor = engager->GetGUID();
    event.secondaryActor = boss->GetGUID();
    event.primaryName = engager->GetName();
    event.secondaryName = boss->GetName();
    event.details = event.primaryName + " engaged " + event.secondaryName + "!";
    event.importance = CalculateImportance(event.type, engager, boss);
    AddEvent(event);
}

void EventCollector::RecordBossKilled(Creature* boss, Player* killer)
{
    if (!boss)
        return;

    WorldEvent event;
    GetZoneAndArea(boss, event.zoneId, event.areaId, event.mapId);
    event.type = WorldEventType::BOSS_KILLED;
    event.secondaryActor = boss->GetGUID();
    event.secondaryName = boss->GetName();

    if (killer)
    {
        event.primaryActor = killer->GetGUID();
        event.primaryName = killer->GetName();
        event.details = event.secondaryName + " was defeated by " + event.primaryName + "!";
    }
    else
    {
        event.details = event.secondaryName + " has been defeated!";
    }

    event.importance = 9;  // Boss kills are always high importance
    AddEvent(event);
}

void EventCollector::RecordChat(Player* sender, const std::string& message, uint32 zoneId)
{
    if (!sender || message.empty())
        return;

    WorldEvent event;
    GetZoneAndArea(sender, event.zoneId, event.areaId, event.mapId);
    if (zoneId > 0) event.zoneId = zoneId;

    event.type = IsBot(sender) ? WorldEventType::BOT_CHAT : WorldEventType::PLAYER_CHAT;
    event.primaryActor = sender->GetGUID();
    event.primaryName = sender->GetName();

    // Truncate long messages for the event log
    std::string truncatedMsg = message.length() > 100 ? message.substr(0, 100) + "..." : message;
    event.details = event.primaryName + " said: \"" + truncatedMsg + "\"";
    event.importance = CalculateImportance(event.type, sender, nullptr);
    AddEvent(event);
}

void EventCollector::RecordEmote(Player* player, uint32 emoteId, uint32 zoneId)
{
    if (!player)
        return;

    WorldEvent event;
    GetZoneAndArea(player, event.zoneId, event.areaId, event.mapId);
    if (zoneId > 0) event.zoneId = zoneId;

    event.type = WorldEventType::PLAYER_EMOTE;
    event.primaryActor = player->GetGUID();
    event.primaryName = player->GetName();
    event.details = event.primaryName + " performed emote #" + std::to_string(emoteId);
    event.importance = 2;
    AddEvent(event);
}

void EventCollector::RecordTrade(Player* player1, Player* player2, uint32 zoneId)
{
    if (!player1 || !player2)
        return;

    WorldEvent event;
    GetZoneAndArea(player1, event.zoneId, event.areaId, event.mapId);
    if (zoneId > 0) event.zoneId = zoneId;

    event.type = WorldEventType::TRADE_COMPLETED;
    event.primaryActor = player1->GetGUID();
    event.secondaryActor = player2->GetGUID();
    event.primaryName = player1->GetName();
    event.secondaryName = player2->GetName();
    event.details = event.primaryName + " traded with " + event.secondaryName;
    event.importance = CalculateImportance(event.type, player1, player2);
    AddEvent(event);
}

void EventCollector::RecordGroupEvent(Player* leader, bool formed, uint32 zoneId)
{
    if (!leader)
        return;

    WorldEvent event;
    GetZoneAndArea(leader, event.zoneId, event.areaId, event.mapId);
    if (zoneId > 0) event.zoneId = zoneId;

    event.type = formed ? WorldEventType::GROUP_FORMED : WorldEventType::GROUP_DISBANDED;
    event.primaryActor = leader->GetGUID();
    event.primaryName = leader->GetName();
    event.details = event.primaryName + (formed ? " formed a group" : " disbanded the group");
    event.importance = CalculateImportance(event.type, leader, nullptr);
    AddEvent(event);
}

void EventCollector::RecordDuel(Player* player1, Player* player2, bool started, Player* winner)
{
    if (!player1 || !player2)
        return;

    WorldEvent event;
    GetZoneAndArea(player1, event.zoneId, event.areaId, event.mapId);

    event.type = started ? WorldEventType::DUEL_STARTED : WorldEventType::DUEL_ENDED;
    event.primaryActor = player1->GetGUID();
    event.secondaryActor = player2->GetGUID();
    event.primaryName = player1->GetName();
    event.secondaryName = player2->GetName();

    if (started)
    {
        event.details = event.primaryName + " challenged " + event.secondaryName + " to a duel!";
    }
    else if (winner)
    {
        event.details = winner->GetName() + " won the duel against " +
            (winner == player1 ? event.secondaryName : event.primaryName) + "!";
    }
    else
    {
        event.details = "The duel between " + event.primaryName + " and " + event.secondaryName + " ended";
    }

    event.importance = CalculateImportance(event.type, player1, player2);
    AddEvent(event);
}

void EventCollector::RecordZoneChange(Player* player, uint32 oldZone, uint32 newZone, bool isBot)
{
    if (!player)
        return;

    // Record leaving old zone
    if (oldZone > 0)
    {
        WorldEvent leaveEvent;
        leaveEvent.zoneId = oldZone;
        leaveEvent.mapId = player->GetMapId();
        leaveEvent.areaId = 0;
        leaveEvent.type = isBot ? WorldEventType::BOT_LEFT_ZONE : WorldEventType::PLAYER_LEFT_ZONE;
        leaveEvent.primaryActor = player->GetGUID();
        leaveEvent.primaryName = player->GetName();
        leaveEvent.details = leaveEvent.primaryName + " left the zone";
        leaveEvent.importance = 1;
        AddEvent(leaveEvent);
    }

    // Record entering new zone
    if (newZone > 0)
    {
        WorldEvent enterEvent;
        enterEvent.zoneId = newZone;
        enterEvent.mapId = player->GetMapId();
        enterEvent.areaId = 0;
        enterEvent.type = isBot ? WorldEventType::BOT_ENTERED_ZONE : WorldEventType::PLAYER_ENTERED_ZONE;
        enterEvent.primaryActor = player->GetGUID();
        enterEvent.primaryName = player->GetName();
        enterEvent.details = enterEvent.primaryName + " entered the zone";
        enterEvent.importance = isBot ? 1 : 3;  // Real players entering is more notable
        AddEvent(enterEvent);
    }
}

void EventCollector::RecordRareSpawn(Creature* creature, uint32 zoneId)
{
    if (!creature)
        return;

    WorldEvent event;
    GetZoneAndArea(creature, event.zoneId, event.areaId, event.mapId);
    if (zoneId > 0) event.zoneId = zoneId;

    event.type = WorldEventType::RARE_SPAWN;
    event.primaryActor = creature->GetGUID();
    event.primaryName = creature->GetName();
    event.details = "Rare creature spotted: " + event.primaryName + "!";
    event.importance = 8;
    AddEvent(event);
}

void EventCollector::RecordWorldBossSpawn(Creature* creature, uint32 zoneId)
{
    if (!creature)
        return;

    WorldEvent event;
    GetZoneAndArea(creature, event.zoneId, event.areaId, event.mapId);
    if (zoneId > 0) event.zoneId = zoneId;

    event.type = WorldEventType::WORLD_BOSS_SPAWN;
    event.primaryActor = creature->GetGUID();
    event.primaryName = creature->GetName();
    event.details = "World boss has appeared: " + event.primaryName + "!";
    event.importance = 10;
    AddEvent(event);
}

void EventCollector::RecordGuardCalled(Unit* criminal, uint32 zoneId, uint8 wantedLevel)
{
    if (!criminal)
        return;

    WorldEvent event;
    GetZoneAndArea(criminal, event.zoneId, event.areaId, event.mapId);
    if (zoneId > 0) event.zoneId = zoneId;

    event.type = WorldEventType::GUARD_CALLED;
    event.primaryActor = criminal->GetGUID();
    event.primaryName = criminal->GetName();
    event.details = "Guards called on " + event.primaryName + "! (Wanted Level: " +
        std::to_string(wantedLevel) + ")";
    event.importance = std::min<uint8>(10, 4 + wantedLevel);
    AddEvent(event);
}

void EventCollector::RecordWorldEvent(uint32 eventId, const std::string& eventName, bool started)
{
    WorldEvent event;
    event.type = started ? WorldEventType::WORLD_EVENT_START : WorldEventType::WORLD_EVENT_END;
    event.details = eventName + (started ? " has begun!" : " has ended.");
    event.importance = 8;
    AddEvent(event);
}

void EventCollector::RecordCustomEvent(WorldEventType type, ObjectGuid primary, ObjectGuid secondary,
                                       const std::string& details, uint8 importance,
                                       uint32 zoneId, uint32 areaId, uint32 mapId)
{
    WorldEvent event;
    event.type = type;
    event.primaryActor = primary;
    event.secondaryActor = secondary;
    event.details = details;
    event.importance = importance;
    event.zoneId = zoneId;
    event.areaId = areaId;
    event.mapId = mapId;
    AddEvent(event);
}

std::vector<WorldEvent> EventCollector::GetRecentEvents(uint32 count) const
{
    std::lock_guard<std::mutex> lock(m_bufferMutex);

    std::vector<WorldEvent> result;
    uint32 toGet = std::min<uint32>(count, static_cast<uint32>(m_eventBuffer.size()));

    for (auto it = m_eventBuffer.rbegin(); it != m_eventBuffer.rend() && result.size() < toGet; ++it)
    {
        result.push_back(*it);
    }

    return result;
}

std::vector<WorldEvent> EventCollector::GetEventsByZone(uint32 zoneId, uint32 count) const
{
    std::lock_guard<std::mutex> lock(m_bufferMutex);

    std::vector<WorldEvent> result;
    for (auto it = m_eventBuffer.rbegin(); it != m_eventBuffer.rend() && result.size() < count; ++it)
    {
        if (it->zoneId == zoneId)
            result.push_back(*it);
    }

    return result;
}

std::vector<WorldEvent> EventCollector::GetEventsByType(WorldEventType type, uint32 count) const
{
    std::lock_guard<std::mutex> lock(m_bufferMutex);

    std::vector<WorldEvent> result;
    for (auto it = m_eventBuffer.rbegin(); it != m_eventBuffer.rend() && result.size() < count; ++it)
    {
        if (it->type == type)
            result.push_back(*it);
    }

    return result;
}

std::vector<WorldEvent> EventCollector::GetEventsAfter(uint64 timestamp) const
{
    std::lock_guard<std::mutex> lock(m_bufferMutex);

    std::vector<WorldEvent> result;
    for (const auto& event : m_eventBuffer)
    {
        if (event.timestamp > timestamp)
            result.push_back(event);
    }

    return result;
}

std::vector<WorldEvent> EventCollector::GetUnprocessedEvents() const
{
    std::lock_guard<std::mutex> lock(m_bufferMutex);

    std::vector<WorldEvent> result;
    for (const auto& event : m_eventBuffer)
    {
        if (!event.processed)
            result.push_back(event);
    }

    return result;
}

const WorldEvent* EventCollector::GetEvent(uint32 eventId) const
{
    std::lock_guard<std::mutex> lock(m_bufferMutex);

    auto it = m_eventIndex.find(eventId);
    if (it == m_eventIndex.end() || it->second >= m_eventBuffer.size())
        return nullptr;

    return &m_eventBuffer[it->second];
}

void EventCollector::MarkEventProcessed(uint32 eventId)
{
    std::lock_guard<std::mutex> lock(m_bufferMutex);

    auto it = m_eventIndex.find(eventId);
    if (it != m_eventIndex.end() && it->second < m_eventBuffer.size())
    {
        m_eventBuffer[it->second].processed = true;
    }
}

void EventCollector::CleanupExpiredEvents()
{
    std::lock_guard<std::mutex> lock(m_bufferMutex);

    uint64 now = NowMs();
    uint64 expirationTime = now - (m_eventTTLSeconds * 1000);
    uint32 removed = 0;

    while (!m_eventBuffer.empty() && m_eventBuffer.front().timestamp < expirationTime)
    {
        m_eventIndex.erase(m_eventBuffer.front().eventId);
        m_eventBuffer.pop_front();
        removed++;
    }

    // Rebuild index after cleanup
    m_eventIndex.clear();
    for (size_t i = 0; i < m_eventBuffer.size(); ++i)
    {
        m_eventIndex[m_eventBuffer[i].eventId] = i;
    }

    if (removed > 0)
    {
        ALIVE_LOG_DEBUG("Cleaned up %u expired events", removed);
    }
}

uint32 EventCollector::GetEventCount() const
{
    std::lock_guard<std::mutex> lock(m_bufferMutex);
    return static_cast<uint32>(m_eventBuffer.size());
}

uint32 EventCollector::GetUnprocessedCount() const
{
    std::lock_guard<std::mutex> lock(m_bufferMutex);

    uint32 count = 0;
    for (const auto& event : m_eventBuffer)
    {
        if (!event.processed)
            count++;
    }
    return count;
}

std::map<WorldEventType, uint32> EventCollector::GetEventTypeCounts() const
{
    std::lock_guard<std::mutex> lock(m_bufferMutex);

    std::map<WorldEventType, uint32> counts;
    for (const auto& event : m_eventBuffer)
    {
        counts[event.type]++;
    }
    return counts;
}

void EventCollector::Update(uint32 diff)
{
    if (!m_enabled)
        return;

    m_cleanupTimer += diff;
    if (m_cleanupTimer >= CLEANUP_INTERVAL_MS)
    {
        m_cleanupTimer = 0;
        CleanupExpiredEvents();
    }

    // Periodic database persistence
    if (m_persistEnabled)
    {
        m_persistTimer += diff;
        if (m_persistTimer >= m_persistIntervalMs)
        {
            m_persistTimer = 0;
            PersistEventsToDatabase();
        }
    }
}

void EventCollector::LoadEventsFromDatabase()
{
    if (!m_persistEnabled)
        return;

    ALIVE_LOG_INFO("Loading recent events from database...");

    // Load events from last TTL period for continuity
    uint64 cutoffTime = NowMs() - (m_eventTTLSeconds * 1000);

    QueryResult result = CharacterDatabase.Query(
        "SELECT event_id, event_type, timestamp, zone_id, area_id, map_id, "
        "primary_guid, primary_name, secondary_guid, secondary_name, "
        "importance, details_json, processed "
        "FROM alive_world_events "
        "WHERE timestamp > {} "
        "ORDER BY timestamp ASC "
        "LIMIT {}",
        cutoffTime, m_maxBufferSize);

    if (!result)
    {
        ALIVE_LOG_INFO("No recent events in database");
        return;
    }

    std::lock_guard<std::mutex> lock(m_bufferMutex);
    uint32 count = 0;

    do
    {
        Field* fields = result->Fetch();

        WorldEvent event;
        event.eventId = fields[0].Get<uint32>();
        event.type = static_cast<WorldEventType>(fields[1].Get<uint8>());
        event.timestamp = fields[2].Get<uint64>();
        event.zoneId = fields[3].Get<uint32>();
        event.areaId = fields[4].Get<uint32>();
        event.mapId = fields[5].Get<uint32>();
        event.primaryActor = ObjectGuid(fields[6].Get<uint64>());
        event.primaryName = fields[7].Get<std::string>();
        event.secondaryActor = ObjectGuid(fields[8].Get<uint64>());
        event.secondaryName = fields[9].Get<std::string>();
        event.importance = fields[10].Get<uint8>();
        event.details = fields[11].Get<std::string>();
        event.processed = fields[12].Get<bool>();

        m_eventBuffer.push_back(event);
        m_eventIndex[event.eventId] = m_eventBuffer.size() - 1;

        // Track highest event ID
        if (event.eventId >= m_nextEventId)
            m_nextEventId = event.eventId + 1;

        count++;
    }
    while (result->NextRow());

    ALIVE_LOG_INFO("Loaded %u events from database, next event ID: %u", count, m_nextEventId.load());
}

void EventCollector::PersistEventsToDatabase()
{
    if (!m_persistEnabled)
        return;

    std::vector<WorldEvent> eventsToPersist;

    // Grab batch from queue
    {
        std::lock_guard<std::mutex> lock(m_persistMutex);
        while (!m_persistQueue.empty() && eventsToPersist.size() < m_persistBatchSize)
        {
            eventsToPersist.push_back(m_persistQueue.front());
            m_persistQueue.pop();
        }
    }

    if (eventsToPersist.empty())
        return;

    // Build batch insert
    CharacterDatabaseTransaction trans = CharacterDatabase.BeginTransaction();

    for (const auto& event : eventsToPersist)
    {
        // Create mutable copies for EscapeString
        std::string primaryNameEsc = event.primaryName;
        std::string secondaryNameEsc = event.secondaryName;
        std::string detailsEsc = event.details;
        CharacterDatabase.EscapeString(primaryNameEsc);
        CharacterDatabase.EscapeString(secondaryNameEsc);
        CharacterDatabase.EscapeString(detailsEsc);

        // Use INSERT IGNORE to handle restarts (event_id is our primary key)
        std::string sql = Acore::StringFormat(
            "INSERT IGNORE INTO alive_world_events "
            "(event_id, event_type, timestamp, zone_id, area_id, map_id, "
            "primary_guid, primary_name, secondary_guid, secondary_name, "
            "importance, details_json, processed) VALUES "
            "({}, {}, {}, {}, {}, {}, {}, '{}', {}, '{}', {}, '{}', {})",
            event.eventId,
            static_cast<uint8>(event.type),
            event.timestamp,
            event.zoneId,
            event.areaId,
            event.mapId,
            event.primaryActor.IsEmpty() ? 0 : event.primaryActor.GetRawValue(),
            primaryNameEsc,
            event.secondaryActor.IsEmpty() ? 0 : event.secondaryActor.GetRawValue(),
            secondaryNameEsc,
            event.importance,
            detailsEsc,
            event.processed ? 1 : 0
        );

        trans->Append(sql.c_str());
    }

    CharacterDatabase.CommitTransaction(trans);

    ALIVE_LOG_DEBUG("Persisted %zu events to database", eventsToPersist.size());
}

} // namespace AliveWorld
