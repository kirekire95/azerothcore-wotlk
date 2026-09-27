/*
 * mod-gtapolice
 * Police Car AI - Vehicle that pursues wanted players and transports to jail
 */

#ifndef GTA_POLICE_CAR_AI_H
#define GTA_POLICE_CAR_AI_H

#include "ScriptMgr.h"
#include "ScriptedCreature.h"
#include "ObjectGuid.h"
#include "EventMap.h"

class npc_gtapolice_car : public CreatureScript
{
public:
    npc_gtapolice_car();

    struct CarAI : public ScriptedAI
    {
        CarAI(Creature* creature);

        enum CarState : uint8
        {
            STATE_IDLE      = 0,
            STATE_CHASE     = 1,
            STATE_APPROACH  = 2,  // Car stopped, crew exiting + approaching on foot
            STATE_LOADING   = 3,  // Arrest done, driver returning to car
            STATE_TRANSPORT = 4,
        };

        enum CarEvents : uint32
        {
            EVENT_CHECK             = 1,
            EVENT_TRANSPORT_FAILSAFE = 2,
            EVENT_LOADING_TIMEOUT   = 3,
            EVENT_RADIO_CHATTER     = 4,
        };

        // Public interface for external coordination
        void SetTarget(ObjectGuid targetGuid);
        void SetPrisoner(ObjectGuid prisonerGuid);
        void StartChase();
        void NotifyCrewArrestComplete();
        void NotifyDriverReboarded();

        Player* GetTarget();
        Player* GetPrisoner();
        CarState GetState() const { return _state; }

        // AI overrides
        void Reset() override;
        void UpdateAI(uint32 diff) override;
        void JustDied(Unit* killer) override;
        void MoveInLineOfSight(Unit*) override {}   // Prevent auto-aggro (pursuit AI handles chase)
        void AttackStart(Unit*) override {}          // Prevent auto-attack (car doesn't melee)

    private:
        void StartApproach();
        void StartLoading();
        void StartTransport();
        void ReleaseAndJail(bool teleport);
        bool TryCommandCrewExit();
        bool TryCommandCrewReturn();
        void DespawnNonDriverCrew();

        void UpdateChase(uint32 diff);
        void UpdateApproach(uint32 diff);
        void UpdateLoading(uint32 diff);
        void UpdateTransport();

        EventMap _events;
        ObjectGuid _targetGuid;
        ObjectGuid _prisonerGuid;
        CarState _state;
        uint32 _chaseElapsedMs;
        uint32 _lastUpdateEmitMs;       // For periodic pursuit_update events
        float _lastTargetX, _lastTargetY, _lastTargetZ;
        bool _hasLastTargetPos;
        bool _crewArrestComplete;
        bool _driverReboarded;

        // Transport destination (resolved from DB jail locations)
        float _transportX, _transportY, _transportZ;
    };

    CreatureAI* GetAI(Creature* creature) const override;
};

#endif // GTA_POLICE_CAR_AI_H
