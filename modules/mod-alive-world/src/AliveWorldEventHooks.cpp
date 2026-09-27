/*
 * mod-alive-world - Unified Event Hooks
 * Copyright (C) 2025 EC
 *
 * Central event capture for all game events that bots should react to.
 * This replaces/supplements mod-ollama-chat's scattered hooks with a unified system.
 *
 * All events flow through the AliveWorldManager and are processed according to
 * the bot's intelligence tier and personality.
 */

#include "AliveWorldManager.h"
#include "AliveWorldBridgeAdapter.h"
#include "EventCollector.h"
#include "BotMemoryManager.h"
#include "IntelligenceTierManager.h"
#include "AIDecisionEngine.h"
#include "BehaviorExecutor.h"
#include "GossipManager.h"
#include "GameTime.h"

#include "Player.h"
#include "Creature.h"
#include "CreatureAI.h"
#include "GameObject.h"
#include "Group.h"
#include "Guild.h"
#include "Chat.h"
#include "ScriptMgr.h"
#include "ObjectAccessor.h"
#include "QuestDef.h"
#include "AchievementMgr.h"
#include "Log.h"
#include "SharedDefines.h"

// Playerbots
#include "PlayerbotAI.h"
#include "PlayerbotMgr.h"

namespace AliveWorld
{

// ============================================================================
// HELPER FUNCTIONS
// ============================================================================

static bool IsPlayerBot(Player* player)
{
    if (!player) return false;
    return sPlayerbotsMgr.GetPlayerbotAI(player) != nullptr;
}

static bool HasNearbyRealPlayer(Player* bot, float range = 100.0f)
{
    if (!bot) return false;

    auto const& players = bot->GetMap()->GetPlayers();
    for (auto const& ref : players)
    {
        Player* player = ref.GetSource();
        if (!player || player == bot) continue;
        if (IsPlayerBot(player)) continue;
        if (bot->GetDistance(player) <= range)
            return true;
    }
    return false;
}

// ============================================================================
// COMBAT HOOKS
// ============================================================================

class AliveWorldCombatScript : public PlayerScript
{
public:
    AliveWorldCombatScript() : PlayerScript("AliveWorldCombatScript") {}

    // Player killed a creature
    void OnPlayerCreatureKill(Player* killer, Creature* victim) override
    {
        if (!sAliveWorldMgr->IsEnabled() || !killer || !victim)
            return;

        sAliveWorldMgr->OnPlayerKillCreature(killer, victim);
    }

    // Player killed another player (PvP)
    void OnPlayerPVPKill(Player* killer, Player* victim) override
    {
        if (!sAliveWorldMgr->IsEnabled() || !killer || !victim)
            return;

        sAliveWorldMgr->OnPlayerKillPlayer(killer, victim);
    }

    // Player died
    void OnPlayerJustDied(Player* player) override
    {
        if (!sAliveWorldMgr->IsEnabled() || !player)
            return;

        // Get the killer unit
        Unit* killer = player->GetCharmerOrOwnerOrSelf();
        if (killer == player)
            killer = nullptr;

        sAliveWorldMgr->OnPlayerDeath(player, killer);
    }
};

// ============================================================================
// LOOT HOOKS
// ============================================================================

class AliveWorldLootScript : public PlayerScript
{
public:
    AliveWorldLootScript() : PlayerScript("AliveWorldLootScript") {}

    void OnPlayerLootItem(Player* player, Item* item, uint32 /*count*/, ObjectGuid /*lootguid*/) override
    {
        if (!sAliveWorldMgr->IsEnabled() || !player || !item)
            return;

        // Log notable loot for potential gossip
        const ItemTemplate* itemTemplate = item->GetTemplate();
        if (itemTemplate && itemTemplate->Quality >= ITEM_QUALITY_EPIC)
        {
            LOG_DEBUG("module.alive-world", "[AliveWorldEventHooks] Player %s looted epic+ item: %s",
                player->GetName().c_str(), itemTemplate->Name1.c_str());
        }
    }
};

// ============================================================================
// QUEST HOOKS
// ============================================================================

class AliveWorldQuestScript : public PlayerScript
{
public:
    AliveWorldQuestScript() : PlayerScript("AliveWorldQuestScript") {}

    void OnPlayerCompleteQuest(Player* player, Quest const* quest) override
    {
        if (!sAliveWorldMgr->IsEnabled() || !player || !quest)
            return;

        // Log quest completions for potential gossip
        LOG_DEBUG("module.alive-world", "[AliveWorldEventHooks] Player %s completed quest: %s",
            player->GetName().c_str(), quest->GetTitle().c_str());
    }
};

// ============================================================================
// LEVEL HOOKS
// ============================================================================

class AliveWorldLevelScript : public PlayerScript
{
public:
    AliveWorldLevelScript() : PlayerScript("AliveWorldLevelScript") {}

    void OnPlayerLevelChanged(Player* player, uint8 oldLevel) override
    {
        if (!sAliveWorldMgr->IsEnabled() || !player)
            return;

        uint8 newLevel = player->GetLevel();

        // Log level ups for potential gossip
        LOG_DEBUG("module.alive-world", "[AliveWorldEventHooks] Player %s leveled up: %u -> %u",
            player->GetName().c_str(), oldLevel, newLevel);
    }
};

// ============================================================================
// ACHIEVEMENT HOOKS
// ============================================================================

class AliveWorldAchievementScript : public PlayerScript
{
public:
    AliveWorldAchievementScript() : PlayerScript("AliveWorldAchievementScript") {}

    void OnPlayerAchievementComplete(Player* player, AchievementEntry const* achievement) override
    {
        if (!sAliveWorldMgr->IsEnabled() || !player || !achievement)
            return;

        // Log achievements for potential gossip
        LOG_DEBUG("module.alive-world", "[AliveWorldEventHooks] Player %s earned achievement: %u",
            player->GetName().c_str(), achievement->ID);
    }
};

// ============================================================================
// CHAT HOOKS
// ============================================================================

class AliveWorldChatScript : public PlayerScript
{
public:
    AliveWorldChatScript() : PlayerScript("AliveWorldChatScript") {}

    void OnPlayerBeforeSendChatMessage(Player* player, uint32& type, uint32& /*lang*/, std::string& message) override
    {
        if (!sAliveWorldMgr->IsEnabled() || !player || message.empty())
            return;

        sAliveWorldMgr->OnPlayerChat(player, type, message);
    }
};

// ============================================================================
// ZONE HOOKS
// ============================================================================

class AliveWorldZoneScript : public PlayerScript
{
public:
    AliveWorldZoneScript() : PlayerScript("AliveWorldZoneScript") {}

    void OnPlayerUpdateZone(Player* player, uint32 newZone, uint32 newArea) override
    {
        if (!sAliveWorldMgr->IsEnabled() || !player)
            return;

        sAliveWorldMgr->OnPlayerChangeZone(player, newZone, newArea);
    }
};

// ============================================================================
// DUEL HOOKS
// ============================================================================

class AliveWorldDuelScript : public PlayerScript
{
public:
    AliveWorldDuelScript() : PlayerScript("AliveWorldDuelScript") {}

    void OnPlayerDuelRequest(Player* target, Player* challenger) override
    {
        if (!sAliveWorldMgr->IsEnabled() || !target || !challenger)
            return;

        LOG_DEBUG("module.alive-world", "[AliveWorldEventHooks] Duel requested: %s challenged %s",
            challenger->GetName().c_str(), target->GetName().c_str());
    }

    void OnPlayerDuelStart(Player* player1, Player* player2) override
    {
        if (!sAliveWorldMgr->IsEnabled() || !player1 || !player2)
            return;

        sAliveWorldMgr->OnDuelStart(player1, player2);
    }

    void OnPlayerDuelEnd(Player* winner, Player* loser, DuelCompleteType /*type*/) override
    {
        if (!sAliveWorldMgr->IsEnabled() || !winner || !loser)
            return;

        sAliveWorldMgr->OnDuelEnd(winner, loser);
    }
};

// ============================================================================
// GUILD HOOKS
// ============================================================================

class AliveWorldGuildScript : public GuildScript
{
public:
    AliveWorldGuildScript() : GuildScript("AliveWorldGuildScript") {}

    void OnAddMember(Guild* guild, Player* player, uint8& /*plRank*/) override
    {
        if (!sAliveWorldMgr->IsEnabled() || !guild || !player)
            return;

        LOG_DEBUG("module.alive-world", "[AliveWorldEventHooks] Player %s joined guild %s",
            player->GetName().c_str(), guild->GetName().c_str());
    }

    void OnRemoveMember(Guild* guild, Player* player, bool /*isDisbanding*/, bool /*isKicked*/) override
    {
        if (!sAliveWorldMgr->IsEnabled() || !guild || !player)
            return;

        LOG_DEBUG("module.alive-world", "[AliveWorldEventHooks] Player %s left guild %s",
            player->GetName().c_str(), guild->GetName().c_str());
    }
};

// ============================================================================
// GROUP HOOKS
// ============================================================================

class AliveWorldGroupScript : public GroupScript
{
public:
    AliveWorldGroupScript() : GroupScript("AliveWorldGroupScript") {}

    void OnCreate(Group* group, Player* leader) override
    {
        if (!sAliveWorldMgr->IsEnabled() || !group || !leader)
            return;

        sAliveWorldMgr->OnGroupFormed(leader);
    }

    void OnDisband(Group* group) override
    {
        if (!sAliveWorldMgr->IsEnabled() || !group)
            return;

        Player* leader = ObjectAccessor::FindPlayer(group->GetLeaderGUID());
        if (leader)
            sAliveWorldMgr->OnGroupDisbanded(leader);
    }
};

// ============================================================================
// CREATURE HOOKS
// ============================================================================

class AliveWorldCreatureScript : public AllCreatureScript
{
public:
    AliveWorldCreatureScript() : AllCreatureScript("AliveWorldCreatureScript") {}

    void OnCreatureAddWorld(Creature* creature) override
    {
        if (!sAliveWorldMgr->IsEnabled() || !creature)
            return;

        // Check for rare spawns using rank
        uint32 rank = creature->GetCreatureTemplate()->rank;
        if (rank == CREATURE_ELITE_RARE || rank == CREATURE_ELITE_RAREELITE)
        {
            sAliveWorldMgr->OnRareSpawn(creature);
        }
    }
};

// ============================================================================
// UNIT DEATH HOOKS
// ============================================================================

class AliveWorldUnitScript : public UnitScript
{
public:
    AliveWorldUnitScript() : UnitScript("AliveWorldUnitScript") {}

    void OnUnitDeath(Unit* unit, Unit* killer) override
    {
        if (!sAliveWorldMgr->IsEnabled() || !unit)
            return;

        // Handle creature deaths
        if (Creature* creature = unit->ToCreature())
        {
            sAliveWorldMgr->OnCreatureDeath(creature, killer);

            // Check for boss kills
            if (creature->isWorldBoss() || creature->IsDungeonBoss())
            {
                Player* playerKiller = killer ? killer->ToPlayer() : nullptr;
                if (!playerKiller && killer)
                {
                    // Try to get player from charmer/owner
                    if (Unit* owner = killer->GetCharmerOrOwner())
                        playerKiller = owner->ToPlayer();
                }
                sAliveWorldMgr->OnBossKilled(creature, playerKiller);
            }
        }
    }
};

// ============================================================================
// LOGIN/LOGOUT HOOKS
// ============================================================================

class AliveWorldLoginScript : public PlayerScript
{
public:
    AliveWorldLoginScript() : PlayerScript("AliveWorldLoginScript") {}

    void OnPlayerLogin(Player* player) override
    {
        if (!sAliveWorldMgr->IsEnabled() || !player)
            return;

        sAliveWorldMgr->OnPlayerLogin(player);

        // Check if this is a bot
        if (IsPlayerBot(player))
        {
            sAliveWorldMgr->OnBotSpawn(player);
        }
    }

    void OnPlayerLogout(Player* player) override
    {
        if (!sAliveWorldMgr->IsEnabled() || !player)
            return;

        // Check if this is a bot
        if (IsPlayerBot(player))
        {
            sAliveWorldMgr->OnBotDespawn(player);
        }

        sAliveWorldMgr->OnPlayerLogout(player);
    }
};

// ============================================================================
// EMOTE HOOKS
// ============================================================================

class AliveWorldEmoteScript : public PlayerScript
{
public:
    AliveWorldEmoteScript() : PlayerScript("AliveWorldEmoteScript") {}

    void OnPlayerEmote(Player* player, uint32 emote) override
    {
        if (!sAliveWorldMgr->IsEnabled() || !player)
            return;

        sAliveWorldMgr->OnPlayerEmote(player, emote);
    }
};

// ============================================================================
// COMBAT STATE HOOKS (for bot combat start/end tracking)
// ============================================================================

class AliveWorldCombatStateScript : public UnitScript
{
public:
    AliveWorldCombatStateScript() : UnitScript("AliveWorldCombatStateScript") {}

    void OnUnitEnterCombat(Unit* unit, Unit* victim) override
    {
        if (!sAliveWorldMgr->IsEnabled() || !unit)
            return;

        // Only track bot combat state
        Player* player = unit->ToPlayer();
        if (!player || !IsPlayerBot(player))
            return;

        // Notify tier manager and bridge
        auto* tierMgr = sAliveWorldMgr->GetTierManager();
        if (tierMgr)
        {
            bool isPlayerTarget = victim && victim->ToPlayer() && !IsPlayerBot(victim->ToPlayer());
            tierMgr->OnCombatStart(player->GetGUID().GetRawValue(),
                                   victim ? victim->GetGUID().GetRawValue() : 0,
                                   isPlayerTarget);
        }

        // Emit to bridge
        sAliveWorldBridge->EmitCombatStart(player->GetGUID().GetCounter(), victim);

        LOG_DEBUG("module.alive-world", "[AliveWorldEventHooks] Bot %s entered combat with %s",
            player->GetName().c_str(), victim ? victim->GetName().c_str() : "unknown");
    }

    // NOTE: OnUnitLeaveCombat doesn't exist in UnitScript base class.
    // Use OnPlayerLeaveCombat in a PlayerScript instead for combat end tracking.
};

// ============================================================================
// TRADE HOOKS
// ============================================================================
// NOTE: OnPlayerTradeAccept doesn't exist in PlayerScript base class.
// We use OnPlayerCanInitTrade (validation hook) to emit trade_started events.

class AliveWorldTradeScript : public PlayerScript
{
public:
    AliveWorldTradeScript() : PlayerScript("AliveWorldTradeScript") {}

    bool OnPlayerCanInitTrade(Player* player, Player* target) override
    {
        if (!sAliveWorldMgr->IsEnabled() || !player || !target)
            return true;  // Allow trade

        sAliveWorldBridge->EmitTradeStarted(player, target);
        sAliveWorldMgr->OnTradeComplete(player, target);

        return true;  // Always allow the trade to proceed
    }
};

// ============================================================================
// REGISTRATION
// ============================================================================

void RegisterAliveWorldEventHooks()
{
    new AliveWorldCombatScript();
    new AliveWorldLootScript();
    new AliveWorldQuestScript();
    new AliveWorldLevelScript();
    new AliveWorldAchievementScript();
    new AliveWorldChatScript();
    new AliveWorldZoneScript();
    new AliveWorldDuelScript();
    new AliveWorldGuildScript();
    new AliveWorldGroupScript();
    new AliveWorldCreatureScript();
    new AliveWorldUnitScript();
    new AliveWorldLoginScript();
    new AliveWorldEmoteScript();
    new AliveWorldCombatStateScript();
    new AliveWorldTradeScript();

    LOG_INFO("module.alive-world", "[AliveWorld] Registered all event hooks");
}

} // namespace AliveWorld
