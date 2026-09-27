/*
 * mod-gtapolice
 * Pay-n-Spray NPC - Clear wanted level for gold (like GTA's Pay 'n' Spray)
 */

#ifndef GTA_POLICE_PAYNSPRAY_H
#define GTA_POLICE_PAYNSPRAY_H

#include "ScriptMgr.h"
#include "ScriptedGossip.h"

class npc_gtapolice_paynspray : public CreatureScript
{
public:
    npc_gtapolice_paynspray();

    bool OnGossipHello(Player* player, Creature* creature) override;
    bool OnGossipSelect(Player* player, Creature* creature, uint32 sender, uint32 action) override;
};

#endif // GTA_POLICE_PAYNSPRAY_H
