/*
 * mod-ec-realtime-bridge - AzerothCore Script Hooks
 * Copyright (C) 2025 EC
 *
 * Integrates the Realtime Bridge with AzerothCore's event system.
 * Provides startup/shutdown lifecycle and debug commands.
 */

#include "RealtimeBridgeSocket.h"
#include "RealtimeBridgeTypes.h"
#include "ScholarshipManager.h"
#include "PlayerbotsBridgeAdapter.h"

#include "Chat.h"
#include "Config.h"
#include "Log.h"
#include "Player.h"
#include "ScriptMgr.h"
#include "World.h"

using namespace ECRealtimeBridge;
using namespace Acore::ChatCommands;

// ============================================================================
// WORLD SCRIPT - Lifecycle management
// ============================================================================

class RealtimeBridgeWorldScript : public WorldScript
{
public:
    RealtimeBridgeWorldScript() : WorldScript("RealtimeBridgeWorldScript") {}

    void OnStartup() override
    {
        LOG_INFO("module.realtime-bridge", "ECRealtimeBridge: OnStartup called");
        sRealtimeBridge->Initialize();

        // Initialize Playerbots Bridge Adapter for bot activity monitoring
        sPlayerbotsBridge->Initialize();

        // Register life events handler for Korean Quest integration
        sRealtimeBridge->RegisterHandler(EventNamespace::LIFE_EVENTS, [](const BridgeCommand& cmd)
        {
            sScholarshipMgr->HandleLifeEvent(cmd.type, cmd.jsonPayload);
        });
    }

    void OnShutdown() override
    {
        LOG_INFO("module.realtime-bridge", "ECRealtimeBridge: OnShutdown called");
        sPlayerbotsBridge->Shutdown();
        sRealtimeBridge->Shutdown();
    }

    void OnUpdate(uint32 /*diff*/) override
    {
        // Process any pending commands from Node.js
        sRealtimeBridge->ProcessPendingCommands();
    }
};

// ============================================================================
// COMMAND SCRIPT - Debug and admin commands
// ============================================================================

class RealtimeBridgeCommandScript : public CommandScript
{
public:
    RealtimeBridgeCommandScript() : CommandScript("RealtimeBridgeCommandScript") {}

    ChatCommandTable GetCommands() const override
    {
        static ChatCommandTable bridgeCommandTable =
        {
            { "status",      HandleBridgeStatusCommand,      SEC_GAMEMASTER,    Console::Yes },
            { "dump",        HandleBridgeDumpCommand,        SEC_GAMEMASTER,    Console::Yes },
            { "debug",       HandleBridgeDebugCommand,       SEC_ADMINISTRATOR, Console::Yes },
            { "reload",      HandleBridgeReloadCommand,      SEC_ADMINISTRATOR, Console::Yes },
            { "reset",       HandleBridgeResetCommand,       SEC_ADMINISTRATOR, Console::Yes },
            { "test",        HandleBridgeTestCommand,        SEC_ADMINISTRATOR, Console::Yes },
            { "scholarship", HandleScholarshipStatusCommand, SEC_GAMEMASTER,    Console::Yes },
        };

        static ChatCommandTable commandTable =
        {
            { "ecbridge", bridgeCommandTable },
            { "bridge",   bridgeCommandTable },  // Short alias
        };

        return commandTable;
    }

    static bool HandleBridgeStatusCommand(ChatHandler* handler, Optional<PlayerIdentifier> /*target*/)
    {
        if (!sRealtimeBridge->IsEnabled())
        {
            handler->PSendSysMessage("ECRealtimeBridge is DISABLED");
            return true;
        }

        BridgeStats stats = sRealtimeBridge->GetStats();

        handler->PSendSysMessage("=== EC Realtime Bridge Status ===");
        handler->PSendSysMessage("Connected: %s | Authenticated: %s",
            stats.connected ? "|cff00ff00YES|r" : "|cffff0000NO|r",
            stats.authenticated ? "|cff00ff00YES|r" : "|cffff0000NO|r");
        handler->PSendSysMessage("Uptime: %llu seconds", stats.uptimeSeconds);
        handler->PSendSysMessage("Events Sent: %llu | Dropped: %llu",
            stats.eventsSent, stats.eventsDropped);
        handler->PSendSysMessage("Commands Received: %llu | Processed: %llu",
            stats.commandsReceived, stats.commandsProcessed);
        handler->PSendSysMessage("Queue: %u / %u", stats.queueSize, stats.maxQueueSize);
        handler->PSendSysMessage("Bytes Out: %llu | In: %llu",
            stats.bytesOut, stats.bytesIn);
        handler->PSendSysMessage("Reconnects: %llu", stats.reconnectCount);
        handler->PSendSysMessage("Debug Mode: %s",
            sRealtimeBridge->IsDebugMode() ? "ON" : "OFF");

        return true;
    }

    static bool HandleBridgeDumpCommand(ChatHandler* handler, Optional<PlayerIdentifier> /*target*/)
    {
        sRealtimeBridge->DumpState();
        handler->PSendSysMessage("State dump written to server log");
        return true;
    }

    static bool HandleBridgeDebugCommand(ChatHandler* handler, Optional<std::string> arg)
    {
        if (!arg)
        {
            handler->PSendSysMessage("Debug mode is currently %s",
                sRealtimeBridge->IsDebugMode() ? "ON" : "OFF");
            return true;
        }

        std::string mode = *arg;
        if (mode == "on" || mode == "1" || mode == "true")
        {
            sRealtimeBridge->SetDebugMode(true);
            handler->PSendSysMessage("Debug mode ENABLED");
        }
        else if (mode == "off" || mode == "0" || mode == "false")
        {
            sRealtimeBridge->SetDebugMode(false);
            handler->PSendSysMessage("Debug mode DISABLED");
        }
        else
        {
            handler->PSendSysMessage("Usage: .bridge debug [on|off]");
        }

        return true;
    }

    static bool HandleBridgeReloadCommand(ChatHandler* handler, Optional<PlayerIdentifier> /*target*/)
    {
        handler->PSendSysMessage("Reloading ECRealtimeBridge configuration...");
        sRealtimeBridge->LoadConfig();
        handler->PSendSysMessage("Configuration reloaded");
        return true;
    }

    static bool HandleBridgeResetCommand(ChatHandler* handler, Optional<PlayerIdentifier> /*target*/)
    {
        sRealtimeBridge->ResetStats();
        handler->PSendSysMessage("Statistics reset");
        return true;
    }

    static bool HandleBridgeTestCommand(ChatHandler* handler, Optional<std::string> ns)
    {
        EventNamespace targetNs = EventNamespace::SYSTEM;
        if (ns)
        {
            targetNs = StringToNamespace(*ns);
        }

        std::ostringstream payload;
        payload << "{\"test\":true,\"message\":\"Test event from GM command\"}";

        sRealtimeBridge->EmitEvent(targetNs, "test", payload.str());

        handler->PSendSysMessage("Test event emitted to namespace '%s'",
            NamespaceToString(targetNs));

        return true;
    }

    static bool HandleScholarshipStatusCommand(ChatHandler* handler, Optional<PlayerIdentifier> /*target*/)
    {
        if (!sScholarshipMgr->HasScholarship())
        {
            handler->PSendSysMessage("=== Scholarship Status ===");
            handler->PSendSysMessage("|cffff8800No scholarship data received yet.|r");
            handler->PSendSysMessage("Awaiting life events from Korean Quest...");
            return true;
        }

        ScholarshipState state = sScholarshipMgr->GetState();

        handler->PSendSysMessage("=== Scholarship Status ===");
        handler->PSendSysMessage("Rank: |cff00ff00%s|r (%s T%u)",
            state.scholarRank.c_str(), state.league.c_str(), state.tier);
        handler->PSendSysMessage("Percentile: |cff00ffff%.1f%%|r", state.percentile);
        handler->PSendSysMessage("Level: %u | XP: %u", state.level, state.totalXp);
        handler->PSendSysMessage("Study Streak: |cff00ff00%u days|r (Best: %u)",
            state.currentStreak, state.longestStreak);

        // Disciplines
        if (!state.disciplines.empty())
        {
            std::string disciplines;
            for (size_t i = 0; i < state.disciplines.size(); ++i)
            {
                if (i > 0) disciplines += ", ";
                disciplines += state.disciplines[i];
            }
            handler->PSendSysMessage("Strong in: |cff00ffff%s|r", disciplines.c_str());
        }

        if (!state.weakDisciplines.empty())
        {
            std::string weak;
            for (size_t i = 0; i < state.weakDisciplines.size(); ++i)
            {
                if (i > 0) weak += ", ";
                weak += state.weakDisciplines[i];
            }
            handler->PSendSysMessage("Needs work: |cffff8800%s|r", weak.c_str());
        }

        // Vocabulary
        if (state.vocabTotal > 0)
        {
            float masteryPercent = (state.vocabMastered * 100.0f) / state.vocabTotal;
            handler->PSendSysMessage("Vocabulary: %u/%u (|cff00ff00%.1f%% mastered|r)",
                state.vocabMastered, state.vocabTotal, masteryPercent);
            handler->PSendSysMessage("Weekly Accuracy: %.1f%%", state.weeklyAccuracy);
        }

        // Arena
        if (state.arenaWins > 0 || state.arenaLosses > 0)
        {
            handler->PSendSysMessage("Arena: |cff00ff00%u W|r / |cffff0000%u L|r (%.1f%% win rate)",
                state.arenaWins, state.arenaLosses, state.winRate);
        }

        // Random dialogue snippet
        std::string snippet = sScholarshipMgr->GetRandomDialogueSnippet();
        if (!snippet.empty())
        {
            handler->PSendSysMessage(" ");
            handler->PSendSysMessage("|cffffcc00\"%s\"|r", snippet.c_str());
        }

        return true;
    }
};

// ============================================================================
// SCRIPT REGISTRATION
// ============================================================================

void AddSC_mod_ec_realtime_bridge()
{
    new RealtimeBridgeWorldScript();
    new RealtimeBridgeCommandScript();
}
