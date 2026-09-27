/*
 * mod-ec-combat - Combat Manager
 * Copyright (C) 2025 EC
 *
 * Tracks kill streaks, multi-kills, and player combat statistics.
 */

#ifndef EC_COMBAT_MANAGER_H
#define EC_COMBAT_MANAGER_H

#include "ObjectGuid.h"
#include "SharedDefines.h"

#include <atomic>
#include <chrono>
#include <map>
#include <mutex>
#include <string>
#include <vector>

// Forward declarations
class Player;
class Creature;
class Battleground;

namespace ECCombat
{

// ============================================================================
// CONFIGURATION
// ============================================================================

struct CombatConfig
{
    // Kill streak thresholds
    uint32 streakStartThreshold = 3;      // Kills to start a streak
    uint32 streakMilestones[5] = {5, 10, 15, 20, 25};

    // Multi-kill window (milliseconds)
    uint32 multiKillWindowMs = 10000;     // 10 seconds between kills

    // Events to emit
    bool emitPvPKills = true;
    bool emitCreatureKills = true;        // Only bosses/elites
    bool emitKillStreaks = true;
    bool emitMultiKills = true;
    bool emitArenaEvents = true;
    bool emitBGEvents = true;

    // Debug mode
    bool debug = false;
};

// ============================================================================
// KILL STREAK TITLES
// ============================================================================

struct StreakTitle
{
    uint32 killCount;
    std::string title;
    std::string color;  // For UI
};

static const StreakTitle STREAK_TITLES[] = {
    {3,  "Killing Spree",   "#ffcc00"},
    {5,  "Rampage",         "#ff9900"},
    {10, "Unstoppable",     "#ff6600"},
    {15, "Dominating",      "#ff3300"},
    {20, "Godlike",         "#ff0000"},
    {25, "Legendary",       "#cc00ff"},
    {30, "Beyond Godlike",  "#ff00ff"},
};

// ============================================================================
// PLAYER STATS
// ============================================================================

struct PlayerCombatStats
{
    ObjectGuid playerGuid;
    std::string playerName;

    // Kill stats
    uint32 totalPvPKills = 0;
    uint32 totalPvPDeaths = 0;
    uint32 currentKillStreak = 0;
    uint32 highestKillStreak = 0;

    // Multi-kills
    uint32 doubleKills = 0;
    uint32 tripleKills = 0;
    uint32 quadraKills = 0;
    uint32 pentaKills = 0;

    // Arena stats
    uint32 arenaKills = 0;
    uint32 arenaDeaths = 0;
    uint32 arenaWins = 0;
    uint32 arenaLosses = 0;

    // BG stats
    uint32 bgKills = 0;
    uint32 bgDeaths = 0;
    uint32 bgWins = 0;
    uint32 bgLosses = 0;
    uint32 flagCaptures = 0;
    uint32 flagReturns = 0;
    uint32 objectiveCaptures = 0;
    uint32 objectiveDefends = 0;

    // Timestamps
    std::chrono::steady_clock::time_point lastKillTime;
    std::chrono::steady_clock::time_point sessionStart;
};

// ============================================================================
// MULTI-KILL TRACKER
// ============================================================================

struct MultiKillTracker
{
    ObjectGuid playerGuid;
    uint32 killCount = 0;
    std::chrono::steady_clock::time_point firstKillTime;
    std::chrono::steady_clock::time_point lastKillTime;
};

// ============================================================================
// BG FIRST BLOOD TRACKER
// ============================================================================

struct BGFirstBloodTracker
{
    uint32 bgInstanceId;
    bool firstBloodClaimed = false;
};

// ============================================================================
// COMBAT MANAGER
// ============================================================================

class CombatManager
{
public:
    static CombatManager* Instance();

    // Lifecycle
    void Initialize();
    void Shutdown();
    void LoadConfig();

    // ========================================================================
    // KILL TRACKING
    // ========================================================================

    // Called when a player kills another player
    void OnPvPKill(Player* killer, Player* victim);

    // Called when a player is killed by another player
    void OnPvPDeath(Player* victim, Player* killer);

    // Called when a player kills a creature
    void OnCreatureKill(Player* killer, Creature* victim);

    // Called when a player is killed by a creature
    void OnKilledByCreature(Player* victim, Creature* killer);

    // Called when a duel ends
    void OnDuelComplete(Player* winner, Player* loser);

    // ========================================================================
    // ARENA TRACKING
    // ========================================================================

    void OnArenaStart(uint32 arenaId, uint32 arenaType, uint8 teamSize,
                      const std::string& team1Name, const std::string& team2Name);
    void OnArenaEnd(uint32 arenaId, uint32 arenaType, uint8 winnerTeamId,
                    uint32 duration, uint32 team1Rating, uint32 team2Rating);
    void OnArenaKill(Player* killer, Player* victim, uint32 arenaId);

    // ========================================================================
    // BATTLEGROUND TRACKING
    // ========================================================================

    void OnBGStart(Battleground* bg);
    void OnBGEnd(Battleground* bg, TeamId winner);
    void OnBGKill(Player* killer, Player* victim, Battleground* bg);
    void OnBGFlagCapture(Player* player, Battleground* bg);
    void OnBGFlagReturn(Player* player, Battleground* bg);
    void OnBGObjectiveCapture(Player* player, Battleground* bg, uint32 objId, const std::string& name);
    void OnBGObjectiveDefend(Player* player, Battleground* bg, uint32 objId, const std::string& name);

    // ========================================================================
    // STATS ACCESS
    // ========================================================================

    PlayerCombatStats GetPlayerStats(Player* player);
    uint32 GetKillStreak(Player* player);
    void ResetKillStreak(Player* player);

    // ========================================================================
    // CONFIG
    // ========================================================================

    const CombatConfig& GetConfig() const { return m_config; }

private:
    CombatManager();
    ~CombatManager();

    // Internal helpers
    PlayerCombatStats& GetOrCreateStats(Player* player);
    void CheckKillStreak(Player* player, PlayerCombatStats& stats);
    void CheckMultiKill(Player* player);
    void CheckFirstBlood(Player* killer, Player* victim, Battleground* bg);
    const StreakTitle* GetStreakTitle(uint32 streak);

    // Data
    std::map<ObjectGuid, PlayerCombatStats> m_playerStats;
    std::map<ObjectGuid, MultiKillTracker> m_multiKillTrackers;
    std::map<uint32, BGFirstBloodTracker> m_bgFirstBlood;

    // Arena pre-match rating snapshots: instanceId -> { playerGuid -> personalRating }
    std::map<uint32, std::map<ObjectGuid, uint32>> m_arenaPreMatchRatings;

    // Config
    CombatConfig m_config;

    // Thread safety (recursive: OnKilledByCreature/OnPvPKill/OnPvPDeath lock,
    // then call CombatBridge which calls back into GetKillStreak)
    std::recursive_mutex m_mutex;

    // State
    bool m_initialized = false;
};

#define sECCombatMgr ECCombat::CombatManager::Instance()

} // namespace ECCombat

#endif // EC_COMBAT_MANAGER_H
