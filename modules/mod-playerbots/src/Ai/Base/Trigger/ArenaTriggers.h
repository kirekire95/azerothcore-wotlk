/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 *
 * EC Arena Triggers - Arena-specific trigger conditions for playerbots.
 * Custom code by EC (kirekire95).
 */

#ifndef _PLAYERBOT_ARENATRIGGERS_H
#define _PLAYERBOT_ARENATRIGGERS_H

#include "Trigger.h"

class PlayerbotAI;

// Fires when team plan needs (re)computation: in arena, in combat, plan stale (>3s)
class ArenaNeedsPlanTrigger : public Trigger
{
public:
    ArenaNeedsPlanTrigger(PlayerbotAI* botAI)
        : Trigger(botAI, "arena needs plan", 2) {}

    bool IsActive() override;
};

// Fires when a kill target is assigned and this bot should switch to it
class ArenaKillTargetAvailableTrigger : public Trigger
{
public:
    ArenaKillTargetAvailableTrigger(PlayerbotAI* botAI)
        : Trigger(botAI, "arena kill target available", 1) {}

    bool IsActive() override;
};

// Fires when an enemy healer is casting a heal AND this bot is next in interrupt rotation
class ArenaEnemyHealerCastingTrigger : public Trigger
{
public:
    ArenaEnemyHealerCastingTrigger(PlayerbotAI* botAI)
        : Trigger(botAI, "arena enemy healer casting", 1) {}

    bool IsActive() override;
};

// Fires when this bot has a CC assignment and the target is not currently CC'd
class ArenaCCAssignedTrigger : public Trigger
{
public:
    ArenaCCAssignedTrigger(PlayerbotAI* botAI)
        : Trigger(botAI, "arena cc assigned", 2) {}

    bool IsActive() override;
};

// Fires when 2+ enemies are targeting this bot
class ArenaFocusedByEnemyTrigger : public Trigger
{
public:
    ArenaFocusedByEnemyTrigger(PlayerbotAI* botAI)
        : Trigger(botAI, "arena focused by enemy", 1) {}

    bool IsActive() override;
};

// Fires when a teammate healer is being focused and is low HP
class ArenaTeammateNeedsPeelTrigger : public Trigger
{
public:
    ArenaTeammateNeedsPeelTrigger(PlayerbotAI* botAI)
        : Trigger(botAI, "arena teammate needs peel", 2) {}

    bool IsActive() override;
};

#endif
