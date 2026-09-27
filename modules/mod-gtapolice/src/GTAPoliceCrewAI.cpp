/*
 * mod-gtapolice
 * Crew Officer AI implementation
 *
 * Lifecycle: spawned → enter car → ride → exit → approach → arrest → (driver: return + board) → done
 */

#include "GTAPoliceCrewAI.h"
#include "GTAPoliceCarAI.h"
#include "GTAPoliceHelpers.h"
#include "GTAPoliceConfig.h"
#include "GTAPoliceBridge.h"
#include "GTAPoliceManager.h"

#include "Player.h"
#include "Creature.h"
#include "MotionMaster.h"
#include "Vehicle.h"
#include "ObjectAccessor.h"
#include "Log.h"

// ============================================================================
// CONSTRUCTOR
// ============================================================================

npc_gtapolice_crew::npc_gtapolice_crew() : CreatureScript("npc_gtapolice_crew") {}

npc_gtapolice_crew::CrewAI::CrewAI(Creature* creature) : ScriptedAI(creature),
    _state(CREW_RIDING),
    _isDriver(false),
    _hasAppliedStun(false),
    _hasAppliedRoot(false),
    _arrestSequenceStarted(false)
{
}

CreatureAI* npc_gtapolice_crew::GetAI(Creature* creature) const
{
    return new CrewAI(creature);
}

// ============================================================================
// HELPERS
// ============================================================================

Player* npc_gtapolice_crew::CrewAI::GetTarget()
{
    return _targetGuid.IsEmpty() ? nullptr : ObjectAccessor::FindConnectedPlayer(_targetGuid);
}

Creature* npc_gtapolice_crew::CrewAI::GetCar()
{
    return _carGuid.IsEmpty() ? nullptr : ObjectAccessor::GetCreature(*me, _carGuid);
}

// ============================================================================
// AI OVERRIDES
// ============================================================================

void npc_gtapolice_crew::CrewAI::Reset()
{
    _events.Reset();
    _targetGuid.Clear();
    _carGuid.Clear();
    _state = CREW_RIDING;
    _isDriver = false;
    _hasAppliedStun = false;
    _hasAppliedRoot = false;
    _arrestSequenceStarted = false;
}

void npc_gtapolice_crew::CrewAI::JustDied(Unit* /*killer*/)
{
    _state = CREW_DEAD;
    sGTAPoliceBridge->EmitCrewOfficerDied(me, GetTarget());

    if (sGTAPoliceConfig->DebugLog)
        LOG_INFO("module", "[mod-gtapolice] Crew officer died (driver={})", _isDriver ? "yes" : "no");
}

void npc_gtapolice_crew::CrewAI::UpdateAI(uint32 diff)
{
    if (!sGTAPoliceConfig->Enabled || _state == CREW_RIDING || _state == CREW_DONE || _state == CREW_DEAD)
        return;

    _events.Update(diff);

    while (uint32 eventId = _events.ExecuteEvent())
    {
        switch (eventId)
        {
            case EVENT_TICK:
                switch (_state)
                {
                    case CREW_EXITING:    UpdateExiting(); break;
                    case CREW_APPROACHING: UpdateApproaching(); break;
                    case CREW_ARRESTING:  UpdateArresting(); break;
                    case CREW_RETURNING:  UpdateReturning(); break;
                    case CREW_BOARDING:   UpdateBoarding(); break;
                    default: break;
                }

                // Keep ticking if still active
                if (_state != CREW_RIDING && _state != CREW_DONE && _state != CREW_DEAD)
                    _events.ScheduleEvent(EVENT_TICK, 250ms);
                break;

            case EVENT_EXIT_DELAY:
                DoExit();
                break;

            case EVENT_ARREST_STEP_2:
            {
                Player* target = GetTarget();
                if (target)
                {
                    // Force target to do hands up emote
                    target->HandleEmoteCommand(16);  // EMOTE_BEG (hands up)

                    // Officer approaches
                    me->GetMotionMaster()->MovePoint(POINT_ARREST,
                        target->GetPositionX(),
                        target->GetPositionY(),
                        target->GetPositionZ());
                }
                _events.ScheduleEvent(EVENT_ARREST_STEP_3, 2000ms);
                break;
            }

            case EVENT_ARREST_STEP_3:
            {
                Player* target = GetTarget();
                if (target)
                {
                    // Officer does arrest motion (point)
                    me->HandleEmoteCommand(25);  // EMOTE_POINT
                    me->Say("You're under arrest!", LANG_UNIVERSAL);

                    // Target kneels
                    target->HandleEmoteCommand(68);  // EMOTE_KNEEL
                }
                _events.ScheduleEvent(EVENT_ARREST_COMPLETE, 2000ms);
                break;
            }

            case EVENT_ARREST_COMPLETE:
            {
                // Now apply the actual arrest logic
                Player* target = GetTarget();
                if (target)
                {
                    // Check if this pursuit has GM override
                    bool gmOverride = sGTAPoliceMgr->IsPursuitGmOverride(_targetGuid);

                    // Apply CC spells
                    if (!_hasAppliedStun && sGTAPoliceConfig->SpellArrestStun)
                    {
                        target->CastSpell(target, sGTAPoliceConfig->SpellArrestStun, true);
                        _hasAppliedStun = true;
                    }

                    if (!_hasAppliedRoot && sGTAPoliceConfig->SpellArrestRoot)
                    {
                        target->CastSpell(target, sGTAPoliceConfig->SpellArrestRoot, true);
                        _hasAppliedRoot = true;
                    }

                    // Force player into car
                    Creature* car = GetCar();
                    if (car && GTAPoliceManager::IsPlayerArrestable(target, gmOverride))
                    {
                        target->EnterVehicle(car, sGTAPoliceConfig->SeatPrisoner);
                        if (auto* carAI = dynamic_cast<npc_gtapolice_car::CarAI*>(car->AI()))
                            carAI->SetPrisoner(target->GetGUID());
                        sGTAPoliceBridge->EmitPlayerArrested(target, car);
                    }
                    else if (sGTAPoliceConfig->DebugLog)
                    {
                        LOG_INFO("module", "[mod-gtapolice] Arrest failed: car={}, arrestable={} (gmOverride={})",
                            car ? "yes" : "no",
                            GTAPoliceManager::IsPlayerArrestable(target, gmOverride) ? "yes" : "no",
                            gmOverride ? "yes" : "no");
                    }
                }

                // Notify car and finish
                NotifyCarArrestComplete();

                if (!_isDriver)
                    _state = CREW_DONE;
                break;
            }
        }
    }
}

// ============================================================================
// COMMANDS FROM CAR AI
// ============================================================================

void npc_gtapolice_crew::CrewAI::CommandExit(ObjectGuid targetGuid, ObjectGuid carGuid, bool isDriver, uint32 delayMs)
{
    _targetGuid = targetGuid;
    _carGuid = carGuid;
    _isDriver = isDriver;
    _hasAppliedStun = false;
    _hasAppliedRoot = false;

    if (delayMs > 0)
    {
        _events.ScheduleEvent(EVENT_EXIT_DELAY, Milliseconds(delayMs));
    }
    else
    {
        DoExit();
    }
}

void npc_gtapolice_crew::CrewAI::CommandReturn()
{
    if (_state == CREW_DEAD)
        return;

    _state = CREW_RETURNING;

    Creature* car = GetCar();
    if (car)
    {
        me->GetMotionMaster()->Clear();
        me->GetMotionMaster()->MovePoint(0,
            car->GetPositionX(),
            car->GetPositionY(),
            car->GetPositionZ());

        sGTAPoliceBridge->EmitCrewReenteringVehicle(me, car);
    }

    if (sGTAPoliceConfig->DebugLog)
        LOG_INFO("module", "[mod-gtapolice] Crew officer returning to car");
}

// ============================================================================
// STATE IMPLEMENTATIONS
// ============================================================================

void npc_gtapolice_crew::CrewAI::DoExit()
{
    _state = CREW_EXITING;

    if (me->GetVehicle())
        me->ExitVehicle();

    _events.ScheduleEvent(EVENT_TICK, 250ms);

    sGTAPoliceBridge->EmitCrewExitedVehicle(me, GetCar(), GetTarget());

    if (sGTAPoliceConfig->DebugLog)
        LOG_INFO("module", "[mod-gtapolice] Crew officer exiting vehicle (driver={})", _isDriver ? "yes" : "no");
}

void npc_gtapolice_crew::CrewAI::UpdateExiting()
{
    // Wait until fully out of vehicle
    if (me->GetVehicle())
        return;

    // Successfully exited - apply run speed and start approaching target
    _state = CREW_APPROACHING;

    // Apply configured run speed
    if (sGTAPoliceConfig->CrewRunSpeed > 0.0f)
        me->SetSpeed(MOVE_RUN, sGTAPoliceConfig->CrewRunSpeed);

    Player* target = GetTarget();
    if (target && target->IsAlive())
    {
        me->GetMotionMaster()->Clear();
        me->GetMotionMaster()->MoveChase(target);

        sGTAPoliceBridge->EmitCrewApproaching(me, target, me->GetDistance(target));
    }

    if (sGTAPoliceConfig->DebugLog)
        LOG_INFO("module", "[mod-gtapolice] Crew officer approaching on foot (speed={:.1f})", sGTAPoliceConfig->CrewRunSpeed);
}

void npc_gtapolice_crew::CrewAI::UpdateApproaching()
{
    Player* target = GetTarget();
    if (!target || !target->IsAlive())
    {
        // Target gone - notify car
        NotifyCarArrestComplete();
        return;
    }

    float dist = me->GetDistance(target);

    // Close enough to arrest
    if (dist <= sGTAPoliceConfig->CrewArrestRange)
    {
        _state = CREW_ARRESTING;
        me->GetMotionMaster()->Clear();
        me->StopMoving();

        if (sGTAPoliceConfig->DebugLog)
            LOG_INFO("module", "[mod-gtapolice] Crew officer in arrest range (dist={:.1f})", dist);
        return;
    }

    // Re-target if MoveChase lost the target
    if (!me->GetVictim() && !me->HasUnitState(UNIT_STATE_CHASE))
    {
        me->GetMotionMaster()->MoveChase(target);
    }
}

void npc_gtapolice_crew::CrewAI::UpdateArresting()
{
    Player* target = GetTarget();
    if (!target || !target->IsAlive())
    {
        NotifyCarArrestComplete();
        return;
    }

    // Start arrest animation sequence (only once per arrest)
    if (!_arrestSequenceStarted)
    {
        _arrestSequenceStarted = true;

        // Face the target
        me->SetFacingToObject(target);

        // Officer points at target
        me->HandleEmoteCommand(25);  // EMOTE_POINT

        // Yell "Freeze!"
        me->Yell("LSPD! Freeze! Hands where I can see them!", LANG_UNIVERSAL);

        // Emit bridge event
        sGTAPoliceBridge->EmitCrewArrestingTarget(me, target);

        // Schedule next step
        _events.ScheduleEvent(EVENT_ARREST_STEP_2, 1500ms);

        if (sGTAPoliceConfig->DebugLog)
            LOG_INFO("module", "[mod-gtapolice] Crew officer starting arrest sequence");
    }
}

void npc_gtapolice_crew::CrewAI::UpdateReturning()
{
    Creature* car = GetCar();
    if (!car)
    {
        _state = CREW_DONE;
        return;
    }

    float dist = me->GetDistance(car);

    // Close enough to board
    if (dist <= 5.0f)
    {
        _state = CREW_BOARDING;
        me->GetMotionMaster()->Clear();
        me->StopMoving();
        me->EnterVehicle(car, sGTAPoliceConfig->SeatDriver);

        if (sGTAPoliceConfig->DebugLog)
            LOG_INFO("module", "[mod-gtapolice] Driver boarding car");
        return;
    }

    // Keep moving toward car if stuck
    if (!me->isMoving())
    {
        me->GetMotionMaster()->MovePoint(0,
            car->GetPositionX(),
            car->GetPositionY(),
            car->GetPositionZ());
    }
}

void npc_gtapolice_crew::CrewAI::UpdateBoarding()
{
    // Check if we're back in the vehicle
    if (me->GetVehicle())
    {
        _state = CREW_DONE;

        sGTAPoliceBridge->EmitCrewReenteredVehicle(me, GetCar());

        // Notify car AI that driver is back
        NotifyCarDriverReboarded();

        if (sGTAPoliceConfig->DebugLog)
            LOG_INFO("module", "[mod-gtapolice] Driver reboarded car");
        return;
    }

    // Still trying to enter - retry
    Creature* car = GetCar();
    if (car)
    {
        me->EnterVehicle(car, sGTAPoliceConfig->SeatDriver);
    }
}

// ============================================================================
// CAR NOTIFICATIONS
// ============================================================================

void npc_gtapolice_crew::CrewAI::NotifyCarArrestComplete()
{
    Creature* car = GetCar();
    if (!car)
        return;

    auto* carAI = dynamic_cast<npc_gtapolice_car::CarAI*>(car->AI());
    if (carAI)
        carAI->NotifyCrewArrestComplete();
}

void npc_gtapolice_crew::CrewAI::NotifyCarDriverReboarded()
{
    Creature* car = GetCar();
    if (!car)
        return;

    auto* carAI = dynamic_cast<npc_gtapolice_car::CarAI*>(car->AI());
    if (carAI)
        carAI->NotifyDriverReboarded();
}
