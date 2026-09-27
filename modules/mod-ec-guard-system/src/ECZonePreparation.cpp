/*
 * EC Guard System - Zone Preparation Phase Implementation
 * "Shatter Before the Storm" - Foreshadowing & Buildup System
 */

#include "ECZonePreparation.h"
#include "ECGuardSystemManager.h"
#include "ECGuardSystemBridge.h"
#include "ECZoneState.h"
#include "Player.h"
#include "Creature.h"
#include "CreatureAI.h"
#include "GameObject.h"
#include "Map.h"
#include "ObjectMgr.h"
#include "ObjectAccessor.h"
#include "DatabaseEnv.h"
#include "WorldSession.h"
#include "WorldSessionMgr.h"
#include "World.h"
#include "Chat.h"
#include "Config.h"
#include "GameTime.h"
#include "DBCStructure.h"
#include "MoveSplineInit.h"
#include "MotionMaster.h"
#include <cmath>
#include <algorithm>
#include <limits>
#include <optional>
#include <random>

namespace ECGuardSystem
{
    namespace
    {
        constexpr uint8 PREP_FACTION_NEUTRAL = 0;
        constexpr uint8 PREP_FACTION_ALLIANCE = 1;
        constexpr uint8 PREP_FACTION_HORDE = 2;
        constexpr uint8 PREP_FACTION_ANY = 3;
        constexpr uint32 ASTRANAAR_ZONE_ID = 331;
        constexpr uint32 ASTRANAAR_AREA_ID = 415;
        constexpr uint32 THE_BARRENS_ZONE_ID = 17;
        constexpr uint32 THE_CROSSROADS_AREA_ID = 380;

        uint8 TeamIdToPreparationFaction(TeamId team)
        {
            if (team == TEAM_ALLIANCE)
                return PREP_FACTION_ALLIANCE;
            if (team == TEAM_HORDE)
                return PREP_FACTION_HORDE;
            return PREP_FACTION_NEUTRAL;
        }

        uint8 GetCreaturePreparationFaction(Creature const* creature)
        {
            if (!creature)
                return PREP_FACTION_NEUTRAL;

            if (FactionTemplateEntry const* faction = creature->GetFactionTemplateEntry())
            {
                if (faction->ourMask & FACTION_MASK_ALLIANCE)
                    return PREP_FACTION_ALLIANCE;
                if (faction->ourMask & FACTION_MASK_HORDE)
                    return PREP_FACTION_HORDE;
            }
            return PREP_FACTION_NEUTRAL;
        }

        bool CreatureMatchesPreparationFaction(Creature const* creature, uint8 dbFaction)
        {
            return dbFaction == PREP_FACTION_ANY ||
                GetCreaturePreparationFaction(creature) == dbFaction;
        }

        Player* FindLiveIncidentPlayer(uint32 zoneId, uint8& dbFaction)
        {
            dbFaction = PREP_FACTION_ANY;
            ZoneState* zone = sECZoneStateMgr->GetZoneState(zoneId);
            if (!zone)
                return nullptr;

            for (ObjectGuid const& criminalGuid : zone->activeCriminals)
            {
                Player* player = ObjectAccessor::FindPlayer(criminalGuid);
                if (!player || !player->IsInWorld() || player->GetZoneId() != zoneId)
                    continue;

                std::optional<WantedState> wanted =
                    sECGuardSystemMgr->GetWantedStateCopy(criminalGuid);
                if (!wanted || !wanted->IsWanted() || wanted->crimeZoneId != zoneId ||
                    (wanted->crimeAreaId != 0 && wanted->crimeAreaId != player->GetAreaId()))
                {
                    continue;
                }

                // The legacy strategic record represents one settlement and
                // one defending faction, even where the WoW zone contains
                // opposing outposts. A newer same-zone crime must not retarget
                // Astranaar speech to Horde (or Crossroads speech to Alliance).
                if (zone->owningFaction != TEAM_NEUTRAL &&
                    wanted->crimeFaction != zone->owningFaction)
                {
                    continue;
                }
                if (zoneId == ASTRANAAR_ZONE_ID && zone->owningFaction == TEAM_ALLIANCE &&
                    (wanted->crimeAreaId != ASTRANAAR_AREA_ID ||
                     player->GetAreaId() != ASTRANAAR_AREA_ID))
                {
                    continue;
                }
                if (zoneId == THE_BARRENS_ZONE_ID && zone->owningFaction == TEAM_HORDE &&
                    (wanted->crimeAreaId != THE_CROSSROADS_AREA_ID ||
                     player->GetAreaId() != THE_CROSSROADS_AREA_ID))
                {
                    continue;
                }

                dbFaction = TeamIdToPreparationFaction(wanted->crimeFaction);
                return player;
            }
            return nullptr;
        }
    }

    // ==========================================================================
    // SINGLETON
    // ==========================================================================

    ZonePreparationManager* ZonePreparationManager::instance()
    {
        static ZonePreparationManager instance;
        return &instance;
    }

    // ==========================================================================
    // INITIALIZATION
    // ==========================================================================

    void ZonePreparationManager::Initialize()
    {
        ECG_LOG_INFO("ZonePreparationManager initializing...");

        LoadFromConfig();
        LoadDialogues();
        LoadDialoguePairs();
        LoadMilitiaSpawnPoints();
        LoadTownCriers();
        LoadGameObjectTemplates();
        LoadFactionBanners();
        LoadConstructionSites();
        LoadWorkerTemplates();
        LoadInteractiveObjects();

        ECG_LOG_INFO("ZonePreparationManager initialized: %zu dialogues, %zu dialogue pairs, "
                     "%zu militia spawns, %zu criers, %zu game objects, %zu banner configs, "
                     "%zu construction sites, %zu worker templates, %zu interactive objects",
                     m_dialogues.size(), m_dialoguePairs.size(), m_militiaSpawns.size(),
                     m_crierConfigs.size(), m_gameObjectTemplates.size(), m_bannerConfigs.size(),
                     m_constructionSites.size(), m_workerTemplates.size(), m_interactiveObjects.size());
    }

    void ZonePreparationManager::LoadFromConfig()
    {
        // General settings
        m_config.enabled = sConfigMgr->GetOption<bool>("ECGuardSystem.Preparation.Enabled", true);
        m_config.dialoguesEnabled = sConfigMgr->GetOption<bool>("ECGuardSystem.Preparation.DialoguesEnabled", false);
        m_config.militiaEnabled = sConfigMgr->GetOption<bool>("ECGuardSystem.Preparation.MilitiaEnabled", true);
        m_config.crierEnabled = sConfigMgr->GetOption<bool>("ECGuardSystem.Preparation.CrierEnabled", false);
        m_config.gameObjectsEnabled = sConfigMgr->GetOption<bool>("ECGuardSystem.Preparation.GameObjectsEnabled", true);
        m_config.bannersEnabled = sConfigMgr->GetOption<bool>("ECGuardSystem.Preparation.BannersEnabled", true);

        // Timing
        m_config.dialogueCheckIntervalMs = sConfigMgr->GetOption<uint32>("ECGuardSystem.Preparation.DialogueCheckIntervalMs", 5000);
        m_config.militiaUpdateIntervalMs = sConfigMgr->GetOption<uint32>("ECGuardSystem.Preparation.MilitiaUpdateIntervalMs", 2000);
        m_config.gameObjectCheckIntervalMs = sConfigMgr->GetOption<uint32>("ECGuardSystem.Preparation.GameObjectCheckIntervalMs", 10000);

        // Dialogue settings
        m_config.dialogueTriggerRadius = sConfigMgr->GetOption<float>("ECGuardSystem.Preparation.DialogueTriggerRadius", 40.0f);
        m_config.minDialogueCooldownMs = sConfigMgr->GetOption<uint32>("ECGuardSystem.Preparation.MinDialogueCooldownMs", 30000);
        m_config.dialoguePairDelayMs = sConfigMgr->GetOption<uint32>("ECGuardSystem.Preparation.DialoguePairDelayMs", 3000);
        m_config.dialogueResponseChance = sConfigMgr->GetOption<float>("ECGuardSystem.Preparation.DialogueResponseChance", 0.4f);

        // Militia settings
        m_config.maxMilitiaPerZone = sConfigMgr->GetOption<uint32>("ECGuardSystem.Preparation.MaxMilitiaPerZone", 10);
        m_config.militiaWalkSpeed = sConfigMgr->GetOption<float>("ECGuardSystem.Preparation.MilitiaWalkSpeed", 1.0f);
        m_config.militiaRunSpeed = sConfigMgr->GetOption<float>("ECGuardSystem.Preparation.MilitiaRunSpeed", 1.5f);

        // Crier settings
        m_config.crierBaseIntervalMs = sConfigMgr->GetOption<uint32>("ECGuardSystem.Preparation.CrierBaseIntervalMs", 45000);
        m_config.crierUrgentIntervalMs = sConfigMgr->GetOption<uint32>("ECGuardSystem.Preparation.CrierUrgentIntervalMs", 20000);
        m_config.crierPanicIntervalMs = sConfigMgr->GetOption<uint32>("ECGuardSystem.Preparation.CrierPanicIntervalMs", 10000);

        // Gameobject settings
        m_config.maxBarricadesPerZone = sConfigMgr->GetOption<uint32>("ECGuardSystem.Preparation.MaxBarricadesPerZone", 15);
        m_config.maxDebrisPerZone = sConfigMgr->GetOption<uint32>("ECGuardSystem.Preparation.MaxDebrisPerZone", 20);
        m_config.barricadeRespawnMs = sConfigMgr->GetOption<uint32>("ECGuardSystem.Preparation.BarricadeRespawnMs", 300000);

        // Banner settings
        m_config.instantBannerSwap = sConfigMgr->GetOption<bool>("ECGuardSystem.Preparation.InstantBannerSwap", false);
        m_config.bannerSwapDelayMs = sConfigMgr->GetOption<uint32>("ECGuardSystem.Preparation.BannerSwapDelayMs", 5000);

        // Construction settings
        m_config.constructionEnabled = sConfigMgr->GetOption<bool>("ECGuardSystem.Preparation.ConstructionEnabled", true);
        m_config.interactiveObjectsEnabled = sConfigMgr->GetOption<bool>("ECGuardSystem.Preparation.InteractiveObjectsEnabled", true);
        m_config.constructionUpdateIntervalMs = sConfigMgr->GetOption<uint32>("ECGuardSystem.Preparation.ConstructionUpdateIntervalMs", 1500);
        m_config.workDurationMs = sConfigMgr->GetOption<uint32>("ECGuardSystem.Preparation.WorkDurationMs", 4000);
        m_config.maxWorkersPerZone = sConfigMgr->GetOption<uint32>("ECGuardSystem.Preparation.MaxWorkersPerZone", 8);

        ECG_LOG_DEBUG("Preparation config loaded: enabled=%d, dialogues=%d, militia=%d, construction=%d",
                      m_config.enabled, m_config.dialoguesEnabled, m_config.militiaEnabled,
                      m_config.constructionEnabled);
    }

    void ZonePreparationManager::LoadDialogues()
    {
        m_dialogues.clear();

        QueryResult result = WorldDatabase.Query(
            "SELECT id, zone_id, min_phase, max_phase, speaker_type, dialogue, response_dialogue, "
            "response_chance, cooldown_ms, trigger_radius, is_yell, requires_audience, faction, "
            "min_hour, max_hour, location_name, enabled FROM ec_guard_zone_dialogues WHERE enabled = 1");

        if (!result)
        {
            ECG_LOG_INFO("No enabled custom zone dialogues found; native-safe silence retained");
            return;
        }

        do
        {
            Field* fields = result->Fetch();
            NPCDialogue dialogue;

            dialogue.id = fields[0].Get<uint32>();
            dialogue.zoneId = fields[1].Get<uint32>();
            dialogue.minPhase = static_cast<ZonePhase>(fields[2].Get<uint8>());
            dialogue.maxPhase = static_cast<ZonePhase>(fields[3].Get<uint8>());
            dialogue.speakerType = fields[4].Get<std::string>();
            dialogue.dialogue = fields[5].Get<std::string>();
            dialogue.responseDialogue = fields[6].Get<std::string>();
            dialogue.responseChance = fields[7].Get<float>();
            dialogue.cooldownMs = fields[8].Get<uint32>();
            dialogue.triggerRadius = fields[9].Get<float>();
            dialogue.isYell = fields[10].Get<bool>();
            dialogue.requiresAudience = fields[11].Get<bool>();
            dialogue.faction = fields[12].Get<uint8>();
            dialogue.minHour = fields[13].Get<uint8>();
            dialogue.maxHour = fields[14].Get<uint8>();
            dialogue.locationName = fields[15].Get<std::string>();
            dialogue.enabled = fields[16].Get<bool>();

            m_dialogues.push_back(dialogue);

        } while (result->NextRow());

        ECG_LOG_INFO("Loaded %zu zone dialogues from database", m_dialogues.size());
    }

    void ZonePreparationManager::LoadDefaultDialogues()
    {
        uint32 id = 1;

        // UNEASY phase dialogues
        for (const auto& text : DefaultDialogues::GetUneasyGuardDialogues())
        {
            NPCDialogue d;
            d.id = id++;
            d.minPhase = ZonePhase::UNEASY;
            d.maxPhase = ZonePhase::UNEASY;
            d.speakerType = "guard";
            d.dialogue = text;
            d.cooldownMs = 60000;
            m_dialogues.push_back(d);
        }

        for (const auto& text : DefaultDialogues::GetUneasyMerchantDialogues())
        {
            NPCDialogue d;
            d.id = id++;
            d.minPhase = ZonePhase::UNEASY;
            d.maxPhase = ZonePhase::UNEASY;
            d.speakerType = "merchant";
            d.dialogue = text;
            d.cooldownMs = 60000;
            m_dialogues.push_back(d);
        }

        for (const auto& text : DefaultDialogues::GetUneasyCivilianDialogues())
        {
            NPCDialogue d;
            d.id = id++;
            d.minPhase = ZonePhase::UNEASY;
            d.maxPhase = ZonePhase::UNEASY;
            d.speakerType = "civilian";
            d.dialogue = text;
            d.cooldownMs = 60000;
            m_dialogues.push_back(d);
        }

        // MOBILIZING phase dialogues
        for (const auto& text : DefaultDialogues::GetMobilizingGuardDialogues())
        {
            NPCDialogue d;
            d.id = id++;
            d.minPhase = ZonePhase::MOBILIZING;
            d.maxPhase = ZonePhase::MOBILIZING;
            d.speakerType = "guard";
            d.dialogue = text;
            d.cooldownMs = 45000;
            m_dialogues.push_back(d);
        }

        for (const auto& text : DefaultDialogues::GetMobilizingMerchantDialogues())
        {
            NPCDialogue d;
            d.id = id++;
            d.minPhase = ZonePhase::MOBILIZING;
            d.maxPhase = ZonePhase::MOBILIZING;
            d.speakerType = "merchant";
            d.dialogue = text;
            d.cooldownMs = 45000;
            m_dialogues.push_back(d);
        }

        ECG_LOG_INFO("Loaded %zu default dialogues", m_dialogues.size());
    }

    void ZonePreparationManager::LoadDialoguePairs()
    {
        m_dialoguePairs.clear();

        QueryResult result = WorldDatabase.Query(
            "SELECT id, zone_id, min_phase, max_phase, speaker_a_type, speaker_b_type, "
            "exchanges, delay_between_lines_ms, cooldown_ms, max_distance, requires_audience, "
            "faction, location_name, enabled FROM ec_guard_zone_dialogue_pairs WHERE enabled = 1");

        if (!result)
        {
            ECG_LOG_INFO("No enabled custom dialogue pairs found; native-safe silence retained");
            return;
        }

        do
        {
            Field* fields = result->Fetch();
            NPCDialoguePair pair;

            pair.id = fields[0].Get<uint32>();
            pair.zoneId = fields[1].Get<uint32>();
            pair.minPhase = static_cast<ZonePhase>(fields[2].Get<uint8>());
            pair.maxPhase = static_cast<ZonePhase>(fields[3].Get<uint8>());
            pair.speakerAType = fields[4].Get<std::string>();
            pair.speakerBType = fields[5].Get<std::string>();

            // Parse exchanges (pipe-separated)
            std::string exchangeStr = fields[6].Get<std::string>();
            std::stringstream ss(exchangeStr);
            std::string line;
            while (std::getline(ss, line, '|'))
            {
                if (!line.empty())
                    pair.exchanges.push_back(line);
            }

            pair.delayBetweenLinesMs = fields[7].Get<uint32>();
            pair.cooldownMs = fields[8].Get<uint32>();
            pair.maxDistance = fields[9].Get<float>();
            pair.requiresAudience = fields[10].Get<bool>();
            pair.faction = fields[11].Get<uint8>();
            pair.locationName = fields[12].Get<std::string>();
            pair.enabled = fields[13].Get<bool>();

            m_dialoguePairs.push_back(pair);

        } while (result->NextRow());

        ECG_LOG_INFO("Loaded %zu dialogue pairs from database", m_dialoguePairs.size());
    }

    void ZonePreparationManager::LoadDefaultDialoguePairs()
    {
        uint32 id = 1;

        // Guard to guard conversations
        for (const auto& exchanges : DefaultDialogues::GetGuardToGuardConversations())
        {
            NPCDialoguePair pair;
            pair.id = id++;
            pair.minPhase = ZonePhase::UNEASY;
            pair.maxPhase = ZonePhase::MOBILIZING;
            pair.speakerAType = "guard";
            pair.speakerBType = "guard";
            pair.exchanges = exchanges;
            pair.delayBetweenLinesMs = 3000;
            pair.cooldownMs = 120000;
            m_dialoguePairs.push_back(pair);
        }

        // Merchant to merchant
        for (const auto& exchanges : DefaultDialogues::GetMerchantToMerchantConversations())
        {
            NPCDialoguePair pair;
            pair.id = id++;
            pair.minPhase = ZonePhase::UNEASY;
            pair.maxPhase = ZonePhase::MOBILIZING;
            pair.speakerAType = "merchant";
            pair.speakerBType = "merchant";
            pair.exchanges = exchanges;
            pair.delayBetweenLinesMs = 3000;
            pair.cooldownMs = 120000;
            m_dialoguePairs.push_back(pair);
        }

        // Civilian to guard
        for (const auto& exchanges : DefaultDialogues::GetCivilianToGuardConversations())
        {
            NPCDialoguePair pair;
            pair.id = id++;
            pair.minPhase = ZonePhase::UNEASY;
            pair.maxPhase = ZonePhase::MOBILIZING;
            pair.speakerAType = "civilian";
            pair.speakerBType = "guard";
            pair.exchanges = exchanges;
            pair.delayBetweenLinesMs = 3000;
            pair.cooldownMs = 120000;
            m_dialoguePairs.push_back(pair);
        }

        ECG_LOG_INFO("Loaded %zu default dialogue pairs", m_dialoguePairs.size());
    }

    void ZonePreparationManager::LoadMilitiaSpawnPoints()
    {
        m_militiaSpawns.clear();

        QueryResult result = WorldDatabase.Query(
            "SELECT id, zone_id, creature_entry, spawn_x, spawn_y, spawn_z, spawn_o, "
            "waypoint1_x, waypoint1_y, waypoint1_z, waypoint2_x, waypoint2_y, waypoint2_z, "
            "activity, min_phase, spawn_delay_ms, despawn_on_phase, faction, enabled "
            "FROM ec_guard_militia_spawns WHERE enabled = 1");

        if (!result)
        {
            ECG_LOG_INFO("No militia spawn points found in database");
            return;
        }

        do
        {
            Field* fields = result->Fetch();
            MilitiaSpawnPoint spawn;

            spawn.id = fields[0].Get<uint32>();
            spawn.zoneId = fields[1].Get<uint32>();
            spawn.creatureEntry = fields[2].Get<uint32>();
            spawn.spawnPos.Relocate(
                fields[3].Get<float>(),
                fields[4].Get<float>(),
                fields[5].Get<float>(),
                fields[6].Get<float>()
            );
            spawn.waypoint1.Relocate(
                fields[7].Get<float>(),
                fields[8].Get<float>(),
                fields[9].Get<float>()
            );
            spawn.waypoint2.Relocate(
                fields[10].Get<float>(),
                fields[11].Get<float>(),
                fields[12].Get<float>()
            );
            spawn.activity = static_cast<PreparationActivity>(fields[13].Get<uint8>());
            spawn.minPhase = static_cast<ZonePhase>(fields[14].Get<uint8>());
            spawn.spawnDelayMs = fields[15].Get<uint32>();
            spawn.despawnOnPhase = fields[16].Get<uint8>();
            spawn.faction = fields[17].Get<uint8>();
            spawn.enabled = fields[18].Get<bool>();

            m_militiaSpawns.push_back(spawn);

        } while (result->NextRow());

        ECG_LOG_INFO("Loaded %zu militia spawn points from database", m_militiaSpawns.size());
    }

    void ZonePreparationManager::LoadTownCriers()
    {
        m_crierConfigs.clear();

        QueryResult result = WorldDatabase.Query(
            "SELECT id, zone_id, crier_entry, pos_x, pos_y, pos_z, pos_o, "
            "calm_announcements, alert_announcements, urgent_announcements, panic_announcements, "
            "announce_interval_ms, yell_radius, faction, location_name, enabled "
            "FROM ec_guard_town_criers WHERE enabled = 1");

        if (!result)
        {
            ECG_LOG_INFO("No enabled custom town criers found; native-safe silence retained");
            return;
        }

        do
        {
            Field* fields = result->Fetch();
            TownCrierConfig crier;

            crier.id = fields[0].Get<uint32>();
            crier.zoneId = fields[1].Get<uint32>();
            crier.crierEntry = fields[2].Get<uint32>();
            crier.crierPosition.Relocate(
                fields[3].Get<float>(),
                fields[4].Get<float>(),
                fields[5].Get<float>(),
                fields[6].Get<float>()
            );

            // Parse announcements (pipe-separated)
            auto parseAnnouncements = [](const std::string& str) -> std::vector<std::string> {
                std::vector<std::string> result;
                std::stringstream ss(str);
                std::string line;
                while (std::getline(ss, line, '|'))
                {
                    if (!line.empty())
                        result.push_back(line);
                }
                return result;
            };

            crier.calmAnnouncements = parseAnnouncements(fields[7].Get<std::string>());
            crier.alertAnnouncements = parseAnnouncements(fields[8].Get<std::string>());
            crier.urgentAnnouncements = parseAnnouncements(fields[9].Get<std::string>());
            crier.panicAnnouncements = parseAnnouncements(fields[10].Get<std::string>());

            crier.announceIntervalMs = fields[11].Get<uint32>();
            crier.yellRadius = fields[12].Get<float>();
            crier.faction = fields[13].Get<uint8>();
            crier.locationName = fields[14].Get<std::string>();
            crier.enabled = fields[15].Get<bool>();

            // If no announcements loaded, use defaults
            if (crier.calmAnnouncements.empty())
                crier.calmAnnouncements = DefaultDialogues::GetCrierCalmAnnouncements();
            if (crier.alertAnnouncements.empty())
                crier.alertAnnouncements = DefaultDialogues::GetCrierAlertAnnouncements();
            if (crier.urgentAnnouncements.empty())
                crier.urgentAnnouncements = DefaultDialogues::GetCrierUrgentAnnouncements();
            if (crier.panicAnnouncements.empty())
                crier.panicAnnouncements = DefaultDialogues::GetCrierPanicAnnouncements();

            m_crierConfigs.push_back(crier);

        } while (result->NextRow());

        ECG_LOG_INFO("Loaded %zu town crier configs from database", m_crierConfigs.size());
    }

    void ZonePreparationManager::LoadGameObjectTemplates()
    {
        m_gameObjectTemplates.clear();

        QueryResult result = WorldDatabase.Query(
            "SELECT id, zone_id, gameobject_entry, type, spawn_x, spawn_y, spawn_z, spawn_o, "
            "spawn_on_phase, despawn_on_phase, spawn_delay_ms, despawn_delay_ms, respawn_time_ms, "
            "faction, faction_override, enabled "
            "FROM ec_guard_zone_gameobjects WHERE enabled = 1");

        if (!result)
        {
            ECG_LOG_INFO("No zone gameobject templates found in database");
            return;
        }

        do
        {
            Field* fields = result->Fetch();
            PrepGameObjectTemplate tmpl;

            tmpl.id = fields[0].Get<uint32>();
            tmpl.zoneId = fields[1].Get<uint32>();
            tmpl.gameObjectEntry = fields[2].Get<uint32>();
            tmpl.type = static_cast<PrepGameObjectType>(fields[3].Get<uint8>());
            tmpl.spawnPos.Relocate(
                fields[4].Get<float>(),
                fields[5].Get<float>(),
                fields[6].Get<float>(),
                fields[7].Get<float>()
            );
            tmpl.spawnOnPhase = static_cast<ZonePhase>(fields[8].Get<uint8>());
            tmpl.despawnOnPhase = static_cast<ZonePhase>(fields[9].Get<uint8>());
            tmpl.spawnDelayMs = fields[10].Get<uint32>();
            tmpl.despawnDelayMs = fields[11].Get<uint32>();
            tmpl.respawnTimeMs = fields[12].Get<uint32>();
            tmpl.faction = fields[13].Get<uint8>();
            tmpl.factionOverride = fields[14].Get<uint8>();
            tmpl.enabled = fields[15].Get<bool>();

            m_gameObjectTemplates.push_back(tmpl);

        } while (result->NextRow());

        ECG_LOG_INFO("Loaded %zu zone gameobject templates from database", m_gameObjectTemplates.size());
    }

    void ZonePreparationManager::LoadFactionBanners()
    {
        m_bannerConfigs.clear();

        QueryResult result = WorldDatabase.Query(
            "SELECT id, zone_id, pos_x, pos_y, pos_z, pos_o, "
            "alliance_banner_entry, horde_banner_entry, neutral_banner_entry, fallen_banner_entry, "
            "enabled FROM ec_guard_faction_banners WHERE enabled = 1");

        if (!result)
        {
            ECG_LOG_INFO("No faction banner configs found in database");
            return;
        }

        do
        {
            Field* fields = result->Fetch();
            FactionBannerConfig banner;

            banner.id = fields[0].Get<uint32>();
            banner.zoneId = fields[1].Get<uint32>();
            banner.position.Relocate(
                fields[2].Get<float>(),
                fields[3].Get<float>(),
                fields[4].Get<float>(),
                fields[5].Get<float>()
            );
            banner.allianceBannerEntry = fields[6].Get<uint32>();
            banner.hordeBannerEntry = fields[7].Get<uint32>();
            banner.neutralBannerEntry = fields[8].Get<uint32>();
            banner.fallenBannerEntry = fields[9].Get<uint32>();
            banner.enabled = fields[10].Get<bool>();

            m_bannerConfigs.push_back(banner);

        } while (result->NextRow());

        ECG_LOG_INFO("Loaded %zu faction banner configs from database", m_bannerConfigs.size());
    }

    // ==========================================================================
    // UPDATE LOOP
    // ==========================================================================

    void ZonePreparationManager::Update(uint32 diff)
    {
        if (!m_config.enabled)
            return;

        // Process scheduled dialogue lines (always check, even if dialogues disabled)
        ProcessScheduledDialogues(diff);

        // Process pending delayed spawns (spawnDelayMs support)
        ProcessPendingSpawns(diff);

        // Dialogue triggers
        if (m_config.dialoguesEnabled)
        {
            m_dialogueTimer += diff;
            if (m_dialogueTimer >= m_config.dialogueCheckIntervalMs)
            {
                // Check dialogues for all active zones
                for (auto& [zoneId, state] : m_zoneStates)
                {
                    ZoneState* zoneState = sECZoneStateMgr->GetZoneState(zoneId);
                    uint8 incidentFaction = PREP_FACTION_ANY;
                    if (zoneState && zoneState->IsUnderThreat() &&
                        FindLiveIncidentPlayer(zoneId, incidentFaction))
                    {
                        TriggerDialogue(zoneId, zoneState->currentPhase);
                        TriggerDialoguePair(zoneId, zoneState->currentPhase);
                    }
                }
                m_dialogueTimer = 0;
            }
        }

        // Militia behavior updates
        if (m_config.militiaEnabled)
        {
            m_militiaTimer += diff;
            if (m_militiaTimer >= m_config.militiaUpdateIntervalMs)
            {
                UpdateMilitiaBehaviors(m_militiaTimer);
                m_militiaTimer = 0;
            }
        }

        // Town crier announcements
        if (m_config.crierEnabled)
        {
            m_crierTimer += diff;
            if (m_crierTimer >= 5000) // Check every 5 seconds
            {
                for (auto& [zoneId, state] : m_zoneStates)
                {
                    uint8 incidentFaction = PREP_FACTION_ANY;
                    if (!FindLiveIncidentPlayer(zoneId, incidentFaction))
                    {
                        state.crierGuid.Clear();
                        continue;
                    }

                    auto now = std::chrono::steady_clock::now();
                    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                        now - state.lastAnnouncement).count();

                    uint32 interval = m_config.crierBaseIntervalMs;
                    if (state.crierUrgency == CrierUrgency::URGENT)
                        interval = m_config.crierUrgentIntervalMs;
                    else if (state.crierUrgency == CrierUrgency::PANIC)
                        interval = m_config.crierPanicIntervalMs;

                    // A database crier may require a longer interval. Never let
                    // the global urgency setting turn a 45-second crier into a
                    // 10/20-second spam source.
                    for (TownCrierConfig const* crier : GetCriersForZone(zoneId, incidentFaction))
                        interval = std::max(interval, crier->announceIntervalMs);

                    if (elapsed >= interval)
                    {
                        MakeCrierAnnouncement(zoneId);
                    }
                }
                m_crierTimer = 0;
            }
        }

        // GameObject check
        if (m_config.gameObjectsEnabled)
        {
            m_gameObjectTimer += diff;
            if (m_gameObjectTimer >= m_config.gameObjectCheckIntervalMs)
            {
                // Check for spawns/despawns based on phase
                for (auto& [zoneId, state] : m_zoneStates)
                {
                    ZoneState* zoneState = sECZoneStateMgr->GetZoneState(zoneId);
                    if (zoneState)
                    {
                        state.elapsedInPhaseMs += m_config.gameObjectCheckIntervalMs;
                    }
                }
                m_gameObjectTimer = 0;
            }
        }

        // Construction worker behavior updates
        if (m_config.constructionEnabled)
        {
            m_constructionTimer += diff;
            if (m_constructionTimer >= m_config.constructionUpdateIntervalMs)
            {
                for (auto& [zoneId, state] : m_zoneStates)
                {
                    if (!state.workerStates.empty())
                    {
                        UpdateConstructionWorkers(zoneId, m_constructionTimer);
                    }
                }
                m_constructionTimer = 0;
            }

            // Gradual decay of completed constructions (when zone cools down)
            m_decayTimer += diff;
            if (m_decayTimer >= m_config.constructionDecayCheckMs)
            {
                for (auto& [zoneId, state] : m_zoneStates)
                {
                    bool hasDecaying = false;
                    for (const auto& [siteId, ac] : state.activeConstructions)
                    {
                        if (ac.isDecaying)
                        {
                            hasDecaying = true;
                            break;
                        }
                    }
                    if (hasDecaying)
                    {
                        DecayConstructions(zoneId, m_decayTimer);
                    }
                }
                m_decayTimer = 0;
            }
        }
    }

    // ==========================================================================
    // PHASE CHANGE HANDLERS
    // ==========================================================================

    void ZonePreparationManager::OnPhaseChanged(uint32 zoneId, ZonePhase oldPhase, ZonePhase newPhase)
    {
        if (!m_config.enabled)
            return;

        ECG_LOG_INFO("ZonePreparation: Phase changed for zone %u: %s -> %s",
                     zoneId, ZoneStateManager::ZonePhaseToString(oldPhase),
                     ZoneStateManager::ZonePhaseToString(newPhase));

        ZonePreparationState& state = GetOrCreateState(zoneId);
        state.lastProcessedPhase = newPhase;
        state.phaseStartTime = std::chrono::steady_clock::now();
        state.elapsedInPhaseMs = 0;

        // Update crier urgency
        UpdateCrierUrgency(zoneId, newPhase);

        // Handle phase-specific actions
        switch (newPhase)
        {
            case ZonePhase::QUIET:
            {
                // Clean up everything
                DespawnMilitia(zoneId);
                DespawnBarricades(zoneId);
                DespawnConstructionWorkers(zoneId, newPhase);
                DespawnInteractiveObjects(zoneId, newPhase);

                // Start gradual decay on any completed constructions before reset
                StartConstructionDecay(zoneId);

                // Preserve decaying constructions and their GO refs through the reset
                std::map<uint32, ActiveConstruction> decayingSave;
                std::set<ObjectGuid> decayingGOs;
                for (auto& [siteId, ac] : state.activeConstructions)
                {
                    if (ac.isDecaying && ac.structureGuid)
                    {
                        decayingSave[siteId] = ac;
                        decayingGOs.insert(ac.structureGuid);
                    }
                }

                state.Reset();

                // Restore decaying constructions so they continue ticking down
                state.activeConstructions = std::move(decayingSave);
                for (const auto& guid : decayingGOs)
                    state.activeGameObjects.insert(guid);

                break;
            }

            case ZonePhase::UNEASY:
            {
                // Start nervous dialogues, light preparation
                ZoneState* zoneState = sECZoneStateMgr->GetZoneState(zoneId);
                std::string zoneName = zoneState ? zoneState->zoneName : "Unknown";
                sGuardSystemBridge->EmitPreparationPhaseStarted(zoneId, zoneName, static_cast<uint8>(newPhase));

                // Spawn peons for priority construction sites (min_phase = UNEASY)
                SpawnConstructionWorkers(zoneId, newPhase);
                // Spawn always-on interactive objects (spawn_on_phase = 0) and UNEASY ones
                SpawnInteractiveObjects(zoneId, newPhase);
                break;
            }

            case ZonePhase::MOBILIZING:
            {
                // Emit preparation phase started for MOBILIZING (full preparation mode)
                ZoneState* zoneState = sECZoneStateMgr->GetZoneState(zoneId);
                std::string zoneName = zoneState ? zoneState->zoneName : "Unknown";
                sGuardSystemBridge->EmitPreparationPhaseStarted(zoneId, zoneName, static_cast<uint8>(newPhase));

                // Full preparation - spawn militia, barricades
                SpawnMilitia(zoneId, newPhase);
                SpawnBarricades(zoneId);
                SpawnPhaseGameObjects(zoneId, newPhase);

                // Spawn additional peons for remaining construction sites
                SpawnConstructionWorkers(zoneId, newPhase);
                // Spawn MOBILIZING-phase interactive objects
                SpawnInteractiveObjects(zoneId, newPhase);
                break;
            }

            case ZonePhase::IMPACT:
                // Combat phase - spawn debris, fires
                SpawnPhaseGameObjects(zoneId, newPhase);
                // Peons flee during IMPACT
                DespawnConstructionWorkers(zoneId, newPhase);
                // Spawn combat-phase interactive objects (traps, area damage)
                SpawnInteractiveObjects(zoneId, newPhase);
                break;

            case ZonePhase::AFTERMATH:
                // Cleanup phase - despawn barricades, debris fades
                DespawnMilitia(zoneId);
                // Despawn combat-phase interactive objects
                DespawnInteractiveObjects(zoneId, newPhase);
                // Start gradual decay on completed constructions instead of instant despawn
                StartConstructionDecay(zoneId);
                break;
        }

        // Emit event for UI
        std::map<std::string, std::string> data;
        data["oldPhase"] = ZoneStateManager::ZonePhaseToString(oldPhase);
        data["newPhase"] = ZoneStateManager::ZonePhaseToString(newPhase);
        EmitPreparationEvent(zoneId, "phase_changed", data);
    }

    void ZonePreparationManager::OnZoneControlChanged(uint32 zoneId, ControlState oldControl, ControlState newControl)
    {
        if (!m_config.enabled || !m_config.bannersEnabled)
            return;

        ECG_LOG_INFO("ZonePreparation: Control changed for zone %u: %s -> %s",
                     zoneId, ZoneStateManager::ControlStateToString(oldControl),
                     ZoneStateManager::ControlStateToString(newControl));

        // Update banners based on new control state
        ZoneState* zoneState = sECZoneStateMgr->GetZoneState(zoneId);
        if (zoneState)
        {
            UpdateBanners(zoneId, newControl, zoneState->owningFaction);
        }
    }

    void ZonePreparationManager::OnZoneFactionChanged(uint32 zoneId, TeamId oldFaction, TeamId newFaction)
    {
        if (!m_config.enabled || !m_config.bannersEnabled)
            return;

        ECG_LOG_INFO("ZonePreparation: Faction changed for zone %u: %u -> %u",
                     zoneId, static_cast<uint32>(oldFaction), static_cast<uint32>(newFaction));

        // Update banners based on new faction
        ZoneState* zoneState = sECZoneStateMgr->GetZoneState(zoneId);
        if (zoneState)
        {
            UpdateBanners(zoneId, zoneState->controlState, newFaction);
        }
    }

    // ==========================================================================
    // DIALOGUE SYSTEM
    // ==========================================================================

    void ZonePreparationManager::TriggerDialogue(uint32 zoneId, ZonePhase phase)
    {
        uint8 incidentFaction = PREP_FACTION_ANY;
        Player* centerPlayer = FindLiveIncidentPlayer(zoneId, incidentFaction);
        if (!centerPlayer)
            return;

        auto dialogues = GetDialoguesForZone(zoneId, phase, incidentFaction);
        if (dialogues.empty())
            return;

        ZonePreparationState& state = GetOrCreateState(zoneId);

        // Filter dialogues by player's location within zone (for multi-town zones like Barrens)
        std::string playerLocation = GetLocationNameForPosition(zoneId, centerPlayer->GetPosition());
        dialogues.erase(std::remove_if(dialogues.begin(), dialogues.end(),
            [&playerLocation](NPCDialogue* d) {
                // Keep if: dialogue has no location restriction OR matches player's location
                return !d->locationName.empty() && d->locationName != playerLocation;
            }), dialogues.end());

        if (dialogues.empty())
            return;

        // Pick a random dialogue
        NPCDialogue* dialogue = dialogues[urand(0, dialogues.size() - 1)];
        if (!CanTriggerDialogue(*dialogue, state))
            return;

        // Find an appropriate NPC near the player
        uint8 speakerFaction = dialogue->faction == PREP_FACTION_ANY ?
            incidentFaction : dialogue->faction;
        Creature* speaker = FindNPCOfType(zoneId, dialogue->speakerType, centerPlayer,
            speakerFaction, dialogue->triggerRadius);
        if (!speaker || !speaker->IsInWorld() || speaker->IsDuringRemoveFromWorld() ||
            !speaker->IsAlive())
            return;

        // Check cooldown for this specific NPC
        auto cooldownIt = state.dialogueCooldowns.find(speaker->GetGUID());
        if (cooldownIt != state.dialogueCooldowns.end())
        {
            auto now = std::chrono::steady_clock::now();
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                now - cooldownIt->second).count();
            if (elapsed < dialogue->cooldownMs)
                return;
        }

        // Execute the dialogue
        ExecuteDialogue(speaker, *dialogue);

        // Update cooldown
        state.dialogueCooldowns[speaker->GetGUID()] = std::chrono::steady_clock::now();
    }

    void ZonePreparationManager::TriggerDialoguePair(uint32 zoneId, ZonePhase phase)
    {
        uint8 incidentFaction = PREP_FACTION_ANY;
        Player* locationPlayer = FindLiveIncidentPlayer(zoneId, incidentFaction);
        if (!locationPlayer)
            return;

        auto pairs = GetDialoguePairsForZone(zoneId, phase, incidentFaction);
        if (pairs.empty())
            return;

        ZonePreparationState& state = GetOrCreateState(zoneId);

        // Filter dialogue pairs by player's location within zone (for multi-town zones like Barrens)
        std::string playerLocation = GetLocationNameForPosition(zoneId, locationPlayer->GetPosition());
        pairs.erase(std::remove_if(pairs.begin(), pairs.end(),
            [&playerLocation](NPCDialoguePair* p) {
                // Keep if: pair has no location restriction OR matches player's location
                return !p->locationName.empty() && p->locationName != playerLocation;
            }), pairs.end());

        if (pairs.empty())
            return;

        // Pick a random dialogue pair
        NPCDialoguePair* pair = pairs[urand(0, pairs.size() - 1)];
        if (!CanTriggerDialoguePair(*pair, state))
            return;

        // Audience check: we already have a player in zone (locationPlayer found above)
        // so if pair requires audience, it's satisfied

        // Find two appropriate NPCs
        uint8 speakerFaction = pair->faction == PREP_FACTION_ANY ?
            incidentFaction : pair->faction;
        auto npcsA = FindNPCsOfType(zoneId, pair->speakerAType, locationPlayer,
            speakerFaction, pair->maxDistance);
        auto npcsB = FindNPCsOfType(zoneId, pair->speakerBType, locationPlayer,
            speakerFaction, pair->maxDistance);

        if (npcsA.empty() || npcsB.empty())
            return;

        // Find a pair that are close to each other
        Creature* speakerA = nullptr;
        Creature* speakerB = nullptr;
        float minDist = pair->maxDistance;

        for (Creature* npcA : npcsA)
        {
            for (Creature* npcB : npcsB)
            {
                if (npcA == npcB)
                    continue;

                float dist = npcA->GetDistance(npcB);
                if (dist < minDist)
                {
                    speakerA = npcA;
                    speakerB = npcB;
                    minDist = dist;
                }
            }
        }

        if (!speakerA || !speakerB || !speakerA->IsInWorld() || !speakerB->IsInWorld() ||
            speakerA->IsDuringRemoveFromWorld() || speakerB->IsDuringRemoveFromWorld() ||
            !speakerA->IsAlive() || !speakerB->IsAlive())
            return;

        // Execute the dialogue pair
        ExecuteDialoguePair(speakerA, speakerB, *pair);

        // Update cooldown
        state.dialoguePairCooldowns[pair->id] = std::chrono::steady_clock::now();
    }

    void ZonePreparationManager::ExecuteDialogue(Creature* speaker, const NPCDialogue& dialogue)
    {
        if (!speaker || !speaker->IsInWorld() || speaker->IsDuringRemoveFromWorld() ||
            !speaker->IsAlive())
            return;

        if (dialogue.isYell)
            speaker->Yell(dialogue.dialogue, LANG_UNIVERSAL);
        else
            speaker->Say(dialogue.dialogue, LANG_UNIVERSAL);

        ECG_LOG_DEBUG("NPC %s (%u) said: %s",
                      speaker->GetName().c_str(), speaker->GetEntry(), dialogue.dialogue.c_str());

        // Handle response if configured - find a nearby NPC to respond
        if (!dialogue.responseDialogue.empty() && frand(0.0f, 1.0f) < dialogue.responseChance)
        {
            Creature* responder = nullptr;
            float searchRadius = 15.0f;
            uint8 responseFaction = dialogue.faction == PREP_FACTION_ANY ?
                GetCreaturePreparationFaction(speaker) : dialogue.faction;

            // Search for nearby civilian, guard, or merchant who can respond
            Map* map = speaker->GetMap();
            if (map)
            {
                for (auto const& pair : map->GetCreatureBySpawnIdStore())
                {
                    Creature* creature = pair.second;
                    if (!creature || !creature->IsInWorld() ||
                        creature->IsDuringRemoveFromWorld() || !creature->IsAlive() ||
                        creature == speaker)
                        continue;

                    if (creature->GetZoneId() != speaker->GetZoneId())
                        continue;

                    if (creature->GetAreaId() != speaker->GetAreaId() ||
                        !CreatureMatchesPreparationFaction(creature, responseFaction))
                    {
                        continue;
                    }

                    if (creature->GetDistance(speaker) > searchRadius)
                        continue;

                    // Accept civilians, guards, or merchants as responders
                    std::string npcType = NPCTypeDefinition::GetNPCType(creature);
                    if (npcType == "civilian" || npcType == "guard" || npcType == "merchant")
                    {
                        responder = creature;
                        break;
                    }
                }
            }

            if (responder)
            {
                // Schedule the response with a 2-4 second delay
                uint32 responseDelay = urand(2000, 4000);
                ScheduleDialogueLine(speaker->GetZoneId(), responder->GetGUID(),
                                    dialogue.responseDialogue, responseDelay, false, 0);

                ECG_LOG_DEBUG("Scheduled response from %s in %ums: \"%s\"",
                              responder->GetName().c_str(), responseDelay,
                              dialogue.responseDialogue.substr(0, 30).c_str());
            }
        }

        // Emit event
        std::map<std::string, std::string> data;
        data["speakerName"] = speaker->GetName();
        data["speakerEntry"] = std::to_string(speaker->GetEntry());
        data["dialogue"] = dialogue.dialogue;
        data["speakerType"] = dialogue.speakerType;
        EmitPreparationEvent(speaker->GetZoneId(), "dialogue_triggered", data);
    }

    void ZonePreparationManager::ExecuteDialoguePair(Creature* speakerA, Creature* speakerB,
                                                      const NPCDialoguePair& pair)
    {
        if (!speakerA || !speakerB || !speakerA->IsInWorld() || !speakerB->IsInWorld() ||
            speakerA->IsDuringRemoveFromWorld() || speakerB->IsDuringRemoveFromWorld() ||
            !speakerA->IsAlive() || !speakerB->IsAlive() || pair.exchanges.empty())
            return;

        uint32 zoneId = speakerA->GetZoneId();
        ZonePreparationState& state = GetOrCreateState(zoneId);
        uint32 conversationId = state.nextConversationId++;

        ECG_LOG_DEBUG("Starting dialogue pair (conv %u) between %s and %s with %zu lines",
                      conversationId, speakerA->GetName().c_str(), speakerB->GetName().c_str(),
                      pair.exchanges.size());

        // Schedule ALL conversation lines with proper delays
        uint32 delay = 0;
        for (size_t i = 0; i < pair.exchanges.size(); ++i)
        {
            Creature* speaker = (i % 2 == 0) ? speakerA : speakerB;
            const std::string& line = pair.exchanges[i];

            ScheduleDialogueLine(zoneId, speaker->GetGUID(), line, delay, false, conversationId);
            delay += pair.delayBetweenLinesMs;
        }

        // Emit event with full conversation info
        std::map<std::string, std::string> data;
        data["speakerAName"] = speakerA->GetName();
        data["speakerBName"] = speakerB->GetName();
        data["lineCount"] = std::to_string(pair.exchanges.size());
        data["conversationId"] = std::to_string(conversationId);
        data["totalDurationMs"] = std::to_string(delay);
        EmitPreparationEvent(zoneId, "dialogue_pair_started", data);

        // Emit bridge event
        sGuardSystemBridge->EmitDialoguePairStarted(zoneId, speakerA->GetName(),
                                                     speakerB->GetName(), pair.exchanges.size());
    }

    // ==========================================================================
    // SCHEDULED DIALOGUE SYSTEM
    // ==========================================================================

    void ZonePreparationManager::ScheduleDialogueLine(uint32 zoneId, ObjectGuid speakerGuid,
                                                       const std::string& text, uint32 delayMs,
                                                       bool isYell, uint32 conversationId)
    {
        ZonePreparationState& state = GetOrCreateState(zoneId);

        ScheduledDialogueLine scheduled;
        scheduled.speakerGuid = speakerGuid;
        scheduled.text = text;
        scheduled.executeTime = std::chrono::steady_clock::now() +
                                std::chrono::milliseconds(delayMs);
        scheduled.isYell = isYell;
        scheduled.zoneId = zoneId;
        scheduled.conversationId = conversationId;

        state.scheduledDialogues.push_back(scheduled);

        ECG_LOG_TRACE("Scheduled dialogue line for conv %u in zone %u, delay %ums: \"%s\"",
                      conversationId, zoneId, delayMs, text.substr(0, 30).c_str());
    }

    void ZonePreparationManager::ProcessScheduledDialogues(uint32 /*diff*/)
    {
        auto now = std::chrono::steady_clock::now();

        for (auto& [zoneId, state] : m_zoneStates)
        {
            if (state.scheduledDialogues.empty())
                continue;

            // Find and execute any due dialogue lines
            std::vector<ScheduledDialogueLine> remaining;
            remaining.reserve(state.scheduledDialogues.size());

            for (auto& scheduled : state.scheduledDialogues)
            {
                if (now >= scheduled.executeTime)
                {
                    uint8 incidentFaction = PREP_FACTION_ANY;
                    Player* incidentPlayer = FindLiveIncidentPlayer(zoneId, incidentFaction);
                    if (!incidentPlayer)
                    {
                        ECG_LOG_DEBUG("Scheduled dialogue cancelled: no live incident in zone %u",
                            zoneId);
                        continue;
                    }

                    // Resolve the speaker only through the live incident player.
                    // ObjectAccessor therefore cannot cross map or instance.
                    Creature* speaker = ObjectAccessor::GetCreature(
                        *incidentPlayer, scheduled.speakerGuid);

                    if (speaker && speaker->IsInWorld() &&
                        !speaker->IsDuringRemoveFromWorld() && speaker->IsAlive() &&
                        speaker->GetZoneId() == zoneId &&
                        speaker->GetAreaId() == incidentPlayer->GetAreaId() &&
                        CreatureMatchesPreparationFaction(speaker, incidentFaction))
                    {
                        // Execute the dialogue
                        if (scheduled.isYell)
                            speaker->Yell(scheduled.text, LANG_UNIVERSAL);
                        else
                            speaker->Say(scheduled.text, LANG_UNIVERSAL);

                        ECG_LOG_DEBUG("Executed scheduled dialogue (conv %u): %s says \"%s\"",
                                      scheduled.conversationId, speaker->GetName().c_str(),
                                      scheduled.text.c_str());

                        // Emit event for each line
                        sGuardSystemBridge->EmitDialogueTriggered(
                            scheduled.zoneId,
                            speaker->GetName(),
                            scheduled.text,
                            NPCTypeDefinition::GetNPCType(speaker)
                        );
                    }
                    else
                    {
                        ECG_LOG_DEBUG("Scheduled dialogue speaker not found or dead (conv %u), skipping line",
                                      scheduled.conversationId);
                    }
                    // Line processed, don't add to remaining
                }
                else
                {
                    // Not yet time, keep in queue
                    remaining.push_back(scheduled);
                }
            }

            state.scheduledDialogues = std::move(remaining);
        }
    }

    void ZonePreparationManager::CancelConversation(uint32 zoneId, uint32 conversationId)
    {
        ZonePreparationState* state = GetPreparationState(zoneId);
        if (!state)
            return;

        auto& dialogues = state->scheduledDialogues;
        dialogues.erase(
            std::remove_if(dialogues.begin(), dialogues.end(),
                [conversationId](const ScheduledDialogueLine& line) {
                    return line.conversationId == conversationId;
                }),
            dialogues.end()
        );

        ECG_LOG_DEBUG("Cancelled conversation %u in zone %u", conversationId, zoneId);
    }

    // ==========================================================================
    // DELAYED SPAWN SYSTEM (spawnDelayMs support)
    // ==========================================================================

    Player* ZonePreparationManager::FindPlayerInZone(uint32 zoneId)
    {
        WorldSessionMgr::SessionMap const& sessions = sWorldSessionMgr->GetAllSessions();
        for (auto const& itr : sessions)
        {
            if (Player* player = itr.second->GetPlayer())
            {
                if (player->IsInWorld() && player->GetZoneId() == zoneId)
                    return player;
            }
        }
        return nullptr;
    }

    void ZonePreparationManager::ProcessPendingSpawns(uint32 /*diff*/)
    {
        auto now = std::chrono::steady_clock::now();

        for (auto& [zoneId, state] : m_zoneStates)
        {
            if (state.pendingSpawns.empty())
                continue;

            uint8 incidentFaction = PREP_FACTION_ANY;
            Player* incidentPlayer = FindLiveIncidentPlayer(zoneId, incidentFaction);
            ZoneState const* zoneState = sECZoneStateMgr->GetZoneState(zoneId);
            if (!incidentPlayer || incidentFaction == PREP_FACTION_ANY || !zoneState ||
                zoneState->currentPhase == ZonePhase::QUIET ||
                zoneState->currentPhase == ZonePhase::AFTERMATH)
            {
                ECG_LOG_INFO("Cancelled %zu delayed preparation spawns in zone %u: incident no longer active",
                    state.pendingSpawns.size(), zoneId);
                state.pendingSpawns.clear();
                continue;
            }

            std::vector<PendingSpawn> remaining;
            remaining.reserve(state.pendingSpawns.size());

            for (auto& pending : state.pendingSpawns)
            {
                if (now < pending.spawnTime)
                {
                    remaining.push_back(pending);
                    continue;
                }

                // Time to spawn
                Map* map = incidentPlayer->GetMap();
                if (!map)
                {
                    remaining.push_back(pending);
                    continue;
                }

                if (pending.type == PendingSpawn::MILITIA)
                {
                    // Check if militia limit reached
                    if (state.activeMilitia.size() >= m_config.maxMilitiaPerZone)
                    {
                        ECG_LOG_DEBUG("Delayed militia spawn %u skipped - limit reached", pending.entry);
                        continue; // Don't re-queue, just skip
                    }

                    Creature* militia = map->SummonCreature(pending.entry, pending.pos, nullptr, 0);
                    if (militia)
                    {
                        state.activeMilitia.insert(militia->GetGUID());
                        AssignPreparationActivity(militia, pending.activity);
                        ECG_LOG_DEBUG("Delayed spawn: militia %u in zone %u", pending.entry, zoneId);
                    }
                    else
                    {
                        ECG_LOG_WARN("Failed to spawn delayed militia %u in zone %u", pending.entry, zoneId);
                    }
                }
                else if (pending.type == PendingSpawn::GAMEOBJECT)
                {
                    GameObject* go = incidentPlayer->SummonGameObject(
                        pending.entry,
                        pending.pos.GetPositionX(),
                        pending.pos.GetPositionY(),
                        pending.pos.GetPositionZ(),
                        pending.pos.GetOrientation(),
                        0.0f, 0.0f, 0.0f, 1.0f, // identity rotation quaternion
                        0 // respawn time - 0 for preparation objects
                    );
                    if (go)
                    {
                        state.activeGameObjects.insert(go->GetGUID());
                        ECG_LOG_DEBUG("Delayed spawn: GO %u in zone %u", pending.entry, zoneId);
                    }
                    else
                    {
                        ECG_LOG_WARN("Failed to spawn delayed GO %u in zone %u", pending.entry, zoneId);
                    }
                }
            }

            state.pendingSpawns = std::move(remaining);
        }
    }

    // ==========================================================================
    // MILITIA SYSTEM
    // ==========================================================================

    void ZonePreparationManager::SpawnMilitia(uint32 zoneId, ZonePhase phase)
    {
        if (!m_config.militiaEnabled)
            return;

        auto spawns = GetMilitiaForZone(zoneId, phase);
        if (spawns.empty())
            return;

        ZonePreparationState& state = GetOrCreateState(zoneId);

        // Check limit
        if (state.activeMilitia.size() >= m_config.maxMilitiaPerZone)
            return;

        // Find a player in the zone to get the map
        Player* playerInZone = nullptr;
        WorldSessionMgr::SessionMap const& sessions = sWorldSessionMgr->GetAllSessions();
        for (auto const& itr : sessions)
        {
            if (Player* player = itr.second->GetPlayer())
            {
                if (player->IsInWorld() && player->GetZoneId() == zoneId)
                {
                    playerInZone = player;
                    break;
                }
            }
        }

        if (!playerInZone)
            return;

        Map* map = playerInZone->GetMap();
        if (!map)
            return;

        for (MilitiaSpawnPoint* spawn : spawns)
        {
            if (state.activeMilitia.size() >= m_config.maxMilitiaPerZone)
                break;

            // Check if this spawn point's creature is already active
            bool alreadySpawned = false;
            for (const auto& guid : state.activeMilitia)
            {
                Creature* existing = ObjectAccessor::GetCreature(*playerInZone, guid);
                if (existing && existing->GetEntry() == spawn->creatureEntry)
                {
                    alreadySpawned = true;
                    break;
                }
            }

            if (alreadySpawned)
                continue;

            // Check for spawn delay - queue for later if spawnDelayMs > 0
            if (spawn->spawnDelayMs > 0)
            {
                PendingSpawn pending;
                pending.type = PendingSpawn::MILITIA;
                pending.entry = spawn->creatureEntry;
                pending.pos = spawn->spawnPos;
                pending.activity = spawn->activity;
                pending.spawnTime = std::chrono::steady_clock::now() +
                                    std::chrono::milliseconds(spawn->spawnDelayMs);
                state.pendingSpawns.push_back(pending);

                ECG_LOG_DEBUG("Queued delayed militia spawn %u in zone %u, delay %ums",
                              spawn->creatureEntry, zoneId, spawn->spawnDelayMs);
                continue;
            }

            // Spawn the militia NPC immediately
            Creature* militia = map->SummonCreature(
                spawn->creatureEntry,
                spawn->spawnPos,
                nullptr,
                0
            );

            if (militia)
            {
                state.activeMilitia.insert(militia->GetGUID());
                AssignPreparationActivity(militia, spawn->activity);

                ECG_LOG_DEBUG("Spawned militia %u in zone %u with activity %s",
                              spawn->creatureEntry, zoneId, ActivityToString(spawn->activity));
            }
        }

        ECG_LOG_INFO("Spawned %zu militia NPCs in zone %u (+ %zu pending)",
                     state.activeMilitia.size(), state.pendingSpawns.size(), zoneId);

        // Emit to bridge for UI updates
        if (!state.activeMilitia.empty())
        {
            sGuardSystemBridge->EmitMilitiaSpawned(zoneId, static_cast<uint32>(state.activeMilitia.size()), "preparation");
        }
    }

    void ZonePreparationManager::DespawnMilitia(uint32 zoneId)
    {
        ZonePreparationState* state = GetPreparationState(zoneId);
        if (!state)
            return;

        for (const auto& guid : state->activeMilitia)
        {
            // Find and despawn the creature
            // We need a reference object to use ObjectAccessor
            WorldSessionMgr::SessionMap const& sessions = sWorldSessionMgr->GetAllSessions();
            for (auto const& itr : sessions)
            {
                if (Player* player = itr.second->GetPlayer())
                {
                    if (Creature* militia = ObjectAccessor::GetCreature(*player, guid))
                    {
                        militia->DespawnOrUnsummon();
                        break;
                    }
                }
            }
        }

        uint32 despawnedCount = static_cast<uint32>(state->activeMilitia.size());

        ECG_LOG_INFO("Despawned %zu militia NPCs from zone %u",
                     state->activeMilitia.size(), zoneId);

        // Emit to bridge for UI updates
        if (despawnedCount > 0)
        {
            sGuardSystemBridge->EmitMilitiaDespawned(zoneId, despawnedCount);
        }

        state->activeMilitia.clear();
        state->npcActivities.clear();
    }

    void ZonePreparationManager::AssignPreparationActivity(Creature* npc, PreparationActivity activity)
    {
        if (!npc || !npc->IsInWorld() || npc->IsDuringRemoveFromWorld() || !npc->IsAlive())
            return;

        uint32 zoneId = npc->GetZoneId();
        ZonePreparationState& state = GetOrCreateState(zoneId);
        state.npcActivities[npc->GetGUID()] = activity;

        // Set up behavior based on activity
        switch (activity)
        {
            case PreparationActivity::CARRYING_CRATE:
                // Set walk speed, maybe an emote
                npc->SetSpeed(MOVE_WALK, m_config.militiaWalkSpeed);
                npc->HandleEmoteCommand(EMOTE_STATE_USE_STANDING);
                break;

            case PreparationActivity::RUNNING_ERRANDS:
                npc->SetSpeed(MOVE_RUN, m_config.militiaRunSpeed);
                break;

            case PreparationActivity::PATROLLING_FAST:
                npc->SetSpeed(MOVE_WALK, m_config.militiaRunSpeed);
                break;

            case PreparationActivity::GATHERING:
                npc->SetStandState(UNIT_STAND_STATE_STAND);
                break;

            case PreparationActivity::WATCHING_ROAD:
                // Face a specific direction
                break;

            case PreparationActivity::PRAYING:
                npc->SetStandState(UNIT_STAND_STATE_KNEEL);
                break;

            default:
                break;
        }

        ECG_LOG_DEBUG("Assigned activity %s to NPC %s (%u)",
                      ActivityToString(activity), npc->GetName().c_str(), npc->GetEntry());
    }

    void ZonePreparationManager::UpdateMilitiaBehaviors(uint32 diff)
    {
        for (auto& [zoneId, state] : m_zoneStates)
        {
            // Skip if no active militia
            if (state.activeMilitia.empty())
                continue;

            // Process each militia NPC
            std::vector<ObjectGuid> toRemove;
            for (const auto& guid : state.activeMilitia)
            {
                Creature* militia = FindCreatureByGuid(guid);
                if (!militia || !militia->IsInWorld() || militia->IsDuringRemoveFromWorld() ||
                    !militia->IsAlive())
                {
                    toRemove.push_back(guid);
                    continue;
                }

                auto actIt = state.npcActivities.find(guid);
                if (actIt == state.npcActivities.end())
                    continue;

                PreparationActivity activity = actIt->second;

                switch (activity)
                {
                    case PreparationActivity::CARRYING_CRATE:
                    {
                        // Check if reached destination
                        if (!militia->isMoving())
                        {
                            // Turn around and go back (toggle direction)
                            auto destIt = state.npcDestinations.find(guid);
                            auto originIt = state.npcOrigins.find(guid);
                            if (destIt != state.npcDestinations.end() && originIt != state.npcOrigins.end())
                            {
                                // Swap origin and destination
                                Position temp = destIt->second;
                                state.npcDestinations[guid] = originIt->second;
                                state.npcOrigins[guid] = temp;
                                MakeNPCCarryCrate(militia, state.npcDestinations[guid]);
                            }
                        }
                        break;
                    }
                    case PreparationActivity::PATROLLING_FAST:
                    {
                        // Continue patrol if stopped
                        if (!militia->isMoving())
                        {
                            auto destIt = state.npcDestinations.find(guid);
                            if (destIt != state.npcDestinations.end())
                            {
                                MakeNPCRun(militia, destIt->second);
                            }
                        }
                        break;
                    }
                    case PreparationActivity::GATHERING:
                    {
                        // Stay at gather point, maybe look around periodically
                        if (!militia->isMoving() && urand(0, 100) < 5)
                        {
                            float randomAngle = frand(0.0f, 2.0f * float(M_PI));
                            militia->SetFacingTo(randomAngle);
                        }
                        break;
                    }
                    case PreparationActivity::WATCHING_ROAD:
                    {
                        // Periodically point/gesture toward threat direction
                        if (urand(0, 100) < 2)
                        {
                            militia->HandleEmoteCommand(EMOTE_ONESHOT_POINT);
                        }
                        break;
                    }
                    case PreparationActivity::BOARDING_UP:
                    {
                        // Continue boarding animation
                        if (urand(0, 100) < 3)
                        {
                            militia->HandleEmoteCommand(EMOTE_STATE_WORK_CHOPWOOD);
                        }
                        break;
                    }
                    case PreparationActivity::PRAYING:
                    {
                        // Maintain prayer stance
                        if (!militia->HasUnitState(UNIT_STATE_STUNNED))
                        {
                            militia->HandleEmoteCommand(EMOTE_STATE_KNEEL);
                        }
                        break;
                    }
                    default:
                        break;
                }
            }

            // Clean up dead/despawned militia
            for (const auto& guid : toRemove)
            {
                state.activeMilitia.erase(guid);
                state.npcActivities.erase(guid);
                state.npcDestinations.erase(guid);
                state.npcOrigins.erase(guid);
            }
        }
    }

    Creature* ZonePreparationManager::FindCreatureByGuid(ObjectGuid guid)
    {
        WorldSessionMgr::SessionMap const& sessions = sWorldSessionMgr->GetAllSessions();
        for (auto const& itr : sessions)
        {
            if (Player* player = itr.second->GetPlayer())
            {
                if (player->IsInWorld())
                {
                    if (Creature* creature = ObjectAccessor::GetCreature(*player, guid))
                        return creature;
                }
            }
        }
        return nullptr;
    }

    void ZonePreparationManager::MakeNPCCarryCrate(Creature* npc, Position const& destination)
    {
        if (!npc || !npc->IsInWorld() || npc->IsDuringRemoveFromWorld() || !npc->IsAlive())
            return;

        npc->SetSpeed(MOVE_WALK, m_config.militiaWalkSpeed);
        npc->HandleEmoteCommand(EMOTE_STATE_USE_STANDING);
        npc->GetMotionMaster()->MovePoint(0, destination);

        ECG_LOG_DEBUG("NPC %s now carrying crate to (%.1f, %.1f, %.1f)",
                      npc->GetName().c_str(), destination.GetPositionX(),
                      destination.GetPositionY(), destination.GetPositionZ());
    }

    void ZonePreparationManager::MakeNPCRun(Creature* npc, Position const& destination)
    {
        if (!npc || !npc->IsInWorld() || npc->IsDuringRemoveFromWorld() || !npc->IsAlive())
            return;

        npc->SetSpeed(MOVE_RUN, m_config.militiaRunSpeed);
        npc->GetMotionMaster()->MovePoint(0, destination);
    }

    void ZonePreparationManager::MakeNPCGather(Creature* npc, Position const& gatherPoint)
    {
        if (!npc || !npc->IsInWorld() || npc->IsDuringRemoveFromWorld() || !npc->IsAlive())
            return;

        npc->GetMotionMaster()->MovePoint(0, gatherPoint);
    }

    void ZonePreparationManager::MakeNPCWatch(Creature* npc, float angle)
    {
        if (!npc || !npc->IsInWorld() || npc->IsDuringRemoveFromWorld() || !npc->IsAlive())
            return;

        npc->SetFacingTo(angle);
        npc->HandleEmoteCommand(EMOTE_ONESHOT_POINT);
    }

    // ==========================================================================
    // TOWN CRIER SYSTEM
    // ==========================================================================

    void ZonePreparationManager::UpdateCrierUrgency(uint32 zoneId, ZonePhase phase)
    {
        ZonePreparationState& state = GetOrCreateState(zoneId);

        switch (phase)
        {
            case ZonePhase::QUIET:
                state.crierUrgency = CrierUrgency::CALM;
                break;
            case ZonePhase::UNEASY:
                state.crierUrgency = CrierUrgency::ALERT;
                break;
            case ZonePhase::MOBILIZING:
                state.crierUrgency = CrierUrgency::URGENT;
                break;
            case ZonePhase::IMPACT:
                state.crierUrgency = CrierUrgency::PANIC;
                break;
            case ZonePhase::AFTERMATH:
                state.crierUrgency = CrierUrgency::CALM;
                break;
        }

        ECG_LOG_DEBUG("Crier urgency for zone %u set to %s",
                      zoneId, CrierUrgencyToString(state.crierUrgency));
    }

    void ZonePreparationManager::MakeCrierAnnouncement(uint32 zoneId)
    {
        ZonePreparationState& state = GetOrCreateState(zoneId);

        // Synthetic warnings are allowed only around a current, validated
        // criminal incident. Persisted heat by itself is not an incoming army.
        uint8 incidentFaction = PREP_FACTION_ANY;
        Player* incidentPlayer = FindLiveIncidentPlayer(zoneId, incidentFaction);
        if (!incidentPlayer)
        {
            state.crierGuid.Clear();
            return;
        }

        // Find matching crier for this location using multi-crier lookup
        std::string locationName = GetLocationNameForPosition(
            zoneId, incidentPlayer->GetPosition());
        TownCrierConfig* crierConfig = nullptr;
        std::vector<TownCrierConfig*> criers = GetCriersForZone(zoneId, incidentFaction);

        // First try to find crier matching current location
        for (auto* crier : criers)
        {
            if (crier->locationName == locationName ||
                (crier->locationName.empty() && locationName.empty()))
            {
                crierConfig = crier;
                break;
            }
        }

        // Fall back to any crier with empty locationName (zone-wide) or first available
        if (!crierConfig)
        {
            for (auto* crier : criers)
            {
                if (crier->locationName.empty())
                {
                    crierConfig = crier;
                    break;
                }
            }
        }
        if (!crierConfig)
            return;

        // Get appropriate announcements based on urgency
        const std::vector<std::string>* announcements = nullptr;
        switch (state.crierUrgency)
        {
            case CrierUrgency::CALM:
                announcements = &crierConfig->calmAnnouncements;
                break;
            case CrierUrgency::ALERT:
                announcements = &crierConfig->alertAnnouncements;
                break;
            case CrierUrgency::URGENT:
                announcements = &crierConfig->urgentAnnouncements;
                break;
            case CrierUrgency::PANIC:
                announcements = &crierConfig->panicAnnouncements;
                break;
        }

        if (!announcements || announcements->empty())
            return;

        // Pick random announcement
        const std::string& announcement = (*announcements)[urand(0, announcements->size() - 1)];

        // Resolve only the configured humanoid crier. Never fall back to an
        // arbitrary civilian (which previously included critters and fish).
        Creature* crier = nullptr;
        if (!state.crierGuid.IsEmpty())
        {
            crier = ObjectAccessor::GetCreature(*incidentPlayer, state.crierGuid);
            if (!crier || !crier->IsInWorld() || crier->IsDuringRemoveFromWorld() ||
                !crier->IsAlive() ||
                crier->GetEntry() != crierConfig->crierEntry ||
                crier->GetZoneId() != zoneId ||
                crier->GetAreaId() != incidentPlayer->GetAreaId() ||
                crier->GetCreatureType() != CREATURE_TYPE_HUMANOID ||
                !CreatureMatchesPreparationFaction(crier, incidentFaction))
            {
                crier = nullptr;
                state.crierGuid.Clear();
            }
        }

        if (!crier)
            crier = FindConfiguredCrier(*crierConfig, incidentPlayer, incidentFaction);

        if (crier && crier->IsInWorld() && !crier->IsDuringRemoveFromWorld() &&
            crier->IsAlive())
        {
            crier->Yell(announcement, LANG_UNIVERSAL);
            state.crierGuid = crier->GetGUID();

            ECG_LOG_DEBUG("Town crier in zone %u announced: %s",
                          zoneId, announcement.c_str());
        }
        else
        {
            ECG_LOG_WARN("Configured crier entry %u unavailable in zone %u area %u; announcement suppressed",
                crierConfig->crierEntry, zoneId, incidentPlayer->GetAreaId());
            return;
        }

        state.lastAnnouncement = std::chrono::steady_clock::now();

        // Emit event
        std::map<std::string, std::string> data;
        data["urgency"] = CrierUrgencyToString(state.crierUrgency);
        data["announcement"] = announcement;
        EmitPreparationEvent(zoneId, "crier_announcement", data);

        // Emit to realtime bridge for UI updates
        sGuardSystemBridge->EmitCrierAnnouncement(zoneId, CrierUrgencyToString(state.crierUrgency), announcement);
    }

    // ==========================================================================
    // GAMEOBJECT MANAGEMENT
    // ==========================================================================

    void ZonePreparationManager::SpawnPhaseGameObjects(uint32 zoneId, ZonePhase phase)
    {
        if (!m_config.gameObjectsEnabled)
            return;

        auto templates = GetGameObjectsForZone(zoneId, phase);
        if (templates.empty())
            return;

        ZonePreparationState& state = GetOrCreateState(zoneId);

        // Find a player in zone to get map
        Player* playerInZone = nullptr;
        WorldSessionMgr::SessionMap const& sessions = sWorldSessionMgr->GetAllSessions();
        for (auto const& itr : sessions)
        {
            if (Player* player = itr.second->GetPlayer())
            {
                if (player->IsInWorld() && player->GetZoneId() == zoneId)
                {
                    playerInZone = player;
                    break;
                }
            }
        }

        if (!playerInZone)
            return;

        Map* map = playerInZone->GetMap();
        if (!map)
            return;

        for (PrepGameObjectTemplate* tmpl : templates)
        {
            if (tmpl->spawnOnPhase != phase)
                continue;

            // Check faction override - only spawn if zone is controlled by this faction
            if (tmpl->factionOverride != 0)
            {
                ZoneState* zoneState = sECZoneStateMgr->GetZoneState(zoneId);
                if (zoneState)
                {
                    // factionOverride: 1=Alliance, 2=Horde
                    // controllingFaction: TEAM_ALLIANCE=0, TEAM_HORDE=1
                    TeamId controlling = zoneState->owningFaction;
                    uint8 factionValue = (controlling == TEAM_ALLIANCE) ? 1 : (controlling == TEAM_HORDE) ? 2 : 0;

                    if (tmpl->factionOverride != factionValue)
                    {
                        ECG_LOG_TRACE("Skipping GO %u - requires faction %u, zone controlled by %u",
                                      tmpl->gameObjectEntry, tmpl->factionOverride, factionValue);
                        continue;
                    }
                }
            }

            // Check for spawn delay - queue for later if spawnDelayMs > 0
            if (tmpl->spawnDelayMs > 0)
            {
                PendingSpawn pending;
                pending.type = PendingSpawn::GAMEOBJECT;
                pending.entry = tmpl->gameObjectEntry;
                pending.pos = tmpl->spawnPos;
                pending.spawnTime = std::chrono::steady_clock::now() +
                                    std::chrono::milliseconds(tmpl->spawnDelayMs);
                state.pendingSpawns.push_back(pending);

                ECG_LOG_DEBUG("Queued delayed GO spawn %u in zone %u, delay %ums",
                              tmpl->gameObjectEntry, zoneId, tmpl->spawnDelayMs);
                continue;
            }

            // Spawn the gameobject immediately
            GameObject* go = playerInZone->SummonGameObject(
                tmpl->gameObjectEntry,
                tmpl->spawnPos.GetPositionX(),
                tmpl->spawnPos.GetPositionY(),
                tmpl->spawnPos.GetPositionZ(),
                tmpl->spawnPos.GetOrientation(),
                0.0f, 0.0f, 0.0f, 1.0f, // identity rotation quaternion
                tmpl->respawnTimeMs > 0 ? tmpl->respawnTimeMs / 1000 : 0
            );

            if (go)
            {
                state.activeGameObjects.insert(go->GetGUID());

                ECG_LOG_DEBUG("Spawned gameobject %u at (%.1f, %.1f, %.1f) in zone %u",
                              tmpl->gameObjectEntry, tmpl->spawnPos.GetPositionX(),
                              tmpl->spawnPos.GetPositionY(), tmpl->spawnPos.GetPositionZ(), zoneId);
            }
        }

        ECG_LOG_INFO("Spawned %zu gameobjects for phase %s in zone %u (+ %zu pending)",
                     state.activeGameObjects.size(), ZoneStateManager::ZonePhaseToString(phase),
                     zoneId, state.pendingSpawns.size());
    }

    void ZonePreparationManager::DespawnPhaseGameObjects(uint32 zoneId, ZonePhase phase)
    {
        ZonePreparationState* state = GetPreparationState(zoneId);
        if (!state)
            return;

        std::set<ObjectGuid> toRemove;

        for (const auto& guid : state->activeGameObjects)
        {
            // Find and check if this GO should despawn
            WorldSessionMgr::SessionMap const& sessions = sWorldSessionMgr->GetAllSessions();
            for (auto const& itr : sessions)
            {
                if (Player* player = itr.second->GetPlayer())
                {
                    if (GameObject* go = ObjectAccessor::GetGameObject(*player, guid))
                    {
                        // Check if this GO's template says to despawn on this phase
                        for (const auto& tmpl : m_gameObjectTemplates)
                        {
                            if (tmpl.gameObjectEntry == go->GetEntry() &&
                                tmpl.despawnOnPhase == phase)
                            {
                                go->Delete();
                                toRemove.insert(guid);
                                break;
                            }
                        }
                        break;
                    }
                }
            }
        }

        for (const auto& guid : toRemove)
        {
            state->activeGameObjects.erase(guid);
        }

        if (!toRemove.empty())
        {
            ECG_LOG_INFO("Despawned %zu gameobjects for phase %s in zone %u",
                         toRemove.size(), ZoneStateManager::ZonePhaseToString(phase), zoneId);
        }
    }

    void ZonePreparationManager::SpawnBarricades(uint32 zoneId)
    {
        ZonePreparationState* state = GetPreparationState(zoneId);
        uint32 countBefore = state ? static_cast<uint32>(state->activeGameObjects.size()) : 0;

        SpawnPhaseGameObjects(zoneId, ZonePhase::MOBILIZING);

        // Emit to bridge for UI updates
        if (state)
        {
            uint32 spawned = static_cast<uint32>(state->activeGameObjects.size()) - countBefore;
            if (spawned > 0)
            {
                sGuardSystemBridge->EmitBarricadesSpawned(zoneId, spawned);
            }
        }
    }

    void ZonePreparationManager::DespawnBarricades(uint32 zoneId)
    {
        ZonePreparationState* state = GetPreparationState(zoneId);
        uint32 countBefore = state ? static_cast<uint32>(state->activeGameObjects.size()) : 0;

        DespawnPhaseGameObjects(zoneId, ZonePhase::QUIET);

        // Emit to bridge for UI updates
        if (state)
        {
            uint32 despawned = countBefore - static_cast<uint32>(state->activeGameObjects.size());
            if (despawned > 0)
            {
                sGuardSystemBridge->EmitBarricadesDespawned(zoneId, despawned);
            }
        }
    }

    void ZonePreparationManager::UpdateBanners(uint32 zoneId, ControlState control, TeamId faction)
    {
        if (!m_config.bannersEnabled)
            return;

        auto banners = GetBannersForZone(zoneId);
        if (banners.empty())
            return;

        ZonePreparationState& state = GetOrCreateState(zoneId);

        // Find a player in zone
        Player* playerInZone = nullptr;
        WorldSessionMgr::SessionMap const& sessions = sWorldSessionMgr->GetAllSessions();
        for (auto const& itr : sessions)
        {
            if (Player* player = itr.second->GetPlayer())
            {
                if (player->IsInWorld() && player->GetZoneId() == zoneId)
                {
                    playerInZone = player;
                    break;
                }
            }
        }

        if (!playerInZone)
            return;

        for (FactionBannerConfig* banner : banners)
        {
            // Determine which banner entry to use
            uint32 newBannerEntry = banner->neutralBannerEntry;

            if (control == ControlState::FALLEN)
            {
                newBannerEntry = banner->fallenBannerEntry;
            }
            else if (faction == TEAM_ALLIANCE)
            {
                newBannerEntry = banner->allianceBannerEntry;
            }
            else if (faction == TEAM_HORDE)
            {
                newBannerEntry = banner->hordeBannerEntry;
            }

            if (newBannerEntry == 0)
                continue;

            // Remove old banner if exists
            auto it = state.activeBanners.find(banner->id);
            if (it != state.activeBanners.end())
            {
                if (GameObject* oldBanner = ObjectAccessor::GetGameObject(*playerInZone, it->second))
                {
                    oldBanner->Delete();
                }
            }

            // Spawn new banner
            GameObject* newBanner = playerInZone->SummonGameObject(
                newBannerEntry,
                banner->position.GetPositionX(),
                banner->position.GetPositionY(),
                banner->position.GetPositionZ(),
                banner->position.GetOrientation(),
                0.0f, 0.0f, 0.0f, 1.0f, // identity rotation quaternion
                0 // Permanent
            );

            if (newBanner)
            {
                state.activeBanners[banner->id] = newBanner->GetGUID();

                // Determine old faction name for bridge event
                std::string oldFactionName = "neutral";
                std::string newFactionName = "neutral";

                if (control == ControlState::FALLEN)
                {
                    newFactionName = "fallen";
                }
                else if (faction == TEAM_ALLIANCE)
                {
                    newFactionName = "alliance";
                }
                else if (faction == TEAM_HORDE)
                {
                    newFactionName = "horde";
                }

                // Emit to bridge for UI updates
                sGuardSystemBridge->EmitBannerChanged(zoneId, oldFactionName, newFactionName);

                ECG_LOG_DEBUG("Updated banner %u in zone %u to entry %u (control: %s, faction: %u)",
                              banner->id, zoneId, newBannerEntry,
                              ZoneStateManager::ControlStateToString(control),
                              static_cast<uint32>(faction));
            }
        }
    }

    // ==========================================================================
    // HELPER METHODS
    // ==========================================================================

    ZonePreparationState& ZonePreparationManager::GetOrCreateState(uint32 zoneId)
    {
        auto it = m_zoneStates.find(zoneId);
        if (it != m_zoneStates.end())
            return it->second;

        ZonePreparationState& state = m_zoneStates[zoneId];
        state.zoneId = zoneId;
        return state;
    }

    ZonePreparationState* ZonePreparationManager::GetPreparationState(uint32 zoneId)
    {
        auto it = m_zoneStates.find(zoneId);
        if (it != m_zoneStates.end())
            return &it->second;
        return nullptr;
    }

    std::vector<NPCDialogue*> ZonePreparationManager::GetDialoguesForZone(
        uint32 zoneId, ZonePhase phase, uint8 dbFaction)
    {
        std::vector<NPCDialogue*> result;
        for (auto& dialogue : m_dialogues)
        {
            if (dialogue.zoneId != 0 && dialogue.zoneId != zoneId)
                continue;
            if (phase < dialogue.minPhase || phase > dialogue.maxPhase)
                continue;
            if (dialogue.faction != PREP_FACTION_ANY && dialogue.faction != dbFaction)
                continue;
            if (!dialogue.enabled)
                continue;
            result.push_back(&dialogue);
        }
        return result;
    }

    std::vector<NPCDialoguePair*> ZonePreparationManager::GetDialoguePairsForZone(
        uint32 zoneId, ZonePhase phase, uint8 dbFaction)
    {
        std::vector<NPCDialoguePair*> result;
        for (auto& pair : m_dialoguePairs)
        {
            if (pair.zoneId != 0 && pair.zoneId != zoneId)
                continue;
            if (phase < pair.minPhase || phase > pair.maxPhase)
                continue;
            if (pair.faction != PREP_FACTION_ANY && pair.faction != dbFaction)
                continue;
            if (!pair.enabled)
                continue;
            result.push_back(&pair);
        }
        return result;
    }

    TownCrierConfig* ZonePreparationManager::GetCrierForZone(uint32 zoneId)
    {
        ZoneState const* zoneState = sECZoneStateMgr->GetZoneState(zoneId);
        uint8 dbFaction = zoneState ?
            TeamIdToPreparationFaction(zoneState->owningFaction) : PREP_FACTION_ANY;
        for (auto& crier : m_crierConfigs)
        {
            if (crier.zoneId == zoneId && crier.enabled &&
                (crier.faction == PREP_FACTION_ANY || crier.faction == dbFaction))
                return &crier;
        }
        return nullptr;
    }

    std::vector<TownCrierConfig*> ZonePreparationManager::GetCriersForZone(
        uint32 zoneId, uint8 dbFaction)
    {
        std::vector<TownCrierConfig*> result;
        for (auto& crier : m_crierConfigs)
        {
            if (crier.zoneId == zoneId && crier.enabled &&
                (crier.faction == PREP_FACTION_ANY || crier.faction == dbFaction))
                result.push_back(&crier);
        }
        return result;
    }

    std::vector<MilitiaSpawnPoint*> ZonePreparationManager::GetMilitiaForZone(uint32 zoneId, ZonePhase phase)
    {
        std::vector<MilitiaSpawnPoint*> result;
        ZoneState const* zoneState = sECZoneStateMgr->GetZoneState(zoneId);
        uint8 dbFaction = zoneState ?
            TeamIdToPreparationFaction(zoneState->owningFaction) : PREP_FACTION_ANY;
        for (auto& spawn : m_militiaSpawns)
        {
            if (spawn.zoneId != zoneId)
                continue;
            if (phase < spawn.minPhase)
                continue;
            if (spawn.faction != PREP_FACTION_ANY && spawn.faction != dbFaction)
                continue;
            if (!spawn.enabled)
                continue;
            result.push_back(&spawn);
        }
        return result;
    }

    std::vector<PrepGameObjectTemplate*> ZonePreparationManager::GetGameObjectsForZone(uint32 zoneId, ZonePhase phase)
    {
        std::vector<PrepGameObjectTemplate*> result;
        ZoneState const* zoneState = sECZoneStateMgr->GetZoneState(zoneId);
        uint8 dbFaction = zoneState ?
            TeamIdToPreparationFaction(zoneState->owningFaction) : PREP_FACTION_ANY;
        for (auto& tmpl : m_gameObjectTemplates)
        {
            if (tmpl.zoneId != zoneId)
                continue;
            if (tmpl.spawnOnPhase != phase)
                continue;
            if (tmpl.faction != PREP_FACTION_ANY && tmpl.faction != dbFaction)
                continue;
            if (!tmpl.enabled)
                continue;
            result.push_back(&tmpl);
        }
        return result;
    }

    std::vector<FactionBannerConfig*> ZonePreparationManager::GetBannersForZone(uint32 zoneId)
    {
        std::vector<FactionBannerConfig*> result;
        for (auto& banner : m_bannerConfigs)
        {
            if (banner.zoneId == zoneId && banner.enabled)
                result.push_back(&banner);
        }
        return result;
    }

    Creature* ZonePreparationManager::FindNPCOfType(uint32 zoneId, const std::string& type,
                                                     Player* centerPlayer, uint8 dbFaction,
                                                     float radius)
    {
        auto npcs = FindNPCsOfType(zoneId, type, centerPlayer, dbFaction, radius);
        if (npcs.empty())
            return nullptr;
        return npcs[urand(0, npcs.size() - 1)];
    }

    std::vector<Creature*> ZonePreparationManager::FindNPCsOfType(
        uint32 zoneId, const std::string& type, Player* centerPlayer,
        uint8 dbFaction, float radius)
    {
        std::vector<Creature*> result;
        if (!centerPlayer || !centerPlayer->IsInWorld() ||
            centerPlayer->GetZoneId() != zoneId)
        {
            return result;
        }

        Map* map = centerPlayer->GetMap();
        if (!map)
            return result;

        for (auto const& pair : map->GetCreatureBySpawnIdStore())
        {
            Creature* creature = pair.second;
            if (!creature || !creature->IsInWorld() || creature->IsDuringRemoveFromWorld() ||
                !creature->IsAlive() ||
                creature->GetZoneId() != zoneId)
                continue;

            // A WoW zone can contain several opposing settlements. Bind speech
            // to the incident's exact area, map, and instance via centerPlayer.
            if (creature->GetAreaId() != centerPlayer->GetAreaId())
                continue;

            if (radius > 0.0f && creature->GetDistance(centerPlayer) > radius)
                continue;

            if (!CreatureMatchesPreparationFaction(creature, dbFaction))
                continue;

            std::string npcType = NPCTypeDefinition::GetNPCType(creature);
            if (npcType != type && type != "any")
                continue;

            result.push_back(creature);
        }

        return result;
    }

    Creature* ZonePreparationManager::FindConfiguredCrier(
        const TownCrierConfig& config, Player* centerPlayer, uint8 dbFaction)
    {
        if (!centerPlayer || !centerPlayer->IsInWorld() ||
            centerPlayer->GetZoneId() != config.zoneId)
        {
            return nullptr;
        }

        Map* map = centerPlayer->GetMap();
        if (!map)
            return nullptr;

        Creature* closest = nullptr;
        float closestDistance = std::numeric_limits<float>::max();
        for (auto const& pair : map->GetCreatureBySpawnIdStore())
        {
            Creature* creature = pair.second;
            if (!creature || !creature->IsInWorld() || creature->IsDuringRemoveFromWorld() ||
                !creature->IsAlive() ||
                creature->GetEntry() != config.crierEntry ||
                creature->GetZoneId() != config.zoneId ||
                creature->GetAreaId() != centerPlayer->GetAreaId() ||
                creature->GetCreatureType() != CREATURE_TYPE_HUMANOID ||
                !CreatureMatchesPreparationFaction(creature, dbFaction))
            {
                continue;
            }

            float distance = creature->GetDistance(config.crierPosition);
            if (distance > std::max(10.0f, config.yellRadius))
                continue;
            if (distance < closestDistance)
            {
                closestDistance = distance;
                closest = creature;
            }
        }
        return closest;
    }

    bool ZonePreparationManager::CanTriggerDialogue(const NPCDialogue& dialogue, const ZonePreparationState& state)
    {
        // Check time of day if specified
        if (dialogue.minHour != 0 || dialogue.maxHour != 0)
        {
            // Get in-game hour
            // Note: In a real implementation, get the actual in-game time
            uint8 currentHour = (GameTime::GetGameTime().count() / 3600) % 24;
            if (dialogue.minHour <= dialogue.maxHour)
            {
                if (currentHour < dialogue.minHour || currentHour > dialogue.maxHour)
                    return false;
            }
            else
            {
                // Wraps around midnight
                if (currentHour < dialogue.minHour && currentHour > dialogue.maxHour)
                    return false;
            }
        }

        return true;
    }

    bool ZonePreparationManager::CanTriggerDialoguePair(const NPCDialoguePair& pair, const ZonePreparationState& state)
    {
        auto now = std::chrono::steady_clock::now();

        // Check pair-specific cooldown
        auto it = state.dialoguePairCooldowns.find(pair.id);
        if (it != state.dialoguePairCooldowns.end())
        {
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                now - it->second).count();
            if (elapsed < pair.cooldownMs)
                return false;
        }

        return true;
    }

    void ZonePreparationManager::EmitPreparationEvent(uint32 zoneId, const std::string& eventType,
                                                       const std::map<std::string, std::string>& data)
    {
        // Build JSON payload
        std::stringstream json;
        json << "{\"zoneId\":" << zoneId << ",\"eventType\":\"" << eventType << "\",\"data\":{";
        bool first = true;
        for (const auto& [key, value] : data)
        {
            if (!first) json << ",";
            json << "\"" << key << "\":\"" << value << "\"";
            first = false;
        }
        json << "}}";

        // Use the existing bridge infrastructure
        // sGuardSystemBridge->EmitPreparationEvent(json.str());

        ECG_LOG_TRACE("Preparation event: %s", json.str().c_str());
    }

    // ==========================================================================
    // UTILITY
    // ==========================================================================

    const char* ZonePreparationManager::ActivityToString(PreparationActivity activity)
    {
        switch (activity)
        {
            case PreparationActivity::IDLE:           return "IDLE";
            case PreparationActivity::GOSSIPING:      return "GOSSIPING";
            case PreparationActivity::CARRYING_CRATE: return "CARRYING_CRATE";
            case PreparationActivity::RUNNING_ERRANDS:return "RUNNING_ERRANDS";
            case PreparationActivity::BOARDING_UP:    return "BOARDING_UP";
            case PreparationActivity::GATHERING:      return "GATHERING";
            case PreparationActivity::PATROLLING_FAST:return "PATROLLING_FAST";
            case PreparationActivity::WATCHING_ROAD:  return "WATCHING_ROAD";
            case PreparationActivity::PRAYING:        return "PRAYING";
            case PreparationActivity::HIDING_GOODS:   return "HIDING_GOODS";
            case PreparationActivity::CONSTRUCTING:   return "CONSTRUCTING";
            default:                                  return "UNKNOWN";
        }
    }

    const char* ZonePreparationManager::CrierUrgencyToString(CrierUrgency urgency)
    {
        switch (urgency)
        {
            case CrierUrgency::CALM:   return "CALM";
            case CrierUrgency::ALERT:  return "ALERT";
            case CrierUrgency::URGENT: return "URGENT";
            case CrierUrgency::PANIC:  return "PANIC";
            default:                   return "UNKNOWN";
        }
    }

    std::string ZonePreparationManager::GetLocationNameForPosition(uint32 zoneId, Position const& pos)
    {
        // Zone 17 = The Barrens (has multiple towns sharing the zone)
        if (zoneId == 17)
        {
            // Crossroads: approximately (-450, -2650)
            // Bounds: X from -600 to -200, Y from -2900 to -2400
            if (pos.GetPositionX() > -600.0f && pos.GetPositionX() < -200.0f &&
                pos.GetPositionY() > -2900.0f && pos.GetPositionY() < -2400.0f)
                return "Crossroads";

            // Ratchet: approximately (-950, -3700)
            // Bounds: X from -1200 to -800, Y from -4000 to -3400
            if (pos.GetPositionX() > -1200.0f && pos.GetPositionX() < -800.0f &&
                pos.GetPositionY() > -4000.0f && pos.GetPositionY() < -3400.0f)
                return "Ratchet";

            // Camp Taurajo: approximately (-2350, -1900)
            // Bounds: X from -2600 to -2100, Y from -2200 to -1600
            if (pos.GetPositionX() > -2600.0f && pos.GetPositionX() < -2100.0f &&
                pos.GetPositionY() > -2200.0f && pos.GetPositionY() < -1600.0f)
                return "Camp Taurajo";
        }

        // Zone 33 = Stranglethorn Vale (multiple settlements)
        if (zoneId == 33)
        {
            // Grom'gol Base Camp: approximately (-12360, 210)
            if (pos.GetPositionX() > -12500.0f && pos.GetPositionX() < -12200.0f &&
                pos.GetPositionY() > 0.0f && pos.GetPositionY() < 400.0f)
                return "Grom'gol";

            // Booty Bay is area 35 but GetZoneId() correctly reports its
            // parent Stranglethorn Vale zone 33.
            if (pos.GetPositionX() > -14650.0f && pos.GetPositionX() < -14250.0f &&
                pos.GetPositionY() > 300.0f && pos.GetPositionY() < 750.0f)
                return "Booty Bay";
        }

        return ""; // No specific location within zone
    }

    // ==========================================================================
    // NPC TYPE DEFINITIONS
    // ==========================================================================

    bool NPCTypeDefinition::IsGuard(Creature* creature)
    {
        if (!creature)
            return false;

        // Check various flags that indicate a guard
        if (creature->IsGuard())
            return true;

        // Check name patterns
        std::string name = creature->GetName();
        std::transform(name.begin(), name.end(), name.begin(), ::tolower);

        return name.find("guard") != std::string::npos ||
               name.find("sentinel") != std::string::npos ||
               name.find("protector") != std::string::npos ||
               name.find("defender") != std::string::npos ||
               name.find("watchman") != std::string::npos ||
               name.find("grunt") != std::string::npos;
    }

    bool NPCTypeDefinition::IsMerchant(Creature* creature)
    {
        if (!creature)
            return false;

        return creature->IsVendor();
    }

    bool NPCTypeDefinition::IsCivilian(Creature* creature)
    {
        if (!creature)
            return false;

        // Civilian dialogue is for people. The former "friendly non-elite"
        // fallback classified fish schools, critters, beasts, totems, and
        // other ambient objects as civilians.
        if (creature->GetCreatureType() != CREATURE_TYPE_HUMANOID)
            return false;

        // Civilians are NPCs that aren't guards, merchants, or hostile
        if (IsGuard(creature) || IsMerchant(creature))
            return false;

        // Check if friendly and non-combat
        // Never call IsHostileTo with nullptr (can crash inside Unit::GetReactionTo).
        if (creature->IsHostileToPlayers())
            return false;

        // Not a boss or elite
        if (creature->isWorldBoss() || creature->isElite())
            return false;

        return true;
    }

    bool NPCTypeDefinition::IsMilitia(Creature* creature)
    {
        if (!creature)
            return false;

        std::string name = creature->GetName();
        std::transform(name.begin(), name.end(), name.begin(), ::tolower);

        return name.find("militia") != std::string::npos ||
               name.find("recruit") != std::string::npos ||
               name.find("volunteer") != std::string::npos;
    }

    bool NPCTypeDefinition::IsCrier(Creature* creature)
    {
        if (!creature)
            return false;

        std::string name = creature->GetName();
        std::transform(name.begin(), name.end(), name.begin(), ::tolower);

        return name.find("crier") != std::string::npos ||
               name.find("herald") != std::string::npos ||
               name.find("announcer") != std::string::npos;
    }

    bool NPCTypeDefinition::IsInnkeeper(Creature* creature)
    {
        if (!creature)
            return false;

        return creature->IsInnkeeper();
    }

    std::string NPCTypeDefinition::GetNPCType(Creature* creature)
    {
        if (IsGuard(creature))     return "guard";
        if (IsCrier(creature))     return "crier";
        if (IsMerchant(creature))  return "merchant";
        if (IsInnkeeper(creature)) return "innkeeper";
        if (IsMilitia(creature))   return "militia";
        if (IsCivilian(creature))  return "civilian";
        return "unknown";
    }

    // ==========================================================================
    // DEFAULT DIALOGUES
    // ==========================================================================

    const std::vector<std::string>& DefaultDialogues::GetUneasyGuardDialogues()
    {
        static std::vector<std::string> dialogues = {
            "Keep your eyes open. Something doesn't feel right.",
            "I've heard rumors of trouble on the roads.",
            "Stay alert, citizen. Report anything suspicious.",
            "The captain's put us on high alert.",
            "Did you see those scouts earlier? They looked worried.",
            "I don't like the look of things today.",
            "Watch the roads. We've had reports of movement.",
            "If you see anything, report to the guard post immediately.",
            "Something's brewing. I can feel it in my bones.",
            "The patrols have been doubled. Orders from command."
        };
        return dialogues;
    }

    const std::vector<std::string>& DefaultDialogues::GetUneasyMerchantDialogues()
    {
        static std::vector<std::string> dialogues = {
            "Business has been slow today. Everyone seems on edge.",
            "I've been thinking of closing early...",
            "Did you hear the news? Travelers are avoiding the roads.",
            "I should probably secure my more valuable goods.",
            "My cousin in the next town warned me about something.",
            "The guards seem nervous. That's never a good sign.",
            "I've got a bad feeling about today.",
            "Maybe I should pack up early. Just to be safe.",
            "Some merchants have already left. I wonder why...",
            "If things get bad, I'm heading to the inn."
        };
        return dialogues;
    }

    const std::vector<std::string>& DefaultDialogues::GetUneasyCivilianDialogues()
    {
        static std::vector<std::string> dialogues = {
            "Did you hear that noise?",
            "I'm staying indoors today.",
            "The children should stay close to home.",
            "Something's not right. I can feel it.",
            "My neighbor says there's trouble coming.",
            "I've been praying at the shrine. We need protection.",
            "Have you seen how the guards are acting?",
            "I'm stocking up on supplies. Just in case.",
            "The animals are acting strange too.",
            "Light protect us from what's coming."
        };
        return dialogues;
    }

    const std::vector<std::string>& DefaultDialogues::GetMobilizingGuardDialogues()
    {
        static std::vector<std::string> dialogues = {
            "All guards to your posts! This is not a drill!",
            "Lock down the district! No one in or out!",
            "Civilians, seek shelter immediately!",
            "Prepare for combat! Arm yourselves!",
            "Reinforcements are on the way! Hold your positions!",
            "Get those barricades up! Now!",
            "Anyone who can fight, report to the guard captain!",
            "This is a full mobilization! All hands!",
            "Protect the civilians! That's our priority!",
            "We've trained for this. Stay focused!"
        };
        return dialogues;
    }

    const std::vector<std::string>& DefaultDialogues::GetMobilizingMerchantDialogues()
    {
        static std::vector<std::string> dialogues = {
            "I'm closing shop! Everyone out!",
            "Help me board up the windows!",
            "Hide the valuables! Quickly!",
            "Head for the inn! It's safer there!",
            "This is bad. This is very bad!",
            "Everything I've worked for...",
            "May the Light protect my store!",
            "Everyone inside! Lock the doors!"
        };
        return dialogues;
    }

    const std::vector<std::string>& DefaultDialogues::GetMobilizingCivilianDialogues()
    {
        static std::vector<std::string> dialogues = {
            "Run! Get to safety!",
            "Where are the children?! Find the children!",
            "To the inn! Everyone to the inn!",
            "Light save us all!",
            "This can't be happening!",
            "Stay together! Don't panic!",
            "Follow the guards! They'll protect us!",
            "My family! I need to find my family!"
        };
        return dialogues;
    }

    const std::vector<std::string>& DefaultDialogues::GetCrierCalmAnnouncements()
    {
        static std::vector<std::string> announcements = {
            "Hear ye, hear ye! All is well in the district!",
            "Citizens! The watch reports no disturbances!",
            "The roads are safe! Trade continues as normal!",
            "Remember to support your local merchants!"
        };
        return announcements;
    }

    const std::vector<std::string>& DefaultDialogues::GetCrierAlertAnnouncements()
    {
        static std::vector<std::string> announcements = {
            "Attention citizens! Increased guard presence is for your safety!",
            "Travelers are advised to use caution on the roads!",
            "Report any suspicious activity to the nearest guard!",
            "Citizens are advised to remain vigilant!",
            "The guard requests all able-bodied citizens to stay alert!"
        };
        return announcements;
    }

    const std::vector<std::string>& DefaultDialogues::GetCrierUrgentAnnouncements()
    {
        static std::vector<std::string> announcements = {
            "URGENT! All citizens return to your homes immediately!",
            "WARNING! A threat has been identified! Seek shelter!",
            "EMERGENCY! The guard is mobilizing! Clear the streets!",
            "ATTENTION! Civilians must evacuate the area immediately!",
            "WARNING! Barricades are being erected! Stay indoors!"
        };
        return announcements;
    }

    const std::vector<std::string>& DefaultDialogues::GetCrierPanicAnnouncements()
    {
        static std::vector<std::string> announcements = {
            "WE ARE UNDER ATTACK! SEEK SHELTER IMMEDIATELY!",
            "EVACUATION IN PROGRESS! FOLLOW THE GUARDS!",
            "THE DISTRICT HAS FALLEN! FLEE FOR YOUR LIVES!",
            "REINFORCEMENTS ARE COMING! HOLD ON!",
            "PROTECT THE CIVILIANS! TO ARMS! TO ARMS!"
        };
        return announcements;
    }

    const std::vector<std::vector<std::string>>& DefaultDialogues::GetGuardToGuardConversations()
    {
        static std::vector<std::vector<std::string>> conversations = {
            {
                "Did you see the scout's report?",
                "Aye. Doesn't look good.",
                "The captain's worried. I can tell.",
                "We've handled worse. Stay sharp."
            },
            {
                "Double patrols again tonight.",
                "Third night in a row.",
                "Something big is coming.",
                "Let them come. We're ready."
            },
            {
                "Heard anything from the outer posts?",
                "No word since this morning.",
                "That's not like them.",
                "I know. Keep your weapon ready."
            }
        };
        return conversations;
    }

    const std::vector<std::vector<std::string>>& DefaultDialogues::GetMerchantToMerchantConversations()
    {
        static std::vector<std::vector<std::string>> conversations = {
            {
                "Business been slow for you too?",
                "Hardly any customers today.",
                "Everyone's staying home. Can't blame them.",
                "I might close early. No point staying open."
            },
            {
                "You hear what happened on the north road?",
                "Caravan got hit. Lost everything.",
                "This is bad for all of us.",
                "I'm hiding my best goods. Just in case."
            }
        };
        return conversations;
    }

    const std::vector<std::vector<std::string>>& DefaultDialogues::GetCivilianToGuardConversations()
    {
        static std::vector<std::vector<std::string>> conversations = {
            {
                "Guard! What's happening? Why are you all on alert?",
                "Just a precaution, citizen. Stay calm.",
                "But the patrols... there are so many more than usual!",
                "Return to your home. We have the situation under control."
            },
            {
                "Is it true? Are we being attacked?",
                "Keep your voice down. We don't want to cause panic.",
                "But my family! I need to know if we're safe!",
                "Get to the inn. It's the safest place right now."
            }
        };
        return conversations;
    }

    // ==========================================================================
    // CONSTRUCTION SYSTEM - DATA LOADING
    // ==========================================================================

    void ZonePreparationManager::LoadConstructionSites()
    {
        m_constructionSites.clear();

        QueryResult result = WorldDatabase.Query(
            "SELECT id, zone_id, site_name, structure_entry, structure_type, "
            "build_x, build_y, build_z, build_o, lumber_x, lumber_y, lumber_z, "
            "trips_required, min_phase, workers_needed, faction, priority "
            "FROM ec_guard_construction_sites WHERE enabled = 1 ORDER BY priority ASC");

        if (!result)
        {
            ECG_LOG_INFO("No construction sites found in database");
            return;
        }

        do
        {
            Field* fields = result->Fetch();
            ConstructionSite site;
            site.id             = fields[0].Get<uint32>();
            site.zoneId         = fields[1].Get<uint32>();
            site.siteName       = fields[2].Get<std::string>();
            site.structureEntry = fields[3].Get<uint32>();
            site.structureType  = fields[4].Get<uint8>();
            site.buildPos.Relocate(fields[5].Get<float>(), fields[6].Get<float>(),
                                   fields[7].Get<float>(), fields[8].Get<float>());
            site.lumberPos.Relocate(fields[9].Get<float>(), fields[10].Get<float>(),
                                    fields[11].Get<float>());
            site.tripsRequired  = fields[12].Get<uint8>();
            uint8 phaseVal      = fields[13].Get<uint8>();
            site.minPhase       = static_cast<ZonePhase>(phaseVal);
            site.workersNeeded  = fields[14].Get<uint8>();
            site.faction        = fields[15].Get<uint8>();
            site.priority       = fields[16].Get<uint8>();
            site.enabled        = true;

            m_constructionSites.push_back(std::move(site));
        } while (result->NextRow());

        ECG_LOG_INFO("Loaded %zu construction sites", m_constructionSites.size());
    }

    void ZonePreparationManager::LoadWorkerTemplates()
    {
        m_workerTemplates.clear();

        QueryResult result = WorldDatabase.Query(
            "SELECT id, faction, creature_entry, lumber_display_id, tool_display_id, "
            "walk_speed, say_start, say_working, say_done, say_lumber, say_flee "
            "FROM ec_guard_worker_templates WHERE enabled = 1");

        if (!result)
        {
            ECG_LOG_INFO("No worker templates found in database");
            return;
        }

        do
        {
            Field* fields = result->Fetch();
            WorkerTemplate tmpl;
            tmpl.id              = fields[0].Get<uint32>();
            tmpl.faction         = fields[1].Get<uint8>();
            tmpl.creatureEntry   = fields[2].Get<uint32>();
            tmpl.lumberDisplayId = fields[3].Get<uint32>();
            tmpl.toolDisplayId   = fields[4].Get<uint32>();
            tmpl.walkSpeed       = fields[5].Get<float>();
            tmpl.sayStart        = SplitPipeDelimited(fields[6].Get<std::string>());
            tmpl.sayWorking      = SplitPipeDelimited(fields[7].Get<std::string>());
            tmpl.sayDone         = SplitPipeDelimited(fields[8].Get<std::string>());
            tmpl.sayLumber       = SplitPipeDelimited(fields[9].Get<std::string>());
            tmpl.sayFlee         = SplitPipeDelimited(fields[10].Get<std::string>());
            tmpl.enabled         = true;

            m_workerTemplates.push_back(std::move(tmpl));
        } while (result->NextRow());

        ECG_LOG_INFO("Loaded %zu worker templates", m_workerTemplates.size());
    }

    void ZonePreparationManager::LoadInteractiveObjects()
    {
        m_interactiveObjects.clear();

        QueryResult result = WorldDatabase.Query(
            "SELECT id, zone_id, gameobject_entry, gameobject_type, "
            "spawn_x, spawn_y, spawn_z, spawn_o, spawn_on_phase, despawn_on_phase, "
            "linked_construction_id, faction, description "
            "FROM ec_guard_interactive_objects WHERE enabled = 1");

        if (!result)
        {
            ECG_LOG_INFO("No interactive objects found in database");
            return;
        }

        do
        {
            Field* fields = result->Fetch();
            InteractiveObjectTemplate obj;
            obj.id                  = fields[0].Get<uint32>();
            obj.zoneId              = fields[1].Get<uint32>();
            obj.gameObjectEntry     = fields[2].Get<uint32>();
            obj.gameObjectType      = fields[3].Get<uint8>();
            obj.spawnPos.Relocate(fields[4].Get<float>(), fields[5].Get<float>(),
                                  fields[6].Get<float>(), fields[7].Get<float>());
            obj.spawnOnPhase        = fields[8].Get<uint8>();
            obj.despawnOnPhase      = fields[9].Get<uint8>();
            obj.linkedConstructionId = fields[10].Get<uint32>();
            obj.faction             = fields[11].Get<uint8>();
            obj.description         = fields[12].Get<std::string>();
            obj.enabled             = true;

            m_interactiveObjects.push_back(std::move(obj));
        } while (result->NextRow());

        ECG_LOG_INFO("Loaded %zu interactive objects", m_interactiveObjects.size());
    }

    // ==========================================================================
    // CONSTRUCTION SYSTEM - HELPERS
    // ==========================================================================

    std::vector<std::string> ZonePreparationManager::SplitPipeDelimited(const std::string& input)
    {
        std::vector<std::string> result;
        if (input.empty())
            return result;

        std::stringstream ss(input);
        std::string token;
        while (std::getline(ss, token, '|'))
        {
            // Trim whitespace
            size_t start = token.find_first_not_of(" \t");
            size_t end = token.find_last_not_of(" \t");
            if (start != std::string::npos && end != std::string::npos)
                result.push_back(token.substr(start, end - start + 1));
            else if (!token.empty())
                result.push_back(token);
        }
        return result;
    }

    std::string ZonePreparationManager::GetRandomLine(const std::vector<std::string>& lines)
    {
        if (lines.empty())
            return "";
        return lines[urand(0, lines.size() - 1)];
    }

    void ZonePreparationManager::WorkerSay(Creature* worker, const std::vector<std::string>& lines)
    {
        if (!worker || !worker->IsInWorld() || worker->IsDuringRemoveFromWorld() ||
            !worker->IsAlive() || lines.empty())
            return;

        std::string line = GetRandomLine(lines);
        if (!line.empty())
        {
            worker->Say(line, LANG_UNIVERSAL);
        }
    }

    void ZonePreparationManager::EquipWorkerItem(Creature* worker, uint32 displayId)
    {
        if (!worker || displayId == 0)
            return;

        worker->SetUInt32Value(UNIT_VIRTUAL_ITEM_SLOT_ID + 0, displayId);
    }

    void ZonePreparationManager::ClearWorkerEquipment(Creature* worker)
    {
        if (!worker)
            return;

        worker->SetUInt32Value(UNIT_VIRTUAL_ITEM_SLOT_ID + 0, 0);
    }

    std::vector<ConstructionSite*> ZonePreparationManager::GetConstructionSitesForZone(uint32 zoneId, ZonePhase phase)
    {
        std::vector<ConstructionSite*> result;
        for (auto& site : m_constructionSites)
        {
            if (site.zoneId == zoneId && site.enabled &&
                static_cast<uint8>(site.minPhase) <= static_cast<uint8>(phase))
            {
                result.push_back(&site);
            }
        }
        return result;
    }

    std::vector<InteractiveObjectTemplate*> ZonePreparationManager::GetInteractiveObjectsForZone(uint32 zoneId, uint8 phase)
    {
        std::vector<InteractiveObjectTemplate*> result;
        for (auto& obj : m_interactiveObjects)
        {
            if (obj.zoneId == zoneId && obj.enabled)
            {
                // Include objects that spawn on this phase or always (0)
                if (obj.spawnOnPhase == 0 || obj.spawnOnPhase == phase)
                {
                    result.push_back(&obj);
                }
            }
        }
        return result;
    }

    WorkerTemplate* ZonePreparationManager::GetWorkerTemplateForFaction(uint8 faction)
    {
        // Collect all templates for this faction
        std::vector<WorkerTemplate*> candidates;
        for (auto& tmpl : m_workerTemplates)
        {
            if (tmpl.faction == faction && tmpl.enabled)
                candidates.push_back(&tmpl);
        }
        if (candidates.empty())
            return nullptr;

        // Pick a random one
        return candidates[urand(0, candidates.size() - 1)];
    }

    // ==========================================================================
    // CONSTRUCTION SYSTEM - SPAWN/DESPAWN WORKERS
    // ==========================================================================

    void ZonePreparationManager::SpawnConstructionWorkers(uint32 zoneId, ZonePhase phase)
    {
        if (!m_config.constructionEnabled)
            return;

        auto sites = GetConstructionSitesForZone(zoneId, phase);
        if (sites.empty())
            return;

        ZonePreparationState& state = GetOrCreateState(zoneId);

        // Find a player in zone to get the map
        Player* playerInZone = FindPlayerInZone(zoneId);
        if (!playerInZone)
            return;

        Map* map = playerInZone->GetMap();
        if (!map)
            return;

        // Determine zone faction for worker template selection
        ZoneState* zoneState = sECZoneStateMgr->GetZoneState(zoneId);
        uint8 zoneFaction = 2; // Default to Horde
        if (zoneState)
        {
            zoneFaction = (zoneState->owningFaction == TEAM_ALLIANCE) ? 1 : 2;
        }

        for (ConstructionSite* site : sites)
        {
            // Skip if already active (from a previous phase)
            if (state.activeConstructions.find(site->id) != state.activeConstructions.end())
            {
                // Already have this construction running, check if it needs more workers
                auto& ac = state.activeConstructions[site->id];
                if (ac.complete || ac.assignedWorkers.size() >= site->workersNeeded)
                    continue;
            }

            // Check faction match
            if (site->faction != 3 && site->faction != zoneFaction)
                continue;

            // Check worker limit
            uint32 currentWorkers = static_cast<uint32>(state.workerStates.size());
            if (currentWorkers >= m_config.maxWorkersPerZone)
                break;

            // Get worker template
            WorkerTemplate* workerTmpl = GetWorkerTemplateForFaction(zoneFaction);
            if (!workerTmpl)
            {
                ECG_LOG_WARN("No worker template found for faction %u in zone %u", zoneFaction, zoneId);
                continue;
            }

            // Initialize active construction if needed
            bool isNewConstruction = (state.activeConstructions.find(site->id) == state.activeConstructions.end());
            if (isNewConstruction)
            {
                ActiveConstruction ac;
                ac.siteId = site->id;
                ac.tripsRequired = site->tripsRequired;
                state.activeConstructions[site->id] = ac;

                // Emit construction started event
                sGuardSystemBridge->EmitConstructionStarted(zoneId, site->id, site->siteName,
                    site->structureEntry, site->workersNeeded, site->tripsRequired);
            }

            ActiveConstruction& ac = state.activeConstructions[site->id];

            // Spawn workers for this site
            uint8 workersToSpawn = site->workersNeeded - static_cast<uint8>(ac.assignedWorkers.size());
            for (uint8 i = 0; i < workersToSpawn; ++i)
            {
                if (state.workerStates.size() >= m_config.maxWorkersPerZone)
                    break;

                // Spawn worker at lumber pile position
                Creature* worker = map->SummonCreature(
                    workerTmpl->creatureEntry,
                    site->lumberPos,
                    nullptr,
                    0
                );

                if (worker)
                {
                    // Configure worker
                    worker->SetSpeed(MOVE_WALK, workerTmpl->walkSpeed);
                    worker->SetFaction(35); // Friendly to all (prevents aggro)

                    // Worker says spawn line
                    WorkerSay(worker, workerTmpl->sayStart);

                    // Track state
                    WorkerState ws;
                    ws.guid = worker->GetGUID();
                    ws.constructionSiteId = site->id;
                    ws.tripsCompleted = 0;
                    ws.carryingLumber = false;
                    ws.isWorking = false;
                    ws.workTimerMs = 0;
                    ws.reachedDestination = false;

                    state.workerStates[worker->GetGUID()] = ws;
                    ac.assignedWorkers.push_back(worker->GetGUID());

                    // Emit worker spawned event
                    sGuardSystemBridge->EmitWorkerSpawned(zoneId, workerTmpl->creatureEntry,
                        worker->GetName(), site->id, site->siteName);

                    ECG_LOG_DEBUG("Spawned construction worker %u at lumber pile for site '%s' in zone %u",
                                  workerTmpl->creatureEntry, site->siteName.c_str(), zoneId);
                }
            }
        }

        if (!state.workerStates.empty())
        {
            ECG_LOG_INFO("Construction workers active: %zu workers across %zu sites in zone %u",
                         state.workerStates.size(), state.activeConstructions.size(), zoneId);
        }
    }

    void ZonePreparationManager::DespawnConstructionWorkers(uint32 zoneId, ZonePhase phase)
    {
        ZonePreparationState* state = GetPreparationState(zoneId);
        if (!state || state->workerStates.empty())
            return;

        bool isImpact = (phase == ZonePhase::IMPACT);

        // Emit workers fleeing event for IMPACT phase
        if (isImpact && !state->workerStates.empty())
        {
            sGuardSystemBridge->EmitWorkersFleeing(zoneId, static_cast<uint32>(state->workerStates.size()));
        }

        // Get worker template for flee lines
        ZoneState* zoneState = sECZoneStateMgr->GetZoneState(zoneId);
        uint8 zoneFaction = zoneState ? ((zoneState->owningFaction == TEAM_ALLIANCE) ? 1 : 2) : 2;

        for (auto& [guid, ws] : state->workerStates)
        {
            Creature* worker = FindCreatureByGuid(guid);
            if (!worker || !worker->IsInWorld() || worker->IsDuringRemoveFromWorld() ||
                !worker->IsAlive())
                continue;

            if (isImpact)
            {
                // Workers flee during IMPACT - say flee line, run away, then despawn
                WorkerTemplate* tmpl = GetWorkerTemplateForFaction(zoneFaction);
                if (tmpl)
                {
                    WorkerSay(worker, tmpl->sayFlee);
                }

                // Clear equipment
                ClearWorkerEquipment(worker);

                // Run away from build site
                float fleeAngle = frand(0.0f, 2.0f * M_PI);
                float fleeDist = frand(30.0f, 60.0f);
                float fleeX = worker->GetPositionX() + std::cos(fleeAngle) * fleeDist;
                float fleeY = worker->GetPositionY() + std::sin(fleeAngle) * fleeDist;
                float fleeZ = worker->GetPositionZ();

                // Snap to ground level so workers don't float or sink
                worker->UpdateAllowedPositionZ(fleeX, fleeY, fleeZ);

                worker->SetSpeed(MOVE_RUN, 2.0f);
                worker->GetMotionMaster()->MovePoint(0, fleeX, fleeY, fleeZ);

                // Schedule despawn after fleeing
                worker->DespawnOrUnsummon(5000ms);
            }
            else
            {
                // Quiet/Aftermath - just despawn
                worker->DespawnOrUnsummon();
            }
        }

        ECG_LOG_INFO("Despawned %zu construction workers from zone %u (phase: %s)",
                     state->workerStates.size(), zoneId,
                     ZoneStateManager::ZonePhaseToString(phase));

        state->workerStates.clear();
        state->activeConstructions.clear();
    }

    // ==========================================================================
    // CONSTRUCTION SYSTEM - WORKER BEHAVIOR UPDATE
    // ==========================================================================

    void ZonePreparationManager::UpdateConstructionWorkers(uint32 zoneId, uint32 diff)
    {
        ZonePreparationState* state = GetPreparationState(zoneId);
        if (!state || state->workerStates.empty())
            return;

        // Determine zone faction for voice lines
        ZoneState* zoneState = sECZoneStateMgr->GetZoneState(zoneId);
        uint8 zoneFaction = zoneState ? ((zoneState->owningFaction == TEAM_ALLIANCE) ? 1 : 2) : 2;
        WorkerTemplate* voiceTmpl = GetWorkerTemplateForFaction(zoneFaction);

        std::vector<ObjectGuid> toRemove;

        for (auto& [guid, ws] : state->workerStates)
        {
            Creature* worker = FindCreatureByGuid(guid);
            if (!worker || !worker->IsInWorld() || worker->IsDuringRemoveFromWorld() ||
                !worker->IsAlive())
            {
                toRemove.push_back(guid);
                continue;
            }

            // Find the construction site for this worker
            auto acIt = state->activeConstructions.find(ws.constructionSiteId);
            if (acIt == state->activeConstructions.end() || acIt->second.complete)
            {
                // Construction done or site not found - worker idles
                continue;
            }

            // Find the site definition
            ConstructionSite* site = nullptr;
            for (auto& s : m_constructionSites)
            {
                if (s.id == ws.constructionSiteId)
                {
                    site = &s;
                    break;
                }
            }
            if (!site)
                continue;

            // Worker state machine
            bool isMoving = worker->isMoving();

            if (ws.isWorking)
            {
                // Worker is at build site, playing work animation
                ws.workTimerMs += diff;
                if (ws.workTimerMs >= m_config.workDurationMs)
                {
                    // Work cycle done - count as a trip
                    ws.tripsCompleted++;
                    acIt->second.totalTripsCompleted++;
                    ws.isWorking = false;
                    ws.workTimerMs = 0;

                    // Emit construction progress event
                    sGuardSystemBridge->EmitConstructionProgress(zoneId, site->id, site->siteName,
                        acIt->second.totalTripsCompleted, acIt->second.tripsRequired);

                    // Occasional working voice line
                    if (voiceTmpl && urand(0, 2) == 0)
                    {
                        WorkerSay(worker, voiceTmpl->sayWorking);
                        if (!voiceTmpl->sayWorking.empty())
                        {
                            sGuardSystemBridge->EmitWorkerSay(zoneId, worker->GetName(),
                                voiceTmpl->sayWorking[0], "working");
                        }
                    }

                    // Check if construction is complete
                    if (acIt->second.totalTripsCompleted >= acIt->second.tripsRequired)
                    {
                        CompleteConstruction(zoneId, ws.constructionSiteId);
                        continue;
                    }

                    // Not done yet - go back to lumber pile
                    ClearWorkerEquipment(worker);
                    ws.carryingLumber = false;
                    ws.reachedDestination = false;
                    worker->GetMotionMaster()->MovePoint(0,
                        site->lumberPos.GetPositionX(),
                        site->lumberPos.GetPositionY(),
                        site->lumberPos.GetPositionZ());
                }
            }
            else if (ws.carryingLumber)
            {
                // Walking to build site with lumber
                if (!isMoving)
                {
                    // Arrived at build site
                    ws.carryingLumber = false;
                    ws.isWorking = true;
                    ws.workTimerMs = 0;

                    // Swap lumber for tool
                    WorkerTemplate* tmpl = GetWorkerTemplateForFaction(zoneFaction);
                    if (tmpl)
                    {
                        EquipWorkerItem(worker, tmpl->toolDisplayId);
                    }

                    // Play work animation
                    worker->HandleEmoteCommand(EMOTE_STATE_WORK_CHOPWOOD);

                    ECG_LOG_TRACE("Worker %s started building at site '%s' (trip %u/%u)",
                                  worker->GetName().c_str(), site->siteName.c_str(),
                                  ws.tripsCompleted + 1, site->tripsRequired);
                }
            }
            else
            {
                // At lumber pile (or just spawned) - need to pick up lumber
                if (!isMoving)
                {
                    // Pick up lumber visual
                    WorkerTemplate* tmpl = GetWorkerTemplateForFaction(zoneFaction);
                    if (tmpl)
                    {
                        EquipWorkerItem(worker, tmpl->lumberDisplayId);
                        WorkerSay(worker, tmpl->sayLumber);
                        // Emit worker say event for lumber pickup
                        if (!tmpl->sayLumber.empty())
                        {
                            sGuardSystemBridge->EmitWorkerSay(zoneId, worker->GetName(),
                                tmpl->sayLumber[0], "lumber");
                        }
                    }

                    ws.carryingLumber = true;
                    ws.reachedDestination = false;

                    // Walk to build site
                    worker->GetMotionMaster()->MovePoint(0,
                        site->buildPos.GetPositionX(),
                        site->buildPos.GetPositionY(),
                        site->buildPos.GetPositionZ());

                    ECG_LOG_TRACE("Worker %s picked up lumber, heading to '%s'",
                                  worker->GetName().c_str(), site->siteName.c_str());
                }
            }
        }

        // Clean up removed workers
        for (const auto& guid : toRemove)
        {
            state->workerStates.erase(guid);
        }
    }

    // ==========================================================================
    // CONSTRUCTION SYSTEM - COMPLETE CONSTRUCTION
    // ==========================================================================

    void ZonePreparationManager::CompleteConstruction(uint32 zoneId, uint32 siteId)
    {
        ZonePreparationState* state = GetPreparationState(zoneId);
        if (!state)
            return;

        auto acIt = state->activeConstructions.find(siteId);
        if (acIt == state->activeConstructions.end() || acIt->second.complete)
            return;

        // Find the site definition
        ConstructionSite* site = nullptr;
        for (auto& s : m_constructionSites)
        {
            if (s.id == siteId)
            {
                site = &s;
                break;
            }
        }
        if (!site)
            return;

        // Find a player to get the map
        Player* playerInZone = FindPlayerInZone(zoneId);
        if (!playerInZone)
            return;

        // Spawn the completed structure
        GameObject* structure = playerInZone->SummonGameObject(
            site->structureEntry,
            site->buildPos.GetPositionX(),
            site->buildPos.GetPositionY(),
            site->buildPos.GetPositionZ(),
            site->buildPos.GetOrientation(),
            0.0f, 0.0f, 0.0f, 1.0f,
            0 // Permanent until phase reset
        );

        if (structure)
        {
            acIt->second.structureGuid = structure->GetGUID();
            state->activeGameObjects.insert(structure->GetGUID());

            ECG_LOG_INFO("Construction complete: '%s' (GO %u) in zone %u",
                         site->siteName.c_str(), site->structureEntry, zoneId);
        }

        acIt->second.complete = true;

        // Workers celebrate - say "Job's done!" and cheer
        ZoneState* zoneState = sECZoneStateMgr->GetZoneState(zoneId);
        uint8 zoneFaction = zoneState ? ((zoneState->owningFaction == TEAM_ALLIANCE) ? 1 : 2) : 2;
        WorkerTemplate* tmpl = GetWorkerTemplateForFaction(zoneFaction);

        for (const auto& workerGuid : acIt->second.assignedWorkers)
        {
            Creature* worker = FindCreatureByGuid(workerGuid);
            if (!worker || !worker->IsInWorld() || worker->IsDuringRemoveFromWorld() ||
                !worker->IsAlive())
                continue;

            // Clear tool, say done line, cheer
            ClearWorkerEquipment(worker);

            if (tmpl && !tmpl->sayDone.empty())
            {
                WorkerSay(worker, tmpl->sayDone);
                // Emit worker say event for "Job's done!"
                std::string doneLine = tmpl->sayDone.empty() ? "Job's done!" : tmpl->sayDone[0];
                sGuardSystemBridge->EmitWorkerSay(zoneId, worker->GetName(), doneLine, "done");
            }

            worker->HandleEmoteCommand(EMOTE_ONESHOT_CHEER);

            // Stop work animation
            worker->HandleEmoteCommand(EMOTE_STATE_NONE);

            // Worker becomes idle after celebrating
            auto wsIt = state->workerStates.find(workerGuid);
            if (wsIt != state->workerStates.end())
            {
                wsIt->second.isWorking = false;
                wsIt->second.carryingLumber = false;
            }
        }

        // Emit bridge event for construction complete
        sGuardSystemBridge->EmitConstructionComplete(zoneId, siteId, site->siteName, site->structureEntry);
    }

    // ==========================================================================
    // CONSTRUCTION DECAY SYSTEM
    // ==========================================================================

    void ZonePreparationManager::StartConstructionDecay(uint32 zoneId)
    {
        ZonePreparationState* state = GetPreparationState(zoneId);
        if (!state)
            return;

        uint32 decayCount = 0;
        for (auto& [siteId, ac] : state->activeConstructions)
        {
            if (ac.complete && !ac.isDecaying && ac.structureGuid)
            {
                ac.isDecaying = true;
                ac.decayHealth = 100.0f;
                decayCount++;

                ECG_LOG_INFO("Construction site %u in zone %u: structure decay started",
                    siteId, zoneId);
            }
        }

        if (decayCount > 0)
        {
            ECG_LOG_INFO("Zone %u: %u completed structures now decaying", zoneId, decayCount);
        }
    }

    void ZonePreparationManager::DecayConstructions(uint32 zoneId, uint32 diffMs)
    {
        ZonePreparationState* state = GetPreparationState(zoneId);
        if (!state)
            return;

        float decayAmount = m_config.constructionDecayRatePerSec * (static_cast<float>(diffMs) / 1000.0f);
        std::vector<uint32> toRemove;

        for (auto& [siteId, ac] : state->activeConstructions)
        {
            if (!ac.isDecaying)
                continue;

            ac.decayHealth -= decayAmount;

            if (ac.decayHealth <= 0.0f)
            {
                ac.decayHealth = 0.0f;

                // Despawn the structure GameObject
                bool removed = false;
                if (ac.structureGuid)
                {
                    Player* nearPlayer = FindPlayerInZone(zoneId);
                    if (nearPlayer)
                    {
                        if (GameObject* go = nearPlayer->GetMap()->GetGameObject(ac.structureGuid))
                        {
                            go->Delete();
                            state->activeGameObjects.erase(ac.structureGuid);
                            removed = true;

                            ECG_LOG_INFO("Construction site %u in zone %u: structure decayed and removed",
                                siteId, zoneId);
                        }
                    }
                    // No player in zone — keep tracking so we can retry next tick
                }
                else
                {
                    // No structure GUID means nothing to despawn
                    removed = true;
                }

                if (removed)
                {
                    toRemove.push_back(siteId);

                    // Emit bridge event
                    std::map<std::string, std::string> data;
                    data["zoneId"] = std::to_string(zoneId);
                    data["siteId"] = std::to_string(siteId);
                    data["reason"] = "decay";
                    EmitPreparationEvent(zoneId, "construction_decayed", data);
                }
            }
        }

        for (uint32 siteId : toRemove)
        {
            state->activeConstructions.erase(siteId);
        }
    }

    // ==========================================================================
    // INTERACTIVE OBJECTS - SPAWN/DESPAWN
    // ==========================================================================

    void ZonePreparationManager::SpawnInteractiveObjects(uint32 zoneId, ZonePhase phase)
    {
        if (!m_config.interactiveObjectsEnabled)
            return;

        uint8 phaseVal = static_cast<uint8>(phase);
        auto objects = GetInteractiveObjectsForZone(zoneId, phaseVal);
        if (objects.empty())
            return;

        ZonePreparationState& state = GetOrCreateState(zoneId);

        Player* playerInZone = FindPlayerInZone(zoneId);
        if (!playerInZone)
            return;

        uint32 spawnedCount = 0;

        for (InteractiveObjectTemplate* obj : objects)
        {
            // Skip if linked to a construction site that isn't complete yet
            if (obj->linkedConstructionId > 0)
            {
                auto acIt = state.activeConstructions.find(obj->linkedConstructionId);
                if (acIt == state.activeConstructions.end() || !acIt->second.complete)
                    continue;
            }

            // Spawn the gameobject
            GameObject* go = playerInZone->SummonGameObject(
                obj->gameObjectEntry,
                obj->spawnPos.GetPositionX(),
                obj->spawnPos.GetPositionY(),
                obj->spawnPos.GetPositionZ(),
                obj->spawnPos.GetOrientation(),
                0.0f, 0.0f, 0.0f, 1.0f,
                0 // No auto-respawn
            );

            if (go)
            {
                state.activeInteractiveObjects.insert(go->GetGUID());
                spawnedCount++;

                // Emit interactive object spawned event
                sGuardSystemBridge->EmitInteractiveObjectSpawned(zoneId, obj->gameObjectEntry,
                    obj->gameObjectType, obj->description);

                ECG_LOG_DEBUG("Spawned interactive object %u (%s) at (%.1f, %.1f, %.1f) in zone %u",
                              obj->gameObjectEntry, obj->description.c_str(),
                              obj->spawnPos.GetPositionX(), obj->spawnPos.GetPositionY(),
                              obj->spawnPos.GetPositionZ(), zoneId);
            }
        }

        if (spawnedCount > 0)
        {
            ECG_LOG_INFO("Spawned %u interactive objects for phase %s in zone %u",
                         spawnedCount, ZoneStateManager::ZonePhaseToString(phase), zoneId);
        }
    }

    void ZonePreparationManager::DespawnInteractiveObjects(uint32 zoneId, ZonePhase phase)
    {
        ZonePreparationState* state = GetPreparationState(zoneId);
        if (!state || state->activeInteractiveObjects.empty())
            return;

        uint8 phaseVal = static_cast<uint8>(phase);
        std::set<ObjectGuid> toRemove;

        for (const auto& guid : state->activeInteractiveObjects)
        {
            // Check if any interactive object template says to despawn on this phase
            bool shouldDespawn = false;

            // For QUIET phase, despawn everything
            if (phase == ZonePhase::QUIET)
            {
                shouldDespawn = true;
            }
            else
            {
                // Check templates for matching despawn phase
                for (const auto& obj : m_interactiveObjects)
                {
                    if (obj.zoneId == zoneId && obj.despawnOnPhase == phaseVal)
                    {
                        shouldDespawn = true;
                        break;
                    }
                }
            }

            if (shouldDespawn)
            {
                // Find and despawn
                WorldSessionMgr::SessionMap const& sessions = sWorldSessionMgr->GetAllSessions();
                for (auto const& itr : sessions)
                {
                    if (Player* player = itr.second->GetPlayer())
                    {
                        if (GameObject* go = ObjectAccessor::GetGameObject(*player, guid))
                        {
                            // Emit despawn event before deleting
                            uint32 goEntry = go->GetEntry();
                            std::string desc = "interactive object";
                            for (const auto& obj : m_interactiveObjects)
                            {
                                if (obj.gameObjectEntry == goEntry && obj.zoneId == zoneId)
                                {
                                    desc = obj.description;
                                    break;
                                }
                            }
                            sGuardSystemBridge->EmitInteractiveObjectDespawned(zoneId, goEntry, desc);

                            go->Delete();
                            toRemove.insert(guid);
                            break;
                        }
                    }
                }
            }
        }

        for (const auto& guid : toRemove)
        {
            state->activeInteractiveObjects.erase(guid);
        }

        if (!toRemove.empty())
        {
            ECG_LOG_INFO("Despawned %zu interactive objects from zone %u (phase: %s)",
                         toRemove.size(), zoneId, ZoneStateManager::ZonePhaseToString(phase));
        }
    }

} // namespace ECGuardSystem
