/*
 * mod-alive-world - Module Loader
 * Copyright (C) 2025 EC
 *
 * Entry point for AzerothCore module system.
 */

// Forward declaration from AliveWorldScripts.cpp
void AddSC_mod_alive_world();

// Module entry point - called by AzerothCore module system
// Name must match: Addmod_<folder_name>Scripts()
void Addmod_alive_worldScripts()
{
    AddSC_mod_alive_world();
}
