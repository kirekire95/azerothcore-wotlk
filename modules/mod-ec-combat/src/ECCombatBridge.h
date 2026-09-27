/*
 * mod-ec-combat - Bridge Integration
 * Copyright (C) 2025 EC
 *
 * Emits combat events (kill feed, arena/BG) to the shared realtime bridge.
 */

#ifndef EC_COMBAT_BRIDGE_H
#define EC_COMBAT_BRIDGE_H

#include "Define.h"
#include <atomic>
#include <string>

// Forward declarations
class Player;
class Creature;
class Unit;
class Battleground;
enum TeamId : uint8;

namespace ECCombat
{

// ============================================================================
// COMBAT BRIDGE
// ============================================================================

class CombatBridge
{
public:
    static CombatBridge* Instance();

    // Initialize bridge connection
    void Initialize();
    void Shutdown();

    // ========================================================================
    // KILL FEED EVENTS
    // ========================================================================

    // PvP kills
    void EmitPvPKill(Player* killer, Player* victim, uint32 zoneId);
    void EmitPvPDeath(Player* victim, Player* killer, uint32 zoneId);

    // PvE kills
    void EmitCreatureKill(Player* killer, Creature* victim);
    void EmitKilledByCreature(Player* victim, Creature* killer);

    // Duel
    void EmitDuelComplete(Player* winner, Player* loser);

    // ========================================================================
    // KILL STREAK EVENTS
    // ========================================================================

    void EmitKillStreakStarted(Player* player, uint32 streak);
    void EmitKillStreakMilestone(Player* player, uint32 streak, const std::string& title);
    void EmitKillStreakEnded(Player* victim, Player* killer, uint32 streak);

    // ========================================================================
    // MULTI-KILL EVENTS
    // ========================================================================

    void EmitMultiKill(Player* killer, uint32 killCount, uint32 windowMs);
    void EmitFirstBlood(Player* killer, Player* victim, uint32 bgInstanceId, uint32 bgTypeId);

    // ========================================================================
    // ARENA EVENTS
    // ========================================================================

    void EmitArenaMatchStart(uint32 arenaId, uint32 arenaType, uint8 teamSize,
                             const std::string& team1Name, const std::string& team2Name);
    void EmitArenaMatchEnd(uint32 arenaId, uint32 arenaType, uint8 winnerTeamId,
                           uint32 duration, uint32 team1Rating, uint32 team2Rating);
    void EmitArenaRatingChange(Player* player, uint32 arenaType, int32 ratingChange,
                               uint32 newRating);
    void EmitArenaKill(Player* killer, Player* victim, uint32 arenaId);

    // ========================================================================
    // BATTLEGROUND EVENTS
    // ========================================================================

    void EmitBGMatchStart(uint32 bgInstanceId, uint32 bgTypeId, const std::string& bgName);
    void EmitBGMatchEnd(uint32 bgInstanceId, uint32 bgTypeId, TeamId winnerTeam,
                        uint32 allianceScore, uint32 hordeScore, uint32 duration);
    void EmitBGKill(Player* killer, Player* victim, uint32 bgInstanceId, uint32 bgTypeId);
    void EmitBGFlagCapture(Player* player, uint32 bgInstanceId, uint32 bgTypeId);
    void EmitBGFlagReturn(Player* player, uint32 bgInstanceId, uint32 bgTypeId);
    void EmitBGObjectiveCapture(Player* player, uint32 bgInstanceId, uint32 bgTypeId,
                                 uint32 objectiveId, const std::string& objectiveName);
    void EmitBGObjectiveDefend(Player* player, uint32 bgInstanceId, uint32 bgTypeId,
                                uint32 objectiveId, const std::string& objectiveName);
    void EmitBGHonorKill(Player* killer, Player* victim, uint32 bgInstanceId,
                          uint32 honor, bool isHonorable);

    // ========================================================================
    // STATISTICS
    // ========================================================================

    uint64 GetEventsSent() const { return m_eventsSent.load(); }

private:
    CombatBridge();
    ~CombatBridge();

    // Helpers
    std::string BuildPlayerJson(Player* player);
    std::string BuildCreatureJson(Creature* creature);
    std::string BuildUnitJson(Unit* unit);

    // Thread-safe event counter
    std::atomic<uint64> m_eventsSent;
    bool m_initialized;
};

#define sCombatBridge ECCombat::CombatBridge::Instance()
#define sECCombatBridge ECCombat::CombatBridge::Instance()

} // namespace ECCombat

#endif // EC_COMBAT_BRIDGE_H
