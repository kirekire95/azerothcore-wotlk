/*
 * mod-ec-world-events - Bridge Integration Implementation
 * Copyright (C) 2025 EC
 */

#include "ECWorldEventBridge.h"
#include "ECWorldEventManager.h"

#include "Creature.h"
#include "Log.h"
#include "Player.h"

#include <sstream>

#ifdef MOD_EC_REALTIME_BRIDGE
#include "RealtimeBridgeSocket.h"
#include "RealtimeBridgeTypes.h"
using namespace ECRealtimeBridge;
#endif

#ifdef MOD_ALIVE_WORLD
#include "AliveWorldManager.h"
#include "EventCollector.h"
#endif

namespace ECWorldEvents
{

// ============================================================================
// SINGLETON
// ============================================================================

WorldEventBridge* WorldEventBridge::Instance()
{
    static WorldEventBridge instance;
    return &instance;
}

WorldEventBridge::WorldEventBridge()
    : m_eventsSent(0)
    , m_initialized(false)
{
}

WorldEventBridge::~WorldEventBridge()
{
    Shutdown();
}

void WorldEventBridge::Initialize()
{
#ifdef MOD_EC_REALTIME_BRIDGE
    EC_LOG_INFO("Registering with shared realtime bridge");

    // Register command handler for world events namespace
    sRealtimeBridge->RegisterHandler(
        EventNamespace::WORLD_EVENTS,
        [](const BridgeCommand& cmd) {
            // Handle incoming commands from console
            if (cmd.type == "start_event")
            {
                // Parse event ID from JSON: {"eventId": 1}
                std::string payload = cmd.jsonPayload;
                size_t pos = payload.find("\"eventId\":");
                if (pos != std::string::npos)
                {
                    try
                    {
                        uint32 eventId = static_cast<uint32>(std::stoul(payload.substr(pos + 10)));

                        if (sECWorldEventMgr->StartEvent(eventId, nullptr))
                        {
                            EC_LOG_INFO("Started event %u via console command", eventId);
                        }
                        else
                        {
                            EC_LOG_WARN("Failed to start event %u via console command", eventId);
                        }
                    }
                    catch (const std::exception& e)
                    {
                        EC_LOG_WARN("Failed to parse eventId from payload: %s", e.what());
                    }
                }
            }
            else if (cmd.type == "stop_event")
            {
                // Parse instance ID from JSON: {"instanceId": 1}
                std::string payload = cmd.jsonPayload;
                size_t pos = payload.find("\"instanceId\":");
                if (pos != std::string::npos)
                {
                    try
                    {
                        uint32 instanceId = static_cast<uint32>(std::stoul(payload.substr(pos + 13)));

                        if (sECWorldEventMgr->StopEvent(instanceId, nullptr))
                        {
                            EC_LOG_INFO("Stopped event instance %u via console command", instanceId);
                        }
                        else
                        {
                            EC_LOG_WARN("Failed to stop event instance %u via console command", instanceId);
                        }
                    }
                    catch (const std::exception& e)
                    {
                        EC_LOG_WARN("Failed to parse instanceId from payload: %s", e.what());
                    }
                }
            }
            else if (cmd.type == "escalate_event")
            {
                // Parse instance ID and manually escalate: {"instanceId": 1}
                std::string payload = cmd.jsonPayload;
                size_t pos = payload.find("\"instanceId\":");
                if (pos != std::string::npos)
                {
                    try
                    {
                        uint32 instanceId = static_cast<uint32>(std::stoul(payload.substr(pos + 13)));
                        EC_LOG_INFO("Escalate command received for instance %u", instanceId);
                        // Note: Escalation would require adding the method to manager
                    }
                    catch (const std::exception& e)
                    {
                        EC_LOG_WARN("Failed to parse instanceId from payload: %s", e.what());
                    }
                }
            }
            else if (cmd.type == "list_events")
            {
                // List all available events - useful for debugging
                EC_LOG_INFO("list_events command received");
            }
        }
    );

    m_initialized = true;
    EC_LOG_INFO("Bridge adapter initialized");
#else
    EC_LOG_WARN("mod-ec-realtime-bridge not available");
#endif
}

void WorldEventBridge::Shutdown()
{
#ifdef MOD_EC_REALTIME_BRIDGE
    if (m_initialized)
    {
        sRealtimeBridge->UnregisterHandler(EventNamespace::WORLD_EVENTS);
        m_initialized = false;
    }
#endif
}

// ============================================================================
// EVENT LIFECYCLE
// ============================================================================

void WorldEventBridge::EmitEventScheduled(uint32_t eventId, uint32_t instanceId,
    const std::string& eventName, const std::string& eventType,
    uint32_t mapId, uint32_t zoneId, uint64_t startTime)
{
#ifdef MOD_EC_REALTIME_BRIDGE
    std::ostringstream ss;
    ss << "{\"eventId\":" << eventId << ","
       << "\"instanceId\":" << instanceId << ","
       << "\"eventName\":\"" << EscapeJsonString(eventName) << "\","
       << "\"eventType\":\"" << EscapeJsonString(eventType) << "\","
       << "\"mapId\":" << mapId << ","
       << "\"zoneId\":" << zoneId << ","
       << "\"startTime\":" << startTime << "}";

    sRealtimeBridge->EmitEvent(EventNamespace::WORLD_EVENTS, "event_scheduled", ss.str());
    m_eventsSent++;
#endif
}

void WorldEventBridge::EmitEventStarting(uint32_t eventId, uint32_t instanceId,
    const std::string& eventName)
{
#ifdef MOD_EC_REALTIME_BRIDGE
    std::ostringstream ss;
    ss << "{\"eventId\":" << eventId << ","
       << "\"instanceId\":" << instanceId << ","
       << "\"eventName\":\"" << EscapeJsonString(eventName) << "\"}";

    sRealtimeBridge->EmitEvent(EventNamespace::WORLD_EVENTS, "event_starting", ss.str());
    m_eventsSent++;
#endif
}

void WorldEventBridge::EmitEventStarted(uint32_t eventId, uint32_t instanceId,
    const std::string& eventName, const std::string& eventType,
    uint32_t mapId, uint32_t zoneId)
{
#ifdef MOD_EC_REALTIME_BRIDGE
    std::ostringstream ss;
    ss << "{\"eventId\":" << eventId << ","
       << "\"instanceId\":" << instanceId << ","
       << "\"eventName\":\"" << EscapeJsonString(eventName) << "\","
       << "\"eventType\":\"" << EscapeJsonString(eventType) << "\","
       << "\"mapId\":" << mapId << ","
       << "\"zoneId\":" << zoneId << ","
       << "\"state\":\"ACTIVE\"}";

    sRealtimeBridge->EmitEvent(EventNamespace::WORLD_EVENTS, "event_started", ss.str());
    m_eventsSent++;
#endif

    // Notify AliveWorld for gossip generation
#ifdef MOD_ALIVE_WORLD
    if (sAliveWorldMgr->IsEnabled())
    {
        if (auto* eventCollector = sAliveWorldMgr->GetEventCollector())
        {
            // Record as world event starting
            eventCollector->RecordWorldEvent(eventId, eventName, true);
        }
    }
#endif
}

void WorldEventBridge::EmitEventEnding(uint32_t eventId, uint32_t instanceId,
    const std::string& eventName)
{
#ifdef MOD_EC_REALTIME_BRIDGE
    std::ostringstream ss;
    ss << "{\"eventId\":" << eventId << ","
       << "\"instanceId\":" << instanceId << ","
       << "\"eventName\":\"" << EscapeJsonString(eventName) << "\","
       << "\"state\":\"ENDING\"}";

    sRealtimeBridge->EmitEvent(EventNamespace::WORLD_EVENTS, "event_ending", ss.str());
    m_eventsSent++;
#endif
}

void WorldEventBridge::EmitEventEnded(uint32_t eventId, uint32_t instanceId,
    const std::string& eventName, uint32_t duration, uint32_t participantCount,
    const std::string& result)
{
#ifdef MOD_EC_REALTIME_BRIDGE
    std::ostringstream ss;
    ss << "{\"eventId\":" << eventId << ","
       << "\"instanceId\":" << instanceId << ","
       << "\"eventName\":\"" << EscapeJsonString(eventName) << "\","
       << "\"result\":\"" << EscapeJsonString(result) << "\","
       << "\"duration\":" << duration << ","
       << "\"participantCount\":" << participantCount << ","
       << "\"state\":\"ENDED\"}";

    sRealtimeBridge->EmitEvent(EventNamespace::WORLD_EVENTS, "event_ended", ss.str());
    m_eventsSent++;
#endif

    // Notify AliveWorld event ended
#ifdef MOD_ALIVE_WORLD
    if (sAliveWorldMgr->IsEnabled())
    {
        if (auto* eventCollector = sAliveWorldMgr->GetEventCollector())
        {
            // Record as world event ending
            eventCollector->RecordWorldEvent(eventId, eventName, false);
        }
    }
#endif
}

void WorldEventBridge::EmitEventCancelled(uint32_t eventId, uint32_t instanceId,
    const std::string& eventName, const std::string& reason)
{
#ifdef MOD_EC_REALTIME_BRIDGE
    std::ostringstream ss;
    ss << "{\"eventId\":" << eventId << ","
       << "\"instanceId\":" << instanceId << ","
       << "\"eventName\":\"" << EscapeJsonString(eventName) << "\","
       << "\"reason\":\"" << EscapeJsonString(reason) << "\","
       << "\"state\":\"CANCELLED\"}";

    sRealtimeBridge->EmitEvent(EventNamespace::WORLD_EVENTS, "event_cancelled", ss.str());
    m_eventsSent++;
#endif
}

// ============================================================================
// PARTICIPANT EVENTS
// ============================================================================

void WorldEventBridge::EmitParticipantJoined(uint32_t eventId, uint32_t instanceId,
    Player* player, bool isBot)
{
#ifdef MOD_EC_REALTIME_BRIDGE
    if (!player) return;

    std::ostringstream ss;
    ss << "{\"eventId\":" << eventId << ","
       << "\"instanceId\":" << instanceId << ","
       << "\"player\":" << BuildPlayerJson(player) << ","
       << "\"isBot\":" << (isBot ? "true" : "false") << "}";

    sRealtimeBridge->EmitEvent(EventNamespace::WORLD_EVENTS, "participant_joined", ss.str());
    m_eventsSent++;
#endif
}

void WorldEventBridge::EmitParticipantLeft(uint32_t eventId, uint32_t instanceId,
    Player* player, bool isBot)
{
#ifdef MOD_EC_REALTIME_BRIDGE
    if (!player) return;

    std::ostringstream ss;
    ss << "{\"eventId\":" << eventId << ","
       << "\"instanceId\":" << instanceId << ","
       << "\"player\":" << BuildPlayerJson(player) << ","
       << "\"isBot\":" << (isBot ? "true" : "false") << "}";

    sRealtimeBridge->EmitEvent(EventNamespace::WORLD_EVENTS, "participant_left", ss.str());
    m_eventsSent++;
#endif
}

void WorldEventBridge::EmitParticipantDied(uint32_t eventId, uint32_t instanceId,
    Player* player, bool isBot)
{
#ifdef MOD_EC_REALTIME_BRIDGE
    if (!player) return;

    std::ostringstream ss;
    ss << "{\"eventId\":" << eventId << ","
       << "\"instanceId\":" << instanceId << ","
       << "\"player\":" << BuildPlayerJson(player) << ","
       << "\"isBot\":" << (isBot ? "true" : "false") << "}";

    sRealtimeBridge->EmitEvent(EventNamespace::WORLD_EVENTS, "participant_died", ss.str());
    m_eventsSent++;
#endif
}

void WorldEventBridge::EmitParticipantRewarded(uint32_t eventId, uint32_t instanceId,
    Player* player, uint32_t gold, uint32_t honor, uint32_t itemId)
{
#ifdef MOD_EC_REALTIME_BRIDGE
    if (!player) return;

    std::ostringstream ss;
    ss << "{\"eventId\":" << eventId << ","
       << "\"instanceId\":" << instanceId << ","
       << "\"player\":" << BuildPlayerJson(player) << ","
       << "\"reward\":{\"gold\":" << gold << ",\"honor\":" << honor
       << ",\"itemId\":" << itemId << "}}";

    sRealtimeBridge->EmitEvent(EventNamespace::WORLD_EVENTS, "participant_rewarded", ss.str());
    m_eventsSent++;
#endif
}

// ============================================================================
// BOSS COMBAT
// ============================================================================

void WorldEventBridge::EmitBossEngaged(uint32_t eventId, uint32_t instanceId,
    Creature* boss, Player* engager)
{
#ifdef MOD_EC_REALTIME_BRIDGE
    std::ostringstream ss;
    ss << "{\"eventId\":" << eventId << ","
       << "\"instanceId\":" << instanceId << ","
       << "\"boss\":" << BuildCreatureJson(boss) << ","
       << "\"engager\":" << BuildPlayerJson(engager) << "}";

    sRealtimeBridge->EmitEvent(EventNamespace::WORLD_EVENTS, "boss_engaged", ss.str());
    m_eventsSent++;
#endif
}

void WorldEventBridge::EmitBossKilled(uint32_t eventId, uint32_t instanceId,
    Creature* boss, Player* killer)
{
#ifdef MOD_EC_REALTIME_BRIDGE
    std::ostringstream ss;
    ss << "{\"eventId\":" << eventId << ","
       << "\"instanceId\":" << instanceId << ","
       << "\"boss\":" << BuildCreatureJson(boss) << ","
       << "\"killer\":" << BuildPlayerJson(killer) << "}";

    sRealtimeBridge->EmitEvent(EventNamespace::WORLD_EVENTS, "boss_killed", ss.str());
    m_eventsSent++;
#endif

    // Notify AliveWorld for gossip generation
#ifdef MOD_ALIVE_WORLD
    if (boss && sAliveWorldMgr->IsEnabled())
    {
        if (auto* eventCollector = sAliveWorldMgr->GetEventCollector())
        {
            // Use the existing RecordBossKilled method
            eventCollector->RecordBossKilled(boss, killer);
        }
    }
#endif
}

void WorldEventBridge::EmitObjectiveCompleted(uint32_t eventId, uint32_t instanceId,
    uint32_t objectiveId, const std::string& objectiveName)
{
#ifdef MOD_EC_REALTIME_BRIDGE
    std::ostringstream ss;
    ss << "{\"eventId\":" << eventId << ","
       << "\"instanceId\":" << instanceId << ","
       << "\"objectiveId\":" << objectiveId << ","
       << "\"objectiveName\":\"" << EscapeJsonString(objectiveName) << "\"}";

    sRealtimeBridge->EmitEvent(EventNamespace::WORLD_EVENTS, "objective_completed", ss.str());
    m_eventsSent++;
#endif
}

// ============================================================================
// BOT MANAGEMENT
// ============================================================================

void WorldEventBridge::EmitBotsRecruited(uint32_t eventId, uint32_t instanceId,
    uint32_t botCount, const std::string& faction)
{
#ifdef MOD_EC_REALTIME_BRIDGE
    std::ostringstream ss;
    ss << "{\"eventId\":" << eventId << ","
       << "\"instanceId\":" << instanceId << ","
       << "\"botCount\":" << botCount << ","
       << "\"faction\":\"" << EscapeJsonString(faction) << "\"}";

    sRealtimeBridge->EmitEvent(EventNamespace::WORLD_EVENTS, "bots_recruited", ss.str());
    m_eventsSent++;
#endif
}

void WorldEventBridge::EmitBotsDismissed(uint32_t eventId, uint32_t instanceId, uint32_t botCount)
{
#ifdef MOD_EC_REALTIME_BRIDGE
    std::ostringstream ss;
    ss << "{\"eventId\":" << eventId << ","
       << "\"instanceId\":" << instanceId << ","
       << "\"botCount\":" << botCount << "}";

    sRealtimeBridge->EmitEvent(EventNamespace::WORLD_EVENTS, "bots_dismissed", ss.str());
    m_eventsSent++;
#endif
}

// ============================================================================
// AUTO-ESCALATION EVENTS
// ============================================================================

void WorldEventBridge::EmitAutoEscalate(uint32_t eventId, uint32_t instanceId,
    const std::string& eventName, uint32_t escalationLevel, uint32_t minutesWithoutDefenders)
{
#ifdef MOD_EC_REALTIME_BRIDGE
    std::ostringstream ss;
    ss << "{\"eventId\":" << eventId << ","
       << "\"instanceId\":" << instanceId << ","
       << "\"eventName\":\"" << EscapeJsonString(eventName) << "\","
       << "\"escalationLevel\":" << escalationLevel << ","
       << "\"minutesWithoutDefenders\":" << minutesWithoutDefenders << ","
       << "\"reason\":\"no_defenders\"}";

    sRealtimeBridge->EmitEvent(EventNamespace::WORLD_EVENTS, "auto_escalate", ss.str());
    m_eventsSent++;
#endif

    LOG_INFO("module.ec-world-events", "Auto-escalate triggered for '{}' - level {} (no defenders for {} minutes)",
        eventName, escalationLevel, minutesWithoutDefenders);
}

// ============================================================================
// BUILDING CAPTURE EVENTS (AV-style towers/bunkers)
// ============================================================================

void WorldEventBridge::EmitBuildingAssault(uint32_t instanceId, uint32_t objectDbId,
    const std::string& buildingName, uint64_t capturerGuid, const std::string& capturerName,
    uint8_t capturerFaction)
{
#ifdef MOD_EC_REALTIME_BRIDGE
    std::ostringstream ss;
    ss << "{\"instanceId\":" << instanceId << ","
       << "\"objectDbId\":" << objectDbId << ","
       << "\"buildingName\":\"" << EscapeJsonString(buildingName) << "\","
       << "\"capturerGuid\":" << capturerGuid << ","
       << "\"capturerName\":\"" << EscapeJsonString(capturerName) << "\","
       << "\"capturerFaction\":" << static_cast<int>(capturerFaction) << ","
       << "\"state\":\"BURNING\"}";

    sRealtimeBridge->EmitEvent(EventNamespace::WORLD_EVENTS, "building_assault", ss.str());
    m_eventsSent++;
#endif

    LOG_INFO("module.ec-world-events", "Building '{}' is under assault by {} (faction {})",
        buildingName, capturerName, capturerFaction);
}

void WorldEventBridge::EmitBuildingDefended(uint32_t instanceId, uint32_t objectDbId,
    const std::string& buildingName)
{
#ifdef MOD_EC_REALTIME_BRIDGE
    std::ostringstream ss;
    ss << "{\"instanceId\":" << instanceId << ","
       << "\"objectDbId\":" << objectDbId << ","
       << "\"buildingName\":\"" << EscapeJsonString(buildingName) << "\","
       << "\"state\":\"INTACT\"}";

    sRealtimeBridge->EmitEvent(EventNamespace::WORLD_EVENTS, "building_defended", ss.str());
    m_eventsSent++;
#endif

    LOG_INFO("module.ec-world-events", "Building '{}' has been defended!", buildingName);
}

void WorldEventBridge::EmitBuildingDestroyed(uint32_t instanceId, uint32_t objectDbId,
    const std::string& buildingName, uint64_t capturerGuid, const std::string& capturerName,
    uint8_t capturerFaction, int32_t points)
{
#ifdef MOD_EC_REALTIME_BRIDGE
    std::ostringstream ss;
    ss << "{\"instanceId\":" << instanceId << ","
       << "\"objectDbId\":" << objectDbId << ","
       << "\"buildingName\":\"" << EscapeJsonString(buildingName) << "\","
       << "\"capturerGuid\":" << capturerGuid << ","
       << "\"capturerName\":\"" << EscapeJsonString(capturerName) << "\","
       << "\"capturerFaction\":" << static_cast<int>(capturerFaction) << ","
       << "\"points\":" << points << ","
       << "\"state\":\"DESTROYED\"}";

    sRealtimeBridge->EmitEvent(EventNamespace::WORLD_EVENTS, "building_destroyed", ss.str());
    m_eventsSent++;
#endif

    LOG_INFO("module.ec-world-events", "Building '{}' DESTROYED by {} (+{} points)!",
        buildingName, capturerName, points);
}

// ============================================================================
// JSON HELPERS
// ============================================================================

std::string WorldEventBridge::BuildPlayerJson(Player* player)
{
    if (!player)
        return "null";

#ifdef MOD_EC_REALTIME_BRIDGE
    std::ostringstream ss;
    ss << "{"
       << "\"guid\":" << player->GetGUID().GetCounter() << ","
       << "\"name\":\"" << EscapeJsonString(player->GetName()) << "\","
       << "\"level\":" << static_cast<int>(player->GetLevel()) << ","
       << "\"class\":" << static_cast<int>(player->getClass()) << ","
       << "\"faction\":" << player->GetTeamId()
       << "}";
    return ss.str();
#else
    return "null";
#endif
}

std::string WorldEventBridge::BuildCreatureJson(Creature* creature)
{
    if (!creature)
        return "null";

#ifdef MOD_EC_REALTIME_BRIDGE
    std::ostringstream ss;
    ss << "{"
       << "\"entry\":" << creature->GetEntry() << ","
       << "\"name\":\"" << EscapeJsonString(creature->GetName()) << "\","
       << "\"guid\":" << creature->GetGUID().GetCounter()
       << "}";
    return ss.str();
#else
    return "null";
#endif
}

} // namespace ECWorldEvents
