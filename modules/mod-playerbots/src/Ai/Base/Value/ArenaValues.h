/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 *
 * EC Arena Values - Value wrappers for arena coordinator data.
 * Custom code by EC (kirekire95).
 */

#ifndef _PLAYERBOT_ARENAVALUES_H
#define _PLAYERBOT_ARENAVALUES_H

#include "Value.h"

class PlayerbotAI;
class Unit;
struct ArenaTeamState;

// Returns the ArenaTeamState* for this bot's current arena team
class ArenaTeamStateValue : public CalculatedValue<ArenaTeamState*>
{
public:
    ArenaTeamStateValue(PlayerbotAI* botAI)
        : CalculatedValue(botAI, "arena team state", 1) {}

    ArenaTeamState* Calculate() override;
};

// Returns the kill target Unit* from the coordinator
class ArenaKillTargetValue : public UnitCalculatedValue
{
public:
    ArenaKillTargetValue(PlayerbotAI* botAI)
        : UnitCalculatedValue(botAI, "arena kill target", 1) {}

    Unit* Calculate() override;
};

// Returns true if this bot is the next in the interrupt rotation
class ArenaIsMyInterruptTurnValue : public BoolCalculatedValue
{
public:
    ArenaIsMyInterruptTurnValue(PlayerbotAI* botAI)
        : BoolCalculatedValue(botAI, "arena is my interrupt turn", 1) {}

    bool Calculate() override;
};

// Returns true if this bot is being focused by 2+ enemies
class ArenaAmIFocusedValue : public BoolCalculatedValue
{
public:
    ArenaAmIFocusedValue(PlayerbotAI* botAI)
        : BoolCalculatedValue(botAI, "arena am i focused", 1) {}

    bool Calculate() override;
};

#endif
