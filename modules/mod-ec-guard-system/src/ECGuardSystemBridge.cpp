/*
 * mod-ec-guard-system - Bridge Integration Implementation
 * Copyright (C) 2025 EC
 */

#include "ECGuardSystemBridge.h"
#include "ECGuardSystemManager.h"
#include "ECZoneSecurityState.h"

#include "Creature.h"
#include "Define.h"  // For TeamId
#include "Log.h"
#include "ObjectAccessor.h"
#include "Player.h"

#include <sstream>
#include <cmath>

#ifdef MOD_EC_REALTIME_BRIDGE
#include "RealtimeBridgeSocket.h"
#include "RealtimeBridgeTypes.h"
using namespace ECRealtimeBridge;
#endif

namespace ECGuardSystem
{

// ============================================================================
// JSON UTILITIES
// ============================================================================

static std::string EscapeJsonString(const std::string& input)
{
    std::string output;
    output.reserve(input.size() * 2);
    for (char c : input)
    {
        switch (c)
        {
            case '"':  output += "\\\""; break;
            case '\\': output += "\\\\"; break;
            case '\n': output += "\\n"; break;
            case '\r': output += "\\r"; break;
            case '\t': output += "\\t"; break;
            default:   output += c;
        }
    }
    return output;
}

// ============================================================================
// SINGLETON
// ============================================================================

GuardSystemBridge* GuardSystemBridge::Instance()
{
    static GuardSystemBridge instance;
    return &instance;
}

GuardSystemBridge::GuardSystemBridge()
    : m_eventsSent(0)
    , m_initialized(false)
{
}

GuardSystemBridge::~GuardSystemBridge()
{
    Shutdown();
}

void GuardSystemBridge::Initialize()
{
#ifdef MOD_EC_REALTIME_BRIDGE
    ECG_LOG_INFO("Registering with shared realtime bridge");

    // Register command handler for guard system namespace
    sRealtimeBridge->RegisterHandler(
        EventNamespace::GUARD_SYSTEM,
        [](const BridgeCommand& cmd) {
            // Handle incoming commands from console
            try
            {
                if (cmd.type == "clear_wanted")
                {
                    // Parse player guid from JSON payload: {"playerGuid": 12345}
                    // Simple JSON parsing (payload format: {"playerGuid":12345})
                    std::string payload = cmd.jsonPayload;
                    size_t pos = payload.find("\"playerGuid\":");
                    if (pos != std::string::npos)
                    {
                        pos += 13; // Skip "playerGuid":
                        uint64 guid = std::stoull(payload.substr(pos));

                        if (Player* player = ObjectAccessor::FindPlayerByLowGUID(guid))
                        {
                            sECGuardSystemMgr->ClearWanted(player);
                            ECG_LOG_INFO("Cleared wanted level for player %s via console command",
                                player->GetName().c_str());
                        }
                        else
                        {
                            ECG_LOG_WARN("clear_wanted: Player with GUID %llu not found", guid);
                        }
                    }
                }
                else if (cmd.type == "set_stars")
                {
                    // Parse player guid and star level from JSON: {"playerGuid": 12345, "stars": 3}
                    std::string payload = cmd.jsonPayload;
                    size_t guidPos = payload.find("\"playerGuid\":");
                    size_t starsPos = payload.find("\"stars\":");

                    if (guidPos != std::string::npos && starsPos != std::string::npos)
                    {
                        uint64 guid = std::stoull(payload.substr(guidPos + 13));
                        uint8 stars = static_cast<uint8>(std::stoi(payload.substr(starsPos + 8)));

                        if (Player* player = ObjectAccessor::FindPlayerByLowGUID(guid))
                        {
                            if (stars > 5)
                            {
                                ECG_LOG_WARN("set_stars: Invalid star level %u (max 5) for player %s",
                                    stars, player->GetName().c_str());
                            }
                            else
                            {
                                WantedState* state = sECGuardSystemMgr->GetOrCreateWantedState(player->GetGUID());
                                if (state)
                                {
                                    state->stars = static_cast<WantedStars>(stars);
                                    ECG_LOG_INFO("Set %u stars for player %s via console command",
                                        stars, player->GetName().c_str());
                                }
                            }
                        }
                        else
                        {
                            ECG_LOG_WARN("set_stars: Player with GUID %llu not found", guid);
                        }
                    }
                }
                else if (cmd.type == "get_wanted")
                {
                    // Query wanted status for a player
                    std::string payload = cmd.jsonPayload;
                    size_t pos = payload.find("\"playerGuid\":");
                    if (pos != std::string::npos)
                    {
                        uint64 guid = std::stoull(payload.substr(pos + 13));
                        if (Player* player = ObjectAccessor::FindPlayerByLowGUID(guid))
                        {
                            WantedState* state = sECGuardSystemMgr->GetWantedState(player->GetGUID());
                            if (state)
                            {
                                ECG_LOG_DEBUG("get_wanted: Player %s has %u stars, %u crime points",
                                    player->GetName().c_str(),
                                    static_cast<uint8>(state->stars),
                                    state->crimePoints);
                            }
                        }
                    }
                }
            }
            catch (const std::exception& e)
            {
                ECG_LOG_ERROR("Guard system bridge: Failed to parse command '%s': %s",
                    cmd.type.c_str(), e.what());
            }
        }
    );

    m_initialized = true;
    ECG_LOG_INFO("Bridge adapter initialized");
#else
    ECG_LOG_WARN("mod-ec-realtime-bridge not available");
#endif
}

void GuardSystemBridge::Shutdown()
{
#ifdef MOD_EC_REALTIME_BRIDGE
    if (m_initialized)
    {
        sRealtimeBridge->UnregisterHandler(EventNamespace::GUARD_SYSTEM);
        m_initialized = false;
    }
#endif
}

// ============================================================================
// CRIME EVENTS
// ============================================================================

void GuardSystemBridge::EmitCrimeCommitted(Player* player, CrimeType type, Creature* victim, uint32 crimePoints)
{
#ifdef MOD_EC_REALTIME_BRIDGE
    if (!player) return;

    const char* crimeTypeStr = "unknown";
    switch (type)
    {
        case CrimeType::CIVILIAN_ATTACK: crimeTypeStr = "civilian_attack"; break;
        case CrimeType::CIVILIAN_KILL:   crimeTypeStr = "civilian_kill"; break;
        case CrimeType::GUARD_ATTACK:    crimeTypeStr = "guard_attack"; break;
        case CrimeType::GUARD_KILL:      crimeTypeStr = "guard_kill"; break;
    }

    WantedState* state = sECGuardSystemMgr->GetWantedState(player->GetGUID());
    uint32 stars = state ? static_cast<uint32>(state->stars) : 0;

    std::ostringstream ss;
    ss << "{\"player\":" << BuildPlayerJson(player) << ","
       << "\"crimeType\":\"" << crimeTypeStr << "\","
       << "\"victim\":" << BuildCreatureJson(victim) << ","
       << "\"crimePoints\":" << crimePoints << ","
       << "\"stars\":" << stars << ","
       << "\"zoneId\":" << player->GetZoneId() << "}";

    sRealtimeBridge->EmitEvent(EventNamespace::GUARD_SYSTEM, "crime_committed", ss.str());
    m_eventsSent++;
#endif
}

void GuardSystemBridge::EmitCivilianKilled(Player* player, Creature* victim)
{
#ifdef MOD_EC_REALTIME_BRIDGE
    if (!player) return;

    std::ostringstream ss;
    ss << "{\"player\":" << BuildPlayerJson(player) << ","
       << "\"victim\":" << BuildCreatureJson(victim) << ","
       << "\"zoneId\":" << player->GetZoneId() << "}";

    sRealtimeBridge->EmitEvent(EventNamespace::GUARD_SYSTEM, "civilian_killed", ss.str());
    m_eventsSent++;
#endif
}

void GuardSystemBridge::EmitGuardAttacked(Player* player, Creature* guard)
{
#ifdef MOD_EC_REALTIME_BRIDGE
    if (!player) return;

    std::ostringstream ss;
    ss << "{\"player\":" << BuildPlayerJson(player) << ","
       << "\"guard\":" << BuildCreatureJson(guard) << ","
       << "\"zoneId\":" << player->GetZoneId() << "}";

    sRealtimeBridge->EmitEvent(EventNamespace::GUARD_SYSTEM, "guard_attacked", ss.str());
    m_eventsSent++;
#endif
}

void GuardSystemBridge::EmitGuardKilled(Player* player, Creature* guard)
{
#ifdef MOD_EC_REALTIME_BRIDGE
    if (!player) return;

    std::ostringstream ss;
    ss << "{\"player\":" << BuildPlayerJson(player) << ","
       << "\"guard\":" << BuildCreatureJson(guard) << ","
       << "\"zoneId\":" << player->GetZoneId() << "}";

    sRealtimeBridge->EmitEvent(EventNamespace::GUARD_SYSTEM, "guard_killed", ss.str());
    m_eventsSent++;
#endif
}

// ============================================================================
// WANTED LEVEL EVENTS
// ============================================================================

void GuardSystemBridge::EmitWantedLevelChanged(Player* player, WantedStars oldStars, WantedStars newStars, uint32 points)
{
#ifdef MOD_EC_REALTIME_BRIDGE
    if (!player) return;

    std::ostringstream ss;
    ss << "{\"player\":" << BuildPlayerJson(player) << ","
       << "\"previousStars\":" << static_cast<int>(oldStars) << ","
       << "\"stars\":" << static_cast<int>(newStars) << ","
       << "\"points\":" << points << ","
       << "\"zoneId\":" << player->GetZoneId() << "}";

    sRealtimeBridge->EmitEvent(EventNamespace::GUARD_SYSTEM, "wanted_level_changed", ss.str());
    m_eventsSent++;
#endif
}

void GuardSystemBridge::EmitWantedCleared(Player* player)
{
#ifdef MOD_EC_REALTIME_BRIDGE
    if (!player) return;

    std::ostringstream ss;
    ss << "{\"player\":" << BuildPlayerJson(player) << ","
       << "\"stars\":0,\"points\":0}";

    sRealtimeBridge->EmitEvent(EventNamespace::GUARD_SYSTEM, "wanted_cleared", ss.str());
    m_eventsSent++;
#endif
}

void GuardSystemBridge::EmitWantedDecayed(Player* player, uint32 oldPoints, uint32 newPoints)
{
#ifdef MOD_EC_REALTIME_BRIDGE
    if (!player) return;

    WantedState* state = sECGuardSystemMgr->GetWantedState(player->GetGUID());
    uint32 stars = state ? static_cast<uint32>(state->stars) : 0;

    std::ostringstream ss;
    ss << "{\"player\":" << BuildPlayerJson(player) << ","
       << "\"oldPoints\":" << oldPoints << ","
       << "\"newPoints\":" << newPoints << ","
       << "\"stars\":" << stars << "}";

    sRealtimeBridge->EmitEvent(EventNamespace::GUARD_SYSTEM, "wanted_decayed", ss.str());
    m_eventsSent++;
#endif
}

// ============================================================================
// GUARD DISPATCH EVENTS
// ============================================================================

void GuardSystemBridge::EmitGuardsAlerted(Player* target, uint32 witnessCount, uint32 zoneId)
{
#ifdef MOD_EC_REALTIME_BRIDGE
    if (!target) return;

    std::ostringstream ss;
    ss << "{\"player\":" << BuildPlayerJson(target) << ","
       << "\"witnessCount\":" << witnessCount << ","
       << "\"zoneId\":" << zoneId << "}";

    sRealtimeBridge->EmitEvent(EventNamespace::GUARD_SYSTEM, "guards_alerted", ss.str());
    m_eventsSent++;
#endif
}

void GuardSystemBridge::EmitGuardsDispatched(Player* target, uint32 guardCount, uint32 zoneId, bool elite)
{
#ifdef MOD_EC_REALTIME_BRIDGE
    if (!target) return;

    std::ostringstream ss;
    ss << "{\"player\":" << BuildPlayerJson(target) << ","
       << "\"guardCount\":" << guardCount << ","
       << "\"zoneId\":" << zoneId << ","
       << "\"elite\":" << (elite ? "true" : "false") << "}";

    sRealtimeBridge->EmitEvent(EventNamespace::GUARD_SYSTEM, "guards_dispatched", ss.str());
    m_eventsSent++;
#endif
}

void GuardSystemBridge::EmitGuardsSpawned(Player* target, uint32 guardCount, uint32 zoneId, bool elite)
{
#ifdef MOD_EC_REALTIME_BRIDGE
    if (!target) return;

    std::ostringstream ss;
    ss << "{\"player\":" << BuildPlayerJson(target) << ","
       << "\"guardCount\":" << guardCount << ","
       << "\"zoneId\":" << zoneId << ","
       << "\"elite\":" << (elite ? "true" : "false") << "}";

    sRealtimeBridge->EmitEvent(EventNamespace::GUARD_SYSTEM, "guards_spawned", ss.str());
    m_eventsSent++;
#endif
}

void GuardSystemBridge::EmitGuardEngaged(Player* target, Creature* guard)
{
#ifdef MOD_EC_REALTIME_BRIDGE
    if (!target) return;

    std::ostringstream ss;
    ss << "{\"player\":" << BuildPlayerJson(target) << ","
       << "\"guard\":" << BuildCreatureJson(guard) << "}";

    sRealtimeBridge->EmitEvent(EventNamespace::GUARD_SYSTEM, "guard_engaged", ss.str());
    m_eventsSent++;
#endif
}

// ============================================================================
// BROADCAST EVENTS
// ============================================================================

void GuardSystemBridge::EmitLocalDefenceAlert(Player* criminal, Creature* victim, uint32 zoneId)
{
#ifdef MOD_EC_REALTIME_BRIDGE
    if (!criminal) return;

    std::ostringstream ss;
    ss << "{\"player\":" << BuildPlayerJson(criminal) << ","
       << "\"victim\":" << BuildCreatureJson(victim) << ","
       << "\"zoneId\":" << zoneId << "}";

    sRealtimeBridge->EmitEvent(EventNamespace::GUARD_SYSTEM, "local_defence_alert", ss.str());
    m_eventsSent++;
#endif
}

// ============================================================================
// ZONE THREAT EVENTS
// ============================================================================

void GuardSystemBridge::EmitZonePhaseChanged(uint32 zoneId, const std::string& zoneName,
    uint8 oldPhase, uint8 newPhase, uint32 heatPoints, uint8 threatLevel,
    uint8 alarmState, uint8 controlState)
{
#ifdef MOD_EC_REALTIME_BRIDGE
    static const char* phaseNames[] = {"quiet", "uneasy", "mobilizing", "impact", "aftermath"};
    static const char* alarmNames[] = {"quiet", "uneasy", "mobilizing", "attacked", "occupied", "recovering"};
    static const char* controlNames[] = {"stable", "contested", "fallen"};

    std::ostringstream ss;
    ss << "{\"zoneId\":" << zoneId << ","
       << "\"zoneName\":\"" << EscapeJsonString(zoneName) << "\","
       << "\"oldPhase\":\"" << (oldPhase < 5 ? phaseNames[oldPhase] : "unknown") << "\","
       << "\"newPhase\":\"" << (newPhase < 5 ? phaseNames[newPhase] : "unknown") << "\","
       << "\"heatPoints\":" << heatPoints << ","
       << "\"threatLevel\":" << static_cast<int>(threatLevel) << ","
       << "\"alarmState\":\"" << (alarmState < 6 ? alarmNames[alarmState] : "unknown") << "\","
       << "\"controlState\":\"" << (controlState < 3 ? controlNames[controlState] : "unknown") << "\"}";

    sRealtimeBridge->EmitEvent(EventNamespace::GUARD_SYSTEM, "zone_phase_changed", ss.str());
    m_eventsSent++;
#endif
}

void GuardSystemBridge::EmitZonePhaseChange(uint32 zoneId, TeamId faction,
    SecurityPhase oldPhase, SecurityPhase newPhase)
{
#ifdef MOD_EC_REALTIME_BRIDGE
    // SecurityPhase state machine phases (from ECZoneSecurityState.h)
    static const char* securityPhaseNames[] = {
        "peaceful",       // 0 - PEACEFUL
        "alert",          // 1 - ALERT
        "active_pursuit", // 2 - ACTIVE_PURSUIT
        "lockdown",       // 3 - LOCKDOWN
        "cooldown"        // 4 - COOLDOWN
    };

    static const char* factionNames[] = {
        "alliance",  // 0 - TEAM_ALLIANCE
        "horde",     // 1 - TEAM_HORDE
        "neutral"    // 2 - TEAM_NEUTRAL
    };

    uint8 oldIdx = static_cast<uint8>(oldPhase);
    uint8 newIdx = static_cast<uint8>(newPhase);
    uint8 factionIdx = static_cast<uint8>(faction);

    std::ostringstream ss;
    ss << "{\"zoneId\":" << zoneId << ","
       << "\"faction\":\"" << (factionIdx <= 2 ? factionNames[factionIdx] : "unknown") << "\","
       << "\"factionId\":" << static_cast<int>(factionIdx) << ","
       << "\"oldPhase\":\"" << (oldIdx < 5 ? securityPhaseNames[oldIdx] : "unknown") << "\","
       << "\"newPhase\":\"" << (newIdx < 5 ? securityPhaseNames[newIdx] : "unknown") << "\","
       << "\"oldPhaseId\":" << static_cast<int>(oldIdx) << ","
       << "\"newPhaseId\":" << static_cast<int>(newIdx) << ","
       << "\"isEscalation\":" << (newIdx > oldIdx ? "true" : "false") << ","
       << "\"isDeescalation\":" << (newIdx < oldIdx ? "true" : "false") << "}";

    sRealtimeBridge->EmitEvent(EventNamespace::GUARD_SYSTEM, "security_phase_change", ss.str());
    m_eventsSent++;

    ECG_LOG_DEBUG("EmitZonePhaseChange: zone %u faction %s: %s -> %s",
        zoneId,
        (factionIdx <= 2 ? factionNames[factionIdx] : "unknown"),
        (oldIdx < 5 ? securityPhaseNames[oldIdx] : "unknown"),
        (newIdx < 5 ? securityPhaseNames[newIdx] : "unknown"));
#endif
}

void GuardSystemBridge::EmitZoneStateChanged(uint32 zoneId, const std::string& zoneName,
    uint8 alarmState, uint8 controlState, float guardSurvivalRate, uint32 activeCriminals)
{
#ifdef MOD_EC_REALTIME_BRIDGE
    static const char* alarmNames[] = {"quiet", "uneasy", "mobilizing", "attacked", "occupied", "recovering"};
    static const char* controlNames[] = {"stable", "contested", "fallen"};

    // Validate float to prevent invalid JSON (NaN, Infinity produce invalid JSON)
    float safeGuardSurvivalRate = guardSurvivalRate;
    if (std::isnan(guardSurvivalRate) || std::isinf(guardSurvivalRate))
    {
        ECG_LOG_WARN("EmitZoneStateChanged: guardSurvivalRate is NaN/Inf, using 0.0");
        safeGuardSurvivalRate = 0.0f;
    }

    std::ostringstream ss;
    ss << "{\"zoneId\":" << zoneId << ","
       << "\"zoneName\":\"" << EscapeJsonString(zoneName) << "\","
       << "\"alarmState\":\"" << (alarmState < 6 ? alarmNames[alarmState] : "unknown") << "\","
       << "\"controlState\":\"" << (controlState < 3 ? controlNames[controlState] : "unknown") << "\","
       << "\"guardSurvivalRate\":" << safeGuardSurvivalRate << ","
       << "\"activeCriminals\":" << activeCriminals << "}";

    sRealtimeBridge->EmitEvent(EventNamespace::GUARD_SYSTEM, "zone_state_changed", ss.str());
    m_eventsSent++;
#endif
}

void GuardSystemBridge::EmitZoneHeatChanged(uint32 zoneId, const std::string& zoneName,
    uint32 oldHeat, uint32 newHeat, uint8 threatLevel, bool isDecay)
{
#ifdef MOD_EC_REALTIME_BRIDGE
    std::ostringstream ss;
    ss << "{\"zoneId\":" << zoneId << ","
       << "\"zoneName\":\"" << EscapeJsonString(zoneName) << "\","
       << "\"oldHeat\":" << oldHeat << ","
       << "\"newHeat\":" << newHeat << ","
       << "\"threatLevel\":" << static_cast<int>(threatLevel) << ","
       << "\"isDecay\":" << (isDecay ? "true" : "false") << "}";

    sRealtimeBridge->EmitEvent(EventNamespace::GUARD_SYSTEM, "zone_heat_changed", ss.str());
    m_eventsSent++;
#endif
}

void GuardSystemBridge::EmitZoneFallen(uint32 zoneId, const std::string& zoneName,
    uint32 guardsKilled, uint32 criminalsInvolved, uint32 eliteResponseDelayMs)
{
#ifdef MOD_EC_REALTIME_BRIDGE
    std::ostringstream ss;
    ss << "{\"zoneId\":" << zoneId << ","
       << "\"zoneName\":\"" << EscapeJsonString(zoneName) << "\","
       << "\"guardsKilled\":" << guardsKilled << ","
       << "\"criminalsInvolved\":" << criminalsInvolved << ","
       << "\"eliteResponseDelayMs\":" << eliteResponseDelayMs << ","
       << "\"eliteResponseDelaySec\":" << (eliteResponseDelayMs / 1000) << "}";

    sRealtimeBridge->EmitEvent(EventNamespace::GUARD_SYSTEM, "zone_fallen", ss.str());
    m_eventsSent++;
#endif
}

void GuardSystemBridge::EmitZoneRecovered(uint32 zoneId, const std::string& zoneName)
{
#ifdef MOD_EC_REALTIME_BRIDGE
    std::ostringstream ss;
    ss << "{\"zoneId\":" << zoneId << ","
       << "\"zoneName\":\"" << EscapeJsonString(zoneName) << "\"}";

    sRealtimeBridge->EmitEvent(EventNamespace::GUARD_SYSTEM, "zone_recovered", ss.str());
    m_eventsSent++;
#endif
}

// ============================================================================
// BOUNTY SYSTEM EVENTS
// ============================================================================

void GuardSystemBridge::EmitBountyPosted(uint32 bountyId, uint32 targetGuid, const std::string& targetName,
    uint32 wantedLevel, uint32 rewardGold, uint32 zoneId, const std::string& zoneName)
{
#ifdef MOD_EC_REALTIME_BRIDGE
    std::ostringstream ss;
    ss << "{\"bountyId\":" << bountyId << ","
       << "\"targetGuid\":" << targetGuid << ","
       << "\"targetName\":\"" << EscapeJsonString(targetName) << "\","
       << "\"wantedLevel\":" << wantedLevel << ","
       << "\"rewardGold\":" << rewardGold << ","
       << "\"rewardGoldDisplay\":" << (rewardGold / 10000) << ","
       << "\"zoneId\":" << zoneId << ","
       << "\"zoneName\":\"" << EscapeJsonString(zoneName) << "\"}";

    sRealtimeBridge->EmitEvent(EventNamespace::GUARD_SYSTEM, "bounty_posted", ss.str());
    m_eventsSent++;
#endif
}

void GuardSystemBridge::EmitBountyClaimed(uint32 bountyId, uint32 targetGuid, const std::string& targetName,
    uint32 claimerGuid, const std::string& claimerName, uint32 rewardGold)
{
#ifdef MOD_EC_REALTIME_BRIDGE
    std::ostringstream ss;
    ss << "{\"bountyId\":" << bountyId << ","
       << "\"targetGuid\":" << targetGuid << ","
       << "\"targetName\":\"" << EscapeJsonString(targetName) << "\","
       << "\"claimerGuid\":" << claimerGuid << ","
       << "\"claimerName\":\"" << EscapeJsonString(claimerName) << "\","
       << "\"rewardGold\":" << rewardGold << ","
       << "\"rewardGoldDisplay\":" << (rewardGold / 10000) << "}";

    sRealtimeBridge->EmitEvent(EventNamespace::GUARD_SYSTEM, "bounty_claimed", ss.str());
    m_eventsSent++;
#endif
}

void GuardSystemBridge::EmitBountyCancelled(uint32 bountyId, uint32 targetGuid, const std::string& targetName,
    const std::string& reason)
{
#ifdef MOD_EC_REALTIME_BRIDGE
    std::ostringstream ss;
    ss << "{\"bountyId\":" << bountyId << ","
       << "\"targetGuid\":" << targetGuid << ","
       << "\"targetName\":\"" << EscapeJsonString(targetName) << "\","
       << "\"reason\":\"" << EscapeJsonString(reason) << "\"}";

    sRealtimeBridge->EmitEvent(EventNamespace::GUARD_SYSTEM, "bounty_cancelled", ss.str());
    m_eventsSent++;
#endif
}

// ============================================================================
// BOUNTY HUNTER NPC EVENTS
// ============================================================================

void GuardSystemBridge::EmitBountyHunterSpawned(uint32 hunterGuid, uint32 targetGuid,
    const std::string& targetName, uint32 hunterEntry, const std::string& hunterName)
{
#ifdef MOD_EC_REALTIME_BRIDGE
    std::ostringstream ss;
    ss << "{\"hunterGuid\":" << hunterGuid << ","
       << "\"targetGuid\":" << targetGuid << ","
       << "\"targetName\":\"" << EscapeJsonString(targetName) << "\","
       << "\"hunterEntry\":" << hunterEntry << ","
       << "\"hunterName\":\"" << EscapeJsonString(hunterName) << "\"}";

    sRealtimeBridge->EmitEvent(EventNamespace::GUARD_SYSTEM, "bounty_hunter_spawned", ss.str());
    m_eventsSent++;
#endif
}

void GuardSystemBridge::EmitBountyHunterKilled(uint32 hunterGuid, uint32 targetGuid,
    const std::string& killerName, const std::string& killedBy)
{
#ifdef MOD_EC_REALTIME_BRIDGE
    std::ostringstream ss;
    ss << "{\"hunterGuid\":" << hunterGuid << ","
       << "\"targetGuid\":" << targetGuid << ","
       << "\"killerName\":\"" << EscapeJsonString(killerName) << "\","
       << "\"killedBy\":\"" << EscapeJsonString(killedBy) << "\"}";

    sRealtimeBridge->EmitEvent(EventNamespace::GUARD_SYSTEM, "bounty_hunter_killed", ss.str());
    m_eventsSent++;
#endif
}

void GuardSystemBridge::EmitBountyHunterTracking(ObjectGuid hunterGuid, ObjectGuid targetGuid,
    uint32 fromZone, uint32 toZone)
{
#ifdef MOD_EC_REALTIME_BRIDGE
    std::ostringstream ss;
    ss << "{\"hunterGuid\":" << hunterGuid.GetCounter() << ","
       << "\"targetGuid\":" << targetGuid.GetCounter() << ","
       << "\"fromZone\":" << fromZone << ","
       << "\"toZone\":" << toZone << "}";

    sRealtimeBridge->EmitEvent(EventNamespace::GUARD_SYSTEM, "bounty_hunter_tracking", ss.str());
    m_eventsSent++;
#endif
}

void GuardSystemBridge::EmitBountyHunterDespawned(ObjectGuid hunterGuid, ObjectGuid targetGuid,
    const std::string& reason)
{
#ifdef MOD_EC_REALTIME_BRIDGE
    std::ostringstream ss;
    ss << "{\"hunterGuid\":" << hunterGuid.GetCounter() << ","
       << "\"targetGuid\":" << targetGuid.GetCounter() << ","
       << "\"reason\":\"" << EscapeJsonString(reason) << "\"}";

    sRealtimeBridge->EmitEvent(EventNamespace::GUARD_SYSTEM, "bounty_hunter_despawned", ss.str());
    m_eventsSent++;
#endif
}

// ============================================================================
// ELITE RESPONSE EVENTS
// ============================================================================

void GuardSystemBridge::EmitEliteResponseTriggered(uint32 zoneId, const std::string& zoneName,
    uint32 delayMs, const std::string& factionForce)
{
#ifdef MOD_EC_REALTIME_BRIDGE
    std::ostringstream ss;
    ss << "{\"zoneId\":" << zoneId << ","
       << "\"zoneName\":\"" << EscapeJsonString(zoneName) << "\","
       << "\"delayMs\":" << delayMs << ","
       << "\"delaySec\":" << (delayMs / 1000) << ","
       << "\"factionForce\":\"" << EscapeJsonString(factionForce) << "\"}";

    sRealtimeBridge->EmitEvent(EventNamespace::GUARD_SYSTEM, "elite_response_triggered", ss.str());
    m_eventsSent++;
#endif
}

void GuardSystemBridge::EmitEliteForcesArriving(uint32 zoneId, const std::string& zoneName, uint32 count)
{
#ifdef MOD_EC_REALTIME_BRIDGE
    std::ostringstream ss;
    ss << "{\"zoneId\":" << zoneId << ","
       << "\"zoneName\":\"" << EscapeJsonString(zoneName) << "\","
       << "\"count\":" << count << "}";

    sRealtimeBridge->EmitEvent(EventNamespace::GUARD_SYSTEM, "elite_forces_arriving", ss.str());
    m_eventsSent++;
#endif
}

void GuardSystemBridge::EmitEliteForcesArrived(uint32 zoneId, const std::string& zoneName, uint32 count)
{
#ifdef MOD_EC_REALTIME_BRIDGE
    std::ostringstream ss;
    ss << "{\"zoneId\":" << zoneId << ","
       << "\"zoneName\":\"" << EscapeJsonString(zoneName) << "\","
       << "\"count\":" << count << "}";

    sRealtimeBridge->EmitEvent(EventNamespace::GUARD_SYSTEM, "elite_forces_arrived", ss.str());
    m_eventsSent++;
#endif
}

// ============================================================================
// PREPARATION PHASE EVENTS
// ============================================================================

void GuardSystemBridge::EmitPreparationPhaseStarted(uint32 zoneId, const std::string& zoneName, uint8 phase)
{
#ifdef MOD_EC_REALTIME_BRIDGE
    static const char* phaseNames[] = {"quiet", "uneasy", "mobilizing", "impact", "aftermath"};

    std::ostringstream ss;
    ss << "{\"zoneId\":" << zoneId << ","
       << "\"zoneName\":\"" << EscapeJsonString(zoneName) << "\","
       << "\"phase\":\"" << (phase < 5 ? phaseNames[phase] : "unknown") << "\"}";

    sRealtimeBridge->EmitEvent(EventNamespace::GUARD_SYSTEM, "preparation_phase_started", ss.str());
    m_eventsSent++;
#endif
}

void GuardSystemBridge::EmitDialogueTriggered(uint32 zoneId, const std::string& speakerName,
    const std::string& dialogue, const std::string& speakerType)
{
#ifdef MOD_EC_REALTIME_BRIDGE
    std::ostringstream ss;
    ss << "{\"zoneId\":" << zoneId << ","
       << "\"speakerName\":\"" << EscapeJsonString(speakerName) << "\","
       << "\"dialogue\":\"" << EscapeJsonString(dialogue) << "\","
       << "\"speakerType\":\"" << EscapeJsonString(speakerType) << "\"}";

    sRealtimeBridge->EmitEvent(EventNamespace::GUARD_SYSTEM, "dialogue_triggered", ss.str());
    m_eventsSent++;
#endif
}

void GuardSystemBridge::EmitDialoguePairStarted(uint32 zoneId, const std::string& speakerA,
    const std::string& speakerB, uint32 lineCount)
{
#ifdef MOD_EC_REALTIME_BRIDGE
    std::ostringstream ss;
    ss << "{\"zoneId\":" << zoneId << ","
       << "\"speakerA\":\"" << EscapeJsonString(speakerA) << "\","
       << "\"speakerB\":\"" << EscapeJsonString(speakerB) << "\","
       << "\"lineCount\":" << lineCount << "}";

    sRealtimeBridge->EmitEvent(EventNamespace::GUARD_SYSTEM, "dialogue_pair_started", ss.str());
    m_eventsSent++;
#endif
}

void GuardSystemBridge::EmitMilitiaSpawned(uint32 zoneId, uint32 count, const std::string& activity)
{
#ifdef MOD_EC_REALTIME_BRIDGE
    std::ostringstream ss;
    ss << "{\"zoneId\":" << zoneId << ","
       << "\"count\":" << count << ","
       << "\"activity\":\"" << EscapeJsonString(activity) << "\"}";

    sRealtimeBridge->EmitEvent(EventNamespace::GUARD_SYSTEM, "militia_spawned", ss.str());
    m_eventsSent++;
#endif
}

void GuardSystemBridge::EmitMilitiaDespawned(uint32 zoneId, uint32 count)
{
#ifdef MOD_EC_REALTIME_BRIDGE
    std::ostringstream ss;
    ss << "{\"zoneId\":" << zoneId << ","
       << "\"count\":" << count << "}";

    sRealtimeBridge->EmitEvent(EventNamespace::GUARD_SYSTEM, "militia_despawned", ss.str());
    m_eventsSent++;
#endif
}

void GuardSystemBridge::EmitCrierAnnouncement(uint32 zoneId, const std::string& urgency,
    const std::string& announcement)
{
#ifdef MOD_EC_REALTIME_BRIDGE
    std::ostringstream ss;
    ss << "{\"zoneId\":" << zoneId << ","
       << "\"urgency\":\"" << EscapeJsonString(urgency) << "\","
       << "\"announcement\":\"" << EscapeJsonString(announcement) << "\"}";

    sRealtimeBridge->EmitEvent(EventNamespace::GUARD_SYSTEM, "crier_announcement", ss.str());
    m_eventsSent++;
#endif
}

void GuardSystemBridge::EmitBarricadesSpawned(uint32 zoneId, uint32 count)
{
#ifdef MOD_EC_REALTIME_BRIDGE
    std::ostringstream ss;
    ss << "{\"zoneId\":" << zoneId << ","
       << "\"count\":" << count << "}";

    sRealtimeBridge->EmitEvent(EventNamespace::GUARD_SYSTEM, "barricades_spawned", ss.str());
    m_eventsSent++;
#endif
}

void GuardSystemBridge::EmitBarricadesDespawned(uint32 zoneId, uint32 count)
{
#ifdef MOD_EC_REALTIME_BRIDGE
    std::ostringstream ss;
    ss << "{\"zoneId\":" << zoneId << ","
       << "\"count\":" << count << "}";

    sRealtimeBridge->EmitEvent(EventNamespace::GUARD_SYSTEM, "barricades_despawned", ss.str());
    m_eventsSent++;
#endif
}

void GuardSystemBridge::EmitBannerChanged(uint32 zoneId, const std::string& oldFaction,
    const std::string& newFaction)
{
#ifdef MOD_EC_REALTIME_BRIDGE
    std::ostringstream ss;
    ss << "{\"zoneId\":" << zoneId << ","
       << "\"oldFaction\":\"" << EscapeJsonString(oldFaction) << "\","
       << "\"newFaction\":\"" << EscapeJsonString(newFaction) << "\"}";

    sRealtimeBridge->EmitEvent(EventNamespace::GUARD_SYSTEM, "banner_changed", ss.str());
    m_eventsSent++;
#endif
}

// ============================================================================
// CONSTRUCTION SYSTEM EVENTS (Peon Building)
// ============================================================================

void GuardSystemBridge::EmitConstructionStarted(uint32 zoneId, uint32 siteId,
    const std::string& siteName, uint32 structureEntry, uint8 workersAssigned, uint8 tripsRequired)
{
#ifdef MOD_EC_REALTIME_BRIDGE
    std::ostringstream ss;
    ss << "{\"zoneId\":" << zoneId << ","
       << "\"siteId\":" << siteId << ","
       << "\"siteName\":\"" << EscapeJsonString(siteName) << "\","
       << "\"structureEntry\":" << structureEntry << ","
       << "\"workersAssigned\":" << static_cast<uint32>(workersAssigned) << ","
       << "\"tripsRequired\":" << static_cast<uint32>(tripsRequired) << "}";

    sRealtimeBridge->EmitEvent(EventNamespace::GUARD_SYSTEM, "construction_started", ss.str());
    m_eventsSent++;
#endif
}

void GuardSystemBridge::EmitConstructionProgress(uint32 zoneId, uint32 siteId,
    const std::string& siteName, uint8 tripsCompleted, uint8 tripsRequired)
{
#ifdef MOD_EC_REALTIME_BRIDGE
    std::ostringstream ss;
    ss << "{\"zoneId\":" << zoneId << ","
       << "\"siteId\":" << siteId << ","
       << "\"siteName\":\"" << EscapeJsonString(siteName) << "\","
       << "\"tripsCompleted\":" << static_cast<uint32>(tripsCompleted) << ","
       << "\"tripsRequired\":" << static_cast<uint32>(tripsRequired) << ","
       << "\"percentComplete\":" << (tripsRequired > 0 ? (100 * tripsCompleted / tripsRequired) : 0) << "}";

    sRealtimeBridge->EmitEvent(EventNamespace::GUARD_SYSTEM, "construction_progress", ss.str());
    m_eventsSent++;
#endif
}

void GuardSystemBridge::EmitConstructionComplete(uint32 zoneId, uint32 siteId,
    const std::string& siteName, uint32 structureEntry)
{
#ifdef MOD_EC_REALTIME_BRIDGE
    std::ostringstream ss;
    ss << "{\"zoneId\":" << zoneId << ","
       << "\"siteId\":" << siteId << ","
       << "\"siteName\":\"" << EscapeJsonString(siteName) << "\","
       << "\"structureEntry\":" << structureEntry << "}";

    sRealtimeBridge->EmitEvent(EventNamespace::GUARD_SYSTEM, "construction_complete", ss.str());
    m_eventsSent++;
#endif
}

void GuardSystemBridge::EmitWorkerSpawned(uint32 zoneId, uint32 workerEntry,
    const std::string& workerName, uint32 siteId, const std::string& siteName)
{
#ifdef MOD_EC_REALTIME_BRIDGE
    std::ostringstream ss;
    ss << "{\"zoneId\":" << zoneId << ","
       << "\"workerEntry\":" << workerEntry << ","
       << "\"workerName\":\"" << EscapeJsonString(workerName) << "\","
       << "\"siteId\":" << siteId << ","
       << "\"siteName\":\"" << EscapeJsonString(siteName) << "\"}";

    sRealtimeBridge->EmitEvent(EventNamespace::GUARD_SYSTEM, "worker_spawned", ss.str());
    m_eventsSent++;
#endif
}

void GuardSystemBridge::EmitWorkerSay(uint32 zoneId, const std::string& workerName,
    const std::string& text, const std::string& sayType)
{
#ifdef MOD_EC_REALTIME_BRIDGE
    std::ostringstream ss;
    ss << "{\"zoneId\":" << zoneId << ","
       << "\"workerName\":\"" << EscapeJsonString(workerName) << "\","
       << "\"text\":\"" << EscapeJsonString(text) << "\","
       << "\"sayType\":\"" << EscapeJsonString(sayType) << "\"}";

    sRealtimeBridge->EmitEvent(EventNamespace::GUARD_SYSTEM, "worker_say", ss.str());
    m_eventsSent++;
#endif
}

void GuardSystemBridge::EmitWorkersFleeing(uint32 zoneId, uint32 workerCount)
{
#ifdef MOD_EC_REALTIME_BRIDGE
    std::ostringstream ss;
    ss << "{\"zoneId\":" << zoneId << ","
       << "\"workerCount\":" << workerCount << "}";

    sRealtimeBridge->EmitEvent(EventNamespace::GUARD_SYSTEM, "workers_fleeing", ss.str());
    m_eventsSent++;
#endif
}

// ============================================================================
// INTERACTIVE OBJECT EVENTS
// ============================================================================

void GuardSystemBridge::EmitInteractiveObjectSpawned(uint32 zoneId, uint32 goEntry,
    uint8 goType, const std::string& description)
{
#ifdef MOD_EC_REALTIME_BRIDGE
    static const char* goTypeNames[] = {"unknown", "door", "button", "questgiver", "chest", "binder",
        "trap", "chair", "spell_focus", "text", "goober", "elevator", "area_damage", "camera",
        "map_object", "transport", "door_2", "fishing_node", "ritual", "mailbox", "auction_house",
        "guardpost", "spellcaster", "meeting_stone", "flagstand", "fishing_pool", "flagdrop", "mini_game",
        "lottery_kiosk", "capture_point", "aura_generator", "dungeon_difficulty", "barber_chair",
        "destructible_building"};

    std::ostringstream ss;
    ss << "{\"zoneId\":" << zoneId << ","
       << "\"goEntry\":" << goEntry << ","
       << "\"goType\":\"" << (goType < 34 ? goTypeNames[goType] : "unknown") << "\","
       << "\"goTypeId\":" << static_cast<uint32>(goType) << ","
       << "\"description\":\"" << EscapeJsonString(description) << "\"}";

    sRealtimeBridge->EmitEvent(EventNamespace::GUARD_SYSTEM, "interactive_object_spawned", ss.str());
    m_eventsSent++;
#endif
}

void GuardSystemBridge::EmitInteractiveObjectDespawned(uint32 zoneId, uint32 goEntry,
    const std::string& description)
{
#ifdef MOD_EC_REALTIME_BRIDGE
    std::ostringstream ss;
    ss << "{\"zoneId\":" << zoneId << ","
       << "\"goEntry\":" << goEntry << ","
       << "\"description\":\"" << EscapeJsonString(description) << "\"}";

    sRealtimeBridge->EmitEvent(EventNamespace::GUARD_SYSTEM, "interactive_object_despawned", ss.str());
    m_eventsSent++;
#endif
}

// ============================================================================
// WITNESS SYSTEM EVENTS
// ============================================================================

void GuardSystemBridge::EmitWitnessDetected(Player* criminal, Creature* witness, Creature* victim,
    CrimeType crimeType, uint32 alertDurationMs)
{
#ifdef MOD_EC_REALTIME_BRIDGE
    if (!criminal) return;

    const char* crimeTypeStr = "unknown";
    switch (crimeType)
    {
        case CrimeType::CIVILIAN_ATTACK: crimeTypeStr = "civilian_attack"; break;
        case CrimeType::CIVILIAN_KILL:   crimeTypeStr = "civilian_kill"; break;
        case CrimeType::GUARD_ATTACK:    crimeTypeStr = "guard_attack"; break;
        case CrimeType::GUARD_KILL:      crimeTypeStr = "guard_kill"; break;
    }

    std::ostringstream ss;
    ss << "{\"criminal\":" << BuildPlayerJson(criminal) << ","
       << "\"witness\":" << BuildCreatureJson(witness) << ","
       << "\"victim\":" << BuildCreatureJson(victim) << ","
       << "\"crimeType\":\"" << crimeTypeStr << "\","
       << "\"alertDurationMs\":" << alertDurationMs << ","
       << "\"zoneId\":" << criminal->GetZoneId() << "}";

    sRealtimeBridge->EmitEvent(EventNamespace::GUARD_SYSTEM, "witness_detected", ss.str());
    m_eventsSent++;
#endif
}

void GuardSystemBridge::EmitWitnessFleeing(Creature* witness, Player* criminal, bool towardGuard,
    const std::string& yell)
{
#ifdef MOD_EC_REALTIME_BRIDGE
    if (!witness) return;

    std::ostringstream ss;
    ss << "{\"witness\":" << BuildCreatureJson(witness) << ","
       << "\"criminal\":" << (criminal ? BuildPlayerJson(criminal) : "null") << ","
       << "\"towardGuard\":" << (towardGuard ? "true" : "false") << ","
       << "\"yell\":\"" << EscapeJsonString(yell) << "\","
       << "\"zoneId\":" << witness->GetZoneId() << "}";

    sRealtimeBridge->EmitEvent(EventNamespace::GUARD_SYSTEM, "witness_fleeing", ss.str());
    m_eventsSent++;
#endif
}

void GuardSystemBridge::EmitWitnessCrimeConfirmed(Player* criminal, CrimeType crimeType, uint32 crimePoints)
{
#ifdef MOD_EC_REALTIME_BRIDGE
    if (!criminal) return;

    const char* crimeTypeStr = "unknown";
    switch (crimeType)
    {
        case CrimeType::CIVILIAN_ATTACK: crimeTypeStr = "civilian_attack"; break;
        case CrimeType::CIVILIAN_KILL:   crimeTypeStr = "civilian_kill"; break;
        case CrimeType::GUARD_ATTACK:    crimeTypeStr = "guard_attack"; break;
        case CrimeType::GUARD_KILL:      crimeTypeStr = "guard_kill"; break;
    }

    std::ostringstream ss;
    ss << "{\"criminal\":" << BuildPlayerJson(criminal) << ","
       << "\"crimeType\":\"" << crimeTypeStr << "\","
       << "\"crimePoints\":" << crimePoints << ","
       << "\"zoneId\":" << criminal->GetZoneId() << "}";

    sRealtimeBridge->EmitEvent(EventNamespace::GUARD_SYSTEM, "witness_crime_confirmed", ss.str());
    m_eventsSent++;
#endif
}

void GuardSystemBridge::EmitWitnessCrimeSuppressed(Player* criminal, CrimeType crimeType, uint32 crimePoints)
{
#ifdef MOD_EC_REALTIME_BRIDGE
    if (!criminal) return;

    const char* crimeTypeStr = "unknown";
    switch (crimeType)
    {
        case CrimeType::CIVILIAN_ATTACK: crimeTypeStr = "civilian_attack"; break;
        case CrimeType::CIVILIAN_KILL:   crimeTypeStr = "civilian_kill"; break;
        case CrimeType::GUARD_ATTACK:    crimeTypeStr = "guard_attack"; break;
        case CrimeType::GUARD_KILL:      crimeTypeStr = "guard_kill"; break;
    }

    std::ostringstream ss;
    ss << "{\"criminal\":" << BuildPlayerJson(criminal) << ","
       << "\"crimeType\":\"" << crimeTypeStr << "\","
       << "\"crimePoints\":" << crimePoints << ","
       << "\"zoneId\":" << criminal->GetZoneId() << "}";

    sRealtimeBridge->EmitEvent(EventNamespace::GUARD_SYSTEM, "witness_crime_suppressed", ss.str());
    m_eventsSent++;
#endif
}

// ============================================================================
// JSON HELPERS
// ============================================================================

std::string GuardSystemBridge::BuildPlayerJson(Player* player)
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
       << "\"race\":" << static_cast<int>(player->getRace()) << ","
       << "\"faction\":" << player->GetTeamId()
       << "}";
    return ss.str();
#else
    return "null";
#endif
}

std::string GuardSystemBridge::BuildCreatureJson(Creature* creature)
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

} // namespace ECGuardSystem
