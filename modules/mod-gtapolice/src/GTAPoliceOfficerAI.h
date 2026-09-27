/*
 * mod-gtapolice
 * Police Officer AI - Stationary NPC that scans for wanted players and dispatches cars
 */

#ifndef GTA_POLICE_OFFICER_AI_H
#define GTA_POLICE_OFFICER_AI_H

#include "ScriptMgr.h"
#include "ScriptedCreature.h"
#include "ObjectGuid.h"
#include "EventMap.h"

class npc_gtapolice_officer : public CreatureScript
{
public:
    npc_gtapolice_officer();

    struct OfficerAI : public ScriptedAI
    {
        OfficerAI(Creature* creature);

        enum Events : uint32 { EVENT_SCAN = 1 };

        void Reset() override;
        void UpdateAI(uint32 diff) override;

    private:
        Player* FindNearestWantedPlayer();
        void SpawnPursuitCar(Player* target);
        Creature* GetActiveCar();

        EventMap _events;
        ObjectGuid _activeCarGuid;
    };

    CreatureAI* GetAI(Creature* creature) const override;
};

#endif // GTA_POLICE_OFFICER_AI_H
