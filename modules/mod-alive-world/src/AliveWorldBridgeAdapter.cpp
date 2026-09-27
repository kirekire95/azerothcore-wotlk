/*
 * mod-alive-world - Bridge Adapter Implementation
 * Copyright (C) 2025 EC
 *
 * Unified communication layer between C++ and Node.js backend.
 * See AliveWorldBridgeAdapter.h for full documentation.
 */

#include "AliveWorldBridgeAdapter.h"
#include "AliveWorldManager.h"
#include "AIDecisionEngine.h"
#include "BotBehaviorController.h"
#include "BotMemoryManager.h"
#include "EventCollector.h"
#include "GossipManager.h"
#include "IntelligenceTierManager.h"
#include "WorldStateManager.h"

#include "Creature.h"
#include "Log.h"
#include "Player.h"

#include <iomanip>
#include <sstream>

#ifdef MOD_EC_REALTIME_BRIDGE
using namespace ECRealtimeBridge;
#endif

namespace AliveWorld
{

// ============================================================================
// JSON STRING ESCAPING
// ============================================================================

static std::string EscapeJsonString(const std::string& input)
{
    std::ostringstream ss;
    for (char c : input)
    {
        switch (c)
        {
            case '\"': ss << "\\\""; break;
            case '\\': ss << "\\\\"; break;
            case '\b': ss << "\\b"; break;
            case '\f': ss << "\\f"; break;
            case '\n': ss << "\\n"; break;
            case '\r': ss << "\\r"; break;
            case '\t': ss << "\\t"; break;
            default:
                if (static_cast<unsigned char>(c) < 0x20)
                {
                    ss << "\\u" << std::hex << std::setfill('0') << std::setw(4) << static_cast<int>(c);
                }
                else
                {
                    ss << c;
                }
                break;
        }
    }
    return ss.str();
}

// ============================================================================
// SINGLETON
// ============================================================================

BridgeAdapter* BridgeAdapter::Instance()
{
    static BridgeAdapter instance;
    return &instance;
}

BridgeAdapter::BridgeAdapter()
    : m_eventCollector(nullptr)
    , m_worldStateManager(nullptr)
    , m_memoryManager(nullptr)
    , m_tierManager(nullptr)
    , m_aiEngine(nullptr)
    , m_gossipManager(nullptr)
    , m_eventsSent(0)
    , m_commandsReceived(0)
    , m_queriesHandled(0)
    , m_initialized(false)
    , m_debugMode(false)
{
}

BridgeAdapter::~BridgeAdapter()
{
    Shutdown();
}

// ============================================================================
// LIFECYCLE
// ============================================================================

void BridgeAdapter::Initialize()
{
#ifdef MOD_EC_REALTIME_BRIDGE
    LOG_INFO("module.alive-world", "BridgeAdapter: Registering with shared realtime bridge");

    // Register our command handler with the shared bridge
    // All messages with namespace ALIVE_WORLD will be routed here
    sRealtimeBridge->RegisterHandler(
        EventNamespace::ALIVE_WORLD,
        [this](const BridgeCommand& cmd) { HandleBridgeCommand(cmd); }
    );

    m_initialized = true;
    LOG_INFO("module.alive-world", "BridgeAdapter: Initialized - unified communication ready");
    LOG_INFO("module.alive-world", "BridgeAdapter: Queries and commands routed through TCP socket");
#else
    LOG_WARN("module.alive-world", "BridgeAdapter: mod-ec-realtime-bridge not available!");
    LOG_WARN("module.alive-world", "BridgeAdapter: No communication with Node.js backend possible");
#endif
}

void BridgeAdapter::Shutdown()
{
#ifdef MOD_EC_REALTIME_BRIDGE
    if (m_initialized)
    {
        LOG_INFO("module.alive-world", "BridgeAdapter: Shutting down (sent %lu events, handled %lu queries)",
                 m_eventsSent.load(), m_queriesHandled.load());
        sRealtimeBridge->UnregisterHandler(EventNamespace::ALIVE_WORLD);
        m_initialized = false;
    }
#endif
}

void BridgeAdapter::SetManagers(EventCollector* eventCollector,
                                 WorldStateManager* worldStateManager,
                                 BotMemoryManager* memoryManager,
                                 IntelligenceTierManager* tierManager,
                                 AIDecisionEngine* aiEngine,
                                 GossipManager* gossipManager)
{
    m_eventCollector = eventCollector;
    m_worldStateManager = worldStateManager;
    m_memoryManager = memoryManager;
    m_tierManager = tierManager;
    m_aiEngine = aiEngine;
    m_gossipManager = gossipManager;

    LOG_INFO("module.alive-world", "BridgeAdapter: Manager references set");
}

bool BridgeAdapter::IsConnected() const
{
#ifdef MOD_EC_REALTIME_BRIDGE
    return sRealtimeBridge->IsConnected();
#else
    return false;
#endif
}

bool BridgeAdapter::IsDaemonAvailable() const
{
#ifdef MOD_EC_REALTIME_BRIDGE
    return sRealtimeBridge->IsConnected() && sRealtimeBridge->IsAuthenticated();
#else
    return false;
#endif
}

// ============================================================================
// COMMAND PROCESSING (called from main thread)
// ============================================================================

void BridgeAdapter::ProcessPendingCommands()
{
    // Commands are processed via the registered callback in HandleBridgeCommand.
    // This method exists for compatibility and any main-thread-only processing.
}

// ============================================================================
// COMMAND HANDLER - Main Router
// ============================================================================

#ifdef MOD_EC_REALTIME_BRIDGE

void BridgeAdapter::HandleBridgeCommand(const BridgeCommand& cmd)
{
    m_commandsReceived++;

    // Extract requestId for response matching (if present)
    std::string requestId = ExtractJsonString(cmd.jsonPayload, "requestId");

    if (m_debugMode)
    {
        LOG_DEBUG("module.alive-world", "BridgeAdapter: Received command type='%s' requestId='%s'",
                  cmd.type.c_str(), requestId.c_str());
    }

    // ========================================================================
    // QUERY HANDLERS - Return data to Node.js
    // ========================================================================

    if (cmd.type == "get_stats")
    {
        m_queriesHandled++;
        SendResponse(requestId, HandleGetStats());
    }
    else if (cmd.type == "get_zones")
    {
        m_queriesHandled++;
        SendResponse(requestId, HandleGetZones());
    }
    else if (cmd.type == "get_zone")
    {
        m_queriesHandled++;
        uint32 zoneId = static_cast<uint32>(ExtractJsonInt(cmd.jsonPayload, "zoneId"));
        SendResponse(requestId, HandleGetZone(zoneId));
    }
    else if (cmd.type == "get_gossip")
    {
        m_queriesHandled++;
        uint32 limit = static_cast<uint32>(ExtractJsonInt(cmd.jsonPayload, "limit"));
        if (limit == 0) limit = 50;
        SendResponse(requestId, HandleGetGossip(limit));
    }
    else if (cmd.type == "get_gossip_item")
    {
        m_queriesHandled++;
        uint32 gossipId = static_cast<uint32>(ExtractJsonInt(cmd.jsonPayload, "gossipId"));
        SendResponse(requestId, HandleGetGossipItem(gossipId));
    }
    else if (cmd.type == "get_events")
    {
        m_queriesHandled++;
        uint32 limit = static_cast<uint32>(ExtractJsonInt(cmd.jsonPayload, "limit"));
        if (limit == 0) limit = 100;
        SendResponse(requestId, HandleGetEvents(limit));
    }
    else if (cmd.type == "get_bots")
    {
        m_queriesHandled++;
        SendResponse(requestId, HandleGetBots());
    }
    else if (cmd.type == "get_bot")
    {
        m_queriesHandled++;
        uint64 botGuid = static_cast<uint64>(ExtractJsonInt(cmd.jsonPayload, "botGuid"));
        SendResponse(requestId, HandleGetBot(botGuid));
    }
    else if (cmd.type == "get_tiers")
    {
        m_queriesHandled++;
        SendResponse(requestId, HandleGetTiers());
    }
    else if (cmd.type == "get_ai_queue")
    {
        m_queriesHandled++;
        SendResponse(requestId, HandleGetAIQueue());
    }

    // ========================================================================
    // ACTION HANDLERS - Execute commands
    // ========================================================================

    else if (cmd.type == "bot_say")
    {
        uint64 botGuid = static_cast<uint64>(ExtractJsonInt(cmd.jsonPayload, "botGuid"));
        std::string message = ExtractJsonString(cmd.jsonPayload, "message");
        bool success = HandleBotSay(botGuid, message);
        SendResponse(requestId, success ? "{\"success\":true}" : "{\"success\":false,\"error\":\"Bot not found\"}");
    }
    else if (cmd.type == "bot_emote")
    {
        uint64 botGuid = static_cast<uint64>(ExtractJsonInt(cmd.jsonPayload, "botGuid"));
        std::string emote = ExtractJsonString(cmd.jsonPayload, "emote");
        bool success = HandleBotEmote(botGuid, emote);
        SendResponse(requestId, success ? "{\"success\":true}" : "{\"success\":false,\"error\":\"Bot not found\"}");
    }
    else if (cmd.type == "bot_whisper")
    {
        uint64 botGuid = static_cast<uint64>(ExtractJsonInt(cmd.jsonPayload, "botGuid"));
        uint64 targetGuid = static_cast<uint64>(ExtractJsonInt(cmd.jsonPayload, "targetGuid"));
        std::string message = ExtractJsonString(cmd.jsonPayload, "message");
        bool success = HandleBotWhisper(botGuid, targetGuid, message);
        SendResponse(requestId, success ? "{\"success\":true}" : "{\"success\":false,\"error\":\"Failed\"}");
    }
    else if (cmd.type == "bot_follow")
    {
        uint64 botGuid = static_cast<uint64>(ExtractJsonInt(cmd.jsonPayload, "botGuid"));
        uint64 targetGuid = static_cast<uint64>(ExtractJsonInt(cmd.jsonPayload, "targetGuid"));
        bool success = HandleBotFollow(botGuid, targetGuid);
        SendResponse(requestId, success ? "{\"success\":true}" : "{\"success\":false,\"error\":\"Failed\"}");
    }
    else if (cmd.type == "bot_stay")
    {
        uint64 botGuid = static_cast<uint64>(ExtractJsonInt(cmd.jsonPayload, "botGuid"));
        bool success = HandleBotStay(botGuid);
        SendResponse(requestId, success ? "{\"success\":true}" : "{\"success\":false,\"error\":\"Failed\"}");
    }
    else if (cmd.type == "bot_move_to")
    {
        uint64 botGuid = static_cast<uint64>(ExtractJsonInt(cmd.jsonPayload, "botGuid"));
        float x = static_cast<float>(ExtractJsonDouble(cmd.jsonPayload, "x"));
        float y = static_cast<float>(ExtractJsonDouble(cmd.jsonPayload, "y"));
        float z = static_cast<float>(ExtractJsonDouble(cmd.jsonPayload, "z"));
        bool success = HandleBotMoveTo(botGuid, x, y, z);
        SendResponse(requestId, success ? "{\"success\":true}" : "{\"success\":false,\"error\":\"Failed\"}");
    }
    else if (cmd.type == "bot_attack")
    {
        uint64 botGuid = static_cast<uint64>(ExtractJsonInt(cmd.jsonPayload, "botGuid"));
        uint64 targetGuid = static_cast<uint64>(ExtractJsonInt(cmd.jsonPayload, "targetGuid"));
        bool success = HandleBotAttack(botGuid, targetGuid);
        SendResponse(requestId, success ? "{\"success\":true}" : "{\"success\":false,\"error\":\"Failed\"}");
    }
    else if (cmd.type == "bot_flee")
    {
        uint64 botGuid = static_cast<uint64>(ExtractJsonInt(cmd.jsonPayload, "botGuid"));
        bool success = HandleBotFlee(botGuid);
        SendResponse(requestId, success ? "{\"success\":true}" : "{\"success\":false,\"error\":\"Failed\"}");
    }
    else if (cmd.type == "set_goal")
    {
        uint64 botGuid = static_cast<uint64>(ExtractJsonInt(cmd.jsonPayload, "botGuid"));
        uint8 goalType = static_cast<uint8>(ExtractJsonInt(cmd.jsonPayload, "goalType"));
        std::string target = ExtractJsonString(cmd.jsonPayload, "target");
        uint8 priority = static_cast<uint8>(ExtractJsonInt(cmd.jsonPayload, "priority"));
        bool success = HandleSetGoal(botGuid, goalType, target, priority);
        SendResponse(requestId, success ? "{\"success\":true}" : "{\"success\":false,\"error\":\"Failed\"}");
    }
    else if (cmd.type == "cancel_goal")
    {
        uint64 botGuid = static_cast<uint64>(ExtractJsonInt(cmd.jsonPayload, "botGuid"));
        uint32 goalId = static_cast<uint32>(ExtractJsonInt(cmd.jsonPayload, "goalId"));
        bool success = HandleCancelGoal(botGuid, goalId);
        SendResponse(requestId, success ? "{\"success\":true}" : "{\"success\":false,\"error\":\"Failed\"}");
    }

    // ========================================================================
    // BOT BEHAVIOR CONTROLLER COMMANDS
    // ========================================================================

    else if (cmd.type == "set_bot_stance")
    {
        uint64 botGuid = static_cast<uint64>(ExtractJsonInt(cmd.jsonPayload, "botGuid"));
        std::string stance = ExtractJsonString(cmd.jsonPayload, "stance");
        bool success = HandleSetBotStance(botGuid, stance);
        SendResponse(requestId, success ? "{\"success\":true}" : "{\"success\":false,\"error\":\"Invalid stance or bot not found\"}");
    }
    else if (cmd.type == "set_bot_behavior_profile")
    {
        uint64 botGuid = static_cast<uint64>(ExtractJsonInt(cmd.jsonPayload, "botGuid"));
        std::string profileJson = ExtractJsonString(cmd.jsonPayload, "profile");
        bool success = HandleSetBotBehaviorProfile(botGuid, profileJson);
        SendResponse(requestId, success ? "{\"success\":true}" : "{\"success\":false,\"error\":\"Failed to set profile\"}");
    }
    else if (cmd.type == "toggle_bot_debug_thoughts")
    {
        uint64 botGuid = static_cast<uint64>(ExtractJsonInt(cmd.jsonPayload, "botGuid"));
        bool enabled = ExtractJsonBool(cmd.jsonPayload, "enabled");
        bool success = HandleToggleBotDebugThoughts(botGuid, enabled);
        SendResponse(requestId, success ? "{\"success\":true}" : "{\"success\":false,\"error\":\"Bot not found\"}");
    }
    else if (cmd.type == "get_bot_behavior")
    {
        m_queriesHandled++;
        uint64 botGuid = static_cast<uint64>(ExtractJsonInt(cmd.jsonPayload, "botGuid"));
        SendResponse(requestId, HandleGetBotBehavior(botGuid));
    }

    // ========================================================================
    // SYSTEM COMMANDS
    // ========================================================================

    else if (cmd.type == "set_debug")
    {
        m_debugMode = ExtractJsonBool(cmd.jsonPayload, "enabled");
        LOG_INFO("module.alive-world", "BridgeAdapter: Debug mode %s", m_debugMode ? "ENABLED" : "DISABLED");
        SendResponse(requestId, m_debugMode ? "{\"debug\":true}" : "{\"debug\":false}");
    }
    else if (cmd.type == "ping")
    {
        SendResponse(requestId, "{\"pong\":true,\"timestamp\":" +
                     std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(
                         std::chrono::system_clock::now().time_since_epoch()).count()) + "}");
    }
    else
    {
        LOG_WARN("module.alive-world", "BridgeAdapter: Unknown command type '%s'", cmd.type.c_str());
        if (!requestId.empty())
        {
            SendErrorResponse(requestId, "Unknown command: " + cmd.type, 400);
        }
    }
}

void BridgeAdapter::SendResponse(const std::string& requestId, const std::string& jsonData)
{
    if (requestId.empty())
        return;

    std::ostringstream ss;
    ss << "{\"requestId\":\"" << requestId << "\",\"data\":" << jsonData << "}";

    sRealtimeBridge->EmitEvent(EventNamespace::ALIVE_WORLD, "response", ss.str());
}

void BridgeAdapter::SendErrorResponse(const std::string& requestId, const std::string& error, int code)
{
    if (requestId.empty())
        return;

    std::ostringstream ss;
    ss << "{\"requestId\":\"" << requestId << "\",\"error\":\"" << EscapeJsonString(error)
       << "\",\"code\":" << code << "}";

    sRealtimeBridge->EmitEvent(EventNamespace::ALIVE_WORLD, "error", ss.str());
}

#endif // MOD_EC_REALTIME_BRIDGE

// ============================================================================
// QUERY HANDLERS
// ============================================================================

std::string BridgeAdapter::HandleGetStats()
{
    APIStats stats = sAliveWorldMgr->GetStats();

    std::ostringstream ss;
    ss << "{";
    ss << "\"totalBots\":" << stats.totalBots << ",";
    ss << "\"tier1Count\":" << stats.tier1Count << ",";
    ss << "\"tier2Count\":" << stats.tier2Count << ",";
    ss << "\"tier3Count\":" << stats.tier3Count << ",";
    ss << "\"activeZones\":" << stats.activeZones << ",";
    ss << "\"gossipPoolSize\":" << stats.gossipPoolSize << ",";
    ss << "\"eventBufferSize\":" << stats.eventBufferSize << ",";
    ss << "\"pendingAIRequests\":" << stats.pendingAIRequests << ",";
    ss << "\"cacheHitRate\":" << std::fixed << std::setprecision(2) << stats.cacheHitRate << ",";
    ss << "\"uptimeSeconds\":" << stats.uptimeSeconds << ",";
    ss << "\"eventsSent\":" << m_eventsSent.load() << ",";
    ss << "\"queriesHandled\":" << m_queriesHandled.load() << ",";
    ss << "\"commandsReceived\":" << m_commandsReceived.load();
    ss << "}";

    return ss.str();
}

std::string BridgeAdapter::HandleGetZones()
{
    if (!m_worldStateManager)
        return "{\"error\":\"World state manager not available\",\"zones\":[]}";

    auto zones = m_worldStateManager->GetAllZoneStates();

    std::ostringstream ss;
    ss << "{\"zones\":[";

    for (size_t i = 0; i < zones.size(); ++i)
    {
        if (i > 0) ss << ",";
        const auto& z = zones[i];
        ss << "{";
        ss << "\"zoneId\":" << z.zoneId << ",";
        ss << "\"mapId\":" << z.mapId << ",";
        ss << "\"alliancePlayers\":" << z.alliancePlayerCount << ",";
        ss << "\"hordePlayers\":" << z.hordePlayerCount << ",";
        ss << "\"allianceBots\":" << z.allianceBotCount << ",";
        ss << "\"hordeBots\":" << z.hordeBotCount << ",";
        ss << "\"activityLevel\":" << static_cast<int>(z.activityLevel) << ",";
        ss << "\"dangerLevel\":" << static_cast<int>(z.dangerLevel) << ",";
        ss << "\"lastUpdate\":" << z.lastUpdateTime;
        ss << "}";
    }

    ss << "]}";
    return ss.str();
}

std::string BridgeAdapter::HandleGetZone(uint32 zoneId)
{
    if (!m_worldStateManager)
        return "{\"error\":\"World state manager not available\"}";

    ZoneState z = m_worldStateManager->GetZoneState(zoneId);
    if (z.zoneId == 0)
        return "{\"error\":\"Zone not found\"}";

    std::ostringstream ss;
    ss << "{";
    ss << "\"zoneId\":" << z.zoneId << ",";
    ss << "\"mapId\":" << z.mapId << ",";
    ss << "\"alliancePlayers\":" << z.alliancePlayerCount << ",";
    ss << "\"hordePlayers\":" << z.hordePlayerCount << ",";
    ss << "\"allianceBots\":" << z.allianceBotCount << ",";
    ss << "\"hordeBots\":" << z.hordeBotCount << ",";
    ss << "\"activityLevel\":" << static_cast<int>(z.activityLevel) << ",";
    ss << "\"dangerLevel\":" << static_cast<int>(z.dangerLevel) << ",";
    ss << "\"lastUpdate\":" << z.lastUpdateTime;
    ss << "}";

    return ss.str();
}

std::string BridgeAdapter::HandleGetGossip(uint32 limit)
{
    if (!m_worldStateManager)
        return "{\"error\":\"World state manager not available\",\"gossip\":[]}";

    auto gossipList = m_worldStateManager->GetRecentGossip(limit);

    std::ostringstream ss;
    ss << "{\"gossip\":[";

    for (size_t i = 0; i < gossipList.size(); ++i)
    {
        if (i > 0) ss << ",";
        const auto& g = gossipList[i];
        ss << "{";
        ss << "\"id\":" << g.gossipId << ",";
        ss << "\"headline\":\"" << EscapeJsonString(g.headline) << "\",";
        ss << "\"details\":\"" << EscapeJsonString(g.details) << "\",";
        ss << "\"timestamp\":" << g.timestamp << ",";
        ss << "\"expiresAt\":" << g.expiresAt << ",";
        ss << "\"zoneId\":" << g.originZoneId << ",";
        ss << "\"importance\":" << static_cast<int>(g.importance) << ",";
        ss << "\"spreadRadius\":" << static_cast<int>(g.spreadRadius) << ",";
        ss << "\"knownByCount\":" << g.knownByBots.size();
        ss << "}";
    }

    ss << "]}";
    return ss.str();
}

std::string BridgeAdapter::HandleGetGossipItem(uint32 gossipId)
{
    if (!m_worldStateManager)
        return "{\"error\":\"World state manager not available\"}";

    auto allGossip = m_worldStateManager->GetRecentGossip(500);
    for (const auto& g : allGossip)
    {
        if (g.gossipId == gossipId)
        {
            std::ostringstream ss;
            ss << "{";
            ss << "\"id\":" << g.gossipId << ",";
            ss << "\"headline\":\"" << EscapeJsonString(g.headline) << "\",";
            ss << "\"details\":\"" << EscapeJsonString(g.details) << "\",";
            ss << "\"timestamp\":" << g.timestamp << ",";
            ss << "\"expiresAt\":" << g.expiresAt << ",";
            ss << "\"zoneId\":" << g.originZoneId << ",";
            ss << "\"importance\":" << static_cast<int>(g.importance) << ",";
            ss << "\"spreadRadius\":" << static_cast<int>(g.spreadRadius) << ",";
            ss << "\"sourceEventId\":" << g.sourceEventId << ",";
            ss << "\"type\":" << static_cast<int>(g.type) << ",";
            ss << "\"knownByBots\":[" ;
            size_t idx = 0;
            for (uint64 botGuid : g.knownByBots)
            {
                if (idx > 0) ss << ",";
                ss << botGuid;
                idx++;
            }
            ss << "]";
            ss << "}";
            return ss.str();
        }
    }

    return "{\"error\":\"Gossip not found\"}";
}

std::string BridgeAdapter::HandleGetEvents(uint32 limit)
{
    if (!m_eventCollector)
        return "{\"error\":\"Event collector not available\",\"events\":[]}";

    auto events = m_eventCollector->GetRecentEvents(limit);

    std::ostringstream ss;
    ss << "{\"events\":[";

    for (size_t i = 0; i < events.size(); ++i)
    {
        if (i > 0) ss << ",";
        const auto& e = events[i];
        ss << "{";
        ss << "\"id\":" << e.eventId << ",";
        ss << "\"type\":" << static_cast<int>(e.type) << ",";
        ss << "\"typeName\":\"" << EventTypeToString(e.type) << "\",";
        ss << "\"timestamp\":" << e.timestamp << ",";
        ss << "\"zoneId\":" << e.zoneId << ",";
        ss << "\"areaId\":" << e.areaId << ",";
        ss << "\"mapId\":" << e.mapId << ",";
        ss << "\"primaryName\":\"" << EscapeJsonString(e.primaryName) << "\",";
        ss << "\"secondaryName\":\"" << EscapeJsonString(e.secondaryName) << "\",";
        ss << "\"details\":\"" << EscapeJsonString(e.details) << "\",";
        ss << "\"importance\":" << static_cast<int>(e.importance) << ",";
        ss << "\"processed\":" << (e.processed ? "true" : "false");
        ss << "}";
    }

    ss << "]}";
    return ss.str();
}

std::string BridgeAdapter::HandleGetBots()
{
    if (!m_tierManager)
        return "{\"error\":\"Tier manager not available\"}";

    std::ostringstream ss;
    ss << "{";
    ss << "\"tier1\":" << m_tierManager->GetTier1Count() << ",";
    ss << "\"tier2\":" << m_tierManager->GetTier2Count() << ",";
    ss << "\"tier3\":" << m_tierManager->GetTier3Count() << ",";
    ss << "\"total\":" << (m_tierManager->GetTier1Count() + m_tierManager->GetTier2Count() + m_tierManager->GetTier3Count()) << ",";

    // Include list of tier 1 bot GUIDs
    ss << "\"tier1Bots\":[";
    auto tier1 = m_tierManager->GetTier1Bots();
    for (size_t i = 0; i < tier1.size(); ++i)
    {
        if (i > 0) ss << ",";
        ss << tier1[i];
    }
    ss << "]";

    ss << "}";
    return ss.str();
}

std::string BridgeAdapter::HandleGetBot(uint64 botGuid)
{
    if (!m_memoryManager)
        return "{\"error\":\"Memory manager not available\"}";

    auto memory = m_memoryManager->GetBotMemory(botGuid);
    if (!memory)
        return "{\"error\":\"Bot not found\"}";

    std::ostringstream ss;
    ss << "{";
    ss << "\"guid\":" << memory->botGuid << ",";
    ss << "\"name\":\"" << EscapeJsonString(memory->botName) << "\",";
    ss << "\"tier\":" << static_cast<int>(memory->currentTier) << ",";
    ss << "\"tierName\":\"" << TierToString(memory->currentTier) << "\",";

    // Personality
    ss << "\"personality\":{";
    ss << "\"aggression\":" << std::fixed << std::setprecision(2) << memory->personality.aggression << ",";
    ss << "\"caution\":" << memory->personality.caution << ",";
    ss << "\"sociability\":" << memory->personality.sociability << ",";
    ss << "\"loyalty\":" << memory->personality.loyalty << ",";
    ss << "\"curiosity\":" << memory->personality.curiosity << ",";
    ss << "\"vengefulness\":" << memory->personality.vengefulness << ",";
    ss << "\"helpfulness\":" << memory->personality.helpfulness << ",";
    ss << "\"greed\":" << memory->personality.greed << ",";
    ss << "\"mood\":\"" << EscapeJsonString(memory->personality.currentMood) << "\"";
    ss << "},";

    // Location
    ss << "\"currentZone\":" << memory->currentZoneId << ",";
    ss << "\"favoriteZone\":" << memory->favoriteZoneId << ",";

    // Combat log (last 10)
    ss << "\"combatLog\":[";
    size_t logCount = std::min(memory->combatLog.size(), static_cast<size_t>(10));
    for (size_t i = 0; i < logCount; ++i)
    {
        if (i > 0) ss << ",";
        const auto& entry = memory->combatLog[i];
        ss << "{\"timestamp\":" << entry.timestamp;
        ss << ",\"opponent\":\"" << EscapeJsonString(entry.opponentName) << "\"";
        ss << ",\"won\":" << (entry.wasVictorious ? "true" : "false");
        ss << ",\"damageDealt\":" << entry.damageDealt;
        ss << ",\"damageTaken\":" << entry.damageTaken << "}";
    }
    ss << "],";

    // Social connections (first 20)
    ss << "\"socialConnections\":[";
    size_t connCount = 0;
    for (const auto& conn : memory->socialConnections)
    {
        if (connCount >= 20) break;
        if (connCount > 0) ss << ",";
        ss << "{\"guid\":" << conn.first;
        ss << ",\"name\":\"" << EscapeJsonString(conn.second.playerName) << "\"";
        ss << ",\"sentiment\":" << std::fixed << std::setprecision(2) << conn.second.sentimentScore;
        ss << ",\"interactions\":" << conn.second.interactionCount << "}";
        connCount++;
    }
    ss << "],";

    // Known gossip IDs
    ss << "\"knownGossip\":[";
    size_t gossipIdx = 0;
    for (uint32 gId : memory->knownGossipIds)
    {
        if (gossipIdx > 0) ss << ",";
        ss << gId;
        gossipIdx++;
    }
    ss << "],";

    // Last AI decision
    ss << "\"lastDecision\":\"" << EscapeJsonString(memory->lastAIDecision) << "\"";

    ss << "}";
    return ss.str();
}

std::string BridgeAdapter::HandleGetTiers()
{
    if (!m_tierManager)
        return "{\"error\":\"Tier manager not available\"}";

    uint32 t1 = m_tierManager->GetTier1Count();
    uint32 t2 = m_tierManager->GetTier2Count();
    uint32 t3 = m_tierManager->GetTier3Count();

    std::ostringstream ss;
    ss << "{";
    ss << "\"tier1\":" << t1 << ",";
    ss << "\"tier2\":" << t2 << ",";
    ss << "\"tier3\":" << t3 << ",";
    ss << "\"total\":" << (t1 + t2 + t3);
    ss << "}";

    return ss.str();
}

std::string BridgeAdapter::HandleGetAIQueue()
{
    if (!m_aiEngine)
        return "{\"error\":\"AI engine not available\"}";

    std::ostringstream ss;
    ss << "{";
    ss << "\"pending\":" << m_aiEngine->GetPendingRequestCount() << ",";
    ss << "\"processing\":" << m_aiEngine->GetProcessingCount() << ",";
    ss << "\"maxConcurrent\":" << m_aiEngine->GetMaxConcurrent() << ",";
    ss << "\"totalRequests\":" << m_aiEngine->GetTotalRequests() << ",";
    ss << "\"successfulRequests\":" << m_aiEngine->GetSuccessfulRequests() << ",";
    ss << "\"failedRequests\":" << m_aiEngine->GetFailedRequests() << ",";
    ss << "\"cacheSize\":" << m_aiEngine->GetCacheSize() << ",";
    ss << "\"cacheHitRate\":" << std::fixed << std::setprecision(1) << m_aiEngine->GetCacheHitRate() << ",";
    ss << "\"avgResponseTime\":" << std::fixed << std::setprecision(1) << m_aiEngine->GetAverageResponseTime();
    ss << "}";

    return ss.str();
}

// ============================================================================
// ACTION HANDLERS
// ============================================================================

bool BridgeAdapter::HandleBotSay(uint64 botGuid, const std::string& message)
{
    SocketCommand cmd = SocketCommand::Say(botGuid, message);
    sAliveWorldMgr->HandleSocketCommand(cmd);
    return true;
}

bool BridgeAdapter::HandleBotEmote(uint64 botGuid, const std::string& emote)
{
    // Convert emote string to ID (try to parse as number first)
    uint32 emoteId = 0;
    try {
        emoteId = std::stoul(emote);
    } catch (...) {
        // If not a number, use a default emote
        emoteId = 1;  // TEXT_EMOTE_WAVE
    }
    SocketCommand cmd = SocketCommand::Emote(botGuid, emoteId);
    sAliveWorldMgr->HandleSocketCommand(cmd);
    return true;
}

bool BridgeAdapter::HandleBotWhisper(uint64 botGuid, uint64 targetGuid, const std::string& message)
{
    SocketCommand cmd;
    cmd.type = SocketCommandType::BOT_WHISPER;
    cmd.botGuid = botGuid;
    cmd.targetGuid = targetGuid;
    cmd.message = message;
    sAliveWorldMgr->HandleSocketCommand(cmd);
    return true;
}

bool BridgeAdapter::HandleBotFollow(uint64 botGuid, uint64 targetGuid)
{
    SocketCommand cmd = SocketCommand::Follow(botGuid, targetGuid);
    sAliveWorldMgr->HandleSocketCommand(cmd);
    return true;
}

bool BridgeAdapter::HandleBotStay(uint64 botGuid)
{
    SocketCommand cmd;
    cmd.type = SocketCommandType::BOT_STAY;
    cmd.botGuid = botGuid;
    sAliveWorldMgr->HandleSocketCommand(cmd);
    return true;
}

bool BridgeAdapter::HandleBotMoveTo(uint64 botGuid, float x, float y, float z)
{
    SocketCommand cmd;
    cmd.type = SocketCommandType::BOT_MOVE_TO;
    cmd.botGuid = botGuid;
    cmd.x = x;
    cmd.y = y;
    cmd.z = z;
    sAliveWorldMgr->HandleSocketCommand(cmd);
    return true;
}

bool BridgeAdapter::HandleBotAttack(uint64 botGuid, uint64 targetGuid)
{
    SocketCommand cmd = SocketCommand::Attack(botGuid, targetGuid);
    sAliveWorldMgr->HandleSocketCommand(cmd);
    return true;
}

bool BridgeAdapter::HandleBotFlee(uint64 botGuid)
{
    SocketCommand cmd;
    cmd.type = SocketCommandType::BOT_FLEE;
    cmd.botGuid = botGuid;
    sAliveWorldMgr->HandleSocketCommand(cmd);
    return true;
}

bool BridgeAdapter::HandleSetGoal(uint64 botGuid, uint8 goalType, const std::string& target, uint8 priority)
{
    SocketCommand cmd;
    cmd.type = SocketCommandType::SET_GOAL;
    cmd.botGuid = botGuid;
    cmd.numParam = goalType;
    cmd.message = target;
    // Priority could be encoded in another field
    sAliveWorldMgr->HandleSocketCommand(cmd);
    return true;
}

bool BridgeAdapter::HandleCancelGoal(uint64 botGuid, uint32 goalId)
{
    SocketCommand cmd;
    cmd.type = SocketCommandType::CANCEL_GOAL;
    cmd.botGuid = botGuid;
    cmd.numParam = goalId;
    sAliveWorldMgr->HandleSocketCommand(cmd);
    return true;
}

// ============================================================================
// BOT BEHAVIOR CONTROLLER HANDLERS
// ============================================================================

bool BridgeAdapter::HandleSetBotStance(uint64 botGuid, const std::string& stance)
{
    BotStance newStance;
    if (stance == "passive")
        newStance = BotStance::PASSIVE;
    else if (stance == "defensive")
        newStance = BotStance::DEFENSIVE;
    else if (stance == "aggressive")
        newStance = BotStance::AGGRESSIVE;
    else
    {
        LOG_WARN("module.alive-world", "BridgeAdapter: Unknown stance '%s' for bot %lu", stance.c_str(), botGuid);
        return false;
    }

    sBotBehaviorController->SetStance(botGuid, newStance, "bridge");
    return true;
}

bool BridgeAdapter::HandleSetBotBehaviorProfile(uint64 botGuid, const std::string& profileJson)
{
    // Parse profile fields from the JSON string
    // Expected format: {"stance":"defensive","attackNeutrals":false,"assistParty":true,
    //                    "followDistance":10,"aggroRadius":30,"customInstructions":"..."}
    BotBehaviorProfile profile = sBotBehaviorController->GetProfile(botGuid);

    // Parse stance
    std::string stanceStr = ExtractJsonString(profileJson, "stance");
    if (!stanceStr.empty())
    {
        if (stanceStr == "passive") profile.stance = BotStance::PASSIVE;
        else if (stanceStr == "defensive") profile.stance = BotStance::DEFENSIVE;
        else if (stanceStr == "aggressive") profile.stance = BotStance::AGGRESSIVE;
    }

    // Parse boolean fields (check if present before overwriting)
    std::string attackNeutralsStr = ExtractJsonString(profileJson, "attackNeutrals");
    if (!attackNeutralsStr.empty())
        profile.attackNeutrals = (attackNeutralsStr == "true");

    std::string assistPartyStr = ExtractJsonString(profileJson, "assistParty");
    if (!assistPartyStr.empty())
        profile.assistParty = (assistPartyStr == "true");

    std::string autoLootStr = ExtractJsonString(profileJson, "autoLoot");
    if (!autoLootStr.empty())
        profile.autoLoot = (autoLootStr == "true");

    // Parse numeric fields
    float followDist = static_cast<float>(ExtractJsonDouble(profileJson, "followDistance"));
    if (followDist > 0.0f)
        profile.followDistance = followDist;

    float aggroRad = static_cast<float>(ExtractJsonDouble(profileJson, "aggroRadius"));
    if (aggroRad > 0.0f)
        profile.aggroRadius = aggroRad;

    // Parse custom instructions
    std::string instructions = ExtractJsonString(profileJson, "customInstructions");
    if (!instructions.empty())
        profile.customInstructions = instructions;

    sBotBehaviorController->SetProfile(botGuid, profile, "bridge");
    return true;
}

bool BridgeAdapter::HandleToggleBotDebugThoughts(uint64 botGuid, bool enabled)
{
    sBotBehaviorController->SetDebugThoughts(botGuid, enabled);
    return true;
}

std::string BridgeAdapter::HandleGetBotBehavior(uint64 botGuid)
{
    if (!sBotBehaviorController->HasProfile(botGuid))
        return "{\"error\":\"No behavior profile for this bot\",\"hasProfile\":false}";

    auto profile = sBotBehaviorController->GetProfile(botGuid);
    auto stance = sBotBehaviorController->GetStance(botGuid);
    bool debugThoughts = sBotBehaviorController->HasDebugThoughts(botGuid);

    std::ostringstream ss;
    ss << "{";
    ss << "\"hasProfile\":true,";
    ss << "\"botGuid\":" << botGuid << ",";
    ss << "\"stance\":\"" << BotStanceToString(stance) << "\",";
    ss << "\"stanceId\":" << static_cast<int>(stance) << ",";
    ss << "\"attackNeutrals\":" << (profile.attackNeutrals ? "true" : "false") << ",";
    ss << "\"assistParty\":" << (profile.assistParty ? "true" : "false") << ",";
    ss << "\"autoLoot\":" << (profile.autoLoot ? "true" : "false") << ",";
    ss << "\"followDistance\":" << std::fixed << std::setprecision(1) << profile.followDistance << ",";
    ss << "\"aggroRadius\":" << std::fixed << std::setprecision(1) << profile.aggroRadius << ",";
    ss << "\"debugThoughts\":" << (debugThoughts ? "true" : "false") << ",";
    ss << "\"customInstructions\":\"" << EscapeJsonString(profile.customInstructions) << "\",";
    ss << "\"ownerGuid\":" << profile.ownerGuid << ",";
    ss << "\"lastModifiedMs\":" << profile.lastModifiedMs;
    ss << "}";

    return ss.str();
}

// ============================================================================
// PLAYER EVENTS
// ============================================================================

void BridgeAdapter::EmitPlayerLogin(Player* player)
{
#ifdef MOD_EC_REALTIME_BRIDGE
    if (!player) return;

    std::ostringstream ss;
    ss << "{\"player\":" << BuildPlayerJson(player) << "}";

    sRealtimeBridge->EmitEvent(EventNamespace::ALIVE_WORLD, "player_login", ss.str());
    m_eventsSent++;
#endif
}

void BridgeAdapter::EmitPlayerLogout(Player* player)
{
#ifdef MOD_EC_REALTIME_BRIDGE
    if (!player) return;

    std::ostringstream ss;
    ss << "{\"player\":" << BuildPlayerJson(player) << "}";

    sRealtimeBridge->EmitEvent(EventNamespace::ALIVE_WORLD, "player_logout", ss.str());
    m_eventsSent++;
#endif
}

void BridgeAdapter::EmitPlayerChat(Player* player, uint32 type, const std::string& message)
{
#ifdef MOD_EC_REALTIME_BRIDGE
    if (!player) return;

    std::ostringstream ss;
    ss << "{\"player\":" << BuildPlayerJson(player) << ","
       << "\"chatType\":" << type << ","
       << "\"message\":\"" << EscapeJsonString(message) << "\"}";

    sRealtimeBridge->EmitEvent(EventNamespace::ALIVE_WORLD, "player_chat", ss.str());
    m_eventsSent++;
#endif
}

void BridgeAdapter::EmitPlayerEmote(Player* player, uint32 emoteId)
{
#ifdef MOD_EC_REALTIME_BRIDGE
    if (!player) return;

    std::ostringstream ss;
    ss << "{\"player\":" << BuildPlayerJson(player) << ","
       << "\"emoteId\":" << emoteId << "}";

    sRealtimeBridge->EmitEvent(EventNamespace::ALIVE_WORLD, "player_emote", ss.str());
    m_eventsSent++;
#endif
}

void BridgeAdapter::EmitPlayerZoneChange(Player* player, uint32 newZoneId, uint32 newAreaId)
{
#ifdef MOD_EC_REALTIME_BRIDGE
    if (!player) return;

    std::ostringstream ss;
    ss << "{\"player\":" << BuildPlayerJson(player) << ","
       << "\"newZoneId\":" << newZoneId << ","
       << "\"newAreaId\":" << newAreaId << "}";

    sRealtimeBridge->EmitEvent(EventNamespace::ALIVE_WORLD, "player_zone_change", ss.str());
    m_eventsSent++;
#endif
}

// ============================================================================
// BOT EVENTS
// ============================================================================

void BridgeAdapter::EmitBotTierChanged(uint32 botGuid, BotTier oldTier, BotTier newTier, const std::string& reason)
{
#ifdef MOD_EC_REALTIME_BRIDGE
    // Get bot name from memory manager
    std::string botName = "Unknown";
    if (m_memoryManager)
    {
        auto memory = m_memoryManager->GetBotMemory(botGuid);
        if (memory)
            botName = memory->botName;
    }

    // Get current timestamp
    auto now = std::chrono::system_clock::now();
    auto timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()).count();

    std::ostringstream ss;
    ss << "{\"botGuid\":" << botGuid << ","
       << "\"botName\":\"" << EscapeJsonString(botName) << "\","
       << "\"oldTier\":" << static_cast<int>(oldTier) << ","
       << "\"newTier\":" << static_cast<int>(newTier) << ","
       << "\"reason\":\"" << EscapeJsonString(reason) << "\","
       << "\"timestamp\":" << timestamp << "}";

    sRealtimeBridge->EmitEvent(EventNamespace::ALIVE_WORLD, "bot_tier_changed", ss.str());
    m_eventsSent++;
#endif
}

void BridgeAdapter::EmitBotNearPlayer(uint32 botGuid, uint32 playerGuid, float distance)
{
#ifdef MOD_EC_REALTIME_BRIDGE
    std::ostringstream ss;
    ss << "{\"botGuid\":" << botGuid << ","
       << "\"playerGuid\":" << playerGuid << ","
       << "\"distance\":" << distance << "}";

    sRealtimeBridge->EmitEvent(EventNamespace::ALIVE_WORLD, "bot_near_player", ss.str());
    m_eventsSent++;
#endif
}

// ============================================================================
// COMBAT EVENTS
// ============================================================================

void BridgeAdapter::EmitCombatStart(uint32 botGuid, Unit* target)
{
#ifdef MOD_EC_REALTIME_BRIDGE
    std::ostringstream ss;
    ss << "{\"botGuid\":" << botGuid << ","
       << "\"target\":" << BuildUnitJson(target) << "}";

    sRealtimeBridge->EmitEvent(EventNamespace::ALIVE_WORLD, "combat_start", ss.str());
    m_eventsSent++;
#endif
}

void BridgeAdapter::EmitCreatureKilled(Player* killer, uint32 creatureEntry)
{
#ifdef MOD_EC_REALTIME_BRIDGE
    std::ostringstream ss;
    ss << "{\"player\":" << BuildPlayerJson(killer) << ","
       << "\"creatureEntry\":" << creatureEntry << "}";

    sRealtimeBridge->EmitEvent(EventNamespace::ALIVE_WORLD, "creature_killed", ss.str());
    m_eventsSent++;
#endif
}

void BridgeAdapter::EmitPlayerKilled(Player* killer, Player* victim)
{
#ifdef MOD_EC_REALTIME_BRIDGE
    std::ostringstream ss;
    ss << "{\"killer\":" << BuildPlayerJson(killer) << ","
       << "\"victim\":" << BuildPlayerJson(victim) << "}";

    sRealtimeBridge->EmitEvent(EventNamespace::ALIVE_WORLD, "player_killed", ss.str());
    m_eventsSent++;
#endif
}

void BridgeAdapter::EmitPlayerDeath(Player* player, Unit* killer)
{
#ifdef MOD_EC_REALTIME_BRIDGE
    std::ostringstream ss;
    ss << "{\"player\":" << BuildPlayerJson(player) << ","
       << "\"killer\":" << BuildUnitJson(killer) << "}";

    sRealtimeBridge->EmitEvent(EventNamespace::ALIVE_WORLD, "player_died", ss.str());
    m_eventsSent++;
#endif
}

void BridgeAdapter::EmitBotLowHealth(uint32 botGuid, float healthPct)
{
#ifdef MOD_EC_REALTIME_BRIDGE
    std::ostringstream ss;
    ss << "{\"botGuid\":" << botGuid << ","
       << "\"healthPct\":" << healthPct << "}";

    sRealtimeBridge->EmitEvent(EventNamespace::ALIVE_WORLD, "bot_low_health", ss.str());
    m_eventsSent++;
#endif
}

void BridgeAdapter::EmitBotLowMana(uint32 botGuid, float manaPct)
{
#ifdef MOD_EC_REALTIME_BRIDGE
    std::ostringstream ss;
    ss << "{\"botGuid\":" << botGuid << ","
       << "\"manaPct\":" << manaPct << "}";

    sRealtimeBridge->EmitEvent(EventNamespace::ALIVE_WORLD, "bot_low_mana", ss.str());
    m_eventsSent++;
#endif
}

// ============================================================================
// SOCIAL EVENTS
// ============================================================================

void BridgeAdapter::EmitGroupFormed(Player* leader)
{
#ifdef MOD_EC_REALTIME_BRIDGE
    std::ostringstream ss;
    ss << "{\"leader\":" << BuildPlayerJson(leader) << "}";

    sRealtimeBridge->EmitEvent(EventNamespace::ALIVE_WORLD, "group_formed", ss.str());
    m_eventsSent++;
#endif
}

void BridgeAdapter::EmitGroupDisbanded(Player* leader)
{
#ifdef MOD_EC_REALTIME_BRIDGE
    std::ostringstream ss;
    ss << "{\"leader\":" << BuildPlayerJson(leader) << "}";

    sRealtimeBridge->EmitEvent(EventNamespace::ALIVE_WORLD, "group_disbanded", ss.str());
    m_eventsSent++;
#endif
}

void BridgeAdapter::EmitDuelStart(Player* player1, Player* player2)
{
#ifdef MOD_EC_REALTIME_BRIDGE
    std::ostringstream ss;
    ss << "{\"player1\":" << BuildPlayerJson(player1) << ","
       << "\"player2\":" << BuildPlayerJson(player2) << "}";

    sRealtimeBridge->EmitEvent(EventNamespace::ALIVE_WORLD, "duel_start", ss.str());
    m_eventsSent++;
#endif
}

void BridgeAdapter::EmitDuelEnd(Player* winner, Player* loser)
{
#ifdef MOD_EC_REALTIME_BRIDGE
    std::ostringstream ss;
    ss << "{\"winner\":" << BuildPlayerJson(winner) << ","
       << "\"loser\":" << BuildPlayerJson(loser) << "}";

    sRealtimeBridge->EmitEvent(EventNamespace::ALIVE_WORLD, "duel_end", ss.str());
    m_eventsSent++;
#endif
}

void BridgeAdapter::EmitTradeStarted(Player* player1, Player* player2)
{
#ifdef MOD_EC_REALTIME_BRIDGE
    std::ostringstream ss;
    ss << "{\"player1\":" << BuildPlayerJson(player1) << ","
       << "\"player2\":" << BuildPlayerJson(player2) << "}";

    sRealtimeBridge->EmitEvent(EventNamespace::ALIVE_WORLD, "trade_started", ss.str());
    m_eventsSent++;
#endif
}

// ============================================================================
// WORLD EVENTS
// ============================================================================

void BridgeAdapter::EmitRareSpawn(uint32 creatureEntry, const std::string& creatureName, uint32 zoneId)
{
#ifdef MOD_EC_REALTIME_BRIDGE
    std::ostringstream ss;
    ss << "{\"creatureEntry\":" << creatureEntry << ","
       << "\"creatureName\":\"" << EscapeJsonString(creatureName) << "\","
       << "\"zoneId\":" << zoneId << "}";

    sRealtimeBridge->EmitEvent(EventNamespace::ALIVE_WORLD, "rare_spawn", ss.str());
    m_eventsSent++;
#endif
}

void BridgeAdapter::EmitBossEngaged(uint32 creatureEntry, const std::string& bossName, Player* engager)
{
#ifdef MOD_EC_REALTIME_BRIDGE
    std::ostringstream ss;
    ss << "{\"creatureEntry\":" << creatureEntry << ","
       << "\"bossName\":\"" << EscapeJsonString(bossName) << "\","
       << "\"engager\":" << BuildPlayerJson(engager) << "}";

    sRealtimeBridge->EmitEvent(EventNamespace::ALIVE_WORLD, "boss_engaged", ss.str());
    m_eventsSent++;
#endif
}

void BridgeAdapter::EmitBossKilled(uint32 creatureEntry, const std::string& bossName, Player* killer)
{
#ifdef MOD_EC_REALTIME_BRIDGE
    std::ostringstream ss;
    ss << "{\"creatureEntry\":" << creatureEntry << ","
       << "\"bossName\":\"" << EscapeJsonString(bossName) << "\","
       << "\"killer\":" << BuildPlayerJson(killer) << "}";

    sRealtimeBridge->EmitEvent(EventNamespace::ALIVE_WORLD, "boss_killed", ss.str());
    m_eventsSent++;
#endif
}

void BridgeAdapter::EmitWorldEventStart(uint32 eventId, const std::string& eventName)
{
#ifdef MOD_EC_REALTIME_BRIDGE
    std::ostringstream ss;
    ss << "{\"eventId\":" << eventId << ","
       << "\"eventName\":\"" << EscapeJsonString(eventName) << "\"}";

    sRealtimeBridge->EmitEvent(EventNamespace::ALIVE_WORLD, "world_event_start", ss.str());
    m_eventsSent++;
#endif
}

void BridgeAdapter::EmitWorldEventEnd(uint32 eventId, const std::string& eventName)
{
#ifdef MOD_EC_REALTIME_BRIDGE
    std::ostringstream ss;
    ss << "{\"eventId\":" << eventId << ","
       << "\"eventName\":\"" << EscapeJsonString(eventName) << "\"}";

    sRealtimeBridge->EmitEvent(EventNamespace::ALIVE_WORLD, "world_event_end", ss.str());
    m_eventsSent++;
#endif
}

void BridgeAdapter::EmitGossipSpread(const std::string& gossipType, const std::string& content, uint32 zoneId)
{
#ifdef MOD_EC_REALTIME_BRIDGE
    std::ostringstream ss;
    ss << "{\"gossipType\":\"" << EscapeJsonString(gossipType) << "\","
       << "\"content\":\"" << EscapeJsonString(content) << "\","
       << "\"zoneId\":" << zoneId << "}";

    sRealtimeBridge->EmitEvent(EventNamespace::ALIVE_WORLD, "gossip_spread", ss.str());
    m_eventsSent++;
#endif
}

// ============================================================================
// GOAL SYSTEM EVENTS
// ============================================================================

void BridgeAdapter::EmitGoalCreated(uint64 botGuid, uint32 goalId, uint8 goalType, const std::string& targetName, uint8 priority)
{
#ifdef MOD_EC_REALTIME_BRIDGE
    // Get current timestamp
    auto now = std::chrono::system_clock::now();
    auto timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()).count();

    std::ostringstream ss;
    ss << "{\"botGuid\":" << botGuid << ","
       << "\"goalId\":" << goalId << ","
       << "\"goalType\":" << static_cast<int>(goalType) << ","
       << "\"targetName\":\"" << EscapeJsonString(targetName) << "\","
       << "\"priority\":" << static_cast<int>(priority) << ","
       << "\"timestamp\":" << timestamp << "}";

    sRealtimeBridge->EmitEvent(EventNamespace::ALIVE_WORLD, "goal_created", ss.str());
    m_eventsSent++;
#endif
}

void BridgeAdapter::EmitGoalProgress(uint64 botGuid, uint32 goalId, float progress, uint32 current, uint32 target)
{
#ifdef MOD_EC_REALTIME_BRIDGE
    // Get bot name from memory manager
    std::string botName = "Unknown";
    if (m_memoryManager)
    {
        auto memory = m_memoryManager->GetBotMemory(botGuid);
        if (memory)
            botName = memory->botName;
    }

    // Get current timestamp
    auto now = std::chrono::system_clock::now();
    auto timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()).count();

    std::ostringstream ss;
    ss << "{\"botGuid\":" << botGuid << ","
       << "\"botName\":\"" << EscapeJsonString(botName) << "\","
       << "\"goalId\":" << goalId << ","
       << "\"progress\":" << progress << ","
       << "\"current\":" << current << ","
       << "\"target\":" << target << ","
       << "\"timestamp\":" << timestamp << "}";

    sRealtimeBridge->EmitEvent(EventNamespace::ALIVE_WORLD, "goal_progress", ss.str());
    m_eventsSent++;
#endif
}

void BridgeAdapter::EmitGoalCompleted(uint64 botGuid, uint32 goalId, const std::string& targetName)
{
#ifdef MOD_EC_REALTIME_BRIDGE
    // Get current timestamp
    auto now = std::chrono::system_clock::now();
    auto timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()).count();

    std::ostringstream ss;
    ss << "{\"botGuid\":" << botGuid << ","
       << "\"goalId\":" << goalId << ","
       << "\"targetName\":\"" << EscapeJsonString(targetName) << "\","
       << "\"timestamp\":" << timestamp << "}";

    sRealtimeBridge->EmitEvent(EventNamespace::ALIVE_WORLD, "goal_completed", ss.str());
    m_eventsSent++;
#endif
}

void BridgeAdapter::EmitGoalAbandoned(uint64 botGuid, uint32 goalId, const std::string& reason)
{
#ifdef MOD_EC_REALTIME_BRIDGE
    // Get current timestamp
    auto now = std::chrono::system_clock::now();
    auto timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()).count();

    std::ostringstream ss;
    ss << "{\"botGuid\":" << botGuid << ","
       << "\"goalId\":" << goalId << ","
       << "\"reason\":\"" << EscapeJsonString(reason) << "\","
       << "\"timestamp\":" << timestamp << "}";

    sRealtimeBridge->EmitEvent(EventNamespace::ALIVE_WORLD, "goal_abandoned", ss.str());
    m_eventsSent++;
#endif
}

// ============================================================================
// AI QUEUE EVENTS
// ============================================================================

void BridgeAdapter::EmitAIRequestQueued(uint64 botGuid, uint32 requestId, uint8 requestType, uint8 priority)
{
#ifdef MOD_EC_REALTIME_BRIDGE
    std::ostringstream ss;
    ss << "{\"botGuid\":" << botGuid << ","
       << "\"requestId\":" << requestId << ","
       << "\"requestType\":" << static_cast<int>(requestType) << ","
       << "\"priority\":" << static_cast<int>(priority) << "}";

    sRealtimeBridge->EmitEvent(EventNamespace::ALIVE_WORLD, "ai_request_queued", ss.str());
    m_eventsSent++;
#endif
}

void BridgeAdapter::EmitAIRequestCompleted(uint64 botGuid, uint32 requestId, bool success, uint32 responseTimeMs)
{
#ifdef MOD_EC_REALTIME_BRIDGE
    std::ostringstream ss;
    ss << "{\"botGuid\":" << botGuid << ","
       << "\"requestId\":" << requestId << ","
       << "\"success\":" << (success ? "true" : "false") << ","
       << "\"responseTimeMs\":" << responseTimeMs << "}";

    sRealtimeBridge->EmitEvent(EventNamespace::ALIVE_WORLD, "ai_request_completed", ss.str());
    m_eventsSent++;
#endif
}

void BridgeAdapter::EmitAICacheHit(uint64 botGuid, const std::string& cacheKey)
{
#ifdef MOD_EC_REALTIME_BRIDGE
    std::ostringstream ss;
    ss << "{\"botGuid\":" << botGuid << ","
       << "\"cacheKey\":\"" << EscapeJsonString(cacheKey) << "\"}";

    sRealtimeBridge->EmitEvent(EventNamespace::ALIVE_WORLD, "ai_cache_hit", ss.str());
    m_eventsSent++;
#endif
}

// ============================================================================
// ZONE STATE EVENTS
// ============================================================================

void BridgeAdapter::EmitZoneStateUpdated(uint32 zoneId, uint32 mapId, uint32 alliancePlayers, uint32 hordePlayers,
                                          uint32 allianceBots, uint32 hordeBots, uint8 dangerLevel, uint8 activityLevel)
{
#ifdef MOD_EC_REALTIME_BRIDGE
    std::ostringstream ss;
    ss << "{\"zoneId\":" << zoneId << ","
       << "\"mapId\":" << mapId << ","
       << "\"alliancePlayers\":" << alliancePlayers << ","
       << "\"hordePlayers\":" << hordePlayers << ","
       << "\"allianceBots\":" << allianceBots << ","
       << "\"hordeBots\":" << hordeBots << ","
       << "\"dangerLevel\":" << static_cast<int>(dangerLevel) << ","
       << "\"activityLevel\":" << static_cast<int>(activityLevel) << "}";

    sRealtimeBridge->EmitEvent(EventNamespace::ALIVE_WORLD, "zone_state_updated", ss.str());
    m_eventsSent++;
#endif
}

// ============================================================================
// JSON HELPERS
// ============================================================================

std::string BridgeAdapter::BuildPlayerJson(Player* player)
{
    if (!player)
        return "null";

    std::ostringstream ss;
    ss << "{"
       << "\"guid\":" << player->GetGUID().GetCounter() << ","
       << "\"name\":\"" << EscapeJsonString(player->GetName()) << "\","
       << "\"level\":" << static_cast<int>(player->GetLevel()) << ","
       << "\"class\":" << static_cast<int>(player->getClass()) << ","
       << "\"race\":" << static_cast<int>(player->getRace()) << ","
       << "\"zone\":" << player->GetZoneId() << ","
       << "\"area\":" << player->GetAreaId() << ","
       << "\"map\":" << player->GetMapId() << ","
       << "\"x\":" << player->GetPositionX() << ","
       << "\"y\":" << player->GetPositionY() << ","
       << "\"z\":" << player->GetPositionZ() << ","
       << "\"faction\":" << static_cast<int>(player->GetTeamId())
       << "}";

    return ss.str();
}

std::string BridgeAdapter::BuildUnitJson(Unit* unit)
{
    if (!unit)
        return "null";

    std::ostringstream ss;
    ss << "{"
       << "\"guid\":" << unit->GetGUID().GetCounter() << ","
       << "\"name\":\"" << EscapeJsonString(unit->GetName()) << "\","
       << "\"type\":" << static_cast<int>(unit->GetTypeId()) << ","
       << "\"entry\":" << (unit->GetTypeId() == TYPEID_UNIT ? unit->GetEntry() : 0)
       << "}";

    return ss.str();
}

std::string BridgeAdapter::EscapeJson(const std::string& input)
{
    return EscapeJsonString(input);
}

} // namespace AliveWorld
