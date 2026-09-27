/*
 * mod-ec-guard-system - Bridge Integration
 * Copyright (C) 2025 EC
 *
 * Emits guard system events to the shared realtime bridge.
 */

#ifndef EC_GUARD_SYSTEM_BRIDGE_H
#define EC_GUARD_SYSTEM_BRIDGE_H

#include "ECWantedLevel.h"
#include <atomic>
#include <string>

// Forward declarations
class Player;
class Creature;
enum TeamId : uint8;

namespace ECGuardSystem
{
    // Forward declare SecurityPhase from ECZoneSecurityState.h
    enum class SecurityPhase : uint8;
}

namespace ECGuardSystem
{

// ============================================================================
// GUARD SYSTEM BRIDGE
// ============================================================================

class GuardSystemBridge
{
public:
    static GuardSystemBridge* Instance();

    // Initialize bridge connection
    void Initialize();
    void Shutdown();

    // Crime events
    void EmitCrimeCommitted(Player* player, CrimeType type, Creature* victim, uint32 crimePoints);
    void EmitCivilianKilled(Player* player, Creature* victim);
    void EmitGuardAttacked(Player* player, Creature* guard);
    void EmitGuardKilled(Player* player, Creature* guard);

    // Wanted level changes
    void EmitWantedLevelChanged(Player* player, WantedStars oldStars, WantedStars newStars, uint32 points);
    void EmitWantedCleared(Player* player);
    void EmitWantedDecayed(Player* player, uint32 oldPoints, uint32 newPoints);

    // Guard dispatch events
    void EmitGuardsAlerted(Player* target, uint32 witnessCount, uint32 zoneId);
    void EmitGuardsDispatched(Player* target, uint32 guardCount, uint32 zoneId, bool elite);
    void EmitGuardsSpawned(Player* target, uint32 guardCount, uint32 zoneId, bool elite);
    void EmitGuardEngaged(Player* target, Creature* guard);

    // Broadcast events
    void EmitLocalDefenceAlert(Player* criminal, Creature* victim, uint32 zoneId);

    // Zone threat events (legacy with full state)
    void EmitZonePhaseChanged(uint32 zoneId, const std::string& zoneName, uint8 oldPhase, uint8 newPhase,
                              uint32 heatPoints, uint8 threatLevel, uint8 alarmState, uint8 controlState);

    // Zone security state machine phase change (new state machine)
    void EmitZonePhaseChange(uint32 zoneId, TeamId faction, SecurityPhase oldPhase, SecurityPhase newPhase);
    void EmitZoneStateChanged(uint32 zoneId, const std::string& zoneName, uint8 alarmState, uint8 controlState,
                              float guardSurvivalRate, uint32 activeCriminals);
    void EmitZoneHeatChanged(uint32 zoneId, const std::string& zoneName, uint32 oldHeat, uint32 newHeat,
                             uint8 threatLevel, bool isDecay);
    void EmitZoneFallen(uint32 zoneId, const std::string& zoneName, uint32 guardsKilled,
                        uint32 criminalsInvolved, uint32 eliteResponseDelayMs);
    void EmitZoneRecovered(uint32 zoneId, const std::string& zoneName);

    // Bounty system events
    void EmitBountyPosted(uint32 bountyId, uint32 targetGuid, const std::string& targetName,
                         uint32 wantedLevel, uint32 rewardGold, uint32 zoneId, const std::string& zoneName);
    void EmitBountyClaimed(uint32 bountyId, uint32 targetGuid, const std::string& targetName,
                          uint32 claimerGuid, const std::string& claimerName, uint32 rewardGold);
    void EmitBountyCancelled(uint32 bountyId, uint32 targetGuid, const std::string& targetName,
                            const std::string& reason);

    // Bounty hunter NPC events
    void EmitBountyHunterSpawned(uint32 hunterGuid, uint32 targetGuid, const std::string& targetName,
                                 uint32 hunterEntry, const std::string& hunterName);
    void EmitBountyHunterKilled(uint32 hunterGuid, uint32 targetGuid, const std::string& killerName,
                               const std::string& killedBy);
    void EmitBountyHunterTracking(ObjectGuid hunterGuid, ObjectGuid targetGuid, uint32 fromZone, uint32 toZone);
    void EmitBountyHunterDespawned(ObjectGuid hunterGuid, ObjectGuid targetGuid, const std::string& reason);

    // Elite response events
    void EmitEliteResponseTriggered(uint32 zoneId, const std::string& zoneName, uint32 delayMs,
                                    const std::string& factionForce);
    void EmitEliteForcesArriving(uint32 zoneId, const std::string& zoneName, uint32 count);
    void EmitEliteForcesArrived(uint32 zoneId, const std::string& zoneName, uint32 count);

    // Preparation phase events ("Shatter Before the Storm")
    void EmitPreparationPhaseStarted(uint32 zoneId, const std::string& zoneName, uint8 phase);
    void EmitDialogueTriggered(uint32 zoneId, const std::string& speakerName, const std::string& dialogue,
                               const std::string& speakerType);
    void EmitDialoguePairStarted(uint32 zoneId, const std::string& speakerA, const std::string& speakerB,
                                  uint32 lineCount);
    void EmitMilitiaSpawned(uint32 zoneId, uint32 count, const std::string& activity);
    void EmitMilitiaDespawned(uint32 zoneId, uint32 count);
    void EmitCrierAnnouncement(uint32 zoneId, const std::string& urgency, const std::string& announcement);
    void EmitBarricadesSpawned(uint32 zoneId, uint32 count);
    void EmitBarricadesDespawned(uint32 zoneId, uint32 count);
    void EmitBannerChanged(uint32 zoneId, const std::string& oldFaction, const std::string& newFaction);

    // Construction system events (peon building)
    void EmitConstructionStarted(uint32 zoneId, uint32 siteId, const std::string& siteName,
                                 uint32 structureEntry, uint8 workersAssigned, uint8 tripsRequired);
    void EmitConstructionProgress(uint32 zoneId, uint32 siteId, const std::string& siteName,
                                  uint8 tripsCompleted, uint8 tripsRequired);
    void EmitConstructionComplete(uint32 zoneId, uint32 siteId, const std::string& siteName,
                                  uint32 structureEntry);
    void EmitWorkerSpawned(uint32 zoneId, uint32 workerEntry, const std::string& workerName,
                           uint32 siteId, const std::string& siteName);
    void EmitWorkerSay(uint32 zoneId, const std::string& workerName, const std::string& text,
                       const std::string& sayType);
    void EmitWorkersFleeing(uint32 zoneId, uint32 workerCount);

    // Interactive object events
    void EmitInteractiveObjectSpawned(uint32 zoneId, uint32 goEntry, uint8 goType,
                                       const std::string& description);
    void EmitInteractiveObjectDespawned(uint32 zoneId, uint32 goEntry, const std::string& description);

    // Witness system events
    void EmitWitnessDetected(Player* criminal, Creature* witness, Creature* victim,
                              CrimeType crimeType, uint32 alertDurationMs);
    void EmitWitnessFleeing(Creature* witness, Player* criminal, bool towardGuard,
                             const std::string& yell);
    void EmitWitnessCrimeConfirmed(Player* criminal, CrimeType crimeType, uint32 crimePoints);
    void EmitWitnessCrimeSuppressed(Player* criminal, CrimeType crimeType, uint32 crimePoints);

    // Statistics
    uint64 GetEventsSent() const { return m_eventsSent.load(); }

private:
    GuardSystemBridge();
    ~GuardSystemBridge();

    // Helpers
    std::string BuildPlayerJson(Player* player);
    std::string BuildCreatureJson(Creature* creature);

    // Thread-safe event counter
    std::atomic<uint64> m_eventsSent;
    bool m_initialized;
};

#define sGuardSystemBridge ECGuardSystem::GuardSystemBridge::Instance()
#define sECGuardBridge     ECGuardSystem::GuardSystemBridge::Instance()

} // namespace ECGuardSystem

#endif // EC_GUARD_SYSTEM_BRIDGE_H
