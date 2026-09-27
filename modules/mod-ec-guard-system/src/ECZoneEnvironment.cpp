/*
 * EC Guard System - Zone Environmental Effects Implementation
 */

#include "ECZoneEnvironment.h"
#include "ECZoneState.h"
#include "ECGuardSystemBridge.h"
#include "Creature.h"

#ifdef MOD_EC_REALTIME_BRIDGE
#include "RealtimeBridgeSocket.h"
#include "RealtimeBridgeTypes.h"
using namespace ECRealtimeBridge;
#endif
#include "GameObject.h"
#include "Map.h"
#include "Weather.h"
#include "WeatherMgr.h"
#include "World.h"
#include "WorldSession.h"
#include "WorldSessionMgr.h"
#include "Player.h"
#include "GridNotifiers.h"
#include "GridNotifiersImpl.h"
#include "CellImpl.h"
#include "Log.h"
#include "Config.h"
#include "ObjectAccessor.h"
#include "DatabaseEnv.h"

#include <sstream>
#include <unordered_set>

// Capital city zone IDs for weather differentiation
// These zones get more dramatic weather effects
static const std::unordered_set<uint32> CAPITAL_ZONE_IDS = {
    1519,  // Stormwind City
    1537,  // Ironforge
    1657,  // Darnassus
    3557,  // The Exodar
    1637,  // Orgrimmar
    1638,  // Thunder Bluff
    1497,  // Undercity
    3487,  // Silvermoon City
    3703,  // Shattrath City
    4395,  // Dalaran
};

// Helper to check if a zone is a capital city
static bool IsCapitalCity(uint32 zoneId)
{
    return CAPITAL_ZONE_IDS.count(zoneId) > 0;
}

// Helper to convert ECGuardSystem::WeatherType to global WeatherType
static ::WeatherType ToGlobalWeatherType(ECGuardSystem::WeatherType type)
{
    switch (type)
    {
        case ECGuardSystem::WeatherType::NONE:        return WEATHER_TYPE_FINE;
        case ECGuardSystem::WeatherType::RAIN_LIGHT:  return WEATHER_TYPE_RAIN;
        case ECGuardSystem::WeatherType::RAIN_MEDIUM: return WEATHER_TYPE_RAIN;
        case ECGuardSystem::WeatherType::RAIN_HEAVY:  return WEATHER_TYPE_RAIN;
        case ECGuardSystem::WeatherType::SNOW_LIGHT:  return WEATHER_TYPE_SNOW;
        case ECGuardSystem::WeatherType::SNOW_MEDIUM: return WEATHER_TYPE_SNOW;
        case ECGuardSystem::WeatherType::SNOW_HEAVY:  return WEATHER_TYPE_SNOW;
        case ECGuardSystem::WeatherType::SANDSTORM:   return WEATHER_TYPE_STORM;
        case ECGuardSystem::WeatherType::FOG:         return WEATHER_TYPE_FINE; // Fog uses FINE type with state
        case ECGuardSystem::WeatherType::THUNDER:     return WEATHER_TYPE_THUNDERS;
        case ECGuardSystem::WeatherType::BLACK_RAIN:  return WEATHER_TYPE_BLACKRAIN;
        default:                                      return WEATHER_TYPE_FINE;
    }
}

namespace ECGuardSystem
{
    ZoneEnvironmentManager* ZoneEnvironmentManager::instance()
    {
        static ZoneEnvironmentManager inst;
        return &inst;
    }

    void ZoneEnvironmentManager::Initialize()
    {
        LOG_INFO("module.ec-guard", "[ECZoneEnvironment] Initializing environmental effects...");
        LoadFromConfig();
        LoadDebrisTemplates();
        LoadCivilianEntries();
        LOG_INFO("module.ec-guard", "[ECZoneEnvironment] Environmental effects initialized");
    }

    void ZoneEnvironmentManager::LoadFromConfig()
    {
        m_config.weatherEnabled = sConfigMgr->GetOption<bool>("ECGuardSystem.Environment.WeatherEnabled", true);
        m_config.intensifyWeatherWithPhase = sConfigMgr->GetOption<bool>("ECGuardSystem.Environment.IntensifyWeather", true);

        m_config.civilianFleeEnabled = sConfigMgr->GetOption<bool>("ECGuardSystem.Environment.CivilianFleeEnabled", true);
        m_config.civilianFleeRadius = sConfigMgr->GetOption<float>("ECGuardSystem.Environment.CivilianFleeRadius", 30.0f);
        m_config.civilianFleeDelayMs = sConfigMgr->GetOption<uint32>("ECGuardSystem.Environment.CivilianFleeDelayMs", 2000);
        m_config.civilianReturnDelayMs = sConfigMgr->GetOption<uint32>("ECGuardSystem.Environment.CivilianReturnDelayMs", 60000);
        m_config.fleeSpeedMultiplier = sConfigMgr->GetOption<float>("ECGuardSystem.Environment.FleeSpeedMultiplier", 1.3f);

        m_config.debrisEnabled = sConfigMgr->GetOption<bool>("ECGuardSystem.Environment.DebrisEnabled", true);
        m_config.maxDebrisPerZone = sConfigMgr->GetOption<uint32>("ECGuardSystem.Environment.MaxDebrisPerZone", 20);
        m_config.debrisDespawnTimeMs = sConfigMgr->GetOption<uint32>("ECGuardSystem.Environment.DebrisDespawnTimeMs", 300000);

        m_config.uneasyWeatherEnabled = sConfigMgr->GetOption<bool>("ECGuardSystem.Environment.UneasyWeatherEnabled", false);
        m_config.mobilizingWeatherEnabled = sConfigMgr->GetOption<bool>("ECGuardSystem.Environment.MobilizingWeatherEnabled", true);
        m_config.impactWeatherEnabled = sConfigMgr->GetOption<bool>("ECGuardSystem.Environment.ImpactWeatherEnabled", true);
        m_config.debrisOnlyInImpact = sConfigMgr->GetOption<bool>("ECGuardSystem.Environment.DebrisOnlyInImpact", true);

        LOG_INFO("module.ec-guard", "[ECZoneEnvironment] Config loaded: Weather={}, CivilianFlee={}, Debris={}",
            m_config.weatherEnabled, m_config.civilianFleeEnabled, m_config.debrisEnabled);
    }

    void ZoneEnvironmentManager::LoadDebrisTemplates()
    {
        // Default debris templates as fallback
        m_debrisTemplates = {
            { 190752, 0.3f, 300000.0f, "Overturned barrel" },
            { 190753, 0.2f, 300000.0f, "Scattered crates" },
            { 190754, 0.15f, 300000.0f, "Broken cart wheel" },
            { 190755, 0.1f, 300000.0f, "Fallen lantern" },
        };

        // Try to load from database table ec_guard_debris_templates
        QueryResult result = WorldDatabase.Query(
            "SELECT gameobject_entry, spawn_chance, despawn_time_ms, description "
            "FROM ec_guard_debris_templates WHERE enabled = 1");

        if (result)
        {
            m_debrisTemplates.clear(); // Replace defaults with database entries
            do
            {
                Field* fields = result->Fetch();
                DebrisTemplate debris;
                debris.gameObjectEntry = fields[0].Get<uint32>();
                debris.spawnChance = fields[1].Get<float>();
                debris.despawnTimeMs = fields[2].Get<float>();
                debris.description = fields[3].Get<std::string>();
                m_debrisTemplates.push_back(debris);
            } while (result->NextRow());

            LOG_INFO("module.ec-guard", "[ECZoneEnvironment] Loaded {} debris templates from database",
                m_debrisTemplates.size());
        }
        else
        {
            LOG_INFO("module.ec-guard", "[ECZoneEnvironment] Using {} default debris templates (no database table)",
                m_debrisTemplates.size());
        }
    }

    void ZoneEnvironmentManager::LoadCivilianEntries()
    {
        m_civilianEntries.clear();

        // Load civilian creature entries from database table ec_guard_civilians
        // Table structure: entry (uint32), name (varchar), PRIMARY KEY (entry)
        QueryResult result = WorldDatabase.Query(
            "SELECT entry FROM ec_guard_civilians WHERE enabled = 1");

        if (result)
        {
            do
            {
                Field* fields = result->Fetch();
                uint32 entry = fields[0].Get<uint32>();
                m_civilianEntries.insert(entry);
            } while (result->NextRow());

            LOG_INFO("module.ec-guard", "[ECZoneEnvironment] Loaded {} civilian entries from database",
                m_civilianEntries.size());
        }
        else
        {
            // Fallback: Detect civilians by creature template type and flags
            // These are common civilian entry IDs from vanilla/TBC/WotLK
            LOG_INFO("module.ec-guard", "[ECZoneEnvironment] No ec_guard_civilians table found, using heuristic detection");
        }

        m_civilianEntriesLoaded = true;
    }

    void ZoneEnvironmentManager::Update(uint32 diff)
    {
        // Update fleeing civilians
        m_civilianUpdateTimer += diff;
        if (m_civilianUpdateTimer >= CIVILIAN_UPDATE_INTERVAL_MS)
        {
            UpdateFleeingCivilians(m_civilianUpdateTimer);
            m_civilianUpdateTimer = 0;
        }

        // Update weather
        m_weatherUpdateTimer += diff;
        if (m_weatherUpdateTimer >= WEATHER_UPDATE_INTERVAL_MS)
        {
            // Weather updates are handled by phase changes, but we could add
            // gradual intensity changes here
            m_weatherUpdateTimer = 0;
        }

        // Debris cleanup check
        m_debrisCleanupTimer += diff;
        if (m_debrisCleanupTimer >= DEBRIS_CLEANUP_INTERVAL_MS)
        {
            // Check and despawn expired debris
            for (auto& [zoneId, effects] : m_zoneEffects)
            {
                // Remove despawned debris from tracking
                // Note: Without a reference WorldObject we can't look up GUIDs
                // The debris will naturally despawn via their own timers
                // We just clean up any GUIDs that are no longer valid
                effects.activeDebris.clear(); // Simplified: rely on GO despawn timers
            }
            m_debrisCleanupTimer = 0;
        }
    }

    void ZoneEnvironmentManager::OnPhaseChanged(uint32 zoneId, ZonePhase oldPhase, ZonePhase newPhase)
    {
        LOG_DEBUG("module.ec-guard", "[ECZoneEnvironment] Zone {} phase changed: {} -> {}",
            zoneId, static_cast<int>(oldPhase), static_cast<int>(newPhase));

        // Apply weather based on new phase
        if (m_config.weatherEnabled)
        {
            ApplyWeatherForPhase(zoneId, newPhase);
        }

        // Trigger civilian flee behavior at certain phases
        if (m_config.civilianFleeEnabled)
        {
            if (newPhase == ZonePhase::MOBILIZING || newPhase == ZonePhase::IMPACT)
            {
                // Get zone state to find crime epicenter
                if (ZoneState* zone = sECZoneStateMgr->GetZoneState(zoneId))
                {
                    // Use tracked crime position as epicenter
                    if (zone->hasValidCrimePosition)
                    {
                        float fleeRadius = (newPhase == ZonePhase::IMPACT)
                            ? m_config.civilianFleeRadius * 2.0f  // Larger radius during IMPACT
                            : m_config.civilianFleeRadius;
                        TriggerCivilianFlee(zoneId, zone->lastCrimePosition, fleeRadius);

                        LOG_DEBUG("module.ec-guard", "[ECZoneEnvironment] Triggered civilian flee in zone {} at crime position ({:.1f}, {:.1f}, {:.1f})",
                            zoneId, zone->lastCrimePosition.GetPositionX(),
                            zone->lastCrimePosition.GetPositionY(),
                            zone->lastCrimePosition.GetPositionZ());
                    }
                }
            }
        }

        // Emit environment event
        std::map<std::string, std::string> eventData;
        eventData["oldPhase"] = ZoneStateManager::ZonePhaseToString(oldPhase);
        eventData["newPhase"] = ZoneStateManager::ZonePhaseToString(newPhase);
        eventData["weatherEnabled"] = m_config.weatherEnabled ? "true" : "false";
        EmitEnvironmentEvent(zoneId, "phase_environment_change", eventData);
    }

    void ZoneEnvironmentManager::OnZoneFallen(uint32 zoneId)
    {
        LOG_DEBUG("module.ec-guard", "[ECZoneEnvironment] Zone {} has fallen!", zoneId);

        // Maximum weather intensity
        if (m_config.weatherEnabled)
        {
            SetZoneWeather(zoneId, WeatherType::BLACK_RAIN, 1.0f);
        }

        // Panic all civilians
        if (m_config.civilianFleeEnabled)
        {
            // Get all sessions and find players in zone to trigger panic
            WorldSessionMgr::SessionMap const& sessions = sWorldSessionMgr->GetAllSessions();
            for (auto const& itr : sessions)
            {
                if (Player* player = itr.second->GetPlayer())
                {
                    if (player->IsInWorld() && player->GetZoneId() == zoneId)
                    {
                        // Find nearby civilians and make them panic
                        std::list<Creature*> civilians;
                        Acore::AllCreaturesOfEntryInRange check(player, 0, m_config.civilianFleeRadius * 2);
                        Acore::CreatureListSearcher<Acore::AllCreaturesOfEntryInRange> searcher(player, civilians, check);
                        Cell::VisitObjects(player, searcher, m_config.civilianFleeRadius * 2);

                        for (Creature* civilian : civilians)
                        {
                            if (IsCivilian(civilian))
                            {
                                MakeCivilianFlee(civilian, player->GetPosition());
                            }
                        }
                    }
                }
            }
        }
    }

    void ZoneEnvironmentManager::OnZoneRecovered(uint32 zoneId)
    {
        LOG_DEBUG("module.ec-guard", "[ECZoneEnvironment] Zone {} is recovering", zoneId);

        // Clear weather
        if (m_config.weatherEnabled)
        {
            ClearWeather(zoneId);
        }

        // Return civilians to posts
        ZoneEffects& effects = GetOrCreateZoneEffects(zoneId);
        for (auto& [guid, civilian] : effects.fleeingCivilians)
        {
            civilian.returnTime = std::chrono::steady_clock::now();
        }

        // Clean up debris
        if (m_config.debrisEnabled)
        {
            CleanupDebris(zoneId);
        }
    }

    void ZoneEnvironmentManager::ApplyWeatherForPhase(uint32 zoneId, ZonePhase phase)
    {
        bool shouldApply = false;
        float intensity = 0.0f;

        switch (phase)
        {
            case ZonePhase::QUIET:
                ClearWeather(zoneId);
                return;

            case ZonePhase::UNEASY:
                shouldApply = m_config.uneasyWeatherEnabled;
                intensity = 0.2f;
                break;

            case ZonePhase::MOBILIZING:
                shouldApply = m_config.mobilizingWeatherEnabled;
                intensity = 0.5f;
                break;

            case ZonePhase::IMPACT:
                shouldApply = m_config.impactWeatherEnabled;
                intensity = 0.85f;
                break;

            case ZonePhase::AFTERMATH:
                shouldApply = m_config.impactWeatherEnabled;
                intensity = 0.4f;
                break;
        }

        if (shouldApply)
        {
            bool isCapital = IsCapitalCity(zoneId);
            WeatherType weather = GetWeatherForPhase(phase, isCapital);
            SetZoneWeather(zoneId, weather, intensity);

            if (isCapital)
            {
                LOG_DEBUG("module.ec-guard", "[ECZoneEnvironment] Capital city {} using enhanced weather effects",
                    zoneId);
            }
        }
    }

    void ZoneEnvironmentManager::ClearWeather(uint32 zoneId)
    {
        ZoneEffects& effects = GetOrCreateZoneEffects(zoneId);
        if (effects.weatherActive)
        {
            SetZoneWeather(zoneId, WeatherType::NONE, 0.0f);
            effects.weatherActive = false;
        }
    }

    void ZoneEnvironmentManager::SetZoneWeather(uint32 zoneId, WeatherType type, float intensity)
    {
        ZoneEffects& effects = GetOrCreateZoneEffects(zoneId);
        effects.currentWeather = type;
        effects.weatherIntensity = intensity;
        effects.weatherActive = (type != WeatherType::NONE);
        effects.lastWeatherUpdate = std::chrono::steady_clock::now();

        // Get all players in zone and update their weather
        WorldSessionMgr::SessionMap const& sessions = sWorldSessionMgr->GetAllSessions();
        for (auto const& itr : sessions)
        {
            if (Player* player = itr.second->GetPlayer())
            {
                if (player->IsInWorld() && player->GetZoneId() == zoneId)
                {
                    // Get or create weather for zone via player's map
                    Map* map = player->GetMap();
                    if (map)
                    {
                        Weather* weather = map->GetOrGenerateZoneDefaultWeather(zoneId);
                        if (weather)
                        {
                            weather->SetWeather(ToGlobalWeatherType(type), intensity);
                            weather->UpdateWeather();
                        }
                    }
                    break; // Only need to set weather once per zone
                }
            }
        }

        LOG_DEBUG("module.ec-guard", "[ECZoneEnvironment] Zone {} weather set to {} intensity {}",
            zoneId, static_cast<int>(type), intensity);
    }

    WeatherType ZoneEnvironmentManager::GetWeatherForPhase(ZonePhase phase, bool isCapital) const
    {
        switch (phase)
        {
            case ZonePhase::UNEASY:
                return isCapital ? WeatherType::FOG : WeatherType::NONE;

            case ZonePhase::MOBILIZING:
                return WeatherType::FOG;

            case ZonePhase::IMPACT:
                return isCapital ? WeatherType::BLACK_RAIN : WeatherType::RAIN_HEAVY;

            case ZonePhase::AFTERMATH:
                return WeatherType::RAIN_LIGHT;

            default:
                return WeatherType::NONE;
        }
    }

    void ZoneEnvironmentManager::TriggerCivilianFlee(uint32 zoneId, Position const& epicenter, float radius)
    {
        if (!m_config.civilianFleeEnabled)
            return;

        // Find players in zone to use as search origin
        WorldSessionMgr::SessionMap const& sessions = sWorldSessionMgr->GetAllSessions();
        for (auto const& itr : sessions)
        {
            if (Player* player = itr.second->GetPlayer())
            {
                if (player->IsInWorld() && player->GetZoneId() == zoneId)
                {
                    // Find civilians near the epicenter
                    std::list<Creature*> creatures;
                    Acore::AllCreaturesOfEntryInRange check(player, 0, radius);
                    Acore::CreatureListSearcher<Acore::AllCreaturesOfEntryInRange> searcher(player, creatures, check);
                    Cell::VisitObjects(player, searcher, radius);

                    for (Creature* creature : creatures)
                    {
                        if (IsCivilian(creature))
                        {
                            // Check if within radius of epicenter
                            if (creature->GetDistance(epicenter) <= radius)
                            {
                                MakeCivilianFlee(creature, epicenter);
                            }
                        }
                    }
                    break; // Only need one player as search origin
                }
            }
        }
    }

    void ZoneEnvironmentManager::MakeCivilianFlee(Creature* civilian, Position const& dangerPos)
    {
        if (!civilian || !civilian->IsInWorld() || civilian->IsDuringRemoveFromWorld() ||
            !civilian->IsAlive())
            return;

        uint32 zoneId = civilian->GetZoneId();
        ZoneEffects& effects = GetOrCreateZoneEffects(zoneId);

        // Check if already fleeing
        auto it = effects.fleeingCivilians.find(civilian->GetGUID());
        if (it != effects.fleeingCivilians.end() && !it->second.hasReturned)
            return;

        // Store original position
        FleeingCivilian fleeing;
        fleeing.creatureGuid = civilian->GetGUID();
        fleeing.originalPosition = civilian->GetPosition();
        fleeing.originalOrientation = civilian->GetOrientation();
        fleeing.fleeStartTime = std::chrono::steady_clock::now();
        fleeing.returnTime = fleeing.fleeStartTime + std::chrono::milliseconds(m_config.civilianReturnDelayMs);
        fleeing.behavior = NPCBehavior::FLEEING;

        effects.fleeingCivilians[civilian->GetGUID()] = fleeing;

        // Calculate flee direction (away from danger)
        float angle = civilian->GetAngle(&dangerPos) + M_PI; // Opposite direction
        float fleeDistance = 25.0f + frand(0.0f, 15.0f);

        float x = civilian->GetPositionX() + cos(angle) * fleeDistance;
        float y = civilian->GetPositionY() + sin(angle) * fleeDistance;
        float z = civilian->GetPositionZ();

        // Update Z for terrain (if possible)
        civilian->UpdateGroundPositionZ(x, y, z);

        // Set flee behavior
        civilian->GetMotionMaster()->MovePoint(0, x, y, z);

        // Civilian yells while fleeing
        static const std::vector<std::string> fleeCries = {
            "Run! Run for your lives!",
            "Help! Help!",
            "The guards! Where are the guards?!",
            "Murderer! Murderer!",
            "Save yourselves!",
            "They're killing everyone!"
        };

        if (urand(0, 100) < 30) // 30% chance to yell
        {
            civilian->Yell(fleeCries[urand(0, fleeCries.size() - 1)], LANG_UNIVERSAL);
        }

        LOG_DEBUG("module.ec-guard", "[ECZoneEnvironment] Civilian {} fleeing from danger",
            civilian->GetGUID().GetCounter());
    }

    void ZoneEnvironmentManager::MakeCivilianCower(Creature* civilian)
    {
        if (!civilian || !civilian->IsInWorld() || civilian->IsDuringRemoveFromWorld() ||
            !civilian->IsAlive())
            return;

        uint32 zoneId = civilian->GetZoneId();
        ZoneEffects& effects = GetOrCreateZoneEffects(zoneId);

        // Store original position
        FleeingCivilian fleeing;
        fleeing.creatureGuid = civilian->GetGUID();
        fleeing.originalPosition = civilian->GetPosition();
        fleeing.originalOrientation = civilian->GetOrientation();
        fleeing.fleeStartTime = std::chrono::steady_clock::now();
        fleeing.returnTime = fleeing.fleeStartTime + std::chrono::milliseconds(m_config.civilianReturnDelayMs);
        fleeing.behavior = NPCBehavior::COWERING;

        effects.fleeingCivilians[civilian->GetGUID()] = fleeing;

        // Play cower emote
        civilian->HandleEmoteCommand(EMOTE_STATE_COWER);

        // Optionally say something
        static const std::vector<std::string> cowerCries = {
            "Please don't hurt me!",
            "I have children!",
            "Take whatever you want!",
            "*whimpers*",
            "I didn't see anything!"
        };

        if (urand(0, 100) < 20) // 20% chance
        {
            civilian->Say(cowerCries[urand(0, cowerCries.size() - 1)], LANG_UNIVERSAL);
        }
    }

    void ZoneEnvironmentManager::ReturnCivilianToPost(ObjectGuid civilianGuid)
    {
        for (auto& [zoneId, effects] : m_zoneEffects)
        {
            auto it = effects.fleeingCivilians.find(civilianGuid);
            if (it != effects.fleeingCivilians.end())
            {
                // Find a player in zone to use as reference for creature lookup
                Creature* civilian = nullptr;
                WorldSessionMgr::SessionMap const& sessions = sWorldSessionMgr->GetAllSessions();
                for (auto const& itr : sessions)
                {
                    if (Player* player = itr.second->GetPlayer())
                    {
                        if (player->IsInWorld() && player->GetZoneId() == zoneId)
                        {
                            civilian = ObjectAccessor::GetCreature(*player, civilianGuid);
                            break;
                        }
                    }
                }

                if (civilian && civilian->IsInWorld() && !civilian->IsDuringRemoveFromWorld() &&
                    civilian->IsAlive())
                {
                    // Return to original position
                    civilian->GetMotionMaster()->MovePoint(1,
                        it->second.originalPosition.GetPositionX(),
                        it->second.originalPosition.GetPositionY(),
                        it->second.originalPosition.GetPositionZ());

                    civilian->HandleEmoteCommand(EMOTE_STATE_STAND);
                }

                it->second.hasReturned = true;
                LOG_DEBUG("module.ec-guard", "[ECZoneEnvironment] Civilian {} returning to post",
                    civilianGuid.GetCounter());
            }
        }
    }

    void ZoneEnvironmentManager::UpdateFleeingCivilians(uint32 /* diff */)
    {
        auto now = std::chrono::steady_clock::now();

        for (auto& [zoneId, effects] : m_zoneEffects)
        {
            // Get zone state to check if still dangerous
            ZoneState* zone = sECZoneStateMgr->GetZoneState(zoneId);
            bool zoneIsCalm = !zone || (zone->currentPhase == ZonePhase::QUIET);

            std::vector<ObjectGuid> toRemove;

            for (auto& [guid, civilian] : effects.fleeingCivilians)
            {
                if (civilian.hasReturned)
                {
                    // Check if enough time has passed to remove from tracking
                    if (now > civilian.returnTime + std::chrono::seconds(30))
                    {
                        toRemove.push_back(guid);
                    }
                    continue;
                }

                // Check if it's time to return
                bool shouldReturn = (zoneIsCalm && now > civilian.returnTime) ||
                                   (now > civilian.returnTime + std::chrono::seconds(60)); // Force return after 60s

                if (shouldReturn)
                {
                    ReturnCivilianToPost(guid);
                }
            }

            // Clean up old entries
            for (ObjectGuid guid : toRemove)
            {
                effects.fleeingCivilians.erase(guid);
            }
        }
    }

    void ZoneEnvironmentManager::SpawnDebris(uint32 zoneId, Position const& location)
    {
        if (!m_config.debrisEnabled)
            return;

        ZoneEffects& effects = GetOrCreateZoneEffects(zoneId);
        if (effects.activeDebris.size() >= m_config.maxDebrisPerZone)
            return;

        if (m_debrisTemplates.empty())
        {
            LOG_DEBUG("module.ec-guard", "[ECZoneEnvironment] No debris templates loaded");
            return;
        }

        // Find a player in the zone to get the Map*
        Map* map = nullptr;
        WorldSessionMgr::SessionMap const& sessions = sWorldSessionMgr->GetAllSessions();
        for (auto const& itr : sessions)
        {
            if (Player* player = itr.second->GetPlayer())
            {
                if (player->IsInWorld() && player->GetZoneId() == zoneId && player->GetMap())
                {
                    map = player->GetMap();
                    break;
                }
            }
        }

        if (!map)
        {
            LOG_DEBUG("module.ec-guard", "[ECZoneEnvironment] No map available for zone %u - no players present", zoneId);
            return;
        }

        // Select debris template based on spawn chance
        const DebrisTemplate* selectedTemplate = nullptr;
        float roll = frand(0.0f, 1.0f);
        float cumulative = 0.0f;
        for (const auto& tmpl : m_debrisTemplates)
        {
            cumulative += tmpl.spawnChance;
            if (roll <= cumulative)
            {
                selectedTemplate = &tmpl;
                break;
            }
        }

        // Fallback to first template if roll exceeded cumulative
        if (!selectedTemplate && !m_debrisTemplates.empty())
        {
            selectedTemplate = &m_debrisTemplates[0];
        }

        if (!selectedTemplate)
            return;

        // Spawn position - adjust Z to ground level
        Position spawnPos = location;
        float groundZ = map->GetHeight(spawnPos.GetPositionX(), spawnPos.GetPositionY(), spawnPos.GetPositionZ() + 5.0f);
        if (groundZ > INVALID_HEIGHT)
        {
            spawnPos.m_positionZ = groundZ;
        }

        // Calculate despawn time from config or template
        uint32 despawnTime = m_config.debrisDespawnTimeMs > 0
            ? m_config.debrisDespawnTimeMs
            : static_cast<uint32>(selectedTemplate->despawnTimeMs);

        // Spawn the game object (rotation0-3 are 0.0f for no rotation, respawnTime in seconds)
        if (GameObject* debris = map->SummonGameObject(selectedTemplate->gameObjectEntry, spawnPos, 0.0f, 0.0f, 0.0f, 0.0f, despawnTime / 1000))
        {
            effects.activeDebris.push_back(debris->GetGUID());
            LOG_DEBUG("module.ec-guard", "[ECZoneEnvironment] Spawned debris '%s' (entry %u) at (%.1f, %.1f, %.1f) in zone %u",
                selectedTemplate->description.c_str(), selectedTemplate->gameObjectEntry,
                spawnPos.GetPositionX(), spawnPos.GetPositionY(), spawnPos.GetPositionZ(), zoneId);
        }
        else
        {
            LOG_WARN("module.ec-guard", "[ECZoneEnvironment] Failed to spawn debris entry %u - check gameobject_template",
                selectedTemplate->gameObjectEntry);
        }
    }

    void ZoneEnvironmentManager::CleanupDebris(uint32 zoneId)
    {
        ZoneEffects& effects = GetOrCreateZoneEffects(zoneId);

        // Find a player in zone to use as reference for finding GameObjects
        Player* referencePlayer = nullptr;
        WorldSessionMgr::SessionMap const& sessions = sWorldSessionMgr->GetAllSessions();
        for (auto const& itr : sessions)
        {
            if (Player* player = itr.second->GetPlayer())
            {
                if (player->IsInWorld() && player->GetZoneId() == zoneId)
                {
                    referencePlayer = player;
                    break;
                }
            }
        }

        uint32 cleanedCount = 0;
        for (ObjectGuid debrisGuid : effects.activeDebris)
        {
            // Try to find and delete the game object
            if (referencePlayer)
            {
                if (GameObject* debris = ObjectAccessor::GetGameObject(*referencePlayer, debrisGuid))
                {
                    debris->Delete();
                    cleanedCount++;
                }
            }
        }

        size_t totalDebris = effects.activeDebris.size();
        effects.activeDebris.clear();

        LOG_DEBUG("module.ec-guard", "[ECZoneEnvironment] Cleaned up %u of %zu debris objects in zone %u",
            cleanedCount, totalDebris, zoneId);
    }

    ZoneEffects& ZoneEnvironmentManager::GetOrCreateZoneEffects(uint32 zoneId)
    {
        auto it = m_zoneEffects.find(zoneId);
        if (it == m_zoneEffects.end())
        {
            ZoneEffects effects;
            effects.zoneId = zoneId;
            m_zoneEffects[zoneId] = effects;
            return m_zoneEffects[zoneId];
        }
        return it->second;
    }

    bool ZoneEnvironmentManager::IsCivilian(Creature* creature) const
    {
        if (!creature || !creature->IsInWorld() || creature->IsDuringRemoveFromWorld() ||
            !creature->IsAlive())
            return false;

        // Flee/cower behavior contains humanoid emotes and spoken lines. Even
        // if a bad database row labels wildlife or a fishing node as civilian,
        // it must never become a speaker.
        if (creature->GetCreatureType() != CREATURE_TYPE_HUMANOID)
            return false;

        // Quick rejection: hostile or elite creatures are never civilians.
        // Never call IsHostileTo with nullptr (can crash inside Unit::GetReactionTo).
        if (creature->IsHostileToPlayers())
            return false;

        if (creature->isElite())
            return false;

        uint32 entry = creature->GetEntry();

        // Check database-loaded civilian entries first (authoritative source)
        if (m_civilianEntriesLoaded && !m_civilianEntries.empty())
        {
            return m_civilianEntries.count(entry) > 0;
        }

        // Heuristic fallback when no database entries are loaded
        // Check for common civilian-like NPCs (vendors, trainers, innkeepers)
        if (creature->IsVendor() || creature->IsTrainer() || creature->IsInnkeeper())
            return true;

        // Check creature type for humanoid civilians
        CreatureTemplate const* cInfo = creature->GetCreatureTemplate();
        if (cInfo)
        {
            // Humanoids that aren't guards are often civilians
            if (cInfo->type == CREATURE_TYPE_HUMANOID && cInfo->rank == 0)
            {
                // Exclude guards/military by name patterns
                std::string name = cInfo->Name;
                if (name.find("Guard") != std::string::npos ||
                    name.find("Patrol") != std::string::npos ||
                    name.find("Sentinel") != std::string::npos ||
                    name.find("Soldier") != std::string::npos ||
                    name.find("Knight") != std::string::npos ||
                    name.find("Warrior") != std::string::npos ||
                    name.find("Grunt") != std::string::npos ||
                    name.find("Protector") != std::string::npos ||
                    name.find("Defender") != std::string::npos)
                {
                    return false;
                }

                // Check for civilian-like names
                if (name.find("Citizen") != std::string::npos ||
                    name.find("Peasant") != std::string::npos ||
                    name.find("Commoner") != std::string::npos ||
                    name.find("Villager") != std::string::npos ||
                    name.find("Resident") != std::string::npos ||
                    name.find("Child") != std::string::npos ||
                    name.find("Orphan") != std::string::npos)
                {
                    return true;
                }

                // Default: non-hostile, non-elite humanoids without guard names are likely civilians
                return true;
            }
        }

        return false;
    }

    Position ZoneEnvironmentManager::FindSafeSpot(uint32 /* zoneId */, Position const& from, float minDistance) const
    {
        // Simple implementation: move in a random direction away from danger
        float angle = frand(0.0f, 2.0f * M_PI);
        float distance = minDistance + frand(0.0f, 10.0f);

        Position safe;
        safe.m_positionX = from.GetPositionX() + cos(angle) * distance;
        safe.m_positionY = from.GetPositionY() + sin(angle) * distance;
        safe.m_positionZ = from.GetPositionZ();

        return safe;
    }

    void ZoneEnvironmentManager::EmitEnvironmentEvent(uint32 zoneId, const std::string& eventType,
                                                       const std::map<std::string, std::string>& data)
    {
        // Emit to realtime bridge
        ZoneState* zone = sECZoneStateMgr->GetZoneState(zoneId);
        std::string zoneName = zone ? zone->zoneName : "Unknown";

        LOG_DEBUG("module.ec-guard", "[ECZoneEnvironment] Emitting {} for zone {} ({})",
            eventType, zoneId, zoneName);

#ifdef MOD_EC_REALTIME_BRIDGE
        // Build JSON payload
        std::ostringstream ss;
        ss << "{\"zoneId\":" << zoneId
           << ",\"zoneName\":\"" << EscapeJsonString(zoneName) << "\""
           << ",\"eventType\":\"" << EscapeJsonString(eventType) << "\"";

        // Add custom data fields
        for (const auto& [key, value] : data)
        {
            ss << ",\"" << EscapeJsonString(key) << "\":\"" << EscapeJsonString(value) << "\"";
        }
        ss << "}";

        sRealtimeBridge->EmitEvent(EventNamespace::GUARD_SYSTEM, "environment_" + eventType, ss.str());
#else
        // Log warning if bridge not available
        LOG_WARN("module.ec-guard", "[ECZoneEnvironment] Realtime bridge not available for event: {}", eventType);
#endif
    }

    void ZoneEnvironmentManager::DebugPrintZoneEffects(uint32 zoneId)
    {
        auto it = m_zoneEffects.find(zoneId);
        if (it == m_zoneEffects.end())
        {
            LOG_INFO("module.ec-guard", "[ECZoneEnvironment] No effects for zone {}", zoneId);
            return;
        }

        ZoneEffects& effects = it->second;
        LOG_INFO("module.ec-guard", "[ECZoneEnvironment] Zone {} Effects:", zoneId);
        LOG_INFO("module.ec-guard", "  Weather: {} intensity {}",
            static_cast<int>(effects.currentWeather), effects.weatherIntensity);
        LOG_INFO("module.ec-guard", "  Active Debris: {}", effects.activeDebris.size());
        LOG_INFO("module.ec-guard", "  Fleeing Civilians: {}", effects.fleeingCivilians.size());
    }
}
