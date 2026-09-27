/*
 * mod-alive-world - AzerothCore Script Hooks
 * Copyright (C) 2025 EC
 *
 * This file contains all the script hooks that integrate the Alive World
 * system with AzerothCore's event system.
 */

#include "AliveWorldBridgeAdapter.h"
#include "AliveWorldManager.h"
#include "AliveWorldTypes.h"
#include "BotMemoryManager.h"

#include "Config.h"
#include "Creature.h"
#include "DatabaseEnv.h"
#include "Group.h"
#include "Log.h"
#include "Map.h"
#include "ObjectAccessor.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "ScriptMgr.h"
#include "World.h"
#include "WorldSession.h"
#include "Chat.h"
#include "GameTime.h"
#include "ChatCommand.h"
#include "ChatCommandTags.h"
#include "Weather.h"

#include <algorithm>
#include <cctype>

using namespace AliveWorld;
using namespace Acore::ChatCommands;

// =========================================================================
// WORLD SCRIPT - Main update loop and initialization
// =========================================================================

class AliveWorldWorldScript : public WorldScript
{
public:
    AliveWorldWorldScript() : WorldScript("AliveWorldWorldScript") { }

    void OnStartup() override
    {
        LOG_INFO("module.alive-world", "AliveWorld: OnStartup called");
        sAliveWorldMgr->Initialize();
    }

    void OnShutdown() override
    {
        LOG_INFO("module.alive-world", "AliveWorld: OnShutdown called");
        sAliveWorldMgr->Shutdown();
    }

    void OnUpdate(uint32 diff) override
    {
        sAliveWorldMgr->Update(diff);

        // Check for time transitions (every 60 seconds to minimize overhead)
        static uint32 timeCheckTimer = 0;
        timeCheckTimer += diff;
        if (timeCheckTimer >= 60000)  // 1 minute
        {
            timeCheckTimer = 0;
            sAliveWorldMgr->CheckTimeTransition();
        }
    }
};

// =========================================================================
// PLAYER SCRIPT - Player events
// =========================================================================

class AliveWorldPlayerScript : public PlayerScript
{
public:
    AliveWorldPlayerScript() : PlayerScript("AliveWorldPlayerScript") { }

    void OnPlayerLogin(Player* player) override
    {
        sAliveWorldMgr->OnPlayerLogin(player);
        sAliveWorldBridge->EmitPlayerLogin(player);
    }

    void OnPlayerLogout(Player* player) override
    {
        sAliveWorldMgr->OnPlayerLogout(player);
        sAliveWorldBridge->EmitPlayerLogout(player);
    }

    void OnPlayerBeforeSendChatMessage(Player* player, uint32& type, uint32& /*lang*/, std::string& msg) override
    {
        sAliveWorldMgr->OnPlayerChat(player, type, msg);
        sAliveWorldBridge->EmitPlayerChat(player, type, msg);
    }

    void OnPlayerEmote(Player* player, uint32 emote) override
    {
        sAliveWorldMgr->OnPlayerEmote(player, emote);
        sAliveWorldBridge->EmitPlayerEmote(player, emote);
    }

    void OnPlayerCreatureKill(Player* player, Creature* creature) override
    {
        sAliveWorldMgr->OnPlayerKillCreature(player, creature);
        if (creature)
            sAliveWorldBridge->EmitCreatureKilled(player, creature->GetEntry());
    }

    void OnPlayerPVPKill(Player* killer, Player* killed) override
    {
        sAliveWorldMgr->OnPlayerKillPlayer(killer, killed);
        sAliveWorldBridge->EmitPlayerKilled(killer, killed);
    }

    void OnPlayerKilledByCreature(Creature* killer, Player* killed) override
    {
        sAliveWorldMgr->OnPlayerDeath(killed, killer);
        sAliveWorldBridge->EmitPlayerDeath(killed, killer);
    }

    void OnPlayerUpdateZone(Player* player, uint32 newZone, uint32 newArea) override
    {
        sAliveWorldMgr->OnPlayerChangeZone(player, newZone, newArea);
        sAliveWorldBridge->EmitPlayerZoneChange(player, newZone, newArea);
    }

    void OnPlayerDuelEnd(Player* winner, Player* loser, DuelCompleteType type) override
    {
        if (type == DUEL_WON)
        {
            sAliveWorldMgr->OnDuelEnd(winner, loser);
            sAliveWorldBridge->EmitDuelEnd(winner, loser);
        }
    }

    void OnPlayerDuelStart(Player* player1, Player* player2) override
    {
        sAliveWorldMgr->OnDuelStart(player1, player2);
        sAliveWorldBridge->EmitDuelStart(player1, player2);
    }
};

// =========================================================================
// UNIT SCRIPT - Combat events
// =========================================================================

class AliveWorldUnitScript : public UnitScript
{
public:
    AliveWorldUnitScript() : UnitScript("AliveWorldUnitScript") { }

    void OnDamage(Unit* attacker, Unit* victim, uint32& /*damage*/) override
    {
        // Detect when a real player damages a bot (potential tier promotion trigger)
        if (!attacker || !victim)
            return;

        Player* attackerPlayer = attacker->ToPlayer();
        Player* victimPlayer = victim->ToPlayer();

        if (!attackerPlayer || !victimPlayer)
            return;

        // Check if victim is a bot being attacked by a real player
        if (sAliveWorldMgr->IsBot(victimPlayer) && !sAliveWorldMgr->IsBot(attackerPlayer))
        {
            // This bot is being attacked by a real player - could trigger tier promotion
            // The tier manager handles this automatically via proximity checks
        }
    }
};

// =========================================================================
// CREATURE SCRIPT - Boss and rare spawn events
// =========================================================================

class AliveWorldCreatureScript : public AllCreatureScript
{
public:
    AliveWorldCreatureScript() : AllCreatureScript("AliveWorldCreatureScript") { }

    void OnCreatureAddWorld(Creature* creature) override
    {
        if (!creature)
            return;

        // Detect rare spawns
        if (creature->isElite() && creature->GetCreatureTemplate()->rank >= CREATURE_ELITE_RAREELITE)
        {
            sAliveWorldMgr->OnRareSpawn(creature);
            sAliveWorldBridge->EmitRareSpawn(creature->GetEntry(), creature->GetName(), creature->GetZoneId());
        }

        // Detect world bosses
        if (creature->isWorldBoss())
        {
            // World boss spawn could be an event
        }
    }

    void OnAllCreatureUpdate(Creature* creature, uint32 /*diff*/) override
    {
        // Could track boss engagement here
    }
};

// =========================================================================
// GROUP SCRIPT - Group formation events
// =========================================================================

class AliveWorldGroupScript : public GroupScript
{
public:
    AliveWorldGroupScript() : GroupScript("AliveWorldGroupScript") { }

    void OnCreate(Group* group, Player* leader) override
    {
        sAliveWorldMgr->OnGroupFormed(leader);
        sAliveWorldBridge->EmitGroupFormed(leader);
    }

    void OnDisband(Group* group) override
    {
        // Group disbanded - leader may not be available
        // Could track group GUID to leader mapping if needed
    }

    void OnAddMember(Group* group, ObjectGuid guid) override
    {
        // Could track group membership changes
    }

    void OnRemoveMember(Group* group, ObjectGuid guid, RemoveMethod /*method*/, ObjectGuid /*kicker*/, char const* /*reason*/) override
    {
        // Could track member removal
    }
};

// =========================================================================
// COMMAND SCRIPT - Debug commands
// =========================================================================

class AliveWorldCommandScript : public CommandScript
{
public:
    AliveWorldCommandScript() : CommandScript("AliveWorldCommandScript") { }

    ChatCommandTable GetCommands() const override
    {
        static ChatCommandTable aliveWorldCommandTable =
        {
            { "status",  HandleAliveWorldStatusCommand,  SEC_GAMEMASTER, Console::Yes },
            { "dump",    HandleAliveWorldDumpCommand,    SEC_GAMEMASTER, Console::Yes },
            { "personality", HandleAliveWorldPersonalityCommand, SEC_GAMEMASTER, Console::Yes },
            { "debug",   HandleAliveWorldDebugCommand,   SEC_ADMINISTRATOR, Console::Yes },
            { "reload",  HandleAliveWorldReloadCommand,  SEC_ADMINISTRATOR, Console::Yes },
        };

        static ChatCommandTable commandTable =
        {
            { "aliveworld", aliveWorldCommandTable },
            { "aw",         aliveWorldCommandTable },  // Short alias
        };

        return commandTable;
    }

    static bool HandleAliveWorldStatusCommand(ChatHandler* handler, Optional<PlayerIdentifier> /*target*/)
    {
        if (!sAliveWorldMgr->IsEnabled())
        {
            handler->PSendSysMessage("Alive World System is DISABLED");
            return true;
        }

        APIStats stats = sAliveWorldMgr->GetStats();

        handler->PSendSysMessage("=== Alive World Status ===");
        handler->PSendSysMessage("Uptime: %llu seconds", stats.uptimeSeconds);
        handler->PSendSysMessage("Total Bots: %u", stats.totalBots);
        handler->PSendSysMessage("  Tier 1 (Active): %u", stats.tier1Count);
        handler->PSendSysMessage("  Tier 2 (Aware): %u", stats.tier2Count);
        handler->PSendSysMessage("  Tier 3 (Passive): %u", stats.tier3Count);
        handler->PSendSysMessage("Active Zones: %u", stats.activeZones);
        handler->PSendSysMessage("Gossip Pool: %u items", stats.gossipPoolSize);
        handler->PSendSysMessage("Event Buffer: %u events", stats.eventBufferSize);
        handler->PSendSysMessage("Pending AI Requests: %u", stats.pendingAIRequests);
        handler->PSendSysMessage("Cache Hit Rate: %u%%", stats.cacheHitRate);

        return true;
    }

    static bool HandleAliveWorldDumpCommand(ChatHandler* handler, Optional<PlayerIdentifier> /*target*/)
    {
        sAliveWorldMgr->DumpState();
        handler->PSendSysMessage("State dump written to server log");
        return true;
    }

    static bool HandleAliveWorldDebugCommand(ChatHandler* handler, Optional<std::string> arg)
    {
        if (!arg)
        {
            handler->PSendSysMessage("Debug mode is currently %s",
                sAliveWorldMgr->IsDebugMode() ? "ON" : "OFF");
            return true;
        }

        std::string mode = *arg;
        if (mode == "on" || mode == "1" || mode == "true")
        {
            sAliveWorldMgr->SetDebugMode(true);
            handler->PSendSysMessage("Debug mode ENABLED");
        }
        else if (mode == "off" || mode == "0" || mode == "false")
        {
            sAliveWorldMgr->SetDebugMode(false);
            handler->PSendSysMessage("Debug mode DISABLED");
        }
        else
        {
            // Try to parse as log level
            try
            {
                uint8 level = std::stoi(mode);
                sAliveWorldMgr->SetDebugLogLevel(level);
                handler->PSendSysMessage("Debug log level set to %u", level);
            }
            catch (...)
            {
                handler->PSendSysMessage("Usage: .aw debug [on|off|<level>]");
            }
        }

        return true;
    }

    static bool HandleAliveWorldReloadCommand(ChatHandler* handler, Optional<PlayerIdentifier> /*target*/)
    {
        handler->PSendSysMessage("Reloading Alive World configuration...");
        sAliveWorldMgr->LoadConfig();
        handler->PSendSysMessage("Configuration reloaded");
        return true;
    }

    static bool HandleAliveWorldPersonalityCommand(ChatHandler* handler, Optional<std::string> arg)
    {
        if (!sAliveWorldMgr->IsEnabled())
        {
            handler->PSendSysMessage("Alive World System is DISABLED");
            return true;
        }

        BotMemoryManager* memoryMgr = sAliveWorldMgr->GetBotMemoryManager();
        if (!memoryMgr)
        {
            handler->PSendSysMessage("Bot memory manager is not available");
            return true;
        }

        uint64 botGuid = 0;
        Player* botPlayer = nullptr;

        if (arg && !arg->empty())
        {
            std::string input = *arg;
            size_t first = input.find_first_not_of(" \t\r\n");
            if (first == std::string::npos)
            {
                handler->PSendSysMessage("Usage: .aw personality <botName|botGuid> (or select a bot and run without args)");
                return true;
            }

            size_t last = input.find_last_not_of(" \t\r\n");
            input = input.substr(first, last - first + 1);

            bool numeric = std::all_of(input.begin(), input.end(), [](unsigned char c) { return std::isdigit(c) != 0; });

            if (numeric)
            {
                try
                {
                    botGuid = static_cast<uint64>(std::stoull(input));
                }
                catch (...)
                {
                    handler->PSendSysMessage("Invalid GUID value: %s", input.c_str());
                    return true;
                }

                botPlayer = ObjectAccessor::FindPlayer(ObjectGuid::Create<HighGuid::Player>(botGuid));
            }
            else
            {
                botPlayer = ObjectAccessor::FindPlayerByName(input, false);
                if (botPlayer)
                {
                    botGuid = botPlayer->GetGUID().GetRawValue();
                }
                else
                {
                    std::string nameEsc = input;
                    CharacterDatabase.EscapeString(nameEsc);
                    QueryResult botResult = CharacterDatabase.Query(
                        "SELECT bot_guid FROM alive_world_bot_memory WHERE bot_name = '{}' LIMIT 1",
                        nameEsc
                    );

                    if (!botResult)
                    {
                        handler->PSendSysMessage("Bot '%s' not found. Use a selected bot, online bot name, or bot GUID.", input.c_str());
                        return true;
                    }

                    Field* botFields = botResult->Fetch();
                    botGuid = botFields[0].Get<uint64>();
                    botPlayer = ObjectAccessor::FindPlayer(ObjectGuid::Create<HighGuid::Player>(botGuid));
                }
            }
        }
        else
        {
            botPlayer = handler->getSelectedPlayer();
            if (!botPlayer)
            {
                handler->PSendSysMessage("Usage: .aw personality <botName|botGuid> (or select a bot and run without args)");
                return true;
            }

            botGuid = botPlayer->GetGUID().GetRawValue();
        }

        if (botPlayer && !sAliveWorldMgr->IsBot(botPlayer))
        {
            handler->PSendSysMessage("Target '%s' is not a playerbot.", botPlayer->GetName().c_str());
            return true;
        }

        BotMemoryData* memory = memoryMgr->GetBotMemory(botGuid);
        if (!memory)
        {
            // If a low counter GUID was provided, try resolving to full raw GUID.
            uint64 fullGuid = ObjectGuid::Create<HighGuid::Player>(botGuid).GetRawValue();
            if (fullGuid != botGuid)
            {
                botGuid = fullGuid;
                memory = memoryMgr->GetBotMemory(botGuid);
            }
        }

        if (!memory)
        {
            handler->PSendSysMessage("No bot memory loaded for GUID %llu.", botGuid);
            return true;
        }

        auto TraitPercent = [](float trait) -> uint32
        {
            return static_cast<uint32>(std::clamp(trait, 0.0f, 1.0f) * 100.0f);
        };

        BotPersonalityOverride profile;
        bool hasOverride = memoryMgr->GetPersonalityOverride(botGuid, profile);

        handler->PSendSysMessage("=== AliveWorld Personality ===");
        handler->PSendSysMessage("Bot: %s (GUID: %llu)", memory->botName.c_str(), botGuid);
        handler->PSendSysMessage("Mood: %s", memory->personality.currentMood.c_str());
        handler->PSendSysMessage("Aggression: %u%%%s", TraitPercent(memory->personality.aggression), (hasOverride && profile.lockAggression) ? " [LOCKED]" : "");
        handler->PSendSysMessage("Caution: %u%%%s", TraitPercent(memory->personality.caution), (hasOverride && profile.lockCaution) ? " [LOCKED]" : "");
        handler->PSendSysMessage("Sociability: %u%%%s", TraitPercent(memory->personality.sociability), (hasOverride && profile.lockSociability) ? " [LOCKED]" : "");
        handler->PSendSysMessage("Loyalty: %u%%%s", TraitPercent(memory->personality.loyalty), (hasOverride && profile.lockLoyalty) ? " [LOCKED]" : "");
        handler->PSendSysMessage("Curiosity: %u%%%s", TraitPercent(memory->personality.curiosity), (hasOverride && profile.lockCuriosity) ? " [LOCKED]" : "");
        handler->PSendSysMessage("Vengefulness: %u%%%s", TraitPercent(memory->personality.vengefulness), (hasOverride && profile.lockVengefulness) ? " [LOCKED]" : "");
        handler->PSendSysMessage("Helpfulness: %u%%%s", TraitPercent(memory->personality.helpfulness), (hasOverride && profile.lockHelpfulness) ? " [LOCKED]" : "");
        handler->PSendSysMessage("Greed: %u%%%s", TraitPercent(memory->personality.greed), (hasOverride && profile.lockGreed) ? " [LOCKED]" : "");

        if (!hasOverride)
        {
            handler->PSendSysMessage("Override Profile: none");
            return true;
        }

        handler->PSendSysMessage("Override Profile: enabled=%s moodLock=%s",
            profile.enabled ? "yes" : "no",
            profile.lockMood ? "yes" : "no");

        if (profile.lockMood)
            handler->PSendSysMessage("Fixed Mood: %s", profile.fixedMood.c_str());

        if (profile.IsRelentlessHunter())
        {
            handler->PSendSysMessage("Hunter: ACTIVE target=%llu pursuit=%.1f engage=%.1f cooldown=%ums",
                profile.relentlessTargetGuid,
                profile.hunterPursuitRadius,
                profile.hunterEngageRadius,
                profile.hunterReengageCooldownMs);
        }
        else
        {
            handler->PSendSysMessage("Hunter: inactive");
        }

        return true;
    }
};

// =========================================================================
// WEATHER SCRIPT - Weather change detection
// =========================================================================

class AliveWorldWeatherScript : public WeatherScript
{
public:
    AliveWorldWeatherScript() : WeatherScript("AliveWorldWeatherScript") { }

    void OnChange(Weather* weather, WeatherState state, float grade) override
    {
        if (!weather)
            return;

        if (!sAliveWorldMgr->IsEnabled())
            return;

        uint32 zoneId = weather->GetZone();

        // Derive weather type from state (Weather::GetWeatherType is private)
        // WeatherState encodes the weather type information
        uint8 typeFromState = static_cast<uint8>(state);

        // Forward to AliveWorldManager
        sAliveWorldMgr->OnWeatherChange(zoneId, typeFromState, state, grade);

        ALIVE_LOG_DEBUG("Weather change in zone %u: state=%u grade=%.2f",
                       zoneId, static_cast<uint8>(state), grade);
    }
};

// =========================================================================
// SCRIPT REGISTRATION
// =========================================================================

void AddSC_mod_alive_world()
{
    new AliveWorldWorldScript();
    new AliveWorldPlayerScript();
    new AliveWorldUnitScript();
    new AliveWorldCreatureScript();
    new AliveWorldGroupScript();
    new AliveWorldCommandScript();
    new AliveWorldWeatherScript();
}
