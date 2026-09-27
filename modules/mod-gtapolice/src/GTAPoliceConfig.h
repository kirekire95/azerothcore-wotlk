/*
 * mod-gtapolice
 * Configuration loader
 */

#ifndef GTA_POLICE_CONFIG_H
#define GTA_POLICE_CONFIG_H

#include "Config.h"
#include "Define.h"

class GTAPoliceConfig
{
public:
    static GTAPoliceConfig* Instance()
    {
        static GTAPoliceConfig instance;
        return &instance;
    }

    void Load();
    void Reload();

    // General
    bool Enabled;
    bool DebugLog;
    bool AffectPlayerbots;
    bool AllowDirectBookingWithoutTransport;

    // Creature entries
    uint32 OfficerEntry;
    uint32 CarEntry;
    uint32 CrewOfficerEntry;
    bool UseCustomCarModel;
    uint32 CustomCarDisplayId;
    uint32 FallbackCarDisplayId;

    // Vehicle seats
    int8 SeatDriver;
    int8 SeatCrewPassenger;
    int8 SeatPrisoner;

    // Crew system
    uint32 CrewSize;
    float CrewExitRange;
    float CrewArrestRange;
    float CrewEscapeRange;
    uint32 CrewExitDelay;
    uint32 CrewLoadingTimeout;
    float CrewRunSpeed;
    bool CrewDespawnNonDriver;

    // Spells
    uint32 SpellArrestStun;
    uint32 SpellArrestRoot;
    uint32 SpellSirenAura;

    // Officer behavior
    float OfficerScanRange;
    uint32 OfficerScanInterval;

    // Pursuit
    float CarSpawnDistance;
    float CarChaseRepathDistance;
    float CarArrestRange;
    uint32 CarChaseTimeout;
    float CarEscapeDistance;

    // Transport
    uint32 TransportFailsafe;
    float ArrivalDistance;

    // Jail
    uint32 JailMap;
    float JailX;
    float JailY;
    float JailZ;
    float JailO;

    // Guard System Integration
    bool UseGuardSystemWanted;
    uint8 MinWantedStarsForPursuit;
    uint8 MinWantedStarsForArrest;
    bool ClearWantedOnJail;

    // Bridge
    bool EmitBridgeEvents;

    // Star decay settings
    bool StarDecayEnabled;
    uint32 StarDecayIntervalMs;
    uint32 StarDecayGracePeriodMs;
    uint32 StarDecayRequiredEvasionMs;

    // Multi-car pursuit settings
    uint32 CarsPerStar[6];           // Cars dispatched per wanted level (index 0 unused, 1-5 stars)
    uint32 MaxCarsPerPursuit;
    uint32 CarReinforceIntervalMs;   // Time between reinforcement checks
    float CarSpawnMinDistance;       // Min distance from target for car spawn
    float CarSpawnMaxDistance;       // Max distance from target for car spawn

    // Radio chatter
    bool RadioChatterEnabled;
    uint32 RadioChatterIntervalMs;   // Time between random radio lines

    // Bribe system
    bool BribeEnabled;
    uint32 BribeCostPerStar;         // Gold cost (in copper) per wanted star
    uint32 BribeCooldownMs;          // Cooldown per player between bribes
    uint8 BribeMaxStars;             // Max stars that can be bribed away

    // Pay-n-Spray (wanted clearing NPC)
    bool PaynSprayEnabled;
    uint32 PaynSprayCostPerStar;     // Gold cost per star
    uint32 PaynSprayCooldownMs;      // Cooldown per player
    uint32 PaynSprayNPCEntry;        // NPC entry for Pay-n-Spray vendor

    // Zone heat system
    bool ZoneHeatEnabled;
    uint32 ZoneHeatDecayIntervalMs;  // How often heat decays
    uint32 ZoneHeatDecayAmount;      // Heat points removed per decay tick
    uint32 ZoneHeatPerCrime;         // Heat gained per crime in zone
    uint32 ZoneHeatThresholdLow;     // Below this = patrol, above = pursuit
    uint32 ZoneHeatThresholdHigh;    // Above this = aggressive dispatch

    // Witness system integration
    bool WitnessAlertsEnabled;           // React to witness events from guard system
    uint32 WitnessDetectedHeat;          // Zone heat added when witness detects crime
    uint32 WitnessConfirmedHeatBonus;    // Extra zone heat when witness confirms crime
    uint32 WitnessSuppressedHeatReduction; // Zone heat reduced when witness silenced

private:
    GTAPoliceConfig() { Load(); }
};

#define sGTAPoliceConfig GTAPoliceConfig::Instance()

#endif // GTA_POLICE_CONFIG_H
