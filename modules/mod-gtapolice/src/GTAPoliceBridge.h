/*
 * mod-gtapolice
 * Bridge adapter for EC Realtime Bridge integration
 */

#ifndef GTA_POLICE_BRIDGE_H
#define GTA_POLICE_BRIDGE_H

#include "Player.h"
#include "Creature.h"
#include "ObjectGuid.h"

// Forward declaration
class RealtimeBridge;

enum class PursuitState : uint8
{
    STARTED,
    CHASE,
    CREW_EXITING,
    CREW_APPROACHING,
    CREW_LOADING,
    ARRESTED,
    TRANSPORTING,
    ESCAPED,
    JAILED,
    TIMEOUT
};

class GTAPoliceBridge
{
public:
    static GTAPoliceBridge* Instance()
    {
        static GTAPoliceBridge instance;
        return &instance;
    }

    void Initialize();

    // Pursuit events
    void EmitPursuitStarted(Player* target, Creature* car, Creature* officer);
    void EmitPursuitStartedMultiCar(Player* target, uint32 carCount, uint8 wantedStars);
    void EmitPursuitStateChanged(Player* target, PursuitState state);
    void EmitPursuitEnded(Player* target, PursuitState reason);
    void EmitPursuitAutoDispatched(Player* target, uint8 wantedStars);
    void EmitReinforcementDispatched(Player* target, Creature* car);
    void EmitPursuitUpdate(Player* target, Creature* car, float distance, float speed, uint32 durationMs);

    // Arrest events
    void EmitPlayerArrested(Player* player, Creature* car);
    void EmitPlayerJailed(Player* player, uint32 mapId, float x, float y, float z);
    void EmitPlayerReleased(Player* player);

    // Star decay events
    void EmitStarDecay(Player* player, uint8 oldStars, uint8 newStars);

    // Bribe events
    void EmitBribe(Player* player, uint32 oldStars, uint32 cost);

    // Pay-n-Spray events
    void EmitPaynSpray(Player* player, uint32 oldStars, uint32 cost);

    // Zone heat events
    void EmitZoneHeatChanged(uint32 zoneId, uint32 heat, uint8 heatLevel);

    // Officer events
    void EmitOfficerDetectedWanted(Creature* officer, Player* target, uint32 wantedStars);
    void EmitOfficerDispatchedCar(Creature* officer, Creature* car, Player* target);

    // Crew events
    void EmitCrewExitedVehicle(Creature* crew, Creature* car, Player* target);
    void EmitCrewApproaching(Creature* crew, Player* target, float distance);
    void EmitCrewArrestingTarget(Creature* crew, Player* target);
    void EmitCrewReenteringVehicle(Creature* crew, Creature* car);
    void EmitCrewReenteredVehicle(Creature* crew, Creature* car);
    void EmitCrewOfficerDied(Creature* crew, Player* target);

private:
    GTAPoliceBridge() = default;

    void SendEvent(const std::string& eventType, const std::string& jsonData);
    std::string BuildPlayerJson(Player* player);
    std::string BuildCreatureJson(Creature* creature);
    std::string BuildPositionJson(float x, float y, float z, uint32 mapId);
};

#define sGTAPoliceBridge GTAPoliceBridge::Instance()

#endif // GTA_POLICE_BRIDGE_H
