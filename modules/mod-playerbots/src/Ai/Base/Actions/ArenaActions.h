/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 *
 * EC Arena Actions - Arena-specific bot actions for playerbots.
 * Custom code by EC (kirekire95).
 */

#ifndef _PLAYERBOT_ARENAACTIONS_H
#define _PLAYERBOT_ARENAACTIONS_H

#include "Action.h"
#include "AttackAction.h"
#include "MovementActions.h"

class PlayerbotAI;

// Scans enemies, classifies healers, assigns kill target, CC, interrupt rotation
class ArenaComputePlanAction : public Action
{
public:
    ArenaComputePlanAction(PlayerbotAI* botAI)
        : Action(botAI, "arena compute plan") {}

    bool Execute(Event event) override;
};

// Switches attack to the coordinator's kill target
class ArenaFocusKillTargetAction : public AttackAction
{
public:
    ArenaFocusKillTargetAction(PlayerbotAI* botAI)
        : AttackAction(botAI, "arena focus kill target") {}

    bool Execute(Event event) override;
    bool isUseful() override;
};

// Uses this bot's class interrupt spell on a casting enemy healer
class ArenaCoordinatedInterruptAction : public Action
{
public:
    ArenaCoordinatedInterruptAction(PlayerbotAI* botAI)
        : Action(botAI, "arena coordinated interrupt") {}

    bool Execute(Event event) override;
};

// Casts the assigned CC spell on the assigned enemy
class ArenaExecuteCCAction : public Action
{
public:
    ArenaExecuteCCAction(PlayerbotAI* botAI)
        : Action(botAI, "arena execute cc") {}

    bool Execute(Event event) override;
};

// Uses class-specific defensive cooldowns when focused
class ArenaDefensiveAction : public Action
{
public:
    ArenaDefensiveAction(PlayerbotAI* botAI)
        : Action(botAI, "arena defensive") {}

    bool Execute(Event event) override;

private:
    bool TryCastDefensive(std::string const& spell);
};

// CCs an enemy attacking a focused teammate
class ArenaPeelAction : public Action
{
public:
    ArenaPeelAction(PlayerbotAI* botAI)
        : Action(botAI, "arena peel") {}

    bool Execute(Event event) override;
};

#endif
