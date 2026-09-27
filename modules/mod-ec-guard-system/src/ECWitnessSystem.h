/*
 * EC Guard System - Witness System
 * Crimes only register if a friendly NPC witnesses them.
 * Witnesses get 5 seconds to flee to a guard. Kill the witness = crime suppressed.
 */

#ifndef EC_WITNESS_SYSTEM_H
#define EC_WITNESS_SYSTEM_H

#include "ECWantedLevel.h"
#include "ObjectGuid.h"
#include <unordered_map>
#include <vector>
#include <mutex>
#include <functional>

class Player;
class Creature;

namespace ECGuardSystem
{
    // Active witness alert: an NPC saw a crime and is fleeing to report it
    struct WitnessAlert
    {
        ObjectGuid witnessGuid;       // The NPC who witnessed the crime
        ObjectGuid criminalGuid;      // The player who committed the crime
        ObjectGuid victimGuid;        // The NPC being attacked/killed
        CrimeType crimeType;
        uint32 crimePoints = 0;       // Points to add if confirmed
        uint32 crimeZoneId = 0;       // Zone where the witnessed crime actually occurred
        uint32 crimeAreaId = 0;       // Exact settlement area of the crime
        uint32 crimeMapId = 0;        // Origin map for resolving witness/victim
        uint32 crimeInstanceId = 0;   // Origin instance for resolving witness/victim
        TeamId crimeFaction = TEAM_NEUTRAL; // Local faction harmed by the crime
        uint32 alertTimerMs = 5000;   // 5-second countdown
        uint32 elapsedMs = 0;
        bool confirmed = false;       // True when timer expires or witness reaches guard
        bool suppressed = false;      // True when witness killed before confirmation
    };

    // Witness event types for external module callbacks
    enum class WitnessEventType : uint8
    {
        DETECTED   = 0,  // Witness found a crime, alert started
        CONFIRMED  = 1,  // Witness survived, crime registered
        SUPPRESSED = 2,  // Witness killed, crime covered up
    };

    // Callback signature: (criminal, zoneId, eventType, crimePoints)
    using WitnessEventCallback = std::function<void(Player*, uint32, WitnessEventType, uint32)>;

    class ECWitnessSystem
    {
    public:
        static ECWitnessSystem* instance();

        // Called instead of AddCrime() directly - checks for witnesses first
        // Returns true if a witness was found (crime is pending), false if no witness (no crime)
        bool CheckForWitnesses(Player* criminal, Creature* victim, CrimeType crimeType);

        // Called every update tick to process pending alerts
        void Update(uint32 diff);

        // Called when a creature dies - check if it was a witness
        void OnCreatureDeath(Creature* creature, Unit* killer);

        // Cancel delayed witness reports when death clears a criminal's wanted state
        void CancelAlertsForCriminal(ObjectGuid criminalGuid);

        // Configuration
        void LoadFromConfig();
        float GetWitnessRange() const { return m_witnessRange; }
        uint32 GetAlertDurationMs() const { return m_alertDurationMs; }
        bool IsEnabled() const { return m_enabled; }

        // External module callback registration
        // Modules like mod-gtapolice can register to receive witness events
        void RegisterCallback(WitnessEventCallback callback);

    private:
        ECWitnessSystem() = default;

        // Find the nearest friendly NPC that can witness the crime
        Creature* FindWitness(Player* criminal, Creature* victim);

        // Make a witness NPC react: yell and flee toward nearest guard
        void MakeWitnessFlee(Creature* witness, Player* criminal);

        // Confirm a crime (witness reached guard or timer expired)
        void ConfirmCrime(const WitnessAlert& alert);

        // Suppress a crime (witness was killed)
        void SuppressCrime(const WitnessAlert& alert, Player* criminal);

        // Active alerts
        mutable std::mutex m_alertMutex;
        std::vector<WitnessAlert> m_activeAlerts;

        // Notify registered callbacks
        void NotifyCallbacks(Player* criminal, uint32 zoneId, WitnessEventType eventType, uint32 crimePoints);

        // Configuration
        bool m_enabled = true;
        float m_witnessRange = 40.0f;          // Range to search for witnesses
        uint32 m_alertDurationMs = 5000;       // Time before crime is confirmed
        bool m_requireLineOfSight = true;      // Witness must have LoS to the crime

        // External module callbacks
        std::vector<WitnessEventCallback> m_callbacks;
    };
}

#define sECWitnessSys ECGuardSystem::ECWitnessSystem::instance()

#endif // EC_WITNESS_SYSTEM_H
