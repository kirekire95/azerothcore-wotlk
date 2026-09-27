/*
 * EC Guard System - Zone State Tracking
 * GTA-style heat/threat system with phase progression
 */

#ifndef EC_GUARD_SYSTEM_ZONE_STATE_H
#define EC_GUARD_SYSTEM_ZONE_STATE_H

#include "ObjectGuid.h"
#include "Position.h"
#include "SharedDefines.h"
#include <algorithm>
#include <chrono>
#include <vector>
#include <set>
#include <map>
#include <mutex>
#include <optional>

namespace ECGuardSystem
{
    // ==========================================================================
    // ALARM STATE - Zone Alert Level
    // ==========================================================================
    enum class AlarmState : uint8
    {
        QUIET       = 0,    // Normal operations, no alerts
        UNEASY      = 1,    // Suspicious activity detected, guards alert
        MOBILIZING  = 2,    // Guards actively mobilizing, reinforcements called
        ATTACKED    = 3,    // Zone is under active attack
        OCCUPIED    = 4,    // Enemy forces control the zone (guards wiped)
        RECOVERING  = 5     // Guards returning, zone stabilizing
    };

    // ==========================================================================
    // CONTROL STATE - Who Controls the Zone
    // ==========================================================================
    enum class ControlState : uint8
    {
        STABLE      = 0,    // Normal faction control
        CONTESTED   = 1,    // Control being challenged (50%+ guards dead)
        FALLEN      = 2     // Zone has fallen to attackers
    };

    // ==========================================================================
    // ZONE PHASE - Event-like Phase Progression
    // ==========================================================================
    enum class ZonePhase : uint8
    {
        QUIET       = 0,    // Normal state - regular patrols
        UNEASY      = 1,    // NPCs nervous, warn players, extra patrols
        MOBILIZING  = 2,    // Guards spawning, barricades erected
        IMPACT      = 3,    // Full combat, chaos, destruction effects
        AFTERMATH   = 4     // Cleanup, rebuilding, special NPCs appear
    };

    // ==========================================================================
    // FACTION ELITE RESPONSE GROUP
    // ==========================================================================
    struct FactionResponseGroup
    {
        uint32 id = 0;
        uint8 faction = 0;                  // 0=Neutral, 1=Alliance, 2=Horde, 3=Both
        std::string responseName;            // Display name (e.g., "Kor'kron Elite")
        uint8 responseTier = 1;              // 1=Regular elite, 2=Champions, 3=Leader guard
        std::vector<uint32> creatureEntries; // Creature template entries
        uint32 spawnCountMin = 3;
        uint32 spawnCountMax = 6;
        uint32 spawnDelayMs = 30000;
        std::string announcementText;        // Zone announcement when they arrive
        std::set<uint32> zoneWhitelist;      // Only respond to these zones (empty = all)
        std::set<uint32> zoneBlacklist;      // Never respond to these zones
        uint8 minThreatLevel = 5;
        bool requiresZoneFallen = false;
        uint32 priority = 100;               // Lower = higher priority
        bool enabled = true;

        // Check if this response applies to a zone
        bool AppliesToZone(uint32 zoneId, TeamId zoneFaction, uint8 threatLevel, bool zoneFallen) const
        {
            if (!enabled) return false;
            if (threatLevel < minThreatLevel) return false;
            if (requiresZoneFallen && !zoneFallen) return false;

            // Faction check
            if (faction == 3) {} // Both factions
            else if (faction == 1 && zoneFaction != TEAM_ALLIANCE) return false;
            else if (faction == 2 && zoneFaction != TEAM_HORDE) return false;
            else if (faction == 0 && zoneFaction != TEAM_NEUTRAL) return false;

            // Zone whitelist/blacklist
            if (!zoneWhitelist.empty() && zoneWhitelist.find(zoneId) == zoneWhitelist.end())
                return false;
            if (!zoneBlacklist.empty() && zoneBlacklist.find(zoneId) != zoneBlacklist.end())
                return false;

            return true;
        }
    };

    // ==========================================================================
    // ZONE STATE CONFIGURATION (must be defined before ZoneState)
    // ==========================================================================
    struct ZoneStateConfig
    {
        // Heat decay
        uint32 heatDecayIntervalMs = 10000;     // Check decay every 10 seconds
        uint32 heatDecayDelayMs = 60000;        // 60 seconds of peace before decay
        uint32 heatDecayRatePerSec = 50;        // Points per second decay

        // Phase thresholds
        uint32 uneasyThreshold = 300;           // Heat for UNEASY
        uint32 mobilizingThreshold = 750;       // Heat for MOBILIZING
        uint32 impactThreshold = 1500;          // Heat for IMPACT
        uint32 maxHeatPoints = 5000;            // Saturation cap; prevents multi-hour stale incidents

        // Custom English phase announcements are not part of native WotLK
        // settlement guard behavior. State transitions and bridge events still
        // run when these player-facing broadcasts are disabled.
        bool phaseBroadcastsEnabled = false;

        // Guard thresholds for phase changes
        float contestedThreshold = 0.5f;        // 50% guards dead = contested
        float fallenThreshold = 0.1f;           // 10% guards alive = fallen

        // Bounty hunter settings
        bool bountyHuntersEnabled = true;
        uint32 bountyHunterSpawnStars = 3;      // Min wanted stars to spawn hunters
        uint32 maxBountyHuntersPerPlayer = 2;   // Max hunters per criminal
        uint32 bountyHunterDespawnMs = 600000;  // 10 minute lifetime

        // Phase durations (minimum time in phase before transition)
        uint32 quietToUneasyMs = 5000;          // 5 seconds
        uint32 uneasyToMobilizingMs = 15000;    // 15 seconds
        uint32 mobilizingToImpactMs = 30000;    // 30 seconds
        uint32 impactToAftermathMs = 60000;     // 60 seconds of no crime
        uint32 aftermathToQuietMs = 120000;     // 2 minutes recovery

        // Elite response settings
        bool eliteResponseEnabled = true;
        uint32 eliteResponseMinDelayMs = 180000;    // 3 minutes minimum (total massacre)
        uint32 eliteResponseMaxDelayMs = 900000;    // 15 minutes maximum (slow takeover)
        uint32 eliteResponseDurationMs = 300000;    // 5 minute event duration

        // Severity multipliers for response time
        float massacreSeverityMultiplier = 0.5f;    // Faster response for quick kills
        uint32 guardsKilledForFastResponse = 20;    // Kill this many guards fast = faster response
    };

    // ==========================================================================
    // ZONE BOUNTY HUNTER STATE (for ZoneState tracking, different from ECBountyHunterManager::BountyHunter)
    // ==========================================================================
    struct ZoneBountyHunter
    {
        ObjectGuid hunterGuid;              // The spawned bounty hunter creature
        ObjectGuid targetGuid;              // Player they're hunting
        uint32 hunterEntry;                 // Creature template entry
        uint32 spawnZoneId;                 // Zone where spawned
        uint32 spawnCrimeAreaId;            // Settlement area that issued pursuit
        TeamId spawnCrimeFaction;           // Offended faction that issued pursuit
        uint32 spawnMapId;                  // Map where spawned
        uint32 spawnInstanceId;             // Instance where spawned

        // Tracking behavior
        uint32 lastKnownZoneId = 0;         // Last zone target was seen
        float lastKnownX = 0.0f;
        float lastKnownY = 0.0f;
        float lastKnownZ = 0.0f;

        // State
        bool isAmbushing = false;           // Set up ambush at predicted location
        std::chrono::steady_clock::time_point spawnTime;
        std::chrono::steady_clock::time_point lastUpdateTime;
    };

    // ==========================================================================
    // ZONE STATE - Comprehensive Zone Tracking
    // ==========================================================================
    struct ZoneState
    {
        // Identity
        uint32 zoneId = 0;
        TeamId owningFaction = TEAM_NEUTRAL;
        std::string zoneName;

        // Threat tracking
        uint8 threatLevel = 0;              // 0-5, like wanted stars but for zones
        uint32 heatPoints = 0;              // Accumulated zone heat

        // Guard status
        uint32 initialGuardCount = 0;       // Guards at full strength
        uint32 guardsAlive = 0;             // Current living guards
        uint32 guardsKilledRecently = 0;    // Guards killed in current incident
        uint32 guardsSpawnedThisIncident = 0; // Reinforcements called in

        // State tracking
        AlarmState alarmState = AlarmState::QUIET;
        ControlState controlState = ControlState::STABLE;
        ZonePhase currentPhase = ZonePhase::QUIET;
        ZonePhase previousPhase = ZonePhase::QUIET;

        // Player tracking
        std::set<ObjectGuid> activeCriminals;  // Players with wanted level in zone
        std::set<ObjectGuid> civilianDeaths;   // Civilians killed in current incident
        uint32 totalCivilianDeaths = 0;
        uint32 totalGuardDeaths = 0;

        // Timing
        std::chrono::steady_clock::time_point incidentStartTime;
        std::chrono::steady_clock::time_point lastCrimeTime;
        std::chrono::steady_clock::time_point phaseChangeTime;
        std::chrono::steady_clock::time_point lastDecayTime;

        // Crime location tracking (for civilian flee behavior)
        Position lastCrimePosition;
        bool hasValidCrimePosition = false;

        // Bounty hunters active in this zone
        std::vector<ObjectGuid> activeBountyHunters;

        // Phase duration tracking (for time-based phase progression)
        uint32 phaseElapsedMs = 0;

        // =======================================================================
        // Helper Methods
        // =======================================================================

        bool IsUnderThreat() const
        {
            return threatLevel > 0 || alarmState != AlarmState::QUIET;
        }

        bool IsFallen() const
        {
            return controlState == ControlState::FALLEN;
        }

        bool IsContested() const
        {
            return controlState == ControlState::CONTESTED;
        }

        float GetGuardSurvivalRate() const
        {
            if (initialGuardCount == 0) return 1.0f;
            return static_cast<float>(guardsAlive) / static_cast<float>(initialGuardCount);
        }

        void AddCriminal(ObjectGuid playerGuid)
        {
            activeCriminals.insert(playerGuid);
            if (activeCriminals.size() == 1)
            {
                // First criminal, start incident
                incidentStartTime = std::chrono::steady_clock::now();
            }
        }

        void RemoveCriminal(ObjectGuid playerGuid)
        {
            activeCriminals.erase(playerGuid);
        }

        void AddHeat(uint32 points, uint32 maxHeatPoints)
        {
            uint64 total = static_cast<uint64>(heatPoints) + points;
            heatPoints = static_cast<uint32>(std::min<uint64>(total, maxHeatPoints));
            lastCrimeTime = std::chrono::steady_clock::now();
            UpdateThreatLevel();
        }

        void DecayHeat(uint32 points)
        {
            if (heatPoints > points)
                heatPoints -= points;
            else
                heatPoints = 0;
            UpdateThreatLevel();
        }

        void UpdateThreatLevel()
        {
            // Threat level thresholds (zone-wide, higher than player)
            if (heatPoints >= 5000)
                threatLevel = 5;
            else if (heatPoints >= 3000)
                threatLevel = 4;
            else if (heatPoints >= 1500)
                threatLevel = 3;
            else if (heatPoints >= 750)
                threatLevel = 2;
            else if (heatPoints >= 300)
                threatLevel = 1;
            else
                threatLevel = 0;
        }

        void OnGuardKilled()
        {
            if (guardsAlive > 0) guardsAlive--;
            guardsKilledRecently++;
            totalGuardDeaths++;

            // Check for control state changes
            float survival = GetGuardSurvivalRate();
            if (survival < 0.1f)
                controlState = ControlState::FALLEN;
            else if (survival < 0.5f)
                controlState = ControlState::CONTESTED;
        }

        void OnGuardSpawned()
        {
            guardsAlive++;
            guardsSpawnedThisIncident++;
        }

        void Reset()
        {
            threatLevel = 0;
            heatPoints = 0;
            guardsAlive = initialGuardCount;
            guardsKilledRecently = 0;
            guardsSpawnedThisIncident = 0;
            alarmState = AlarmState::QUIET;
            controlState = ControlState::STABLE;
            currentPhase = ZonePhase::QUIET;
            activeCriminals.clear();
            civilianDeaths.clear();
            activeBountyHunters.clear();
            phaseElapsedMs = 0;
        }

        // Calculate dynamic elite response delay based on massacre severity
        // Faster response for: more guards killed, quicker time-to-fall, more criminals
        uint32 CalculateEliteResponseDelay(const ZoneStateConfig& config) const
        {
            // Base delay starts at max
            float delayFactor = 1.0f;

            // Factor 1: Guards killed ratio (more guards = faster response)
            float guardKillRatio = static_cast<float>(totalGuardDeaths) /
                static_cast<float>(std::max(initialGuardCount, 1u));
            delayFactor -= guardKillRatio * 0.3f; // Up to 30% reduction

            // Factor 2: Speed of massacre (if many guards killed recently)
            if (guardsKilledRecently >= config.guardsKilledForFastResponse)
            {
                delayFactor *= config.massacreSeverityMultiplier;
            }
            else if (guardsKilledRecently >= config.guardsKilledForFastResponse / 2)
            {
                delayFactor *= (1.0f + config.massacreSeverityMultiplier) / 2.0f;
            }

            // Factor 3: Number of active criminals (more attackers = faster response)
            if (activeCriminals.size() >= 5)
                delayFactor *= 0.7f; // Group attack = 30% faster
            else if (activeCriminals.size() >= 3)
                delayFactor *= 0.85f; // Small group = 15% faster

            // Factor 4: Civilian casualties make it more urgent
            if (totalCivilianDeaths >= 10)
                delayFactor *= 0.8f; // 20% faster for civilian massacre

            // Clamp delay factor
            delayFactor = std::clamp(delayFactor, 0.2f, 1.0f);

            // Calculate actual delay
            uint32 delayRange = config.eliteResponseMaxDelayMs - config.eliteResponseMinDelayMs;
            uint32 delay = config.eliteResponseMinDelayMs +
                static_cast<uint32>(delayRange * delayFactor);

            return delay;
        }
    };

    // ==========================================================================
    // ZONE STATE MANAGER
    // ==========================================================================
    class ZoneStateManager
    {
    public:
        static ZoneStateManager* instance();

        // Initialization
        void Initialize();
        void LoadFromConfig();
        void LoadZoneStates();

        // Update
        void Update(uint32 diff);

        // Zone state access
        ZoneState* GetZoneState(uint32 zoneId);
        const ZoneState* GetZoneState(uint32 zoneId) const;  // Const overload
        ZoneState* GetOrCreateZoneState(uint32 zoneId, TeamId faction);
        std::vector<ZoneState> GetAllActiveZonesCopy() const;   // Thread-safe: returns copies
        std::vector<ZoneState> GetFallenZonesCopy() const;      // Thread-safe: returns copies
        // Legacy pointer versions - INTERNAL USE ONLY (not thread-safe across calls)
        std::vector<const ZoneState*> GetAllActiveZones() const;
        std::vector<const ZoneState*> GetFallenZones() const;

        // Zone events
        void OnZoneCrime(uint32 zoneId, ObjectGuid criminalGuid, uint32 heatPoints);
        void OnZoneGuardKilled(uint32 zoneId, ObjectGuid killerGuid);
        void OnZoneGuardSpawned(uint32 zoneId, ObjectGuid guardGuid);
        void OnPlayerEnterZone(ObjectGuid playerGuid, uint32 zoneId);
        void OnPlayerLeaveZone(ObjectGuid playerGuid, uint32 zoneId);

        // Bounty hunter management
        void SpawnBountyHunter(ObjectGuid targetGuid, uint32 zoneId);
        void UpdateBountyHunters(uint32 diff);
        void DespawnBountyHunter(ObjectGuid hunterGuid);
        std::optional<ZoneBountyHunter> GetBountyHunterForTargetCopy(ObjectGuid targetGuid) const;  // Thread-safe
        std::vector<ZoneBountyHunter> GetAllBountyHuntersForTargetCopy(ObjectGuid targetGuid) const;  // Thread-safe
        // Legacy pointer versions - INTERNAL USE ONLY (not thread-safe across calls)
        ZoneBountyHunter* GetBountyHunterForTarget(ObjectGuid targetGuid);
        std::vector<ZoneBountyHunter*> GetAllBountyHuntersForTarget(ObjectGuid targetGuid);

        // Phase management
        void CheckPhaseTransitions(uint32 diff);
        void TransitionPhase(uint32 zoneId, ZonePhase newPhase);
        void ApplyPhaseEffects(uint32 zoneId, ZonePhase phase);

        // Heat decay
        void ProcessHeatDecay(uint32 diff);

        // Elite response (bridges to World Events)
        void TriggerEliteResponse(uint32 zoneId);

        // Faction response groups
        void LoadFactionResponseGroups();
        const FactionResponseGroup* GetBestResponseForZone(uint32 zoneId) const;
        const std::vector<FactionResponseGroup>& GetAllResponseGroups() const { return m_factionResponses; }

        // Configuration
        const ZoneStateConfig& GetConfig() const { return m_config; }

        // Database Persistence
        void SaveAllZoneStates();
        void SaveZoneState(uint32 zoneId);
        void LoadSavedZoneStates();

        // Utility
        static const char* AlarmStateToString(AlarmState state);
        static const char* ControlStateToString(ControlState state);
        static const char* ZonePhaseToString(ZonePhase phase);

    private:
        ZoneStateManager() = default;
        ~ZoneStateManager() = default;

        // Thread safety mutex for zone state access
        // Using recursive_mutex because some public methods call other public methods
        mutable std::recursive_mutex m_stateMutex;

        // Zone states (key: zoneId) - PROTECTED BY m_stateMutex
        std::map<uint32, ZoneState> m_zoneStates;

        // Active bounty hunters - PROTECTED BY m_stateMutex
        std::map<ObjectGuid, ZoneBountyHunter> m_bountyHunters;

        // Faction response groups (loaded from database) - PROTECTED BY m_stateMutex
        std::vector<FactionResponseGroup> m_factionResponses;

        // Configuration
        ZoneStateConfig m_config;

        // Timers
        uint32 m_heatDecayTimer = 0;
        uint32 m_bountyHunterUpdateTimer = 0;
        uint32 m_phaseCheckTimer = 0;
        uint32 m_persistenceTimer = 0;
        static constexpr uint32 PERSISTENCE_INTERVAL_MS = 60000; // Save every 60 seconds

        // Internal methods
        void UpdateAlarmState(ZoneState& zone);
        void UpdateControlState(ZoneState& zone);
        ZonePhase DeterminePhase(const ZoneState& zone) const;
        void BroadcastPhaseChange(uint32 zoneId, ZonePhase oldPhase, ZonePhase newPhase);
        void BroadcastNPCBark(uint32 zoneId, const std::string& message);
        void ApplyUneasyEffects(uint32 zoneId);
    };

    // ==========================================================================
    // NPC FLAVOR TEXT FOR PHASES
    // ==========================================================================
    struct ZonePhaseFlavorText
    {
        static const std::vector<std::string>& GetUneasyBarks()
        {
            static std::vector<std::string> barks = {
                "Something doesn't feel right...",
                "Keep your eyes open.",
                "Did you hear that?",
                "Stay alert, citizen.",
                "I have a bad feeling about this.",
                "The guards seem on edge today."
            };
            return barks;
        }

        static const std::vector<std::string>& GetMobilizingBarks()
        {
            static std::vector<std::string> barks = {
                "All guards to your posts!",
                "Prepare for combat!",
                "Reinforcements are on the way!",
                "Civilians, seek shelter!",
                "Lock down the district!",
                "This is not a drill!"
            };
            return barks;
        }

        static const std::vector<std::string>& GetImpactBarks()
        {
            static std::vector<std::string> barks = {
                "We're under attack!",
                "Hold the line!",
                "Don't let them through!",
                "For the Alliance/Horde!",
                "They're breaking through!",
                "Protect the civilians!"
            };
            return barks;
        }

        static const std::vector<std::string>& GetAftermathBarks()
        {
            static std::vector<std::string> barks = {
                "Is it over?",
                "Check for survivors.",
                "The healers are on their way.",
                "We'll rebuild.",
                "Justice will be served.",
                "Count the dead..."
            };
            return barks;
        }

        static const std::vector<std::string>& GetFallenZoneBarks()
        {
            static std::vector<std::string> barks = {
                "The town has fallen!",
                "Retreat! Fall back!",
                "Send word to the capital!",
                "We need reinforcements!",
                "All hope is lost here...",
                "The Warchief/King must know!"
            };
            return barks;
        }
    };
}

#define sECZoneStateMgr ECGuardSystem::ZoneStateManager::instance()

#endif // EC_GUARD_SYSTEM_ZONE_STATE_H
