/*
 * mod-gtapolice v2 (Modular)
 * GTA-style police pursuit, arrest, and jail system
 *
 * Architecture:
 * - GTAPoliceConfig:    Loads settings from mod_gtapolice.conf
 * - GTAPoliceBridge:    Emits events to EC Realtime Bridge
 * - GTAPoliceManager:   Singleton managing pursuit state
 * - GTAPoliceCarAI:     Police car vehicle AI (chase, approach, transport)
 * - GTAPoliceOfficerAI: Stationary scanner officer AI
 * - GTAPoliceCrewAI:    Crew officers that ride in car, exit, arrest on foot
 * - GTAPoliceCommands:  GM chat commands (.wanted, .pursue, .arrest)
 * - GTAPoliceHelpers:   Shared utility functions
 *
 * This file: Script registration and world script only.
 */

#include "ScriptMgr.h"
#include "GTAPoliceConfig.h"
#include "GTAPoliceManager.h"
#include "Log.h"

// Forward declarations for script registration
#include "GTAPoliceCarAI.h"
#include "GTAPoliceOfficerAI.h"
#include "GTAPoliceCrewAI.h"
#include "GTAPoliceCommands.h"
#include "GTAPolicePaynSpray.h"

// ============================================================================
// PLAYER SCRIPT - Cleanup on logout
// ============================================================================

class gtapolice_player_script : public PlayerScript
{
public:
    gtapolice_player_script() : PlayerScript("gtapolice_player_script") {}

    void OnPlayerLogout(Player* player) override
    {
        if (!player || !sGTAPoliceConfig->Enabled)
            return;

        // Clean up any active pursuit when player logs out
        if (sGTAPoliceMgr->IsUnderPursuit(player->GetGUID()))
        {
            LOG_DEBUG("module", "[mod-gtapolice] Player {} logged out during pursuit, cleaning up",
                player->GetName());
            sGTAPoliceMgr->EndPursuit(player->GetGUID(), false);
        }
    }

    void OnPlayerResurrect(Player* player, float /*restore_percent*/, bool& /*applySickness*/) override
    {
        if (!player || !sGTAPoliceConfig->Enabled)
            return;

        // Clear wanted on death/resurrect (configurable behavior)
        // For now just log it; wanted persists by default
        if (sGTAPoliceMgr->IsUnderPursuit(player->GetGUID()))
        {
            LOG_DEBUG("module", "[mod-gtapolice] Player {} resurrected during pursuit",
                player->GetName());
        }
    }
};

// ============================================================================
// WORLD SCRIPT - Initialization
// ============================================================================

class gtapolice_world_script : public WorldScript
{
public:
    gtapolice_world_script() : WorldScript("gtapolice_world_script") {}

    void OnStartup() override
    {
        if (sGTAPoliceConfig->Enabled)
        {
            sGTAPoliceMgr->Initialize();
            LOG_INFO("module", "[mod-gtapolice] Module initialized (v2 - modular)");
        }
    }

    void OnUpdate(uint32 diff) override
    {
        if (sGTAPoliceConfig->Enabled)
        {
            sGTAPoliceMgr->Update(diff);
        }
    }

    void OnShutdown() override
    {
        sGTAPoliceMgr->Shutdown();
    }
};

// ============================================================================
// SCRIPT REGISTRATION
// ============================================================================

void Addmod_gtapoliceScripts()
{
    new npc_gtapolice_car();
    new npc_gtapolice_officer();
    new npc_gtapolice_crew();
    new npc_gtapolice_paynspray();
    new gtapolice_commands();
    new gtapolice_world_script();
    new gtapolice_player_script();
}
