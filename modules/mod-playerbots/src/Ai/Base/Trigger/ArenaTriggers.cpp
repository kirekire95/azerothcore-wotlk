/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 *
 * EC Arena Triggers - Arena-specific trigger conditions for playerbots.
 * Custom code by EC (kirekire95).
 */

#include "ArenaTriggers.h"

#include "ArenaCoordinator.h"
#include "Battleground.h"
#include "Log.h"
#include "PlayerbotAI.h"
#include "Playerbots.h"
#include "Spell.h"
#include "SpellInfo.h"
#include "Timer.h"

static constexpr uint32 PLAN_REFRESH_INTERVAL_MS = 3000;  // Recompute plan every 3s

// ============================================================================
// ArenaNeedsPlanTrigger
// ============================================================================

bool ArenaNeedsPlanTrigger::IsActive()
{
    if (!bot->InArena() || !bot->IsInCombat() || bot->isDead())
        return false;

    Battleground* bg = bot->GetBattleground();
    if (!bg || bg->GetStatus() != STATUS_IN_PROGRESS)
    {
        LOG_DEBUG("playerbots", "[EC Arena] {} NeedsPlan - BG null or not in progress", bot->GetName());
        return false;
    }

    ArenaTeamState* state = AI_VALUE(ArenaTeamState*, "arena team state");
    if (!state)
    {
        LOG_DEBUG("playerbots", "[EC Arena] {} NeedsPlan - could not get ArenaTeamState", bot->GetName());
        return false;
    }

    // Plan needs computation if never computed or stale
    uint32 now = getMSTime();
    if (!state->planComputed || (now - state->lastPlanTime) > PLAN_REFRESH_INTERVAL_MS)
    {
        LOG_DEBUG("playerbots", "[EC Arena] {} triggers plan recomputation (planComputed={}, stale={}ms)",
            bot->GetName(), state->planComputed ? "true" : "false",
            state->planComputed ? (now - state->lastPlanTime) : 0);
        return true;
    }

    return false;
}

// ============================================================================
// ArenaKillTargetAvailableTrigger
// ============================================================================

bool ArenaKillTargetAvailableTrigger::IsActive()
{
    if (!bot->InArena() || !bot->IsInCombat() || bot->isDead())
        return false;

    Unit* killTarget = AI_VALUE(Unit*, "arena kill target");
    if (!killTarget || !killTarget->IsAlive())
    {
        LOG_DEBUG("playerbots", "[EC Arena] {} KillTargetAvailable - no kill target or target dead", bot->GetName());
        return false;
    }

    // Only fire if the kill target differs from current target
    Unit* currentTarget = AI_VALUE(Unit*, "current target");
    if (currentTarget == killTarget)
    {
        LOG_DEBUG("playerbots", "[EC Arena] {} already attacking kill target {}", bot->GetName(), killTarget->GetName());
        return false;
    }

    LOG_INFO("playerbots", "[EC Arena] {} should switch to kill target {} (currently attacking: {})",
        bot->GetName(), killTarget->GetName(),
        currentTarget ? currentTarget->GetName() : "nothing");
    return true;
}

// ============================================================================
// ArenaEnemyHealerCastingTrigger
// ============================================================================

bool ArenaEnemyHealerCastingTrigger::IsActive()
{
    if (!bot->InArena() || !bot->IsInCombat() || bot->isDead())
        return false;

    // Only fire if this bot is next in the interrupt rotation
    if (!AI_VALUE(bool, "arena is my interrupt turn"))
    {
        LOG_DEBUG("playerbots", "[EC Arena] {} HealerCasting - not my interrupt turn", bot->GetName());
        return false;
    }

    ArenaTeamState* state = AI_VALUE(ArenaTeamState*, "arena team state");
    if (!state)
    {
        LOG_DEBUG("playerbots", "[EC Arena] {} HealerCasting - no ArenaTeamState", bot->GetName());
        return false;
    }

    // Check if any enemy healer is casting a heal
    Battleground* bg = bot->GetBattleground();
    if (!bg)
        return false;

    for (auto const& [guid, bgPlayer] : bg->GetPlayers())
    {
        Player* enemy = ObjectAccessor::FindPlayer(guid);
        if (!enemy || !enemy->IsAlive())
            continue;

        if (enemy->GetBgTeamId() == bot->GetBgTeamId())
            continue;  // Skip teammates

        // Check if this enemy is a known healer
        auto enemyIt = state->enemies.find(enemy->GetGUID());
        if (enemyIt == state->enemies.end() || !enemyIt->second.isHealer)
            continue;

        // Check if casting a positive (healing) spell
        Spell* genericSpell = enemy->GetCurrentSpell(CURRENT_GENERIC_SPELL);
        if (genericSpell && genericSpell->m_spellInfo && genericSpell->m_spellInfo->IsPositive())
        {
            // Check if the spell is interruptible
            if (!genericSpell->m_spellInfo->HasAttribute(SPELL_ATTR0_NO_IMMUNITIES) &&
                !(genericSpell->m_spellInfo->InterruptFlags == 0))
            {
                LOG_INFO("playerbots", "[EC Arena] {} detected enemy healer {} casting heal '{}' (id={}) - MY TURN TO INTERRUPT!",
                    bot->GetName(), enemy->GetName(),
                    genericSpell->m_spellInfo->SpellName[LOCALE_enUS],
                    genericSpell->m_spellInfo->Id);
                return true;
            }
            else
            {
                LOG_DEBUG("playerbots", "[EC Arena] {} healer {} casting '{}' but spell is uninterruptible (InterruptFlags={})",
                    bot->GetName(), enemy->GetName(),
                    genericSpell->m_spellInfo->SpellName[LOCALE_enUS],
                    genericSpell->m_spellInfo->InterruptFlags);
            }
        }

        Spell* channeledSpell = enemy->GetCurrentSpell(CURRENT_CHANNELED_SPELL);
        if (channeledSpell && channeledSpell->m_spellInfo && channeledSpell->m_spellInfo->IsPositive())
        {
            LOG_INFO("playerbots", "[EC Arena] {} detected enemy healer {} channeling heal '{}' (id={}) - MY TURN TO INTERRUPT!",
                bot->GetName(), enemy->GetName(),
                channeledSpell->m_spellInfo->SpellName[LOCALE_enUS],
                channeledSpell->m_spellInfo->Id);
            return true;
        }
    }

    LOG_DEBUG("playerbots", "[EC Arena] {} is next interrupter but no enemy healer currently casting", bot->GetName());
    return false;
}

// ============================================================================
// ArenaCCAssignedTrigger
// ============================================================================

bool ArenaCCAssignedTrigger::IsActive()
{
    if (!bot->InArena() || !bot->IsInCombat() || bot->isDead())
        return false;

    ArenaTeamState* state = AI_VALUE(ArenaTeamState*, "arena team state");
    if (!state)
    {
        LOG_DEBUG("playerbots", "[EC Arena] {} CCAssigned - no ArenaTeamState", bot->GetName());
        return false;
    }

    if (state->ccAssignments.empty())
    {
        LOG_DEBUG("playerbots", "[EC Arena] {} CCAssigned - no CC assignments exist", bot->GetName());
        return false;
    }

    // Check if this bot has a CC assignment
    for (auto const& [enemyGuid, assignedBot] : state->ccAssignments)
    {
        if (assignedBot != bot->GetGUID())
            continue;

        // Found our assignment. Check if the target is valid for CC.
        auto enemyIt = state->enemies.find(enemyGuid);
        if (enemyIt == state->enemies.end() || !enemyIt->second.isAlive)
        {
            LOG_DEBUG("playerbots", "[EC Arena] {} CC assignment on {} skipped - target dead or not found",
                bot->GetName(), enemyGuid.ToString());
            continue;
        }

        // Don't CC if target is already in CC
        if (enemyIt->second.isInCC)
        {
            Player* enemy = ObjectAccessor::FindPlayer(enemyGuid);
            LOG_DEBUG("playerbots", "[EC Arena] {} CC assignment on {} skipped - target already CC'd",
                bot->GetName(), enemy ? enemy->GetName() : enemyGuid.ToString());
            continue;
        }

        // Check DR immunity for the assigned CC category
        auto catIt = state->ccAssignmentCategory.find(enemyGuid);
        if (catIt != state->ccAssignmentCategory.end())
        {
            if (sArenaCoordinator->IsDRImmune(state, enemyGuid, catIt->second))
            {
                Player* enemy = ObjectAccessor::FindPlayer(enemyGuid);
                LOG_DEBUG("playerbots", "[EC Arena] {} CC assignment on {} ({}) skipped - DR immune to {} (count={})",
                    bot->GetName(), enemy ? enemy->GetName() : enemyGuid.ToString(),
                    GetClassName(enemyIt->second.playerClass),
                    GetDRCategoryName(catIt->second),
                    sArenaCoordinator->GetDRCount(state, enemyGuid, catIt->second));
                continue;
            }
        }

        // Target needs CC
        Player* enemy = ObjectAccessor::FindPlayer(enemyGuid);
        LOG_INFO("playerbots", "[EC Arena] {} has CC assignment on {} ({}) - trigger active (DR category: {})",
            bot->GetName(), enemy ? enemy->GetName() : enemyGuid.ToString(),
            GetClassName(enemyIt->second.playerClass),
            catIt != state->ccAssignmentCategory.end() ? GetDRCategoryName(catIt->second) : "none");
        return true;
    }

    LOG_DEBUG("playerbots", "[EC Arena] {} has no valid CC assignment (all targets dead/CC'd/DR-immune or not assigned)",
        bot->GetName());
    return false;
}

// ============================================================================
// ArenaFocusedByEnemyTrigger
// ============================================================================

bool ArenaFocusedByEnemyTrigger::IsActive()
{
    if (!bot->InArena() || !bot->IsInCombat() || bot->isDead())
        return false;

    // Also require low-ish HP to avoid triggering defensive CDs unnecessarily
    float healthPct = bot->GetHealthPct();
    if (healthPct > 50.0f)
    {
        LOG_DEBUG("playerbots", "[EC Arena] {} FocusedByEnemy - HP {:.0f}% > 50%%, not checking focus",
            bot->GetName(), healthPct);
        return false;
    }

    bool focused = AI_VALUE(bool, "arena am i focused");
    if (focused)
    {
        // Update coordinator state
        ArenaTeamState* state = AI_VALUE(ArenaTeamState*, "arena team state");
        if (state)
        {
            state->focusedTeammate = bot->GetGUID();
            state->focusedTeammateTime = getMSTime();
        }

        LOG_INFO("playerbots", "[EC Arena] {} is FOCUSED by 2+ enemies and LOW HP ({:.0f}%) - DEFENSIVE TRIGGER!",
            bot->GetName(), healthPct);
    }
    else
    {
        LOG_DEBUG("playerbots", "[EC Arena] {} at {:.0f}% HP but not focused (< 2 enemies targeting)",
            bot->GetName(), healthPct);
    }

    return focused;
}

// ============================================================================
// ArenaTeammateNeedsPeelTrigger
// ============================================================================

bool ArenaTeammateNeedsPeelTrigger::IsActive()
{
    if (!bot->InArena() || !bot->IsInCombat() || bot->isDead())
        return false;

    ArenaTeamState* state = AI_VALUE(ArenaTeamState*, "arena team state");
    if (!state || state->focusedTeammate.IsEmpty())
    {
        LOG_DEBUG("playerbots", "[EC Arena] {} TeammateNeedsPeel - no state or no focused teammate", bot->GetName());
        return false;
    }

    // Don't peel for ourselves
    if (state->focusedTeammate == bot->GetGUID())
    {
        LOG_DEBUG("playerbots", "[EC Arena] {} TeammateNeedsPeel - focused teammate is self, skipping", bot->GetName());
        return false;
    }

    // Check if the focused teammate is still alive and low HP
    Player* teammate = ObjectAccessor::FindPlayer(state->focusedTeammate);
    if (!teammate || !teammate->IsAlive())
    {
        LOG_DEBUG("playerbots", "[EC Arena] {} TeammateNeedsPeel - focused teammate {} is null or dead",
            bot->GetName(), state->focusedTeammate.ToString());
        return false;
    }

    float teammateHp = teammate->GetHealthPct();
    if (teammateHp > 50.0f)
    {
        LOG_DEBUG("playerbots", "[EC Arena] {} TeammateNeedsPeel - teammate {} HP {:.0f}% > 50%%, no peel needed",
            bot->GetName(), teammate->GetName(), teammateHp);
        return false;
    }

    // Check if the timestamp is fresh (within 5s)
    uint32 now = getMSTime();
    if ((now - state->focusedTeammateTime) > 5000)
    {
        LOG_DEBUG("playerbots", "[EC Arena] {} TeammateNeedsPeel - focus data on {} is stale ({}ms ago)",
            bot->GetName(), teammate->GetName(), now - state->focusedTeammateTime);
        return false;
    }

    LOG_INFO("playerbots", "[EC Arena] {} sees teammate {} needs PEEL (hp={:.0f}%, focused {}ms ago)",
        bot->GetName(), teammate->GetName(), teammateHp, now - state->focusedTeammateTime);
    return true;
}
