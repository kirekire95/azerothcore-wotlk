/*
 * mod-gtapolice
 * Police Car AI implementation
 */

#include "GTAPoliceCarAI.h"
#include "GTAPoliceHelpers.h"
#include "GTAPoliceConfig.h"
#include "GTAPoliceBridge.h"
#include "GTAPoliceManager.h"
#include "GTAPoliceCrewAI.h"

#include "Player.h"
#include "Chat.h"
#include "Creature.h"
#include "MotionMaster.h"
#include "Vehicle.h"
#include "ObjectAccessor.h"
#include "ObjectMgr.h"
#include "Log.h"

// ============================================================================
// CONSTRUCTOR
// ============================================================================

npc_gtapolice_car::npc_gtapolice_car() : CreatureScript("npc_gtapolice_car") {}

npc_gtapolice_car::CarAI::CarAI(Creature* creature) : ScriptedAI(creature),
    _state(STATE_IDLE),
    _chaseElapsedMs(0),
    _lastUpdateEmitMs(0),
    _lastTargetX(0), _lastTargetY(0), _lastTargetZ(0),
    _hasLastTargetPos(false),
    _crewArrestComplete(false),
    _driverReboarded(false),
    _transportX(0), _transportY(0), _transportZ(0)
{
}

CreatureAI* npc_gtapolice_car::GetAI(Creature* creature) const
{
    return new CarAI(creature);
}

// ============================================================================
// PUBLIC INTERFACE
// ============================================================================

void npc_gtapolice_car::CarAI::SetTarget(ObjectGuid targetGuid) { _targetGuid = targetGuid; }
void npc_gtapolice_car::CarAI::SetPrisoner(ObjectGuid prisonerGuid) { _prisonerGuid = prisonerGuid; }

Player* npc_gtapolice_car::CarAI::GetTarget()
{
    return _targetGuid.IsEmpty() ? nullptr : ObjectAccessor::FindConnectedPlayer(_targetGuid);
}

Player* npc_gtapolice_car::CarAI::GetPrisoner()
{
    return _prisonerGuid.IsEmpty() ? nullptr : ObjectAccessor::FindConnectedPlayer(_prisonerGuid);
}

void npc_gtapolice_car::CarAI::NotifyCrewArrestComplete()
{
    _crewArrestComplete = true;
}

void npc_gtapolice_car::CarAI::NotifyDriverReboarded()
{
    _driverReboarded = true;
}

// ============================================================================
// AI OVERRIDES
// ============================================================================

void npc_gtapolice_car::CarAI::Reset()
{
    _events.Reset();
    _targetGuid.Clear();
    _prisonerGuid.Clear();
    _state = STATE_IDLE;
    _chaseElapsedMs = 0;
    _hasLastTargetPos = false;
    _crewArrestComplete = false;
    _driverReboarded = false;
    GTAPolice::ApplySiren(me, false);

    if (sGTAPoliceConfig->UseCustomCarModel)
    {
        uint32 displayId = sGTAPoliceConfig->CustomCarDisplayId;

        if (!displayId || !sObjectMgr->GetCreatureModelInfo(displayId))
        {
            LOG_WARN("module",
                "[mod-gtapolice] Custom car display {} unavailable, trying fallback {}",
                displayId, sGTAPoliceConfig->FallbackCarDisplayId);
            displayId = sGTAPoliceConfig->FallbackCarDisplayId;
        }

        if (displayId && sObjectMgr->GetCreatureModelInfo(displayId))
        {
            me->SetNativeDisplayId(displayId);
            me->SetDisplayId(displayId);

            if (sGTAPoliceConfig->DebugLog)
                LOG_INFO("module", "[mod-gtapolice] Applied car display {}", displayId);
        }
        else
        {
            LOG_WARN("module",
                "[mod-gtapolice] No valid display id for police car (custom={}, fallback={}), using template default",
                sGTAPoliceConfig->CustomCarDisplayId, sGTAPoliceConfig->FallbackCarDisplayId);
        }
    }
}

void npc_gtapolice_car::CarAI::UpdateAI(uint32 diff)
{
    if (!sGTAPoliceConfig->Enabled)
        return;

    _events.Update(diff);

    while (uint32 eventId = _events.ExecuteEvent())
    {
        switch (eventId)
        {
            case EVENT_CHECK:
                if (_state == STATE_CHASE)
                    UpdateChase(diff);
                else if (_state == STATE_APPROACH)
                    UpdateApproach(diff);
                else if (_state == STATE_LOADING)
                    UpdateLoading(diff);
                else if (_state == STATE_TRANSPORT)
                    UpdateTransport();

                if (_state == STATE_CHASE)
                    _events.ScheduleEvent(EVENT_CHECK, 250ms);
                else if (_state != STATE_IDLE)
                    _events.ScheduleEvent(EVENT_CHECK, 500ms);
                break;

            case EVENT_TRANSPORT_FAILSAFE:
                if (sGTAPoliceConfig->DebugLog)
                    LOG_INFO("module", "[mod-gtapolice] Transport failsafe triggered; releasing without booking");
                if (Player* prisoner = GetPrisoner())
                    ChatHandler(prisoner->GetSession()).SendSysMessage(
                        "|cffff0000[LSPD]|r Transport failed. You have been released without booking.");
                ReleaseAndJail(false);
                return;

            case EVENT_LOADING_TIMEOUT:
                if (sGTAPoliceConfig->DebugLog)
                    LOG_INFO("module", "[mod-gtapolice] Loading timeout - forcing transport");
                // Driver didn't make it back, force transport anyway
                StartTransport();
                return;

            case EVENT_RADIO_CHATTER:
            {
                uint8 phase = 0;
                if (_state == STATE_CHASE)       phase = 0;
                else if (_state == STATE_APPROACH) phase = 1;
                else if (_state == STATE_TRANSPORT) phase = 3;
                else break;

                GTAPolice::DoRadioChatter(me, phase);

                // Reschedule
                _events.ScheduleEvent(EVENT_RADIO_CHATTER,
                    Milliseconds(sGTAPoliceConfig->RadioChatterIntervalMs));
                break;
            }
        }
    }
}

void npc_gtapolice_car::CarAI::JustDied(Unit* /*killer*/)
{
    if (sGTAPoliceConfig->DebugLog)
        LOG_INFO("module", "[mod-gtapolice] Police car destroyed during pursuit");

    // Release prisoner if one is in the car
    Player* prisoner = GetPrisoner();
    if (prisoner)
    {
        if (prisoner->GetVehicle())
            prisoner->ExitVehicle();

        GTAPolice::ClearArrestCC(prisoner);
    }

    // End pursuit with failure (car destroyed = escape)
    if (!_targetGuid.IsEmpty())
    {
        Player* target = GetTarget();
        if (target)
            sGTAPoliceBridge->EmitPursuitEnded(target, PursuitState::ESCAPED);

        sGTAPoliceMgr->EndPursuit(_targetGuid, false);
    }

    // Despawn crew officers (they're orphaned now)
    auto data = sGTAPoliceMgr->GetPursuitData(_targetGuid);
    if (data.has_value())
    {
        auto despawnCreature = [this](ObjectGuid guid) {
            if (guid.IsEmpty())
                return;
            Creature* c = ObjectAccessor::GetCreature(*me, guid);
            if (c && c->IsAlive())
                c->DespawnOrUnsummon(1000ms);
        };

        const auto* crew = data->FindCrew(me->GetGUID());
        if (crew)
        {
            despawnCreature(crew->driverGuid);
            despawnCreature(crew->passengerGuid);
        }
    }

    GTAPolice::ApplySiren(me, false);
}

// ============================================================================
// STATE TRANSITIONS
// ============================================================================

void npc_gtapolice_car::CarAI::StartChase()
{
    _state = STATE_CHASE;
    _events.Reset();
    _events.ScheduleEvent(EVENT_CHECK, 250ms);
    GTAPolice::ApplySiren(me, true);

    // Schedule radio chatter
    if (sGTAPoliceConfig->RadioChatterEnabled)
        _events.ScheduleEvent(EVENT_RADIO_CHATTER, 2000ms);  // First line after 2s

    sGTAPoliceBridge->EmitPursuitStateChanged(GetTarget(), PursuitState::CHASE);

    if (sGTAPoliceConfig->DebugLog)
        LOG_INFO("module", "[mod-gtapolice] Car started CHASE");
}

void npc_gtapolice_car::CarAI::StartApproach()
{
    _state = STATE_APPROACH;
    _crewArrestComplete = false;

    // Stop the car
    me->GetMotionMaster()->Clear();
    me->StopMoving();

    sGTAPoliceBridge->EmitPursuitStateChanged(GetTarget(), PursuitState::CREW_EXITING);

    // Command crew to exit
    TryCommandCrewExit();

    if (sGTAPoliceConfig->DebugLog)
        LOG_INFO("module", "[mod-gtapolice] Car started APPROACH - crew exiting");
}

void npc_gtapolice_car::CarAI::StartLoading()
{
    _state = STATE_LOADING;
    _driverReboarded = false;
    _events.ScheduleEvent(EVENT_LOADING_TIMEOUT, Milliseconds(sGTAPoliceConfig->CrewLoadingTimeout));

    sGTAPoliceBridge->EmitPursuitStateChanged(GetTarget(), PursuitState::CREW_LOADING);

    // Command driver to return to car
    TryCommandCrewReturn();

    if (sGTAPoliceConfig->DebugLog)
        LOG_INFO("module", "[mod-gtapolice] Car started LOADING - driver returning");
}

void npc_gtapolice_car::CarAI::StartTransport()
{
    _state = STATE_TRANSPORT;
    _events.Reset();

    // Despawn non-driver crew
    DespawnNonDriverCrew();

    // Resolve jail location from DB for the prisoner's faction
    // Falls back to config values if DB has no match or prisoner is on a different map
    Player* prisoner = GetPrisoner();
    uint32 jailMapId = sGTAPoliceConfig->JailMap;
    float jailO = sGTAPoliceConfig->JailO;
    _transportX = sGTAPoliceConfig->JailX;
    _transportY = sGTAPoliceConfig->JailY;
    _transportZ = sGTAPoliceConfig->JailZ;

    if (prisoner)
    {
        uint32 dbMapId = jailMapId;
        float dbX = _transportX, dbY = _transportY, dbZ = _transportZ;
        sGTAPoliceMgr->GetJailLocation(prisoner, dbMapId, dbX, dbY, dbZ, jailO);

        // Only drive to DB jail if it's on the same map as the car
        if (dbMapId == me->GetMapId())
        {
            _transportX = dbX;
            _transportY = dbY;
            _transportZ = dbZ;
        }
    }

    me->GetMotionMaster()->Clear();
    me->GetMotionMaster()->MovePoint(1, _transportX, _transportY, _transportZ);

    _events.ScheduleEvent(EVENT_CHECK, 500ms);
    _events.ScheduleEvent(EVENT_TRANSPORT_FAILSAFE, Milliseconds(sGTAPoliceConfig->TransportFailsafe));

    sGTAPoliceBridge->EmitPursuitStateChanged(GetPrisoner(), PursuitState::TRANSPORTING);

    if (sGTAPoliceConfig->DebugLog)
        LOG_INFO("module", "[mod-gtapolice] Car started TRANSPORT to ({:.1f}, {:.1f}, {:.1f})",
            _transportX, _transportY, _transportZ);
}

void npc_gtapolice_car::CarAI::ReleaseAndJail(bool teleport)
{
    Player* prisoner = GetPrisoner();
    if (prisoner)
    {
        if (prisoner->GetVehicle())
            prisoner->ExitVehicle();

        GTAPolice::ClearArrestCC(prisoner);

        if (teleport)
            sGTAPoliceMgr->JailPlayer(prisoner);
    }

    // End pursuit tracking
    if (!_targetGuid.IsEmpty())
        sGTAPoliceMgr->EndPursuit(_targetGuid, teleport);

    // Despawn all crew
    DespawnNonDriverCrew();

    // Despawn driver
    auto data = sGTAPoliceMgr->GetPursuitData(_targetGuid);
    if (data.has_value())
    {
        const auto* crew = data->FindCrew(me->GetGUID());
        if (crew)
        {
            Creature* driver = ObjectAccessor::GetCreature(*me, crew->driverGuid);
            if (driver)
                driver->DespawnOrUnsummon(1000ms);
        }
    }

    GTAPolice::ApplySiren(me, false);
    me->DespawnOrUnsummon(1000ms);
}

// ============================================================================
// CREW COORDINATION
// ============================================================================

bool npc_gtapolice_car::CarAI::TryCommandCrewExit()
{
    auto data = sGTAPoliceMgr->GetPursuitData(_targetGuid);
    if (!data.has_value())
        return false;

    Player* target = GetTarget();
    if (!target)
        return false;

    auto commandExit = [&](ObjectGuid crewGuid, bool isDriver, uint32 delay)
    {
        if (crewGuid.IsEmpty()) return;
        Creature* crew = ObjectAccessor::GetCreature(*me, crewGuid);
        if (!crew || !crew->IsAlive())
            return;

        auto* crewAI = dynamic_cast<npc_gtapolice_crew::CrewAI*>(crew->AI());
        if (crewAI)
            crewAI->CommandExit(target->GetGUID(), me->GetGUID(), isDriver, delay);
    };

    const auto* myCrew = data->FindCrew(me->GetGUID());
    if (myCrew)
    {
        commandExit(myCrew->driverGuid, true, 0);
        commandExit(myCrew->passengerGuid, false, sGTAPoliceConfig->CrewExitDelay);
    }

    return true;
}

bool npc_gtapolice_car::CarAI::TryCommandCrewReturn()
{
    auto data = sGTAPoliceMgr->GetPursuitData(_targetGuid);
    if (!data.has_value())
        return false;

    const auto* myCrew = data->FindCrew(me->GetGUID());
    if (!myCrew)
        return false;

    // Only the driver returns
    Creature* driver = ObjectAccessor::GetCreature(*me, myCrew->driverGuid);
    if (!driver || !driver->IsAlive())
        return false;

    auto* crewAI = dynamic_cast<npc_gtapolice_crew::CrewAI*>(driver->AI());
    if (crewAI)
    {
        crewAI->CommandReturn();
        return true;
    }

    return false;
}

void npc_gtapolice_car::CarAI::DespawnNonDriverCrew()
{
    if (!sGTAPoliceConfig->CrewDespawnNonDriver)
        return;

    auto data = sGTAPoliceMgr->GetPursuitData(_targetGuid);
    if (!data.has_value())
        return;

    const auto* myCrew = data->FindCrew(me->GetGUID());
    if (!myCrew)
        return;

    Creature* passenger = ObjectAccessor::GetCreature(*me, myCrew->passengerGuid);
    if (passenger)
        passenger->DespawnOrUnsummon(1000ms);
}

// ============================================================================
// STATE UPDATES
// ============================================================================

void npc_gtapolice_car::CarAI::UpdateChase(uint32 diff)
{
    _chaseElapsedMs += diff;
    _lastUpdateEmitMs += diff;

    if (_chaseElapsedMs > sGTAPoliceConfig->CarChaseTimeout)
    {
        if (sGTAPoliceConfig->DebugLog)
            LOG_INFO("module", "[mod-gtapolice] Chase timeout");
        sGTAPoliceBridge->EmitPursuitEnded(GetTarget(), PursuitState::TIMEOUT);
        sGTAPoliceMgr->EndPursuit(_targetGuid, false);
        GTAPolice::ApplySiren(me, false);
        me->DespawnOrUnsummon(1000ms);
        return;
    }

    Player* target = GetTarget();
    if (!target || !target->IsAlive() || target->IsBeingTeleported())
    {
        sGTAPoliceMgr->EndPursuit(_targetGuid, false);
        GTAPolice::ApplySiren(me, false);
        me->DespawnOrUnsummon(1000ms);
        return;
    }

    // Check if target is no longer wanted (skip for GM-initiated pursuits)
    if (!sGTAPoliceMgr->IsPursuitGmOverride(_targetGuid) && !sGTAPoliceMgr->IsPlayerWanted(target))
    {
        if (sGTAPoliceConfig->DebugLog)
            LOG_INFO("module", "[mod-gtapolice] Target no longer wanted, ending chase");
        sGTAPoliceBridge->EmitPursuitEnded(target, PursuitState::ESCAPED);
        sGTAPoliceMgr->EndPursuit(_targetGuid, false);
        GTAPolice::ApplySiren(me, false);
        me->DespawnOrUnsummon(1000ms);
        return;
    }

    // Check escape distance
    float dist = me->GetDistance(target);
    if (dist > sGTAPoliceConfig->CarEscapeDistance)
    {
        if (sGTAPoliceConfig->DebugLog)
            LOG_INFO("module", "[mod-gtapolice] Target escaped (dist={:.1f})", dist);
        sGTAPoliceBridge->EmitPursuitEnded(target, PursuitState::ESCAPED);
        sGTAPoliceMgr->EndPursuit(_targetGuid, false);
        GTAPolice::ApplySiren(me, false);
        me->DespawnOrUnsummon(1000ms);
        return;
    }

    // A pursuit may begin below the arrest threshold, but the crew may not arrest yet.
    bool gmOverride = sGTAPoliceMgr->IsPursuitGmOverride(_targetGuid);
    if (dist <= sGTAPoliceConfig->CrewExitRange &&
        GTAPoliceManager::MeetsArrestWantedThreshold(target, gmOverride))
    {
        StartApproach();
        return;
    }

    // Update chase path if target moved
    float tx = target->GetPositionX();
    float ty = target->GetPositionY();
    float tz = target->GetPositionZ();

    bool shouldRepath = true;
    if (_hasLastTargetPos)
    {
        float moved = GTAPolice::Dist3(tx, ty, tz, _lastTargetX, _lastTargetY, _lastTargetZ);
        shouldRepath = (moved >= sGTAPoliceConfig->CarChaseRepathDistance);
    }

    if (shouldRepath)
    {
        _lastTargetX = tx; _lastTargetY = ty; _lastTargetZ = tz;
        _hasLastTargetPos = true;
        me->GetMotionMaster()->MoveChase(target);
    }

    // Emit periodic pursuit update (every 2 seconds)
    constexpr uint32 PURSUIT_UPDATE_INTERVAL_MS = 2000;
    if (_lastUpdateEmitMs >= PURSUIT_UPDATE_INTERVAL_MS)
    {
        _lastUpdateEmitMs = 0;
        sGTAPoliceBridge->EmitPursuitUpdate(target, me, dist, me->GetSpeed(MOVE_RUN), _chaseElapsedMs);
    }
}

void npc_gtapolice_car::CarAI::UpdateApproach(uint32 diff)
{
    (void)diff;

    Player* target = GetTarget();

    // Death is not an arrest and must never trigger a jail teleport.
    if (!target || !target->IsAlive())
    {
        sGTAPoliceMgr->EndPursuit(_targetGuid, false);
        GTAPolice::ApplySiren(me, false);
        me->DespawnOrUnsummon(1000ms);
        return;
    }

    // Player disconnected
    if (target->IsBeingTeleported())
    {
        sGTAPoliceMgr->EndPursuit(_targetGuid, false);
        GTAPolice::ApplySiren(me, false);
        me->DespawnOrUnsummon(1000ms);
        return;
    }

    // Player escaped while crew approaching - re-engage (crew re-boards, resume chase)
    float dist = me->GetDistance(target);
    if (dist > sGTAPoliceConfig->CrewEscapeRange)
    {
        if (sGTAPoliceConfig->DebugLog)
            LOG_INFO("module", "[mod-gtapolice] Target escaped during approach (dist={:.1f}) - re-engaging", dist);

        // Command all crew to return to car
        auto data = sGTAPoliceMgr->GetPursuitData(_targetGuid);
        if (data.has_value())
        {
            auto commandReturn = [&](ObjectGuid crewGuid) {
                if (crewGuid.IsEmpty()) return;
                Creature* crew = ObjectAccessor::GetCreature(*me, crewGuid);
                if (!crew || !crew->IsAlive()) return;
                auto* crewAI = dynamic_cast<npc_gtapolice_crew::CrewAI*>(crew->AI());
                if (crewAI)
                    crewAI->CommandReturn();
            };
            const auto* myCrew = data->FindCrew(me->GetGUID());
            if (myCrew)
            {
                commandReturn(myCrew->driverGuid);
                commandReturn(myCrew->passengerGuid);
            }
        }

        // Transition to LOADING state (waiting for crew to re-board)
        _state = STATE_LOADING;
        _driverReboarded = false;
        _crewArrestComplete = false;
        _events.ScheduleEvent(EVENT_LOADING_TIMEOUT, Milliseconds(sGTAPoliceConfig->CrewLoadingTimeout));

        sGTAPoliceBridge->EmitPursuitStateChanged(target, PursuitState::CREW_LOADING);
        return;
    }

    // Check if crew completed arrest
    if (_crewArrestComplete)
    {
        StartLoading();
        return;
    }

    // Check if both crew officers died
    auto data2 = sGTAPoliceMgr->GetPursuitData(_targetGuid);
    if (data2.has_value())
    {
        const auto* myCrew2 = data2->FindCrew(me->GetGUID());
        if (myCrew2)
        {
            Creature* driver = ObjectAccessor::GetCreature(*me, myCrew2->driverGuid);
            Creature* passenger = ObjectAccessor::GetCreature(*me, myCrew2->passengerGuid);

            bool driverAlive = driver && driver->IsAlive();
            bool passengerAlive = passenger && passenger->IsAlive();

            if (!driverAlive && !passengerAlive)
            {
                if (sGTAPoliceConfig->DebugLog)
                    LOG_INFO("module", "[mod-gtapolice] Both crew officers dead - ending pursuit");
                sGTAPoliceBridge->EmitPursuitEnded(target, PursuitState::ESCAPED);
                sGTAPoliceMgr->EndPursuit(_targetGuid, false);
                GTAPolice::ApplySiren(me, false);
                me->DespawnOrUnsummon(1000ms);
                return;
            }
        }
    }
}

void npc_gtapolice_car::CarAI::UpdateLoading(uint32 diff)
{
    (void)diff;

    if (_driverReboarded)
    {
        // If arrest was completed, transport to jail
        // If crew returned because player escaped, resume chase
        if (_crewArrestComplete)
        {
            StartTransport();
        }
        else
        {
            if (sGTAPoliceConfig->DebugLog)
                LOG_INFO("module", "[mod-gtapolice] Crew reboarded after escape - resuming CHASE");
            StartChase();
        }
        return;
    }
}

void npc_gtapolice_car::CarAI::UpdateTransport()
{
    float dist = GTAPolice::Dist3(
        me->GetPositionX(), me->GetPositionY(), me->GetPositionZ(),
        _transportX, _transportY, _transportZ);

    if (dist <= sGTAPoliceConfig->ArrivalDistance)
    {
        if (sGTAPoliceConfig->DebugLog)
            LOG_INFO("module", "[mod-gtapolice] Arrived at jail");
        ReleaseAndJail(true);
    }
}
