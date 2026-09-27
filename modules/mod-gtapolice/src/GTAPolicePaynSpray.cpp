/*
 * mod-gtapolice
 * Pay-n-Spray NPC implementation
 *
 * A shady NPC who clears your wanted level for gold.
 * Costs more than bribing and has a longer cooldown,
 * but works at any wanted level (no max star limit).
 */

#include "GTAPolicePaynSpray.h"
#include "GTAPoliceConfig.h"
#include "GTAPoliceManager.h"
#include "GTAPoliceBridge.h"

#include "Player.h"
#include "Creature.h"
#include "Chat.h"
#include "Log.h"

enum PaynSprayGossip
{
    GOSSIP_MENU_MAIN     = 0,
    GOSSIP_ACTION_CLEAR  = 1,
    GOSSIP_ACTION_CANCEL = 2,
};

// ============================================================================
// CONSTRUCTOR
// ============================================================================

npc_gtapolice_paynspray::npc_gtapolice_paynspray() : CreatureScript("npc_gtapolice_paynspray") {}

// ============================================================================
// GOSSIP HELLO
// ============================================================================

bool npc_gtapolice_paynspray::OnGossipHello(Player* player, Creature* creature)
{
    if (!sGTAPoliceConfig->Enabled || !sGTAPoliceConfig->PaynSprayEnabled)
    {
        player->PlayerTalkClass->SendCloseGossip();
        return true;
    }

    uint32 stars = sGTAPoliceMgr->GetPlayerWantedStars(player);

    if (stars == 0)
    {
        AddGossipItemFor(player, GOSSIP_ICON_CHAT, "You look clean to me. Come back when you need my services.", GOSSIP_MENU_MAIN, GOSSIP_ACTION_CANCEL);
        SendGossipMenuFor(player, player->GetGossipTextId(creature), creature->GetGUID());
        return true;
    }

    // Check if player is under active pursuit
    if (sGTAPoliceMgr->IsUnderPursuit(player->GetGUID()))
    {
        AddGossipItemFor(player, GOSSIP_ICON_CHAT, "Are you crazy?! The cops are right behind you! Get out of here!", GOSSIP_MENU_MAIN, GOSSIP_ACTION_CANCEL);
        SendGossipMenuFor(player, player->GetGossipTextId(creature), creature->GetGUID());
        return true;
    }

    // Check cooldown (use bribe cooldown map, reuse the logic)
    uint32 cost = stars * sGTAPoliceConfig->PaynSprayCostPerStar;
    uint32 goldCost = cost / 10000;

    if (player->GetMoney() < cost)
    {
        char buf[128];
        snprintf(buf, sizeof(buf), "You need %u gold. Come back when you have the coin.", goldCost);
        AddGossipItemFor(player, GOSSIP_ICON_CHAT, buf, GOSSIP_MENU_MAIN, GOSSIP_ACTION_CANCEL);
        SendGossipMenuFor(player, player->GetGossipTextId(creature), creature->GetGUID());
        return true;
    }

    char buf[256];
    snprintf(buf, sizeof(buf),
        "I can make your... problems... disappear. %u stars? That'll be %u gold. Deal?",
        stars, goldCost);

    AddGossipItemFor(player, GOSSIP_ICON_MONEY_BAG, buf, GOSSIP_MENU_MAIN, GOSSIP_ACTION_CLEAR);
    AddGossipItemFor(player, GOSSIP_ICON_CHAT, "No thanks, I'll handle it myself.", GOSSIP_MENU_MAIN, GOSSIP_ACTION_CANCEL);
    SendGossipMenuFor(player, player->GetGossipTextId(creature), creature->GetGUID());
    return true;
}

// ============================================================================
// GOSSIP SELECT
// ============================================================================

bool npc_gtapolice_paynspray::OnGossipSelect(Player* player, Creature* creature, uint32 /*sender*/, uint32 action)
{
    player->PlayerTalkClass->ClearMenus();

    if (action == GOSSIP_ACTION_CANCEL)
    {
        player->PlayerTalkClass->SendCloseGossip();
        return true;
    }

    if (action != GOSSIP_ACTION_CLEAR)
    {
        player->PlayerTalkClass->SendCloseGossip();
        return true;
    }

    // Validate again (race condition protection)
    uint32 stars = sGTAPoliceMgr->GetPlayerWantedStars(player);
    if (stars == 0)
    {
        player->PlayerTalkClass->SendCloseGossip();
        return true;
    }

    if (sGTAPoliceMgr->IsUnderPursuit(player->GetGUID()))
    {
        ChatHandler(player->GetSession()).SendSysMessage(
            "|cFFFF0000[Pay-n-Spray]|r Can't help you while the cops are chasing you!");
        player->PlayerTalkClass->SendCloseGossip();
        return true;
    }

    uint32 cost = stars * sGTAPoliceConfig->PaynSprayCostPerStar;

    if (player->GetMoney() < cost)
    {
        ChatHandler(player->GetSession()).SendSysMessage(
            "|cFFFF0000[Pay-n-Spray]|r You can't afford this.");
        player->PlayerTalkClass->SendCloseGossip();
        return true;
    }

    // Take money
    player->ModifyMoney(-static_cast<int32>(cost));

    // Clear wanted
    sGTAPoliceMgr->ClearPlayerWanted(player);

    // Clean up decay state
    // (Directly clearing via the manager is sufficient since ClearPlayerWanted resets guard system)

    // Emit bridge event
    sGTAPoliceBridge->EmitPaynSpray(player, stars, cost);

    // Visual feedback
    creature->HandleEmoteCommand(EMOTE_ONESHOT_LAUGH);

    uint32 goldCost = cost / 10000;
    ChatHandler(player->GetSession()).PSendSysMessage(
        "|cFF00FF00[Pay-n-Spray]|r Your record has been... cleaned. ({} gold)", goldCost);

    if (sGTAPoliceConfig->DebugLog)
        LOG_INFO("module", "[mod-gtapolice] Player {} used Pay-n-Spray for {} copper ({} stars)",
            player->GetName(), cost, stars);

    player->PlayerTalkClass->SendCloseGossip();
    return true;
}
