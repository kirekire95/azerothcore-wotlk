/*
 * EC Guard System - Zone Security State Machine Implementation
 */

#include "ECZoneSecurityState.h"
#include "ECGuardSystemManager.h"
#include "ECGuardSystemBridge.h"
#include "Log.h"
#include "Timer.h"

namespace ECGuardSystem
{
    // =========================================================================
    // ZONE SECURITY STATE IMPLEMENTATION
    // =========================================================================

    ZoneSecurityState::ZoneSecurityState(uint32 zoneId, TeamId faction)
        : m_zoneId(zoneId)
        , m_faction(faction)
        , m_phase(SecurityPhase::PEACEFUL)
        , m_phaseTimeMs(0)
        , m_lastTransitionTime(0)
        , m_heatLevel(0)
        , m_peakHeatLevel(0)
        , m_timeSinceLastCrime(0)
    {
    }

    bool ZoneSecurityState::CanTransitionTo(SecurityPhase newPhase) const
    {
        // Define valid state transitions (like BG status transitions)
        switch (m_phase)
        {
            case SecurityPhase::PEACEFUL:
                return newPhase == SecurityPhase::ALERT;

            case SecurityPhase::ALERT:
                return newPhase == SecurityPhase::ACTIVE_PURSUIT ||
                       newPhase == SecurityPhase::COOLDOWN;

            case SecurityPhase::ACTIVE_PURSUIT:
                return newPhase == SecurityPhase::LOCKDOWN ||
                       newPhase == SecurityPhase::COOLDOWN;

            case SecurityPhase::LOCKDOWN:
                return newPhase == SecurityPhase::COOLDOWN;

            case SecurityPhase::COOLDOWN:
                return newPhase == SecurityPhase::PEACEFUL ||
                       newPhase == SecurityPhase::ALERT;  // Re-escalate if new crime

            default:
                return false;
        }
    }

    bool ZoneSecurityState::TryTransition(SecurityTransition trigger)
    {
        SecurityPhase targetPhase = m_phase;

        // Determine target phase based on trigger and current state
        switch (trigger)
        {
            case SecurityTransition::CRIME_REPORTED:
                if (m_phase == SecurityPhase::PEACEFUL)
                    targetPhase = SecurityPhase::ALERT;
                else if (m_phase == SecurityPhase::COOLDOWN)
                    targetPhase = SecurityPhase::ALERT;  // Re-escalate
                break;

            case SecurityTransition::CRIME_ESCALATED:
                if (m_phase == SecurityPhase::ALERT)
                    targetPhase = SecurityPhase::ACTIVE_PURSUIT;
                break;

            case SecurityTransition::CRITICAL_THREAT:
                if (m_phase == SecurityPhase::ACTIVE_PURSUIT)
                    targetPhase = SecurityPhase::LOCKDOWN;
                break;

            case SecurityTransition::CRIMINAL_ESCAPED:
            case SecurityTransition::CRIMINAL_CAPTURED:
                if (m_activeCriminals.empty())
                {
                    if (m_phase == SecurityPhase::ALERT ||
                        m_phase == SecurityPhase::ACTIVE_PURSUIT ||
                        m_phase == SecurityPhase::LOCKDOWN)
                    {
                        targetPhase = SecurityPhase::COOLDOWN;
                    }
                }
                break;

            case SecurityTransition::TIMEOUT_EXPIRED:
                // Phase-specific timeout behavior
                if (m_phase == SecurityPhase::ALERT && m_heatLevel >= m_config.pursuitThreshold)
                    targetPhase = SecurityPhase::ACTIVE_PURSUIT;
                else if (m_phase == SecurityPhase::LOCKDOWN)
                    targetPhase = SecurityPhase::COOLDOWN;
                break;

            case SecurityTransition::SITUATION_RESOLVED:
                if (m_phase == SecurityPhase::COOLDOWN)
                    targetPhase = SecurityPhase::PEACEFUL;
                break;
        }

        // Attempt transition
        if (targetPhase != m_phase && CanTransitionTo(targetPhase))
        {
            TransitionTo(targetPhase);
            return true;
        }

        return false;
    }

    void ZoneSecurityState::ForceTransition(SecurityPhase newPhase)
    {
        if (newPhase != m_phase)
        {
            TransitionTo(newPhase);
        }
    }

    void ZoneSecurityState::TransitionTo(SecurityPhase newPhase)
    {
        SecurityPhase oldPhase = m_phase;

        // Exit current phase
        OnExitPhase(m_phase);

        // Update state
        m_phase = newPhase;
        m_phaseTimeMs = 0;
        m_lastTransitionTime = getMSTime();

        // Enter new phase
        OnEnterPhase(newPhase);

        // Notify callback
        if (m_onPhaseChange)
        {
            m_onPhaseChange(m_zoneId, oldPhase, newPhase);
        }

        ECG_LOG_INFO("Zone %u faction %u: %s -> %s (heat: %u)",
            m_zoneId, static_cast<uint8>(m_faction),
            SecurityPhaseToString(oldPhase),
            SecurityPhaseToString(newPhase),
            m_heatLevel);
    }

    void ZoneSecurityState::OnEnterPhase(SecurityPhase phase)
    {
        switch (phase)
        {
            case SecurityPhase::PEACEFUL:
                // Reset heat on entering peaceful
                m_heatLevel = 0;
                m_peakHeatLevel = 0;
                m_activeCriminals.clear();
                m_criminalHeatContribution.clear();
                break;

            case SecurityPhase::ALERT:
                // Guards become vigilant
                ECG_LOG_DEBUG("Zone %u entering ALERT phase", m_zoneId);
                break;

            case SecurityPhase::ACTIVE_PURSUIT:
                // Guards actively chase
                ECG_LOG_DEBUG("Zone %u entering ACTIVE_PURSUIT phase", m_zoneId);
                break;

            case SecurityPhase::LOCKDOWN:
                // Maximum response
                ECG_LOG_DEBUG("Zone %u entering LOCKDOWN phase", m_zoneId);
                break;

            case SecurityPhase::COOLDOWN:
                // Start decay timer
                ECG_LOG_DEBUG("Zone %u entering COOLDOWN phase", m_zoneId);
                break;
        }
    }

    void ZoneSecurityState::OnExitPhase(SecurityPhase phase)
    {
        switch (phase)
        {
            case SecurityPhase::LOCKDOWN:
                // Could despawn extra guards here
                break;
            default:
                break;
        }
    }

    void ZoneSecurityState::Update(uint32 diffMs)
    {
        m_phaseTimeMs += diffMs;

        // Decay heat over time
        DecayHeat(diffMs);

        // Check for automatic transitions
        CheckAutoTransitions();
    }

    void ZoneSecurityState::CheckAutoTransitions()
    {
        // Check heat thresholds for auto-escalation
        if (m_phase == SecurityPhase::PEACEFUL && m_heatLevel >= m_config.alertThreshold)
        {
            TryTransition(SecurityTransition::CRIME_REPORTED);
        }
        else if (m_phase == SecurityPhase::ALERT && m_heatLevel >= m_config.pursuitThreshold)
        {
            TryTransition(SecurityTransition::CRIME_ESCALATED);
        }
        else if (m_phase == SecurityPhase::ACTIVE_PURSUIT && m_heatLevel >= m_config.lockdownThreshold)
        {
            TryTransition(SecurityTransition::CRITICAL_THREAT);
        }

        // Check phase timeouts
        switch (m_phase)
        {
            case SecurityPhase::ALERT:
                if (m_phaseTimeMs >= m_config.alertDurationMs && m_activeCriminals.empty())
                {
                    TryTransition(SecurityTransition::SITUATION_RESOLVED);
                }
                break;

            case SecurityPhase::ACTIVE_PURSUIT:
                if (m_phaseTimeMs >= m_config.pursuitDurationMs && m_activeCriminals.empty())
                {
                    TryTransition(SecurityTransition::TIMEOUT_EXPIRED);
                }
                break;

            case SecurityPhase::LOCKDOWN:
                if (m_phaseTimeMs >= m_config.lockdownDurationMs)
                {
                    TryTransition(SecurityTransition::TIMEOUT_EXPIRED);
                }
                break;

            case SecurityPhase::COOLDOWN:
                if (m_phaseTimeMs >= m_config.cooldownDurationMs)
                {
                    TryTransition(SecurityTransition::SITUATION_RESOLVED);
                }
                break;

            default:
                break;
        }
    }

    void ZoneSecurityState::OnCrimeCommitted(ObjectGuid criminal, uint32 crimePoints)
    {
        // Track criminal
        m_activeCriminals.insert(criminal);
        m_criminalHeatContribution[criminal] += crimePoints;

        // Add heat
        AddHeat(crimePoints);

        // Reset crime timer
        m_timeSinceLastCrime = 0;

        // Trigger transition if needed
        if (m_phase == SecurityPhase::PEACEFUL)
        {
            TryTransition(SecurityTransition::CRIME_REPORTED);
        }
        else if (m_phase == SecurityPhase::COOLDOWN)
        {
            // Re-escalate if new crime during cooldown
            TryTransition(SecurityTransition::CRIME_REPORTED);
        }
    }

    void ZoneSecurityState::OnCriminalEscaped(ObjectGuid criminal)
    {
        m_activeCriminals.erase(criminal);

        if (m_activeCriminals.empty())
        {
            TryTransition(SecurityTransition::CRIMINAL_ESCAPED);
        }
    }

    void ZoneSecurityState::OnCriminalCaptured(ObjectGuid criminal)
    {
        m_activeCriminals.erase(criminal);

        // Reduce heat when criminal is captured
        auto it = m_criminalHeatContribution.find(criminal);
        if (it != m_criminalHeatContribution.end())
        {
            m_heatLevel = (m_heatLevel > it->second) ? m_heatLevel - it->second : 0;
            m_criminalHeatContribution.erase(it);
        }

        if (m_activeCriminals.empty())
        {
            TryTransition(SecurityTransition::CRIMINAL_CAPTURED);
        }
    }

    void ZoneSecurityState::OnGuardSpawned(ObjectGuid guard)
    {
        m_activeGuards.insert(guard);
    }

    void ZoneSecurityState::OnGuardDespawned(ObjectGuid guard)
    {
        m_activeGuards.erase(guard);
    }

    void ZoneSecurityState::AddHeat(uint32 amount)
    {
        uint32 oldHeat = m_heatLevel;
        m_heatLevel += amount;
        if (m_heatLevel > m_peakHeatLevel)
        {
            m_peakHeatLevel = m_heatLevel;
        }

        ECG_LOG_DEBUG("Zone %u heat: %u (+%u)", m_zoneId, m_heatLevel, amount);

        // Emit heat change to bridge
        uint8 threatLevel = static_cast<uint8>(m_phase);
        sGuardSystemBridge->EmitZoneHeatChanged(m_zoneId, "", oldHeat, m_heatLevel, threatLevel, false);
    }

    void ZoneSecurityState::DecayHeat(uint32 diffMs)
    {
        m_timeSinceLastCrime += diffMs;

        // Only decay after delay and if not in active phases
        if (m_timeSinceLastCrime < HEAT_DECAY_DELAY_MS)
            return;

        if (m_phase == SecurityPhase::ACTIVE_PURSUIT || m_phase == SecurityPhase::LOCKDOWN)
            return;

        // Calculate decay
        uint32 decayAmount = (diffMs * HEAT_DECAY_PER_SEC) / 1000;
        if (decayAmount > 0 && m_heatLevel > 0)
        {
            m_heatLevel = (m_heatLevel > decayAmount) ? m_heatLevel - decayAmount : 0;
        }
    }

    bool ZoneSecurityState::IsCriminalActive(ObjectGuid guid) const
    {
        return m_activeCriminals.find(guid) != m_activeCriminals.end();
    }

    std::vector<ObjectGuid> ZoneSecurityState::GetActiveCriminals() const
    {
        return std::vector<ObjectGuid>(m_activeCriminals.begin(), m_activeCriminals.end());
    }

    const char* ZoneSecurityState::GetPhaseString() const
    {
        return SecurityPhaseToString(m_phase);
    }

    void ZoneSecurityState::DumpState() const
    {
        LOG_INFO("module", "=== Zone Security State: Zone %u, Faction %u ===", m_zoneId, static_cast<uint8>(m_faction));
        LOG_INFO("module", "  Phase: %s", GetPhaseString());
        LOG_INFO("module", "  Phase Time: %u ms", m_phaseTimeMs);
        LOG_INFO("module", "  Heat Level: %u (peak: %u)", m_heatLevel, m_peakHeatLevel);
        LOG_INFO("module", "  Active Criminals: %zu", m_activeCriminals.size());
        LOG_INFO("module", "  Active Guards: %zu", m_activeGuards.size());
        LOG_INFO("module", "  Time Since Crime: %u ms", m_timeSinceLastCrime);
    }

    // =========================================================================
    // ZONE SECURITY MANAGER IMPLEMENTATION
    // =========================================================================

    ZoneSecurityManager* ZoneSecurityManager::Instance()
    {
        static ZoneSecurityManager instance;
        return &instance;
    }

    void ZoneSecurityManager::Initialize()
    {
        LOG_INFO("module", "ECGuardSystem: ZoneSecurityManager initialized");
    }

    void ZoneSecurityManager::Update(uint32 diffMs)
    {
        UpdateAllZones(diffMs);
    }

    void ZoneSecurityManager::UpdateAllZones(uint32 diffMs)
    {
        for (auto& [key, state] : m_zoneStates)
        {
            state->Update(diffMs);
        }
    }

    ZoneSecurityState* ZoneSecurityManager::GetZoneState(uint32 zoneId, TeamId faction)
    {
        uint64 key = MakeKey(zoneId, faction);
        auto it = m_zoneStates.find(key);
        return (it != m_zoneStates.end()) ? it->second.get() : nullptr;
    }

    ZoneSecurityState* ZoneSecurityManager::GetOrCreateZoneState(uint32 zoneId, TeamId faction)
    {
        uint64 key = MakeKey(zoneId, faction);
        auto it = m_zoneStates.find(key);

        if (it != m_zoneStates.end())
        {
            return it->second.get();
        }

        // Create new state
        auto state = std::make_unique<ZoneSecurityState>(zoneId, faction);

        // Set up callback to forward to manager callback
        state->SetPhaseChangeCallback([this, faction](uint32 zId, SecurityPhase oldPhase, SecurityPhase newPhase) {
            if (m_onZonePhaseChange)
            {
                m_onZonePhaseChange(zId, faction, oldPhase, newPhase);
            }
        });

        ZoneSecurityState* ptr = state.get();
        m_zoneStates[key] = std::move(state);

        ECG_LOG_DEBUG("Created security state for zone %u faction %u", zoneId, static_cast<uint8>(faction));

        return ptr;
    }

    std::vector<ZoneSecurityState*> ZoneSecurityManager::GetEscalatedZones() const
    {
        std::vector<ZoneSecurityState*> result;
        for (const auto& [key, state] : m_zoneStates)
        {
            if (state->IsEscalated())
            {
                result.push_back(state.get());
            }
        }
        return result;
    }

    std::vector<ZoneSecurityState*> ZoneSecurityManager::GetLockdownZones() const
    {
        std::vector<ZoneSecurityState*> result;
        for (const auto& [key, state] : m_zoneStates)
        {
            if (state->IsLockdown())
            {
                result.push_back(state.get());
            }
        }
        return result;
    }

    std::vector<std::tuple<uint32, TeamId, ZoneSecurityState*>> ZoneSecurityManager::GetAllNonPeacefulStates() const
    {
        std::vector<std::tuple<uint32, TeamId, ZoneSecurityState*>> result;
        for (const auto& [key, state] : m_zoneStates)
        {
            if (state->GetPhase() != SecurityPhase::PEACEFUL)
            {
                // Extract zoneId and faction from key
                uint32 zoneId = static_cast<uint32>(key >> 8);
                TeamId faction = static_cast<TeamId>(key & 0xFF);
                result.emplace_back(zoneId, faction, state.get());
            }
        }
        return result;
    }

    void ZoneSecurityManager::OnCrimeInZone(uint32 zoneId, TeamId faction, ObjectGuid criminal, uint32 points)
    {
        ZoneSecurityState* state = GetOrCreateZoneState(zoneId, faction);
        state->OnCrimeCommitted(criminal, points);
    }

    void ZoneSecurityManager::OnCriminalLeftZone(uint32 zoneId, TeamId faction, ObjectGuid criminal)
    {
        ZoneSecurityState* state = GetZoneState(zoneId, faction);
        if (state)
        {
            state->OnCriminalEscaped(criminal);
        }
    }

    void ZoneSecurityManager::OnCriminalEnteredZone(uint32 zoneId, TeamId faction, ObjectGuid criminal)
    {
        // If criminal enters a new zone, create state there
        ZoneSecurityState* state = GetOrCreateZoneState(zoneId, faction);
        // Note: Don't add crime here, just track presence
        // The WantedState tracks if they're wanted, this tracks zone awareness
    }

    uint32 ZoneSecurityManager::GetTotalActiveZones() const
    {
        uint32 count = 0;
        for (const auto& [key, state] : m_zoneStates)
        {
            if (state->GetPhase() != SecurityPhase::PEACEFUL)
            {
                ++count;
            }
        }
        return count;
    }

    uint32 ZoneSecurityManager::GetTotalEscalatedZones() const
    {
        return static_cast<uint32>(GetEscalatedZones().size());
    }

    uint32 ZoneSecurityManager::GetGlobalHeatLevel() const
    {
        uint32 total = 0;
        for (const auto& [key, state] : m_zoneStates)
        {
            total += state->GetHeatLevel();
        }
        return total;
    }

} // namespace ECGuardSystem
