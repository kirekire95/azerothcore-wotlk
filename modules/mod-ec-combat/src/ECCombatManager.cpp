/*
 * mod-ec-combat - Combat Manager Implementation
 * Copyright (C) 2025 EC
 */

#include "ECCombatManager.h"
#include "ECCombatBridge.h"

#include "ArenaTeam.h"
#include "ArenaTeamMgr.h"
#include "Battleground.h"
#include "Config.h"
#include "Creature.h"
#include "Log.h"
#include "Player.h"

// Log macros
#define ECC_LOG_INFO(fmt, ...)  LOG_INFO("module.ec-combat", fmt, ##__VA_ARGS__)
#define ECC_LOG_DEBUG(fmt, ...) LOG_DEBUG("module.ec-combat", fmt, ##__VA_ARGS__)
#define ECC_LOG_ERROR(fmt, ...) LOG_ERROR("module.ec-combat", fmt, ##__VA_ARGS__)

namespace ECCombat
{

// ============================================================================
// SINGLETON
// ============================================================================

CombatManager* CombatManager::Instance()
{
    static CombatManager instance;
    return &instance;
}

CombatManager::CombatManager()
    : m_initialized(false)
{
}

CombatManager::~CombatManager()
{
    Shutdown();
}

// ============================================================================
// LIFECYCLE
// ============================================================================

void CombatManager::Initialize()
{
    if (m_initialized)
        return;

    LoadConfig();

    // Initialize bridge
    sCombatBridge->Initialize();

    m_initialized = true;
    ECC_LOG_INFO("Combat Manager initialized");
}

void CombatManager::Shutdown()
{
    if (!m_initialized)
        return;

    sCombatBridge->Shutdown();

    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    m_playerStats.clear();
    m_multiKillTrackers.clear();
    m_bgFirstBlood.clear();

    m_initialized = false;
    ECC_LOG_INFO("Combat Manager shutdown");
}

void CombatManager::LoadConfig()
{
    // Load from worldserver.conf
    m_config.streakStartThreshold = sConfigMgr->GetOption<uint32>("ECCombat.StreakStartThreshold", 3);
    m_config.multiKillWindowMs = sConfigMgr->GetOption<uint32>("ECCombat.MultiKillWindowMs", 10000);
    m_config.emitPvPKills = sConfigMgr->GetOption<bool>("ECCombat.EmitPvPKills", true);
    m_config.emitCreatureKills = sConfigMgr->GetOption<bool>("ECCombat.EmitCreatureKills", true);
    m_config.emitKillStreaks = sConfigMgr->GetOption<bool>("ECCombat.EmitKillStreaks", true);
    m_config.emitMultiKills = sConfigMgr->GetOption<bool>("ECCombat.EmitMultiKills", true);
    m_config.emitArenaEvents = sConfigMgr->GetOption<bool>("ECCombat.EmitArenaEvents", true);
    m_config.emitBGEvents = sConfigMgr->GetOption<bool>("ECCombat.EmitBGEvents", true);
    m_config.debug = sConfigMgr->GetOption<bool>("ECCombat.Debug", false);

    ECC_LOG_INFO("Config loaded - StreakStart: {}, MultiKillWindow: {}ms",
        m_config.streakStartThreshold, m_config.multiKillWindowMs);
}

// ============================================================================
// INTERNAL HELPERS
// ============================================================================

PlayerCombatStats& CombatManager::GetOrCreateStats(Player* player)
{
    auto it = m_playerStats.find(player->GetGUID());
    if (it != m_playerStats.end())
        return it->second;

    PlayerCombatStats stats;
    stats.playerGuid = player->GetGUID();
    stats.playerName = player->GetName();
    stats.sessionStart = std::chrono::steady_clock::now();

    auto result = m_playerStats.emplace(player->GetGUID(), stats);
    return result.first->second;
}

const StreakTitle* CombatManager::GetStreakTitle(uint32 streak)
{
    const StreakTitle* best = nullptr;
    for (const auto& title : STREAK_TITLES)
    {
        if (streak >= title.killCount)
            best = &title;
    }
    return best;
}

void CombatManager::CheckKillStreak(Player* player, PlayerCombatStats& stats)
{
    if (!m_config.emitKillStreaks)
        return;

    // Check if streak just started
    if (stats.currentKillStreak == m_config.streakStartThreshold)
    {
        sCombatBridge->EmitKillStreakStarted(player, stats.currentKillStreak);
    }

    // Check for milestone
    const StreakTitle* title = GetStreakTitle(stats.currentKillStreak);
    const StreakTitle* prevTitle = GetStreakTitle(stats.currentKillStreak - 1);

    if (title && title != prevTitle)
    {
        sCombatBridge->EmitKillStreakMilestone(player, stats.currentKillStreak, title->title);
    }
}

void CombatManager::CheckMultiKill(Player* player)
{
    if (!m_config.emitMultiKills)
        return;

    auto now = std::chrono::steady_clock::now();
    auto& tracker = m_multiKillTrackers[player->GetGUID()];

    auto msSinceLastKill = std::chrono::duration_cast<std::chrono::milliseconds>(
        now - tracker.lastKillTime).count();

    if (msSinceLastKill > m_config.multiKillWindowMs)
    {
        // Reset tracker - too long since last kill
        tracker.killCount = 1;
        tracker.firstKillTime = now;
    }
    else
    {
        tracker.killCount++;
    }
    tracker.lastKillTime = now;
    tracker.playerGuid = player->GetGUID();

    // Check for multi-kill (2+ kills in window)
    if (tracker.killCount >= 2)
    {
        auto windowMs = std::chrono::duration_cast<std::chrono::milliseconds>(
            now - tracker.firstKillTime).count();

        sCombatBridge->EmitMultiKill(player, tracker.killCount, (uint32)windowMs);

        // Update stats
        std::lock_guard<std::recursive_mutex> lock(m_mutex);
        auto& stats = GetOrCreateStats(player);
        switch (tracker.killCount)
        {
            case 2: stats.doubleKills++; break;
            case 3: stats.tripleKills++; break;
            case 4: stats.quadraKills++; break;
            default: if (tracker.killCount >= 5) stats.pentaKills++; break;
        }
    }
}

void CombatManager::CheckFirstBlood(Player* killer, Player* victim, Battleground* bg)
{
    if (!bg || !m_config.emitBGEvents)
        return;

    auto& tracker = m_bgFirstBlood[bg->GetInstanceID()];
    if (!tracker.firstBloodClaimed)
    {
        tracker.firstBloodClaimed = true;
        tracker.bgInstanceId = bg->GetInstanceID();

        sCombatBridge->EmitFirstBlood(killer, victim, bg->GetInstanceID(), bg->GetBgTypeID());
    }
}

// ============================================================================
// KILL TRACKING
// ============================================================================

void CombatManager::OnPvPKill(Player* killer, Player* victim)
{
    if (!m_initialized || !killer || !victim)
        return;

    std::lock_guard<std::recursive_mutex> lock(m_mutex);

    // Update killer stats
    auto& killerStats = GetOrCreateStats(killer);
    killerStats.totalPvPKills++;
    killerStats.currentKillStreak++;
    killerStats.lastKillTime = std::chrono::steady_clock::now();

    if (killerStats.currentKillStreak > killerStats.highestKillStreak)
        killerStats.highestKillStreak = killerStats.currentKillStreak;

    // Check streaks and multi-kills
    CheckKillStreak(killer, killerStats);
    CheckMultiKill(killer);

    // Emit kill event
    if (m_config.emitPvPKills)
    {
        ECC_LOG_INFO("PvP Kill: {} (L{}) killed {} (L{}) [streak: {}]",
            killer->GetName(), killer->GetLevel(),
            victim->GetName(), victim->GetLevel(),
            killerStats.currentKillStreak);
        sCombatBridge->EmitPvPKill(killer, victim, killer->GetZoneId());
    }
}

void CombatManager::OnPvPDeath(Player* victim, Player* killer)
{
    if (!m_initialized || !victim)
        return;

    std::lock_guard<std::recursive_mutex> lock(m_mutex);

    auto& victimStats = GetOrCreateStats(victim);

    // Check if victim had a kill streak to end
    if (victimStats.currentKillStreak >= m_config.streakStartThreshold)
    {
        ECC_LOG_INFO("Kill streak ended: {} had {} kills, ended by {}",
            victim->GetName(), victimStats.currentKillStreak,
            killer ? killer->GetName() : "unknown");
        sCombatBridge->EmitKillStreakEnded(victim, killer, victimStats.currentKillStreak);
    }

    victimStats.totalPvPDeaths++;
    victimStats.currentKillStreak = 0;

    // Reset multi-kill tracker
    m_multiKillTrackers.erase(victim->GetGUID());

    // Emit death event
    if (m_config.emitPvPKills)
    {
        sCombatBridge->EmitPvPDeath(victim, killer, victim->GetZoneId());
    }
}

void CombatManager::OnCreatureKill(Player* killer, Creature* victim)
{
    if (!m_initialized || !killer || !victim)
        return;

    if (!m_config.emitCreatureKills)
        return;

    // Only emit for notable kills (elite, rare elite, world boss, dungeon boss)
    uint32 rank = victim->GetCreatureTemplate()->rank;
    bool isNotable = victim->IsDungeonBoss() || victim->isWorldBoss() ||
                     victim->isElite() || rank == CREATURE_ELITE_RAREELITE || rank == CREATURE_ELITE_RARE;
    if (!isNotable)
        return;

    ECC_LOG_DEBUG("Notable creature kill: {} (L{}) killed {} (entry {})",
        killer->GetName(), killer->GetLevel(),
        victim->GetName(), victim->GetEntry());
    sCombatBridge->EmitCreatureKill(killer, victim);
}

void CombatManager::OnKilledByCreature(Player* victim, Creature* killer)
{
    if (!m_initialized || !victim || !killer)
        return;

    std::lock_guard<std::recursive_mutex> lock(m_mutex);

    auto& victimStats = GetOrCreateStats(victim);

    // Check if victim had a kill streak to end
    if (victimStats.currentKillStreak >= m_config.streakStartThreshold)
    {
        ECC_LOG_INFO("Kill streak ended: {} had {} kills, killed by creature {}",
            victim->GetName(), victimStats.currentKillStreak, killer->GetName());
        sCombatBridge->EmitKillStreakEnded(victim, nullptr, victimStats.currentKillStreak);
    }

    ECC_LOG_DEBUG("Player killed by creature: {} (L{}) killed by {} (entry {})",
        victim->GetName(), victim->GetLevel(),
        killer->GetName(), killer->GetEntry());

    victimStats.currentKillStreak = 0;

    // Reset multi-kill tracker
    m_multiKillTrackers.erase(victim->GetGUID());

    sCombatBridge->EmitKilledByCreature(victim, killer);
}

void CombatManager::OnDuelComplete(Player* winner, Player* loser)
{
    if (!m_initialized || !winner || !loser)
        return;

    sCombatBridge->EmitDuelComplete(winner, loser);
}

// ============================================================================
// ARENA TRACKING
// ============================================================================

void CombatManager::OnArenaStart(uint32 arenaId, uint32 arenaType, uint8 teamSize,
                                  const std::string& team1Name, const std::string& team2Name)
{
    if (!m_initialized || !m_config.emitArenaEvents)
        return;

    sCombatBridge->EmitArenaMatchStart(arenaId, arenaType, teamSize, team1Name, team2Name);
}

void CombatManager::OnArenaEnd(uint32 arenaId, uint32 arenaType, uint8 winnerTeamId,
                                uint32 duration, uint32 team1Rating, uint32 team2Rating)
{
    if (!m_initialized || !m_config.emitArenaEvents)
        return;

    sCombatBridge->EmitArenaMatchEnd(arenaId, arenaType, winnerTeamId, duration, team1Rating, team2Rating);
}

void CombatManager::OnArenaKill(Player* killer, Player* victim, uint32 arenaId)
{
    if (!m_initialized || !killer || !victim || !m_config.emitArenaEvents)
        return;

    std::lock_guard<std::recursive_mutex> lock(m_mutex);

    auto& killerStats = GetOrCreateStats(killer);
    killerStats.arenaKills++;

    auto& victimStats = GetOrCreateStats(victim);
    victimStats.arenaDeaths++;

    sCombatBridge->EmitArenaKill(killer, victim, arenaId);
}

// ============================================================================
// BATTLEGROUND TRACKING
// ============================================================================

void CombatManager::OnBGStart(Battleground* bg)
{
    if (!m_initialized || !bg || !m_config.emitBGEvents)
        return;

    // Initialize first blood tracker
    m_bgFirstBlood[bg->GetInstanceID()] = BGFirstBloodTracker{bg->GetInstanceID(), false};

    // If this is an arena, snapshot pre-match ratings and emit arena start
    if (bg->isArena() && m_config.emitArenaEvents)
    {
        uint8 arenaSlot = ArenaTeam::GetSlotByType(bg->GetArenaType());

        // Snapshot pre-match ratings for all players in the arena
        std::map<ObjectGuid, uint32> preMatchRatings;
        for (auto const& itr : bg->GetPlayers())
        {
            Player* player = itr.second;
            if (player)
                preMatchRatings[player->GetGUID()] = player->GetArenaPersonalRating(arenaSlot);
        }
        m_arenaPreMatchRatings[bg->GetInstanceID()] = std::move(preMatchRatings);

        // Get team names for the arena start event
        std::string team1Name = "Team 1";
        std::string team2Name = "Team 2";
        for (auto const& itr : bg->GetPlayers())
        {
            Player* player = itr.second;
            if (!player) continue;
            uint32 teamId = player->GetArenaTeamId(arenaSlot);
            if (ArenaTeam* team = sArenaTeamMgr->GetArenaTeamById(teamId))
            {
                if (player->GetBgTeamId() == TEAM_ALLIANCE)
                    team1Name = team->GetName();
                else
                    team2Name = team->GetName();
            }
        }

        OnArenaStart(bg->GetInstanceID(), bg->GetArenaType(),
                     bg->GetArenaType(), team1Name, team2Name);
    }

    sCombatBridge->EmitBGMatchStart(bg->GetInstanceID(), bg->GetBgTypeID(), bg->GetName());
}

void CombatManager::OnBGEnd(Battleground* bg, TeamId winner)
{
    if (!m_initialized || !bg || !m_config.emitBGEvents)
        return;

    uint32 duration = (uint32)(bg->GetEndTime() - bg->GetStartTime()) / 1000;

    // Get scores (this varies by BG type)
    uint32 allianceScore = 0;
    uint32 hordeScore = 0;

    // If this is an arena, emit rating changes (ratings are already updated by this point)
    if (bg->isArena() && m_config.emitArenaEvents)
    {
        uint8 arenaSlot = ArenaTeam::GetSlotByType(bg->GetArenaType());
        auto preMatchIt = m_arenaPreMatchRatings.find(bg->GetInstanceID());

        // Get team ratings for OnArenaEnd
        uint32 team1Rating = 0;
        uint32 team2Rating = 0;

        for (auto const& itr : bg->GetPlayers())
        {
            Player* player = itr.second;
            if (!player)
                continue;

            uint32 newRating = player->GetArenaPersonalRating(arenaSlot);
            int32 ratingChange = 0;

            // Compute delta from pre-match snapshot
            if (preMatchIt != m_arenaPreMatchRatings.end())
            {
                auto ratingIt = preMatchIt->second.find(player->GetGUID());
                if (ratingIt != preMatchIt->second.end())
                    ratingChange = static_cast<int32>(newRating) - static_cast<int32>(ratingIt->second);
            }

            // Track team ratings
            if (player->GetBgTeamId() == TEAM_ALLIANCE)
                team1Rating = newRating;
            else
                team2Rating = newRating;

            sCombatBridge->EmitArenaRatingChange(player, bg->GetArenaType(),
                                                  ratingChange, newRating);
        }

        // Clean up snapshot
        m_arenaPreMatchRatings.erase(bg->GetInstanceID());

        OnArenaEnd(bg->GetInstanceID(), bg->GetArenaType(),
                   static_cast<uint8>(winner), duration, team1Rating, team2Rating);
    }

    // Clean up first blood tracker
    m_bgFirstBlood.erase(bg->GetInstanceID());

    sCombatBridge->EmitBGMatchEnd(bg->GetInstanceID(), bg->GetBgTypeID(), winner,
                                   allianceScore, hordeScore, duration);
}

void CombatManager::OnBGKill(Player* killer, Player* victim, Battleground* bg)
{
    if (!m_initialized || !killer || !victim || !bg || !m_config.emitBGEvents)
        return;

    std::lock_guard<std::recursive_mutex> lock(m_mutex);

    auto& killerStats = GetOrCreateStats(killer);
    killerStats.bgKills++;

    auto& victimStats = GetOrCreateStats(victim);
    victimStats.bgDeaths++;

    // Check for first blood
    CheckFirstBlood(killer, victim, bg);

    sCombatBridge->EmitBGKill(killer, victim, bg->GetInstanceID(), bg->GetBgTypeID());

    // Also emit as honor kill (honor amount unavailable from hook, pass 0)
    sCombatBridge->EmitBGHonorKill(killer, victim, bg->GetInstanceID(), 0, true);
}

void CombatManager::OnBGFlagCapture(Player* player, Battleground* bg)
{
    if (!m_initialized || !player || !bg || !m_config.emitBGEvents)
        return;

    std::lock_guard<std::recursive_mutex> lock(m_mutex);

    auto& stats = GetOrCreateStats(player);
    stats.flagCaptures++;

    sCombatBridge->EmitBGFlagCapture(player, bg->GetInstanceID(), bg->GetBgTypeID());
}

void CombatManager::OnBGFlagReturn(Player* player, Battleground* bg)
{
    if (!m_initialized || !player || !bg || !m_config.emitBGEvents)
        return;

    std::lock_guard<std::recursive_mutex> lock(m_mutex);

    auto& stats = GetOrCreateStats(player);
    stats.flagReturns++;

    sCombatBridge->EmitBGFlagReturn(player, bg->GetInstanceID(), bg->GetBgTypeID());
}

void CombatManager::OnBGObjectiveCapture(Player* player, Battleground* bg, uint32 objId, const std::string& name)
{
    if (!m_initialized || !player || !bg || !m_config.emitBGEvents)
        return;

    std::lock_guard<std::recursive_mutex> lock(m_mutex);

    auto& stats = GetOrCreateStats(player);
    stats.objectiveCaptures++;

    sCombatBridge->EmitBGObjectiveCapture(player, bg->GetInstanceID(), bg->GetBgTypeID(), objId, name);
}

void CombatManager::OnBGObjectiveDefend(Player* player, Battleground* bg, uint32 objId, const std::string& name)
{
    if (!m_initialized || !player || !bg || !m_config.emitBGEvents)
        return;

    std::lock_guard<std::recursive_mutex> lock(m_mutex);

    auto& stats = GetOrCreateStats(player);
    stats.objectiveDefends++;

    sCombatBridge->EmitBGObjectiveDefend(player, bg->GetInstanceID(), bg->GetBgTypeID(), objId, name);
}

// ============================================================================
// STATS ACCESS
// ============================================================================

PlayerCombatStats CombatManager::GetPlayerStats(Player* player)
{
    if (!player)
        return PlayerCombatStats{};

    std::lock_guard<std::recursive_mutex> lock(m_mutex);

    auto it = m_playerStats.find(player->GetGUID());
    if (it != m_playerStats.end())
        return it->second;

    return PlayerCombatStats{};
}

uint32 CombatManager::GetKillStreak(Player* player)
{
    if (!player)
        return 0;

    std::lock_guard<std::recursive_mutex> lock(m_mutex);

    auto it = m_playerStats.find(player->GetGUID());
    if (it != m_playerStats.end())
        return it->second.currentKillStreak;

    return 0;
}

void CombatManager::ResetKillStreak(Player* player)
{
    if (!player)
        return;

    std::lock_guard<std::recursive_mutex> lock(m_mutex);

    auto it = m_playerStats.find(player->GetGUID());
    if (it != m_playerStats.end())
    {
        it->second.currentKillStreak = 0;
    }
}

} // namespace ECCombat
