/*
 * mod-gtapolice
 * Main manager singleton for police pursuit system
 */

#ifndef GTA_POLICE_MANAGER_H
#define GTA_POLICE_MANAGER_H

#include "ObjectGuid.h"
#include "Player.h"
#include "Creature.h"
#include "Map.h"
#include "Position.h"
#include <unordered_map>
#include <mutex>
#include <optional>
#include <ctime>

// Forward declarations
class GTAPoliceConfig;
class GTAPoliceBridge;

// ============================================================================
// PURSUIT DATA
// ============================================================================

struct PursuitData
{
    ObjectGuid targetGuid;
    std::vector<ObjectGuid> carGuids;     // Multiple police cars
    ObjectGuid officerGuid;               // Scanner officer who dispatched (if any)

    // Crew tracking per car
    struct CarCrew {
        ObjectGuid carGuid;
        ObjectGuid driverGuid;
        ObjectGuid passengerGuid;
        bool crewExited = false;
        bool arrestComplete = false;
        bool driverReboarded = false;
    };
    std::vector<CarCrew> crews;

    // Find crew for a specific car
    const CarCrew* FindCrew(ObjectGuid carGuid) const
    {
        for (const auto& c : crews)
            if (c.carGuid == carGuid) return &c;
        return crews.empty() ? nullptr : &crews[0];
    }

    uint32 startTime;
    uint32 wantedStars;
    uint32 lastReinforcementTime;
    bool arrested;
    bool gmOverride = false;    // GM-initiated pursuit bypasses wanted checks
};

// ============================================================================
// MANAGER SINGLETON
// ============================================================================

class GTAPoliceManager
{
public:
    static GTAPoliceManager* Instance()
    {
        static GTAPoliceManager instance;
        return &instance;
    }

    void Initialize();
    void Shutdown();

    // Pursuit management
    bool CanStartPursuit(ObjectGuid playerGuid);
    bool StartPursuit(Player* target, Creature* officer = nullptr, bool gmOverride = false);
    bool StartImmediateArrest(Player* target, Unit* spawnRef = nullptr, bool gmOverride = false);
    void EndPursuit(ObjectGuid playerGuid, bool success);

    // State queries
    bool IsUnderPursuit(ObjectGuid playerGuid) const;
    bool IsPursuitGmOverride(ObjectGuid playerGuid) const;
    std::optional<PursuitData> GetPursuitData(ObjectGuid playerGuid) const;
    uint32 GetActivePursuitCount() const;
    bool IsVehicleModeAvailable() const;

    // Crew management
    void SetCrewGuids(ObjectGuid playerGuid, ObjectGuid carGuid, ObjectGuid driverGuid, ObjectGuid passengerGuid);

    // Wanted level integration (uses guard system if enabled)
    bool IsPlayerWanted(Player* player) const;
    uint32 GetPlayerWantedStars(Player* player) const;
    void SetPlayerWanted(Player* player, bool wanted);
    void ClearPlayerWanted(Player* player);

    // Star decay tracking
    void RecordCrime(ObjectGuid playerGuid);
    void StartEvasion(ObjectGuid playerGuid);

    // Guard system integration callback
    // Called by mod-ec-guard-system when wanted level changes
    void OnWantedLevelChanged(Player* player, uint8 oldStars, uint8 newStars);

    // Jail
    void JailPlayer(Player* player);
    void GetJailLocation(Player* player, uint32& mapId, float& x, float& y, float& z, float& o);
    void ApplyJailPunishment(Player* player, uint32 wantedStars);

    // Helpers
    static Position GetOffsetInFront(Unit* unit, float distance);
    static bool IsPlayerArrestable(Player* player, bool gmOverride = false);
    static bool MeetsArrestWantedThreshold(Player* player, bool gmOverride = false);

    // Bribe system
    bool AttemptBribe(Player* player);
    bool CanBribe(Player* player) const;
    uint32 GetBribeCost(Player* player) const;

    // Zone heat system
    void AddZoneHeat(uint32 zoneId, uint32 amount);
    void ReduceZoneHeat(uint32 zoneId, uint32 amount);
    uint32 GetZoneHeat(uint32 zoneId) const;
    uint8 GetZoneHeatLevel(uint32 zoneId) const;  // 0=cold, 1=warm, 2=hot

    // Periodic update (called from world update)
    void Update(uint32 diff);

private:
    GTAPoliceManager();
    ~GTAPoliceManager();

    struct JailLocation
    {
        uint32 mapId;
        float x, y, z, o;
        uint8 faction;  // 0=Alliance, 1=Horde, 2=Neutral
    };

    void LoadJailLocations();
    void ValidateVehicleSupport();
    void CleanExpiredCooldowns();

    bool SpawnCrewIntoVehicle(Creature* car, Map* map, PursuitData::CarCrew& crew);
    ObjectGuid SpawnPoliceCar(Player* target, uint32 carIndex, PursuitData::CarCrew& outCrew);
    void SpawnReinforcementCar(Player* target);

    // Reinforcement system
    void UpdateReinforcements(uint32 diff);

    // Star decay internal methods
    void UpdateStarDecay(uint32 diff);
    void DecayPlayerStars(ObjectGuid playerGuid);

    // Zone heat internal methods
    void UpdateZoneHeat(uint32 diff);

    // Star decay tracking per player
    struct DecayState {
        time_t lastCrimeTime = 0;        // When player last committed crime
        time_t evasionStartTime = 0;     // When player started evading (no pursuit)
        uint32 decayTimer = 0;           // Accumulator for decay interval
    };

    // Jail timer tracking
    struct JailData {
        uint32 releaseTime;              // getMSTime() when player should be released
        uint32 jailedAt;                 // getMSTime() when player was jailed
    };
    void CheckJailReleases();

    mutable std::mutex _mutex;
    std::unordered_map<ObjectGuid, PursuitData> _activePursuits;
    std::unordered_map<ObjectGuid, uint32> _pursuitCooldowns;  // player -> cooldown end time (getMSTime())
    std::vector<JailLocation> _jailLocations;  // Cached jail locations (loaded at Initialize)
    std::unordered_map<ObjectGuid, DecayState> _decayStates;  // Star decay tracking
    std::unordered_map<ObjectGuid, uint32> _bribeCooldowns;   // player -> cooldown end time (getMSTime())
    std::unordered_map<uint32, uint32> _zoneHeat;             // zoneId -> heat points
    std::unordered_map<ObjectGuid, JailData> _jailedPlayers;  // Jail timer tracking
    uint32 _zoneHeatDecayTimer = 0;
    bool _vehicleModeAvailable = true;

    static constexpr uint32 PURSUIT_COOLDOWN_MS = 30000;
};

#define sGTAPoliceMgr GTAPoliceManager::Instance()

#endif // GTA_POLICE_MANAGER_H
