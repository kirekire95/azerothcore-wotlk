/*
 * mod-gtapolice
 * Main manager singleton implementation
 */

#include "GTAPoliceManager.h"
#include "GTAPoliceConfig.h"
#include "GTAPoliceBridge.h"
#include "GTAPoliceCarAI.h"
#include "Log.h"
#include "World.h"
#include "ObjectAccessor.h"
#include "TemporarySummon.h"
#include "Vehicle.h"
#include "SpellAuras.h"
#include "DatabaseEnv.h"
#include "WorldDatabase.h"
#include "Chat.h"
#include "ObjectMgr.h"
#include "DBCStores.h"
#include "GridDefines.h"
#include "GridTerrainData.h"
#include <cmath>

// Conditional guard system integration
#ifdef MOD_EC_GUARD_SYSTEM
#include "ECGuardSystemManager.h"
#include "ECWantedLevel.h"
#include "ECWitnessSystem.h"
using namespace ECGuardSystem;
#endif

namespace
{
bool IsUsableGroundZ(float z)
{
    return std::isfinite(z) && z > INVALID_HEIGHT && std::fabs(z) < MAX_HEIGHT;
}

bool ResolveSafeGroundZ(Map* map, Unit* reference, float x, float y, float& outZ)
{
    if (!map || !reference)
        return false;

    float seedZ = reference->GetPositionZ();
    float groundZ = seedZ;
    float waterOrGroundZ = map->GetWaterOrGroundLevel(reference->GetPhaseMask(), x, y, seedZ, &groundZ);

    float chosenZ = waterOrGroundZ;
    if (!IsUsableGroundZ(chosenZ))
        chosenZ = groundZ;

    if (!IsUsableGroundZ(chosenZ))
        chosenZ = map->GetHeight(x, y, MAX_HEIGHT);

    if (!IsUsableGroundZ(chosenZ))
        chosenZ = map->GetHeight(x, y, seedZ + 50.0f);

    if (!IsUsableGroundZ(chosenZ))
        chosenZ = seedZ;

    if (!Acore::IsValidMapCoord(x, y, chosenZ))
        return false;

    outZ = chosenZ;
    return true;
}
}

// ============================================================================
// CONSTRUCTOR / DESTRUCTOR
// ============================================================================

GTAPoliceManager::GTAPoliceManager()
{
}

GTAPoliceManager::~GTAPoliceManager()
{
    Shutdown();
}

void GTAPoliceManager::Initialize()
{
    LoadJailLocations();
    ValidateVehicleSupport();
    sGTAPoliceBridge->Initialize();

    // Register witness callback for zone heat integration
#ifdef MOD_EC_GUARD_SYSTEM
    if (sGTAPoliceConfig->WitnessAlertsEnabled && sGTAPoliceConfig->ZoneHeatEnabled)
    {
        sECWitnessSys->RegisterCallback(
            [](Player* criminal, uint32 zoneId, WitnessEventType eventType, uint32 /*crimePoints*/)
            {
                if (!criminal || zoneId == 0 || !sGTAPoliceConfig->ZoneHeatEnabled)
                    return;

                switch (eventType)
                {
                    case WitnessEventType::DETECTED:
                    {
                        // Witness spotted a crime — adds minor heat (the yelling draws attention)
                        uint32 heat = sGTAPoliceConfig->WitnessDetectedHeat;
                        if (heat > 0)
                        {
                            sGTAPoliceMgr->AddZoneHeat(zoneId, heat);
                            if (sGTAPoliceConfig->DebugLog)
                                LOG_INFO("module", "[mod-gtapolice] Witness DETECTED crime in zone {}, +{} heat",
                                    zoneId, heat);
                        }
                        break;
                    }
                    case WitnessEventType::CONFIRMED:
                    {
                        // Witness reported — bonus heat on top of the normal crime heat
                        uint32 heat = sGTAPoliceConfig->WitnessConfirmedHeatBonus;
                        if (heat > 0)
                        {
                            uint8 oldLevel = sGTAPoliceMgr->GetZoneHeatLevel(zoneId);
                            sGTAPoliceMgr->AddZoneHeat(zoneId, heat);
                            uint8 newLevel = sGTAPoliceMgr->GetZoneHeatLevel(zoneId);

                            if (newLevel != oldLevel)
                                sGTAPoliceBridge->EmitZoneHeatChanged(
                                    zoneId, sGTAPoliceMgr->GetZoneHeat(zoneId), newLevel);

                            if (sGTAPoliceConfig->DebugLog)
                                LOG_INFO("module", "[mod-gtapolice] Witness CONFIRMED crime in zone {}, +{} bonus heat",
                                    zoneId, heat);
                        }
                        break;
                    }
                    case WitnessEventType::SUPPRESSED:
                    {
                        // Witness silenced — reduce heat slightly (crime covered up)
                        uint32 reduction = sGTAPoliceConfig->WitnessSuppressedHeatReduction;
                        if (reduction > 0)
                        {
                            uint8 oldLevel = sGTAPoliceMgr->GetZoneHeatLevel(zoneId);
                            sGTAPoliceMgr->ReduceZoneHeat(zoneId, reduction);
                            uint8 newLevel = sGTAPoliceMgr->GetZoneHeatLevel(zoneId);

                            if (newLevel != oldLevel)
                                sGTAPoliceBridge->EmitZoneHeatChanged(
                                    zoneId, sGTAPoliceMgr->GetZoneHeat(zoneId), newLevel);

                            if (sGTAPoliceConfig->DebugLog)
                                LOG_INFO("module", "[mod-gtapolice] Witness SUPPRESSED in zone {}, -{} heat",
                                    zoneId, reduction);
                        }
                        break;
                    }
                }
            }
        );
        LOG_INFO("module", "[mod-gtapolice] Witness callback registered with guard system");
    }
#endif

    LOG_INFO("module", "[mod-gtapolice] Manager initialized");
}

bool GTAPoliceManager::IsVehicleModeAvailable() const
{
    return _vehicleModeAvailable;
}

void GTAPoliceManager::ValidateVehicleSupport()
{
    _vehicleModeAvailable = true;

    CreatureTemplate const* carTemplate = sObjectMgr->GetCreatureTemplate(sGTAPoliceConfig->CarEntry);
    if (!carTemplate)
    {
        LOG_ERROR("module",
            "[mod-gtapolice] CarEntry {} has no creature_template row. Disabling vehicle mode.",
            sGTAPoliceConfig->CarEntry);
        _vehicleModeAvailable = false;
        return;
    }

    if (carTemplate->VehicleId == 0)
    {
        LOG_WARN("module",
            "[mod-gtapolice] CarEntry {} has VehicleId=0. Running in no-vehicle arrest mode.",
            sGTAPoliceConfig->CarEntry);
        _vehicleModeAvailable = false;
        return;
    }

    if (!sVehicleStore.LookupEntry(carTemplate->VehicleId))
    {
        LOG_ERROR("module",
            "[mod-gtapolice] CarEntry {} uses missing VehicleId {} (Vehicle.dbc). "
            "Disabling vehicle mode to prevent client freezes.",
            sGTAPoliceConfig->CarEntry, carTemplate->VehicleId);

        // Self-heal DB for next restart so startup warning/freeze risk is removed.
        WorldDatabase.Execute(
            "UPDATE creature_template SET VehicleId = 0 WHERE entry = {} AND VehicleId = {}",
            sGTAPoliceConfig->CarEntry, carTemplate->VehicleId);
        LOG_WARN("module",
            "[mod-gtapolice] Updated creature_template entry {} VehicleId {} -> 0. "
            "Install custom Vehicle.dbc and set it back if you want vehicle transport.",
            sGTAPoliceConfig->CarEntry, carTemplate->VehicleId);

        _vehicleModeAvailable = false;
        return;
    }

    LOG_INFO("module",
        "[mod-gtapolice] Vehicle mode enabled (CarEntry {}, VehicleId {}).",
        sGTAPoliceConfig->CarEntry, carTemplate->VehicleId);
}

void GTAPoliceManager::Shutdown()
{
    std::lock_guard<std::mutex> lock(_mutex);
    _activePursuits.clear();
    _pursuitCooldowns.clear();
    _jailLocations.clear();
    _decayStates.clear();
    _bribeCooldowns.clear();
    _zoneHeat.clear();
    _jailedPlayers.clear();
}

void GTAPoliceManager::Update(uint32 diff)
{
    // Clean expired cooldowns every 60 seconds
    static uint32 cooldownCleanupTimer = 0;
    cooldownCleanupTimer += diff;
    if (cooldownCleanupTimer >= 60000)
    {
        cooldownCleanupTimer = 0;
        CleanExpiredCooldowns();
    }

    // Star decay processing
    UpdateStarDecay(diff);

    // Reinforcement system
    UpdateReinforcements(diff);

    // Zone heat decay
    UpdateZoneHeat(diff);

    // Jail release checks
    CheckJailReleases();
}

void GTAPoliceManager::CleanExpiredCooldowns()
{
    uint32 now = getMSTime();
    std::lock_guard<std::mutex> lock(_mutex);
    for (auto it = _pursuitCooldowns.begin(); it != _pursuitCooldowns.end(); )
    {
        if (it->second < now)
            it = _pursuitCooldowns.erase(it);
        else
            ++it;
    }
    for (auto it = _bribeCooldowns.begin(); it != _bribeCooldowns.end(); )
    {
        if (it->second < now)
            it = _bribeCooldowns.erase(it);
        else
            ++it;
    }
}

void GTAPoliceManager::LoadJailLocations()
{
    QueryResult result = WorldDatabase.Query(
        "SELECT faction, map_id, cell_x, cell_y, cell_z, cell_o FROM ec_jail_locations ORDER BY faction, id");

    if (!result)
    {
        LOG_WARN("module", "[mod-gtapolice] No jail locations found in ec_jail_locations table");
        return;
    }

    do
    {
        Field* fields = result->Fetch();
        JailLocation loc;
        loc.faction = fields[0].Get<uint8>();
        loc.mapId = fields[1].Get<uint16>();
        loc.x = fields[2].Get<float>();
        loc.y = fields[3].Get<float>();
        loc.z = fields[4].Get<float>();
        loc.o = fields[5].Get<float>();
        _jailLocations.push_back(loc);
    } while (result->NextRow());

    LOG_INFO("module", "[mod-gtapolice] Loaded {} jail locations", _jailLocations.size());
}

// ============================================================================
// PURSUIT MANAGEMENT
// ============================================================================

bool GTAPoliceManager::CanStartPursuit(ObjectGuid playerGuid)
{
    std::lock_guard<std::mutex> lock(_mutex);

    // Already under pursuit?
    if (_activePursuits.find(playerGuid) != _activePursuits.end())
        return false;

    // Cooldown check
    auto it = _pursuitCooldowns.find(playerGuid);
    if (it != _pursuitCooldowns.end())
    {
        if (getMSTime() < it->second)
            return false;
    }

    return true;
}

bool GTAPoliceManager::StartPursuit(Player* target, Creature* officer, bool gmOverride)
{
    if (!target || !target->IsAlive())
        return false;

    if (!gmOverride)
    {
        if (!sGTAPoliceConfig->AffectPlayerbots && target->GetSession() && target->GetSession()->IsBot())
            return false;

        uint32 wantedStars = GetPlayerWantedStars(target);
        if (wantedStars < sGTAPoliceConfig->MinWantedStarsForPursuit)
            return false;
    }

    if (!CanStartPursuit(target->GetGUID()))
        return false;

    if (!IsPlayerArrestable(target, gmOverride))
        return false;

    // Missing vehicle data is not an arrest. Keep the wanted state active and let normal guards respond.
    if (!_vehicleModeAvailable)
    {
        if (!sGTAPoliceConfig->AllowDirectBookingWithoutTransport)
        {
            LOG_WARN("module",
                "[mod-gtapolice] Vehicle mode unavailable; pursuit skipped for {} (no teleport or booking)",
                target->GetName());
            return false;
        }

        LOG_WARN("module",
            "[mod-gtapolice] Vehicle mode unavailable; legacy direct-booking fallback enabled for {}",
            target->GetName());
        Unit* spawnRef = officer ? static_cast<Unit*>(officer) : static_cast<Unit*>(target);
        return StartImmediateArrest(target, spawnRef, gmOverride);
    }

    ObjectGuid targetGuid = target->GetGUID();
    uint32 stars = GetPlayerWantedStars(target);

    // Clamp to valid range (1-5 stars)
    if (stars < 1) stars = 1;
    if (stars > 5) stars = 5;

    uint32 numCars = std::min(sGTAPoliceConfig->CarsPerStar[stars], sGTAPoliceConfig->MaxCarsPerPursuit);

    LOG_INFO("module", "[mod-gtapolice] StartPursuit: {} stars={}, numCars={}, zone={}, map={}, pos=({:.0f},{:.0f},{:.0f})",
        target->GetName(), stars, numCars,
        target->GetZoneId(), target->GetMapId(),
        target->GetPositionX(), target->GetPositionY(), target->GetPositionZ());

    PursuitData data;
    data.targetGuid = targetGuid;
    data.wantedStars = stars;
    data.startTime = getMSTime();
    data.lastReinforcementTime = data.startTime;
    data.arrested = false;
    data.gmOverride = gmOverride;

    if (officer)
        data.officerGuid = officer->GetGUID();

    // Spawn initial cars
    for (uint32 i = 0; i < numCars; ++i)
    {
        PursuitData::CarCrew crew;
        ObjectGuid carGuid = SpawnPoliceCar(target, i, crew);
        if (!carGuid.IsEmpty())
        {
            data.carGuids.push_back(carGuid);
            data.crews.push_back(crew);
        }
    }

    if (data.carGuids.empty())
    {
        LOG_ERROR("module", "[mod-gtapolice] Failed to spawn any cars for pursuit");
        return false;
    }

    // Capture values before moving data into the map
    ObjectGuid firstCarGuid = data.carGuids[0];
    uint32 totalCars = static_cast<uint32>(data.carGuids.size());

    // Store pursuit data
    {
        std::lock_guard<std::mutex> lock(_mutex);
        _activePursuits[targetGuid] = std::move(data);
    }

    // Announce pursuit to the player
    ChatHandler(target->GetSession()).PSendSysMessage(
        "|cffff0000[LSPD]|r Attention! You have a {}-star wanted level. {} unit{} dispatched!",
        stars, totalCars, totalCars > 1 ? "s" : "");
    ChatHandler(target->GetSession()).SendSysMessage(
        "|cffff6600[LSPD Radio]|r All units, suspect spotted. Initiating pursuit.");

    // Emit bridge events (use captured values since data was moved)
    Creature* firstCar = ObjectAccessor::GetCreature(*target, firstCarGuid);
    sGTAPoliceBridge->EmitPursuitStarted(target, firstCar, officer);

    if (totalCars > 1)
    {
        sGTAPoliceBridge->EmitPursuitStartedMultiCar(target, totalCars, stars);
    }

    // Make the first car yell
    if (firstCar)
        firstCar->Yell("Pull over! This is the LSPD!", LANG_UNIVERSAL);

    if (sGTAPoliceConfig->DebugLog)
        LOG_INFO("module", "[mod-gtapolice] Started pursuit for {} with {} cars ({} stars)",
            target->GetName(), totalCars, stars);

    return true;
}

bool GTAPoliceManager::SpawnCrewIntoVehicle(Creature* car, Map* map, PursuitData::CarCrew& crew)
{
    if (!car || !map)
        return false;

    Position carPos;
    carPos.m_positionX = car->GetPositionX();
    carPos.m_positionY = car->GetPositionY();
    carPos.m_positionZ = car->GetPositionZ();
    carPos.m_orientation = car->GetOrientation();

    uint32 totalDuration = sGTAPoliceConfig->CarChaseTimeout
                         + sGTAPoliceConfig->CrewLoadingTimeout
                         + sGTAPoliceConfig->TransportFailsafe
                         + 10000;

    // Spawn driver officer
    TempSummon* driver = map->SummonCreature(
        sGTAPoliceConfig->CrewOfficerEntry,
        carPos,
        nullptr,
        totalDuration
    );

    if (driver)
    {
        driver->EnterVehicle(car, sGTAPoliceConfig->SeatDriver);
        crew.driverGuid = driver->GetGUID();

        if (sGTAPoliceConfig->DebugLog)
            LOG_INFO("module", "[mod-gtapolice] Spawned crew driver (guid={}) into seat {}",
                driver->GetGUID().GetCounter(), sGTAPoliceConfig->SeatDriver);
    }
    else
    {
        LOG_ERROR("module", "[mod-gtapolice] Failed to spawn crew driver");
    }

    // Spawn passenger officer
    if (sGTAPoliceConfig->CrewSize >= 2)
    {
        TempSummon* passenger = map->SummonCreature(
            sGTAPoliceConfig->CrewOfficerEntry,
            carPos,
            nullptr,
            totalDuration
        );

        if (passenger)
        {
            passenger->EnterVehicle(car, sGTAPoliceConfig->SeatCrewPassenger);
            crew.passengerGuid = passenger->GetGUID();

            if (sGTAPoliceConfig->DebugLog)
                LOG_INFO("module", "[mod-gtapolice] Spawned crew passenger (guid={}) into seat {}",
                    passenger->GetGUID().GetCounter(), sGTAPoliceConfig->SeatCrewPassenger);
        }
        else
        {
            LOG_ERROR("module", "[mod-gtapolice] Failed to spawn crew passenger");
        }
    }

    return driver != nullptr;
}

ObjectGuid GTAPoliceManager::SpawnPoliceCar(Player* target, uint32 carIndex, PursuitData::CarCrew& outCrew)
{
    if (!target)
        return ObjectGuid::Empty;

    if (!_vehicleModeAvailable)
        return ObjectGuid::Empty;

    Map* map = target->GetMap();
    if (!map)
        return ObjectGuid::Empty;

    // Calculate spawn position - offset to the side/front so player can see it
    // First car spawns ahead-left, subsequent cars spread in arc
    float baseAngle = target->GetOrientation();  // Player's facing direction
    float angleOffset = (M_PI / 3.0f) + (carIndex * M_PI / 4.0f);  // 60° left + 45° spread per car
    float angle = baseAngle + angleOffset;

    float dist = frand(sGTAPoliceConfig->CarSpawnMinDistance, sGTAPoliceConfig->CarSpawnMaxDistance);

    float x = target->GetPositionX() + std::cos(angle) * dist;
    float y = target->GetPositionY() + std::sin(angle) * dist;
    float z = target->GetPositionZ();
    if (!ResolveSafeGroundZ(map, target, x, y, z))
    {
        LOG_ERROR("module",
            "[mod-gtapolice] Invalid ground at spawn point ({:.2f}, {:.2f}); skipping police car spawn",
            x, y);
        return ObjectGuid::Empty;
    }

    if (!Acore::IsValidMapCoord(x, y, z, angle + M_PI))
    {
        LOG_ERROR("module",
            "[mod-gtapolice] Computed invalid police car position ({:.2f}, {:.2f}, {:.2f}, {:.2f})",
            x, y, z, angle + M_PI);
        return ObjectGuid::Empty;
    }

    // Calculate total duration
    uint32 totalDuration = sGTAPoliceConfig->CarChaseTimeout
                         + sGTAPoliceConfig->CrewLoadingTimeout
                         + sGTAPoliceConfig->TransportFailsafe
                         + 10000;  // Extra buffer

    // Spawn the car
    TempSummon* car = map->SummonCreature(
        sGTAPoliceConfig->CarEntry,
        Position(x, y, z, angle + M_PI),  // Face toward player
        nullptr,
        totalDuration
    );

    if (!car)
    {
        LOG_ERROR("module", "[mod-gtapolice] Failed to spawn police car #{}", carIndex);
        return ObjectGuid::Empty;
    }

    // Spawn crew into this car - populate outCrew so caller has the GUIDs
    outCrew.carGuid = car->GetGUID();
    SpawnCrewIntoVehicle(car, map, outCrew);

    if (sGTAPoliceConfig->DebugLog)
        LOG_INFO("module", "[mod-gtapolice] Spawned police car #{} (guid={}) at angle={:.2f}, dist={:.1f}",
            carIndex, car->GetGUID().GetCounter(), angle, dist);

    return car->GetGUID();
}

bool GTAPoliceManager::StartImmediateArrest(Player* target, Unit* spawnRef, bool gmOverride)
{
    if (!target || !IsPlayerArrestable(target, gmOverride) || !MeetsArrestWantedThreshold(target, gmOverride))
        return false;

    Map* map = target->GetMap();
    if (!map)
        return false;

    // Never turn missing vehicle support into an invisible automatic arrest by default.
    if (!_vehicleModeAvailable)
    {
        if (!sGTAPoliceConfig->AllowDirectBookingWithoutTransport)
        {
            LOG_WARN("module",
                "[mod-gtapolice] Immediate arrest rejected for {}: vehicle transport is unavailable",
                target->GetName());
            return false;
        }

        if (WorldSession* targetSession = target->GetSession())
        {
            ChatHandler(targetSession).SendSysMessage("|cffff0000[LSPD]|r You are under arrest.");
            ChatHandler(targetSession).SendSysMessage("|cffff0000[LSPD]|r Vehicle transport unavailable. Booking directly.");
        }
        sGTAPoliceBridge->EmitPlayerArrested(target, nullptr);
        JailPlayer(target);
        return true;
    }

    Unit* ref = spawnRef ? spawnRef : static_cast<Unit*>(target);
    float angle = ref->GetOrientation();
    float x = ref->GetPositionX() + std::cos(angle) * 6.0f;
    float y = ref->GetPositionY() + std::sin(angle) * 6.0f;
    float z = ref->GetPositionZ();
    if (!ResolveSafeGroundZ(map, ref, x, y, z))
    {
        if (!sGTAPoliceConfig->AllowDirectBookingWithoutTransport)
        {
            LOG_WARN("module",
                "[mod-gtapolice] Immediate arrest rejected for {}: no safe transport spawn position",
                target->GetName());
            return false;
        }

        LOG_WARN("module",
            "[mod-gtapolice] Could not resolve a safe arrest spawn position near ({:.2f}, {:.2f}); jailing directly",
            x, y);
        sGTAPoliceBridge->EmitPlayerArrested(target, nullptr);
        JailPlayer(target);
        return true;
    }

    Position spawnPos(x, y, z, angle);

    // Spawn car briefly for visual effect
    TempSummon* car = map->SummonCreature(
        sGTAPoliceConfig->CarEntry,
        spawnPos,
        nullptr,
        5000  // 5 seconds visual
    );

    if (!car)
    {
        if (!sGTAPoliceConfig->AllowDirectBookingWithoutTransport)
        {
            LOG_WARN("module",
                "[mod-gtapolice] Immediate arrest rejected for {}: transport summon failed",
                target->GetName());
            return false;
        }

        LOG_WARN("module", "[mod-gtapolice] Failed to spawn car for immediate arrest, jailing anyway");
        sGTAPoliceBridge->EmitPlayerArrested(target, nullptr);
        JailPlayer(target);
        return true;
    }

    // Announce arrest to the player
    ChatHandler(target->GetSession()).SendSysMessage("|cffff0000[LSPD]|r You are under arrest! Do not resist.");
    car->Yell("LSPD! Hands where I can see them!", LANG_UNIVERSAL);

    // Force player into prisoner seat briefly
    target->EnterVehicle(car, sGTAPoliceConfig->SeatPrisoner);

    // Emit arrested event
    sGTAPoliceBridge->EmitPlayerArrested(target, car);

    if (sGTAPoliceConfig->DebugLog)
        LOG_INFO("module", "[mod-gtapolice] Immediate arrest for {}", target->GetName());

    // Announce transport after a brief delay, then jail
    ObjectGuid targetGuid = target->GetGUID();
    ObjectGuid carGuid = car->GetGUID();

    // Phase 1: Announce transport (1 second after arrest)
    car->m_Events.AddEventAtOffset([targetGuid, carGuid]() {
        Player* player = ObjectAccessor::FindConnectedPlayer(targetGuid);
        if (player && player->IsInWorld())
        {
            ChatHandler(player->GetSession()).SendSysMessage("|cffff0000[LSPD]|r Transporting suspect to booking...");
        }
    }, 1s);

    // Phase 2: Exit vehicle and jail (3 seconds after arrest)
    car->m_Events.AddEventAtOffset([targetGuid]() {
        Player* player = ObjectAccessor::FindConnectedPlayer(targetGuid);
        if (player && player->IsInWorld())
        {
            // Exit vehicle if still in one
            if (player->GetVehicle())
                player->ExitVehicle();

            sGTAPoliceMgr->JailPlayer(player);
        }
    }, 3s);

    return true;
}

void GTAPoliceManager::EndPursuit(ObjectGuid playerGuid, bool success)
{
    // Step 1: Extract data under lock
    PursuitData pursuitCopy;
    bool found = false;
    {
        std::lock_guard<std::mutex> lock(_mutex);
        auto it = _activePursuits.find(playerGuid);
        if (it == _activePursuits.end())
            return;

        pursuitCopy = it->second;  // Copy
        found = true;

        // Set cooldown before erasing
        _pursuitCooldowns[playerGuid] = getMSTime() + PURSUIT_COOLDOWN_MS;

        // Remove from active pursuits
        _activePursuits.erase(it);
    }
    // Lock released here

    // Step 2: Do slow operations OUTSIDE lock
    if (!found)
        return;

    // If player escaped (not arrested), start evasion timer for star decay
    if (!success)
    {
        Player* player = ObjectAccessor::FindConnectedPlayer(playerGuid);
        if (player && GetPlayerWantedStars(player) > 0)
        {
            StartEvasion(playerGuid);
        }
    }

    // Despawn all crews and cars (ObjectAccessor calls can block)
    Player* refPlayer = ObjectAccessor::FindConnectedPlayer(playerGuid);
    if (refPlayer)
    {
        auto despawnCreature = [refPlayer](ObjectGuid guid) {
            if (guid.IsEmpty())
                return;
            Creature* c = ObjectAccessor::GetCreature(*refPlayer, guid);
            if (c && c->IsAlive())
                c->DespawnOrUnsummon(1000ms);
        };

        // Despawn all crew members from all cars
        for (const auto& crew : pursuitCopy.crews)
        {
            despawnCreature(crew.driverGuid);
            despawnCreature(crew.passengerGuid);
        }

        // Despawn all cars
        for (const auto& carGuid : pursuitCopy.carGuids)
        {
            despawnCreature(carGuid);
        }

        // Emit bridge event
        sGTAPoliceBridge->EmitPursuitEnded(refPlayer,
            success ? PursuitState::JAILED : PursuitState::ESCAPED);
    }
}

bool GTAPoliceManager::IsUnderPursuit(ObjectGuid playerGuid) const
{
    std::lock_guard<std::mutex> lock(_mutex);
    return _activePursuits.find(playerGuid) != _activePursuits.end();
}

bool GTAPoliceManager::IsPursuitGmOverride(ObjectGuid playerGuid) const
{
    std::lock_guard<std::mutex> lock(_mutex);
    auto it = _activePursuits.find(playerGuid);
    if (it != _activePursuits.end())
        return it->second.gmOverride;
    return false;
}

std::optional<PursuitData> GTAPoliceManager::GetPursuitData(ObjectGuid playerGuid) const
{
    std::lock_guard<std::mutex> lock(_mutex);
    auto it = _activePursuits.find(playerGuid);
    if (it != _activePursuits.end())
        return it->second;  // Return copy
    return std::nullopt;
}

uint32 GTAPoliceManager::GetActivePursuitCount() const
{
    std::lock_guard<std::mutex> lock(_mutex);
    return static_cast<uint32>(_activePursuits.size());
}

// ============================================================================
// CREW MANAGEMENT
// ============================================================================

void GTAPoliceManager::SetCrewGuids(ObjectGuid playerGuid, ObjectGuid carGuid, ObjectGuid driverGuid, ObjectGuid passengerGuid)
{
    std::lock_guard<std::mutex> lock(_mutex);
    auto it = _activePursuits.find(playerGuid);
    if (it != _activePursuits.end())
    {
        // Find the crew entry for this car
        for (auto& crew : it->second.crews)
        {
            if (crew.carGuid == carGuid)
            {
                crew.driverGuid = driverGuid;
                crew.passengerGuid = passengerGuid;
                return;
            }
        }
        // If no matching car found, update the first crew entry as fallback
        if (!it->second.crews.empty())
        {
            it->second.crews[0].driverGuid = driverGuid;
            it->second.crews[0].passengerGuid = passengerGuid;
        }
    }
}

// ============================================================================
// WANTED LEVEL INTEGRATION
// ============================================================================

bool GTAPoliceManager::IsPlayerWanted(Player* player) const
{
    if (!player)
        return false;

#ifdef MOD_EC_GUARD_SYSTEM
    if (sGTAPoliceConfig->UseGuardSystemWanted)
    {
        WantedStars stars = sECGuardSystemMgr->GetPlayerWantedStars(player->GetGUID());
        return stars >= static_cast<WantedStars>(sGTAPoliceConfig->MinWantedStarsForPursuit);
    }
#endif

    // Fallback to spell aura
    uint32 wantedSpell = 2479; // Default SPELL_WANTED_AURA
    if (wantedSpell == 0)
        return false;

    return player->HasAura(wantedSpell);
}

uint32 GTAPoliceManager::GetPlayerWantedStars(Player* player) const
{
    if (!player)
        return 0;

#ifdef MOD_EC_GUARD_SYSTEM
    if (sGTAPoliceConfig->UseGuardSystemWanted)
    {
        return static_cast<uint32>(sECGuardSystemMgr->GetPlayerWantedStars(player->GetGUID()));
    }
#endif

    // Fallback: if has wanted aura, return 1 star
    uint32 wantedSpell = 2479;
    if (wantedSpell && player->HasAura(wantedSpell))
        return 1;

    return 0;
}

void GTAPoliceManager::SetPlayerWanted(Player* player, bool wanted)
{
    if (!player)
        return;

#ifdef MOD_EC_GUARD_SYSTEM
    if (sGTAPoliceConfig->UseGuardSystemWanted)
    {
        if (wanted)
            sECGuardSystemMgr->AddCrime(player, CrimeType::CIVILIAN_ATTACK, nullptr);
        else
            sECGuardSystemMgr->ClearWanted(player);
        return;
    }
#endif

    // Fallback to spell aura
    uint32 wantedSpell = 2479;
    if (wantedSpell)
    {
        if (wanted)
            player->CastSpell(player, wantedSpell, true);
        else
            player->RemoveAurasDueToSpell(wantedSpell);
    }
}

void GTAPoliceManager::ClearPlayerWanted(Player* player)
{
    SetPlayerWanted(player, false);
}

void GTAPoliceManager::OnWantedLevelChanged(Player* player, uint8 oldStars, uint8 newStars)
{
    if (!player || !sGTAPoliceConfig->Enabled)
        return;

    if (!sGTAPoliceConfig->AffectPlayerbots && player->GetSession() && player->GetSession()->IsBot())
        return;

    LOG_INFO("module", "[mod-gtapolice] OnWantedLevelChanged: {} stars {} -> {} (zone={}, map={}, isBot={})",
        player->GetName(), oldStars, newStars,
        player->GetZoneId(), player->GetMapId(),
        (player->GetSession() && player->GetSession()->IsBot()) ? "yes" : "no");

    // Record crime time for decay tracking (on any wanted level increase)
    if (newStars > oldStars)
    {
        RecordCrime(player->GetGUID());

        // Add zone heat for this crime
        uint32 zoneId = player->GetZoneId();
        if (zoneId > 0)
        {
            uint8 oldLevel = GetZoneHeatLevel(zoneId);
            AddZoneHeat(zoneId, sGTAPoliceConfig->ZoneHeatPerCrime);
            uint8 newLevel = GetZoneHeatLevel(zoneId);

            LOG_INFO("module", "[mod-gtapolice] Zone heat: zone={} heat {} -> {} (level {} -> {})",
                zoneId, GetZoneHeat(zoneId) - sGTAPoliceConfig->ZoneHeatPerCrime,
                GetZoneHeat(zoneId), oldLevel, newLevel);

            // Emit bridge event if heat level changed
            if (newLevel != oldLevel)
                sGTAPoliceBridge->EmitZoneHeatChanged(zoneId, GetZoneHeat(zoneId), newLevel);
        }
    }

    // Only trigger on wanted level INCREASE
    if (newStars <= oldStars)
        return;

    // Check if new wanted level meets threshold for auto-dispatch
    if (newStars < sGTAPoliceConfig->MinWantedStarsForPursuit)
    {
        LOG_INFO("module", "[mod-gtapolice] Skipping dispatch: {} stars < {} threshold",
            newStars, sGTAPoliceConfig->MinWantedStarsForPursuit);
        return;
    }

    // Already under pursuit?
    if (IsUnderPursuit(player->GetGUID()))
    {
        LOG_INFO("module", "[mod-gtapolice] Skipping dispatch: {} already under pursuit", player->GetName());
        return;
    }

    // Can we start a pursuit?
    if (!CanStartPursuit(player->GetGUID()))
    {
        LOG_INFO("module", "[mod-gtapolice] Skipping dispatch: cannot start pursuit for {}", player->GetName());
        return;
    }

    // Is player arrestable?
    if (!IsPlayerArrestable(player))
    {
        LOG_INFO("module", "[mod-gtapolice] Skipping dispatch: {} not arrestable", player->GetName());
        return;
    }

    LOG_INFO("module", "[mod-gtapolice] Auto-dispatching pursuit for {} (wanted stars: {} -> {}, zone={}, map={})",
        player->GetName(), oldStars, newStars, player->GetZoneId(), player->GetMapId());

    // Start pursuit (no officer, automatic dispatch)
    if (StartPursuit(player, nullptr))
    {
        // Start all cars' chase
        auto data = GetPursuitData(player->GetGUID());
        if (data.has_value())
        {
            for (const auto& carGuid : data->carGuids)
            {
                Creature* car = ObjectAccessor::GetCreature(*player, carGuid);
                if (car)
                {
                    if (auto* carAI = dynamic_cast<npc_gtapolice_car::CarAI*>(car->AI()))
                    {
                        carAI->SetTarget(player->GetGUID());
                        carAI->StartChase();
                    }
                }
            }
        }

        sGTAPoliceBridge->EmitPursuitAutoDispatched(player, newStars);
    }
}

// ============================================================================
// JAIL
// ============================================================================

void GTAPoliceManager::JailPlayer(Player* player)
{
    if (!player)
        return;

    uint32 mapId;
    float x, y, z, o;
    GetJailLocation(player, mapId, x, y, z, o);

    // Apply punishment based on wanted stars
    uint32 stars = GetPlayerWantedStars(player);
    ApplyJailPunishment(player, stars);

    // Announce sentence
    uint32 jailSecs = 0;
    if (stars <= 2) jailSecs = 30;
    else if (stars <= 4) jailSecs = 60;
    else jailSecs = 120;

    ChatHandler(player->GetSession()).PSendSysMessage(
        "|cffff0000[LSPD]|r Booked at the station. Wanted level: {} star{}. Sentence: {} seconds.",
        stars, stars > 1 ? "s" : "", jailSecs);
    ChatHandler(player->GetSession()).SendSysMessage(
        "|cffff0000[LSPD]|r Your wanted level has been cleared.");

    // Clear the old jurisdiction before changing zones. Teleporting first lets the guard callback
    // dispatch the crime faction into the jail destination during the zone-change re-entry window.
    if (sGTAPoliceConfig->ClearWantedOnJail)
        ClearPlayerWanted(player);

    player->TeleportTo(mapId, x, y, z, o);

    // Emit event
    sGTAPoliceBridge->EmitPlayerJailed(player, mapId, x, y, z);

    if (sGTAPoliceConfig->DebugLog)
        LOG_INFO("module", "[mod-gtapolice] Jailed player {} (stars={})", player->GetName(), stars);
}

void GTAPoliceManager::GetJailLocation(Player* player, uint32& mapId, float& x, float& y, float& z, float& o)
{
    // Determine faction: 0=Alliance, 1=Horde, 2=Neutral
    uint8 faction = 2; // Default neutral
    if (player)
    {
        uint8 race = player->getRace();
        // Alliance races: Human(1), Dwarf(3), NightElf(4), Gnome(7), Draenei(11)
        if (race == 1 || race == 3 || race == 4 || race == 7 || race == 11)
            faction = 0;
        // Horde races: Orc(2), Undead(5), Tauren(6), Troll(8), BloodElf(10)
        else if (race == 2 || race == 5 || race == 6 || race == 8 || race == 10)
            faction = 1;
    }

    // Search cached jail locations (prefer faction-specific, fall back to neutral)
    const JailLocation* best = nullptr;
    for (const auto& loc : _jailLocations)
    {
        // Exact faction match
        if (loc.faction == faction)
        {
            best = &loc;
            break;
        }
        // Neutral jail as fallback
        if (loc.faction == 2 && !best)
            best = &loc;
    }

    if (best)
    {
        mapId = best->mapId;
        x = best->x;
        y = best->y;
        z = best->z;
        o = best->o;

        if (sGTAPoliceConfig->DebugLog)
            LOG_INFO("module", "[mod-gtapolice] Found jail location from cache (map={}, faction={})", mapId, best->faction);
        return;
    }

    // Fallback to config values if no cached jail found
    if (sGTAPoliceConfig->DebugLog)
        LOG_INFO("module", "[mod-gtapolice] No jail in cache, using config fallback");

    mapId = sGTAPoliceConfig->JailMap;
    x = sGTAPoliceConfig->JailX;
    y = sGTAPoliceConfig->JailY;
    z = sGTAPoliceConfig->JailZ;
    o = sGTAPoliceConfig->JailO;
}

void GTAPoliceManager::ApplyJailPunishment(Player* player, uint32 wantedStars)
{
    if (!player || wantedStars == 0)
        return;

    // Query ec_jail_punishments for this star level
    QueryResult result = WorldDatabase.Query(
        "SELECT gold_fine, debuff_spell_id, debuff_duration_ms FROM ec_jail_punishments "
        "WHERE stars = {} LIMIT 1",
        wantedStars);

    if (!result)
    {
        if (sGTAPoliceConfig->DebugLog)
            LOG_INFO("module", "[mod-gtapolice] No punishment defined for {} stars", wantedStars);
        return;
    }

    Field* fields = result->Fetch();
    uint32 goldFine = fields[0].Get<uint32>();
    uint32 debuffSpellId = fields[1].Get<uint32>();
    uint32 debuffDurationMs = fields[2].Get<uint32>();

    // Apply gold fine (in copper: goldFine is in copper from table)
    if (goldFine > 0)
    {
        uint32 currentMoney = player->GetMoney();
        if (currentMoney >= goldFine)
            player->ModifyMoney(-static_cast<int32>(goldFine));
        else
            player->SetMoney(0);

        if (sGTAPoliceConfig->DebugLog)
            LOG_INFO("module", "[mod-gtapolice] Fined {} {} copper ({} stars)",
                player->GetName(), goldFine, wantedStars);
    }

    // Apply debuff spell
    if (debuffSpellId > 0)
    {
        player->CastSpell(player, debuffSpellId, true);

        if (sGTAPoliceConfig->DebugLog)
            LOG_INFO("module", "[mod-gtapolice] Applied debuff spell {} to {}",
                debuffSpellId, player->GetName());
    }

    // Track jail sentence for release event
    if (debuffDurationMs > 0)
    {
        JailData jailData;
        jailData.jailedAt = getMSTime();
        jailData.releaseTime = jailData.jailedAt + debuffDurationMs;

        {
            std::lock_guard<std::mutex> lock(_mutex);
            _jailedPlayers[player->GetGUID()] = jailData;
        }

        if (sGTAPoliceConfig->DebugLog)
            LOG_INFO("module", "[mod-gtapolice] Tracking jail sentence for {}, release in {} ms",
                player->GetName(), debuffDurationMs);
    }
}

// ============================================================================
// HELPERS
// ============================================================================

Position GTAPoliceManager::GetOffsetInFront(Unit* unit, float distance)
{
    float o = unit->GetOrientation();
    Position pos;
    pos.m_positionX = unit->GetPositionX() + std::cos(o) * distance;
    pos.m_positionY = unit->GetPositionY() + std::sin(o) * distance;
    pos.m_positionZ = unit->GetPositionZ();
    pos.m_orientation = o;
    return pos;
}

bool GTAPoliceManager::IsPlayerArrestable(Player* player, bool gmOverride /*= false*/)
{
    if (!player)
        return false;
    if (!player->IsAlive())
        return false;
    if (player->IsInFlight())
        return false;
    if (player->IsBeingTeleported())
        return false;
    if (player->GetVehicle())
        return false;
    // Allow GM commands to bypass the GM-mode check for testing
    if (!gmOverride && player->IsGameMaster())
        return false;
    return true;
}

bool GTAPoliceManager::MeetsArrestWantedThreshold(Player* player, bool gmOverride /*= false*/)
{
    if (!player)
        return false;
    if (gmOverride)
        return true;
    return sGTAPoliceMgr->GetPlayerWantedStars(player) >= sGTAPoliceConfig->MinWantedStarsForArrest;
}

// ============================================================================
// REINFORCEMENT SYSTEM
// ============================================================================

void GTAPoliceManager::UpdateReinforcements(uint32 /*diff*/)
{
    uint32 now = getMSTime();

    // Collect players needing reinforcements (can't modify while iterating with lock)
    std::vector<std::pair<ObjectGuid, uint32>> reinforcementsNeeded;

    {
        std::lock_guard<std::mutex> lock(_mutex);
        for (auto& [guid, data] : _activePursuits)
        {
            // Check interval
            if (now - data.lastReinforcementTime < sGTAPoliceConfig->CarReinforceIntervalMs)
                continue;

            // Check if we need more cars
            uint32 targetCars = std::min(sGTAPoliceConfig->CarsPerStar[data.wantedStars],
                                         sGTAPoliceConfig->MaxCarsPerPursuit);

            // Count active cars (alive cars only)
            uint32 activeCars = 0;
            Player* player = ObjectAccessor::FindConnectedPlayer(guid);
            if (player)
            {
                for (const auto& carGuid : data.carGuids)
                {
                    Creature* car = ObjectAccessor::GetCreature(*player, carGuid);
                    if (car && car->IsAlive())
                        ++activeCars;
                }
            }

            if (activeCars < targetCars)
            {
                if (player)
                    LOG_INFO("module", "[mod-gtapolice] Reinforcement needed: {} has {}/{} active cars (zone={}, map={})",
                        player->GetName(), activeCars, targetCars,
                        player->GetZoneId(), player->GetMapId());
                reinforcementsNeeded.push_back({guid, targetCars - activeCars});
                data.lastReinforcementTime = now;
            }
        }
    }

    // Spawn reinforcements outside lock
    for (const auto& [playerGuid, count] : reinforcementsNeeded)
    {
        Player* player = ObjectAccessor::FindConnectedPlayer(playerGuid);
        if (player)
        {
            for (uint32 i = 0; i < count; ++i)
            {
                SpawnReinforcementCar(player);
            }
        }
    }
}

void GTAPoliceManager::SpawnReinforcementCar(Player* target)
{
    if (!target)
        return;

    // Use random offset to prevent all reinforcements spawning at same spot
    uint32 randomOffset = urand(0, 3);
    PursuitData::CarCrew crew;
    ObjectGuid carGuid = SpawnPoliceCar(target, randomOffset, crew);

    if (carGuid.IsEmpty())
    {
        LOG_WARN("module", "[mod-gtapolice] Failed to spawn reinforcement car");
        return;
    }

    // Add to pursuit data
    {
        std::lock_guard<std::mutex> lock(_mutex);
        auto it = _activePursuits.find(target->GetGUID());
        if (it != _activePursuits.end())
        {
            it->second.carGuids.push_back(carGuid);
            it->second.crews.push_back(crew);
        }
    }

    // Emit bridge event
    Creature* car = ObjectAccessor::GetCreature(*target, carGuid);
    if (car)
    {
        sGTAPoliceBridge->EmitReinforcementDispatched(target, car);
    }

    if (sGTAPoliceConfig->DebugLog)
        LOG_INFO("module", "[mod-gtapolice] Spawned reinforcement car for {} (guid={})",
            target->GetName(), carGuid.GetCounter());
}

// ============================================================================
// STAR DECAY SYSTEM
// ============================================================================

void GTAPoliceManager::RecordCrime(ObjectGuid playerGuid)
{
    std::lock_guard<std::mutex> lock(_mutex);
    _decayStates[playerGuid].lastCrimeTime = time(nullptr);
    _decayStates[playerGuid].evasionStartTime = 0; // Reset evasion
}

void GTAPoliceManager::StartEvasion(ObjectGuid playerGuid)
{
    std::lock_guard<std::mutex> lock(_mutex);
    auto& state = _decayStates[playerGuid];
    if (state.evasionStartTime == 0)
    {
        state.evasionStartTime = time(nullptr);
        LOG_DEBUG("module", "[mod-gtapolice] Player {} started evasion", playerGuid.GetCounter());
    }
}

void GTAPoliceManager::UpdateStarDecay(uint32 diff)
{
    if (!sGTAPoliceConfig->StarDecayEnabled)
        return;

    // Collect players to decay (can't modify while iterating with lock held during player ops)
    std::vector<ObjectGuid> playersToDecay;

    {
        std::lock_guard<std::mutex> lock(_mutex);
        time_t now = time(nullptr);

        for (auto& [guid, state] : _decayStates)
        {
            // Skip if in active pursuit
            if (_activePursuits.find(guid) != _activePursuits.end())
            {
                state.evasionStartTime = 0; // Reset evasion while pursued
                state.decayTimer = 0;
                continue;
            }

            // Skip if no evasion started
            if (state.evasionStartTime == 0)
                continue;

            // Check grace period since last crime
            time_t gracePeriodSec = sGTAPoliceConfig->StarDecayGracePeriodMs / 1000;
            if (now - state.lastCrimeTime < gracePeriodSec)
                continue;

            // Check required evasion time
            time_t evasionRequiredSec = sGTAPoliceConfig->StarDecayRequiredEvasionMs / 1000;
            if (now - state.evasionStartTime < evasionRequiredSec)
                continue;

            // Accumulate decay timer
            state.decayTimer += diff;

            // Check if decay interval reached
            if (state.decayTimer >= sGTAPoliceConfig->StarDecayIntervalMs)
            {
                state.decayTimer = 0;
                playersToDecay.push_back(guid);
            }
        }
    }

    // Process decay outside lock
    for (const auto& guid : playersToDecay)
    {
        DecayPlayerStars(guid);
    }
}

void GTAPoliceManager::DecayPlayerStars(ObjectGuid playerGuid)
{
    // Get current wanted level
    Player* player = ObjectAccessor::FindConnectedPlayer(playerGuid);
    if (!player)
    {
        // Clean up decay state if player is gone
        std::lock_guard<std::mutex> lock(_mutex);
        _decayStates.erase(playerGuid);
        return;
    }

    uint32 currentStars = GetPlayerWantedStars(player);
    if (currentStars == 0)
    {
        // Clean up decay state if no longer wanted
        std::lock_guard<std::mutex> lock(_mutex);
        _decayStates.erase(playerGuid);
        return;
    }

    // Decay by 1 star
    uint8 newStars = static_cast<uint8>(currentStars - 1);

#ifdef MOD_EC_GUARD_SYSTEM
    if (sGTAPoliceConfig->UseGuardSystemWanted)
    {
        // Guard system manages wanted via crime points - just clear if reached 0
        if (newStars == 0)
        {
            sECGuardSystemMgr->ClearWanted(player);
        }
        // Note: partial decay not supported when using guard system
        // Stars are recalculated from crime points by the guard system
    }
    else
#endif
    {
        // Fallback: clear aura if reached 0
        if (newStars == 0)
        {
            SetPlayerWanted(player, false);
        }
    }

    // Emit bridge event
    sGTAPoliceBridge->EmitStarDecay(player, static_cast<uint8>(currentStars), newStars);

    // Notify player
    ChatHandler(player->GetSession()).PSendSysMessage(
        "|cFF00FF00[Police]|r Your wanted level has decreased to {} stars.", newStars);

    if (sGTAPoliceConfig->DebugLog)
        LOG_INFO("module", "[mod-gtapolice] Player {} wanted stars decayed: {} -> {}",
            playerGuid.GetCounter(), currentStars, newStars);

    // If fully cleared, clean up
    if (newStars == 0)
    {
        std::lock_guard<std::mutex> lock(_mutex);
        _decayStates.erase(playerGuid);
    }
}

// ============================================================================
// BRIBE SYSTEM
// ============================================================================

bool GTAPoliceManager::CanBribe(Player* player) const
{
    if (!player || !sGTAPoliceConfig->BribeEnabled)
        return false;

    uint32 stars = GetPlayerWantedStars(player);
    if (stars == 0)
        return false;

    // Can't bribe if stars too high
    if (stars > sGTAPoliceConfig->BribeMaxStars)
        return false;

    // Can't bribe during active pursuit
    if (IsUnderPursuit(player->GetGUID()))
        return false;

    // Cooldown check
    {
        std::lock_guard<std::mutex> lock(_mutex);
        auto it = _bribeCooldowns.find(player->GetGUID());
        if (it != _bribeCooldowns.end() && getMSTime() < it->second)
            return false;
    }

    // Check if player can afford it
    uint32 cost = GetBribeCost(player);
    return player->GetMoney() >= cost;
}

uint32 GTAPoliceManager::GetBribeCost(Player* player) const
{
    if (!player)
        return 0;

    uint32 stars = GetPlayerWantedStars(player);
    return stars * sGTAPoliceConfig->BribeCostPerStar;
}

bool GTAPoliceManager::AttemptBribe(Player* player)
{
    if (!CanBribe(player))
        return false;

    uint32 cost = GetBribeCost(player);
    uint32 oldStars = GetPlayerWantedStars(player);

    // Take the money
    player->ModifyMoney(-static_cast<int32>(cost));

    // Clear wanted level
    ClearPlayerWanted(player);

    // Set bribe cooldown
    {
        std::lock_guard<std::mutex> lock(_mutex);
        _bribeCooldowns[player->GetGUID()] = getMSTime() + sGTAPoliceConfig->BribeCooldownMs;
    }

    // Clean up decay state
    {
        std::lock_guard<std::mutex> lock(_mutex);
        _decayStates.erase(player->GetGUID());
    }

    // Emit bridge event
    sGTAPoliceBridge->EmitBribe(player, oldStars, cost);

    // Notify player
    uint32 gold = cost / 10000;
    ChatHandler(player->GetSession()).PSendSysMessage(
        "|cFF00FF00[Police]|r You bribed the authorities for {} gold. Wanted level cleared.", gold);

    if (sGTAPoliceConfig->DebugLog)
        LOG_INFO("module", "[mod-gtapolice] Player {} bribed for {} copper ({} stars)",
            player->GetName(), cost, oldStars);

    return true;
}

// ============================================================================
// ZONE HEAT SYSTEM
// ============================================================================

void GTAPoliceManager::AddZoneHeat(uint32 zoneId, uint32 amount)
{
    if (!sGTAPoliceConfig->ZoneHeatEnabled || zoneId == 0)
        return;

    std::lock_guard<std::mutex> lock(_mutex);
    _zoneHeat[zoneId] += amount;

    // Cap at 100
    if (_zoneHeat[zoneId] > 100)
        _zoneHeat[zoneId] = 100;
}

void GTAPoliceManager::ReduceZoneHeat(uint32 zoneId, uint32 amount)
{
    if (!sGTAPoliceConfig->ZoneHeatEnabled || zoneId == 0)
        return;

    std::lock_guard<std::mutex> lock(_mutex);
    auto it = _zoneHeat.find(zoneId);
    if (it == _zoneHeat.end() || it->second == 0)
        return;

    if (it->second <= amount)
        _zoneHeat.erase(it);
    else
        it->second -= amount;
}

uint32 GTAPoliceManager::GetZoneHeat(uint32 zoneId) const
{
    if (!sGTAPoliceConfig->ZoneHeatEnabled)
        return 0;

    std::lock_guard<std::mutex> lock(_mutex);
    auto it = _zoneHeat.find(zoneId);
    return (it != _zoneHeat.end()) ? it->second : 0;
}

uint8 GTAPoliceManager::GetZoneHeatLevel(uint32 zoneId) const
{
    uint32 heat = GetZoneHeat(zoneId);
    if (heat >= sGTAPoliceConfig->ZoneHeatThresholdHigh)
        return 2;  // Hot
    if (heat >= sGTAPoliceConfig->ZoneHeatThresholdLow)
        return 1;  // Warm
    return 0;      // Cold
}

void GTAPoliceManager::UpdateZoneHeat(uint32 diff)
{
    if (!sGTAPoliceConfig->ZoneHeatEnabled)
        return;

    _zoneHeatDecayTimer += diff;
    if (_zoneHeatDecayTimer < sGTAPoliceConfig->ZoneHeatDecayIntervalMs)
        return;

    _zoneHeatDecayTimer = 0;

    std::lock_guard<std::mutex> lock(_mutex);
    for (auto it = _zoneHeat.begin(); it != _zoneHeat.end(); )
    {
        if (it->second <= sGTAPoliceConfig->ZoneHeatDecayAmount)
        {
            it = _zoneHeat.erase(it);
        }
        else
        {
            it->second -= sGTAPoliceConfig->ZoneHeatDecayAmount;
            ++it;
        }
    }
}

// ============================================================================
// JAIL RELEASE TRACKING
// ============================================================================

void GTAPoliceManager::CheckJailReleases()
{
    uint32 now = getMSTime();
    std::vector<ObjectGuid> toRelease;

    // Collect released players under lock
    {
        std::lock_guard<std::mutex> lock(_mutex);
        for (auto it = _jailedPlayers.begin(); it != _jailedPlayers.end(); )
        {
            if (now >= it->second.releaseTime)
            {
                toRelease.push_back(it->first);
                it = _jailedPlayers.erase(it);
            }
            else
            {
                ++it;
            }
        }
    }

    // Emit release events outside lock
    for (const auto& guid : toRelease)
    {
        Player* player = ObjectAccessor::FindConnectedPlayer(guid);
        if (player)
        {
            sGTAPoliceBridge->EmitPlayerReleased(player);

            // Notify player
            ChatHandler(player->GetSession()).PSendSysMessage(
                "|cFF00FF00[Police]|r You have served your time. You are free to go.");

            if (sGTAPoliceConfig->DebugLog)
                LOG_INFO("module", "[mod-gtapolice] Released player {} from jail",
                    player->GetName());
        }
    }
}
