/*
 * EC Guard System - Zone Environmental Effects
 *
 * Manages environmental effects based on zone phase:
 * - Weather changes (fog, rain, darkness)
 * - NPC behavior (fleeing civilians, cowering, hiding)
 * - Debris spawning (overturned carts, scattered goods)
 */

#ifndef EC_GUARD_SYSTEM_ZONE_ENVIRONMENT_H
#define EC_GUARD_SYSTEM_ZONE_ENVIRONMENT_H

#include "ECZoneState.h"
#include "ObjectGuid.h"
#include "Position.h"
#include <vector>
#include <set>
#include <map>
#include <mutex>

class Creature;
class GameObject;
class Map;

namespace ECGuardSystem
{
    // ==========================================================================
    // WEATHER TYPES (matches client-side weather IDs)
    // ==========================================================================
    enum class WeatherType : uint32
    {
        NONE        = 0,
        RAIN_LIGHT  = 1,
        RAIN_MEDIUM = 2,
        RAIN_HEAVY  = 3,
        SNOW_LIGHT  = 6,
        SNOW_MEDIUM = 7,
        SNOW_HEAVY  = 8,
        SANDSTORM   = 41,
        FOG         = 46,      // Fog/mist effect
        THUNDER     = 86,      // Thunder without rain
        BLACK_RAIN  = 90       // Ominous dark rain
    };

    // ==========================================================================
    // NPC BEHAVIOR STATES
    // ==========================================================================
    enum class NPCBehavior : uint8
    {
        NORMAL      = 0,    // Normal behavior
        ALERT       = 1,    // Looking around nervously
        FLEEING     = 2,    // Running to safety
        COWERING    = 3,    // Crouching in fear
        HIDING      = 4,    // Moved to a hiding spot
        PANICKED    = 5     // Running randomly
    };

    // ==========================================================================
    // DEBRIS TEMPLATE
    // ==========================================================================
    struct DebrisTemplate
    {
        uint32 gameObjectEntry;     // GO template entry
        float spawnChance;          // Chance to spawn at each valid location
        float despawnTimeMs;        // How long debris stays
        std::string description;    // For logging/debugging
    };

    // ==========================================================================
    // ENVIRONMENTAL EFFECTS CONFIG
    // ==========================================================================
    struct EnvironmentConfig
    {
        // Weather settings
        bool weatherEnabled = true;
        bool intensifyWeatherWithPhase = true;     // Weather gets worse as phase escalates

        // NPC behavior settings
        bool civilianFleeEnabled = true;
        float civilianFleeRadius = 30.0f;          // Distance from crime to trigger flee
        uint32 civilianFleeDelayMs = 2000;         // Delay before civilians start fleeing
        uint32 civilianReturnDelayMs = 60000;      // Time before civilians return to posts
        float fleeSpeedMultiplier = 1.3f;          // Speed boost when fleeing

        // Debris settings
        bool debrisEnabled = true;
        uint32 maxDebrisPerZone = 20;              // Maximum debris objects per zone
        uint32 debrisDespawnTimeMs = 300000;       // 5 minutes

        // Which phases trigger effects
        bool uneasyWeatherEnabled = false;         // Light weather starts at UNEASY
        bool mobilizingWeatherEnabled = true;      // Weather at MOBILIZING
        bool impactWeatherEnabled = true;          // Heavy weather at IMPACT
        bool debrisOnlyInImpact = true;            // Only spawn debris in IMPACT phase
    };

    // ==========================================================================
    // FLEEING CIVILIAN TRACKING
    // ==========================================================================
    struct FleeingCivilian
    {
        ObjectGuid creatureGuid;
        Position originalPosition;
        float originalOrientation;
        std::chrono::steady_clock::time_point fleeStartTime;
        std::chrono::steady_clock::time_point returnTime;
        bool hasReturned = false;
        NPCBehavior behavior = NPCBehavior::NORMAL;
    };

    // ==========================================================================
    // ACTIVE ZONE EFFECTS
    // ==========================================================================
    struct ZoneEffects
    {
        uint32 zoneId = 0;
        WeatherType currentWeather = WeatherType::NONE;
        float weatherIntensity = 0.0f;             // 0.0 - 1.0
        std::vector<ObjectGuid> activeDebris;      // Spawned debris GOs
        std::map<ObjectGuid, FleeingCivilian> fleeingCivilians;
        std::chrono::steady_clock::time_point lastWeatherUpdate;
        bool weatherActive = false;
    };

    // ==========================================================================
    // ZONE ENVIRONMENT MANAGER
    // ==========================================================================
    class ZoneEnvironmentManager
    {
    public:
        static ZoneEnvironmentManager* instance();

        // Initialization
        void Initialize();
        void LoadFromConfig();
        void LoadDebrisTemplates();

        // Update
        void Update(uint32 diff);

        // Phase change handlers
        void OnPhaseChanged(uint32 zoneId, ZonePhase oldPhase, ZonePhase newPhase);
        void OnZoneFallen(uint32 zoneId);
        void OnZoneRecovered(uint32 zoneId);

        // Effect triggers
        void ApplyWeatherForPhase(uint32 zoneId, ZonePhase phase);
        void ClearWeather(uint32 zoneId);
        void TriggerCivilianFlee(uint32 zoneId, Position const& epicenter, float radius);
        void SpawnDebris(uint32 zoneId, Position const& location);
        void CleanupDebris(uint32 zoneId);

        // NPC behavior
        void MakeCivilianFlee(Creature* civilian, Position const& dangerPos);
        void MakeCivilianCower(Creature* civilian);
        void ReturnCivilianToPost(ObjectGuid civilianGuid);
        void UpdateFleeingCivilians(uint32 diff);

        // Weather
        void SetZoneWeather(uint32 zoneId, WeatherType type, float intensity);
        WeatherType GetWeatherForPhase(ZonePhase phase, bool isCapital) const;

        // Configuration
        const EnvironmentConfig& GetConfig() const { return m_config; }

        // Debug
        void DebugPrintZoneEffects(uint32 zoneId);

        // Event emission (public for cross-module use)
        void EmitEnvironmentEvent(uint32 zoneId, const std::string& eventType,
                                  const std::map<std::string, std::string>& data);

    private:
        ZoneEnvironmentManager() = default;
        ~ZoneEnvironmentManager() = default;

        // Thread safety mutex for environment state access
        // Using recursive_mutex because some public methods call other public methods
        mutable std::recursive_mutex m_envMutex;

        // Zone effects tracking - PROTECTED BY m_envMutex
        std::map<uint32, ZoneEffects> m_zoneEffects;

        // Debris templates - PROTECTED BY m_envMutex
        std::vector<DebrisTemplate> m_debrisTemplates;

        // Configuration
        EnvironmentConfig m_config;

        // Timers
        uint32 m_civilianUpdateTimer = 0;
        uint32 m_weatherUpdateTimer = 0;
        uint32 m_debrisCleanupTimer = 0;
        static constexpr uint32 CIVILIAN_UPDATE_INTERVAL_MS = 1000;
        static constexpr uint32 WEATHER_UPDATE_INTERVAL_MS = 5000;
        static constexpr uint32 DEBRIS_CLEANUP_INTERVAL_MS = 10000;

        // Internal methods
        ZoneEffects& GetOrCreateZoneEffects(uint32 zoneId);
        bool IsCivilian(Creature* creature) const;
        Position FindSafeSpot(uint32 zoneId, Position const& from, float minDistance) const;
        void LoadCivilianEntries();

        // Civilian creature entries loaded from database - PROTECTED BY m_envMutex
        std::set<uint32> m_civilianEntries;
        bool m_civilianEntriesLoaded = false;
    };
}

#define sECZoneEnvironmentMgr ECGuardSystem::ZoneEnvironmentManager::instance()

#endif // EC_GUARD_SYSTEM_ZONE_ENVIRONMENT_H
