/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 *
 * EC Arena Coordinator - Team-level arena AI coordination for playerbots.
 * Custom code by EC (kirekire95). Adds focus fire, interrupt rotation,
 * CC chains with DR tracking, and defensive awareness to arena bots.
 */

#ifndef _PLAYERBOT_ARENACOORDINATOR_H
#define _PLAYERBOT_ARENACOORDINATOR_H

#include "ObjectGuid.h"

#include <mutex>
#include <unordered_map>
#include <vector>

class Battleground;
class Player;

// WoW 3.3.5 diminishing returns categories
enum DRCategory : uint8
{
    DR_NONE = 0,
    DR_STUN,            // Kidney Shot, Cheap Shot, HoJ, Deep Freeze, Concussion Blow, Shockwave, War Stomp, Shadowfury
    DR_INCAPACITATE,    // Polymorph, Sap, Gouge, Hex, Repentance
    DR_DISORIENT,       // Fear, Psychic Scream, Blind, Howl of Terror, Seduction
    DR_SILENCE,         // Silence, Imp. Counterspell, Silencing Shot, Imp. Kick
    DR_ROOT,            // Entangling Roots, Frost Nova, Freeze (Water Elemental)
    DR_CYCLONE,         // Cyclone
    DR_HORROR,          // Death Coil (Warlock), Psychic Horror
    DR_DISARM,          // Disarm, Psychic Horror (disarm component)
    DR_MAX
};

// DR state for a single category on a single target
struct DRState
{
    uint32 count = 0;       // 0 = full duration, 1 = half, 2 = quarter, 3 = immune
    uint32 resetTime = 0;   // getMSTime() when DR resets (18s decay)
};

// Per-enemy tracking within an arena match
struct ArenaEnemyState
{
    ObjectGuid guid;
    uint8 playerClass = 0;
    bool isHealer = false;
    float healthPct = 100.0f;
    bool isAlive = true;
    bool isInCC = false;                                         // Currently crowd-controlled
    std::unordered_map<uint8 /*DRCategory*/, DRState> drStates;  // DR per category
};

// Per-team coordination state (shared by all bots on the same arena team)
struct ArenaTeamState
{
    uint32 instanceId = 0;
    uint32 teamId = 0;

    // Kill target -- the enemy the whole team should focus
    ObjectGuid killTarget;
    uint32 killTargetSetTime = 0;

    // CC assignments: enemy GUID -> bot GUID assigned to CC them
    std::unordered_map<ObjectGuid, ObjectGuid> ccAssignments;
    // Which CC spell (by DR category) each assignment uses
    std::unordered_map<ObjectGuid, DRCategory> ccAssignmentCategory;

    // Interrupt rotation: ordered list of bot GUIDs
    std::vector<ObjectGuid> interruptRotation;
    uint32 lastInterruptBotIndex = 0;
    uint32 lastInterruptTime = 0;

    // Defensive: which teammate is currently being focused by enemies
    ObjectGuid focusedTeammate;
    uint32 focusedTeammateTime = 0;

    // Enemy tracking
    std::unordered_map<ObjectGuid, ArenaEnemyState> enemies;

    // Team member GUIDs
    std::vector<ObjectGuid> teamMembers;

    // Arena bracket (2, 3, or 5)
    uint8 bracketSize = 0;

    // Match state
    bool planComputed = false;
    uint32 lastPlanTime = 0;
};

class ArenaCoordinator
{
public:
    ArenaCoordinator() {}
    ~ArenaCoordinator() {}

    static ArenaCoordinator* instance()
    {
        static ArenaCoordinator inst;
        return &inst;
    }

    // Get or create team state for an arena instance + team
    ArenaTeamState* GetTeamState(uint32 instanceId, uint32 teamId);

    // Clean up when arena ends
    void RemoveInstance(uint32 instanceId);

    // Compute the best kill target for the team
    ObjectGuid ComputeKillTarget(ArenaTeamState* state);

    // Build the interrupt rotation based on team composition
    void BuildInterruptRotation(ArenaTeamState* state);

    // Get the next bot who should interrupt
    ObjectGuid GetNextInterrupter(ArenaTeamState* state);

    // Record that a bot used their interrupt
    void RecordInterruptUsed(ArenaTeamState* state, ObjectGuid botGuid);

    // DR tracking
    void RecordCCApplied(ArenaTeamState* state, ObjectGuid enemyGuid, DRCategory cat);
    uint32 GetDRCount(ArenaTeamState* state, ObjectGuid enemyGuid, DRCategory cat);
    bool IsDRImmune(ArenaTeamState* state, ObjectGuid enemyGuid, DRCategory cat);

    // Assign CC targets for non-kill-target enemies
    void AssignCCTargets(ArenaTeamState* state);

private:
    std::unordered_map<uint64, ArenaTeamState> teamStates_;
    std::mutex mutex_;

    uint64 MakeKey(uint32 instanceId, uint32 teamId) const
    {
        return (static_cast<uint64>(instanceId) << 32) | teamId;
    }
};

#define sArenaCoordinator ArenaCoordinator::instance()

// Helpers
DRCategory GetDRCategoryForSpell(uint32 spellId);
bool IsHealerClass(uint8 playerClass, Player* player = nullptr);
uint32 GetInterruptCooldownForClass(uint8 playerClass);
uint32 GetInterruptSpellIdForClass(uint8 playerClass);
std::string GetInterruptSpellForClass(uint8 playerClass);
std::string GetBestCCSpellForClass(uint8 playerClass, DRCategory& outCategory);
char const* GetDRCategoryName(DRCategory cat);
char const* GetClassName(uint8 playerClass);

#endif
