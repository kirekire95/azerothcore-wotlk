/*
 * EC Combat Module - Module Loader
 * Kill feed, arena/BG tracking, PvP statistics for AzerothCore
 */

#include "ScriptMgr.h"

// AddSC functions
void AddSC_mod_ec_combat();

// Called when scripts are loaded
void Addmod_ec_combatScripts()
{
    AddSC_mod_ec_combat();
}
