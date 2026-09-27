/*
 * mod-ec-combat - Bridge Integration Implementation
 * Copyright (C) 2025 EC
 */

#include "ECCombatBridge.h"
#include "ECCombatManager.h"

#include "Battleground.h"
#include "Creature.h"
#include "Log.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "Unit.h"

#include <sstream>

#ifdef MOD_EC_REALTIME_BRIDGE
#include "RealtimeBridgeSocket.h"
#include "RealtimeBridgeTypes.h"
using namespace ECRealtimeBridge;
#endif

// Log macro
#define ECC_LOG_INFO(fmt, ...)  LOG_INFO("module.ec-combat", fmt, ##__VA_ARGS__)
#define ECC_LOG_DEBUG(fmt, ...) LOG_DEBUG("module.ec-combat", fmt, ##__VA_ARGS__)
#define ECC_LOG_ERROR(fmt, ...) LOG_ERROR("module.ec-combat", fmt, ##__VA_ARGS__)

namespace ECCombat
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

CombatBridge* CombatBridge::Instance()
{
    static CombatBridge instance;
    return &instance;
}

CombatBridge::CombatBridge()
    : m_eventsSent(0)
    , m_initialized(false)
{
}

CombatBridge::~CombatBridge()
{
    Shutdown();
}

void CombatBridge::Initialize()
{
#ifdef MOD_EC_REALTIME_BRIDGE
    ECC_LOG_INFO("Registering with shared realtime bridge");

    // Register command handler for combat namespace
    sRealtimeBridge->RegisterHandler(
        EventNamespace::COMBAT,
        [](const BridgeCommand& cmd) {
            // Handle incoming commands from console
            try
            {
                if (cmd.type == "get_kill_stats")
                {
                    // Parse player guid and return stats
                    std::string payload = cmd.jsonPayload;
                    size_t pos = payload.find("\"playerGuid\":");
                    if (pos != std::string::npos)
                    {
                        pos += 13;
                        uint64 guid = std::stoull(payload.substr(pos));

                        if (Player* player = ObjectAccessor::FindPlayerByLowGUID(guid))
                        {
                            auto stats = sECCombatMgr->GetPlayerStats(player);
                            // Response handled by manager
                            ECC_LOG_DEBUG("Retrieved kill stats for {} via console",
                                player->GetName().c_str());
                        }
                    }
                }
                else if (cmd.type == "reset_kill_streak")
                {
                    std::string payload = cmd.jsonPayload;
                    size_t pos = payload.find("\"playerGuid\":");
                    if (pos != std::string::npos)
                    {
                        pos += 13;
                        uint64 guid = std::stoull(payload.substr(pos));

                        if (Player* player = ObjectAccessor::FindPlayerByLowGUID(guid))
                        {
                            sECCombatMgr->ResetKillStreak(player);
                            ECC_LOG_INFO("Reset kill streak for {} via console",
                                player->GetName().c_str());
                        }
                    }
                }
            }
            catch (const std::exception& e)
            {
                ECC_LOG_ERROR("Error handling combat command '{}': {}",
                    cmd.type.c_str(), e.what());
            }
        }
    );

    m_initialized = true;
    ECC_LOG_INFO("Combat bridge initialized");
#else
    ECC_LOG_INFO("Realtime bridge not available, events will be logged only");
    m_initialized = true;
#endif
}

void CombatBridge::Shutdown()
{
    if (!m_initialized)
        return;

    ECC_LOG_INFO("Combat bridge shutting down. Events sent: {}", m_eventsSent.load());
    m_initialized = false;
}

// ============================================================================
// HELPER METHODS
// ============================================================================

std::string CombatBridge::BuildPlayerJson(Player* player)
{
    if (!player)
        return "null";

    std::ostringstream ss;
    ss << "{"
       << "\"guid\":" << player->GetGUID().GetCounter() << ","
       << "\"name\":\"" << EscapeJsonString(player->GetName()) << "\","
       << "\"level\":" << (int)player->GetLevel() << ","
       << "\"race\":" << (int)player->getRace() << ","
       << "\"class\":" << (int)player->getClass() << ","
       << "\"faction\":\"" << (player->GetTeamId() == TEAM_ALLIANCE ? "ALLIANCE" : "HORDE") << "\","
       << "\"health\":" << player->GetHealth() << ","
       << "\"maxHealth\":" << player->GetMaxHealth() << ","
       << "\"zoneId\":" << player->GetZoneId() << ","
       << "\"mapId\":" << player->GetMapId()
       << "}";
    return ss.str();
}

std::string CombatBridge::BuildCreatureJson(Creature* creature)
{
    if (!creature)
        return "null";

    std::ostringstream ss;
    ss << "{"
       << "\"guid\":" << creature->GetGUID().GetCounter() << ","
       << "\"entry\":" << creature->GetEntry() << ","
       << "\"name\":\"" << EscapeJsonString(creature->GetName()) << "\","
       << "\"level\":" << (int)creature->GetLevel() << ","
       << "\"isBoss\":" << (creature->IsDungeonBoss() || creature->isWorldBoss() ? "true" : "false") << ","
       << "\"isElite\":" << (creature->isElite() ? "true" : "false")
       << "}";
    return ss.str();
}

std::string CombatBridge::BuildUnitJson(Unit* unit)
{
    if (!unit)
        return "null";

    if (Player* player = unit->ToPlayer())
        return BuildPlayerJson(player);
    if (Creature* creature = unit->ToCreature())
        return BuildCreatureJson(creature);

    // Generic unit
    std::ostringstream ss;
    ss << "{"
       << "\"guid\":" << unit->GetGUID().GetCounter() << ","
       << "\"name\":\"" << EscapeJsonString(unit->GetName()) << "\","
       << "\"level\":" << (int)unit->GetLevel()
       << "}";
    return ss.str();
}

// ============================================================================
// KILL FEED EVENTS
// ============================================================================

void CombatBridge::EmitPvPKill(Player* killer, Player* victim, uint32 zoneId)
{
    if (!m_initialized || !killer || !victim)
        return;

#ifdef MOD_EC_REALTIME_BRIDGE
    std::ostringstream payload;
    payload << "{"
            << "\"killer\":" << BuildPlayerJson(killer) << ","
            << "\"victim\":" << BuildPlayerJson(victim) << ","
            << "\"zoneId\":" << zoneId << ","
            << "\"killerStreak\":" << sECCombatMgr->GetKillStreak(killer)
            << "}";

    BridgeEvent event(EventNamespace::COMBAT, "pvp_kill", payload.str());
    sRealtimeBridge->EmitEvent(event);
    m_eventsSent++;
#endif

    ECC_LOG_DEBUG("[KILL] {} killed {} in zone {}",
        killer->GetName().c_str(), victim->GetName().c_str(), zoneId);
}

void CombatBridge::EmitPvPDeath(Player* victim, Player* killer, uint32 zoneId)
{
    if (!m_initialized || !victim)
        return;

#ifdef MOD_EC_REALTIME_BRIDGE
    std::ostringstream payload;
    payload << "{"
            << "\"victim\":" << BuildPlayerJson(victim) << ","
            << "\"killer\":" << (killer ? BuildPlayerJson(killer) : "null") << ","
            << "\"zoneId\":" << zoneId << ","
            << "\"lostStreak\":" << sECCombatMgr->GetKillStreak(victim)
            << "}";

    BridgeEvent event(EventNamespace::COMBAT, "pvp_death", payload.str());
    sRealtimeBridge->EmitEvent(event);
    m_eventsSent++;
#endif
}

void CombatBridge::EmitCreatureKill(Player* killer, Creature* victim)
{
    if (!m_initialized || !killer || !victim)
        return;

    // Only emit for notable kills (boss, elite, rare)
    // Note: AzerothCore uses isElite() (lowercase) and rank values for creature types
    // CREATURE_ELITE_RAREELITE = 2, CREATURE_ELITE_RARE = 4
    uint32 rank = victim->GetCreatureTemplate()->rank;
    bool isNotable = victim->IsDungeonBoss() || victim->isWorldBoss() ||
                     victim->isElite() || rank == CREATURE_ELITE_RAREELITE || rank == CREATURE_ELITE_RARE;
    if (!isNotable)
        return;

#ifdef MOD_EC_REALTIME_BRIDGE
    std::ostringstream payload;
    payload << "{"
            << "\"killer\":" << BuildPlayerJson(killer) << ","
            << "\"creature\":" << BuildCreatureJson(victim)
            << "}";

    BridgeEvent event(EventNamespace::COMBAT, "creature_kill", payload.str());
    sRealtimeBridge->EmitEvent(event);
    m_eventsSent++;
#endif

    ECC_LOG_DEBUG("[KILL] {} killed {} (creature)",
        killer->GetName().c_str(), victim->GetName().c_str());
}

void CombatBridge::EmitKilledByCreature(Player* victim, Creature* killer)
{
    if (!m_initialized || !victim || !killer)
        return;

#ifdef MOD_EC_REALTIME_BRIDGE
    std::ostringstream payload;
    payload << "{"
            << "\"victim\":" << BuildPlayerJson(victim) << ","
            << "\"creature\":" << BuildCreatureJson(killer) << ","
            << "\"lostStreak\":" << sECCombatMgr->GetKillStreak(victim)
            << "}";

    BridgeEvent event(EventNamespace::COMBAT, "killed_by_creature", payload.str());
    sRealtimeBridge->EmitEvent(event);
    m_eventsSent++;
#endif
}

void CombatBridge::EmitDuelComplete(Player* winner, Player* loser)
{
    if (!m_initialized || !winner || !loser)
        return;

#ifdef MOD_EC_REALTIME_BRIDGE
    std::ostringstream payload;
    payload << "{"
            << "\"winner\":" << BuildPlayerJson(winner) << ","
            << "\"loser\":" << BuildPlayerJson(loser)
            << "}";

    BridgeEvent event(EventNamespace::COMBAT, "duel_complete", payload.str());
    sRealtimeBridge->EmitEvent(event);
    m_eventsSent++;
#endif
}

// ============================================================================
// KILL STREAK EVENTS
// ============================================================================

void CombatBridge::EmitKillStreakStarted(Player* player, uint32 streak)
{
    if (!m_initialized || !player)
        return;

#ifdef MOD_EC_REALTIME_BRIDGE
    std::ostringstream payload;
    payload << "{"
            << "\"player\":" << BuildPlayerJson(player) << ","
            << "\"streak\":" << streak
            << "}";

    BridgeEvent event(EventNamespace::COMBAT, "kill_streak_started", payload.str());
    sRealtimeBridge->EmitEvent(event);
    m_eventsSent++;
#endif

    ECC_LOG_DEBUG("[STREAK] {} started a kill streak ({} kills)",
        player->GetName().c_str(), streak);
}

void CombatBridge::EmitKillStreakMilestone(Player* player, uint32 streak, const std::string& title)
{
    if (!m_initialized || !player)
        return;

#ifdef MOD_EC_REALTIME_BRIDGE
    std::ostringstream payload;
    payload << "{"
            << "\"player\":" << BuildPlayerJson(player) << ","
            << "\"streak\":" << streak << ","
            << "\"title\":\"" << EscapeJsonString(title) << "\""
            << "}";

    BridgeEvent event(EventNamespace::COMBAT, "kill_streak_milestone", payload.str());
    sRealtimeBridge->EmitEvent(event);
    m_eventsSent++;
#endif

    ECC_LOG_INFO("[STREAK] {}: {} ({} kills)",
        player->GetName().c_str(), title.c_str(), streak);
}

void CombatBridge::EmitKillStreakEnded(Player* victim, Player* killer, uint32 streak)
{
    if (!m_initialized || !victim)
        return;

#ifdef MOD_EC_REALTIME_BRIDGE
    std::ostringstream payload;
    payload << "{"
            << "\"victim\":" << BuildPlayerJson(victim) << ","
            << "\"killer\":" << (killer ? BuildPlayerJson(killer) : "null") << ","
            << "\"streak\":" << streak
            << "}";

    BridgeEvent event(EventNamespace::COMBAT, "kill_streak_ended", payload.str());
    sRealtimeBridge->EmitEvent(event);
    m_eventsSent++;
#endif

    ECC_LOG_INFO("[STREAK] {}'s kill streak ({}) ended by {}",
        victim->GetName().c_str(), streak,
        killer ? killer->GetName().c_str() : "death");
}

// ============================================================================
// MULTI-KILL EVENTS
// ============================================================================

void CombatBridge::EmitMultiKill(Player* killer, uint32 killCount, uint32 windowMs)
{
    if (!m_initialized || !killer)
        return;

    const char* title = "Multi Kill";
    if (killCount == 2) title = "Double Kill";
    else if (killCount == 3) title = "Triple Kill";
    else if (killCount == 4) title = "Quadra Kill";
    else if (killCount >= 5) title = "Penta Kill";

#ifdef MOD_EC_REALTIME_BRIDGE
    std::ostringstream payload;
    payload << "{"
            << "\"killer\":" << BuildPlayerJson(killer) << ","
            << "\"killCount\":" << killCount << ","
            << "\"windowMs\":" << windowMs << ","
            << "\"title\":\"" << title << "\""
            << "}";

    BridgeEvent event(EventNamespace::COMBAT, "multi_kill", payload.str());
    sRealtimeBridge->EmitEvent(event);
    m_eventsSent++;
#endif

    ECC_LOG_INFO("[MULTI] {}: {} ({} kills in {} ms)",
        killer->GetName().c_str(), title, killCount, windowMs);
}

void CombatBridge::EmitFirstBlood(Player* killer, Player* victim, uint32 bgInstanceId, uint32 bgTypeId)
{
    if (!m_initialized || !killer || !victim)
        return;

#ifdef MOD_EC_REALTIME_BRIDGE
    std::ostringstream payload;
    payload << "{"
            << "\"killer\":" << BuildPlayerJson(killer) << ","
            << "\"victim\":" << BuildPlayerJson(victim) << ","
            << "\"bgInstanceId\":" << bgInstanceId << ","
            << "\"bgTypeId\":" << bgTypeId
            << "}";

    BridgeEvent event(EventNamespace::COMBAT, "first_blood", payload.str());
    sRealtimeBridge->EmitEvent(event);
    m_eventsSent++;
#endif

    ECC_LOG_INFO("[FIRST BLOOD] {} killed {}",
        killer->GetName().c_str(), victim->GetName().c_str());
}

// ============================================================================
// ARENA EVENTS
// ============================================================================

void CombatBridge::EmitArenaMatchStart(uint32 arenaId, uint32 arenaType, uint8 teamSize,
                                        const std::string& team1Name, const std::string& team2Name)
{
    if (!m_initialized)
        return;

#ifdef MOD_EC_REALTIME_BRIDGE
    std::ostringstream payload;
    payload << "{"
            << "\"arenaId\":" << arenaId << ","
            << "\"arenaType\":" << arenaType << ","
            << "\"teamSize\":" << (int)teamSize << ","
            << "\"team1Name\":\"" << EscapeJsonString(team1Name) << "\","
            << "\"team2Name\":\"" << EscapeJsonString(team2Name) << "\""
            << "}";

    BridgeEvent event(EventNamespace::COMBAT, "arena_match_start", payload.str());
    sRealtimeBridge->EmitEvent(event);
    m_eventsSent++;
#endif

    ECC_LOG_INFO("[ARENA] Match started: {} vs {} ({}v{})",
        team1Name.c_str(), team2Name.c_str(), teamSize, teamSize);
}

void CombatBridge::EmitArenaMatchEnd(uint32 arenaId, uint32 arenaType, uint8 winnerTeamId,
                                      uint32 duration, uint32 team1Rating, uint32 team2Rating)
{
    if (!m_initialized)
        return;

#ifdef MOD_EC_REALTIME_BRIDGE
    std::ostringstream payload;
    payload << "{"
            << "\"arenaId\":" << arenaId << ","
            << "\"arenaType\":" << arenaType << ","
            << "\"winnerTeamId\":" << (int)winnerTeamId << ","
            << "\"duration\":" << duration << ","
            << "\"team1Rating\":" << team1Rating << ","
            << "\"team2Rating\":" << team2Rating
            << "}";

    BridgeEvent event(EventNamespace::COMBAT, "arena_match_end", payload.str());
    sRealtimeBridge->EmitEvent(event);
    m_eventsSent++;
#endif

    ECC_LOG_INFO("[ARENA] Match ended: Team {} won (duration: {}s)",
        winnerTeamId, duration);
}

void CombatBridge::EmitArenaRatingChange(Player* player, uint32 arenaType, int32 ratingChange,
                                          uint32 newRating)
{
    if (!m_initialized || !player)
        return;

#ifdef MOD_EC_REALTIME_BRIDGE
    std::ostringstream payload;
    payload << "{"
            << "\"player\":" << BuildPlayerJson(player) << ","
            << "\"arenaType\":" << arenaType << ","
            << "\"ratingChange\":" << ratingChange << ","
            << "\"newRating\":" << newRating
            << "}";

    BridgeEvent event(EventNamespace::COMBAT, "arena_rating_change", payload.str());
    sRealtimeBridge->EmitEvent(event);
    m_eventsSent++;
#endif
}

void CombatBridge::EmitArenaKill(Player* killer, Player* victim, uint32 arenaId)
{
    if (!m_initialized || !killer || !victim)
        return;

#ifdef MOD_EC_REALTIME_BRIDGE
    std::ostringstream payload;
    payload << "{"
            << "\"killer\":" << BuildPlayerJson(killer) << ","
            << "\"victim\":" << BuildPlayerJson(victim) << ","
            << "\"arenaId\":" << arenaId
            << "}";

    BridgeEvent event(EventNamespace::COMBAT, "arena_kill", payload.str());
    sRealtimeBridge->EmitEvent(event);
    m_eventsSent++;
#endif
}

// ============================================================================
// BATTLEGROUND EVENTS
// ============================================================================

void CombatBridge::EmitBGMatchStart(uint32 bgInstanceId, uint32 bgTypeId, const std::string& bgName)
{
    if (!m_initialized)
        return;

#ifdef MOD_EC_REALTIME_BRIDGE
    std::ostringstream payload;
    payload << "{"
            << "\"bgInstanceId\":" << bgInstanceId << ","
            << "\"bgTypeId\":" << bgTypeId << ","
            << "\"bgName\":\"" << EscapeJsonString(bgName) << "\""
            << "}";

    BridgeEvent event(EventNamespace::COMBAT, "bg_match_start", payload.str());
    sRealtimeBridge->EmitEvent(event);
    m_eventsSent++;
#endif

    ECC_LOG_INFO("[BG] Match started: {} (Instance {})", bgName.c_str(), bgInstanceId);
}

void CombatBridge::EmitBGMatchEnd(uint32 bgInstanceId, uint32 bgTypeId, TeamId winnerTeam,
                                   uint32 allianceScore, uint32 hordeScore, uint32 duration)
{
    if (!m_initialized)
        return;

#ifdef MOD_EC_REALTIME_BRIDGE
    std::ostringstream payload;
    payload << "{"
            << "\"bgInstanceId\":" << bgInstanceId << ","
            << "\"bgTypeId\":" << bgTypeId << ","
            << "\"winner\":\"" << (winnerTeam == TEAM_ALLIANCE ? "ALLIANCE" : "HORDE") << "\","
            << "\"allianceScore\":" << allianceScore << ","
            << "\"hordeScore\":" << hordeScore << ","
            << "\"duration\":" << duration
            << "}";

    BridgeEvent event(EventNamespace::COMBAT, "bg_match_end", payload.str());
    sRealtimeBridge->EmitEvent(event);
    m_eventsSent++;
#endif

    ECC_LOG_INFO("[BG] Match ended: {} wins (Alliance: {}, Horde: {})",
        winnerTeam == TEAM_ALLIANCE ? "Alliance" : "Horde", allianceScore, hordeScore);
}

void CombatBridge::EmitBGKill(Player* killer, Player* victim, uint32 bgInstanceId, uint32 bgTypeId)
{
    if (!m_initialized || !killer || !victim)
        return;

#ifdef MOD_EC_REALTIME_BRIDGE
    std::ostringstream payload;
    payload << "{"
            << "\"killer\":" << BuildPlayerJson(killer) << ","
            << "\"victim\":" << BuildPlayerJson(victim) << ","
            << "\"bgInstanceId\":" << bgInstanceId << ","
            << "\"bgTypeId\":" << bgTypeId
            << "}";

    BridgeEvent event(EventNamespace::COMBAT, "bg_kill", payload.str());
    sRealtimeBridge->EmitEvent(event);
    m_eventsSent++;
#endif
}

void CombatBridge::EmitBGFlagCapture(Player* player, uint32 bgInstanceId, uint32 bgTypeId)
{
    if (!m_initialized || !player)
        return;

#ifdef MOD_EC_REALTIME_BRIDGE
    std::ostringstream payload;
    payload << "{"
            << "\"player\":" << BuildPlayerJson(player) << ","
            << "\"bgInstanceId\":" << bgInstanceId << ","
            << "\"bgTypeId\":" << bgTypeId
            << "}";

    BridgeEvent event(EventNamespace::COMBAT, "bg_flag_capture", payload.str());
    sRealtimeBridge->EmitEvent(event);
    m_eventsSent++;
#endif

    ECC_LOG_INFO("[BG] {} captured the flag!", player->GetName().c_str());
}

void CombatBridge::EmitBGFlagReturn(Player* player, uint32 bgInstanceId, uint32 bgTypeId)
{
    if (!m_initialized || !player)
        return;

#ifdef MOD_EC_REALTIME_BRIDGE
    std::ostringstream payload;
    payload << "{"
            << "\"player\":" << BuildPlayerJson(player) << ","
            << "\"bgInstanceId\":" << bgInstanceId << ","
            << "\"bgTypeId\":" << bgTypeId
            << "}";

    BridgeEvent event(EventNamespace::COMBAT, "bg_flag_return", payload.str());
    sRealtimeBridge->EmitEvent(event);
    m_eventsSent++;
#endif
}

void CombatBridge::EmitBGObjectiveCapture(Player* player, uint32 bgInstanceId, uint32 bgTypeId,
                                           uint32 objectiveId, const std::string& objectiveName)
{
    if (!m_initialized || !player)
        return;

#ifdef MOD_EC_REALTIME_BRIDGE
    std::ostringstream payload;
    payload << "{"
            << "\"player\":" << BuildPlayerJson(player) << ","
            << "\"bgInstanceId\":" << bgInstanceId << ","
            << "\"bgTypeId\":" << bgTypeId << ","
            << "\"objectiveId\":" << objectiveId << ","
            << "\"objectiveName\":\"" << EscapeJsonString(objectiveName) << "\""
            << "}";

    BridgeEvent event(EventNamespace::COMBAT, "bg_objective_capture", payload.str());
    sRealtimeBridge->EmitEvent(event);
    m_eventsSent++;
#endif
}

void CombatBridge::EmitBGObjectiveDefend(Player* player, uint32 bgInstanceId, uint32 bgTypeId,
                                          uint32 objectiveId, const std::string& objectiveName)
{
    if (!m_initialized || !player)
        return;

#ifdef MOD_EC_REALTIME_BRIDGE
    std::ostringstream payload;
    payload << "{"
            << "\"player\":" << BuildPlayerJson(player) << ","
            << "\"bgInstanceId\":" << bgInstanceId << ","
            << "\"bgTypeId\":" << bgTypeId << ","
            << "\"objectiveId\":" << objectiveId << ","
            << "\"objectiveName\":\"" << EscapeJsonString(objectiveName) << "\""
            << "}";

    BridgeEvent event(EventNamespace::COMBAT, "bg_objective_defend", payload.str());
    sRealtimeBridge->EmitEvent(event);
    m_eventsSent++;
#endif
}

void CombatBridge::EmitBGHonorKill(Player* killer, Player* victim, uint32 bgInstanceId,
                                    uint32 honor, bool isHonorable)
{
    if (!m_initialized || !killer || !victim)
        return;

#ifdef MOD_EC_REALTIME_BRIDGE
    std::ostringstream payload;
    payload << "{"
            << "\"killer\":" << BuildPlayerJson(killer) << ","
            << "\"victim\":" << BuildPlayerJson(victim) << ","
            << "\"bgInstanceId\":" << bgInstanceId << ","
            << "\"honor\":" << honor << ","
            << "\"isHonorable\":" << (isHonorable ? "true" : "false")
            << "}";

    BridgeEvent event(EventNamespace::COMBAT, "bg_honor_kill", payload.str());
    sRealtimeBridge->EmitEvent(event);
    m_eventsSent++;
#endif
}

} // namespace ECCombat
