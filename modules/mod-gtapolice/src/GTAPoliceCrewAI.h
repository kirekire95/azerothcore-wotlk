/*
 * mod-gtapolice
 * Crew Officer AI - Rides in police car, exits, approaches, arrests player
 */

#ifndef GTA_POLICE_CREW_AI_H
#define GTA_POLICE_CREW_AI_H

#include "ScriptMgr.h"
#include "ScriptedCreature.h"
#include "ObjectGuid.h"
#include "EventMap.h"

class npc_gtapolice_crew : public CreatureScript
{
public:
    npc_gtapolice_crew();

    struct CrewAI : public ScriptedAI
    {
        CrewAI(Creature* creature);

        enum CrewState : uint8
        {
            CREW_RIDING      = 0,  // Inside vehicle, passive
            CREW_EXITING     = 1,  // ExitVehicle() in progress
            CREW_APPROACHING = 2,  // MoveChase(target) on foot
            CREW_ARRESTING   = 3,  // Applying CC, forcing into car
            CREW_RETURNING   = 4,  // Driver: walking back to car
            CREW_BOARDING    = 5,  // Driver: EnterVehicle(car, 0)
            CREW_DONE        = 6,  // Back in car, ready for transport
            CREW_DEAD        = 7,  // Killed
        };

        enum CrewEvents : uint32
        {
            EVENT_TICK          = 1,
            EVENT_EXIT_DELAY    = 2,
            EVENT_ARREST_STEP_2 = 3,
            EVENT_ARREST_STEP_3 = 4,
            EVENT_ARREST_COMPLETE = 5,
        };

        enum CrewPoints : uint32
        {
            POINT_ARREST = 1,
            POINT_REBOARD = 2,
        };

        // Commands from car AI
        void CommandExit(ObjectGuid targetGuid, ObjectGuid carGuid, bool isDriver, uint32 delayMs);
        void CommandReturn();

        CrewState GetState() const { return _state; }
        bool IsDriver() const { return _isDriver; }

        // AI overrides
        void Reset() override;
        void UpdateAI(uint32 diff) override;
        void JustDied(Unit* killer) override;
        void MoveInLineOfSight(Unit*) override {}   // Prevent auto-aggro (arrest AI handles chase)
        void AttackStart(Unit*) override {}          // Prevent auto-attack (arrest sequence handles CC)

    private:
        Player* GetTarget();
        Creature* GetCar();

        void DoExit();
        void UpdateExiting();
        void UpdateApproaching();
        void UpdateArresting();
        void UpdateReturning();
        void UpdateBoarding();

        void NotifyCarArrestComplete();
        void NotifyCarDriverReboarded();

        EventMap _events;
        ObjectGuid _targetGuid;
        ObjectGuid _carGuid;
        CrewState _state;
        bool _isDriver;
        bool _hasAppliedStun;
        bool _hasAppliedRoot;
        bool _arrestSequenceStarted;
    };

    CreatureAI* GetAI(Creature* creature) const override;
};

#endif // GTA_POLICE_CREW_AI_H
