/*
 * mod-gtapolice
 * Bridge adapter for EC Realtime Bridge integration
 */

#include "GTAPoliceBridge.h"
#include "GTAPoliceConfig.h"
#include "Log.h"
#include <sstream>

// Conditional compilation with realtime bridge
#ifdef MOD_EC_REALTIME_BRIDGE
#include "RealtimeBridgeSocket.h"
#include "RealtimeBridgeTypes.h"
using ECRealtimeBridge::EscapeJsonString;
using ECRealtimeBridge::EventNamespace;
#else
// Local fallback when bridge is not available
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
#endif

static const char* PursuitStateToString(PursuitState state)
{
    switch (state)
    {
        case PursuitState::STARTED:          return "started";
        case PursuitState::CHASE:            return "chase";
        case PursuitState::CREW_EXITING:     return "crew_exiting";
        case PursuitState::CREW_APPROACHING: return "crew_approaching";
        case PursuitState::CREW_LOADING:     return "crew_loading";
        case PursuitState::ARRESTED:         return "arrested";
        case PursuitState::TRANSPORTING:     return "transporting";
        case PursuitState::ESCAPED:          return "escaped";
        case PursuitState::JAILED:           return "jailed";
        case PursuitState::TIMEOUT:          return "timeout";
        default:                             return "unknown";
    }
}

// ============================================================================
// INITIALIZATION
// ============================================================================

void GTAPoliceBridge::Initialize()
{
#ifdef MOD_EC_REALTIME_BRIDGE
    if (sGTAPoliceConfig->DebugLog)
        LOG_INFO("module", "[mod-gtapolice] Bridge integration initialized");
#endif
}

// ============================================================================
// HELPER JSON BUILDERS
// ============================================================================

std::string GTAPoliceBridge::BuildPlayerJson(Player* player)
{
    if (!player)
        return "null";

    std::ostringstream ss;
    ss << "{"
       << "\"guid\":" << player->GetGUID().GetCounter() << ","
       << "\"name\":\"" << EscapeJsonString(player->GetName()) << "\","
       << "\"level\":" << static_cast<uint32>(player->GetLevel()) << ","
       << "\"class\":" << static_cast<uint32>(player->getClass()) << ","
       << "\"race\":" << static_cast<uint32>(player->getRace()) << ","
       << "\"x\":" << player->GetPositionX() << ","
       << "\"y\":" << player->GetPositionY() << ","
       << "\"z\":" << player->GetPositionZ() << ","
       << "\"mapId\":" << player->GetMapId() << ","
       << "\"zoneId\":" << player->GetZoneId()
       << "}";
    return ss.str();
}

std::string GTAPoliceBridge::BuildCreatureJson(Creature* creature)
{
    if (!creature)
        return "null";

    std::ostringstream ss;
    ss << "{"
       << "\"guid\":" << creature->GetGUID().GetCounter() << ","
       << "\"entry\":" << creature->GetEntry() << ","
       << "\"name\":\"" << EscapeJsonString(creature->GetName()) << "\","
       << "\"x\":" << creature->GetPositionX() << ","
       << "\"y\":" << creature->GetPositionY() << ","
       << "\"z\":" << creature->GetPositionZ() << ","
       << "\"mapId\":" << creature->GetMapId()
       << "}";
    return ss.str();
}

std::string GTAPoliceBridge::BuildPositionJson(float x, float y, float z, uint32 mapId)
{
    std::ostringstream ss;
    ss << "{"
       << "\"x\":" << x << ","
       << "\"y\":" << y << ","
       << "\"z\":" << z << ","
       << "\"mapId\":" << mapId
       << "}";
    return ss.str();
}

void GTAPoliceBridge::SendEvent(const std::string& eventType, const std::string& jsonData)
{
#ifdef MOD_EC_REALTIME_BRIDGE
    if (!sGTAPoliceConfig->EmitBridgeEvents)
        return;

    // Use GUARD_SYSTEM namespace since police is part of crime/wanted system
    sRealtimeBridge->EmitEvent(EventNamespace::GUARD_SYSTEM, eventType, jsonData);

    if (sGTAPoliceConfig->DebugLog)
        LOG_INFO("module", "[mod-gtapolice] Emitted event: {}", eventType);
#else
    (void)eventType;
    (void)jsonData;
#endif
}

// ============================================================================
// PURSUIT EVENTS
// ============================================================================

void GTAPoliceBridge::EmitPursuitStarted(Player* target, Creature* car, Creature* officer)
{
    std::ostringstream ss;
    ss << "{"
       << "\"player\":" << BuildPlayerJson(target) << ","
       << "\"car\":" << BuildCreatureJson(car) << ","
       << "\"officer\":" << BuildCreatureJson(officer) << ","
       << "\"state\":\"started\""
       << "}";

    SendEvent("pursuit_started", ss.str());
}

void GTAPoliceBridge::EmitPursuitStartedMultiCar(Player* target, uint32 carCount, uint8 wantedStars)
{
    std::ostringstream ss;
    ss << "{"
       << "\"player\":" << BuildPlayerJson(target) << ","
       << "\"carCount\":" << carCount << ","
       << "\"wantedStars\":" << static_cast<uint32>(wantedStars) << ","
       << "\"state\":\"started\""
       << "}";

    SendEvent("pursuit_started_multicar", ss.str());
}

void GTAPoliceBridge::EmitReinforcementDispatched(Player* target, Creature* car)
{
    std::ostringstream ss;
    ss << "{"
       << "\"player\":" << BuildPlayerJson(target) << ","
       << "\"car\":" << BuildCreatureJson(car) << ","
       << "\"timestamp\":" << time(nullptr)
       << "}";

    SendEvent("reinforcement_dispatched", ss.str());
}

void GTAPoliceBridge::EmitPursuitUpdate(Player* target, Creature* car, float distance, float speed, uint32 durationMs)
{
    std::ostringstream ss;
    ss << "{"
       << "\"player\":" << BuildPlayerJson(target) << ","
       << "\"car\":" << BuildCreatureJson(car) << ","
       << "\"distance\":" << distance << ","
       << "\"speed\":" << speed << ","
       << "\"durationMs\":" << durationMs << ","
       << "\"eta\":" << (speed > 0.1f ? static_cast<uint32>(distance / speed * 1000) : 0)
       << "}";

    SendEvent("pursuit_update", ss.str());
}

void GTAPoliceBridge::EmitPursuitStateChanged(Player* target, PursuitState state)
{
    std::ostringstream ss;
    ss << "{"
       << "\"player\":" << BuildPlayerJson(target) << ","
       << "\"state\":\"" << PursuitStateToString(state) << "\""
       << "}";

    SendEvent("pursuit_state_changed", ss.str());
}

void GTAPoliceBridge::EmitPursuitEnded(Player* target, PursuitState reason)
{
    std::ostringstream ss;
    ss << "{"
       << "\"player\":" << BuildPlayerJson(target) << ","
       << "\"reason\":\"" << PursuitStateToString(reason) << "\""
       << "}";

    SendEvent("pursuit_ended", ss.str());
}

void GTAPoliceBridge::EmitPursuitAutoDispatched(Player* target, uint8 wantedStars)
{
    std::ostringstream ss;
    ss << "{"
       << "\"player\":" << BuildPlayerJson(target) << ","
       << "\"wantedStars\":" << static_cast<uint32>(wantedStars) << ","
       << "\"reason\":\"auto_dispatch\""
       << "}";

    SendEvent("pursuit_auto_dispatched", ss.str());
}

// ============================================================================
// ARREST EVENTS
// ============================================================================

void GTAPoliceBridge::EmitPlayerArrested(Player* player, Creature* car)
{
    std::ostringstream ss;
    ss << "{"
       << "\"player\":" << BuildPlayerJson(player) << ","
       << "\"car\":" << BuildCreatureJson(car)
       << "}";

    SendEvent("player_arrested", ss.str());
}

void GTAPoliceBridge::EmitPlayerJailed(Player* player, uint32 mapId, float x, float y, float z)
{
    std::ostringstream ss;
    ss << "{"
       << "\"player\":" << BuildPlayerJson(player) << ","
       << "\"jailLocation\":" << BuildPositionJson(x, y, z, mapId)
       << "}";

    SendEvent("player_jailed", ss.str());
}

void GTAPoliceBridge::EmitPlayerReleased(Player* player)
{
    std::ostringstream ss;
    ss << "{"
       << "\"player\":" << BuildPlayerJson(player)
       << "}";

    SendEvent("player_released", ss.str());
}

// ============================================================================
// STAR DECAY EVENTS
// ============================================================================

void GTAPoliceBridge::EmitStarDecay(Player* player, uint8 oldStars, uint8 newStars)
{
    std::ostringstream ss;
    ss << "{"
       << "\"player\":" << BuildPlayerJson(player) << ","
       << "\"oldStars\":" << static_cast<uint32>(oldStars) << ","
       << "\"newStars\":" << static_cast<uint32>(newStars) << ","
       << "\"zoneId\":" << player->GetZoneId() << ","
       << "\"timestamp\":" << time(nullptr)
       << "}";

    SendEvent("star_decay", ss.str());
}

// ============================================================================
// BRIBE EVENTS
// ============================================================================

void GTAPoliceBridge::EmitBribe(Player* player, uint32 oldStars, uint32 cost)
{
    std::ostringstream ss;
    ss << "{"
       << "\"player\":" << BuildPlayerJson(player) << ","
       << "\"oldStars\":" << oldStars << ","
       << "\"cost\":" << cost << ","
       << "\"zoneId\":" << (player ? player->GetZoneId() : 0) << ","
       << "\"timestamp\":" << time(nullptr)
       << "}";

    SendEvent("player_bribed", ss.str());
}

// ============================================================================
// PAY-N-SPRAY EVENTS
// ============================================================================

void GTAPoliceBridge::EmitPaynSpray(Player* player, uint32 oldStars, uint32 cost)
{
    std::ostringstream ss;
    ss << "{"
       << "\"player\":" << BuildPlayerJson(player) << ","
       << "\"oldStars\":" << oldStars << ","
       << "\"cost\":" << cost << ","
       << "\"zoneId\":" << (player ? player->GetZoneId() : 0) << ","
       << "\"timestamp\":" << time(nullptr)
       << "}";

    SendEvent("player_paynspray", ss.str());
}

// ============================================================================
// ZONE HEAT EVENTS
// ============================================================================

void GTAPoliceBridge::EmitZoneHeatChanged(uint32 zoneId, uint32 heat, uint8 heatLevel)
{
    std::ostringstream ss;
    ss << "{"
       << "\"zoneId\":" << zoneId << ","
       << "\"heat\":" << heat << ","
       << "\"heatLevel\":" << static_cast<uint32>(heatLevel) << ","
       << "\"timestamp\":" << time(nullptr)
       << "}";

    SendEvent("zone_heat_changed", ss.str());
}

// ============================================================================
// OFFICER EVENTS
// ============================================================================

void GTAPoliceBridge::EmitOfficerDetectedWanted(Creature* officer, Player* target, uint32 wantedStars)
{
    std::ostringstream ss;
    ss << "{"
       << "\"officer\":" << BuildCreatureJson(officer) << ","
       << "\"player\":" << BuildPlayerJson(target) << ","
       << "\"wantedStars\":" << wantedStars
       << "}";

    SendEvent("officer_detected_wanted", ss.str());
}

void GTAPoliceBridge::EmitOfficerDispatchedCar(Creature* officer, Creature* car, Player* target)
{
    std::ostringstream ss;
    ss << "{"
       << "\"officer\":" << BuildCreatureJson(officer) << ","
       << "\"car\":" << BuildCreatureJson(car) << ","
       << "\"player\":" << BuildPlayerJson(target)
       << "}";

    SendEvent("officer_dispatched_car", ss.str());
}

// ============================================================================
// CREW EVENTS
// ============================================================================

void GTAPoliceBridge::EmitCrewExitedVehicle(Creature* crew, Creature* car, Player* target)
{
    std::ostringstream ss;
    ss << "{"
       << "\"crew\":" << BuildCreatureJson(crew) << ","
       << "\"car\":" << BuildCreatureJson(car) << ","
       << "\"target\":" << BuildPlayerJson(target)
       << "}";

    SendEvent("crew_exited_vehicle", ss.str());
}

void GTAPoliceBridge::EmitCrewApproaching(Creature* crew, Player* target, float distance)
{
    std::ostringstream ss;
    ss << "{"
       << "\"crew\":" << BuildCreatureJson(crew) << ","
       << "\"target\":" << BuildPlayerJson(target) << ","
       << "\"distance\":" << distance
       << "}";

    SendEvent("crew_approaching", ss.str());
}

void GTAPoliceBridge::EmitCrewArrestingTarget(Creature* crew, Player* target)
{
    std::ostringstream ss;
    ss << "{"
       << "\"crew\":" << BuildCreatureJson(crew) << ","
       << "\"target\":" << BuildPlayerJson(target)
       << "}";

    SendEvent("crew_arresting_target", ss.str());
}

void GTAPoliceBridge::EmitCrewReenteringVehicle(Creature* crew, Creature* car)
{
    std::ostringstream ss;
    ss << "{"
       << "\"crew\":" << BuildCreatureJson(crew) << ","
       << "\"car\":" << BuildCreatureJson(car)
       << "}";

    SendEvent("crew_reentering_vehicle", ss.str());
}

void GTAPoliceBridge::EmitCrewReenteredVehicle(Creature* crew, Creature* car)
{
    std::ostringstream ss;
    ss << "{"
       << "\"crew\":" << BuildCreatureJson(crew) << ","
       << "\"car\":" << BuildCreatureJson(car)
       << "}";

    SendEvent("crew_reentered_vehicle", ss.str());
}

void GTAPoliceBridge::EmitCrewOfficerDied(Creature* crew, Player* target)
{
    std::ostringstream ss;
    ss << "{"
       << "\"crew\":" << BuildCreatureJson(crew) << ","
       << "\"target\":" << BuildPlayerJson(target)
       << "}";

    SendEvent("crew_officer_died", ss.str());
}
