/*
 * EC Guard System - Zone Security State Machine
 * Inspired by Battleground status state machine pattern
 *
 * Each zone has a security state that transitions based on crime activity.
 * States flow: PEACEFUL -> ALERT -> PURSUIT -> LOCKDOWN -> COOLDOWN -> PEACEFUL
 */

#ifndef EC_ZONE_SECURITY_STATE_H
#define EC_ZONE_SECURITY_STATE_H

#include "Common.h"
#include "ObjectGuid.h"
#include "SharedDefines.h"
#include <chrono>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <vector>

namespace ECGuardSystem
{
    // =========================================================================
    // SECURITY PHASE (like BattlegroundStatus)
    // =========================================================================

    enum class SecurityPhase : uint8
    {
        PEACEFUL       = 0,  // No crimes, guards idle/patrol
        ALERT          = 1,  // Recent crime detected, guards on alert
        ACTIVE_PURSUIT = 2,  // Guards actively pursuing criminals
        LOCKDOWN       = 3,  // 5-star level - zone on lockdown, elite response
        COOLDOWN       = 4   // Post-incident, returning to normal
    };

    // =========================================================================
    // PHASE TRANSITION EVENTS
    // =========================================================================

    enum class SecurityTransition : uint8
    {
        CRIME_REPORTED      = 1,  // Any crime triggers PEACEFUL -> ALERT
        CRIME_ESCALATED     = 2,  // Serious crime triggers ALERT -> PURSUIT
        CRITICAL_THREAT     = 3,  // 5-star triggers PURSUIT -> LOCKDOWN
        CRIMINAL_ESCAPED    = 4,  // All criminals left zone -> COOLDOWN
        CRIMINAL_CAPTURED   = 5,  // Criminal killed/captured -> COOLDOWN
        TIMEOUT_EXPIRED     = 6,  // Phase timer expired
        SITUATION_RESOLVED  = 7   // COOLDOWN -> PEACEFUL
    };

    // =========================================================================
    // PHASE CONFIGURATION (like BG time intervals)
    // =========================================================================

    struct PhaseConfig
    {
        uint32 alertDurationMs      = 60000;    // 1 minute alert before pursuit
        uint32 pursuitDurationMs    = 180000;   // 3 minutes active pursuit
        uint32 lockdownDurationMs   = 300000;   // 5 minutes lockdown max
        uint32 cooldownDurationMs   = 120000;   // 2 minutes cooldown

        uint32 alertThreshold       = 50;       // Heat level to trigger ALERT
        uint32 pursuitThreshold     = 150;      // Heat level to trigger PURSUIT
        uint32 lockdownThreshold    = 300;      // Heat level to trigger LOCKDOWN

        uint32 guardsPerPhase[5]    = {0, 1, 3, 5, 2};  // Guards spawned per phase
        bool   elitesInLockdown     = true;     // Spawn elite guards in lockdown
        bool   announceTransitions  = true;     // Broadcast phase changes
    };

    // =========================================================================
    // ZONE SECURITY STATE (the state machine)
    // =========================================================================

    class ZoneSecurityState
    {
    public:
        ZoneSecurityState(uint32 zoneId, TeamId faction);
        ~ZoneSecurityState() = default;

        // Core state machine
        SecurityPhase GetPhase() const { return m_phase; }
        bool CanTransitionTo(SecurityPhase newPhase) const;
        bool TryTransition(SecurityTransition trigger);
        void ForceTransition(SecurityPhase newPhase);

        // Update (called from manager)
        void Update(uint32 diffMs);

        // Crime/heat tracking
        void OnCrimeCommitted(ObjectGuid criminal, uint32 crimePoints);
        void OnCriminalEscaped(ObjectGuid criminal);
        void OnCriminalCaptured(ObjectGuid criminal);
        void OnGuardSpawned(ObjectGuid guard);
        void OnGuardDespawned(ObjectGuid guard);

        // Accessors
        uint32 GetZoneId() const { return m_zoneId; }
        TeamId GetFaction() const { return m_faction; }
        uint32 GetHeatLevel() const { return m_heatLevel; }
        uint32 GetPhaseTimeMs() const { return m_phaseTimeMs; }
        uint32 GetActiveCriminalCount() const { return static_cast<uint32>(m_activeCriminals.size()); }
        uint32 GetActiveGuardCount() const { return static_cast<uint32>(m_activeGuards.size()); }
        bool IsEscalated() const { return m_phase >= SecurityPhase::ACTIVE_PURSUIT; }
        bool IsLockdown() const { return m_phase == SecurityPhase::LOCKDOWN; }

        // Criminal tracking
        bool IsCriminalActive(ObjectGuid guid) const;
        std::vector<ObjectGuid> GetActiveCriminals() const;

        // Configuration
        void SetConfig(const PhaseConfig& config) { m_config = config; }
        const PhaseConfig& GetConfig() const { return m_config; }

        // Callbacks for manager integration
        using PhaseChangeCallback = std::function<void(uint32 zoneId, SecurityPhase oldPhase, SecurityPhase newPhase)>;
        void SetPhaseChangeCallback(PhaseChangeCallback callback) { m_onPhaseChange = callback; }

        // Debug
        const char* GetPhaseString() const;
        void DumpState() const;

    private:
        // Transition logic
        void TransitionTo(SecurityPhase newPhase);
        void OnEnterPhase(SecurityPhase phase);
        void OnExitPhase(SecurityPhase phase);
        void CheckAutoTransitions();

        // Heat management
        void AddHeat(uint32 amount);
        void DecayHeat(uint32 diffMs);

        // Zone identity
        uint32 m_zoneId;
        TeamId m_faction;

        // State machine
        SecurityPhase m_phase = SecurityPhase::PEACEFUL;
        uint32 m_phaseTimeMs = 0;           // Time in current phase
        uint32 m_lastTransitionTime = 0;    // Game time of last transition

        // Heat system (like AV reinforcements)
        uint32 m_heatLevel = 0;
        uint32 m_peakHeatLevel = 0;         // Highest heat reached
        uint32 m_timeSinceLastCrime = 0;    // For decay calculation

        // Tracking
        std::set<ObjectGuid> m_activeCriminals;
        std::set<ObjectGuid> m_activeGuards;
        std::map<ObjectGuid, uint32> m_criminalHeatContribution;  // Per-criminal heat

        // Configuration
        PhaseConfig m_config;

        // Callbacks
        PhaseChangeCallback m_onPhaseChange;

        // Static decay rates
        static constexpr uint32 HEAT_DECAY_DELAY_MS = 30000;   // 30s peace before decay
        static constexpr uint32 HEAT_DECAY_PER_SEC = 5;        // Heat points per second
    };

    // =========================================================================
    // ZONE SECURITY MANAGER (manages all zone states)
    // =========================================================================

    class ZoneSecurityManager
    {
    public:
        static ZoneSecurityManager* Instance();

        // Lifecycle
        void Initialize();
        void Update(uint32 diffMs);

        // Zone state access
        ZoneSecurityState* GetZoneState(uint32 zoneId, TeamId faction);
        ZoneSecurityState* GetOrCreateZoneState(uint32 zoneId, TeamId faction);

        // Bulk operations
        void UpdateAllZones(uint32 diffMs);
        std::vector<ZoneSecurityState*> GetEscalatedZones() const;
        std::vector<ZoneSecurityState*> GetLockdownZones() const;

        // For GM commands - returns all non-peaceful zones with their metadata
        std::vector<std::tuple<uint32, TeamId, ZoneSecurityState*>> GetAllNonPeacefulStates() const;

        // Events (forwarded to appropriate zone)
        void OnCrimeInZone(uint32 zoneId, TeamId faction, ObjectGuid criminal, uint32 points);
        void OnCriminalLeftZone(uint32 zoneId, TeamId faction, ObjectGuid criminal);
        void OnCriminalEnteredZone(uint32 zoneId, TeamId faction, ObjectGuid criminal);

        // Statistics
        uint32 GetTotalActiveZones() const;
        uint32 GetTotalEscalatedZones() const;
        uint32 GetGlobalHeatLevel() const;

        // Callbacks for bridge integration
        using ZonePhaseCallback = std::function<void(uint32 zoneId, TeamId faction, SecurityPhase oldPhase, SecurityPhase newPhase)>;
        void SetZonePhaseCallback(ZonePhaseCallback callback) { m_onZonePhaseChange = callback; }

    private:
        ZoneSecurityManager() = default;
        ~ZoneSecurityManager() = default;

        // Zone state storage (key = zoneId << 8 | faction)
        std::map<uint64, std::unique_ptr<ZoneSecurityState>> m_zoneStates;

        // Callbacks
        ZonePhaseCallback m_onZonePhaseChange;

        // Helper
        uint64 MakeKey(uint32 zoneId, TeamId faction) const {
            return (static_cast<uint64>(zoneId) << 8) | static_cast<uint64>(faction);
        }
    };

    // =========================================================================
    // HELPER FUNCTIONS
    // =========================================================================

    inline const char* SecurityPhaseToString(SecurityPhase phase)
    {
        switch (phase)
        {
            case SecurityPhase::PEACEFUL:       return "Peaceful";
            case SecurityPhase::ALERT:          return "Alert";
            case SecurityPhase::ACTIVE_PURSUIT: return "Active Pursuit";
            case SecurityPhase::LOCKDOWN:       return "Lockdown";
            case SecurityPhase::COOLDOWN:       return "Cooldown";
            default: return "Unknown";
        }
    }

    inline const char* SecurityTransitionToString(SecurityTransition transition)
    {
        switch (transition)
        {
            case SecurityTransition::CRIME_REPORTED:     return "Crime Reported";
            case SecurityTransition::CRIME_ESCALATED:    return "Crime Escalated";
            case SecurityTransition::CRITICAL_THREAT:    return "Critical Threat";
            case SecurityTransition::CRIMINAL_ESCAPED:   return "Criminal Escaped";
            case SecurityTransition::CRIMINAL_CAPTURED:  return "Criminal Captured";
            case SecurityTransition::TIMEOUT_EXPIRED:    return "Timeout Expired";
            case SecurityTransition::SITUATION_RESOLVED: return "Situation Resolved";
            default: return "Unknown";
        }
    }

    #define sZoneSecurityMgr ECGuardSystem::ZoneSecurityManager::Instance()

} // namespace ECGuardSystem

#endif // EC_ZONE_SECURITY_STATE_H
