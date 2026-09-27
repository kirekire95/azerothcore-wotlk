/*
 * EC Guard System - Script Hooks
 * GTA-style wanted level system for AzerothCore
 */

#include "ECGuardSystemManager.h"
#include "ECBountyHunterManager.h"
#include "ECWitnessSystem.h"
#include "ECZoneState.h"
#include "ECZoneEnvironment.h"
#include "ECZoneSecurityState.h"
#include "Creature.h"
#include "Player.h"
#include "WorldSession.h"
#include "ScriptMgr.h"
#include "Config.h"
#include "Chat.h"
#include "SpellAuras.h"
#include "SpellAuraEffects.h"
#include "DatabaseEnv.h"
#include "QueryResult.h"

#ifdef MOD_PLAYERBOTS
#include "PlayerbotMgr.h"
#include "RandomPlayerbotMgr.h"
#endif
#include <chrono>
#include <mutex>
#include <unordered_map>
#include <unordered_set>

using namespace ECGuardSystem;
using namespace Acore::ChatCommands;

namespace
{
    struct CreatureAttackKey
    {
        ObjectGuid creatureGuid;
        uint32 mapId = 0;
        uint32 instanceId = 0;

        bool operator==(CreatureAttackKey const& other) const = default;
    };

    struct CreatureAttackKeyHash
    {
        std::size_t operator()(CreatureAttackKey const& key) const
        {
            std::size_t seed = std::hash<ObjectGuid>{}(key.creatureGuid);
            seed ^= std::hash<uint32>{}(key.mapId) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
            seed ^= std::hash<uint32>{}(key.instanceId) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
            return seed;
        }
    };

    using AttackReportPlayers = std::unordered_set<ObjectGuid>;
    using AttackReportMap = std::unordered_map<CreatureAttackKey, AttackReportPlayers, CreatureAttackKeyHash>;
    using CivilianAlarmPlayers = std::unordered_map<ObjectGuid, std::chrono::steady_clock::time_point>;
    using CivilianAlarmMap = std::unordered_map<CreatureAttackKey, CivilianAlarmPlayers, CreatureAttackKeyHash>;
    using CivilianScanMap = std::unordered_map<CreatureAttackKey, std::chrono::steady_clock::time_point, CreatureAttackKeyHash>;

    std::mutex s_attackReportMutex;
    AttackReportMap s_attackReports;
    CivilianAlarmMap s_civilianAlarms;
    CivilianScanMap s_civilianScanDue;

    CreatureAttackKey MakeCreatureAttackKey(Creature const* creature)
    {
        return { creature->GetGUID(), creature->GetMapId(), creature->GetInstanceId() };
    }

    bool MarkCreatureAttackReported(Creature const* creature, ObjectGuid playerGuid)
    {
        std::lock_guard<std::mutex> lock(s_attackReportMutex);
        return s_attackReports[MakeCreatureAttackKey(creature)].insert(playerGuid).second;
    }

    bool MarkCivilianAlarmAllowed(Creature const* creature, ObjectGuid playerGuid, uint32 repeatMs)
    {
        auto const now = std::chrono::steady_clock::now();
        std::lock_guard<std::mutex> lock(s_attackReportMutex);
        auto& playerTimes = s_civilianAlarms[MakeCreatureAttackKey(creature)];
        auto const it = playerTimes.find(playerGuid);
        if (it != playerTimes.end() &&
            now - it->second < std::chrono::milliseconds(repeatMs))
            return false;

        // Commit before any yell, summon, or AttackStart callback.
        playerTimes[playerGuid] = now;
        return true;
    }

    bool ShouldScanCivilian(Creature const* creature, uint32 intervalMs)
    {
        auto const now = std::chrono::steady_clock::now();
        std::lock_guard<std::mutex> lock(s_attackReportMutex);
        auto const key = MakeCreatureAttackKey(creature);
        auto [it, inserted] = s_civilianScanDue.try_emplace(key);

        if (inserted)
        {
            // Stagger first scans so all loaded civilians do not search on the
            // same world tick.
            uint32 const offset = intervalMs > 0
                ? creature->GetGUID().GetCounter() % intervalMs
                : 0;
            it->second = now + std::chrono::milliseconds(offset);
        }

        if (now < it->second)
            return false;

        it->second = now + std::chrono::milliseconds(intervalMs);
        return true;
    }

    void ClearCreatureAttackReports(Creature const* creature)
    {
        if (!creature)
            return;

        std::lock_guard<std::mutex> lock(s_attackReportMutex);
        s_attackReports.erase(MakeCreatureAttackKey(creature));
    }

    void ClearCreatureTracking(Creature const* creature)
    {
        if (!creature)
            return;

        std::lock_guard<std::mutex> lock(s_attackReportMutex);
        auto const key = MakeCreatureAttackKey(creature);
        s_attackReports.erase(key);
        s_civilianAlarms.erase(key);
        s_civilianScanDue.erase(key);
    }

    void ClearPlayerAttackReports(ObjectGuid playerGuid)
    {
        std::lock_guard<std::mutex> lock(s_attackReportMutex);
        for (auto it = s_attackReports.begin(); it != s_attackReports.end();)
        {
            it->second.erase(playerGuid);
            if (it->second.empty())
                it = s_attackReports.erase(it);
            else
                ++it;
        }

        for (auto it = s_civilianAlarms.begin(); it != s_civilianAlarms.end();)
        {
            it->second.erase(playerGuid);
            if (it->second.empty())
                it = s_civilianAlarms.erase(it);
            else
                ++it;
        }
    }
}

// =============================================================================
// PlayerScript - Track player kills and combat
// =============================================================================
class ECGuardSystem_PlayerScript : public PlayerScript
{
public:
    ECGuardSystem_PlayerScript() : PlayerScript("ECGuardSystem_PlayerScript") { }

    static bool IsSessionOnlyGuardBot(Player* player)
    {
#ifdef MOD_PLAYERBOTS
        if (!player)
            return false;

        // Persisted cleanup is deliberately RNDbot-only (account_type=1),
        // matching the SQL migrations and preserving AddClass/manual bot
        // characters. Cache by account to avoid one synchronous classification
        // query per character, and retain positive GUIDs through AI teardown.
        static std::mutex classificationMutex;
        static std::unordered_map<uint32, bool> randomAccountCache;
        static std::unordered_set<uint32> randomBotGuids;

        uint32 const guid = player->GetGUID().GetCounter();
        {
            std::lock_guard<std::mutex> lock(classificationMutex);
            if (randomBotGuids.contains(guid))
                return true;
        }

        WorldSession* session = player->GetSession();
        if (!session || !session->IsBot())
            return false;

        uint32 const accountId = session->GetAccountId();
        {
            std::lock_guard<std::mutex> lock(classificationMutex);
            auto cached = randomAccountCache.find(accountId);
            if (cached != randomAccountCache.end())
            {
                if (cached->second)
                    randomBotGuids.insert(guid);
                return cached->second;
            }
        }

        bool const isRandomAccount = sRandomPlayerbotMgr.IsAccountType(accountId, 1);
        {
            std::lock_guard<std::mutex> lock(classificationMutex);
            randomAccountCache[accountId] = isRandomAccount;
            if (isRandomAccount)
                randomBotGuids.insert(guid);
        }
        return isRandomAccount;
#else
        return false;
#endif
    }

    void OnPlayerLogin(Player* player) override
    {
        if (!player)
            return;

        // Skip loading saved wanted state for bots — bots are re-created each
        // server restart so they have no persisted state.  They CAN still become
        // wanted during gameplay (crimes create state at runtime).  The sync DB
        // query blocks the world thread; 80 bots × ~20ms/query = 1.6s freeze.
        if (IsSessionOnlyGuardBot(player))
        {
            // Recover cleanly from residues written by an older binary. This
            // runs after character auras load but before the bot enters play.
            // A delayed witness report from a previous bot session must not
            // attach fresh wanted state to the recycled character GUID.
            sECWitnessSys->CancelAlertsForCriminal(player->GetGUID());
            if (sECBountyHunterMgr->IsEnabled() &&
                sECBountyHunterMgr->HasActiveBounty(player->GetGUID()))
            {
                sECBountyHunterMgr->ClearBounty(player->GetGUID(), BountyStatus::EXPIRED);
            }
            GuardSpells::RemoveAllWantedMarks(player);
            player->RemoveAura(GuardSpells::HUNTED_MARK);
            sECGuardSystemMgr->RemoveWantedState(player->GetGUID());
            CharacterDatabase.Execute(
                "UPDATE ec_bounties SET status = {}, expires_time = COALESCE(expires_time, UTC_TIMESTAMP()) "
                "WHERE target_guid = {} AND status = 0",
                static_cast<uint8>(BountyStatus::EXPIRED),
                player->GetGUID().GetCounter());
            CharacterDatabase.Execute(
                "DELETE FROM ec_guard_wanted_state WHERE guid = {}",
                player->GetGUID().GetCounter());
            return;
        }

        if (!sECGuardSystemMgr->IsEnabled())
            return;

        ECG_LOG_TRACE("OnPlayerLogin: Loading wanted state for %s", player->GetName().c_str());

        // Load saved wanted state from database
        QueryResult result = CharacterDatabase.Query(
            "SELECT crime_points, stars, civilian_kills, guard_kills, crime_zone_id, crime_area_id, crime_faction, last_crime_time "
            "FROM ec_guard_wanted_state WHERE guid = {}",
            player->GetGUID().GetCounter());

        if (result)
        {
            Field* fields = result->Fetch();
            WantedState* state = sECGuardSystemMgr->GetOrCreateWantedState(player->GetGUID());
            if (state)
            {
                state->crimePoints = fields[0].Get<uint32>();
                state->stars = static_cast<WantedStars>(fields[1].Get<uint8>());
                state->civilianKills = fields[2].Get<uint32>();
                state->guardKills = fields[3].Get<uint32>();
                state->crimeZoneId = fields[4].Get<uint32>();
                state->crimeAreaId = fields[5].Get<uint32>();
                state->crimeFaction = static_cast<TeamId>(fields[6].Get<uint8>());

                // Convert saved timestamp to chrono time_point
                uint64 savedTime = fields[7].Get<uint64>();
                auto now = std::chrono::steady_clock::now();
                auto elapsed = std::chrono::system_clock::now().time_since_epoch();
                auto secondsAgo = static_cast<int64_t>(std::chrono::duration_cast<std::chrono::seconds>(elapsed).count()) - static_cast<int64_t>(savedTime);
                state->lastCrimeTime = now - std::chrono::seconds(secondsAgo);

                if (state->IsWanted())
                {
                    ECG_LOG_INFO("OnPlayerLogin: Loaded wanted state for %s: %u stars, %u points, %u civilian kills, %u guard kills",
                        player->GetName().c_str(), static_cast<uint8>(state->stars), state->crimePoints,
                        state->civilianKills, state->guardKills);

                    // Re-apply star-specific wanted mark aura (persists across sessions)
                    uint32 markSpell = GuardSpells::GetWantedMarkForStars(state->stars);
                    if (markSpell > 0)
                    {
                        ECG_LOG_DEBUG("OnPlayerLogin: Applying wanted aura (spell %u) to %s (%u stars)",
                            markSpell, player->GetName().c_str(), static_cast<uint8>(state->stars));
                        player->AddAura(markSpell, player);

                        if (!player->HasAura(markSpell))
                            ECG_LOG_ERROR("OnPlayerLogin: Wanted aura spell %u FAILED to apply on %s!",
                                markSpell, player->GetName().c_str());
                    }

                    // A persisted human wanted state can outlive a missing or
                    // expired bounty row. Re-enter the threshold transition
                    // only when no active bounty exists so login self-heals
                    // the missing row without duplicating hunters or rewards.
                    if (sECBountyHunterMgr->IsEnabled() &&
                        !sECBountyHunterMgr->HasActiveBounty(player->GetGUID()))
                    {
                        sECBountyHunterMgr->OnWantedLevelChanged(
                            player, WantedStars::NONE, state->stars);
                    }

                    // Re-apply HUNTED_MARK if bounty hunters are still tracking this player
                    if (sECBountyHunterMgr->IsEnabled() &&
                        sECBountyHunterMgr->GetActiveHunterCountForTarget(player->GetGUID()) > 0)
                    {
                        if (!player->HasAura(GuardSpells::HUNTED_MARK))
                        {
                            player->AddAura(GuardSpells::HUNTED_MARK, player);
                            ECG_LOG_DEBUG("OnPlayerLogin: Re-applied HUNTED_MARK to %s (active hunters exist)",
                                player->GetName().c_str());
                        }
                    }

                    // Notify player they're still wanted
                    uint8 const starCount = static_cast<uint8>(state->stars);
                    ChatHandler(player->GetSession()).PSendSysMessage(
                        "|cffff0000[WANTED]|r You are still wanted! {} {}! Be careful!",
                        starCount, starCount == 1 ? "star" : "stars");
                }
            }
        }
        else
        {
            ECG_LOG_TRACE("No saved wanted state for %s", player->GetName().c_str());
        }
    }

    void OnPlayerCreatureKill(Player* killer, Creature* killed) override
    {
        if (!sECGuardSystemMgr->IsEnabled())
            return;

        if (!killer || !killed)
            return;

        // Check if the killed creature was a witness (must check before OnPlayerKillCreature
        // because the kill might trigger a new witness check for the same creature)
        sECWitnessSys->OnCreatureDeath(killed, killer);

        bool const allowTownAlarm =
            sECGuardSystemMgr->HasTownGuardResponse(killer, killed) &&
            MarkCivilianAlarmAllowed(killed, killer->GetGUID(),
                sECGuardSystemMgr->GetCivilianAlarmRepeatMs());

        sECGuardSystemMgr->OnPlayerKillCreature(killer, killed,
            allowTownAlarm, "kill-fallback");

        // Preserve the alarm latch through the kill callback so an attack and
        // its killing blow cannot produce two waves, then clear all state for
        // this creature's eventual respawn/removal.
        ClearCreatureTracking(killed);
    }

    void OnPlayerCreatureKilledByPet(Player* petOwner, Creature* killed) override
    {
        // AzerothCore delivers pet/totem killing blows through a separate hook.
        // Route them through the same single kill path as direct player blows.
        OnPlayerCreatureKill(petOwner, killed);
    }

    void OnPlayerPVPKill(Player* killer, Player* killed) override
    {
        if (!sECGuardSystemMgr->IsEnabled())
            return;

        if (!killer || !killed)
            return;

        ECG_LOG_TRACE("OnPlayerPVPKill: %s killed %s", killer->GetName().c_str(), killed->GetName().c_str());

        // Check if killed player has an active bounty - killer claims it
        if (sECBountyHunterMgr->IsEnabled() && sECBountyHunterMgr->HasActiveBounty(killed->GetGUID()))
        {
            ECG_LOG_INFO("Player %s killed wanted criminal %s - claiming bounty!",
                killer->GetName().c_str(), killed->GetName().c_str());
            sECBountyHunterMgr->ClaimBounty(killer, killed);
        }

        // Note: Same-faction PvP is mechanically impossible in WoW 3.3.5
        // (players cannot target or damage their own faction members outside duels).
        // Crime system is cross-faction only: Alliance crimes against Horde NPCs and vice versa.
    }

    // Static zone tracking - shared across all instances
    // Must be declared at class level for cleanup access in OnPlayerLogout
    static inline std::mutex s_lastZoneMutex;
    static inline std::map<ObjectGuid, uint32> s_lastZone;

    void OnPlayerUpdateZone(Player* player, uint32 newZone, uint32 /*newArea*/) override
    {
        if (!sECGuardSystemMgr->IsEnabled())
            return;

        if (!player)
            return;

        // Track zone transitions for ZoneStateManager
        // Thread-safe: using mutex to protect static map access
        uint32 oldZone = 0;
        {
            std::lock_guard<std::mutex> lock(s_lastZoneMutex);
            auto zoneIt = s_lastZone.find(player->GetGUID());
            if (zoneIt != s_lastZone.end())
                oldZone = zoneIt->second;
            s_lastZone[player->GetGUID()] = newZone;
        }

        // Zone state updates outside the lock to avoid potential deadlocks
        if (oldZone != 0 && oldZone != newZone)
        {
            sECZoneStateMgr->OnPlayerLeaveZone(player->GetGUID(), oldZone);
            sECZoneStateMgr->OnPlayerEnterZone(player->GetGUID(), newZone);
        }
        else if (oldZone == 0)
        {
            sECZoneStateMgr->OnPlayerEnterZone(player->GetGUID(), newZone);
        }
        WantedState* state = sECGuardSystemMgr->GetWantedState(player->GetGUID());
        if (!state || !state->IsWanted())
            return;

        ECG_LOG_TRACE("OnPlayerUpdateZone: %s entered zone %u (was in zone %u, crime zone %u)",
            player->GetName().c_str(), newZone, player->GetZoneId(), state->crimeZoneId);

        // Check if player is fleeing from the crime zone
        if (newZone != state->crimeZoneId)
        {
            ECG_LOG_DEBUG("Player %s left crime zone %u, now in zone %u",
                player->GetName().c_str(), state->crimeZoneId, newZone);

            // Check if the new zone has guards of the opposing faction
            const ZoneGuardConfig* newZoneConfig = sECGuardSystemMgr->GetZoneConfig(newZone, state->crimeFaction);
            bool const matchesLocalGuardArea = newZoneConfig &&
                (newZoneConfig->areaId == 0 || newZoneConfig->areaId == player->GetAreaId());

            if (matchesLocalGuardArea)
            {
                // Player entered another zone controlled by the faction they angered
                // Wanted level transfers - guards here will also pursue
                ECG_LOG_INFO("Player %s wanted level transfers to new zone %u (same faction guards)",
                    player->GetName().c_str(), newZone);

                // Notify player
                uint8 const starCount = static_cast<uint8>(state->stars);
                ChatHandler(player->GetSession()).PSendSysMessage(
                    "|cffff0000[WANTED]|r Your crimes are known here! {} {} wanted!",
                    starCount, starCount == 1 ? "star" : "stars");

                // Dispatch guards in the new zone after a brief delay
                sECGuardSystemMgr->DispatchGuards(player, nullptr);
            }
            else
            {
                // Player escaped to a zone without guards of that faction
                // Start faster decay - they're "hiding"
                ECG_LOG_INFO("Player %s escaped to neutral/safe zone %u - wanted level will decay faster",
                    player->GetName().c_str(), newZone);

                // Notify player
                ChatHandler(player->GetSession()).PSendSysMessage(
                    "|cffffff00[WANTED]|r You've escaped to safety. Keep a low profile and your wanted level will decay.");

                // Accelerate decay by resetting the last crime time
                // (Makes it seem like time since last crime is longer)
                state->lastCrimeTime -= std::chrono::seconds(30);
            }
        }
    }

    void OnPlayerUpdateArea(Player* player, uint32 oldArea, uint32 newArea) override
    {
        if (!sECGuardSystemMgr->IsEnabled() || !player || oldArea == newArea)
            return;

        // AzerothCore reports same-zone subarea boundaries through this hook,
        // not OnPlayerUpdateZone. Re-evaluate the legacy settlement state so
        // Astranaar/Crossroads phases neither follow a player elsewhere in the
        // larger zone nor miss a criminal who walks back into the settlement.
        uint32 const zoneId = player->GetZoneId();
        sECZoneStateMgr->OnPlayerLeaveZone(player->GetGUID(), zoneId);
        sECZoneStateMgr->OnPlayerEnterZone(player->GetGUID(), zoneId);
    }

    void OnPlayerBeforeLogout(Player* player) override
    {
        if (!player)
            return;

        if (IsSessionOnlyGuardBot(player))
        {
            // WorldSession saves the character (including character_aura)
            // after this hook and before OnPlayerLogout. Remove permanent
            // wanted/hunted marks here so they cannot be persisted.
            sECWitnessSys->CancelAlertsForCriminal(player->GetGUID());
            GuardSpells::RemoveAllWantedMarks(player);
            player->RemoveAura(GuardSpells::HUNTED_MARK);
        }
    }

    void OnPlayerLogout(Player* player) override
    {
        if (!player)
            return;

        ClearPlayerAttackReports(player->GetGUID());

        bool const sessionOnlyBot = IsSessionOnlyGuardBot(player);
        if (sessionOnlyBot)
        {
            // Defense in depth for removal paths where OnPlayerBeforeLogout
            // was skipped: no queued witness callback may outlive this bot
            // session and recreate wanted state after teardown.
            sECWitnessSys->CancelAlertsForCriminal(player->GetGUID());
        }
        if (!sECGuardSystemMgr->IsEnabled())
        {
            if (sessionOnlyBot)
            {
                GuardSpells::RemoveAllWantedMarks(player);
                player->RemoveAura(GuardSpells::HUNTED_MARK);
                sECGuardSystemMgr->RemoveWantedState(player->GetGUID());
                CharacterDatabase.Execute(
                    "DELETE FROM ec_guard_wanted_state WHERE guid = {}",
                    player->GetGUID().GetCounter());
            }
            return;
        }

        // Remove the player from strategic incident tracking before the world
        // object disappears. Leaving an offline GUID enrolled can keep a zone
        // moving through threat phases with nobody actually present.
        uint32 trackedZone = player->GetZoneId();
        {
            std::lock_guard<std::mutex> lock(s_lastZoneMutex);
            auto zoneIt = s_lastZone.find(player->GetGUID());
            if (zoneIt != s_lastZone.end())
            {
                trackedZone = zoneIt->second;
                s_lastZone.erase(zoneIt);
            }
        }
        if (trackedZone != 0)
            sECZoneStateMgr->OnPlayerLeaveZone(player->GetGUID(), trackedZone);

        WantedState* state = sECGuardSystemMgr->GetWantedState(player->GetGUID());
        if (state && state->crimeZoneId != 0 && state->crimeZoneId != trackedZone)
            sECZoneStateMgr->OnPlayerLeaveZone(player->GetGUID(), state->crimeZoneId);

        // Bot wanted state is intentionally not loaded on login because random
        // bots are recreated each server run. Saving it here made that policy
        // asymmetric and recreated phantom wanted rows/bounties after every
        // graceful restart. Keep bot crime state session-local instead.
        if (sessionOnlyBot)
        {
            bool const hadWantedState = state && state->IsWanted();
            if (sECBountyHunterMgr->IsEnabled() &&
                (hadWantedState || sECBountyHunterMgr->HasActiveBounty(player->GetGUID())))
                sECBountyHunterMgr->ClearBounty(player->GetGUID(), BountyStatus::EXPIRED);

            GuardSpells::RemoveAllWantedMarks(player);
            player->RemoveAura(GuardSpells::HUNTED_MARK);
            sECGuardSystemMgr->RemoveWantedState(player->GetGUID());
            CharacterDatabase.Execute(
                "DELETE FROM ec_guard_wanted_state WHERE guid = {}",
                player->GetGUID().GetCounter());

            ECG_LOG_TRACE("OnPlayerLogout: Cleared session-only bot wanted state for %s",
                player->GetName().c_str());
            return;
        }

        if (!state || !state->IsWanted())
        {
            // Clean up memory - erase unwanted state entries on logout
            // This prevents m_wantedStates from growing unboundedly
            sECGuardSystemMgr->RemoveWantedState(player->GetGUID());

            ECG_LOG_TRACE("OnPlayerLogout: %s has no wanted level, cleaned up state entry",
                player->GetName().c_str());
            return;
        }

        ECG_LOG_INFO("OnPlayerLogout: Saving wanted state for %s (stars=%u, points=%u)",
            player->GetName().c_str(), static_cast<uint8>(state->stars), state->crimePoints);

        // Save wanted level to database for persistence across sessions
        // Uses the characters database since it's per-character data
        CharacterDatabase.Execute(
            "REPLACE INTO ec_guard_wanted_state (guid, crime_points, stars, civilian_kills, guard_kills, crime_zone_id, crime_area_id, crime_faction, last_crime_time) "
            "VALUES ({}, {}, {}, {}, {}, {}, {}, {}, UNIX_TIMESTAMP())",
            player->GetGUID().GetCounter(),
            state->crimePoints,
            static_cast<uint8>(state->stars),
            state->civilianKills,
            state->guardKills,
            state->crimeZoneId,
            state->crimeAreaId,
            static_cast<uint8>(state->crimeFaction)
        );

        ECG_LOG_DEBUG("Saved wanted state for player GUID %u to database",
            player->GetGUID().GetCounter());
    }

    void OnPlayerKilledByCreature(Creature* killer, Player* killed) override
    {
        if (!sECGuardSystemMgr->IsEnabled())
            return;

        if (!killer || !killed)
            return;

        // Check if this creature is a bounty hunter that killed its target
        if (sECBountyHunterMgr->IsEnabled())
        {
            // Let the bounty hunter manager know the target was killed
            sECBountyHunterMgr->OnTargetKilledByHunter(killed, killer);
        }
    }

    void OnPlayerJustDied(Player* player) override
    {
        if (!player)
            return;

        ClearPlayerAttackReports(player->GetGUID());

        if (!sECGuardSystemMgr->IsEnabled())
            return;

        // Death clears wanted state, so discard reports for crimes committed before death.
        // This also prevents their delayed confirmation from applying an aura to a corpse.
        sECWitnessSys->CancelAlertsForCriminal(player->GetGUID());

        // GTA-style: death clears your wanted level
        WantedStars oldStars = sECGuardSystemMgr->GetPlayerWantedStars(player->GetGUID());
        if (oldStars == WantedStars::NONE)
            return;

        ECG_LOG_INFO("Player %s died - clearing wanted level (%u stars) GTA-style",
            player->GetName().c_str(), static_cast<uint8>(oldStars));

        // Clear wanted state
        sECGuardSystemMgr->ClearWanted(player);

        // Clear any active bounties
        if (sECBountyHunterMgr->IsEnabled())
        {
            sECBountyHunterMgr->ClearBounty(player->GetGUID(), BountyStatus::EXPIRED);
        }

        // Delete persisted wanted state from DB
        CharacterDatabase.Execute(
            "DELETE FROM ec_guard_wanted_state WHERE guid = {}",
            player->GetGUID().GetCounter());
    }

    void OnPlayerMapChanged(Player* player) override
    {
        if (player)
            ClearPlayerAttackReports(player->GetGUID());
    }
};

// =============================================================================
// UnitScript - Track damage dealt to NPCs
// =============================================================================
class ECGuardSystem_UnitScript : public UnitScript
{
public:
    ECGuardSystem_UnitScript() : UnitScript("ECGuardSystem_UnitScript") { }

    void OnUnitEnterCombat(Unit* /*unit*/, Unit* /*enemy*/) override
    {
        // Damage is the authoritative attack signal. Enter-combat is fired
        // before the first damage is applied; reporting here can make a victim
        // appear to speak from its corpse after a one-shot killing blow.
    }

    void OnDamage(Unit* attacker, Unit* victim, uint32& damage) override
    {
        if (!sECGuardSystemMgr->IsEnabled())
            return;

        if (!attacker || !victim || damage == 0)
            return;

        // Preserve attack direction: only player-owned damage to a creature can
        // report a crime. A guard initiating combat against a player therefore
        // cannot recursively charge the target or summon another guard wave.
        Player* player = attacker->GetCharmerOrOwnerPlayerOrPlayerItself();
        Creature* creature = victim->ToCreature();
        if (!player || !player->IsInWorld() || !player->IsAlive() ||
            !creature || !creature->IsInWorld() ||
            creature->IsDuringRemoveFromWorld() || !creature->IsAlive())
            return;

        // Do not retain combat-latch entries for unrelated wildlife/mobs.
        if (!sECGuardSystemMgr->IsCreatureCivilian(creature) &&
            !sECGuardSystemMgr->IsCreatureGuard(creature))
            return;

        // OnDamage runs before health is deducted. A lethal hit must not emit
        // victim speech. The subsequent kill hook records the kill and can
        // dispatch a deliberately silent local response.
        if (damage >= creature->GetHealth())
        {
            ECG_LOG_TRACE("LETHAL DAMAGE ROUTE: attacker=%s victim=%s entry=%u damage=%u health=%u speech=suppressed",
                player->GetName().c_str(), creature->GetName().c_str(), creature->GetEntry(),
                damage, creature->GetHealth());
            return;
        }

        // OnDamage runs after combat begins and before health is deducted, so
        // combat/full-health tests are not reliable first-hit indicators. Latch
        // each player/creature pair until evade, death, or world removal.
        if (!MarkCreatureAttackReported(creature, player->GetGUID()))
            return;

        bool const allowTownAlarm =
            sECGuardSystemMgr->HasTownGuardResponse(player, creature) &&
            MarkCivilianAlarmAllowed(creature, player->GetGUID(),
                sECGuardSystemMgr->GetCivilianAlarmRepeatMs());

        sECGuardSystemMgr->OnPlayerAttackCreature(player, creature,
            allowTownAlarm, "damage-fallback");
    }

    void OnUnitEnterEvadeMode(Unit* unit, uint8 /*evadeReason*/) override
    {
        Creature* creature = unit ? unit->ToCreature() : nullptr;
        if (!creature)
            return;

        ClearCreatureAttackReports(creature);
    }

    void OnUnitDeath(Unit* unit, Unit* /*killer*/) override
    {
        Creature* creature = unit ? unit->ToCreature() : nullptr;
        if (!creature)
            return;

        ClearCreatureAttackReports(creature);
    }
};

// =============================================================================
// WorldScript - Global update tick and config loading
// =============================================================================
class ECGuardSystem_WorldScript : public WorldScript
{
public:
    ECGuardSystem_WorldScript() : WorldScript("ECGuardSystem_WorldScript") { }

    void OnAfterConfigLoad(bool reload) override
    {
        sECGuardSystemMgr->LoadFromConfig();
        sECZoneStateMgr->LoadFromConfig();
        sECZoneEnvironmentMgr->LoadFromConfig();
        sECBountyHunterMgr->LoadFromConfig();
        sECWitnessSys->LoadFromConfig();

        if (reload)
        {
            sECGuardSystemMgr->LoadZoneGuardData();
            sECZoneStateMgr->LoadZoneStates();
        }
    }

    void OnStartup() override
    {
        sECGuardSystemMgr->Initialize();
        sECZoneStateMgr->Initialize();
        sECZoneEnvironmentMgr->Initialize();
        sECBountyHunterMgr->Initialize();
        sECWitnessSys->LoadFromConfig();
    }

    void OnUpdate(uint32 diff) override
    {
        sECGuardSystemMgr->Update(diff);
        sECZoneStateMgr->Update(diff);
        sECZoneEnvironmentMgr->Update(diff);
        sECBountyHunterMgr->Update(diff);
        sECWitnessSys->Update(diff);
    }
};

// =============================================================================
// AllCreatureScript - Track spawned guards and their behavior
// =============================================================================
class ECGuardSystem_AllCreatureScript : public AllCreatureScript
{
public:
    ECGuardSystem_AllCreatureScript() : AllCreatureScript("ECGuardSystem_AllCreatureScript") { }

    void OnAllCreatureUpdate(Creature* creature, uint32 diff) override
    {
        if (!sECGuardSystemMgr->IsEnabled())
            return;

        if (!creature || !creature->IsAlive())
            return;

        // AzerothCore intentionally prevents civilians from starting normal
        // aggro, while original WotLK's CALLS_GUARDS behavior still reacted to
        // an opposing player entering the civilian's aggro radius. Recreate
        // that narrow behavior without replacing the NPC's normal AI.
        if (sECGuardSystemMgr->IsCivilianProximityEnabled() &&
            creature->IsCivilian() &&
            sECGuardSystemMgr->IsTownCivilianCandidate(creature) &&
            ShouldScanCivilian(creature,
                sECGuardSystemMgr->GetCivilianProximityScanIntervalMs()))
        {
            Player* nearestHostile = nullptr;
            float nearestDistance = 0.0f;

            creature->DoForAllVisiblePlayers(
                [creature, &nearestHostile, &nearestDistance](Player* player)
                {
                    if (!player || !player->IsAlive() || player->IsGameMaster() ||
                        player->IsSpectator() || !creature->IsHostileTo(player) ||
                        !player->isTargetableForAttack(false, creature) ||
                        !creature->CanSeeOrDetect(player, false, true))
                        return;

                    float const aggroRange = creature->GetAggroRange(player);
                    if (!creature->IsWithinDistInMap(player, aggroRange))
                        return;

                    float const distance = creature->GetDistance(player);
                    if (!nearestHostile || distance < nearestDistance)
                    {
                        nearestHostile = player;
                        nearestDistance = distance;
                    }
                });

            if (nearestHostile &&
                sECGuardSystemMgr->HasTownGuardResponse(nearestHostile, creature) &&
                MarkCivilianAlarmAllowed(creature, nearestHostile->GetGUID(),
                    sECGuardSystemMgr->GetCivilianAlarmRepeatMs()))
            {
                sECGuardSystemMgr->OnHostilePlayerNearCivilian(
                    nearestHostile, creature);
            }
        }

        // Only process guards that we spawned
        if (!sECGuardSystemMgr->IsSpawnedGuard(creature->GetGUID()))
            return;

        if (creature->IsInCombat())
        {
            // Elite guard spell casting (veteran+ guards use real WoW spells)
            sECGuardSystemMgr->UpdateGuardSpells(creature, diff);

            // If guard's target escaped (too far), log it
            Unit* target = creature->GetVictim();
            if (target && target->GetTypeId() == TYPEID_PLAYER)
            {
                Player* player = target->ToPlayer();
                float distance = creature->GetDistance(player);

                // If player is fleeing (> 60 yards away), log it
                if (distance > 60.0f)
                {
                    ECG_LOG_DEBUG("Guard %s (entry %u) losing pursuit of %s at distance %.1f yards",
                        creature->GetName().c_str(), creature->GetEntry(),
                        player->GetName().c_str(), distance);
                }
            }
        }
    }

    void OnCreatureRemoveWorld(Creature* creature) override
    {
        if (!creature)
            return;

        bool const spawnedGuard = sECGuardSystemMgr->IsEnabled() &&
            sECGuardSystemMgr->IsSpawnedGuard(creature->GetGUID());

        ClearCreatureTracking(creature);

        if (!sECGuardSystemMgr->IsEnabled())
            return;

        // Check if a spawned guard was removed (despawned/died)
        if (spawnedGuard)
        {
            ECG_LOG_INFO("Temporary guard %s (entry %u) removed from world; timed out, died, or unloaded",
                creature->GetName().c_str(), creature->GetEntry());

            // Clean up guard tracking
            sECGuardSystemMgr->UnregisterSpawnedGuard(creature->GetGUID());
        }
    }
};

// =============================================================================
// CommandScript - Admin commands for the guard system
// =============================================================================
class ECGuardSystem_CommandScript : public CommandScript
{
public:
    ECGuardSystem_CommandScript() : CommandScript("ECGuardSystem_CommandScript") { }

    ChatCommandTable GetCommands() const override
    {
        static ChatCommandTable bountyCommandTable =
        {
            { "list",   HandleBountyListCommand,   SEC_PLAYER,     Console::Yes },
            { "clear",  HandleBountyClearCommand,  SEC_GAMEMASTER, Console::No },
        };

        static ChatCommandTable ecGuardCommandTable =
        {
            { "status",     HandleGuardStatusCommand,     SEC_PLAYER,         Console::No },
            { "clear",      HandleGuardClearCommand,      SEC_GAMEMASTER,     Console::No },
            { "setstars",   HandleGuardSetStarsCommand,   SEC_GAMEMASTER,     Console::No },
            { "reload",     HandleGuardReloadCommand,     SEC_ADMINISTRATOR,  Console::Yes },
            { "debug",      HandleGuardDebugCommand,      SEC_GAMEMASTER,     Console::Yes },
            { "loglevel",   HandleGuardLogLevelCommand,   SEC_ADMINISTRATOR,  Console::Yes },
            { "ingamelog",  HandleGuardInGameLogCommand,  SEC_GAMEMASTER,     Console::Yes },
            { "zone",       HandleGuardZoneCommand,       SEC_GAMEMASTER,     Console::No },
            { "zones",      HandleGuardZonesCommand,      SEC_GAMEMASTER,     Console::Yes },
            { "security",   HandleGuardSecurityCommand,   SEC_GAMEMASTER,     Console::No },
            { "seczones",   HandleGuardSecZonesCommand,   SEC_GAMEMASTER,     Console::Yes },
            { "bounty",     bountyCommandTable },
        };

        static ChatCommandTable commandTable =
        {
            { "ecguard",    ecGuardCommandTable },
            { "bounty",     bountyCommandTable },
        };

        return commandTable;
    }

    static bool HandleGuardStatusCommand(ChatHandler* handler, Optional<PlayerIdentifier> target)
    {
        Player* player = nullptr;
        if (target)
            player = target->GetConnectedPlayer();
        else
            player = handler->GetPlayer();

        if (!player)
        {
            handler->SendSysMessage("Player not found.");
            return false;
        }

        WantedState* state = sECGuardSystemMgr->GetWantedState(player->GetGUID());

        if (!state || !state->IsWanted())
        {
            handler->PSendSysMessage("%s is not wanted.", player->GetName().c_str());
            return true;
        }

        handler->PSendSysMessage("Wanted Status for %s:", player->GetName().c_str());
        handler->PSendSysMessage("  Stars: %u", static_cast<uint8>(state->stars));
        handler->PSendSysMessage("  Crime Points: %u", state->crimePoints);
        handler->PSendSysMessage("  Civilian Kills: %u", state->civilianKills);
        handler->PSendSysMessage("  Guard Kills: %u", state->guardKills);
        handler->PSendSysMessage("  Active Guards: %u", state->activeGuardCount);

        return true;
    }

    static bool HandleGuardClearCommand(ChatHandler* handler, Optional<PlayerIdentifier> target)
    {
        Player* player = nullptr;
        if (target)
            player = target->GetConnectedPlayer();
        else
            player = handler->GetPlayer();

        if (!player)
        {
            handler->SendSysMessage("Player not found.");
            return false;
        }

        sECGuardSystemMgr->ClearWanted(player);
        handler->PSendSysMessage("Cleared wanted level for %s.", player->GetName().c_str());

        return true;
    }

    static bool HandleGuardSetStarsCommand(ChatHandler* handler, Optional<PlayerIdentifier> target, uint8 stars)
    {
        Player* player = nullptr;
        if (target)
            player = target->GetConnectedPlayer();
        else
            player = handler->GetPlayer();

        if (!player)
        {
            handler->SendSysMessage("Player not found.");
            return false;
        }

        if (stars > 5)
        {
            handler->SendSysMessage("Stars must be between 0 and 5.");
            return false;
        }

        WantedState* state = sECGuardSystemMgr->GetOrCreateWantedState(player->GetGUID());
        if (!state)
        {
            handler->SendSysMessage("Failed to create wanted state.");
            return false;
        }

        // Set crime points to match the star level
        const auto& config = sECGuardSystemMgr->GetEscalationConfig();
        switch (stars)
        {
            case 0: state->crimePoints = 0; break;
            case 1: state->crimePoints = config.pointsFor1Star; break;
            case 2: state->crimePoints = config.pointsFor2Star; break;
            case 3: state->crimePoints = config.pointsFor3Star; break;
            case 4: state->crimePoints = config.pointsFor4Star; break;
            case 5: state->crimePoints = config.pointsFor5Star; break;
        }

        state->UpdateStars(
            config.pointsFor1Star,
            config.pointsFor2Star,
            config.pointsFor3Star,
            config.pointsFor4Star,
            config.pointsFor5Star
        );

        handler->PSendSysMessage("Set wanted level for {} to {} {}.",
            player->GetName().c_str(), stars, stars == 1 ? "star" : "stars");

        // Notify the player
        if (stars > 0)
        {
            ChatHandler(player->GetSession()).PSendSysMessage(
                "|cffff0000[WANTED]|r Your wanted level is now {} {}!",
                stars, stars == 1 ? "star" : "stars");

            // IMPORTANT: Dispatch guards to the player's current location
            // This makes the command work in any zone with a guard config
            state->crimeZoneId = player->GetZoneId();
            state->crimeFaction = sECGuardSystemMgr->GetZoneFaction(player->GetZoneId());
            state->lastCrimeTime = std::chrono::steady_clock::now();

            // Try to dispatch guards (will use zone config if available)
            sECGuardSystemMgr->DispatchGuards(player, nullptr);

            // Report zone info to GM for debugging
            const ZoneGuardConfig* zoneConfig = sECGuardSystemMgr->GetZoneConfig(
                player->GetZoneId(), state->crimeFaction);

            if (!zoneConfig)
            {
                // Try neutral zone config as fallback
                zoneConfig = sECGuardSystemMgr->GetZoneConfig(player->GetZoneId(), TEAM_NEUTRAL);
            }

            bool const matchesLocalGuardArea = zoneConfig &&
                (zoneConfig->areaId == 0 || zoneConfig->areaId == player->GetAreaId());
            if (matchesLocalGuardArea)
            {
                handler->PSendSysMessage("  Zone %u area %u has a matching guard config. Guards dispatched.",
                    player->GetZoneId(), player->GetAreaId());
            }
            else
            {
                handler->PSendSysMessage("  |cffff0000WARNING:|r Zone %u area %u has no matching guard config. No guards spawned.",
                    player->GetZoneId(), player->GetAreaId());
                handler->SendSysMessage("  Use SQL to add an area-valid guard config or teleport to a configured settlement.");
            }
        }
        else
        {
            ChatHandler(player->GetSession()).SendSysMessage("|cff00ff00You are no longer wanted.|r");
        }

        return true;
    }

    static bool HandleGuardReloadCommand(ChatHandler* handler)
    {
        sECGuardSystemMgr->LoadFromConfig();
        sECGuardSystemMgr->LoadZoneGuardData();
        handler->SendSysMessage("EC Guard System configuration reloaded.");
        ECG_LOG_INFO("Configuration reloaded by GM");
        return true;
    }

    static bool HandleGuardDebugCommand(ChatHandler* handler, Optional<std::string> arg)
    {
        if (!arg)
        {
            handler->PSendSysMessage("Debug mode is currently %s",
                ECGuardDebug::IsDebugEnabled() ? "|cff00ff00ON|r" : "|cffff0000OFF|r");
            handler->PSendSysMessage("Log level: %u (%s)",
                ECGuardDebug::GetLogLevel(),
                ECGuardDebug::LogLevelToString(static_cast<ECGLogLevel>(ECGuardDebug::GetLogLevel())));
            handler->PSendSysMessage("In-game logging: %s",
                ECGuardDebug::IsInGameLoggingEnabled() ? "|cff00ff00ON|r" : "|cffff0000OFF|r");
            return true;
        }

        std::string mode = *arg;
        if (mode == "on" || mode == "1" || mode == "true")
        {
            ECGuardDebug::SetDebugEnabled(true);
            handler->SendSysMessage("EC Guard System debug mode |cff00ff00ENABLED|r");
            ECG_LOG_INFO("Debug mode enabled by GM");
        }
        else if (mode == "off" || mode == "0" || mode == "false")
        {
            ECGuardDebug::SetDebugEnabled(false);
            handler->SendSysMessage("EC Guard System debug mode |cffff0000DISABLED|r");
        }
        else
        {
            handler->SendSysMessage("Usage: .ecguard debug [on|off]");
        }

        return true;
    }

    static bool HandleGuardLogLevelCommand(ChatHandler* handler, Optional<uint8> level)
    {
        if (!level)
        {
            handler->PSendSysMessage("Current log level: %u (%s)",
                ECGuardDebug::GetLogLevel(),
                ECGuardDebug::LogLevelToString(static_cast<ECGLogLevel>(ECGuardDebug::GetLogLevel())));
            handler->SendSysMessage("Levels: 0=ERROR, 1=WARNING, 2=INFO, 3=DEBUG, 4=TRACE");
            return true;
        }

        if (*level > 4)
        {
            handler->SendSysMessage("Log level must be 0-4");
            return false;
        }

        ECGuardDebug::SetLogLevel(*level);
        handler->PSendSysMessage("Log level set to %u (%s)",
            *level, ECGuardDebug::LogLevelToString(static_cast<ECGLogLevel>(*level)));
        ECG_LOG_INFO("Log level changed to %u by GM", *level);
        return true;
    }

    static bool HandleGuardInGameLogCommand(ChatHandler* handler, Optional<std::string> arg)
    {
        if (!arg)
        {
            handler->PSendSysMessage("In-game logging is currently %s",
                ECGuardDebug::IsInGameLoggingEnabled() ? "|cff00ff00ON|r" : "|cffff0000OFF|r");
            handler->SendSysMessage("When enabled, log messages appear in-game for GMs.");
            return true;
        }

        std::string mode = *arg;
        if (mode == "on" || mode == "1" || mode == "true")
        {
            ECGuardDebug::SetInGameLogging(true);
            handler->SendSysMessage("In-game logging |cff00ff00ENABLED|r - logs will appear in-game");
            ECG_LOG_INFO("In-game logging enabled by GM");
        }
        else if (mode == "off" || mode == "0" || mode == "false")
        {
            ECGuardDebug::SetInGameLogging(false);
            handler->SendSysMessage("In-game logging |cffff0000DISABLED|r");
        }
        else
        {
            handler->SendSysMessage("Usage: .ecguard ingamelog [on|off]");
        }

        return true;
    }

    static bool HandleGuardZoneCommand(ChatHandler* handler, Optional<uint32> zoneId)
    {
        Player* player = handler->GetPlayer();
        uint32 targetZone = zoneId ? *zoneId : (player ? player->GetZoneId() : 0);

        if (targetZone == 0)
        {
            handler->SendSysMessage("Usage: .ecguard zone [zoneId]");
            return false;
        }

        ZoneState* zone = sECZoneStateMgr->GetZoneState(targetZone);
        if (!zone)
        {
            handler->PSendSysMessage("Zone %u has no threat tracking configured.", targetZone);
            return true;
        }

        handler->PSendSysMessage("=== Zone Threat Status: %s (ID: %u) ===",
            zone->zoneName.c_str(), targetZone);
        handler->PSendSysMessage("  Faction: %s",
            zone->owningFaction == TEAM_ALLIANCE ? "Alliance" :
            zone->owningFaction == TEAM_HORDE ? "Horde" : "Neutral");
        handler->PSendSysMessage("  Heat: %u (Threat Level: %u/5)", zone->heatPoints, zone->threatLevel);
        handler->PSendSysMessage("  Guards: %u/%u (%.1f%% survival)",
            zone->guardsAlive, zone->initialGuardCount, zone->GetGuardSurvivalRate() * 100.0f);
        handler->PSendSysMessage("  Alarm State: |cff%s%s|r",
            zone->alarmState == AlarmState::QUIET ? "00ff00" :
            zone->alarmState == AlarmState::UNEASY ? "ffff00" :
            zone->alarmState == AlarmState::MOBILIZING ? "ff8800" :
            zone->alarmState == AlarmState::ATTACKED ? "ff0000" :
            zone->alarmState == AlarmState::OCCUPIED ? "990000" : "00ffff",
            ZoneStateManager::AlarmStateToString(zone->alarmState));
        handler->PSendSysMessage("  Control: |cff%s%s|r",
            zone->controlState == ControlState::STABLE ? "00ff00" :
            zone->controlState == ControlState::CONTESTED ? "ffff00" : "ff0000",
            ZoneStateManager::ControlStateToString(zone->controlState));
        handler->PSendSysMessage("  Phase: %s", ZoneStateManager::ZonePhaseToString(zone->currentPhase));
        handler->PSendSysMessage("  Active Criminals: %zu", zone->activeCriminals.size());
        handler->PSendSysMessage("  Bounty Hunters: %zu", zone->activeBountyHunters.size());

        return true;
    }

    static bool HandleGuardZonesCommand(ChatHandler* handler)
    {
        auto activeZones = sECZoneStateMgr->GetAllActiveZones();
        auto fallenZones = sECZoneStateMgr->GetFallenZones();

        handler->SendSysMessage("=== Zone Threat Overview ===");

        if (activeZones.empty())
        {
            handler->SendSysMessage("  No zones currently under threat.");
        }
        else
        {
            handler->PSendSysMessage("  Active Zones: %zu", activeZones.size());
            for (const auto* zone : activeZones)
            {
                handler->PSendSysMessage("    - %s (ID: %u): Heat %u, Threat %u, Phase %s",
                    zone->zoneName.c_str(), zone->zoneId, zone->heatPoints, zone->threatLevel,
                    ZoneStateManager::ZonePhaseToString(zone->currentPhase));
            }
        }

        if (!fallenZones.empty())
        {
            handler->PSendSysMessage("  |cffff0000FALLEN ZONES: %zu|r", fallenZones.size());
            for (const auto* zone : fallenZones)
            {
                handler->PSendSysMessage("    - |cffff0000%s|r (ID: %u): Guards %u/%u",
                    zone->zoneName.c_str(), zone->zoneId, zone->guardsAlive, zone->initialGuardCount);
            }
        }

        return true;
    }

    // =========================================================================
    // ZONE SECURITY STATE MACHINE COMMANDS
    // =========================================================================

    static bool HandleGuardSecurityCommand(ChatHandler* handler, Optional<uint32> zoneId)
    {
        Player* player = handler->GetPlayer();
        uint32 targetZone = zoneId ? *zoneId : (player ? player->GetZoneId() : 0);

        if (targetZone == 0)
        {
            handler->SendSysMessage("Usage: .ecguard security [zoneId]");
            return false;
        }

        // Check both Alliance and Horde security states
        handler->PSendSysMessage("=== Zone Security State: Zone %u ===", targetZone);

        bool foundAny = false;
        for (uint8 factionIdx = 0; factionIdx <= 2; ++factionIdx)
        {
            TeamId faction = static_cast<TeamId>(factionIdx);
            ZoneSecurityState* state = sZoneSecurityMgr->GetZoneState(targetZone, faction);

            if (!state)
                continue;

            foundAny = true;
            const char* factionName = faction == TEAM_ALLIANCE ? "Alliance" :
                                      faction == TEAM_HORDE ? "Horde" : "Neutral";

            handler->PSendSysMessage("  --- %s Security ---", factionName);
            handler->PSendSysMessage("    Phase: |cff%s%s|r",
                state->GetPhase() == SecurityPhase::PEACEFUL ? "00ff00" :
                state->GetPhase() == SecurityPhase::ALERT ? "ffff00" :
                state->GetPhase() == SecurityPhase::ACTIVE_PURSUIT ? "ff8800" :
                state->GetPhase() == SecurityPhase::LOCKDOWN ? "ff0000" : "00ffff",
                SecurityPhaseToString(state->GetPhase()));
            handler->PSendSysMessage("    Heat: %u (Threshold: Alert=%u, Pursuit=%u, Lockdown=%u)",
                state->GetHeatLevel(),
                state->GetConfig().alertThreshold,
                state->GetConfig().pursuitThreshold,
                state->GetConfig().lockdownThreshold);
            handler->PSendSysMessage("    Time in Phase: %u sec",
                state->GetPhaseTimeMs() / 1000);
            handler->PSendSysMessage("    Active Criminals: %u, Active Guards: %u",
                state->GetActiveCriminalCount(),
                state->GetActiveGuardCount());

            // Show transition info
            if (state->GetPhase() == SecurityPhase::ALERT)
            {
                handler->PSendSysMessage("    |cffffcc00Escalates to ACTIVE_PURSUIT if heat >= %u|r",
                    state->GetConfig().pursuitThreshold);
            }
            else if (state->GetPhase() == SecurityPhase::LOCKDOWN)
            {
                handler->PSendSysMessage("    |cffff0000LOCKDOWN: Maximum guard response active|r");
            }
            else if (state->GetPhase() == SecurityPhase::COOLDOWN)
            {
                uint32 cooldownRemaining = (state->GetConfig().cooldownDurationMs - state->GetPhaseTimeMs()) / 1000;
                handler->PSendSysMessage("    |cff00ffffCooldown: %u sec remaining|r", cooldownRemaining);
            }
        }

        if (!foundAny)
        {
            handler->PSendSysMessage("  No security states configured for zone %u.", targetZone);
            handler->SendSysMessage("  Use '.ecguard zone' for legacy zone state info.");
        }

        return true;
    }

    static bool HandleGuardSecZonesCommand(ChatHandler* handler)
    {
        handler->SendSysMessage("=== Zone Security State Overview ===");

        // Get all zones with non-peaceful security
        auto allStates = sZoneSecurityMgr->GetAllNonPeacefulStates();

        if (allStates.empty())
        {
            handler->SendSysMessage("  All zones are currently PEACEFUL.");
            return true;
        }

        handler->PSendSysMessage("  Non-Peaceful Zones: %zu", allStates.size());

        for (const auto& [zoneId, faction, state] : allStates)
        {
            const char* factionName = faction == TEAM_ALLIANCE ? "A" :
                                      faction == TEAM_HORDE ? "H" : "N";
            const char* phaseColor =
                state->GetPhase() == SecurityPhase::ALERT ? "ffff00" :
                state->GetPhase() == SecurityPhase::ACTIVE_PURSUIT ? "ff8800" :
                state->GetPhase() == SecurityPhase::LOCKDOWN ? "ff0000" : "00ffff";

            handler->PSendSysMessage("    - Zone %u [%s]: |cff%s%s|r (Heat: %u, Criminals: %u)",
                zoneId, factionName, phaseColor,
                SecurityPhaseToString(state->GetPhase()),
                state->GetHeatLevel(),
                state->GetActiveCriminalCount());
        }

        // Show lockdown count
        uint32 lockdownCount = 0;
        for (const auto& [zoneId, faction, state] : allStates)
        {
            if (state->GetPhase() == SecurityPhase::LOCKDOWN)
                ++lockdownCount;
        }

        if (lockdownCount > 0)
        {
            handler->PSendSysMessage("  |cffff0000ZONES IN LOCKDOWN: %u|r", lockdownCount);
        }

        return true;
    }

    // =========================================================================
    // Bounty Commands
    // =========================================================================

    static bool HandleBountyListCommand(ChatHandler* handler)
    {
        if (!sECBountyHunterMgr->IsEnabled())
        {
            handler->SendSysMessage("|cffff0000Bounty system is disabled.|r");
            return true;
        }

        auto bounties = sECBountyHunterMgr->GetAllActiveBounties();

        handler->SendSysMessage("|cffffd700=== Active Bounties ===|r");

        if (bounties.empty())
        {
            handler->SendSysMessage("  No active bounties.");
            return true;
        }

        for (const auto& bounty : bounties)
        {
            const char* starColor =
                bounty.wantedLevel >= WantedStars::FIVE_STAR ? "ff0000" :
                bounty.wantedLevel >= WantedStars::FOUR_STAR ? "ff4400" :
                bounty.wantedLevel >= WantedStars::THREE_STAR ? "ff8800" :
                bounty.wantedLevel >= WantedStars::TWO_STAR ? "ffcc00" : "ffff00";

            handler->PSendSysMessage("  |cff{}{}|r - {} {} - |cffffd700{} gold|r - Last seen: {}",
                starColor, bounty.targetName.c_str(),
                static_cast<uint32>(bounty.wantedLevel),
                bounty.wantedLevel == WantedStars::ONE_STAR ? "star" : "stars",
                bounty.rewardGold / 10000,
                bounty.lastKnownZoneName.c_str());
        }

        handler->PSendSysMessage("Total bounties: %zu", bounties.size());
        return true;
    }

    static bool HandleBountyClearCommand(ChatHandler* handler, Optional<PlayerIdentifier> target)
    {
        if (!sECBountyHunterMgr->IsEnabled())
        {
            handler->SendSysMessage("|cffff0000Bounty system is disabled.|r");
            return true;
        }

        Player* player = nullptr;
        if (target)
            player = target->GetConnectedPlayer();
        else
            player = handler->GetPlayer();

        if (!player)
        {
            handler->SendSysMessage("Player not found.");
            return false;
        }

        if (!sECBountyHunterMgr->HasActiveBounty(player->GetGUID()))
        {
            handler->PSendSysMessage("%s has no active bounty.", player->GetName().c_str());
            return true;
        }

        sECBountyHunterMgr->GMClearBounty(player->GetGUID());
        handler->PSendSysMessage("|cff00ff00Cleared bounty on %s.|r", player->GetName().c_str());

        return true;
    }
};

// =============================================================================
// Script Registration
// =============================================================================
void AddSC_mod_ec_guard_system()
{
    new ECGuardSystem_PlayerScript();
    new ECGuardSystem_UnitScript();
    new ECGuardSystem_WorldScript();
    new ECGuardSystem_AllCreatureScript();
    new ECGuardSystem_CommandScript();
}
