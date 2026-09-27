/*
 * EC Guard System - Core Manager Implementation
 * GTA-style wanted level system for AzerothCore
 */

#include "ECGuardSystemManager.h"
#include "ECBountyHunterManager.h"
#include <cassert>
#include "ECZoneState.h"
#include "ECZoneSecurityState.h"  // New state machine
#include "ECGuardSystemBridge.h"
#include "Chat.h"
#include "Config.h"
#include "Creature.h"
#include "CreatureAI.h"
#include "DatabaseEnv.h"
#include "Log.h"
#include "Map.h"
#include "ObjectAccessor.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "SpellAuraEffects.h"
#include "TemporarySummon.h"
#include "World.h"
#include "WorldSession.h"
#include "WorldSessionMgr.h"
#include "ChannelMgr.h"
#include "Channel.h"
#include "DBCStores.h"
#include "Random.h"
#include "Timer.h"
#include <random>
#include <set>
#include <cstdio>
#include <algorithm>
#include <array>
#include <cctype>
#include <string_view>

#include "ECWitnessSystem.h"

#ifdef MOD_ALIVE_WORLD
#include "AliveWorldManager.h"
#endif

#ifdef MOD_GTA_POLICE
#include "GTAPoliceManager.h"
#endif

namespace ECGuardSystem
{
    namespace
    {
        constexpr uint32 ASTRANAAR_ZONE_ID = 331;
        constexpr uint32 ASTRANAAR_AREA_ID = 415;
        constexpr uint32 THE_BARRENS_ZONE_ID = 17;
        constexpr uint32 THE_CROSSROADS_AREA_ID = 380;

        bool ConfigMatchesArea(ZoneGuardConfig const& config, uint32 areaId)
        {
            return config.areaId == 0 || config.areaId == areaId;
        }

        bool IsGuardTemplateSafe(uint32 entry, uint32 zoneId, uint32 areaId,
                                 TeamId faction, char const* slot)
        {
            if (entry == 0)
                return false;

            CreatureTemplate const* creatureTemplate = sObjectMgr->GetCreatureTemplate(entry);
            if (!creatureTemplate)
            {
                ECG_LOG_ERROR("ZONE CONFIG REJECTED: missing creature_template entry=%u slot=%s zone=%u area=%u faction=%u",
                    entry, slot, zoneId, areaId, TeamIdToGuardDbFaction(faction));
                return false;
            }

            bool const hasNativeGuardFlag =
                (creatureTemplate->flags_extra & CREATURE_FLAG_EXTRA_GUARD) != 0;

            std::string lowerName = creatureTemplate->Name;
            std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(),
                [](unsigned char value) { return static_cast<char>(std::tolower(value)); });

            static constexpr std::array<std::string_view, 13> GuardNameTokens = {
                "guard", "guardian", "grunt", "sentinel", "watcher", "mountaineer",
                "deathguard", "vindicator", "peacekeeper", "protector", "bruiser", "militia",
                "sentry"
            };
            bool const hasGuardLikeName = std::any_of(
                GuardNameTokens.begin(), GuardNameTokens.end(),
                [&lowerName](std::string_view token)
                {
                    return lowerName.find(token) != std::string::npos;
                });

            uint32 constexpr NonGuardServiceFlags =
                UNIT_NPC_FLAG_QUESTGIVER | UNIT_NPC_FLAG_TRAINER |
                UNIT_NPC_FLAG_TRAINER_CLASS | UNIT_NPC_FLAG_TRAINER_PROFESSION |
                UNIT_NPC_FLAG_VENDOR | UNIT_NPC_FLAG_VENDOR_AMMO |
                UNIT_NPC_FLAG_VENDOR_FOOD | UNIT_NPC_FLAG_VENDOR_POISON |
                UNIT_NPC_FLAG_VENDOR_REAGENT | UNIT_NPC_FLAG_REPAIR |
                UNIT_NPC_FLAG_FLIGHTMASTER | UNIT_NPC_FLAG_SPIRITHEALER |
                UNIT_NPC_FLAG_SPIRITGUIDE | UNIT_NPC_FLAG_INNKEEPER |
                UNIT_NPC_FLAG_BANKER | UNIT_NPC_FLAG_PETITIONER |
                UNIT_NPC_FLAG_TABARDDESIGNER | UNIT_NPC_FLAG_BATTLEMASTER |
                UNIT_NPC_FLAG_AUCTIONEER | UNIT_NPC_FLAG_STABLEMASTER |
                UNIT_NPC_FLAG_GUILD_BANKER | UNIT_NPC_FLAG_SPELLCLICK |
                UNIT_NPC_FLAG_PLAYER_VEHICLE | UNIT_NPC_FLAG_MAILBOX;
            bool const isServiceNpc = (creatureTemplate->npcflag & NonGuardServiceFlags) != 0;

            // A service role is a stronger signal than a stray guard flag or
            // guard-like substring. This explicitly catches bad mappings such
            // as battlemaster Gargok and prevents a vendor/trainer/innkeeper
            // from ever being accepted as a reinforcement template.
            if (isServiceNpc)
            {
                ECG_LOG_ERROR("ZONE CONFIG REJECTED: service NPC entry=%u name=%s npcflag=%u slot=%s zone=%u area=%u faction=%u",
                    entry, creatureTemplate->Name.c_str(), creatureTemplate->npcflag,
                    slot, zoneId, areaId, TeamIdToGuardDbFaction(faction));
                return false;
            }

            // Explicitly flagged guards include legitimate non-humanoid
            // archetypes such as Forsaken/Undead guards, Scryer elementals,
            // and Ancient Protectors. Type is therefore a fallback constraint,
            // not a reason to override AzerothCore's native guard flag.
            // Unflagged rows remain restricted to humanoid/undead templates
            // with an unambiguous guard name and guard-appropriate level.
            bool const supportsNameFallback =
                creatureTemplate->type == CREATURE_TYPE_HUMANOID ||
                creatureTemplate->type == CREATURE_TYPE_UNDEAD;
            bool const safeNameFallback = supportsNameFallback &&
                hasGuardLikeName && creatureTemplate->minlevel >= 20;

            if (!hasNativeGuardFlag && !safeNameFallback)
            {
                ECG_LOG_ERROR("ZONE CONFIG REJECTED: non-guard entry=%u name=%s level=%u-%u npcflag=%u flags_extra=%u slot=%s zone=%u area=%u faction=%u",
                    entry, creatureTemplate->Name.c_str(), creatureTemplate->minlevel,
                    creatureTemplate->maxlevel, creatureTemplate->npcflag,
                    creatureTemplate->flags_extra, slot, zoneId, areaId,
                    TeamIdToGuardDbFaction(faction));
                return false;
            }

            return true;
        }

        constexpr bool IsNonCivilianCreatureType(uint32 creatureType)
        {
            return creatureType == CREATURE_TYPE_BEAST ||
                   creatureType == CREATURE_TYPE_CRITTER ||
                   creatureType == CREATURE_TYPE_TOTEM ||
                   creatureType == CREATURE_TYPE_NON_COMBAT_PET ||
                   creatureType == CREATURE_TYPE_GAS_CLOUD;
        }

        static_assert(IsNonCivilianCreatureType(CREATURE_TYPE_CRITTER));
        static_assert(IsNonCivilianCreatureType(CREATURE_TYPE_BEAST));
        static_assert(IsNonCivilianCreatureType(CREATURE_TYPE_NON_COMBAT_PET));
        static_assert(!IsNonCivilianCreatureType(CREATURE_TYPE_HUMANOID));

        bool MatchesStrategicState(ZoneState const& zone, TeamId eventFaction, Creature const& subject)
        {
            if (zone.owningFaction != TEAM_NEUTRAL && zone.owningFaction != eventFaction)
                return false;

            // The legacy state is keyed by zone, while Ashenvale contains
            // several same-faction outposts. Its configured 11-guard count is
            // specifically Astranaar, so only area 415 belongs to that state.
            if (zone.zoneId == ASTRANAAR_ZONE_ID && zone.owningFaction == TEAM_ALLIANCE)
                return subject.GetAreaId() == ASTRANAAR_AREA_ID;

            // The Barrens has 46 entry-3501 guards across multiple settlements.
            // This legacy 14-guard strategic state represents the Crossroads,
            // not every Horde Guard in the entire zone.
            if (zone.zoneId == THE_BARRENS_ZONE_ID && zone.owningFaction == TEAM_HORDE)
                return subject.GetAreaId() == THE_CROSSROADS_AREA_ID;

            return true;
        }
    }

    // ==========================================================================
    // DEBUG LOGGING IMPLEMENTATION
    // ==========================================================================

    bool ECGuardDebug::s_debugEnabled = false;
    uint8 ECGuardDebug::s_logLevel = static_cast<uint8>(ECGLogLevel::ECG_INFO);
    bool ECGuardDebug::s_inGameLogging = false;

    void ECGuardDebug::Log(ECGLogLevel level, const char* func, const char* fmt, ...)
    {
        if (!s_debugEnabled && level > ECGLogLevel::ECG_INFO)
            return;

        if (static_cast<uint8>(level) > s_logLevel)
            return;

        char buffer[1024];
        va_list args;
        va_start(args, fmt);
        vsnprintf(buffer, sizeof(buffer), fmt, args);
        va_end(args);

        // Format: [ECGuard][LEVEL][Function] Message
        std::ostringstream ss;
        ss << "[ECGuard][" << LogLevelToString(level) << "][" << func << "] " << buffer;

        // Log to AzerothCore log system
        switch (level)
        {
            case ECGLogLevel::ECG_ERROR:
                LOG_ERROR("module", "{}", ss.str());
                break;
            case ECGLogLevel::ECG_WARNING:
                LOG_WARN("module", "{}", ss.str());
                break;
            case ECGLogLevel::ECG_INFO:
                LOG_INFO("module", "{}", ss.str());
                break;
            case ECGLogLevel::ECG_DEBUG:
            case ECGLogLevel::ECG_TRACE:
            default:
                LOG_DEBUG("module", "{}", ss.str());
                break;
        }

        // Also log to game if enabled
        if (s_inGameLogging)
        {
            std::ostringstream gameMsg;
            gameMsg << LogLevelToColor(level) << "[ECGuard][" << LogLevelToString(level) << "]|r " << buffer;
            LogToGame(gameMsg.str());
        }
    }

    void ECGuardDebug::LogToGame(const std::string& message)
    {
        // Send to all online GMs
        WorldSessionMgr::SessionMap const& sessions = sWorldSessionMgr->GetAllSessions();
        for (auto const& pair : sessions)
        {
            if (WorldSession* session = pair.second)
            {
                if (Player* player = session->GetPlayer())
                {
                    if (player->IsGameMaster())
                    {
                        ChatHandler(player->GetSession()).SendSysMessage(message.c_str());
                    }
                }
            }
        }
    }

    void ECGuardDebug::SetDebugEnabled(bool enabled) { s_debugEnabled = enabled; }
    void ECGuardDebug::SetLogLevel(uint8 level) { s_logLevel = level; }
    void ECGuardDebug::SetInGameLogging(bool enabled) { s_inGameLogging = enabled; }
    bool ECGuardDebug::IsDebugEnabled() { return s_debugEnabled; }
    uint8 ECGuardDebug::GetLogLevel() { return s_logLevel; }
    bool ECGuardDebug::IsInGameLoggingEnabled() { return s_inGameLogging; }

    const char* ECGuardDebug::LogLevelToString(ECGLogLevel level)
    {
        switch (level)
        {
            case ECGLogLevel::ECG_ERROR:   return "ERROR";
            case ECGLogLevel::ECG_WARNING: return "WARN";
            case ECGLogLevel::ECG_INFO:    return "INFO";
            case ECGLogLevel::ECG_DEBUG:   return "DEBUG";
            case ECGLogLevel::ECG_TRACE:   return "TRACE";
            default:                       return "???";
        }
    }

    const char* ECGuardDebug::LogLevelToColor(ECGLogLevel level)
    {
        switch (level)
        {
            case ECGLogLevel::ECG_ERROR:   return "|cffff0000";  // Red
            case ECGLogLevel::ECG_WARNING: return "|cffff9900";  // Orange
            case ECGLogLevel::ECG_INFO:    return "|cff00ff00";  // Green
            case ECGLogLevel::ECG_DEBUG:   return "|cff00ffff";  // Cyan
            case ECGLogLevel::ECG_TRACE:   return "|cff888888";  // Gray
            default:                       return "|cffffffff";  // White
        }
    }

    // ==========================================================================
    // MANAGER IMPLEMENTATION
    // ==========================================================================

    ECGuardSystemManager* ECGuardSystemManager::instance()
    {
        static ECGuardSystemManager instance;
        return &instance;
    }

    void ECGuardSystemManager::LoadFromConfig()
    {
        m_enabled = sConfigMgr->GetOption<bool>("ECGuardSystem.Enable", true);
        m_debugMode = sConfigMgr->GetOption<bool>("ECGuardSystem.Debug", false);

        // Debug logging settings
        ECGuardDebug::SetDebugEnabled(m_debugMode);
        ECGuardDebug::SetLogLevel(sConfigMgr->GetOption<uint8>("ECGuardSystem.DebugLogLevel", 2));  // Default INFO
        ECGuardDebug::SetInGameLogging(sConfigMgr->GetOption<bool>("ECGuardSystem.InGameLogging", false));

        // Crime point values
        m_escalation.civilianAttackPoints = sConfigMgr->GetOption<uint32>("ECGuardSystem.CivilianAttackPoints", 50);
        m_escalation.civilianKillPoints = sConfigMgr->GetOption<uint32>("ECGuardSystem.CivilianKillPoints", 100);
        m_escalation.guardAttackPoints = sConfigMgr->GetOption<uint32>("ECGuardSystem.GuardAttackPoints", 150);
        m_escalation.guardKillPoints = sConfigMgr->GetOption<uint32>("ECGuardSystem.GuardKillPoints", 300);

        // Star thresholds
        m_escalation.pointsFor1Star = sConfigMgr->GetOption<uint32>("ECGuardSystem.PointsFor1Star", 100);
        m_escalation.pointsFor2Star = sConfigMgr->GetOption<uint32>("ECGuardSystem.PointsFor2Star", 300);
        m_escalation.pointsFor3Star = sConfigMgr->GetOption<uint32>("ECGuardSystem.PointsFor3Star", 600);
        m_escalation.pointsFor4Star = sConfigMgr->GetOption<uint32>("ECGuardSystem.PointsFor4Star", 1000);
        m_escalation.pointsFor5Star = sConfigMgr->GetOption<uint32>("ECGuardSystem.PointsFor5Star", 1500);

        // Decay settings
        m_decayDelayMs = sConfigMgr->GetOption<uint32>("ECGuardSystem.DecayDelaySeconds", 30) * 1000;
        m_decayRatePerSec = sConfigMgr->GetOption<uint32>("ECGuardSystem.DecayRatePerSecond", 10);

        // Guard spawning
        m_guardSpawnCooldownMs = sConfigMgr->GetOption<uint32>("ECGuardSystem.GuardSpawnCooldownMs", 10000);
        m_maxActiveGuards = sConfigMgr->GetOption<uint32>("ECGuardSystem.MaxActiveGuards", 6);
        m_guardSearchRadius = sConfigMgr->GetOption<float>("ECGuardSystem.GuardSearchRadius", 80.0f);
        m_guardPatrolDurationMs = sConfigMgr->GetOption<uint32>("ECGuardSystem.GuardPatrolDurationSeconds", 60) * 1000;

        // Original WotLK town-defense behavior. Explicit civilian NPCs can
        // call local guards when an opposing player enters their normal aggro
        // range, before the player deals damage. Exact retail repeat/despawn
        // timings are not preserved in public data, so those remain tunable.
        m_civilianProximityEnabled = sConfigMgr->GetOption<bool>("ECGuardSystem.CivilianProximityEnabled", true);
        m_civilianProximityScanIntervalMs = std::max<uint32>(
            250, sConfigMgr->GetOption<uint32>("ECGuardSystem.CivilianProximityScanIntervalMs", 1000));
        m_civilianAlarmRepeatMs = std::max<uint32>(
            1000, sConfigMgr->GetOption<uint32>("ECGuardSystem.CivilianAlarmRepeatMs", 10000));
        m_civilianAlarmMinGuards = std::max<uint32>(
            1, sConfigMgr->GetOption<uint32>("ECGuardSystem.CivilianAlarmMinGuards", 1));
        m_civilianAlarmMaxGuards = std::max<uint32>(
            m_civilianAlarmMinGuards,
            sConfigMgr->GetOption<uint32>("ECGuardSystem.CivilianAlarmMaxGuards", 2));

        // Guard patrol behavior
        m_guardPatrolEnabled = sConfigMgr->GetOption<bool>("ECGuardSystem.GuardPatrolEnabled", false);
        m_guardPatrolRadius = sConfigMgr->GetOption<float>("ECGuardSystem.GuardPatrolRadius", 30.0f);
        m_customGuardYellsEnabled = sConfigMgr->GetOption<bool>(
            "ECGuardSystem.CustomGuardYellsEnabled", false);

        // LocalDefence
        m_broadcastNPCDeaths = sConfigMgr->GetOption<bool>("ECGuardSystem.BroadcastNPCDeaths", true);
        m_customLocalDefenceTextEnabled = sConfigMgr->GetOption<bool>(
            "ECGuardSystem.CustomLocalDefenceTextEnabled", false);

        ECG_LOG_INFO("Configuration loaded - Enabled: %s, Debug: %s, InGame: %s, civilian proximity: %s (%ums scan, %ums repeat, %u-%u guards)",
            m_enabled ? "yes" : "no",
            m_debugMode ? "yes" : "no",
            ECGuardDebug::IsInGameLoggingEnabled() ? "yes" : "no",
            m_civilianProximityEnabled ? "yes" : "no",
            m_civilianProximityScanIntervalMs, m_civilianAlarmRepeatMs,
            m_civilianAlarmMinGuards, m_civilianAlarmMaxGuards);
    }

    void ECGuardSystemManager::LoadZoneGuardData()
    {
        uint32 oldMSTime = getMSTime();

        m_zoneConfigs.clear();

        QueryResult result = WorldDatabase.Query("SELECT zone_id, area_id, faction, guard_entry_1, guard_entry_2, elite_entry_1, captain_entry, search_radius FROM ec_guard_system_zone_config");

        if (!result)
        {
            LOG_WARN("module", "EC Guard System: No zone guard configurations found in database");
            return;
        }

        uint32 rowCount = 0;
        uint32 loadedCount = 0;
        uint32 rejectedRows = 0;
        uint32 rejectedTemplates = 0;
        do
        {
            Field* fields = result->Fetch();
            ++rowCount;

            ZoneGuardConfig config;
            config.zoneId = fields[0].Get<uint32>();
            config.areaId = fields[1].Get<uint32>();
            config.faction = GuardDbFactionToTeamId(fields[2].Get<uint8>());

            uint32 guard1 = fields[3].Get<uint32>();
            uint32 guard2 = fields[4].Get<uint32>();
            uint32 elite1 = fields[5].Get<uint32>();
            uint32 captainEntry = fields[6].Get<uint32>();
            config.searchRadius = fields[7].Get<float>();

            if (config.areaId != 0)
            {
                AreaTableEntry const* area = sAreaTableStore.LookupEntry(config.areaId);
                if (!area || area->zone != config.zoneId)
                {
                    ECG_LOG_ERROR("ZONE CONFIG ROW REJECTED: area=%u does not belong to zone=%u faction=%u",
                        config.areaId, config.zoneId, TeamIdToGuardDbFaction(config.faction));
                    ++rejectedRows;
                    continue;
                }
            }

            auto addIfSafe = [&](uint32 entry, char const* slot,
                                 std::vector<uint32>& destination)
            {
                if (entry == 0)
                    return;
                if (IsGuardTemplateSafe(entry, config.zoneId, config.areaId,
                                        config.faction, slot))
                    destination.push_back(entry);
                else
                    ++rejectedTemplates;
            };

            addIfSafe(guard1, "guard_entry_1", config.guardEntries);
            addIfSafe(guard2, "guard_entry_2", config.guardEntries);
            addIfSafe(elite1, "elite_entry_1", config.eliteEntries);

            if (captainEntry != 0)
            {
                if (IsGuardTemplateSafe(captainEntry, config.zoneId, config.areaId,
                                        config.faction, "captain_entry"))
                    config.captainEntry = captainEntry;
                else
                    ++rejectedTemplates;
            }

            if (config.guardEntries.empty())
            {
                ECG_LOG_ERROR("ZONE CONFIG ROW REJECTED: no valid regular guards zone=%u area=%u faction=%u",
                    config.zoneId, config.areaId, TeamIdToGuardDbFaction(config.faction));
                ++rejectedRows;
                continue;
            }

            RegisterZoneConfig(config);
            ++loadedCount;

        } while (result->NextRow());

        LOG_INFO("module", "EC Guard System: Loaded {} of {} zone guard configurations in {} ms (rows rejected: {}, templates rejected: {})",
            loadedCount, rowCount, GetMSTimeDiffToNow(oldMSTime),
            rejectedRows, rejectedTemplates);
    }

    void ECGuardSystemManager::Initialize()
    {
        LoadFromConfig();
        LoadZoneGuardData();

        // Initialize the Zone Security State Machine manager
        sZoneSecurityMgr->Initialize();

        // Set up callback for zone phase changes -> bridge events
        sZoneSecurityMgr->SetZonePhaseCallback([](uint32 zoneId, TeamId faction, SecurityPhase oldPhase, SecurityPhase newPhase) {
            ECG_LOG_INFO("Zone %u faction %u: Phase changed %s -> %s",
                zoneId, static_cast<uint8>(faction),
                SecurityPhaseToString(oldPhase),
                SecurityPhaseToString(newPhase));

            // Emit to bridge
            #ifdef MOD_EC_REALTIME_BRIDGE
            sECGuardBridge->EmitZonePhaseChange(zoneId, faction, oldPhase, newPhase);
            #endif

            // Announce lockdown in LocalDefense channel
            if (newPhase == SecurityPhase::LOCKDOWN)
            {
                // Could broadcast to LocalDefense here
            }
        });

        LOG_INFO("module", "EC Guard System: Initialized with Zone Security State Machine");
    }

    void ECGuardSystemManager::Update(uint32 diff)
    {
        if (!m_enabled)
            return;

        m_updateTimer += diff;

        // Update Zone Security State Machine (handles heat decay, phase transitions)
        sZoneSecurityMgr->Update(diff);

        // Legacy wanted level decay (still needed for per-player tracking)
        if (m_updateTimer >= m_decayIntervalMs)
        {
            m_updateTimer = 0;
            DecayWantedLevels(m_decayIntervalMs);

            // Clean up stale guard tracking entries (guards that despawned without dying,
            // e.g. TEMPSUMMON_TIMED_DESPAWN_OUT_OF_COMBAT expiry). Without this,
            // activeGuardCount drifts up and eventually blocks all new guard spawns.
            CleanupDespawnedGuards();
        }
    }

    void ECGuardSystemManager::CleanupDespawnedGuards()
    {
        std::vector<ObjectGuid> staleGuards;
        std::vector<Creature*> idleGuards;

        {
            std::lock_guard<std::mutex> lock(m_guardMutex);
            for (const auto& [guardGuid, targetGuid] : m_guardToTarget)
            {
                // Use the target player as reference to look up the guard creature
                Player* target = ObjectAccessor::FindPlayer(targetGuid);
                if (!target || !target->IsInWorld())
                {
                    // Target player offline - guard is definitely stale
                    staleGuards.push_back(guardGuid);
                    continue;
                }

                Creature* guard = ObjectAccessor::GetCreature(*target, guardGuid);
                if (!guard || !guard->IsInWorld() || !guard->IsAlive())
                {
                    staleGuards.push_back(guardGuid);
                    continue;
                }

                // Guard is alive but not in combat — queue it for patrol
                if (m_guardPatrolEnabled && !guard->IsInCombat() && !guard->GetVictim())
                {
                    idleGuards.push_back(guard);
                }
            }
        }

        for (const auto& guardGuid : staleGuards)
        {
            ECG_LOG_DEBUG("CleanupDespawnedGuards: Guard %s no longer exists, unregistering",
                guardGuid.ToString().c_str());
            UnregisterSpawnedGuard(guardGuid);
        }

        // Set idle guards to patrol (outside lock)
        for (Creature* guard : idleGuards)
        {
            SetGuardPatrolling(guard);
        }
    }

    WantedState* ECGuardSystemManager::GetWantedState(ObjectGuid playerGuid)
    {
        std::lock_guard<std::recursive_mutex> lock(m_wantedMutex);
        auto it = m_wantedStates.find(playerGuid);
        return (it != m_wantedStates.end()) ? &it->second : nullptr;
    }

    WantedState* ECGuardSystemManager::GetOrCreateWantedState(ObjectGuid playerGuid)
    {
        std::lock_guard<std::recursive_mutex> lock(m_wantedMutex);
        auto& state = m_wantedStates[playerGuid];
        if (state.playerGuid.IsEmpty())
            state.playerGuid = playerGuid;
        return &state;
    }

    // Thread-safe accessor: check if player is wanted
    bool ECGuardSystemManager::IsPlayerWanted(ObjectGuid playerGuid) const
    {
        std::lock_guard<std::recursive_mutex> lock(m_wantedMutex);
        auto it = m_wantedStates.find(playerGuid);
        return (it != m_wantedStates.end()) && it->second.IsWanted();
    }

    // Thread-safe accessor: get player's wanted stars
    WantedStars ECGuardSystemManager::GetPlayerWantedStars(ObjectGuid playerGuid) const
    {
        std::lock_guard<std::recursive_mutex> lock(m_wantedMutex);
        auto it = m_wantedStates.find(playerGuid);
        return (it != m_wantedStates.end()) ? it->second.stars : WantedStars::NONE;
    }

    // Thread-safe accessor: get player's crime points
    uint32 ECGuardSystemManager::GetPlayerCrimePoints(ObjectGuid playerGuid) const
    {
        std::lock_guard<std::recursive_mutex> lock(m_wantedMutex);
        auto it = m_wantedStates.find(playerGuid);
        return (it != m_wantedStates.end()) ? it->second.crimePoints : 0;
    }

    // Thread-safe accessor: get a copy of the wanted state (safe for external use)
    std::optional<WantedState> ECGuardSystemManager::GetWantedStateCopy(ObjectGuid playerGuid) const
    {
        std::lock_guard<std::recursive_mutex> lock(m_wantedMutex);
        auto it = m_wantedStates.find(playerGuid);
        if (it != m_wantedStates.end())
            return it->second;
        return std::nullopt;
    }

    void ECGuardSystemManager::AddCrime(Player* player, CrimeType type, Creature* victim)
    {
        if (!player || !victim || !player->IsAlive())
        {
            ECG_LOG_WARN("AddCrime ignored invalid/dead player or null victim");
            return;
        }

        ECG_LOG_DEBUG("AddCrime: Player %s committed crime type %u against %s (entry %u)",
            player->GetName().c_str(), static_cast<uint8>(type),
            victim->GetName().c_str(), victim->GetEntry());

        WantedState* state = GetOrCreateWantedState(player->GetGUID());
        // Note: GetOrCreateWantedState always returns valid pointer (creates if not exists)
        // Using assert for defensive programming in debug builds
        assert(state && "GetOrCreateWantedState should never return nullptr");

        WantedStars oldStars = state->stars;
        uint32 pointsAdded = 0;

        // Add crime points based on type
        switch (type)
        {
            case CrimeType::CIVILIAN_ATTACK:
                pointsAdded = m_escalation.civilianAttackPoints;
                state->crimePoints += pointsAdded;
                ECG_LOG_TRACE("Civilian attack: +%u points", pointsAdded);
                break;
            case CrimeType::CIVILIAN_KILL:
                pointsAdded = m_escalation.civilianKillPoints;
                state->crimePoints += pointsAdded;
                state->civilianKills++;
                ECG_LOG_INFO("Civilian killed: %s by %s (+%u points, total kills: %u)",
                    victim->GetName().c_str(), player->GetName().c_str(), pointsAdded, state->civilianKills);
                break;
            case CrimeType::GUARD_ATTACK:
                pointsAdded = m_escalation.guardAttackPoints;
                state->crimePoints += pointsAdded;
                ECG_LOG_TRACE("Guard attack: +%u points", pointsAdded);
                break;
            case CrimeType::GUARD_KILL:
                pointsAdded = m_escalation.guardKillPoints;
                state->crimePoints += pointsAdded;
                state->guardKills++;
                ECG_LOG_INFO("Guard killed: %s by %s (+%u points, total guard kills: %u)",
                    victim->GetName().c_str(), player->GetName().c_str(), pointsAdded, state->guardKills);
                break;
        }

        // Update tracking
        state->lastCrimeTime = std::chrono::steady_clock::now();
        state->crimeZoneId = victim->GetZoneId();
        state->crimeAreaId = victim->GetAreaId();

        // Resolve the offended faction from the victim/player relationship. A
        // contested zone can have both Alliance and Horde guard configurations,
        // so a zone-wide faction guess is not safe here.
        state->crimeFaction = GetCrimeFaction(player, victim);

        // Calculate new star level
        state->UpdateStars(
            m_escalation.pointsFor1Star,
            m_escalation.pointsFor2Star,
            m_escalation.pointsFor3Star,
            m_escalation.pointsFor4Star,
            m_escalation.pointsFor5Star
        );

        // Notify player of star change
        if (state->stars > oldStars)
        {
            ECG_LOG_INFO("Wanted level INCREASED: %s now has %u stars (was %u)",
                player->GetName().c_str(), static_cast<uint8>(state->stars), static_cast<uint8>(oldStars));

            uint8 const starCount = static_cast<uint8>(state->stars);
            ChatHandler(player->GetSession()).PSendSysMessage(
                "|cffff0000[WANTED]|r Your wanted level has increased to {} {}!",
                starCount, starCount == 1 ? "star" : "stars");

            // Swap star-specific wanted mark aura (each star has its own icon/tooltip)
            {
                // Remove old star aura (if any)
                uint32 oldSpell = GuardSpells::GetWantedMarkForStars(oldStars);
                if (oldSpell > 0)
                    player->RemoveAura(oldSpell);

                // Apply new star aura
                uint32 newSpell = GuardSpells::GetWantedMarkForStars(state->stars);
                if (newSpell > 0)
                {
                    ECG_LOG_INFO("AddCrime: Swapping wanted aura on %s: spell %u -> %u (%u stars)",
                        player->GetName().c_str(), oldSpell, newSpell, static_cast<uint8>(state->stars));
                    player->AddAura(newSpell, player);

                    if (!player->HasAura(newSpell))
                        ECG_LOG_ERROR("AddCrime: Wanted aura spell %u FAILED to apply on %s!",
                            newSpell, player->GetName().c_str());
                }
            }

            // Emit wanted level change to realtime bridge
            sGuardSystemBridge->EmitWantedLevelChanged(player, oldStars, state->stars, state->crimePoints);

            // Notify bounty hunter system of wanted level change
            sECBountyHunterMgr->OnWantedLevelChanged(player, oldStars, state->stars);

            // Notify GTA Police system of wanted level change for auto-dispatch
#ifdef MOD_GTA_POLICE
            sGTAPoliceMgr->OnWantedLevelChanged(player, static_cast<uint8>(oldStars), static_cast<uint8>(state->stars));
#endif
        }

        // Emit crime committed to realtime bridge
        sGuardSystemBridge->EmitCrimeCommitted(player, type, victim, pointsAdded);

        ECG_LOG_DEBUG("Crime processed: %s total points=%u, stars=%u, zone=%u, faction=%u",
            player->GetName().c_str(), state->crimePoints, static_cast<uint8>(state->stars),
            state->crimeZoneId, static_cast<uint8>(state->crimeFaction));

        // The legacy strategic state is keyed only by zone, not faction. Do not
        // let a crime against a Horde outpost in a contested zone add heat to
        // that zone's configured Alliance garrison (or vice versa).
        const ZoneState* strategicZone = sECZoneStateMgr->GetZoneState(state->crimeZoneId);
        if (!strategicZone || MatchesStrategicState(*strategicZone, state->crimeFaction, *victim))
        {
            sECZoneStateMgr->OnZoneCrime(state->crimeZoneId, player->GetGUID(), pointsAdded);
        }
        else
        {
            ECG_LOG_DEBUG("Legacy zone heat skipped: zone=%u area=%u owner=%u crimeFaction=%u",
                state->crimeZoneId, victim->GetAreaId(), static_cast<uint8>(strategicZone->owningFaction),
                static_cast<uint8>(state->crimeFaction));
        }

        // Update Zone Security State Machine (new state machine system)
        // This tracks zone-wide security phases: PEACEFUL -> ALERT -> PURSUIT -> LOCKDOWN
        sZoneSecurityMgr->OnCrimeInZone(
            state->crimeZoneId,
            state->crimeFaction,
            player->GetGUID(),
            pointsAdded
        );

        // Dispatch guards if warranted
        if (state->IsWanted())
        {
            ECG_LOG_DEBUG("Player %s is wanted - dispatching guards", player->GetName().c_str());
            DispatchGuards(player, victim);
        }

        // Notify AliveWorld system for gossip generation and bot reactions
#ifdef MOD_ALIVE_WORLD
        if (AliveWorld::AliveWorldManager::Instance()->IsEnabled())
        {
            // Trigger crime event (records gossip and emote reactions)
            sAliveWorldMgr->OnCrimeCommitted(
                player,
                player->GetZoneId(),
                static_cast<uint8>(state->stars)
            );

            ECG_LOG_TRACE("Notified AliveWorld: Crime in zone %u, wanted level %u",
                          player->GetZoneId(), static_cast<uint8>(state->stars));
        }
#endif
    }

    void ECGuardSystemManager::ClearWanted(Player* player)
    {
        if (!player)
            return;

        uint32 clearedCrimeZoneId = 0;
        {
            std::lock_guard<std::recursive_mutex> lock(m_wantedMutex);
            auto it = m_wantedStates.find(player->GetGUID());
            if (it != m_wantedStates.end())
            {
                ECG_LOG_INFO("ClearWanted: Clearing wanted state for %s (was %u stars, %u points)",
                    player->GetName().c_str(), static_cast<uint8>(it->second.stars), it->second.crimePoints);

                clearedCrimeZoneId = it->second.crimeZoneId;
                it->second.Reset();

                // Remove all star-specific wanted mark auras
                GuardSpells::RemoveAllWantedMarks(player);
                // Also remove hunted mark if present
                player->RemoveAura(GuardSpells::HUNTED_MARK);
                ECG_LOG_DEBUG("ClearWanted: Removed all wanted/hunted auras from %s", player->GetName().c_str());

                ChatHandler(player->GetSession()).SendSysMessage("|cff00ff00You are no longer wanted.|r");

                // Emit wanted cleared to realtime bridge
                sGuardSystemBridge->EmitWantedCleared(player);
            }
            else
            {
                ECG_LOG_TRACE("ClearWanted: %s has no wanted state to clear", player->GetName().c_str());
            }
        }

        // Do this after releasing the wanted-state lock. It cancels delayed
        // spawns and despawns every active bounty hunter for this player, even
        // if an inconsistent bounty survived without a wanted-state row.
        sECBountyHunterMgr->ClearBounty(player->GetGUID(), BountyStatus::EXPIRED);

        // Remove the actor from the zone incident immediately. Previously a
        // dead/cleared player remained enrolled and could keep fake preparation
        // warnings alive until a later zone-change or phase-side prune.
        if (clearedCrimeZoneId != 0)
            sECZoneStateMgr->OnPlayerLeaveZone(player->GetGUID(), clearedCrimeZoneId);
    }

    void ECGuardSystemManager::RemoveWantedState(ObjectGuid playerGuid)
    {
        std::lock_guard<std::recursive_mutex> lock(m_wantedMutex);
        auto it = m_wantedStates.find(playerGuid);
        if (it != m_wantedStates.end())
        {
            ECG_LOG_DEBUG("RemoveWantedState: Erasing wanted state entry for GUID %s",
                playerGuid.ToString().c_str());
            m_wantedStates.erase(it);
        }
    }

    void ECGuardSystemManager::DecayWantedLevels(uint32 diffMs)
    {
        auto now = std::chrono::steady_clock::now();
        struct WantedLevelChange
        {
            ObjectGuid playerGuid;
            WantedStars oldStars;
            WantedStars newStars;
            uint32 crimeZoneId;
        };
        std::vector<WantedLevelChange> bountyChanges;

        {
            std::lock_guard<std::recursive_mutex> lock(m_wantedMutex);
            for (auto& pair : m_wantedStates)
            {
                WantedState& state = pair.second;

                if (!state.IsWanted())
                    continue;

            // Check if player is in combat
                Player* player = ObjectAccessor::FindPlayer(state.playerGuid);
                // Persisted wanted states are saved at logout. Do not decay the
                // in-memory copy while the player is offline or it will diverge
                // from the DB row and leave bounty cleanup without a live owner.
                if (!player)
                    continue;

                if (player->IsInCombat())
                {
                    // No decay during combat
                    continue;
                }

            // Check if enough time has passed since last crime
                auto msSinceLastCrime = std::chrono::duration_cast<std::chrono::milliseconds>(now - state.lastCrimeTime).count();
                if (msSinceLastCrime < static_cast<int64>(m_decayDelayMs))
                    continue;

            // Apply decay
                uint32 decay = (m_decayRatePerSec * diffMs) / 1000;
                uint32 oldPoints = state.crimePoints;
                if (state.crimePoints > decay)
                    state.crimePoints -= decay;
                else
                    state.crimePoints = 0;

            // Emit decay event to realtime bridge (only if player is online)
                if (player && oldPoints != state.crimePoints)
                {
                    sGuardSystemBridge->EmitWantedDecayed(player, oldPoints, state.crimePoints);
                }

            // Update star level
                WantedStars oldStars = state.stars;
                state.UpdateStars(
                    m_escalation.pointsFor1Star,
                    m_escalation.pointsFor2Star,
                    m_escalation.pointsFor3Star,
                    m_escalation.pointsFor4Star,
                    m_escalation.pointsFor5Star
                );

            // Swap star-specific aura on star decrease
                if (state.stars < oldStars && player)
                {
                    ECG_LOG_DEBUG("DecayWantedLevels: %s stars decreased %u -> %u (points: %u -> %u)",
                        player->GetName().c_str(), static_cast<uint8>(oldStars), static_cast<uint8>(state.stars),
                        oldPoints, state.crimePoints);

                // Remove old star aura, apply new one
                    uint32 oldSpell = GuardSpells::GetWantedMarkForStars(oldStars);
                    if (oldSpell > 0)
                        player->RemoveAura(oldSpell);

                    uint32 newSpell = GuardSpells::GetWantedMarkForStars(state.stars);
                    if (newSpell > 0)
                        player->AddAura(newSpell, player);

                    uint8 const starCount = static_cast<uint8>(state.stars);
                    ChatHandler(player->GetSession()).PSendSysMessage(
                        "|cff00ff00Your wanted level has decreased to {} {}.|r",
                        starCount, starCount == 1 ? "star" : "stars");

                    bountyChanges.push_back({state.playerGuid, oldStars, state.stars,
                        state.crimeZoneId});
                }

            // Clear if no more wanted
                if (!state.IsWanted() && player)
                {
                    ECG_LOG_INFO("DecayWantedLevels: %s wanted level fully decayed (was %u stars -> 0).",
                        player->GetName().c_str(), static_cast<uint8>(oldStars));

                    state.Reset();
                    GuardSpells::RemoveAllWantedMarks(player);
                    player->RemoveAura(GuardSpells::HUNTED_MARK);

                    ChatHandler(player->GetSession()).SendSysMessage("|cff00ff00You are no longer wanted.|r");
                }
            }
        }

        // Bounty cleanup acquires its own locks. Notify only after releasing the
        // wanted-state lock to keep lock ordering consistent with tracking.
        for (WantedLevelChange const& change : bountyChanges)
        {
            if (Player* player = ObjectAccessor::FindPlayer(change.playerGuid))
                sECBountyHunterMgr->OnWantedLevelChanged(player, change.oldStars, change.newStars);
            if (change.newStars == WantedStars::NONE && change.crimeZoneId != 0)
                sECZoneStateMgr->OnPlayerLeaveZone(change.playerGuid, change.crimeZoneId);
        }
    }

    void ECGuardSystemManager::DispatchGuards(Player* target, Creature* witness)
    {
        if (!target || !target->IsAlive())
        {
            ECG_LOG_WARN("DispatchGuards called with null/dead target");
            return;
        }

        ECG_LOG_TRACE("DispatchGuards: Attempting to dispatch guards for %s", target->GetName().c_str());

        WantedState* state = GetWantedState(target->GetGUID());
        if (!state || !state->IsWanted())
        {
            ECG_LOG_TRACE("DispatchGuards: %s is not wanted, skipping", target->GetName().c_str());
            return;
        }

        if (state->crimeZoneId != 0 && state->crimeZoneId != target->GetZoneId())
        {
            ECG_LOG_WARN("DispatchGuards: Rejecting stale jurisdiction for %s: crime zone %u, current zone %u",
                target->GetName().c_str(), state->crimeZoneId, target->GetZoneId());
            return;
        }

        if (witness && (witness->GetZoneId() != target->GetZoneId() ||
            witness->GetMapId() != target->GetMapId() ||
            witness->GetInstanceId() != target->GetInstanceId()))
        {
            ECG_LOG_WARN("DispatchGuards: Rejecting cross-map witness response for %s",
                target->GetName().c_str());
            return;
        }

        // Check cooldown
        if (!state->CanSpawnGuard(m_guardSpawnCooldownMs))
        {
            ECG_LOG_TRACE("DispatchGuards: Spawn cooldown active for %s", target->GetName().c_str());
            return;
        }

        // Check max active guards
        if (state->activeGuardCount >= m_maxActiveGuards)
        {
            ECG_LOG_DEBUG("DispatchGuards: Max guards (%u) already active for %s",
                m_maxActiveGuards, target->GetName().c_str());
            return;
        }

        uint32 guardsNeeded = GetGuardCountForStars(state->stars);
        uint32 guardsToDispatch = std::min(guardsNeeded, m_maxActiveGuards - state->activeGuardCount);

        if (guardsToDispatch == 0)
            return;

        ECG_LOG_INFO("DispatchGuards: Need %u guards for %s (%u stars), %u active, dispatching %u",
            guardsNeeded, target->GetName().c_str(), static_cast<uint8>(state->stars),
            state->activeGuardCount, guardsToDispatch);

        // Commit the cooldown before any AI AttackStart call. Combat hooks may
        // run synchronously, so updating it afterward leaves a re-entry window.
        state->lastGuardSpawnTime = std::chrono::steady_clock::now();

        // First try to alert nearby idle guards
        uint32 alertedGuards = 0;
        if (witness)
        {
            alertedGuards = AlertNearbyGuards(target, witness, guardsToDispatch);
            if (alertedGuards > 0)
            {
                ECG_LOG_DEBUG("DispatchGuards: Alerted %u nearby guards for %s",
                    alertedGuards, target->GetName().c_str());
            }
        }

        // Spawn remaining guards if needed
        if (alertedGuards < guardsToDispatch)
        {
            uint32 toSpawn = guardsToDispatch - alertedGuards;
            bool elite = state->stars >= WantedStars::THREE_STAR;
            ECG_LOG_DEBUG("DispatchGuards: Spawning %u %s guards for %s",
                toSpawn, elite ? "ELITE" : "regular", target->GetName().c_str());
            SpawnGuards(target, toSpawn, elite);
        }

        // Emit dispatch event to realtime bridge
        bool elite = state->stars >= WantedStars::THREE_STAR;
        sGuardSystemBridge->EmitGuardsDispatched(target, guardsToDispatch, target->GetZoneId(), elite);

        ECG_LOG_TRACE("DispatchGuards: Dispatch complete for %s", target->GetName().c_str());
    }

    bool ECGuardSystemManager::DispatchCivilianTownAlarm(Player* target, Creature* caller,
                                                          char const* trigger,
                                                          bool allowCallerSpeech)
    {
        if (!target || !target->IsInWorld() || target->IsDuringRemoveFromWorld() ||
            !target->IsAlive() ||
            !caller || !caller->IsInWorld() || caller->IsDuringRemoveFromWorld() ||
            (allowCallerSpeech && !caller->IsAlive()))
        {
            ECG_LOG_DEBUG("DispatchCivilianTownAlarm suppressed: target/caller lifecycle invalid (speech=%s)",
                allowCallerSpeech ? "enabled" : "disabled");
            return false;
        }

        TeamId const responseFaction = GetCrimeFaction(target, caller);
        // A civilian alarm must use the caller's exact local faction mapping.
        // Falling back to a neutral row can make, for example, an Alliance NPC
        // in the Barrens summon Ratchet bruisers. An exact neutral row remains
        // valid for genuinely neutral towns such as Ratchet.
        const ZoneGuardConfig* config = GetExactZoneConfig(caller->GetZoneId(), responseFaction);
        if (!config || !ConfigMatchesArea(*config, caller->GetAreaId()) ||
            config->guardEntries.empty())
        {
            ECG_LOG_WARN("TOWN ALARM: no area-valid guard response for zone %u area %u faction %u (caller %s entry %u)",
                caller->GetZoneId(), caller->GetAreaId(), TeamIdToGuardDbFaction(responseFaction),
                caller->GetName().c_str(), caller->GetEntry());
            return false;
        }

        // Proximity is not a crime, but the existing per-player state is the
        // authoritative owner of temporary-guard counts and normal wanted
        // dispatch cooldowns. Empty (zero-point) states are removed at logout.
        WantedState* state = GetOrCreateWantedState(target->GetGUID());

        // One settlement-wide call at a time for this player. Without this
        // shared latch, several civilians whose staggered scans land together
        // can each yell and summon a wave in the same second.
        if (!state->CanSpawnGuard(m_civilianAlarmRepeatMs))
        {
            ECG_LOG_TRACE("TOWN ALARM: shared repeat cooldown active for %s in zone %u",
                target->GetName().c_str(), caller->GetZoneId());
            return false;
        }

        if (state->activeGuardCount >= m_maxActiveGuards)
        {
            ECG_LOG_DEBUG("TOWN ALARM: active guard cap %u reached for %s",
                m_maxActiveGuards, target->GetName().c_str());
            return false;
        }

        uint32 const requestedGuards = urand(m_civilianAlarmMinGuards, m_civilianAlarmMaxGuards);
        uint32 const guardsToSpawn = std::min(requestedGuards,
            m_maxActiveGuards - state->activeGuardCount);
        if (guardsToSpawn == 0)
            return false;

        // The client performs language scrambling. Send the readable native
        // line in the caller faction's language; never hard-code a garbled
        // Common/Orcish rendering.
        if (allowCallerSpeech)
            PlayVictimYell(caller, target);

        ECG_LOG_INFO("TOWN CIVILIAN ALARM: trigger=%s caller=%s entry=%u GUID=%s faction=%u zone=%u area=%u target=%s requested=%u spawning=%u",
            trigger ? trigger : "unknown",
            caller->GetName().c_str(), caller->GetEntry(), caller->GetGUID().ToString().c_str(),
            TeamIdToGuardDbFaction(responseFaction), caller->GetZoneId(), caller->GetAreaId(),
            target->GetName().c_str(), requestedGuards, guardsToSpawn);

        // Commit before AttackStart: an attack crime recorded immediately after
        // this alarm must not launch a second wanted-level wave synchronously.
        state->lastGuardSpawnTime = std::chrono::steady_clock::now();

        // Retail civilian calls create temporary local reinforcements. Normal
        // permanent guards retain their own GuardAI aggro/assistance behavior.
        SpawnGuards(target, guardsToSpawn, false, responseFaction,
            caller->GetZoneId(), false, false);

        ECG_LOG_INFO("TOWN CIVILIAN ALARM RESPONSE: trigger=%s target=%s zone=%u faction=%u temporary_spawn_requested=%u active_after=%u patrol_after_combat=%s despawn_ooc_ms=%u",
            trigger ? trigger : "unknown", target->GetName().c_str(), caller->GetZoneId(),
            TeamIdToGuardDbFaction(responseFaction), guardsToSpawn,
            state->activeGuardCount, m_guardPatrolEnabled ? "yes" : "no",
            m_guardPatrolDurationMs);

        sGuardSystemBridge->EmitGuardsDispatched(target, guardsToSpawn,
            caller->GetZoneId(), false);
        return true;
    }

    void ECGuardSystemManager::OnHostilePlayerNearCivilian(Player* player, Creature* civilian)
    {
        if (!m_enabled || !m_civilianProximityEnabled || !player || !player->IsInWorld() ||
            !player->IsAlive() || !civilian || !civilian->IsInWorld() ||
            civilian->IsDuringRemoveFromWorld() || !civilian->IsAlive())
            return;

        if (!HasTownGuardResponse(player, civilian))
            return;

        ECG_LOG_INFO("CIVILIAN PROXIMITY ROUTE: attacker=%s victim=%s entry=%u GUID=%s zone=%u area=%u faction=%u route=native-town-defense",
            player->GetName().c_str(), civilian->GetName().c_str(), civilian->GetEntry(),
            civilian->GetGUID().ToString().c_str(), civilian->GetZoneId(), civilian->GetAreaId(),
            TeamIdToGuardDbFaction(GetCrimeFaction(player, civilian)));

        // Merely entering an opposing settlement's civilian aggro radius is not
        // a crime and must not grant wanted points. It only triggers the retail-
        // style local call for help.
        DispatchCivilianTownAlarm(player, civilian, "hostile-proximity");
    }

    uint32 ECGuardSystemManager::AlertNearbyGuards(Player* target, Creature* witness, uint32 maxGuards)
    {
        if (!target || !target->IsInWorld() || !target->IsAlive() ||
            !witness || !witness->IsInWorld() || witness->IsDuringRemoveFromWorld() ||
            !witness->IsAlive())
            return 0;

        WantedState* state = GetWantedState(target->GetGUID());
        TeamId responseFaction = state ? state->crimeFaction : GetZoneFaction(witness->GetZoneId());
        const ZoneGuardConfig* config = GetZoneConfig(witness->GetZoneId(), responseFaction);
        if (!config || !ConfigMatchesArea(*config, witness->GetAreaId()) ||
            config->guardEntries.empty())
            return 0;

        // Collect all guard entries to search for
        std::set<uint32> guardEntriesToFind;
        for (uint32 entry : config->guardEntries)
            guardEntriesToFind.insert(entry);
        for (uint32 entry : config->eliteEntries)
            guardEntriesToFind.insert(entry);

        // Find nearby guards by searching for each guard entry type
        std::list<Creature*> nearbyGuards;
        for (uint32 guardEntry : guardEntriesToFind)
        {
            std::list<Creature*> foundGuards;
            witness->GetCreatureListWithEntryInGrid(foundGuards, guardEntry, m_guardSearchRadius);
            nearbyGuards.splice(nearbyGuards.end(), foundGuards);
        }

        uint32 alertedCount = 0;
        for (Creature* creature : nearbyGuards)
        {
            if (alertedCount >= maxGuards)
                break;

            // Check if guard is available (alive, not in combat, not already chasing someone)
            if (!creature || !creature->IsInWorld() || creature->IsDuringRemoveFromWorld() ||
                !creature->IsAlive() ||
                creature->IsInCombat() || creature->GetVictim())
                continue;

            // Alert the guard
            creature->AI()->AttackStart(target);

            // Random yell
            const auto& yells = FlavorText::GetGuardChaseYells();
            if (m_customGuardYellsEnabled && !yells.empty() &&
                creature->IsInWorld() && !creature->IsDuringRemoveFromWorld() &&
                creature->IsAlive())
            {
                creature->Yell(yells[urand(0, yells.size() - 1)], LANG_UNIVERSAL);
            }

            alertedCount++;

            if (m_debugMode)
            {
                LOG_DEBUG("module", "EC Guard System: Alerted guard {} (entry {}) to chase {}",
                    creature->GetName(), creature->GetEntry(), target->GetName());
            }
        }

        // Emit guards alerted event to realtime bridge
        if (alertedCount > 0)
        {
            sGuardSystemBridge->EmitGuardsAlerted(target, alertedCount, witness->GetZoneId());
        }

        return alertedCount;
    }

    void ECGuardSystemManager::SpawnGuards(Player* target, uint32 count, bool elite,
                                            std::optional<TeamId> responseFaction,
                                            uint32 responseZoneId,
                                            bool scaleToPlayer,
                                            bool useCustomChaseYell)
    {
        if (!target || count == 0)
        {
            ECG_LOG_WARN("SpawnGuards: Invalid target or count");
            return;
        }

        uint32 const zoneId = responseZoneId != 0 ? responseZoneId : target->GetZoneId();

        if (responseZoneId != 0 && responseZoneId != target->GetZoneId())
        {
            ECG_LOG_WARN("SpawnGuards: Rejecting stale response for %s: requested zone %u, current zone %u",
                target->GetName().c_str(), responseZoneId, target->GetZoneId());
            return;
        }

        ECG_LOG_TRACE("SpawnGuards: Spawning %u %s guards for %s in zone %u",
            count, elite ? "elite" : "regular", target->GetName().c_str(), zoneId);

        Map* map = target->GetMap();
        if (!map)
        {
            ECG_LOG_WARN("SpawnGuards: No map for player %s", target->GetName().c_str());
            return;
        }

        // The civilian-alarm path supplies its local response faction directly.
        // Normal wanted dispatches continue to use the player's crime state.
        WantedState* state = GetWantedState(target->GetGUID());
        if (!responseFaction.has_value() && state && state->crimeZoneId != 0 &&
            state->crimeZoneId != zoneId)
        {
            ECG_LOG_WARN("SpawnGuards: Rejecting cross-zone wanted response for %s: crime zone %u, current zone %u",
                target->GetName().c_str(), state->crimeZoneId, zoneId);
            return;
        }
        TeamId targetFaction = responseFaction.value_or(
            state ? state->crimeFaction : TEAM_NEUTRAL);

        // Try the crime faction first, then try neutral as fallback
        const ZoneGuardConfig* config = GetZoneConfig(zoneId, targetFaction);
        if (!config)
        {
            // Fallback to neutral zone config
            config = GetZoneConfig(zoneId, TEAM_NEUTRAL);
            if (config)
            {
                ECG_LOG_DEBUG("SpawnGuards: Using neutral zone config for zone %u", zoneId);
            }
        }

        if (!config)
        {
            ECG_LOG_WARN("SpawnGuards: No zone config for zone %u faction %u (or neutral fallback)",
                zoneId, static_cast<uint8>(targetFaction));
            return;
        }

        if (!ConfigMatchesArea(*config, target->GetAreaId()))
        {
            ECG_LOG_WARN("SpawnGuards: Rejecting area-leaked response for %s: config zone=%u area=%u, current area=%u faction=%u",
                target->GetName().c_str(), zoneId, config->areaId, target->GetAreaId(),
                TeamIdToGuardDbFaction(targetFaction));
            return;
        }

        const std::vector<uint32>& entries = elite && !config->eliteEntries.empty()
            ? config->eliteEntries
            : config->guardEntries;

        if (entries.empty())
            return;

        // Determine guard tier based on the player's wanted level
        GuardTier guardTier = scaleToPlayer && state
            ? GetGuardTierForStars(state->stars)
            : GuardTier::REGULAR;
        uint32 spawnedCount = 0;

        for (uint32 i = 0; i < count; ++i)
        {
            // At 3+ stars, use elite entries for some guards (every other one gets elites if available)
            bool useElite = elite && !config->eliteEntries.empty() &&
                (i % 2 == 0 || (state && state->stars >= WantedStars::FIVE_STAR));
            const std::vector<uint32>& spawnEntries = useElite ? config->eliteEntries : config->guardEntries;
            if (spawnEntries.empty())
                continue;
            uint32 entry = spawnEntries[urand(0, spawnEntries.size() - 1)];

            // Spawn position: random point around player
            float angle = frand(0.0f, 2.0f * static_cast<float>(M_PI));
            float dist = frand(15.0f, 30.0f);  // 15-30 yards away
            float x = target->GetPositionX() + cos(angle) * dist;
            float y = target->GetPositionY() + sin(angle) * dist;
            float z = target->GetPositionZ();

            // Adjust Z to ground level
            target->UpdateAllowedPositionZ(x, y, z);

            // Spawn the guard as temporary summon
            if (TempSummon* guard = map->SummonCreature(entry, {x, y, z, 0.0f}, nullptr, m_guardPatrolDurationMs))
            {
                guard->SetTempSummonType(TEMPSUMMON_TIMED_DESPAWN_OUT_OF_COMBAT);

                uint8 guardBaseLevel = guard->GetLevel();
                uint8 targetLevel = guardBaseLevel;

                if (scaleToPlayer)
                {
                    // The custom wanted system uses hybrid scaling. Native town
                    // calls retain the local guard template's original level.
                    uint8 playerScaledLevel = static_cast<uint8>(
                        std::max(1, static_cast<int>(target->GetLevel()) - 5));
                    targetLevel = std::max(guardBaseLevel, playerScaledLevel);

                    if (targetLevel != guardBaseLevel)
                    {
                        uint32 oldHP = guard->GetMaxHealth();
                        guard->SetLevel(targetLevel);
                        guard->SelectLevel();
                        uint32 newHP = guard->GetMaxHealth();
                        ECG_LOG_INFO("SpawnGuards: Guard %s SCALED level %u->%u (HP: %u->%u, player level %u)",
                            guard->GetName().c_str(), guardBaseLevel, targetLevel,
                            oldHP, newHP, target->GetLevel());
                    }
                }

                // Track/count before AttackStart. AI combat hooks can execute
                // synchronously and must observe this guard as already active.
                RegisterSpawnedGuard(guard->GetGUID(), target->GetGUID());

                // Register spell timers for veteran+ guards
                RegisterGuardSpellTimers(guard->GetGUID(), guardTier);

                if (state)
                    state->activeGuardCount++;

                spawnedCount++;

                guard->AI()->AttackStart(target);

                // Emit guard engaged event to realtime bridge
                sGuardSystemBridge->EmitGuardEngaged(target, guard);

                // The GTA-style wanted route may retain its custom flavor text.
                // Retail-style civilian responders do not emit invented English.
                if (guard->IsInWorld() && !guard->IsDuringRemoveFromWorld() && guard->IsAlive() &&
                    useCustomChaseYell && m_customGuardYellsEnabled)
                {
                    const auto& yells = FlavorText::GetGuardChaseYells();
                    if (!yells.empty())
                        guard->Yell(yells[urand(0, yells.size() - 1)], LANG_UNIVERSAL);
                }

                ECG_LOG_INFO("Spawned temporary guard %s (entry %u) zone=%u faction=%u at [%.1f, %.1f, %.1f] to chase %s base_level=%u final_level=%u native_level=%s patrol_radius=%.0f despawn_ooc_ms=%u",
                    guard->GetName().c_str(), entry, zoneId,
                    TeamIdToGuardDbFaction(targetFaction), x, y, z,
                    target->GetName().c_str(), guardBaseLevel, targetLevel,
                    scaleToPlayer ? "no" : "yes", m_guardPatrolRadius,
                    m_guardPatrolDurationMs);
            }
        }

        // Emit guards spawned event to realtime bridge (after all guards are spawned)
        if (spawnedCount > 0)
        {
            sGuardSystemBridge->EmitGuardsSpawned(target, spawnedCount, zoneId, elite);
        }
    }

    uint32 ECGuardSystemManager::GetGuardCountForStars(WantedStars stars) const
    {
        // Tiered guard response: more guards at higher stars
        switch (stars)
        {
            case WantedStars::ONE_STAR:   return 2;  // 2 regular melee
            case WantedStars::TWO_STAR:   return 3;  // 3 regular melee
            case WantedStars::THREE_STAR: return 4;  // 4 guards (mix regular + veteran)
            case WantedStars::FOUR_STAR:  return 5;  // 5 guards (mix regular + elite)
            case WantedStars::FIVE_STAR:  return 6;  // 6 guards (elite squad with spells)
            default: return 0;
        }
    }

    bool ECGuardSystemManager::IsTownCivilianCandidate(Creature* creature) const
    {
        // The proximity scanner is a live-world path. Keep it narrower than
        // the static crime-victim classifier so corpses, combat actors, and
        // ambient objects never acquire civilian guard-call behaviour.
        if (!creature || !creature->IsInWorld() || creature->IsDuringRemoveFromWorld() ||
            !creature->IsAlive() || creature->IsInCombat() ||
            !creature->IsCivilian() ||
            creature->GetCreatureType() != CREATURE_TYPE_HUMANOID)
            return false;

        if (creature->IsSummon() || creature->IsPet() || creature->IsTotem() ||
            creature->IsCritter() || !creature->GetOwnerGUID().IsEmpty() ||
            creature->IsTrigger() || creature->IsHostileToPlayers() ||
            creature->GetReactState() == REACT_AGGRESSIVE)
            return false;

        if (IsNonCivilianCreatureType(creature->GetCreatureType()))
            return false;

        // Avoid a full configured-entry scan in the per-tick proximity prefilter.
        // HasTownGuardResponse performs the authoritative classification after
        // the staggered scan finds a hostile player.
        return !(creature->GetCreatureTemplate()->flags_extra & CREATURE_FLAG_EXTRA_GUARD);
    }

    bool ECGuardSystemManager::IsCreatureCivilian(Creature* creature) const
    {
        if (!creature)
            return false;

        // Civilian crimes and witness dialogue belong to people, not passive
        // wildlife or world/combat helper objects. In particular, a service
        // NPC flag on a hostile template is not sufficient evidence that the
        // creature is a civilian.
        if (creature->IsSummon() || creature->IsPet() || creature->IsTotem() ||
            creature->IsCritter() || !creature->GetOwnerGUID().IsEmpty() ||
            creature->IsTrigger() ||
            creature->GetCreatureType() != CREATURE_TYPE_HUMANOID ||
            IsNonCivilianCreatureType(creature->GetCreatureType()) ||
            creature->IsHostileToPlayers())
            return false;

        // Guards are handled separately.
        if (IsCreatureGuard(creature))
            return false;

        // AzerothCore's explicit civilian template flag is authoritative once
        // the identity/hostility exclusions above have passed.
        if (creature->IsCivilian())
            return true;

        // A service flag may identify an unflagged town NPC, but it must never
        // turn an aggressive combat actor into a civilian.
        if (creature->GetReactState() == REACT_AGGRESSIVE)
            return false;

        // Check if it's an NPC type we consider civilian (vendor, trainer, questgiver, etc.)
        uint32 npcFlags = creature->GetNpcFlags();
        if (npcFlags & (UNIT_NPC_FLAG_VENDOR | UNIT_NPC_FLAG_TRAINER | UNIT_NPC_FLAG_QUESTGIVER |
                        UNIT_NPC_FLAG_GOSSIP | UNIT_NPC_FLAG_INNKEEPER | UNIT_NPC_FLAG_BANKER |
                        UNIT_NPC_FLAG_AUCTIONEER | UNIT_NPC_FLAG_STABLEMASTER | UNIT_NPC_FLAG_GUILD_BANKER))
        {
            return true;
        }

        // Deliberately no "non-aggressive means civilian" fallback. That broad
        // heuristic classified fish schools and other ambient units as people.
        return false;
    }

    bool ECGuardSystemManager::IsCreatureGuard(Creature* creature) const
    {
        if (!creature)
            return false;

        // Check CREATURE_FLAG_EXTRA_GUARD
        if (creature->GetCreatureTemplate()->flags_extra & CREATURE_FLAG_EXTRA_GUARD)
            return true;

        // Also check every faction configuration in this zone. Contested zones
        // such as Ashenvale have both Alliance and Horde rows, so selecting one
        // arbitrary zone faction misses valid local guards.
        uint32 zoneId = creature->GetZoneId();
        uint32 entry = creature->GetEntry();
        for (auto const& [key, config] : m_zoneConfigs)
        {
            if ((key >> 8) != zoneId)
                continue;
            if (!ConfigMatchesArea(config, creature->GetAreaId()))
                continue;

            for (uint32 guardEntry : config.guardEntries)
            {
                if (entry == guardEntry)
                    return true;
            }
            for (uint32 eliteEntry : config.eliteEntries)
            {
                if (entry == eliteEntry)
                    return true;
            }
            if (entry == config.captainEntry)
                return true;
        }

        return false;
    }

    TeamId ECGuardSystemManager::GetZoneFaction(uint32 zoneId) const
    {
        // Check our config first
        for (const auto& pair : m_zoneConfigs)
        {
            if ((pair.first >> 8) == zoneId)
                return static_cast<TeamId>(pair.first & 0xFF);
        }

        // Default faction detection based on zone
        // This is a simplified version - in practice you'd want to use AreaTableEntry
        // Major Alliance cities
        if (zoneId == 1519 || zoneId == 1537 || zoneId == 1657 || zoneId == 3557)
            return TEAM_ALLIANCE;

        // Major Horde cities
        if (zoneId == 1637 || zoneId == 1638 || zoneId == 1497 || zoneId == 3487)
            return TEAM_HORDE;

        // Neutral
        return TEAM_NEUTRAL;
    }

    TeamId ECGuardSystemManager::GetCrimeFaction(Player* player, Creature* victim) const
    {
        if (!player || !victim || victim->IsNeutralToAll())
            return TEAM_NEUTRAL;

        if (victim->IsFriendlyTo(player))
            return player->GetTeamId();

        if (victim->IsHostileTo(player))
            return player->GetTeamId() == TEAM_ALLIANCE ? TEAM_HORDE : TEAM_ALLIANCE;

        return TEAM_NEUTRAL;
    }

    bool ECGuardSystemManager::HasTownGuardResponse(Player* player, Creature* victim) const
    {
        if (!player || !victim || !victim->IsCivilian() || !IsCreatureCivilian(victim))
            return false;

        TeamId faction = GetCrimeFaction(player, victim);
        const ZoneGuardConfig* config = GetExactZoneConfig(victim->GetZoneId(), faction);
        return config && ConfigMatchesArea(*config, victim->GetAreaId()) &&
            !config->guardEntries.empty();
    }

    void ECGuardSystemManager::BroadcastNPCDeath(Creature* npc, Player* killer)
    {
        if (!npc || !killer || !m_broadcastNPCDeaths)
            return;

        uint32 zoneId = npc->GetZoneId();
        TeamId faction = GetCrimeFaction(killer, npc);

        // Only broadcast if there's a faction to notify
        if (faction == TEAM_NEUTRAL)
            return;

        if (m_customLocalDefenceTextEnabled)
        {
            AreaTableEntry const* area = sAreaTableStore.LookupEntry(zoneId);
            std::string zoneName = area ? area->area_name[0] : "Unknown";

            std::ostringstream ss;
            ss << "|cffff0000[ALERT]|r " << killer->GetName()
               << " has killed " << npc->GetName() << " in " << zoneName << "!";

            if (ChannelMgr* channelMgr = ChannelMgr::forTeam(faction))
            {
                if (Channel* localDefense = channelMgr->GetChannel("LocalDefense", killer))
                {
                    WorldPacket data;
                    ChatHandler::BuildChatPacket(data, CHAT_MSG_CHANNEL, LANG_UNIVERSAL,
                        nullptr, nullptr, ss.str(), 0, "LocalDefense", DEFAULT_LOCALE);

                    if (Map* map = npc->GetMap())
                    {
                        map->DoForAllPlayers([&](Player* player)
                        {
                            if (player->GetZoneId() == zoneId && player->GetTeamId() == faction)
                                player->SendDirectMessage(&data);
                        });
                    }
                }
            }
        }

        // Preserve AzerothCore's native faction/zone defense notification.
        npc->SendZoneUnderAttackMessage(killer);

        // Emit local defence alert to realtime bridge
        sGuardSystemBridge->EmitLocalDefenceAlert(killer, npc, zoneId);

        ECG_LOG_DEBUG("Native zone-under-attack notification: victim=%s entry=%u killer=%s zone=%u faction=%u custom_text=%s",
            npc->GetName().c_str(), npc->GetEntry(), killer->GetName().c_str(),
            zoneId, TeamIdToGuardDbFaction(faction),
            m_customLocalDefenceTextEnabled ? "enabled" : "disabled");
    }

    void ECGuardSystemManager::PlayVictimYell(Creature* victim, Player* attacker)
    {
        if (!victim || !victim->IsInWorld() || victim->IsDuringRemoveFromWorld() ||
            !victim->IsAlive() ||
            !attacker || !attacker->IsInWorld() || !attacker->IsAlive())
            return;

        if (IsCreatureCivilian(victim))
        {
            TeamId faction = GetCrimeFaction(attacker, victim);
            victim->Yell("Guards! Help me!",
                faction == TEAM_ALLIANCE ? LANG_COMMON :
                faction == TEAM_HORDE ? LANG_ORCISH : LANG_UNIVERSAL);
            return;
        }

        const std::vector<std::string>& yells = FlavorText::GetGuardYells();
        if (m_customGuardYellsEnabled && !yells.empty())
        {
            victim->Yell(yells[urand(0, yells.size() - 1)], LANG_UNIVERSAL);
        }
    }

    const ZoneGuardConfig* ECGuardSystemManager::GetExactZoneConfig(uint32 zoneId, TeamId faction) const
    {
        auto it = m_zoneConfigs.find(MakeZoneKey(zoneId, faction));
        if (it != m_zoneConfigs.end())
            return &it->second;

        return nullptr;
    }

    const ZoneGuardConfig* ECGuardSystemManager::GetZoneConfig(uint32 zoneId, TeamId faction) const
    {
        if (const ZoneGuardConfig* exact = GetExactZoneConfig(zoneId, faction))
            return exact;

        // Try neutral faction as fallback
        return GetExactZoneConfig(zoneId, TEAM_NEUTRAL);
    }

    void ECGuardSystemManager::RegisterZoneConfig(const ZoneGuardConfig& config)
    {
        m_zoneConfigs[MakeZoneKey(config.zoneId, config.faction)] = config;
    }

    void ECGuardSystemManager::RegisterSpawnedGuard(ObjectGuid guardGuid, ObjectGuid targetGuid)
    {
        std::lock_guard<std::mutex> lock(m_guardMutex);
        m_guardToTarget[guardGuid] = targetGuid;
    }

    void ECGuardSystemManager::UnregisterSpawnedGuard(ObjectGuid guardGuid)
    {
        ObjectGuid targetGuid;
        {
            std::lock_guard<std::mutex> lock(m_guardMutex);
            auto it = m_guardToTarget.find(guardGuid);
            if (it == m_guardToTarget.end())
                return;
            targetGuid = it->second;
            m_guardToTarget.erase(it);
            m_guardSpellTimers.erase(guardGuid);
        }

        // Decrease active guard count for target (outside guard lock to avoid deadlock)
        WantedState* state = GetWantedState(targetGuid);
        if (state && state->activeGuardCount > 0)
            state->activeGuardCount--;
    }

    void ECGuardSystemManager::OnSpawnedGuardDeath(Creature* guard)
    {
        if (!guard)
            return;

        // Note: OnZoneGuardKilled is called from OnPlayerKillCreature with the killer's GUID
        // This function handles cleanup only (no zone state update needed here)
        UnregisterSpawnedGuard(guard->GetGUID());
    }

    bool ECGuardSystemManager::IsSpawnedGuard(ObjectGuid guardGuid) const
    {
        std::lock_guard<std::mutex> lock(m_guardMutex);
        return m_guardToTarget.find(guardGuid) != m_guardToTarget.end();
    }

    void ECGuardSystemManager::SetGuardPatrolling(Creature* guard)
    {
        if (!guard || !guard->IsAlive() || !m_guardPatrolEnabled)
            return;

        // Only set patrol if guard is not already in combat or moving to a target
        if (guard->IsInCombat() || guard->GetVictim())
            return;

        // Check if already patrolling (has random movement)
        if (guard->GetMotionMaster()->GetCurrentMovementGeneratorType() == RANDOM_MOTION_TYPE)
            return;

        guard->GetMotionMaster()->MoveRandom(m_guardPatrolRadius);

        ECG_LOG_INFO("Temporary guard %s (entry %u) leaving combat and patrolling radius %.0f until out-of-combat despawn",
            guard->GetName().c_str(), guard->GetEntry(), m_guardPatrolRadius);
    }

    void ECGuardSystemManager::OnPlayerKillCreature(Player* killer, Creature* victim,
                                                     bool allowTownAlarm,
                                                     char const* trigger)
    {
        if (!m_enabled || !killer || !victim)
            return;

        if (IsCreatureCivilian(victim))
        {
            bool const explicitTownCivilian = HasTownGuardResponse(killer, victim);
            TeamId const offendedFaction = GetCrimeFaction(killer, victim);

            ECG_LOG_INFO("CIVILIAN KILL ROUTE: killer=%s victim=%s entry=%u GUID=%s zone=%u area=%u explicit=%s classified=yes faction=%u route=%s",
                killer->GetName().c_str(), victim->GetName().c_str(), victim->GetEntry(),
                victim->GetGUID().ToString().c_str(), victim->GetZoneId(), victim->GetAreaId(),
                victim->IsCivilian() ? "yes" : "no", TeamIdToGuardDbFaction(offendedFaction),
                explicitTownCivilian ? "immediate-town-defense" :
                (sECWitnessSys->IsEnabled() ? "pending-witness" : "immediate-no-witness-system"));

            // Explicit town civilians call guards as soon as they are attacked.
            // Their kill is therefore public even if no separate witness survives.
            if (explicitTownCivilian)
            {
                // Commit the town response before AddCrime. AddCrime may invoke
                // the separate wanted dispatcher, which observes the spawn
                // cooldown and therefore cannot create a duplicate wave.
                if (allowTownAlarm)
                    // The kill hook runs after the victim is dead. Preserve the
                    // local guard response, but never let the corpse originate
                    // speech.
                    DispatchCivilianTownAlarm(killer, victim,
                        trigger ? trigger : "kill-fallback", false);
                AddCrime(killer, CrimeType::CIVILIAN_KILL, victim);
            }
            else if (sECWitnessSys->IsEnabled())
            {
                if (!sECWitnessSys->CheckForWitnesses(killer, victim, CrimeType::CIVILIAN_KILL))
                    ECG_LOG_DEBUG("Civilian kill by %s went unwitnessed - no crime registered",
                        killer->GetName().c_str());
            }
            else
                AddCrime(killer, CrimeType::CIVILIAN_KILL, victim);

            BroadcastNPCDeath(victim, killer);
            sGuardSystemBridge->EmitCivilianKilled(killer, victim);
        }
        else if (IsCreatureGuard(victim))
        {
            bool const transientGuard = IsSpawnedGuard(victim->GetGUID());

            // Guard kills are always detected (guards report their own attacks)
            AddCrime(killer, CrimeType::GUARD_KILL, victim);
            BroadcastNPCDeath(victim, killer);
            sGuardSystemBridge->EmitGuardKilled(killer, victim);

            // Pursuit/alarm summons are temporary and must not alter the
            // strategic permanent-garrison count. Counting their spawn but not
            // their timed despawn caused Ashenvale to inflate to 352 guards.
            if (!transientGuard)
            {
                TeamId const guardFaction = GetCrimeFaction(killer, victim);
                const ZoneState* strategicZone = sECZoneStateMgr->GetZoneState(victim->GetZoneId());

                // ZoneState is zone-keyed. In mixed-faction Ashenvale, only a
                // guard matching Astranaar's configured owner may reduce its
                // strategic garrison. Horde outpost guards remain guard crimes
                // but cannot decrement the Alliance Sentinel count.
                if (strategicZone && MatchesStrategicState(*strategicZone, guardFaction, *victim))
                {
                    sECZoneStateMgr->OnZoneGuardKilled(victim->GetZoneId(), killer->GetGUID());
                }
                else if (strategicZone)
                {
                    ECG_LOG_INFO("STRATEGIC GUARD DEATH SKIPPED: guard=%s entry=%u zone=%u area=%u guardFaction=%u owner=%u",
                        victim->GetName().c_str(), victim->GetEntry(), victim->GetZoneId(), victim->GetAreaId(),
                        static_cast<uint8>(guardFaction), static_cast<uint8>(strategicZone->owningFaction));
                }
            }

            OnSpawnedGuardDeath(victim);
        }
    }

    void ECGuardSystemManager::OnPlayerAttackCreature(Player* attacker, Creature* victim,
                                                       bool allowTownAlarm,
                                                       char const* trigger)
    {
        if (!m_enabled || !attacker || !attacker->IsInWorld() || !attacker->IsAlive() ||
            !victim || !victim->IsInWorld() || victim->IsDuringRemoveFromWorld() ||
            !victim->IsAlive())
            return;

        if (IsCreatureCivilian(victim))
        {
            if (HasTownGuardResponse(attacker, victim))
            {
                ECG_LOG_INFO("CIVILIAN ATTACK ROUTE: attacker=%s victim=%s entry=%u GUID=%s zone=%u area=%u explicit=yes faction=%u route=immediate-town-defense",
                    attacker->GetName().c_str(), victim->GetName().c_str(), victim->GetEntry(),
                    victim->GetGUID().ToString().c_str(), victim->GetZoneId(), victim->GetAreaId(),
                    TeamIdToGuardDbFaction(GetCrimeFaction(attacker, victim)));

                if (allowTownAlarm)
                    DispatchCivilianTownAlarm(attacker, victim,
                        trigger ? trigger : "damage-fallback");
                AddCrime(attacker, CrimeType::CIVILIAN_ATTACK, victim);
            }
            else if (sECWitnessSys->IsEnabled())
            {
                if (!sECWitnessSys->CheckForWitnesses(attacker, victim, CrimeType::CIVILIAN_ATTACK))
                    ECG_LOG_DEBUG("Civilian attack by %s went unwitnessed - no crime registered",
                        attacker->GetName().c_str());
            }
            else
            {
                PlayVictimYell(victim, attacker);
                AddCrime(attacker, CrimeType::CIVILIAN_ATTACK, victim);
            }
        }
        else if (IsCreatureGuard(victim))
        {
            // Guard attacks are always detected (the guard itself is the witness)
            AddCrime(attacker, CrimeType::GUARD_ATTACK, victim);
            PlayVictimYell(victim, attacker);
            sGuardSystemBridge->EmitGuardAttacked(attacker, victim);
        }
    }

    void ECGuardSystemManager::OnGuardDeath(Creature* guard, Player* killer)
    {
        if (!m_enabled || !guard || !killer)
            return;

        AddCrime(killer, CrimeType::GUARD_KILL, guard);
        BroadcastNPCDeath(guard, killer);
        OnSpawnedGuardDeath(guard);
    }

    void ECGuardSystemManager::OnNPCDeath(Creature* npc, Player* killer)
    {
        if (!m_enabled || !npc || !killer)
            return;

        if (IsCreatureGuard(npc))
        {
            OnGuardDeath(npc, killer);
        }
        else if (IsCreatureCivilian(npc))
        {
            AddCrime(killer, CrimeType::CIVILIAN_KILL, npc);
            BroadcastNPCDeath(npc, killer);
        }
    }

    // ==========================================================================
    // ELITE GUARD SPELL SYSTEM
    // ==========================================================================

    void ECGuardSystemManager::RegisterGuardSpellTimers(ObjectGuid guardGuid, GuardTier tier)
    {
        if (tier == GuardTier::REGULAR)
        {
            ECG_LOG_TRACE("RegisterGuardSpellTimers: Guard %s is REGULAR tier, no spell timers needed",
                guardGuid.ToString().c_str());
            return;  // Regular guards don't cast spells
        }

        std::lock_guard<std::mutex> lock(m_guardMutex);
        GuardSpellTimers timers;
        timers.tier = tier;
        m_guardSpellTimers[guardGuid] = timers;

        ECG_LOG_INFO("RegisterGuardSpellTimers: Guard %s registered as tier %u (%s) - spells: %s%s%s%s%s",
            guardGuid.ToString().c_str(), static_cast<uint8>(tier),
            tier == GuardTier::CHAMPION ? "CHAMPION" :
            tier == GuardTier::ELITE ? "ELITE" :
            tier == GuardTier::VETERAN ? "VETERAN" : "REGULAR",
            tier >= GuardTier::VETERAN ? "Hamstring " : "",
            tier >= GuardTier::VETERAN ? "Charge " : "",
            tier >= GuardTier::ELITE ? "ShieldBash " : "",
            tier >= GuardTier::ELITE ? "Net " : "",
            tier >= GuardTier::CHAMPION ? "IntimidatingShout" : "");
    }

    void ECGuardSystemManager::UnregisterGuardSpellTimers(ObjectGuid guardGuid)
    {
        std::lock_guard<std::mutex> lock(m_guardMutex);
        m_guardSpellTimers.erase(guardGuid);
    }

    GuardSpellTimers* ECGuardSystemManager::GetGuardSpellTimers(ObjectGuid guardGuid)
    {
        // Caller must hold m_guardMutex
        auto it = m_guardSpellTimers.find(guardGuid);
        return (it != m_guardSpellTimers.end()) ? &it->second : nullptr;
    }

    void ECGuardSystemManager::UpdateGuardSpells(Creature* guard, uint32 diff)
    {
        if (!guard || !guard->IsAlive() || !guard->IsInCombat())
            return;

        Unit* victim = guard->GetVictim();
        if (!victim)
            return;

        GuardSpellTimers* timers = nullptr;
        {
            std::lock_guard<std::mutex> lock(m_guardMutex);
            timers = GetGuardSpellTimers(guard->GetGUID());
        }

        if (!timers)
            return;

        float distToTarget = guard->GetDistance(victim);

        // Charge (one-shot gap closer, VETERAN+)
        if (!timers->chargeUsed && timers->tier >= GuardTier::VETERAN)
        {
            if (timers->chargeTimer <= diff)
            {
                if (distToTarget >= 8.0f && distToTarget <= 25.0f)
                {
                    ECG_LOG_DEBUG("UpdateGuardSpells: %s casting CHARGE (spell %u) on %s at %.1f yards",
                        guard->GetName().c_str(), GuardSpells::CHARGE,
                        victim->GetName().c_str(), distToTarget);
                    guard->CastSpell(victim, GuardSpells::CHARGE, false);
                    timers->chargeUsed = true;
                }
                else
                {
                    ECG_LOG_TRACE("UpdateGuardSpells: %s charge ready but target at %.1f yards (need 8-25)",
                        guard->GetName().c_str(), distToTarget);
                }
            }
            else
            {
                timers->chargeTimer -= diff;
            }
        }

        // Hamstring (snare, VETERAN+)
        if (timers->tier >= GuardTier::VETERAN)
        {
            if (timers->hamstringTimer <= diff)
            {
                ECG_LOG_DEBUG("UpdateGuardSpells: %s casting HAMSTRING (spell %u) on %s",
                    guard->GetName().c_str(), GuardSpells::HAMSTRING, victim->GetName().c_str());
                guard->CastSpell(victim, GuardSpells::HAMSTRING, false);
                timers->hamstringTimer = 8000;  // 8s cooldown
            }
            else
            {
                timers->hamstringTimer -= diff;
            }
        }

        // Shield Bash (interrupt, ELITE+)
        if (timers->tier >= GuardTier::ELITE)
        {
            if (timers->shieldBashTimer <= diff)
            {
                if (victim->HasUnitState(UNIT_STATE_CASTING))
                {
                    ECG_LOG_DEBUG("UpdateGuardSpells: %s casting SHIELD_BASH (spell %u) - interrupting %s!",
                        guard->GetName().c_str(), GuardSpells::SHIELD_BASH, victim->GetName().c_str());
                    guard->CastSpell(victim, GuardSpells::SHIELD_BASH, false);
                }
                else
                {
                    ECG_LOG_TRACE("UpdateGuardSpells: %s Shield Bash ready but target not casting",
                        guard->GetName().c_str());
                }
                timers->shieldBashTimer = 12000;  // 12s cooldown
            }
            else
            {
                timers->shieldBashTimer -= diff;
            }
        }

        // Net (root, ELITE+)
        if (timers->tier >= GuardTier::ELITE)
        {
            if (timers->netTimer <= diff)
            {
                if (distToTarget >= 5.0f)  // Only net if target is running
                {
                    ECG_LOG_DEBUG("UpdateGuardSpells: %s casting NET (spell %u) on %s at %.1f yards",
                        guard->GetName().c_str(), GuardSpells::NET,
                        victim->GetName().c_str(), distToTarget);
                    guard->CastSpell(victim, GuardSpells::NET, false);
                }
                else
                {
                    ECG_LOG_TRACE("UpdateGuardSpells: %s Net ready but target only %.1f yards away (need 5+)",
                        guard->GetName().c_str(), distToTarget);
                }
                timers->netTimer = 15000;  // 15s cooldown
            }
            else
            {
                timers->netTimer -= diff;
            }
        }

        // Intimidating Shout (fear, CHAMPION only)
        if (timers->tier >= GuardTier::CHAMPION)
        {
            if (timers->shoutTimer <= diff)
            {
                ECG_LOG_DEBUG("UpdateGuardSpells: %s casting INTIMIDATING_SHOUT (spell %u) on %s",
                    guard->GetName().c_str(), GuardSpells::INTIMIDATING_SHOUT, victim->GetName().c_str());
                guard->CastSpell(victim, GuardSpells::INTIMIDATING_SHOUT, false);
                timers->shoutTimer = 30000;  // 30s cooldown (don't spam fear)
            }
            else
            {
                timers->shoutTimer -= diff;
            }
        }
    }

} // namespace ECGuardSystem
