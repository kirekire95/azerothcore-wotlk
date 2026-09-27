/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 *
 * EC Arena Coordinator - Team-level arena AI coordination for playerbots.
 * Custom code by EC (kirekire95).
 */

#include "ArenaCoordinator.h"

#include "Battleground.h"
#include "Log.h"
#include "Player.h"
#include "SpellAuras.h"
#include "SpellInfo.h"
#include "Timer.h"

static constexpr uint32 DR_RESET_TIME_MS = 18000;  // 18 seconds
static constexpr uint32 KILL_TARGET_HYSTERESIS_MS = 5000;
static constexpr float KILL_TARGET_SWITCH_THRESHOLD = 60.0f;
static constexpr float LOW_HP_FINISH_THRESHOLD = 30.0f;

ArenaTeamState* ArenaCoordinator::GetTeamState(uint32 instanceId, uint32 teamId)
{
    std::lock_guard<std::mutex> lock(mutex_);
    uint64 key = MakeKey(instanceId, teamId);

    auto it = teamStates_.find(key);
    if (it != teamStates_.end())
        return &it->second;

    LOG_INFO("playerbots", "[EC Arena] Creating new ArenaTeamState for instance={} team={}",
        instanceId, teamId);

    ArenaTeamState& state = teamStates_[key];
    state.instanceId = instanceId;
    state.teamId = teamId;
    return &state;
}

void ArenaCoordinator::RemoveInstance(uint32 instanceId)
{
    std::lock_guard<std::mutex> lock(mutex_);
    LOG_INFO("playerbots", "[EC Arena] Cleaning up ArenaTeamState for instance={}", instanceId);
    teamStates_.erase(MakeKey(instanceId, TEAM_ALLIANCE));
    teamStates_.erase(MakeKey(instanceId, TEAM_HORDE));
}

ObjectGuid ArenaCoordinator::ComputeKillTarget(ArenaTeamState* state)
{
    if (!state)
    {
        LOG_INFO("playerbots", "[EC Arena] ComputeKillTarget called with null state!");
        return ObjectGuid::Empty;
    }

    LOG_DEBUG("playerbots", "[EC Arena] ComputeKillTarget - evaluating {} enemies", state->enemies.size());

    uint32 now = getMSTime();
    ObjectGuid currentKillTarget = state->killTarget;

    // Hysteresis: don't switch if current target is healthy and was set recently
    if (!currentKillTarget.IsEmpty())
    {
        auto it = state->enemies.find(currentKillTarget);
        if (it != state->enemies.end() && it->second.isAlive && !it->second.isInCC
            && it->second.healthPct > KILL_TARGET_SWITCH_THRESHOLD
            && (now - state->killTargetSetTime) < KILL_TARGET_HYSTERESIS_MS)
        {
            LOG_DEBUG("playerbots", "[EC Arena] Kill target unchanged (hysteresis): {} hp={:.0f}%",
                currentKillTarget.ToString(), it->second.healthPct);
            return currentKillTarget;
        }
    }

    ObjectGuid bestTarget = ObjectGuid::Empty;
    float bestScore = -1.0f;

    for (auto& [guid, enemy] : state->enemies)
    {
        // Refresh CC state inline to avoid 3s staleness window from plan recompute
        Player* enemyPlayer = ObjectAccessor::FindPlayer(guid);
        if (enemyPlayer)
        {
            enemy.isAlive = enemyPlayer->IsAlive();
            enemy.healthPct = enemyPlayer->GetHealthPct();
            enemy.isInCC = enemyPlayer->HasUnitState(UNIT_STATE_STUNNED) ||
                           enemyPlayer->HasUnitState(UNIT_STATE_CONFUSED) ||
                           enemyPlayer->HasUnitState(UNIT_STATE_FLEEING) ||
                           enemyPlayer->HasUnitState(UNIT_STATE_ROOT) ||
                           enemyPlayer->HasAuraType(SPELL_AURA_MOD_STUN) ||
                           enemyPlayer->HasAuraType(SPELL_AURA_MOD_CONFUSE) ||
                           enemyPlayer->HasAuraType(SPELL_AURA_MOD_FEAR) ||
                           enemyPlayer->HasAuraType(SPELL_AURA_TRANSFORM);
        }

        if (!enemy.isAlive || enemy.isInCC)
        {
            LOG_DEBUG("playerbots", "[EC Arena]   Kill target skip: {} ({}) - {} (hp={:.0f}%)",
                guid.ToString(), GetClassName(enemy.playerClass),
                !enemy.isAlive ? "DEAD" : "CC'd", enemy.healthPct);
            continue;
        }

        float score = 0.0f;

        // Priority 1: Enemy healer (huge priority boost)
        if (enemy.isHealer)
            score += 1000.0f;

        // Priority 2: Low HP enemies (finish the kill)
        if (enemy.healthPct < LOW_HP_FINISH_THRESHOLD)
            score += 500.0f;

        // Priority 3: Lower HP = higher score (focus damage)
        score += (100.0f - enemy.healthPct);

        LOG_DEBUG("playerbots", "[EC Arena] Kill target candidate: {} class={} healer={} hp={:.0f}% inCC={} score={:.0f}",
            guid.ToString(), GetClassName(enemy.playerClass),
            enemy.isHealer ? "YES" : "no", enemy.healthPct, enemy.isInCC ? "YES" : "no", score);

        if (score > bestScore)
        {
            bestScore = score;
            bestTarget = guid;
        }
    }

    if (bestTarget != currentKillTarget)
    {
        state->killTarget = bestTarget;
        state->killTargetSetTime = now;
        if (!bestTarget.IsEmpty())
        {
            auto it = state->enemies.find(bestTarget);
            LOG_INFO("playerbots", "[EC Arena] >>> KILL TARGET SWITCHED to {} (class={}, healer={}, hp={:.0f}%, score={:.0f})",
                bestTarget.ToString(),
                it != state->enemies.end() ? GetClassName(it->second.playerClass) : "??",
                it != state->enemies.end() && it->second.isHealer ? "YES" : "no",
                it != state->enemies.end() ? it->second.healthPct : 0.0f,
                bestScore);
        }
        else
        {
            LOG_INFO("playerbots", "[EC Arena] >>> No valid kill target found (all dead or CC'd)");
        }
    }

    return bestTarget;
}

void ArenaCoordinator::BuildInterruptRotation(ArenaTeamState* state)
{
    if (!state)
    {
        LOG_INFO("playerbots", "[EC Arena] BuildInterruptRotation called with null state!");
        return;
    }

    struct InterruptEntry
    {
        ObjectGuid guid;
        uint32 cooldown;
        uint8 playerClass;
    };

    std::vector<InterruptEntry> entries;
    for (ObjectGuid const& memberGuid : state->teamMembers)
    {
        Player* player = ObjectAccessor::FindPlayer(memberGuid);
        if (!player || !player->IsAlive())
        {
            LOG_DEBUG("playerbots", "[EC Arena] Interrupt rotation - skipping member {} (null or dead)",
                memberGuid.ToString());
            continue;
        }

        uint32 cd = GetInterruptCooldownForClass(player->getClass());
        if (cd > 0)  // 0 means no interrupt ability
        {
            entries.push_back({memberGuid, cd, player->getClass()});
            LOG_DEBUG("playerbots", "[EC Arena] Interrupt candidate: {} ({}) cd={}s",
                player->GetName(), GetClassName(player->getClass()), cd);
        }
        else
        {
            LOG_DEBUG("playerbots", "[EC Arena] {} ({}) has no interrupt ability - excluded from rotation",
                player->GetName(), GetClassName(player->getClass()));
        }
    }

    // Sort by cooldown ascending (shortest CD = first in rotation)
    std::sort(entries.begin(), entries.end(),
        [](InterruptEntry const& a, InterruptEntry const& b) { return a.cooldown < b.cooldown; });

    state->interruptRotation.clear();
    for (auto const& entry : entries)
        state->interruptRotation.push_back(entry.guid);

    state->lastInterruptBotIndex = 0;

    LOG_INFO("playerbots", "[EC Arena] Interrupt rotation built ({} bots):", state->interruptRotation.size());
    for (uint32 i = 0; i < entries.size(); ++i)
    {
        Player* p = ObjectAccessor::FindPlayer(entries[i].guid);
        LOG_INFO("playerbots", "[EC Arena]   #{}: {} ({}) - {}s CD",
            i + 1, p ? p->GetName() : "??",
            GetClassName(entries[i].playerClass), entries[i].cooldown);
    }
}

ObjectGuid ArenaCoordinator::GetNextInterrupter(ArenaTeamState* state)
{
    if (!state || state->interruptRotation.empty())
        return ObjectGuid::Empty;

    uint32 size = static_cast<uint32>(state->interruptRotation.size());
    for (uint32 i = 0; i < size; ++i)
    {
        uint32 idx = (state->lastInterruptBotIndex + i) % size;
        ObjectGuid botGuid = state->interruptRotation[idx];

        Player* player = ObjectAccessor::FindPlayer(botGuid);
        if (!player || !player->IsAlive())
            continue;

        uint32 spellId = GetInterruptSpellIdForClass(player->getClass());
        if (!spellId)
            continue;

        if (player->HasSpell(spellId) && !player->HasSpellCooldown(spellId))
        {
            LOG_DEBUG("playerbots", "[EC Arena] Next interrupter: {} ({}) [rotation idx={}]",
                player->GetName(), GetClassName(player->getClass()), idx);
            return botGuid;
        }
        else
        {
            LOG_DEBUG("playerbots", "[EC Arena] Interrupter {} ({}) skipped (spell on CD or missing)",
                player->GetName(), GetClassName(player->getClass()));
        }
    }

    LOG_DEBUG("playerbots", "[EC Arena] No interrupter available (all on CD)");
    return ObjectGuid::Empty;
}

void ArenaCoordinator::RecordInterruptUsed(ArenaTeamState* state, ObjectGuid botGuid)
{
    if (!state)
    {
        LOG_INFO("playerbots", "[EC Arena] RecordInterruptUsed called with null state!");
        return;
    }

    state->lastInterruptTime = getMSTime();

    for (uint32 i = 0; i < state->interruptRotation.size(); ++i)
    {
        if (state->interruptRotation[i] == botGuid)
        {
            state->lastInterruptBotIndex = (i + 1) % static_cast<uint32>(state->interruptRotation.size());
            Player* p = ObjectAccessor::FindPlayer(botGuid);
            LOG_INFO("playerbots", "[EC Arena] Interrupt used by {} - rotation advances to idx={}",
                p ? p->GetName() : botGuid.ToString(), state->lastInterruptBotIndex);
            break;
        }
    }
}

void ArenaCoordinator::RecordCCApplied(ArenaTeamState* state, ObjectGuid enemyGuid, DRCategory cat)
{
    if (!state || cat == DR_NONE)
    {
        LOG_DEBUG("playerbots", "[EC Arena] RecordCCApplied skipped - null state or DR_NONE");
        return;
    }

    std::lock_guard<std::mutex> lock(mutex_);

    auto it = state->enemies.find(enemyGuid);
    if (it == state->enemies.end())
    {
        LOG_INFO("playerbots", "[EC Arena] RecordCCApplied - enemy {} not found in state!", enemyGuid.ToString());
        return;
    }

    DRState& dr = it->second.drStates[cat];
    uint32 now = getMSTime();

    // If DR has expired, reset before incrementing
    if (dr.resetTime > 0 && now >= dr.resetTime)
        dr.count = 0;

    uint32 oldCount = dr.count;
    dr.count = std::min(dr.count + 1, 3u);
    dr.resetTime = now + DR_RESET_TIME_MS;
    it->second.isInCC = true;

    LOG_INFO("playerbots", "[EC Arena] CC applied on {} ({}) - DR {} {}->{} (immune at 3, resets in 18s)",
        enemyGuid.ToString(), GetClassName(it->second.playerClass),
        GetDRCategoryName(cat), oldCount, dr.count);
}

uint32 ArenaCoordinator::GetDRCount(ArenaTeamState* state, ObjectGuid enemyGuid, DRCategory cat)
{
    if (!state || cat == DR_NONE)
        return 0;

    auto enemyIt = state->enemies.find(enemyGuid);
    if (enemyIt == state->enemies.end())
        return 0;

    auto drIt = enemyIt->second.drStates.find(cat);
    if (drIt == enemyIt->second.drStates.end())
        return 0;

    uint32 now = getMSTime();
    if (drIt->second.resetTime > 0 && now >= drIt->second.resetTime)
    {
        drIt->second.count = 0;
        return 0;
    }

    return drIt->second.count;
}

bool ArenaCoordinator::IsDRImmune(ArenaTeamState* state, ObjectGuid enemyGuid, DRCategory cat)
{
    return GetDRCount(state, enemyGuid, cat) >= 3;
}

void ArenaCoordinator::AssignCCTargets(ArenaTeamState* state)
{
    if (!state)
        return;

    state->ccAssignments.clear();
    state->ccAssignmentCategory.clear();

    // Collect non-kill-target alive enemies
    std::vector<ObjectGuid> ccCandidates;
    for (auto& [guid, enemy] : state->enemies)
    {
        if (!enemy.isAlive || guid == state->killTarget)
            continue;
        ccCandidates.push_back(guid);
    }

    if (ccCandidates.empty())
    {
        LOG_DEBUG("playerbots", "[EC Arena] No CC candidates (all enemies are kill target or dead)");
        return;
    }

    LOG_DEBUG("playerbots", "[EC Arena] Assigning CC for {} candidate(s):", ccCandidates.size());

    for (ObjectGuid const& enemyGuid : ccCandidates)
    {
        auto enemyIt = state->enemies.find(enemyGuid);
        if (enemyIt == state->enemies.end())
            continue;

        ObjectGuid bestBot = ObjectGuid::Empty;
        uint32 bestPriority = 0;
        DRCategory bestCategory = DR_NONE;

        for (ObjectGuid const& memberGuid : state->teamMembers)
        {
            // Skip bots already assigned to CC someone else
            bool alreadyAssigned = false;
            for (auto& [assignedEnemy, assignedBot] : state->ccAssignments)
            {
                if (assignedBot == memberGuid)
                {
                    alreadyAssigned = true;
                    break;
                }
            }
            if (alreadyAssigned)
            {
                Player* p = ObjectAccessor::FindPlayer(memberGuid);
                LOG_DEBUG("playerbots", "[EC Arena]   Skipping {} for CC - already assigned to CC another enemy",
                    p ? p->GetName() : memberGuid.ToString());
                continue;
            }

            // Skip the next interrupter — they need to stay ready for kicks
            if (!state->interruptRotation.empty())
            {
                ObjectGuid nextInterrupter = GetNextInterrupter(state);
                if (memberGuid == nextInterrupter)
                {
                    Player* p = ObjectAccessor::FindPlayer(memberGuid);
                    LOG_DEBUG("playerbots", "[EC Arena]   Skipping {} ({}) for CC - is next interrupter",
                        p ? p->GetName() : memberGuid.ToString(),
                        p ? GetClassName(p->getClass()) : "??");
                    continue;
                }
            }

            Player* player = ObjectAccessor::FindPlayer(memberGuid);
            if (!player || !player->IsAlive())
            {
                LOG_DEBUG("playerbots", "[EC Arena]   Skipping member {} for CC - null or dead", memberGuid.ToString());
                continue;
            }

            DRCategory cat = DR_NONE;
            std::string ccSpell = GetBestCCSpellForClass(player->getClass(), cat);
            if (ccSpell.empty() || cat == DR_NONE)
            {
                LOG_DEBUG("playerbots", "[EC Arena]   {} ({}) has no CC spell - excluded from CC assignment",
                    player->GetName(), GetClassName(player->getClass()));
                continue;
            }

            if (IsDRImmune(state, enemyGuid, cat))
            {
                LOG_DEBUG("playerbots", "[EC Arena]   Skipping {} ({}) for CC on {} - DR immune ({})",
                    player->GetName(), GetClassName(player->getClass()),
                    enemyGuid.ToString(), GetDRCategoryName(cat));
                continue;
            }

            uint32 priority = 0;
            switch (player->getClass())
            {
                case CLASS_MAGE:     priority = 10; break;
                case CLASS_ROGUE:    priority = 9;  break;
                case CLASS_DRUID:    priority = 8;  break;
                case CLASS_PRIEST:   priority = 7;  break;
                case CLASS_WARLOCK:  priority = 6;  break;
                case CLASS_PALADIN:  priority = 5;  break;
                case CLASS_SHAMAN:   priority = 4;  break;
                default:             priority = 1;  break;
            }

            if (priority > bestPriority)
            {
                bestPriority = priority;
                bestBot = memberGuid;
                bestCategory = cat;
            }
        }

        if (!bestBot.IsEmpty())
        {
            state->ccAssignments[enemyGuid] = bestBot;
            state->ccAssignmentCategory[enemyGuid] = bestCategory;

            Player* assignee = ObjectAccessor::FindPlayer(bestBot);
            LOG_INFO("playerbots", "[EC Arena] CC Assignment: {} ({}) will CC enemy {} ({}) using {} (DR={})",
                assignee ? assignee->GetName() : "??",
                assignee ? GetClassName(assignee->getClass()) : "??",
                enemyGuid.ToString(), GetClassName(enemyIt->second.playerClass),
                GetBestCCSpellForClass(assignee ? assignee->getClass() : 0, bestCategory),
                GetDRCategoryName(bestCategory));
        }
        else
        {
            Player* enemy = ObjectAccessor::FindPlayer(enemyGuid);
            LOG_INFO("playerbots", "[EC Arena] CC Assignment FAILED for enemy {} ({}) - no suitable bot available (all busy/interrupter/no CC spell/DR immune)",
                enemy ? enemy->GetName() : enemyGuid.ToString(),
                GetClassName(enemyIt->second.playerClass));
        }
    }

    LOG_INFO("playerbots", "[EC Arena] CC assignment complete: {} of {} enemies assigned",
        state->ccAssignments.size(), ccCandidates.size());
}

// ====================================================================================
// Helper functions
// ====================================================================================

DRCategory GetDRCategoryForSpell(uint32 spellId)
{
    switch (spellId)
    {
        // Stuns
        case 408:   case 51724:  // Kidney Shot
        case 1833:               // Cheap Shot
        case 853:                // Hammer of Justice
        case 44572:              // Deep Freeze
        case 12809:              // Concussion Blow
        case 20549:              // War Stomp
        case 30283:              // Shadowfury
        case 46968:              // Shockwave
        case 49012:              // Gnaw (DK pet)
        case 5211:               // Bash (Druid)
        case 22570:              // Maim
            return DR_STUN;

        // Incapacitates
        case 118:   case 12824: case 12825: case 12826: case 28272: case 28271:
        case 61305: case 61721: case 61780:  // Polymorph variants
        case 6770:   // Sap
        case 1776:   // Gouge
        case 51514:  // Hex
        case 20066:  // Repentance
            return DR_INCAPACITATE;

        // Disorients / Fears
        case 5782:  case 6213:  case 6215:               // Fear
        case 8122:  case 8124:  case 10888: case 10890:  // Psychic Scream
        case 2094:   // Blind
        case 5484:   // Howl of Terror
        case 6358:   // Seduction
        case 1513:   // Scare Beast
        case 10326:  // Turn Evil
            return DR_DISORIENT;

        // Silence
        case 15487:  // Silence (Priest)
        case 18469:  // Silenced - Improved Counterspell
        case 34490:  // Silencing Shot (Hunter)
        case 18425:  // Silenced - Improved Kick
        case 47476:  // Strangulate (DK)
            return DR_SILENCE;

        // Roots
        case 339:   case 1062:  case 5195: case 5196: case 9852: case 9853: case 26989: case 53308:  // Entangling Roots
        case 122:   case 865:   case 6131: case 10230: case 27088: case 42917:                         // Frost Nova
        case 33395:  // Freeze (Water Elemental)
            return DR_ROOT;

        // Cyclone
        case 33786:
            return DR_CYCLONE;

        // Horror
        case 6789:  case 17925: case 17926: case 27223: case 47859: case 47860:  // Death Coil (Warlock)
        case 64044:  // Psychic Horror
            return DR_HORROR;

        // Disarm
        case 676:  // Disarm (Warrior)
            return DR_DISARM;

        default:
            return DR_NONE;
    }
}

bool IsHealerClass(uint8 playerClass, Player* player)
{
    switch (playerClass)
    {
        case CLASS_PRIEST:
            if (player)
            {
                // Disc: Penance (47540), Pain Suppression (33206)
                // Holy: Circle of Healing (34861), Guardian Spirit (47788)
                if (player->HasSpell(47540) || player->HasSpell(33206) ||
                    player->HasSpell(34861) || player->HasSpell(47788))
                {
                    LOG_DEBUG("playerbots", "[EC Arena] {} detected as HEALER Priest (disc/holy talent found)",
                        player->GetName());
                    return true;
                }
                // Shadow: Vampiric Touch (34914)
                if (player->HasSpell(34914))
                {
                    LOG_DEBUG("playerbots", "[EC Arena] {} detected as Shadow Priest (DPS)", player->GetName());
                    return false;
                }
            }
            LOG_DEBUG("playerbots", "[EC Arena] Priest spec unknown, assuming HEALER");
            return true;

        case CLASS_PALADIN:
            if (player)
            {
                if (player->HasSpell(20473) || player->HasSpell(53563))
                {
                    LOG_DEBUG("playerbots", "[EC Arena] {} detected as Holy Paladin (HEALER)", player->GetName());
                    return true;
                }
            }
            return false;

        case CLASS_DRUID:
            if (player)
            {
                if (player->HasAura(33891) || player->HasSpell(48451) || player->HasSpell(48438))
                {
                    LOG_DEBUG("playerbots", "[EC Arena] {} detected as Resto Druid (HEALER)", player->GetName());
                    return true;
                }
            }
            return false;

        case CLASS_SHAMAN:
            if (player)
            {
                if (player->HasSpell(49284) || player->HasSpell(61301) || player->HasSpell(16190))
                {
                    LOG_DEBUG("playerbots", "[EC Arena] {} detected as Resto Shaman (HEALER)", player->GetName());
                    return true;
                }
            }
            return false;

        default:
            return false;
    }
}

uint32 GetInterruptCooldownForClass(uint8 playerClass)
{
    switch (playerClass)
    {
        case CLASS_SHAMAN:       return 6;
        case CLASS_ROGUE:        return 10;
        case CLASS_WARRIOR:      return 10;
        case CLASS_DEATH_KNIGHT: return 10;
        case CLASS_HUNTER:       return 10;
        case CLASS_MAGE:         return 24;
        case CLASS_PRIEST:       return 45;
        default:                 return 0;
    }
}

uint32 GetInterruptSpellIdForClass(uint8 playerClass)
{
    switch (playerClass)
    {
        case CLASS_SHAMAN:       return 57994;  // Wind Shear
        case CLASS_ROGUE:        return 1766;   // Kick
        case CLASS_WARRIOR:      return 6552;   // Pummel
        case CLASS_DEATH_KNIGHT: return 47528;  // Mind Freeze
        case CLASS_MAGE:         return 2139;   // Counterspell
        case CLASS_PRIEST:       return 15487;  // Silence
        case CLASS_HUNTER:       return 34490;  // Silencing Shot
        default:                 return 0;
    }
}

std::string GetInterruptSpellForClass(uint8 playerClass)
{
    switch (playerClass)
    {
        case CLASS_SHAMAN:       return "wind shear";
        case CLASS_ROGUE:        return "kick";
        case CLASS_WARRIOR:      return "pummel";
        case CLASS_DEATH_KNIGHT: return "mind freeze";
        case CLASS_MAGE:         return "counterspell";
        case CLASS_PRIEST:       return "silence";
        case CLASS_HUNTER:       return "silencing shot";
        default:                 return "";
    }
}

std::string GetBestCCSpellForClass(uint8 playerClass, DRCategory& outCategory)
{
    switch (playerClass)
    {
        case CLASS_MAGE:
            outCategory = DR_INCAPACITATE;
            return "polymorph";
        case CLASS_ROGUE:
            outCategory = DR_DISORIENT;
            return "blind";
        case CLASS_DRUID:
            outCategory = DR_CYCLONE;
            return "cyclone";
        case CLASS_PRIEST:
            outCategory = DR_DISORIENT;
            return "psychic scream";
        case CLASS_WARLOCK:
            outCategory = DR_DISORIENT;
            return "fear";
        case CLASS_PALADIN:
            outCategory = DR_STUN;
            return "hammer of justice";
        case CLASS_SHAMAN:
            outCategory = DR_INCAPACITATE;
            return "hex";
        case CLASS_HUNTER:
            outCategory = DR_INCAPACITATE;
            return "freezing trap";
        default:
            outCategory = DR_NONE;
            return "";
    }
}

char const* GetDRCategoryName(DRCategory cat)
{
    switch (cat)
    {
        case DR_STUN:         return "Stun";
        case DR_INCAPACITATE: return "Incapacitate";
        case DR_DISORIENT:    return "Disorient/Fear";
        case DR_SILENCE:      return "Silence";
        case DR_ROOT:         return "Root";
        case DR_CYCLONE:      return "Cyclone";
        case DR_HORROR:       return "Horror";
        case DR_DISARM:       return "Disarm";
        default:              return "None";
    }
}

char const* GetClassName(uint8 playerClass)
{
    switch (playerClass)
    {
        case CLASS_WARRIOR:      return "Warrior";
        case CLASS_PALADIN:      return "Paladin";
        case CLASS_HUNTER:       return "Hunter";
        case CLASS_ROGUE:        return "Rogue";
        case CLASS_PRIEST:       return "Priest";
        case CLASS_DEATH_KNIGHT: return "DeathKnight";
        case CLASS_SHAMAN:       return "Shaman";
        case CLASS_MAGE:         return "Mage";
        case CLASS_WARLOCK:      return "Warlock";
        case CLASS_DRUID:        return "Druid";
        default:                 return "Unknown";
    }
}
