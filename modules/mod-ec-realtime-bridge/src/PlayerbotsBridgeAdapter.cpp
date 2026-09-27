/*
 * mod-ec-realtime-bridge - Playerbots Bridge Adapter Implementation
 * Copyright (C) 2025 EC
 */

#include "PlayerbotsBridgeAdapter.h"

#ifdef MOD_EC_REALTIME_BRIDGE
#include "RealtimeBridgeSocket.h"
#endif

// Include Event from mod-playerbots
#include "Bot/Engine/WorldPacket/Event.h"

#include "Player.h"
#include "Log.h"

#include <sstream>
#include <random>
#include <ctime>

namespace PlayerbotsBridge
{

// ============================================================================
// SINGLETON
// ============================================================================

BridgeAdapter* BridgeAdapter::Instance()
{
    static BridgeAdapter instance;
    return &instance;
}

BridgeAdapter::BridgeAdapter()
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
    if (m_initialized)
        return;

#ifdef MOD_EC_REALTIME_BRIDGE
    LOG_INFO("module.playerbots.bridge", "PlayerbotsBridgeAdapter: Initializing...");

    // Register handler for incoming commands from Node.js
    sRealtimeBridge->RegisterHandler(
        ECRealtimeBridge::EventNamespace::PLAYERBOTS,
        [this](const ECRealtimeBridge::BridgeCommand& cmd)
        {
            // Handle commands from Node.js (e.g., set_sample_rate, monitor_bot)
            if (cmd.type == "set_sample_rate")
            {
                // Parse rate from data
                // SetActionSampleRate(rate);
            }
            else if (cmd.type == "monitor_bot")
            {
                // Add bot to monitored set
            }
            else if (cmd.type == "get_stats")
            {
                // Send statistics back
                std::ostringstream ss;
                ss << "{\"requestId\":\"" << cmd.requestId << "\","
                   << "\"actionsEmitted\":" << m_actionsEmitted.load() << ","
                   << "\"strategiesEmitted\":" << m_strategiesEmitted.load() << ","
                   << "\"actionsSkipped\":" << m_actionsSkipped.load() << "}";
                sRealtimeBridge->EmitEvent(
                    ECRealtimeBridge::EventNamespace::PLAYERBOTS,
                    "response",
                    ss.str());
            }
        });

    m_initialized = true;
    LOG_INFO("module.playerbots.bridge", "PlayerbotsBridgeAdapter: Initialized");
#else
    LOG_WARN("module.playerbots.bridge", "PlayerbotsBridgeAdapter: mod-ec-realtime-bridge not available");
#endif
}

void BridgeAdapter::Shutdown()
{
    if (!m_initialized)
        return;

#ifdef MOD_EC_REALTIME_BRIDGE
    sRealtimeBridge->UnregisterHandler(ECRealtimeBridge::EventNamespace::PLAYERBOTS);
#endif

    m_initialized = false;
    LOG_INFO("module.playerbots.bridge", "PlayerbotsBridgeAdapter: Shutdown complete");
}

// ============================================================================
// ACTION LISTENER INTERFACE
// ============================================================================

bool BridgeAdapter::Before(Action* action, Event event)
{
    // Don't block any actions
    return true;
}

bool BridgeAdapter::AllowExecution(Action* action, Event event)
{
    // Allow all actions
    return true;
}

void BridgeAdapter::After(Action* action, bool executed, Event event)
{
#ifdef MOD_EC_REALTIME_BRIDGE
    if (!m_initialized || !sRealtimeBridge->IsConnected())
        return;

    // We can't get bot from Action directly, but we can from PlayerbotAI
    // This method gets called from Engine::ListenAndExecute which has access to botAI
    // For now, we'll need to hook this differently - see RegisterWithEngine

    // Sampling check
    if (!ShouldSampleAction())
    {
        m_actionsSkipped++;
        return;
    }

    // The actual emission happens in EmitBotActionExecuted which is called
    // from a modified Engine.cpp or a script hook
#endif
}

bool BridgeAdapter::OverrideResult(Action* action, bool executed, Event event)
{
    // Don't override results
    return executed;
}

// ============================================================================
// EVENT EMISSION
// ============================================================================

void BridgeAdapter::EmitBotActionExecuted(Player* bot, const std::string& actionName,
                                           float relevance, const std::string& eventSource)
{
#ifdef MOD_EC_REALTIME_BRIDGE
    if (!m_initialized || !sRealtimeBridge->IsConnected())
        return;

    if (!ShouldEmitForBot(bot))
        return;

    if (!ShouldSampleAction())
    {
        m_actionsSkipped++;
        return;
    }

    std::ostringstream ss;
    ss << "{" << BuildBotJson(bot) << ","
       << "\"action\":\"" << EscapeJson(actionName) << "\","
       << "\"relevance\":" << relevance << ","
       << "\"trigger\":\"" << EscapeJson(eventSource) << "\","
       << "\"timestamp\":" << time(nullptr) << "}";

    sRealtimeBridge->EmitEvent(
        ECRealtimeBridge::EventNamespace::PLAYERBOTS,
        "bot_action_executed",
        ss.str());

    m_actionsEmitted++;
#endif
}

void BridgeAdapter::EmitBotActionFailed(Player* bot, const std::string& actionName,
                                         const std::string& reason)
{
#ifdef MOD_EC_REALTIME_BRIDGE
    if (!m_initialized || !sRealtimeBridge->IsConnected())
        return;

    if (!ShouldEmitForBot(bot))
        return;

    std::ostringstream ss;
    ss << "{" << BuildBotJson(bot) << ","
       << "\"action\":\"" << EscapeJson(actionName) << "\","
       << "\"reason\":\"" << EscapeJson(reason) << "\","
       << "\"timestamp\":" << time(nullptr) << "}";

    sRealtimeBridge->EmitEvent(
        ECRealtimeBridge::EventNamespace::PLAYERBOTS,
        "bot_action_failed",
        ss.str());

    m_actionsEmitted++;
#endif
}

void BridgeAdapter::EmitBotStrategyChanged(Player* bot, const std::string& strategy,
                                            bool added, BridgeBotState state)
{
#ifdef MOD_EC_REALTIME_BRIDGE
    if (!m_initialized || !sRealtimeBridge->IsConnected())
        return;

    if (!ShouldEmitForBot(bot))
        return;

    std::ostringstream ss;
    ss << "{" << BuildBotJson(bot) << ","
       << "\"strategy\":\"" << EscapeJson(strategy) << "\","
       << "\"added\":" << (added ? "true" : "false") << ","
       << "\"state\":\"" << GetBotStateString(state) << "\","
       << "\"timestamp\":" << time(nullptr) << "}";

    sRealtimeBridge->EmitEvent(
        ECRealtimeBridge::EventNamespace::PLAYERBOTS,
        "bot_strategy_changed",
        ss.str());

    m_strategiesEmitted++;
#endif
}

void BridgeAdapter::EmitBotStrategiesSnapshot(Player* bot,
                                               const std::vector<std::string>& strategies,
                                               BridgeBotState state)
{
#ifdef MOD_EC_REALTIME_BRIDGE
    if (!m_initialized || !sRealtimeBridge->IsConnected())
        return;

    if (!ShouldEmitForBot(bot))
        return;

    std::ostringstream ss;
    ss << "{" << BuildBotJson(bot) << ","
       << "\"strategies\":[";

    for (size_t i = 0; i < strategies.size(); ++i)
    {
        if (i > 0) ss << ",";
        ss << "\"" << EscapeJson(strategies[i]) << "\"";
    }

    ss << "],"
       << "\"state\":\"" << GetBotStateString(state) << "\","
       << "\"timestamp\":" << time(nullptr) << "}";

    sRealtimeBridge->EmitEvent(
        ECRealtimeBridge::EventNamespace::PLAYERBOTS,
        "bot_strategies_snapshot",
        ss.str());

    m_strategiesEmitted++;
#endif
}

void BridgeAdapter::EmitBotStateChanged(Player* bot, BridgeBotState oldState,
                                         BridgeBotState newState)
{
#ifdef MOD_EC_REALTIME_BRIDGE
    if (!m_initialized || !sRealtimeBridge->IsConnected())
        return;

    if (!ShouldEmitForBot(bot))
        return;

    std::ostringstream ss;
    ss << "{" << BuildBotJson(bot) << ","
       << "\"oldState\":\"" << GetBotStateString(oldState) << "\","
       << "\"newState\":\"" << GetBotStateString(newState) << "\","
       << "\"timestamp\":" << time(nullptr) << "}";

    sRealtimeBridge->EmitEvent(
        ECRealtimeBridge::EventNamespace::PLAYERBOTS,
        "bot_state_changed",
        ss.str());
#endif
}

void BridgeAdapter::EmitBotTargetChanged(Player* bot, uint64_t targetGuid,
                                          const std::string& targetName, bool isHostile)
{
#ifdef MOD_EC_REALTIME_BRIDGE
    if (!m_initialized || !sRealtimeBridge->IsConnected())
        return;

    if (!ShouldEmitForBot(bot))
        return;

    std::ostringstream ss;
    ss << "{" << BuildBotJson(bot) << ","
       << "\"targetGuid\":" << targetGuid << ","
       << "\"targetName\":\"" << EscapeJson(targetName) << "\","
       << "\"isHostile\":" << (isHostile ? "true" : "false") << ","
       << "\"timestamp\":" << time(nullptr) << "}";

    sRealtimeBridge->EmitEvent(
        ECRealtimeBridge::EventNamespace::PLAYERBOTS,
        "bot_target_changed",
        ss.str());
#endif
}

void BridgeAdapter::EmitBotSpawned(Player* bot, uint32_t zoneId, uint32_t areaId)
{
#ifdef MOD_EC_REALTIME_BRIDGE
    if (!m_initialized || !sRealtimeBridge->IsConnected())
        return;

    std::ostringstream ss;
    ss << "{" << BuildBotJson(bot) << ","
       << "\"zoneId\":" << zoneId << ","
       << "\"areaId\":" << areaId << ","
       << "\"timestamp\":" << time(nullptr) << "}";

    sRealtimeBridge->EmitEvent(
        ECRealtimeBridge::EventNamespace::PLAYERBOTS,
        "bot_spawned",
        ss.str());
#endif
}

void BridgeAdapter::EmitBotDespawned(Player* bot, const std::string& reason)
{
#ifdef MOD_EC_REALTIME_BRIDGE
    if (!m_initialized || !sRealtimeBridge->IsConnected())
        return;

    std::ostringstream ss;
    ss << "{" << BuildBotJson(bot) << ","
       << "\"reason\":\"" << EscapeJson(reason) << "\","
       << "\"timestamp\":" << time(nullptr) << "}";

    sRealtimeBridge->EmitEvent(
        ECRealtimeBridge::EventNamespace::PLAYERBOTS,
        "bot_despawned",
        ss.str());
#endif
}

// ============================================================================
// REGISTRATION
// ============================================================================

void BridgeAdapter::RegisterWithEngine(Engine* engine)
{
    // This would be called from mod-playerbots when a bot's engine is created
    // engine->AddActionExecutionListener(this);

    // However, the Engine's listener interface expects the exact ActionExecutionListener
    // type from mod-playerbots, not our BridgeActionListener interface.
    //
    // The cleanest solution is to:
    // 1. Add emit calls directly in Engine.cpp (requires modifying mod-playerbots)
    // 2. Or create a player script hook that fires on bot actions
    //
    // For now, we provide the methods and hook from mod-playerbots side.
}

void BridgeAdapter::UnregisterFromEngine(Engine* engine)
{
    // engine->removeActionExecutionListener(this);
}

// ============================================================================
// SAMPLING CONTROL
// ============================================================================

void BridgeAdapter::SetMonitoredZones(const std::unordered_set<uint32_t>& zones)
{
    std::lock_guard<std::mutex> lock(m_filterMutex);
    m_monitoredZones = zones;
}

void BridgeAdapter::SetMonitoredBots(const std::unordered_set<uint64_t>& guids)
{
    std::lock_guard<std::mutex> lock(m_filterMutex);
    m_monitoredBots = guids;
}

// ============================================================================
// HELPERS
// ============================================================================

bool BridgeAdapter::ShouldEmitForBot(Player* bot)
{
    if (!bot)
        return false;

    std::lock_guard<std::mutex> lock(m_filterMutex);

    // If specific bots are monitored, only emit for those
    if (!m_monitoredBots.empty())
    {
        if (m_monitoredBots.find(bot->GetGUID().GetCounter()) == m_monitoredBots.end())
            return false;
    }

    // If specific zones are monitored, only emit for bots in those zones
    if (!m_monitoredZones.empty())
    {
        if (m_monitoredZones.find(bot->GetZoneId()) == m_monitoredZones.end())
            return false;
    }

    return true;
}

bool BridgeAdapter::ShouldSampleAction()
{
    // Always emit if sample rate is 1.0
    if (m_actionSampleRate >= 1.0f)
        return true;

    // Never emit if sample rate is 0.0
    if (m_actionSampleRate <= 0.0f)
        return false;

    // Simple counter-based sampling (deterministic, less overhead than random)
    m_sampleCounter++;
    uint32_t threshold = static_cast<uint32_t>(100.0f * m_actionSampleRate);

    return (m_sampleCounter % 100) < threshold;
}

std::string BridgeAdapter::BuildBotJson(Player* bot)
{
    if (!bot)
        return "\"bot\":null";

    std::ostringstream ss;
    ss << "\"bot\":{"
       << "\"guid\":" << bot->GetGUID().GetCounter() << ","
       << "\"name\":\"" << EscapeJson(bot->GetName()) << "\","
       << "\"level\":" << static_cast<uint32_t>(bot->GetLevel()) << ","
       << "\"class\":" << static_cast<uint32_t>(bot->getClass()) << ","
       << "\"race\":" << static_cast<uint32_t>(bot->getRace()) << ","
       << "\"zone\":" << bot->GetZoneId() << ","
       << "\"map\":" << bot->GetMapId() << ","
       << "\"x\":" << bot->GetPositionX() << ","
       << "\"y\":" << bot->GetPositionY() << ","
       << "\"z\":" << bot->GetPositionZ() << ","
       << "\"health\":" << bot->GetHealth() << ","
       << "\"maxHealth\":" << bot->GetMaxHealth() << ","
       << "\"mana\":" << bot->GetPower(POWER_MANA) << ","
       << "\"maxMana\":" << bot->GetMaxPower(POWER_MANA)
       << "}";

    return ss.str();
}

std::string BridgeAdapter::EscapeJson(const std::string& input)
{
    std::string output;
    output.reserve(input.size() + 10);

    for (char c : input)
    {
        switch (c)
        {
            case '"':  output += "\\\""; break;
            case '\\': output += "\\\\"; break;
            case '\b': output += "\\b"; break;
            case '\f': output += "\\f"; break;
            case '\n': output += "\\n"; break;
            case '\r': output += "\\r"; break;
            case '\t': output += "\\t"; break;
            default:
                if (c >= 0 && c < 32)
                {
                    // Control characters - skip
                }
                else
                {
                    output += c;
                }
                break;
        }
    }

    return output;
}

const char* BridgeAdapter::GetBotStateString(BridgeBotState state)
{
    switch (state)
    {
        case BridgeBotState::COMBAT:     return "combat";
        case BridgeBotState::NON_COMBAT: return "non_combat";
        case BridgeBotState::DEAD:       return "dead";
        default:                         return "unknown";
    }
}

} // namespace PlayerbotsBridge
