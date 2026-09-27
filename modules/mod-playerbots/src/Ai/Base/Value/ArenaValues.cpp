/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 *
 * EC Arena Values - Value wrappers for arena coordinator data.
 * Custom code by EC (kirekire95).
 */

#include "ArenaValues.h"

#include "ArenaCoordinator.h"
#include "Battleground.h"
#include "Log.h"
#include "PlayerbotAI.h"
#include "Playerbots.h"

ArenaTeamState* ArenaTeamStateValue::Calculate()
{
    if (!bot->InArena())
    {
        LOG_DEBUG("playerbots", "[EC Arena] {} ArenaTeamStateValue - not in arena, returning null", bot->GetName());
        return nullptr;
    }

    Battleground* bg = bot->GetBattleground();
    if (!bg)
    {
        LOG_DEBUG("playerbots", "[EC Arena] {} ArenaTeamStateValue - no Battleground, returning null", bot->GetName());
        return nullptr;
    }

    ArenaTeamState* state = sArenaCoordinator->GetTeamState(bg->GetInstanceID(), bot->GetBgTeamId());
    LOG_DEBUG("playerbots", "[EC Arena] {} ArenaTeamStateValue - instance={} team={} state={}",
        bot->GetName(), bg->GetInstanceID(), bot->GetBgTeamId(),
        state ? "OK" : "NULL");
    return state;
}

Unit* ArenaKillTargetValue::Calculate()
{
    ArenaTeamState* state = AI_VALUE(ArenaTeamState*, "arena team state");
    if (!state || state->killTarget.IsEmpty())
    {
        LOG_DEBUG("playerbots", "[EC Arena] {} ArenaKillTargetValue - no state or no kill target set", bot->GetName());
        return nullptr;
    }

    Unit* target = botAI->GetUnit(state->killTarget);
    if (!target || !target->IsAlive())
    {
        LOG_DEBUG("playerbots", "[EC Arena] {} ArenaKillTargetValue - kill target {} is null or dead",
            bot->GetName(), state->killTarget.ToString());
        return nullptr;
    }

    LOG_DEBUG("playerbots", "[EC Arena] {} ArenaKillTargetValue = {} (hp={:.0f}%)",
        bot->GetName(), target->GetName(), target->GetHealthPct());
    return target;
}

bool ArenaIsMyInterruptTurnValue::Calculate()
{
    ArenaTeamState* state = AI_VALUE(ArenaTeamState*, "arena team state");
    if (!state)
    {
        LOG_DEBUG("playerbots", "[EC Arena] {} ArenaIsMyInterruptTurn - no state", bot->GetName());
        return false;
    }

    if (state->interruptRotation.empty())
    {
        LOG_DEBUG("playerbots", "[EC Arena] {} ArenaIsMyInterruptTurn - interrupt rotation is empty", bot->GetName());
        return false;
    }

    ObjectGuid nextInterrupter = sArenaCoordinator->GetNextInterrupter(state);
    bool isMyTurn = nextInterrupter == bot->GetGUID();

    if (isMyTurn)
    {
        LOG_DEBUG("playerbots", "[EC Arena] {} IS the next interrupter (rotation size={})",
            bot->GetName(), state->interruptRotation.size());
    }

    return isMyTurn;
}

bool ArenaAmIFocusedValue::Calculate()
{
    if (!bot->InArena() || !bot->IsInCombat())
    {
        LOG_DEBUG("playerbots", "[EC Arena] {} ArenaAmIFocused - not in arena or not in combat", bot->GetName());
        return false;
    }

    Battleground* bg = bot->GetBattleground();
    if (!bg)
    {
        LOG_DEBUG("playerbots", "[EC Arena] {} ArenaAmIFocused - no Battleground", bot->GetName());
        return false;
    }

    uint32 attackerCount = 0;
    std::string attackerNames;
    for (auto const& [guid, bgPlayer] : bg->GetPlayers())
    {
        Player* player = ObjectAccessor::FindPlayer(guid);
        if (!player || !player->IsAlive())
            continue;

        if (player->GetBgTeamId() == bot->GetBgTeamId())
            continue;  // Skip teammates

        if (player->GetVictim() == bot)
        {
            attackerCount++;
            if (!attackerNames.empty())
                attackerNames += ", ";
            attackerNames += player->GetName();
        }
    }

    if (attackerCount >= 2)
    {
        LOG_DEBUG("playerbots", "[EC Arena] {} is being FOCUSED by {} enemies: [{}]",
            bot->GetName(), attackerCount, attackerNames);
    }
    else
    {
        LOG_DEBUG("playerbots", "[EC Arena] {} has {} attacker(s) - not focused (need 2+)",
            bot->GetName(), attackerCount);
    }

    return attackerCount >= 2;
}
