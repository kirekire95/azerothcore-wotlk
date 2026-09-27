/*
 * mod-gtapolice
 * GM chat commands implementation
 */

#include "GTAPoliceCommands.h"
#include "GTAPoliceCarAI.h"
#include "GTAPoliceConfig.h"
#include "GTAPoliceManager.h"
#include "GTAPoliceBridge.h"

#include "Chat.h"
#include "Player.h"
#include "ObjectAccessor.h"

using namespace Acore::ChatCommands;

// ============================================================================
// CONSTRUCTOR
// ============================================================================

gtapolice_commands::gtapolice_commands() : CommandScript("gtapolice_commands") {}

ChatCommandTable gtapolice_commands::GetCommands() const
{
    static ChatCommandTable policeCommandTable =
    {
        { "wanted", HandleWantedCommand, SEC_GAMEMASTER, Console::No },
        { "pursue", HandlePursueCommand, SEC_GAMEMASTER, Console::No },
        { "arrest", HandleArrestCommand, SEC_GAMEMASTER, Console::No },
        { "status", HandleStatusCommand, SEC_GAMEMASTER, Console::No },
        { "clear",  HandleClearCommand,  SEC_GAMEMASTER, Console::No },
        { "bribe",  HandleBribeCommand,  SEC_GAMEMASTER, Console::No },
        { "heat",   HandleHeatCommand,   SEC_GAMEMASTER, Console::No },
        { "reload", HandleReloadCommand, SEC_GAMEMASTER, Console::No },
    };

    static ChatCommandTable commandTable =
    {
        { "police", policeCommandTable },
    };

    return commandTable;
}

// ============================================================================
// COMMAND HANDLERS
// ============================================================================

bool gtapolice_commands::HandleWantedCommand(ChatHandler* handler, Optional<PlayerIdentifier> target, std::string action)
{
    if (!sGTAPoliceConfig->Enabled)
    {
        handler->SendSysMessage("mod-gtapolice is disabled.");
        return true;
    }

    Player* player = nullptr;
    if (target)
        player = target->GetConnectedPlayer();
    else
        player = handler->GetPlayer();

    if (!player)
    {
        handler->SendSysMessage("Player not found.");
        return false;
    }

    if (action == "on")
    {
        sGTAPoliceMgr->SetPlayerWanted(player, true);
        handler->PSendSysMessage("Wanted: ON for {}", player->GetName());
        return true;
    }
    if (action == "off")
    {
        sGTAPoliceMgr->SetPlayerWanted(player, false);
        handler->PSendSysMessage("Wanted: OFF for {}", player->GetName());
        return true;
    }

    handler->SendSysMessage("Usage: .police wanted [player] on|off");
    return false;
}

bool gtapolice_commands::HandlePursueCommand(ChatHandler* handler, Optional<PlayerIdentifier> target)
{
    if (!sGTAPoliceConfig->Enabled)
    {
        handler->SendSysMessage("mod-gtapolice is disabled.");
        return true;
    }

    Player* player = nullptr;
    if (target)
        player = target->GetConnectedPlayer();
    else
        player = handler->GetPlayer();

    if (!player)
    {
        handler->SendSysMessage("Player not found.");
        return false;
    }

    if (!sGTAPoliceMgr->StartPursuit(player, nullptr, /*gmOverride=*/true))
    {
        handler->SendSysMessage("Pursuit failed. Player may be on cooldown or not arrestable.");
        return true;
    }

    // Start all cars' chase
    auto data = sGTAPoliceMgr->GetPursuitData(player->GetGUID());
    if (data.has_value())
    {
        for (const auto& carGuid : data->carGuids)
        {
            Creature* car = ObjectAccessor::GetCreature(*player, carGuid);
            if (car)
            {
                if (auto* carAI = dynamic_cast<npc_gtapolice_car::CarAI*>(car->AI()))
                {
                    carAI->SetTarget(player->GetGUID());
                    carAI->StartChase();
                }
            }
        }
    }

    handler->PSendSysMessage("Pursuit started for {}.", player->GetName());
    return true;
}

bool gtapolice_commands::HandleArrestCommand(ChatHandler* handler, Optional<PlayerIdentifier> target)
{
    if (!sGTAPoliceConfig->Enabled)
    {
        handler->SendSysMessage("mod-gtapolice is disabled.");
        return true;
    }

    Player* player = nullptr;
    if (target)
        player = target->GetConnectedPlayer();
    else
        player = handler->GetPlayer();

    if (!player)
    {
        handler->SendSysMessage("Player not found.");
        return false;
    }

    if (!GTAPoliceManager::IsPlayerArrestable(player, /*gmOverride=*/true))
    {
        handler->SendSysMessage("Target is not arrestable right now.");
        return true;
    }

    if (!sGTAPoliceMgr->StartImmediateArrest(
            player, handler->GetSession()->GetPlayer(), /*gmOverride=*/true))
    {
        handler->SendSysMessage("Arrest failed. Check VehicleId/seats for the car.");
        return true;
    }

    handler->PSendSysMessage("Arrested {}.", player->GetName());
    return true;
}

bool gtapolice_commands::HandleStatusCommand(ChatHandler* handler)
{
    if (!sGTAPoliceConfig->Enabled)
    {
        handler->SendSysMessage("mod-gtapolice is disabled.");
        return true;
    }

    uint32 pursuitCount = sGTAPoliceMgr->GetActivePursuitCount();

    handler->SendSysMessage("=== GTA Police Status ===");
    handler->PSendSysMessage("Enabled: {}", sGTAPoliceConfig->Enabled ? "YES" : "NO");
    handler->PSendSysMessage("Debug Log: {}", sGTAPoliceConfig->DebugLog ? "ON" : "OFF");
    handler->PSendSysMessage("Active Pursuits: {}", pursuitCount);
    handler->PSendSysMessage("Vehicle Mode: {}", sGTAPoliceMgr->IsVehicleModeAvailable() ? "ENABLED" : "DISABLED (fallback)");
    handler->PSendSysMessage("Car Entry: {}, Officer Entry: {}, Crew Entry: {}",
        sGTAPoliceConfig->CarEntry,
        sGTAPoliceConfig->OfficerEntry,
        sGTAPoliceConfig->CrewOfficerEntry);
    handler->PSendSysMessage("Vehicle Seats: Driver={}, Passenger={}, Prisoner={}",
        sGTAPoliceConfig->SeatDriver,
        sGTAPoliceConfig->SeatCrewPassenger,
        sGTAPoliceConfig->SeatPrisoner);

    // Check if calling player is under pursuit
    Player* caller = handler->GetPlayer();
    if (caller)
    {
        bool underPursuit = sGTAPoliceMgr->IsUnderPursuit(caller->GetGUID());
        bool isWanted = sGTAPoliceMgr->IsPlayerWanted(caller);
        uint32 stars = sGTAPoliceMgr->GetPlayerWantedStars(caller);

        handler->PSendSysMessage("Your Status: Wanted={} ({} stars), Under Pursuit={}",
            isWanted ? "YES" : "NO",
            stars,
            underPursuit ? "YES" : "NO");
    }

    return true;
}

bool gtapolice_commands::HandleClearCommand(ChatHandler* handler, Optional<PlayerIdentifier> target)
{
    if (!sGTAPoliceConfig->Enabled)
    {
        handler->SendSysMessage("mod-gtapolice is disabled.");
        return true;
    }

    Player* player = nullptr;
    if (target)
        player = target->GetConnectedPlayer();
    else
        player = handler->GetPlayer();

    if (!player)
    {
        handler->SendSysMessage("Player not found.");
        return false;
    }

    if (!sGTAPoliceMgr->IsUnderPursuit(player->GetGUID()))
    {
        handler->PSendSysMessage("{} is not under pursuit.", player->GetName());
        return true;
    }

    sGTAPoliceMgr->EndPursuit(player->GetGUID(), false);
    handler->PSendSysMessage("Cleared pursuit for {}.", player->GetName());
    return true;
}

// ============================================================================
// BRIBE COMMAND - Force bribe attempt for target player
// Usage: .police bribe [player]
// ============================================================================

bool gtapolice_commands::HandleBribeCommand(ChatHandler* handler, Optional<PlayerIdentifier> target)
{
    if (!sGTAPoliceConfig->Enabled)
    {
        handler->SendSysMessage("mod-gtapolice is disabled.");
        return true;
    }

    if (!sGTAPoliceConfig->BribeEnabled)
    {
        handler->SendSysMessage("Bribe system is disabled in config.");
        return true;
    }

    Player* player = nullptr;
    if (target)
        player = target->GetConnectedPlayer();
    else
        player = handler->GetPlayer();

    if (!player)
    {
        handler->SendSysMessage("Player not found.");
        return false;
    }

    uint32 stars = sGTAPoliceMgr->GetPlayerWantedStars(player);
    if (stars == 0)
    {
        handler->PSendSysMessage("{} is not wanted.", player->GetName());
        return true;
    }

    if (!sGTAPoliceMgr->CanBribe(player))
    {
        handler->PSendSysMessage("{} cannot bribe right now (cooldown or too many stars).", player->GetName());
        return true;
    }

    uint32 cost = sGTAPoliceMgr->GetBribeCost(player);
    uint32 goldCost = cost / 10000;

    if (sGTAPoliceMgr->AttemptBribe(player))
    {
        handler->PSendSysMessage("Bribe successful for {}. Cost: {} gold. Stars: {} -> {}.",
            player->GetName(), goldCost, stars, sGTAPoliceMgr->GetPlayerWantedStars(player));
    }
    else
    {
        handler->PSendSysMessage("Bribe failed for {} (insufficient gold: needs {} gold).",
            player->GetName(), goldCost);
    }

    return true;
}

// ============================================================================
// HEAT COMMAND - Show or modify zone heat
// Usage: .police heat [zoneId]
// ============================================================================

bool gtapolice_commands::HandleHeatCommand(ChatHandler* handler, Optional<uint32> zoneId)
{
    if (!sGTAPoliceConfig->Enabled)
    {
        handler->SendSysMessage("mod-gtapolice is disabled.");
        return true;
    }

    if (!sGTAPoliceConfig->ZoneHeatEnabled)
    {
        handler->SendSysMessage("Zone heat system is disabled in config.");
        return true;
    }

    // If no zone specified, use calling player's current zone
    uint32 zone = 0;
    if (zoneId)
    {
        zone = *zoneId;
    }
    else
    {
        Player* caller = handler->GetPlayer();
        if (caller)
            zone = caller->GetZoneId();
    }

    if (zone == 0)
    {
        handler->SendSysMessage("Invalid zone. Usage: .police heat [zoneId]");
        return false;
    }

    uint32 heat = sGTAPoliceMgr->GetZoneHeat(zone);
    uint8 level = sGTAPoliceMgr->GetZoneHeatLevel(zone);

    const char* levelStr = "COLD";
    if (level == 1) levelStr = "WARM";
    else if (level >= 2) levelStr = "HOT";

    handler->SendSysMessage("=== Zone Heat ===");
    handler->PSendSysMessage("Zone ID: {}", zone);
    handler->PSendSysMessage("Heat: {} / {} (high threshold)", heat, sGTAPoliceConfig->ZoneHeatThresholdHigh);
    handler->PSendSysMessage("Level: {} ({})", levelStr, level);
    handler->PSendSysMessage("Thresholds: Low={}, High={}",
        sGTAPoliceConfig->ZoneHeatThresholdLow,
        sGTAPoliceConfig->ZoneHeatThresholdHigh);

    return true;
}

// ============================================================================
// RELOAD COMMAND - Reload configuration from file
// Usage: .police reload
// ============================================================================

bool gtapolice_commands::HandleReloadCommand(ChatHandler* handler)
{
    sGTAPoliceConfig->Reload();
    handler->SendSysMessage("[mod-gtapolice] Configuration reloaded.");
    return true;
}
