/*
 * mod-ec-combat - AzerothCore Script Hooks
 * Copyright (C) 2025 EC
 *
 * Hooks into player kills, arena, and battleground events.
 */

#include "ECCombatManager.h"
#include "ECCombatBridge.h"

#include "Battleground.h"
#include "BattlegroundMgr.h"
#include "Chat.h"
#include "Config.h"
#include "Creature.h"
#include "Log.h"
#include "Player.h"
#include "ScriptMgr.h"

using namespace ECCombat;
using namespace Acore::ChatCommands;

// ============================================================================
// WORLD SCRIPT - Initialize on startup
// ============================================================================

class ECCombatWorldScript : public WorldScript
{
public:
    ECCombatWorldScript() : WorldScript("ECCombatWorldScript") {}

    void OnStartup() override
    {
        LOG_INFO("module.ec-combat", "=== EC Combat Module Starting ===");
        sECCombatMgr->Initialize();
    }

    void OnShutdown() override
    {
        LOG_INFO("module.ec-combat", "=== EC Combat Module Shutting Down ===");
        sECCombatMgr->Shutdown();
    }
};

// ============================================================================
// PLAYER SCRIPT - Track PvP kills and deaths
// ============================================================================

class ECCombatPlayerScript : public PlayerScript
{
public:
    ECCombatPlayerScript() : PlayerScript("ECCombatPlayerScript") {}

    // Called when a player kills another player
    void OnPlayerPVPKill(Player* killer, Player* killed) override
    {
        if (!killer || !killed)
            return;

        // Track the kill for the killer
        sECCombatMgr->OnPvPKill(killer, killed);

        // Track the death for the victim
        sECCombatMgr->OnPvPDeath(killed, killer);

        // If inside a battleground, also track as BG kill
        if (Battleground* bg = killer->GetBattleground())
        {
            sECCombatMgr->OnBGKill(killer, killed, bg);
        }
    }

    // Called when a player kills a creature
    void OnPlayerCreatureKill(Player* killer, Creature* killed) override
    {
        if (!killer || !killed)
            return;

        sECCombatMgr->OnCreatureKill(killer, killed);
    }

    // Called when a player is killed by a creature
    void OnPlayerKilledByCreature(Creature* killer, Player* killed) override
    {
        if (!killed)
            return;

        sECCombatMgr->OnKilledByCreature(killed, killer);
    }

    // Called when a duel ends
    void OnPlayerDuelEnd(Player* winner, Player* loser, DuelCompleteType /*type*/) override
    {
        if (!winner || !loser)
            return;

        sECCombatMgr->OnDuelComplete(winner, loser);
    }

    // Called when player logs out - don't reset streak on logout, only on death
    void OnPlayerLogout(Player* /*player*/) override
    {
        // Could optionally reset stats here, but we'll persist them during session
    }
};

// ============================================================================
// BATTLEGROUND SCRIPT - Track BG events
// ============================================================================

class ECCombatBGScript : public AllBattlegroundScript
{
public:
    ECCombatBGScript() : AllBattlegroundScript("ECCombatBGScript") {}

    void OnBattlegroundStart(Battleground* bg) override
    {
        if (!bg)
            return;

        sECCombatMgr->OnBGStart(bg);
    }

    void OnBattlegroundEnd(Battleground* bg, TeamId winnerTeamId) override
    {
        if (!bg)
            return;

        sECCombatMgr->OnBGEnd(bg, winnerTeamId);
    }

    void OnBattlegroundEndReward(Battleground* bg, Player* player, TeamId winnerTeamId) override
    {
        if (!bg || !player)
            return;

        // Track win/loss for player
        // This is called for each player at end of BG
        // We could update stats here if needed
    }
};

// ============================================================================
// GM COMMANDS
// ============================================================================

class ECCombatCommandScript : public CommandScript
{
public:
    ECCombatCommandScript() : CommandScript("ECCombatCommandScript") {}

    ChatCommandTable GetCommands() const override
    {
        static ChatCommandTable combatSubCommands =
        {
            { "stats",  HandleCombatStatsCommand,  SEC_PLAYER,   Console::No },
            { "streak", HandleCombatStreakCommand, SEC_PLAYER,   Console::No },
            { "debug",  HandleCombatDebugCommand,  SEC_GAMEMASTER, Console::No },
        };

        static ChatCommandTable commandTable =
        {
            { "combat", combatSubCommands },
        };

        return commandTable;
    }

    // .combat stats - Show your combat stats
    static bool HandleCombatStatsCommand(ChatHandler* handler)
    {
        Player* player = handler->GetSession()->GetPlayer();
        if (!player)
            return false;

        auto stats = sECCombatMgr->GetPlayerStats(player);

        handler->PSendSysMessage("=== Combat Statistics ===");
        handler->PSendSysMessage("PvP Kills: %u | Deaths: %u | K/D: %.2f",
            stats.totalPvPKills, stats.totalPvPDeaths,
            stats.totalPvPDeaths > 0 ? (float)stats.totalPvPKills / stats.totalPvPDeaths : (float)stats.totalPvPKills);
        handler->PSendSysMessage("Current Streak: %u | Highest: %u",
            stats.currentKillStreak, stats.highestKillStreak);
        handler->PSendSysMessage("Multi-kills - Double: %u | Triple: %u | Quadra: %u | Penta: %u",
            stats.doubleKills, stats.tripleKills, stats.quadraKills, stats.pentaKills);
        handler->PSendSysMessage("BG Stats - Kills: %u | Deaths: %u | Flags: %u | Objectives: %u",
            stats.bgKills, stats.bgDeaths, stats.flagCaptures, stats.objectiveCaptures);
        handler->PSendSysMessage("Arena - Kills: %u | Deaths: %u",
            stats.arenaKills, stats.arenaDeaths);

        return true;
    }

    // .combat streak - Show your current kill streak
    static bool HandleCombatStreakCommand(ChatHandler* handler)
    {
        Player* player = handler->GetSession()->GetPlayer();
        if (!player)
            return false;

        uint32 streak = sECCombatMgr->GetKillStreak(player);

        if (streak == 0)
            handler->SendSysMessage("You don't have a kill streak.");
        else if (streak < 3)
            handler->PSendSysMessage("Current streak: %u kills (need %u for Killing Spree)", streak, 3 - streak);
        else
        {
            const char* title = "Killing Spree";
            if (streak >= 30) title = "Beyond Godlike";
            else if (streak >= 25) title = "Legendary";
            else if (streak >= 20) title = "Godlike";
            else if (streak >= 15) title = "Dominating";
            else if (streak >= 10) title = "Unstoppable";
            else if (streak >= 5) title = "Rampage";

            handler->PSendSysMessage("|cffff0000%s!|r Kill streak: %u", title, streak);
        }

        return true;
    }

    // .combat debug on/off - Toggle debug mode (GM only)
    static bool HandleCombatDebugCommand(ChatHandler* handler, Optional<std::string> arg)
    {
        if (!arg || arg->empty())
        {
            handler->SendSysMessage("Usage: .combat debug on/off");
            return false;
        }

        if (*arg == "on")
        {
            handler->SendSysMessage("Combat debug mode enabled.");
            // Would set debug flag in manager
        }
        else if (*arg == "off")
        {
            handler->SendSysMessage("Combat debug mode disabled.");
        }
        else
        {
            handler->SendSysMessage("Usage: .combat debug on/off");
        }

        return true;
    }
};

// ============================================================================
// SCRIPT REGISTRATION
// ============================================================================

void AddSC_mod_ec_combat()
{
    new ECCombatWorldScript();
    new ECCombatPlayerScript();
    new ECCombatBGScript();
    new ECCombatCommandScript();
}
