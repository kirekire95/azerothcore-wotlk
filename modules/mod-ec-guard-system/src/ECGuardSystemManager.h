/*
 * EC Guard System - Core Manager
 * GTA-style wanted level system for AzerothCore
 */

#ifndef EC_GUARD_SYSTEM_MANAGER_H
#define EC_GUARD_SYSTEM_MANAGER_H

#include "Common.h"
#include "ECWantedLevel.h"
#include "ObjectGuid.h"
#include "Log.h"
#include <map>
#include <unordered_map>
#include <mutex>
#include <optional>
#include <cstdarg>
#include <sstream>

class Player;
class Creature;
class Map;
class Channel;

namespace ECGuardSystem
{
    // Guard configuration tables use 0=Neutral, 1=Alliance, 2=Horde.
    // AzerothCore TeamId uses 0=Alliance, 1=Horde, 2=Neutral, so never
    // static_cast values at this database boundary.
    constexpr TeamId GuardDbFactionToTeamId(uint8 faction)
    {
        switch (faction)
        {
            case 1: return TEAM_ALLIANCE;
            case 2: return TEAM_HORDE;
            default: return TEAM_NEUTRAL;
        }
    }

    constexpr uint8 TeamIdToGuardDbFaction(TeamId team)
    {
        switch (team)
        {
            case TEAM_ALLIANCE: return 1;
            case TEAM_HORDE: return 2;
            default: return 0;
        }
    }

    static_assert(GuardDbFactionToTeamId(0) == TEAM_NEUTRAL);
    static_assert(GuardDbFactionToTeamId(1) == TEAM_ALLIANCE);
    static_assert(GuardDbFactionToTeamId(2) == TEAM_HORDE);
    static_assert(TeamIdToGuardDbFaction(TEAM_ALLIANCE) == 1);
    static_assert(TeamIdToGuardDbFaction(TEAM_HORDE) == 2);
    static_assert(TeamIdToGuardDbFaction(TEAM_NEUTRAL) == 0);

    // ==========================================================================
    // DEBUG LOGGING SYSTEM
    // ==========================================================================

    enum class ECGLogLevel : uint8
    {
        ECG_ERROR   = 0,
        ECG_WARNING = 1,
        ECG_INFO    = 2,
        ECG_DEBUG   = 3,
        ECG_TRACE   = 4
    };

    class ECGuardDebug
    {
    public:
        static void Log(ECGLogLevel level, const char* func, const char* fmt, ...);
        static void LogToGame(const std::string& message);
        static void SetDebugEnabled(bool enabled);
        static void SetLogLevel(uint8 level);
        static void SetInGameLogging(bool enabled);
        static bool IsDebugEnabled();
        static uint8 GetLogLevel();
        static bool IsInGameLoggingEnabled();
        static const char* LogLevelToString(ECGLogLevel level);
        static const char* LogLevelToColor(ECGLogLevel level);

    private:
        static bool s_debugEnabled;
        static uint8 s_logLevel;
        static bool s_inGameLogging;
    };

    // Logging macros for convenience
    #define ECG_LOG_ERROR(fmt, ...) ECGuardSystem::ECGuardDebug::Log(ECGuardSystem::ECGLogLevel::ECG_ERROR, __FUNCTION__, fmt, ##__VA_ARGS__)
    #define ECG_LOG_WARN(fmt, ...)  ECGuardSystem::ECGuardDebug::Log(ECGuardSystem::ECGLogLevel::ECG_WARNING, __FUNCTION__, fmt, ##__VA_ARGS__)
    #define ECG_LOG_INFO(fmt, ...)  ECGuardSystem::ECGuardDebug::Log(ECGuardSystem::ECGLogLevel::ECG_INFO, __FUNCTION__, fmt, ##__VA_ARGS__)
    #define ECG_LOG_DEBUG(fmt, ...) ECGuardSystem::ECGuardDebug::Log(ECGuardSystem::ECGLogLevel::ECG_DEBUG, __FUNCTION__, fmt, ##__VA_ARGS__)
    #define ECG_LOG_TRACE(fmt, ...) ECGuardSystem::ECGuardDebug::Log(ECGuardSystem::ECGLogLevel::ECG_TRACE, __FUNCTION__, fmt, ##__VA_ARGS__)

    // ==========================================================================
    // GUARD SYSTEM MANAGER
    // ==========================================================================
    class ECGuardSystemManager
    {
    public:
        static ECGuardSystemManager* instance();

        // Initialization
        void LoadFromConfig();
        void LoadZoneGuardData();
        void Initialize();
        void Update(uint32 diff);

        // Crime handling
        void OnPlayerAttackCreature(Player* attacker, Creature* victim,
                                    bool allowTownAlarm = true,
                                    char const* trigger = "damage");
        void OnHostilePlayerNearCivilian(Player* player, Creature* civilian);
        void OnPlayerKillCreature(Player* killer, Creature* victim,
                                  bool allowTownAlarm = false,
                                  char const* trigger = "kill-fallback");
        void OnGuardDeath(Creature* guard, Player* killer);
        void OnNPCDeath(Creature* npc, Player* killer);

        // Wanted level management - Thread-safe accessors (preferred)
        bool IsPlayerWanted(ObjectGuid playerGuid) const;
        WantedStars GetPlayerWantedStars(ObjectGuid playerGuid) const;
        uint32 GetPlayerCrimePoints(ObjectGuid playerGuid) const;
        std::optional<WantedState> GetWantedStateCopy(ObjectGuid playerGuid) const;

        // Wanted level management - Pointer accessors (INTERNAL USE ONLY)
        // WARNING: These return pointers that may be invalidated by concurrent access.
        // Use the thread-safe accessors above for external code.
        WantedState* GetWantedState(ObjectGuid playerGuid);
        WantedState* GetOrCreateWantedState(ObjectGuid playerGuid);

        void AddCrime(Player* player, CrimeType type, Creature* victim);
        void ClearWanted(Player* player);
        void RemoveWantedState(ObjectGuid playerGuid);  // Erase entry from map (memory cleanup)
        void DecayWantedLevels(uint32 diffMs);

        // Guard dispatch
        void DispatchGuards(Player* target, Creature* witness = nullptr);
        bool DispatchCivilianTownAlarm(Player* target, Creature* caller,
                                       char const* trigger,
                                       bool allowCallerSpeech = true);
        uint32 AlertNearbyGuards(Player* target, Creature* witness, uint32 maxGuards);
        void SpawnGuards(Player* target, uint32 count, bool elite = false,
                         std::optional<TeamId> responseFaction = std::nullopt,
                         uint32 responseZoneId = 0,
                         bool scaleToPlayer = true,
                         bool useCustomChaseYell = true);
        uint32 GetGuardCountForStars(WantedStars stars) const;

        // Faction checks
        bool IsTownCivilianCandidate(Creature* creature) const;
        bool IsCreatureCivilian(Creature* creature) const;
        bool IsCreatureGuard(Creature* creature) const;
        bool HasTownGuardResponse(Player* player, Creature* victim) const;
        TeamId GetZoneFaction(uint32 zoneId) const;
        TeamId GetCrimeFaction(Player* player, Creature* victim) const;

        // LocalDefence broadcast
        void BroadcastNPCDeath(Creature* npc, Player* killer);

        // Voice/Flavor
        void PlayVictimYell(Creature* victim, Player* attacker);

        // Configuration accessors
        bool IsEnabled() const { return m_enabled; }
        bool IsDebugMode() const { return m_debugMode; }
        bool IsCivilianProximityEnabled() const { return m_civilianProximityEnabled; }
        uint32 GetCivilianProximityScanIntervalMs() const { return m_civilianProximityScanIntervalMs; }
        uint32 GetCivilianAlarmRepeatMs() const { return m_civilianAlarmRepeatMs; }
        const EscalationConfig& GetEscalationConfig() const { return m_escalation; }

        // Zone data
        const ZoneGuardConfig* GetExactZoneConfig(uint32 zoneId, TeamId faction) const;
        const ZoneGuardConfig* GetZoneConfig(uint32 zoneId, TeamId faction) const;
        void RegisterZoneConfig(const ZoneGuardConfig& config);

        // Guard tracking
        void RegisterSpawnedGuard(ObjectGuid guardGuid, ObjectGuid targetGuid);
        void UnregisterSpawnedGuard(ObjectGuid guardGuid);
        void OnSpawnedGuardDeath(Creature* guard);
        bool IsSpawnedGuard(ObjectGuid guardGuid) const;
        void SetGuardPatrolling(Creature* guard);

        // Elite guard spell system
        void RegisterGuardSpellTimers(ObjectGuid guardGuid, GuardTier tier);
        void UnregisterGuardSpellTimers(ObjectGuid guardGuid);
        GuardSpellTimers* GetGuardSpellTimers(ObjectGuid guardGuid);
        void UpdateGuardSpells(Creature* guard, uint32 diff);

    private:
        ECGuardSystemManager() = default;
        ~ECGuardSystemManager() = default;

        // Thread safety
        mutable std::recursive_mutex m_wantedMutex;

        // Per-player wanted states - PROTECTED BY m_wantedMutex
        std::unordered_map<ObjectGuid, WantedState> m_wantedStates;

        // Zone guard configurations (key: zoneId << 8 | faction)
        std::map<uint64, ZoneGuardConfig> m_zoneConfigs;

        // Thread safety for guard tracking
        mutable std::mutex m_guardMutex;

        // Spawned guard tracking (Guard GUID -> Target GUID) - PROTECTED BY m_guardMutex
        std::unordered_map<ObjectGuid, ObjectGuid> m_guardToTarget;

        // Elite guard spell timers (Guard GUID -> spell timers) - PROTECTED BY m_guardMutex
        std::unordered_map<ObjectGuid, GuardSpellTimers> m_guardSpellTimers;

        // Configuration
        bool m_enabled = true;
        bool m_debugMode = false;
        EscalationConfig m_escalation;

        // Decay settings
        uint32 m_decayIntervalMs = 5000;        // Check decay every 5 seconds
        uint32 m_decayDelayMs = 30000;          // 30 seconds of peace before decay
        uint32 m_decayRatePerSec = 10;          // Points per second decay

        // Guard spawn settings
        uint32 m_guardSpawnCooldownMs = 10000;  // 10 seconds between spawns
        uint32 m_maxActiveGuards = 6;           // Max guards per player
        uint32 m_guardPatrolDurationMs = 60000; // Out-of-combat lifetime for temporary guards
        float m_guardSearchRadius = 80.0f;      // Search radius for idle guards

        // Retail-style civilian guard calls
        bool m_civilianProximityEnabled = true;
        uint32 m_civilianProximityScanIntervalMs = 1000;
        uint32 m_civilianAlarmRepeatMs = 10000;
        uint32 m_civilianAlarmMinGuards = 1;
        uint32 m_civilianAlarmMaxGuards = 2;

        // Guard patrol settings
        bool m_guardPatrolEnabled = false;       // Optional non-native random wandering
        float m_guardPatrolRadius = 30.0f;       // Wander radius in yards
        bool m_customGuardYellsEnabled = false;  // Optional invented Universal-language lines

        // LocalDefence settings
        bool m_broadcastNPCDeaths = true;
        bool m_customLocalDefenceTextEnabled = false;

        // Timers
        uint32 m_updateTimer = 0;

        // Cleanup guards that despawned without dying (timer expiry)
        void CleanupDespawnedGuards();

        // Helper methods
        uint64 MakeZoneKey(uint32 zoneId, TeamId faction) const
        {
            return (static_cast<uint64>(zoneId) << 8) | static_cast<uint64>(faction);
        }
    };
}

#define sECGuardSystemMgr ECGuardSystem::ECGuardSystemManager::instance()

#endif // EC_GUARD_SYSTEM_MANAGER_H
