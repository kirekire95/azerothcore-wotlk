/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 *
 * EC Arena Actions - Arena-specific bot actions for playerbots.
 * Custom code by EC (kirekire95).
 */

#include "ArenaActions.h"

#include "ArenaCoordinator.h"
#include "Battleground.h"
#include "Log.h"
#include "PlayerbotAI.h"
#include "Playerbots.h"
#include "ServerFacade.h"
#include "Spell.h"
#include "SpellInfo.h"
#include "Timer.h"

// ============================================================================
// ArenaComputePlanAction
// ============================================================================

bool ArenaComputePlanAction::Execute(Event /*event*/)
{
    if (!bot->InArena())
    {
        LOG_DEBUG("playerbots", "[EC Arena] {} ArenaComputePlan skipped - not in arena", bot->GetName());
        return false;
    }

    Battleground* bg = bot->GetBattleground();
    if (!bg || bg->GetStatus() != STATUS_IN_PROGRESS)
    {
        LOG_DEBUG("playerbots", "[EC Arena] {} ArenaComputePlan skipped - BG null or not in progress", bot->GetName());
        return false;
    }

    ArenaTeamState* state = AI_VALUE(ArenaTeamState*, "arena team state");
    if (!state)
    {
        LOG_INFO("playerbots", "[EC Arena] {} ArenaComputePlan FAILED - could not get ArenaTeamState", bot->GetName());
        return false;
    }

    LOG_INFO("playerbots", "[EC Arena] === {} computing arena plan (instance={}, team={}) ===",
        bot->GetName(), bg->GetInstanceID(), bot->GetBgTeamId());

    // --- Populate team members ---
    state->teamMembers.clear();
    Group* myGroup = bot->GetGroup();
    if (myGroup)
    {
        for (GroupReference* ref = myGroup->GetFirstMember(); ref; ref = ref->next())
        {
            Player* member = ref->GetSource();
            if (member && member->IsAlive())
            {
                state->teamMembers.push_back(member->GetGUID());
                LOG_DEBUG("playerbots", "[EC Arena]   Team member: {} ({})",
                    member->GetName(), GetClassName(member->getClass()));
            }
        }
    }
    else
    {
        // Solo (shouldn't happen in arena but handle gracefully)
        state->teamMembers.push_back(bot->GetGUID());
        LOG_INFO("playerbots", "[EC Arena] {} has no group - adding self as solo team member (unusual for arena)", bot->GetName());
    }

    state->bracketSize = static_cast<uint8>(state->teamMembers.size());
    LOG_INFO("playerbots", "[EC Arena] Team size: {} (bracket: {}v{})",
        state->bracketSize, state->bracketSize, state->bracketSize);

    // --- Scan and classify enemies ---
    state->enemies.clear();
    uint32 enemyCount = 0;
    uint32 healerCount = 0;

    for (auto const& [guid, bgPlayer] : bg->GetPlayers())
    {
        Player* player = ObjectAccessor::FindPlayer(guid);
        if (!player || !player->IsAlive())
            continue;

        if (player->GetBgTeamId() == bot->GetBgTeamId())
            continue;  // Skip teammates

        ArenaEnemyState& enemy = state->enemies[player->GetGUID()];
        enemy.guid = player->GetGUID();
        enemy.playerClass = player->getClass();
        enemy.isHealer = IsHealerClass(player->getClass(), player);
        enemy.healthPct = player->GetHealthPct();
        enemy.isAlive = player->IsAlive();

        // Check if currently CC'd (has a loss-of-control aura)
        enemy.isInCC = player->HasUnitState(UNIT_STATE_STUNNED) ||
                       player->HasUnitState(UNIT_STATE_CONFUSED) ||
                       player->HasUnitState(UNIT_STATE_FLEEING) ||
                       player->HasUnitState(UNIT_STATE_ROOT) ||
                       player->HasAuraType(SPELL_AURA_MOD_STUN) ||
                       player->HasAuraType(SPELL_AURA_MOD_CONFUSE) ||
                       player->HasAuraType(SPELL_AURA_MOD_FEAR) ||
                       player->HasAuraType(SPELL_AURA_TRANSFORM);  // Polymorph

        enemyCount++;
        if (enemy.isHealer)
            healerCount++;

        LOG_INFO("playerbots", "[EC Arena]   Enemy: {} ({}) healer={} hp={:.0f}% inCC={}",
            player->GetName(), GetClassName(player->getClass()),
            enemy.isHealer ? "YES" : "no", enemy.healthPct,
            enemy.isInCC ? "YES" : "no");
    }

    LOG_INFO("playerbots", "[EC Arena] Enemies found: {} ({} healer(s))", enemyCount, healerCount);

    // --- Compute kill target ---
    ObjectGuid killTarget = sArenaCoordinator->ComputeKillTarget(state);
    if (!killTarget.IsEmpty())
    {
        Player* kt = ObjectAccessor::FindPlayer(killTarget);
        LOG_INFO("playerbots", "[EC Arena] Kill target: {} ({})",
            kt ? kt->GetName() : killTarget.ToString(),
            kt ? GetClassName(kt->getClass()) : "??");
    }
    else
    {
        LOG_INFO("playerbots", "[EC Arena] No kill target selected (all enemies dead/CC'd or no enemies found)");
    }

    // --- Build interrupt rotation ---
    sArenaCoordinator->BuildInterruptRotation(state);

    // --- Assign CC targets ---
    sArenaCoordinator->AssignCCTargets(state);

    // --- Mark plan as computed ---
    state->planComputed = true;
    state->lastPlanTime = getMSTime();

    LOG_INFO("playerbots", "[EC Arena] === Plan computation complete ===");
    return true;
}

// ============================================================================
// ArenaFocusKillTargetAction
// ============================================================================

bool ArenaFocusKillTargetAction::Execute(Event /*event*/)
{
    Unit* killTarget = AI_VALUE(Unit*, "arena kill target");
    if (!killTarget || !killTarget->IsAlive())
    {
        LOG_INFO("playerbots", "[EC Arena] {} ArenaFocusKillTarget FAILED - kill target null or dead", bot->GetName());
        return false;
    }

    Unit* currentTarget = AI_VALUE(Unit*, "current target");
    LOG_INFO("playerbots", "[EC Arena] {} switching to kill target: {} (was targeting: {})",
        bot->GetName(), killTarget->GetName(),
        currentTarget ? currentTarget->GetName() : "nothing");

    bool result = Attack(killTarget);
    if (!result)
        LOG_INFO("playerbots", "[EC Arena] {} Attack() on kill target {} FAILED", bot->GetName(), killTarget->GetName());

    return result;
}

bool ArenaFocusKillTargetAction::isUseful()
{
    Unit* killTarget = AI_VALUE(Unit*, "arena kill target");
    if (!killTarget || !killTarget->IsAlive())
        return false;

    Unit* currentTarget = AI_VALUE(Unit*, "current target");
    bool useful = currentTarget != killTarget;
    if (useful)
    {
        LOG_DEBUG("playerbots", "[EC Arena] {} FocusKillTarget isUseful=true (current={}, killTarget={})",
            bot->GetName(),
            currentTarget ? currentTarget->GetName() : "none",
            killTarget->GetName());
    }
    return useful;
}

// ============================================================================
// ArenaCoordinatedInterruptAction
// ============================================================================

bool ArenaCoordinatedInterruptAction::Execute(Event /*event*/)
{
    if (!bot->InArena())
    {
        LOG_DEBUG("playerbots", "[EC Arena] {} CoordinatedInterrupt skipped - not in arena", bot->GetName());
        return false;
    }

    ArenaTeamState* state = AI_VALUE(ArenaTeamState*, "arena team state");
    if (!state)
    {
        LOG_INFO("playerbots", "[EC Arena] {} CoordinatedInterrupt FAILED - no ArenaTeamState", bot->GetName());
        return false;
    }

    // Double-check it's our turn
    ObjectGuid nextInterrupter = sArenaCoordinator->GetNextInterrupter(state);
    if (nextInterrupter != bot->GetGUID())
    {
        LOG_DEBUG("playerbots", "[EC Arena] {} not next interrupter, skipping", bot->GetName());
        return false;
    }

    // Find the enemy healer who is casting
    Battleground* bg = bot->GetBattleground();
    if (!bg)
    {
        LOG_INFO("playerbots", "[EC Arena] {} CoordinatedInterrupt FAILED - no Battleground", bot->GetName());
        return false;
    }

    Player* castingHealer = nullptr;
    for (auto const& [guid, bgPlayer] : bg->GetPlayers())
    {
        Player* enemy = ObjectAccessor::FindPlayer(guid);
        if (!enemy || !enemy->IsAlive())
            continue;

        if (enemy->GetBgTeamId() == bot->GetBgTeamId())
            continue;

        auto enemyIt = state->enemies.find(enemy->GetGUID());
        if (enemyIt == state->enemies.end() || !enemyIt->second.isHealer)
            continue;

        // Check for active positive spell cast
        Spell* spell = enemy->GetCurrentSpell(CURRENT_GENERIC_SPELL);
        if (spell && spell->m_spellInfo && spell->m_spellInfo->IsPositive())
        {
            castingHealer = enemy;
            break;
        }

        spell = enemy->GetCurrentSpell(CURRENT_CHANNELED_SPELL);
        if (spell && spell->m_spellInfo && spell->m_spellInfo->IsPositive())
        {
            castingHealer = enemy;
            break;
        }
    }

    if (!castingHealer)
    {
        LOG_DEBUG("playerbots", "[EC Arena] {} CoordinatedInterrupt - no enemy healer currently casting", bot->GetName());
        return false;
    }

    // Use our class interrupt spell
    std::string interruptSpell = GetInterruptSpellForClass(bot->getClass());
    if (interruptSpell.empty())
    {
        LOG_INFO("playerbots", "[EC Arena] {} CoordinatedInterrupt FAILED - {} has no interrupt spell",
            bot->GetName(), GetClassName(bot->getClass()));
        return false;
    }

    if (!botAI->CanCastSpell(interruptSpell, castingHealer))
    {
        LOG_DEBUG("playerbots", "[EC Arena] {} cannot cast {} on {} (out of range or on CD)",
            bot->GetName(), interruptSpell, castingHealer->GetName());
        return false;
    }

    LOG_INFO("playerbots", "[EC Arena] >>> {} ({}) INTERRUPTING healer {} with {}!",
        bot->GetName(), GetClassName(bot->getClass()),
        castingHealer->GetName(), interruptSpell);

    bool result = botAI->CastSpell(interruptSpell, castingHealer);
    if (result)
    {
        sArenaCoordinator->RecordInterruptUsed(state, bot->GetGUID());
    }
    else
    {
        LOG_INFO("playerbots", "[EC Arena] {} CastSpell({}) on healer {} FAILED (spell fizzled or was resisted)",
            bot->GetName(), interruptSpell, castingHealer->GetName());
    }

    return result;
}

// ============================================================================
// ArenaExecuteCCAction
// ============================================================================

bool ArenaExecuteCCAction::Execute(Event /*event*/)
{
    if (!bot->InArena())
    {
        LOG_DEBUG("playerbots", "[EC Arena] {} ExecuteCC skipped - not in arena", bot->GetName());
        return false;
    }

    ArenaTeamState* state = AI_VALUE(ArenaTeamState*, "arena team state");
    if (!state)
    {
        LOG_INFO("playerbots", "[EC Arena] {} ExecuteCC FAILED - no ArenaTeamState", bot->GetName());
        return false;
    }

    // Find our CC assignment
    ObjectGuid ccTargetGuid;
    DRCategory ccCategory = DR_NONE;

    for (auto const& [enemyGuid, assignedBot] : state->ccAssignments)
    {
        if (assignedBot != bot->GetGUID())
            continue;

        auto enemyIt = state->enemies.find(enemyGuid);
        if (enemyIt == state->enemies.end() || !enemyIt->second.isAlive || enemyIt->second.isInCC)
            continue;

        auto catIt = state->ccAssignmentCategory.find(enemyGuid);
        if (catIt != state->ccAssignmentCategory.end() &&
            sArenaCoordinator->IsDRImmune(state, enemyGuid, catIt->second))
            continue;

        ccTargetGuid = enemyGuid;
        ccCategory = catIt != state->ccAssignmentCategory.end() ? catIt->second : DR_NONE;
        break;
    }

    if (ccTargetGuid.IsEmpty())
    {
        LOG_DEBUG("playerbots", "[EC Arena] {} ExecuteCC - no valid CC assignment found (all targets dead/CC'd/DR-immune)", bot->GetName());
        return false;
    }

    Unit* ccTarget = botAI->GetUnit(ccTargetGuid);
    if (!ccTarget || !ccTarget->IsAlive())
    {
        LOG_INFO("playerbots", "[EC Arena] {} ExecuteCC FAILED - CC target {} is null or dead",
            bot->GetName(), ccTargetGuid.ToString());
        return false;
    }

    // Get our CC spell
    DRCategory cat = DR_NONE;
    std::string ccSpell = GetBestCCSpellForClass(bot->getClass(), cat);
    if (ccSpell.empty())
    {
        LOG_INFO("playerbots", "[EC Arena] {} ExecuteCC FAILED - {} has no CC spell",
            bot->GetName(), GetClassName(bot->getClass()));
        return false;
    }

    if (!botAI->CanCastSpell(ccSpell, ccTarget))
    {
        LOG_DEBUG("playerbots", "[EC Arena] {} cannot cast {} on {} (out of range or on CD)",
            bot->GetName(), ccSpell, ccTarget->GetName());
        return false;
    }

    LOG_INFO("playerbots", "[EC Arena] >>> {} ({}) CC'ing {} with {} (DR category: {})",
        bot->GetName(), GetClassName(bot->getClass()),
        ccTarget->GetName(), ccSpell, GetDRCategoryName(cat));

    bool result = botAI->CastSpell(ccSpell, ccTarget);
    if (result)
    {
        sArenaCoordinator->RecordCCApplied(state, ccTargetGuid, cat);
    }
    else
    {
        LOG_INFO("playerbots", "[EC Arena] {} CastSpell({}) on CC target {} FAILED",
            bot->GetName(), ccSpell, ccTarget->GetName());
    }

    return result;
}

// ============================================================================
// ArenaDefensiveAction
// ============================================================================

bool ArenaDefensiveAction::TryCastDefensive(std::string const& spell)
{
    if (botAI->CanCastSpell(spell, bot))
    {
        LOG_INFO("playerbots", "[EC Arena] >>> {} ({}) using DEFENSIVE: {}",
            bot->GetName(), GetClassName(bot->getClass()), spell);
        return botAI->CastSpell(spell, bot);
    }
    return false;
}

bool ArenaDefensiveAction::Execute(Event /*event*/)
{
    if (!bot->InArena())
    {
        LOG_DEBUG("playerbots", "[EC Arena] {} ArenaDefensive skipped - not in arena", bot->GetName());
        return false;
    }

    float healthPct = bot->GetHealthPct();
    uint8 cls = bot->getClass();

    LOG_INFO("playerbots", "[EC Arena] {} ({}) defensive check at {:.0f}% HP",
        bot->GetName(), GetClassName(cls), healthPct);

    // Critical HP defensives (< 30%)
    if (healthPct < 30.0f)
    {
        switch (cls)
        {
            case CLASS_PALADIN:
                if (TryCastDefensive("divine shield")) return true;
                if (TryCastDefensive("lay on hands")) return true;
                break;
            case CLASS_MAGE:
                if (TryCastDefensive("ice block")) return true;
                if (TryCastDefensive("blink")) return true;
                break;
            case CLASS_ROGUE:
                if (TryCastDefensive("vanish")) return true;
                if (TryCastDefensive("cloak of shadows")) return true;
                break;
            case CLASS_WARRIOR:
                if (TryCastDefensive("shield wall")) return true;
                if (TryCastDefensive("enraged regeneration")) return true;
                break;
            case CLASS_PRIEST:
                if (TryCastDefensive("desperate prayer")) return true;
                if (TryCastDefensive("pain suppression")) return true;
                if (TryCastDefensive("dispersion")) return true;
                break;
            case CLASS_DRUID:
                if (TryCastDefensive("barkskin")) return true;
                if (TryCastDefensive("survival instincts")) return true;
                break;
            case CLASS_DEATH_KNIGHT:
                if (TryCastDefensive("icebound fortitude")) return true;
                if (TryCastDefensive("anti-magic shell")) return true;
                break;
            case CLASS_HUNTER:
                if (TryCastDefensive("deterrence")) return true;
                break;
            case CLASS_SHAMAN:
                if (TryCastDefensive("shamanistic rage")) return true;
                break;
            case CLASS_WARLOCK:
                if (TryCastDefensive("death coil")) return true;  // Horror for self-peel
                break;
        }
    }

    // Medium HP defensives (< 50%)
    if (healthPct < 50.0f) switch (cls)
    {
        case CLASS_PALADIN:
            if (TryCastDefensive("divine protection")) return true;
            break;
        case CLASS_MAGE:
            if (TryCastDefensive("ice barrier")) return true;
            if (TryCastDefensive("mana shield")) return true;
            break;
        case CLASS_ROGUE:
            if (TryCastDefensive("evasion")) return true;
            if (TryCastDefensive("feint")) return true;
            break;
        case CLASS_WARRIOR:
            if (TryCastDefensive("last stand")) return true;
            if (TryCastDefensive("spell reflection")) return true;
            break;
        case CLASS_PRIEST:
            if (TryCastDefensive("power word: shield")) return true;
            if (TryCastDefensive("fade")) return true;
            break;
        case CLASS_DRUID:
            if (TryCastDefensive("barkskin")) return true;
            break;
        case CLASS_DEATH_KNIGHT:
            if (TryCastDefensive("anti-magic shell")) return true;
            break;
        case CLASS_HUNTER:
            if (TryCastDefensive("disengage")) return true;
            break;
        case CLASS_SHAMAN:
            if (TryCastDefensive("grounding totem")) return true;
            break;
        case CLASS_WARLOCK:
            if (TryCastDefensive("shadow ward")) return true;
            break;
    }

    LOG_DEBUG("playerbots", "[EC Arena] {} no defensive CD available (hp={:.0f}%)", bot->GetName(), healthPct);
    return false;
}

// ============================================================================
// ArenaPeelAction
// ============================================================================

bool ArenaPeelAction::Execute(Event /*event*/)
{
    if (!bot->InArena())
    {
        LOG_DEBUG("playerbots", "[EC Arena] {} ArenaPeel skipped - not in arena", bot->GetName());
        return false;
    }

    ArenaTeamState* state = AI_VALUE(ArenaTeamState*, "arena team state");
    if (!state || state->focusedTeammate.IsEmpty())
    {
        LOG_DEBUG("playerbots", "[EC Arena] {} ArenaPeel skipped - no state or no focused teammate", bot->GetName());
        return false;
    }

    Player* teammate = ObjectAccessor::FindPlayer(state->focusedTeammate);
    if (!teammate || !teammate->IsAlive())
    {
        LOG_INFO("playerbots", "[EC Arena] {} ArenaPeel FAILED - focused teammate {} is null or dead",
            bot->GetName(), state->focusedTeammate.ToString());
        return false;
    }

    // Find an enemy attacking our teammate that is NOT the kill target
    Battleground* bg = bot->GetBattleground();
    if (!bg)
    {
        LOG_INFO("playerbots", "[EC Arena] {} ArenaPeel FAILED - no Battleground", bot->GetName());
        return false;
    }

    Player* peelTarget = nullptr;
    for (auto const& [guid, bgPlayer] : bg->GetPlayers())
    {
        Player* enemy = ObjectAccessor::FindPlayer(guid);
        if (!enemy || !enemy->IsAlive())
            continue;

        if (enemy->GetBgTeamId() == bot->GetBgTeamId())
            continue;

        // Must be attacking our focused teammate
        if (enemy->GetVictim() != teammate)
            continue;

        // Prefer peeling someone who isn't the kill target
        if (enemy->GetGUID() != state->killTarget)
        {
            peelTarget = enemy;
            break;
        }

        // If all enemies on our teammate are the kill target, peel them anyway
        if (!peelTarget)
            peelTarget = enemy;
    }

    if (!peelTarget)
    {
        LOG_DEBUG("playerbots", "[EC Arena] {} ArenaPeel - no enemy found attacking teammate {}",
            bot->GetName(), teammate->GetName());
        return false;
    }

    LOG_INFO("playerbots", "[EC Arena] {} attempting to peel {} off teammate {} (isKillTarget={})",
        bot->GetName(), peelTarget->GetName(), teammate->GetName(),
        peelTarget->GetGUID() == state->killTarget ? "YES" : "no");

    // Use our best CC on the peel target
    DRCategory cat = DR_NONE;
    std::string ccSpell = GetBestCCSpellForClass(bot->getClass(), cat);

    // Check DR before using CC
    if (!ccSpell.empty() && cat != DR_NONE)
    {
        if (sArenaCoordinator->IsDRImmune(state, peelTarget->GetGUID(), cat))
        {
            LOG_INFO("playerbots", "[EC Arena] {} peel CC ({}) on {} is DR-immune ({}) - trying stun fallback",
                bot->GetName(), ccSpell, peelTarget->GetName(), GetDRCategoryName(cat));
        }
        else if (botAI->CanCastSpell(ccSpell, peelTarget))
        {
            LOG_INFO("playerbots", "[EC Arena] >>> {} ({}) PEELING for {} - CC'ing {} with {}",
                bot->GetName(), GetClassName(bot->getClass()),
                teammate->GetName(), peelTarget->GetName(), ccSpell);

            bool result = botAI->CastSpell(ccSpell, peelTarget);
            if (result)
            {
                sArenaCoordinator->RecordCCApplied(state, peelTarget->GetGUID(), cat);
            }
            else
            {
                LOG_INFO("playerbots", "[EC Arena] {} peel CastSpell({}) on {} FAILED",
                    bot->GetName(), ccSpell, peelTarget->GetName());
            }
            return result;
        }
        else
        {
            LOG_DEBUG("playerbots", "[EC Arena] {} cannot cast peel {} on {} (out of range or on CD) - trying stun fallback",
                bot->GetName(), ccSpell, peelTarget->GetName());
        }
    }
    else if (ccSpell.empty())
    {
        LOG_DEBUG("playerbots", "[EC Arena] {} ({}) has no CC spell for peeling - trying stun fallback",
            bot->GetName(), GetClassName(bot->getClass()));
    }

    // Fallback: try a stun if CC was on DR
    std::string stunSpell;
    switch (bot->getClass())
    {
        case CLASS_ROGUE:    stunSpell = "kidney shot"; break;
        case CLASS_WARRIOR:  stunSpell = "concussion blow"; break;
        case CLASS_PALADIN:  stunSpell = "hammer of justice"; break;
        case CLASS_DRUID:    stunSpell = "bash"; break;
        default: break;
    }

    if (!stunSpell.empty() && botAI->CanCastSpell(stunSpell, peelTarget))
    {
        LOG_INFO("playerbots", "[EC Arena] >>> {} ({}) PEELING (stun fallback) for {} - stunning {} with {}",
            bot->GetName(), GetClassName(bot->getClass()),
            teammate->GetName(), peelTarget->GetName(), stunSpell);
        return botAI->CastSpell(stunSpell, peelTarget);
    }

    LOG_DEBUG("playerbots", "[EC Arena] {} has no peel ability available for {}", bot->GetName(), peelTarget->GetName());
    return false;
}
