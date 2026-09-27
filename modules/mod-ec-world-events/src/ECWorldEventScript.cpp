/*
 * EC World Events - Script Hooks and Commands
 * Copyright (C) 2026 EC
 */

#include "ECWorldEventManager.h"
#include "Chat.h"
#include "Config.h"
#include "Player.h"
#include "ScriptMgr.h"
#include "World.h"
#include <algorithm>
#include <ctime>
#include <sstream>

using namespace EC;

namespace
{
    uint32 GetTotalPhases(const EventDefinition& def)
    {
        uint32 total = 1; // ACTIVE always exists
        if (def.preparationMinutes > 0)
            ++total;
        if (def.endingMinutes > 0)
            ++total;
        return total;
    }

    uint32 GetCurrentPhaseIndex(const ActiveEvent& event, const EventDefinition& def)
    {
        switch (event.phase)
        {
            case EventPhase::PREPARING:
                return 1;
            case EventPhase::ACTIVE:
                return def.preparationMinutes > 0 ? 2 : 1;
            case EventPhase::ENDING:
                if (def.preparationMinutes > 0)
                    return 3;
                return def.endingMinutes > 0 ? 2 : 1;
            default:
                return 1;
        }
    }

    int64 ClampToNonNegativeMinutes(std::chrono::system_clock::duration delta)
    {
        int64 minutes = std::chrono::duration_cast<std::chrono::minutes>(delta).count();
        return std::max<int64>(0, minutes);
    }

    std::string FormatLocalTimestamp(std::chrono::system_clock::time_point timePoint)
    {
        std::time_t timeValue = std::chrono::system_clock::to_time_t(timePoint);
        std::tm localTm{};
#if defined(_WIN32)
        localtime_s(&localTm, &timeValue);
#else
        localtime_r(&timeValue, &localTm);
#endif

        char buffer[32];
        if (std::strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", &localTm) == 0)
            return "unknown";
        return std::string(buffer);
    }
}

// =============================================================================
// WorldScript - Server startup and update hooks
// =============================================================================

class ECWorldEvent_WorldScript : public WorldScript
{
public:
    ECWorldEvent_WorldScript() : WorldScript("ECWorldEvent_WorldScript") { }

    void OnAfterConfigLoad(bool /*reload*/) override
    {
        sECWorldEventMgr->LoadFromConfig();
    }

    void OnStartup() override
    {
        sECWorldEventMgr->Initialize();
    }

    void OnShutdown() override
    {
        // Active events are cancelled and their durable bot checkpoints are
        // deliberately retained for restoration on the next startup.
        sECWorldEventMgr->Shutdown();
    }

    void OnUpdate(uint32 diff) override
    {
        sECWorldEventMgr->Update(diff);
    }
};

// =============================================================================
// PlayerScript - Zone change and kill hooks
// =============================================================================

class ECWorldEvent_PlayerScript : public PlayerScript
{
public:
    ECWorldEvent_PlayerScript() : PlayerScript("ECWorldEvent_PlayerScript") { }

    void OnPlayerUpdateZone(Player* player, uint32 newZone, uint32 newArea) override
    {
        sECWorldEventMgr->OnPlayerEnterZone(player, newZone, newArea);
    }

    void OnPlayerCreatureKill(Player* killer, Creature* killed) override
    {
        sECWorldEventMgr->OnPlayerKillCreature(killer, killed);
    }

    void OnPlayerPVPKill(Player* killer, Player* killed) override
    {
        sECWorldEventMgr->OnPlayerDeath(killed, killer);
    }

    void OnPlayerKilledByCreature(Creature* /*killer*/, Player* killed) override
    {
        sECWorldEventMgr->OnPlayerDeath(killed, nullptr);
    }
};

// =============================================================================
// CommandScript - .ecevent commands
// =============================================================================

class ECWorldEvent_CommandScript : public CommandScript
{
public:
    ECWorldEvent_CommandScript() : CommandScript("ECWorldEvent_CommandScript") { }

    Acore::ChatCommands::ChatCommandTable GetCommands() const override
    {
        static Acore::ChatCommands::ChatCommandTable ecEventDebugTable =
        {
            { "on",          HandleDebugOnCommand,          SEC_GAMEMASTER, Acore::ChatCommands::Console::Yes },
            { "off",         HandleDebugOffCommand,         SEC_GAMEMASTER, Acore::ChatCommands::Console::Yes },
            { "level",       HandleDebugLevelCommand,       SEC_GAMEMASTER, Acore::ChatCommands::Console::Yes },
            { "dump",        HandleDebugDumpCommand,        SEC_GAMEMASTER, Acore::ChatCommands::Console::Yes },
            { "event",       HandleDebugEventCommand,       SEC_GAMEMASTER, Acore::ChatCommands::Console::Yes },
            { "participants",HandleDebugParticipantsCommand,SEC_GAMEMASTER, Acore::ChatCommands::Console::Yes },
        };

        static Acore::ChatCommands::ChatCommandTable ecEventCommandTable =
        {
            { "start",    HandleEventStartCommand,    SEC_GAMEMASTER, Acore::ChatCommands::Console::Yes },
            { "stop",     HandleEventStopCommand,     SEC_GAMEMASTER, Acore::ChatCommands::Console::Yes },
            { "status",   HandleEventStatusCommand,   SEC_PLAYER,     Acore::ChatCommands::Console::Yes },
            { "list",     HandleEventListCommand,     SEC_PLAYER,     Acore::ChatCommands::Console::Yes },
            { "reload",   HandleEventReloadCommand,   SEC_GAMEMASTER, Acore::ChatCommands::Console::Yes },
            { "escalate", HandleEventEscalateCommand, SEC_GAMEMASTER, Acore::ChatCommands::Console::Yes },
            { "debug",    ecEventDebugTable },
            { "help",     HandleEventHelpCommand,     SEC_PLAYER,     Acore::ChatCommands::Console::Yes },
        };

        static Acore::ChatCommands::ChatCommandTable commandTable =
        {
            { "ecevent", ecEventCommandTable },
        };

        return commandTable;
    }

    // .ecevent start <event_id>
    static bool HandleEventStartCommand(ChatHandler* handler, uint32 eventId)
    {
        if (!sECWorldEventMgr->IsEnabled())
        {
            handler->PSendSysMessage("|cffff0000EC World Events is disabled.|r");
            return true;
        }

        Player* player = handler->GetPlayer();

        if (sECWorldEventMgr->StartEvent(eventId, player))
        {
            const EventDefinition* def = sECWorldEventMgr->GetEventDefinition(eventId);
            handler->PSendSysMessage("|cff00ff00Event '{}' started successfully!|r",
                def ? def->name.c_str() : "Unknown");
        }
        else
        {
            handler->PSendSysMessage("|cffff0000Failed to start event {}. Check logs for details.|r", eventId);
        }

        return true;
    }

    // .ecevent stop <instance_id>
    static bool HandleEventStopCommand(ChatHandler* handler, uint32 instanceId)
    {
        if (!sECWorldEventMgr->IsEnabled())
        {
            handler->PSendSysMessage("|cffff0000EC World Events is disabled.|r");
            return true;
        }

        Player* player = handler->GetPlayer();

        if (sECWorldEventMgr->StopEvent(instanceId, player))
        {
            handler->PSendSysMessage("|cff00ff00Event instance {} stopped.|r", instanceId);
        }
        else
        {
            handler->PSendSysMessage("|cffff0000Failed to stop event instance {}.|r", instanceId);
        }

        return true;
    }

    // .ecevent status
    static bool HandleEventStatusCommand(ChatHandler* handler)
    {
        if (!sECWorldEventMgr->IsEnabled())
        {
            handler->PSendSysMessage("|cffff0000EC World Events is disabled.|r");
            return true;
        }

        auto activeEvents = sECWorldEventMgr->GetAllActiveEvents();

        if (activeEvents.empty())
        {
            handler->PSendSysMessage("|cff00ff00No active events.|r");
            return true;
        }

        handler->PSendSysMessage("|cff00ff00=== Active Events ({}) ===|r", activeEvents.size());

        for (const ActiveEvent* event : activeEvents)
        {
            const EventDefinition* def = sECWorldEventMgr->GetEventDefinition(event->eventId);
            if (!def)
                continue;

            auto now = std::chrono::system_clock::now();
            int64 eventRemainingMinutes = ClampToNonNegativeMinutes(event->endTime - now);
            int64 phaseRemainingMinutes = ClampToNonNegativeMinutes(event->phaseEndTime - now);

            uint32 totalPhases = GetTotalPhases(*def);
            uint32 phaseIndex = GetCurrentPhaseIndex(*event, *def);

            EventPhase finalPhase = (def->endingMinutes > 0) ? EventPhase::ENDING : EventPhase::ACTIVE;
            std::chrono::system_clock::time_point finalPhaseStart =
                (def->endingMinutes > 0)
                    ? (event->endTime - std::chrono::minutes(def->endingMinutes))
                    : (def->preparationMinutes > 0
                        ? (event->startTime + std::chrono::minutes(def->preparationMinutes))
                        : event->startTime);

            bool finalPhaseActive = now >= finalPhaseStart;
            int64 finalPhaseStartsInMinutes = ClampToNonNegativeMinutes(finalPhaseStart - now);

            handler->PSendSysMessage("  [{}] {} - {} ({} participants, {} kills, {} min remaining total)",
                event->instanceId,
                def->name.c_str(),
                WorldEventManager::EventStateToString(event->state).c_str(),
                event->participants.size(),
                event->totalKills,
                eventRemainingMinutes);

            if (def->type == EventType::RAID && event->townPopulationStart > 0)
            {
                uint32 npcLossPct = event->townCasualties * 100 / event->townPopulationStart;
                handler->PSendSysMessage(
                    "      Settlement: {} NPC casualties {}/{} ({}%); live attackers {}; live defenders {}; sacking={}",
                    def->location.name.c_str(), event->townCasualties, event->townPopulationStart, npcLossPct,
                    event->presentAttackers.size(), event->presentDefenders.size(), event->raidSacking ? "yes" : "no");
                uint32 columnsActive = 0;
                uint32 columnsArrived = 0;
                uint32 columnsFailed = 0;
                for (auto const& column : event->regionalColumns)
                {
                    if (column.arrived)
                        ++columnsArrived;
                    else if (column.failed)
                        ++columnsFailed;
                    else if (column.dispatched)
                        ++columnsActive;
                }
                handler->PSendSysMessage(
                    "      Regional support: {} marching, {} arrived, {} intercepted",
                    columnsActive, columnsArrived, columnsFailed);
            }

            handler->PSendSysMessage("      Phase {}/{}: {} ({} min remaining in phase; phase ends at {})",
                phaseIndex,
                totalPhases,
                WorldEventManager::EventPhaseToString(event->phase).c_str(),
                phaseRemainingMinutes,
                FormatLocalTimestamp(event->phaseEndTime).c_str());

            if (finalPhaseActive)
            {
                handler->PSendSysMessage("      Final phase: {} (already active since {})",
                    WorldEventManager::EventPhaseToString(finalPhase).c_str(),
                    FormatLocalTimestamp(finalPhaseStart).c_str());
            }
            else
            {
                handler->PSendSysMessage("      Final phase: {} (starts in {} min at {})",
                    WorldEventManager::EventPhaseToString(finalPhase).c_str(),
                    finalPhaseStartsInMinutes,
                    FormatLocalTimestamp(finalPhaseStart).c_str());
            }
        }

        return true;
    }

    // .ecevent list
    static bool HandleEventListCommand(ChatHandler* handler)
    {
        if (!sECWorldEventMgr->IsEnabled())
        {
            handler->PSendSysMessage("|cffff0000EC World Events is disabled.|r");
            return true;
        }

        auto definitions = sECWorldEventMgr->GetAllEventDefinitions();

        if (definitions.empty())
        {
            handler->PSendSysMessage("|cffff9900No event definitions found.|r");
            return true;
        }

        handler->PSendSysMessage("|cff00ff00=== Available Events ({}) ===|r", definitions.size());

        for (const EventDefinition* def : definitions)
        {
            handler->PSendSysMessage("  [{}] {} - {} at {} (L{}-{}, {}-{} bots)",
                def->eventId,
                def->name.c_str(),
                WorldEventManager::EventTypeToString(def->type).c_str(),
                def->location.name.c_str(),
                def->minLevel, def->maxLevel,
                def->minBots, def->maxBots);
        }

        return true;
    }

    // .ecevent reload
    static bool HandleEventReloadCommand(ChatHandler* handler)
    {
        sECWorldEventMgr->ReloadEvents();
        handler->PSendSysMessage("|cff00ff00Event definitions reloaded.|r");
        return true;
    }

    // .ecevent escalate <instance_id>
    static bool HandleEventEscalateCommand(ChatHandler* handler, uint32 instanceId)
    {
        if (!sECWorldEventMgr->IsEnabled())
        {
            handler->PSendSysMessage("|cffff0000EC World Events is disabled.|r");
            return true;
        }

        if (sECWorldEventMgr->EscalateEvent(instanceId))
        {
            handler->PSendSysMessage("|cff00ff00Event instance {} escalated.|r", instanceId);
        }
        else
        {
            handler->PSendSysMessage("|cffff0000Failed to escalate event instance {}.|r", instanceId);
        }

        return true;
    }

    // ==========================================================================
    // DEBUG COMMANDS
    // ==========================================================================

    // .ecevent debug on
    static bool HandleDebugOnCommand(ChatHandler* handler)
    {
        sECWorldEventMgr->SetDebugMode(true);
        handler->PSendSysMessage("|cff00ff00EC World Events: Debug mode ENABLED|r");
        handler->PSendSysMessage("Debug messages will now appear in-game for all GMs.");
        return true;
    }

    // .ecevent debug off
    static bool HandleDebugOffCommand(ChatHandler* handler)
    {
        sECWorldEventMgr->SetDebugMode(false);
        handler->PSendSysMessage("|cffff9900EC World Events: Debug mode DISABLED|r");
        return true;
    }

    // .ecevent debug level <0-4>
    static bool HandleDebugLevelCommand(ChatHandler* handler, uint8 level)
    {
        if (level > 4)
        {
            handler->PSendSysMessage("|cffff0000Invalid log level. Use 0-4:|r");
            handler->PSendSysMessage("  0 = Errors only");
            handler->PSendSysMessage("  1 = Errors + Warnings");
            handler->PSendSysMessage("  2 = Errors + Warnings + Info");
            handler->PSendSysMessage("  3 = All + Debug");
            handler->PSendSysMessage("  4 = All + Trace (very verbose)");
            return true;
        }

        sECWorldEventMgr->SetDebugLogLevel(level);
        handler->PSendSysMessage("|cff00ff00EC World Events: Debug log level set to %u|r", level);
        return true;
    }

    // .ecevent debug dump
    static bool HandleDebugDumpCommand(ChatHandler* handler)
    {
        handler->PSendSysMessage("|cff00ffff=== EC WORLD EVENTS STATE DUMP ===|r");
        handler->PSendSysMessage("Module Enabled: %s", sECWorldEventMgr->IsEnabled() ? "YES" : "NO");
        handler->PSendSysMessage("Debug Mode: %s", sECWorldEventMgr->IsDebugMode() ? "ON" : "OFF");
        handler->PSendSysMessage("Debug Log Level: %u", sECWorldEventMgr->GetDebugLogLevel());
        handler->PSendSysMessage("Active Events: %u", sECWorldEventMgr->GetActiveEventCount());

        auto definitions = sECWorldEventMgr->GetAllEventDefinitions();
        handler->PSendSysMessage("Event Definitions: %zu", definitions.size());

        auto activeEvents = sECWorldEventMgr->GetAllActiveEvents();
        if (!activeEvents.empty())
        {
            handler->PSendSysMessage("|cff00ff00--- Active Events ---");
            for (const auto* event : activeEvents)
            {
                const auto* def = sECWorldEventMgr->GetEventDefinition(event->eventId);
                handler->PSendSysMessage("  [%u] %s - %s, Kills: %u, Participants: %zu",
                    event->instanceId,
                    def ? def->name.c_str() : "Unknown",
                    WorldEventManager::EventStateToString(event->state).c_str(),
                    event->totalKills,
                    event->participants.size());
            }
        }

        // Also dump to server logs
        sECWorldEventMgr->DebugDumpState();

        handler->PSendSysMessage("|cff00ffff================================|r");
        return true;
    }

    // .ecevent debug event <instance_id>
    static bool HandleDebugEventCommand(ChatHandler* handler, uint32 instanceId)
    {
        const auto* event = sECWorldEventMgr->GetActiveEvent(instanceId);
        if (!event)
        {
            handler->PSendSysMessage("|cffff0000Event instance %u not found.|r", instanceId);
            return true;
        }

        const auto* def = sECWorldEventMgr->GetEventDefinition(event->eventId);

        handler->PSendSysMessage("|cff00ffff=== EVENT DUMP: Instance %u ===|r", instanceId);
        handler->PSendSysMessage("Event ID: %u", event->eventId);
        handler->PSendSysMessage("Name: %s", def ? def->name.c_str() : "Unknown");
        handler->PSendSysMessage("State: %s", WorldEventManager::EventStateToString(event->state).c_str());
        handler->PSendSysMessage("Total Kills: %u", event->totalKills);
        handler->PSendSysMessage("Escalation Level: %u", event->escalationLevel);
        handler->PSendSysMessage("Participants: %zu", event->participants.size());
        handler->PSendSysMessage("Spawned Bots: %zu", event->spawnedBots.size());

        if (def)
        {
            handler->PSendSysMessage("|cff888888--- Definition ---");
            handler->PSendSysMessage("  Location: %s", def->location.name.c_str());
            handler->PSendSysMessage("  Zone: %u, Area: %u", def->location.zoneId, def->location.areaId);
            handler->PSendSysMessage("  Position: (%.1f, %.1f, %.1f)", def->location.x, def->location.y, def->location.z);
            handler->PSendSysMessage("  Bots: %u - %u", def->minBots, def->maxBots);
            handler->PSendSysMessage("  Duration: %u min", def->durationMinutes);
        }

        // Also dump to server logs
        sECWorldEventMgr->DebugDumpEvent(instanceId);

        return true;
    }

    // .ecevent debug participants <instance_id>
    static bool HandleDebugParticipantsCommand(ChatHandler* handler, uint32 instanceId)
    {
        const auto* event = sECWorldEventMgr->GetActiveEvent(instanceId);
        if (!event)
        {
            handler->PSendSysMessage("|cffff0000Event instance %u not found.|r", instanceId);
            return true;
        }

        handler->PSendSysMessage("|cff00ffff=== PARTICIPANTS: Instance %u (%zu total) ===|r",
            instanceId, event->participants.size());

        for (const auto& pair : event->participants)
        {
            const auto& p = pair.second;
            handler->PSendSysMessage("  %s: K=%u D=%u Dmg=%llu Heal=%llu %s",
                p.playerName.c_str(),
                p.kills, p.deaths,
                p.damageDealt, p.healingDone,
                p.rewarded ? "|cff00ff00[REWARDED]|r" : "");
        }

        // Also dump to server logs
        sECWorldEventMgr->DebugDumpParticipants(instanceId);

        return true;
    }

    // ==========================================================================
    // HELP COMMAND
    // ==========================================================================

    // .ecevent help
    static bool HandleEventHelpCommand(ChatHandler* handler)
    {
        handler->PSendSysMessage("|cff00ff00=== EC World Events Commands ===|r");
        handler->PSendSysMessage("|cffffffffBasic Commands:|r");
        handler->PSendSysMessage("  .ecevent list              - List all available events");
        handler->PSendSysMessage("  .ecevent status            - Show active events");
        handler->PSendSysMessage("|cffffffffGM Commands:|r");
        handler->PSendSysMessage("  .ecevent start <id>        - Start an event");
        handler->PSendSysMessage("  .ecevent stop <instance>   - Stop an active event");
        handler->PSendSysMessage("  .ecevent escalate <inst>   - Escalate an event");
        handler->PSendSysMessage("  .ecevent reload            - Reload event definitions");
        handler->PSendSysMessage("|cff00ffffDebug Commands:|r");
        handler->PSendSysMessage("  .ecevent debug on          - Enable debug mode (shows in-game)");
        handler->PSendSysMessage("  .ecevent debug off         - Disable debug mode");
        handler->PSendSysMessage("  .ecevent debug level <0-4> - Set log verbosity");
        handler->PSendSysMessage("  .ecevent debug dump        - Dump module state");
        handler->PSendSysMessage("  .ecevent debug event <id>  - Dump event details");
        handler->PSendSysMessage("  .ecevent debug participants <id> - Dump participants");
        return true;
    }
};

// =============================================================================
// Module loader registration
// =============================================================================

void AddECWorldEventScripts()
{
    new ECWorldEvent_WorldScript();
    new ECWorldEvent_PlayerScript();
    new ECWorldEvent_CommandScript();
}
