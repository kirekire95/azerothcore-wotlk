/*
 * EC World Events - Custom World Event System for AzerothCore
 * Copyright (C) 2026 EC
 *
 * This module manages dynamic world events like town raids, defense events,
 * and scheduled PvP activities using the Playerbots module for NPC spawning.
 */

#ifndef EC_WORLD_EVENT_MANAGER_H
#define EC_WORLD_EVENT_MANAGER_H

#include "Common.h"
#include "ObjectGuid.h"
#include "RaidAssaultController.h"
#include "Log.h"
#include <map>
#include <deque>
#include <set>
#include <vector>
#include <string>
#include <memory>
#include <chrono>
#include <sstream>

class Player;
class Creature;
class GameObject;
class TransactionCallback;

// Forward declarations for Playerbots integration
class RandomPlayerbotMgr;

namespace EC
{
    struct BotRuntimeActivitySnapshot;

    // ==========================================================================
    // DEBUG LOGGING SYSTEM
    // ==========================================================================

    // Use EC-prefixed names to avoid conflicts with AzerothCore LOG_LEVEL_* macros
    enum class ECLogLevel : uint8
    {
        EC_ERROR   = 0,
        EC_WARNING = 1,
        EC_INFO    = 2,
        EC_DEBUG   = 3,
        EC_TRACE   = 4
    };

    // Forward declaration
    class WorldEventManager;

    // Debug helper class
    class ECDebug
    {
    public:
        static void Log(ECLogLevel level, const char* func, const char* fmt, ...);
        static void LogToGame(const std::string& message);
        static void SetDebugEnabled(bool enabled);
        static void SetLogLevel(uint8 level);
        static bool IsDebugEnabled();
        static uint8 GetLogLevel();
        static const char* LogLevelToString(ECLogLevel level);
        static const char* LogLevelToColor(ECLogLevel level);

    private:
        static bool s_debugEnabled;
        static uint8 s_logLevel;
    };

    // Logging macros for convenience
    #define EC_LOG_ERROR(fmt, ...) EC::ECDebug::Log(EC::ECLogLevel::EC_ERROR, __FUNCTION__, fmt, ##__VA_ARGS__)
    #define EC_LOG_WARN(fmt, ...)  EC::ECDebug::Log(EC::ECLogLevel::EC_WARNING, __FUNCTION__, fmt, ##__VA_ARGS__)
    #define EC_LOG_INFO(fmt, ...)  EC::ECDebug::Log(EC::ECLogLevel::EC_INFO, __FUNCTION__, fmt, ##__VA_ARGS__)
    #define EC_LOG_DEBUG(fmt, ...) EC::ECDebug::Log(EC::ECLogLevel::EC_DEBUG, __FUNCTION__, fmt, ##__VA_ARGS__)
    #define EC_LOG_TRACE(fmt, ...) EC::ECDebug::Log(EC::ECLogLevel::EC_TRACE, __FUNCTION__, fmt, ##__VA_ARGS__)

    // ==========================================================================
    // EVENT SYSTEM
    // ==========================================================================

    // Event states
    enum class EventState : uint8
    {
        SCHEDULED = 0,  // Waiting to start
        ACTIVE    = 1,  // Currently running
        COMPLETED = 2,  // Finished successfully
        CANCELLED = 3,  // Manually stopped
        FAILED    = 4   // Failed (e.g., no defenders)
    };

    // Event phases (within ACTIVE state)
    enum class EventPhase : uint8
    {
        NONE       = 0,  // Not in a phase (scheduled/completed/cancelled)
        PREPARING  = 1,  // Preparation phase: spawning objects/creatures, countdown warnings
        ACTIVE     = 2,  // Main event phase: combat, objectives
        ENDING     = 3   // Winding down: last chance for participation, cleanup warnings
    };

    // Event outcome (determined at end of ACTIVE phase or on early victory)
    enum class EventOutcome : uint8
    {
        PENDING       = 0,  // Not yet determined (event still running)
        ATTACKER_WIN  = 1,  // Attacking faction achieved their objectives
        DEFENDER_WIN  = 2,  // Defending faction held the line
        DRAW          = 3   // Neither side achieved decisive victory
    };

    // Event types
    enum class EventType : uint8
    {
        RAID        = 0,  // Attack on a town (Horde raids Alliance town, etc.)
        DEFENSE     = 1,  // Defend a town from attackers
        WORLD_BOSS  = 2,  // World boss spawn
        GATHERING   = 3,  // Resource gathering competition
        PVP_ZONE    = 4   // Open PvP zone activation
    };

    // Faction for events
    enum class EventFaction : uint8
    {
        NEUTRAL   = 0,
        ALLIANCE  = 1,
        HORDE     = 2
    };

    // Reward types
    enum class RewardType : uint8
    {
        GOLD   = 0,
        HONOR  = 1,
        ITEM   = 2,
        TITLE  = 3
    };

    // Building/fortification state (for capturable objects like AV towers)
    enum class BuildingState : uint8
    {
        INTACT   = 0,  // Normal state, fully functional
        BURNING  = 1,  // Being captured/burned (timer running)
        DESTROYED = 2  // Fully destroyed, no longer functional
    };

    // Event location data
    struct EventLocation
    {
        uint32 mapId;
        float x;
        float y;
        float z;
        float orientation;
        uint32 zoneId;
        uint32 areaId;
        std::string name;
    };

    // Event reward definition
    struct EventReward
    {
        uint32 rewardId;
        RewardType type;
        uint32 value;       // Gold amount, honor points, or item entry
        uint32 itemCount;   // For item rewards
        uint32 minKills;    // Minimum kills required
        float participation; // Minimum participation % (0.0 - 1.0)
        uint8 rewardCondition; // 0=any participant, 1=winning faction only, 2=losing faction only
    };

    // Event object definition (barricades, fortifications, etc.)
    struct EventObject
    {
        uint32 objectId;        // Unique ID for this object definition
        uint32 gameObjectEntry; // GameObject template entry from gameobject_template
        float offsetX;          // Offset from event center X
        float offsetY;          // Offset from event center Y
        float offsetZ;          // Offset from event center Z
        float orientation;      // Object orientation (rotation)
        uint8 spawnPhase;       // 1=PREPARING, 2=ACTIVE, 3=ENDING (when to spawn)
        uint8 despawnPhase;     // Phase when object despawns (0 = never auto-despawn)
        std::string name;       // Display name (e.g., "Barricade", "War Banner")

        // Capturable building settings (AV-style towers/bunkers)
        bool isCapturable;          // Can this object be captured/burned?
        uint32 captureTimeSeconds;  // How long to capture/burn (240 = 4 min like AV)
        int32 pointsOnCapture;      // Score points when captured
        uint32 burningEntry;        // GameObject entry for burning state visual
        uint32 destroyedEntry;      // GameObject entry for destroyed state visual

        // Conditional spawning (for victory/defeat objects in ENDING phase)
        uint8 spawnCondition;       // 0=always, 1=spawn only if attacker won, 2=spawn only if defender won

        // Barracks-style guard production (for capturable buildings)
        uint32 guardRespawnSeconds; // Interval between guard spawns (0 = disabled)
        uint32 maxGuards;           // Maximum alive guards this building can maintain
    };

    // Runtime state for a spawned capturable building
    struct SpawnedBuilding
    {
        uint32 objectDbId;      // ec_world_event_objects.id
        ObjectGuid objectGuid;  // Spawned GameObject GUID (intact state)
        ObjectGuid burningGuid; // Spawned burning visual GUID (if burning)
        ObjectGuid destroyedGuid; // Spawned destroyed visual GUID (if destroyed)
        BuildingState state;
        ObjectGuid capturerGuid; // Player who started the capture
        EventFaction capturingFaction; // Faction that's capturing
        std::chrono::system_clock::time_point captureStartTime;
        std::chrono::system_clock::time_point captureEndTime;
        std::string buildingName;
        int32 pointsValue;      // Points awarded on capture

        // Barracks-style guard production
        uint32 lastGuardRespawnMs;  // ms since last guard was produced
        uint32 guardsProduced;      // Total guards spawned by this building
        uint32 maxGuards;           // Max simultaneous alive guards
        uint32 guardRespawnMs;      // Respawn interval in ms (0 = disabled)
        std::vector<ObjectGuid> guardGuids; // Track which guards belong to this building
    };

    // Arrival methods for reinforcements
    enum class ArrivalMethod : uint8
    {
        MARCH       = 0,  // Instant spawn or marching in
        ZEPPELIN    = 1,  // Arrives by zeppelin/airship
        BOAT        = 2,  // Arrives by boat/ship
        PORTAL      = 3,  // Arrives through portal
        FLIGHT_PATH = 4,  // Arrives by flight path (wyvern/gryphon/bat/hippogryph)
        MOUNT       = 5   // Arrives riding a ground mount
    };

    // Event creature definition (named NPCs, elite guards, etc.)
    struct EventCreature
    {
        uint32 creatureId;      // Unique ID for this creature definition
        uint32 creatureEntry;   // Creature template entry from creature_template
        float offsetX;          // Offset from event center X (ignored if assigned to building)
        float offsetY;          // Offset from event center Y (ignored if assigned to building)
        float offsetZ;          // Offset from event center Z (ignored if assigned to building)
        float orientation;      // Creature facing direction
        uint8 spawnPhase;       // 1=PREPARING, 2=ACTIVE, 3=ENDING
        uint8 despawnPhase;     // Phase when creature despawns
        bool isBoss;            // Is this a boss creature for the event?
        bool isLeader;          // Is this an event leader (Thrall, Jaina, etc.)?
        std::string name;       // Display name (e.g., "Kor'kron Elite", "Thrall")

        // Building assignment (for tower/bunker guards)
        uint32 assignedBuildingId;  // ec_world_event_objects.id (0 = not assigned)
        float patrolRadius;         // Patrol radius around building or spawn point

        // Reinforcement/Arrival system (zeppelin, boat, portal arrivals)
        uint32 spawnDelaySeconds;   // Delay after phase starts (0 = immediate)
        float arrivalX;             // Arrival point X (0 = no arrival, spawn directly)
        float arrivalY;             // Arrival point Y
        float arrivalZ;             // Arrival point Z
        ArrivalMethod arrivalMethod; // How they arrive (for announcements)
        uint8 reinforcementWave;    // Wave number (0 = initial spawn)
        bool hasArrivalPoint;       // True if arrival coordinates are set
    };

    // Participant tracking
    struct EventParticipant
    {
        ObjectGuid playerGuid;
        std::string playerName;
        uint32 kills;
        uint32 deaths;
        uint64 damageDealt;
        uint64 healingDone;
        std::chrono::system_clock::time_point joinTime;
        bool rewarded;
    };

    // Event definition (loaded from DB)
    struct EventDefinition
    {
        uint32 eventId;
        std::string name;
        std::string description;
        EventType type;
        EventFaction attackerFaction;
        EventFaction defenderFaction;
        EventLocation location;
        uint32 minBots;
        uint32 maxBots;
        uint32 durationMinutes;
        uint32 minLevel;
        uint32 maxLevel;
        bool enabled;
        uint32 cooldownMinutes;  // Per-event cooldown override (0 = use global default)
        std::vector<EventReward> rewards;

        // Phase timing (in minutes)
        uint32 preparationMinutes;  // How long the preparation phase lasts (0 = skip)
        uint32 endingMinutes;       // How long the ending phase lasts (0 = immediate end)

        // Objects and creatures for this event
        std::vector<EventObject> objects;       // Barricades, banners, fortifications
        std::vector<EventCreature> creatures;   // Named NPCs, elite guards, bosses
    };

    // Pending reinforcement tracking
    struct PendingReinforcement
    {
        uint32 creatureDefId;       // Index into EventDefinition.creatures
        uint32 spawnTimeMs;         // Time (ms since phase start) when to spawn
        uint8 wave;                 // Reinforcement wave number
        bool spawned;               // Has this been spawned already?
    };

    // Pending bot teleport (deferred to avoid freezing world thread)
    struct PendingBotTeleport
    {
        ObjectGuid botGuid;
        float x, y, z, orientation;
        uint32 mapId;
        std::string botName;
        uint32 botLevel;
        std::string eventName;
        std::string role;
    };

    // Persistent checkpoint used to resume a bot's pre-event activity. This is
    // intentionally separate from Player::EntryPointData because battlegrounds
    // and LFG share that single slot.
    struct BotReturnContext
    {
        ObjectGuid botGuid;
        uint32 eventInstanceId = 0;
        uint32 eventId = 0;
        uint32 mapId = 0;
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;
        float orientation = 0.0f;
        std::string combatStrategies;
        std::string nonCombatStrategies;
        std::string deadStrategies;

        // Live-process details that PlayerbotAI::Reset(true) clears on a far
        // teleport. Durable recovery still has location + strategies; this
        // snapshot additionally resumes the exact travel/RPG target when the
        // event begins and ends without a server restart.
        std::shared_ptr<BotRuntimeActivitySnapshot> runtimeActivity;

        // Runtime-only restore state. The durable row is retained until the
        // asynchronous character-save transaction atomically removes it.
        bool teleportStarted = false;
        bool finalizeQueued = false;
        bool finalizeDelayWarned = false;
        bool finalizeCompleted = false;
        bool finalizeSucceeded = false;
        uint32 restoreAttempts = 0;
        std::chrono::steady_clock::time_point teleportStartedAt{};
        std::chrono::steady_clock::time_point finalizeQueuedAt{};
        std::chrono::steady_clock::time_point nextAttemptAt{};
        std::shared_ptr<TransactionCallback> finalizeCallback;
    };

    // Active event instance
    struct ActiveEvent
    {
        uint32 instanceId;
        uint32 eventId;
        EventState state;
        EventPhase phase;         // Current phase within the event
        std::chrono::system_clock::time_point startTime;
        std::chrono::system_clock::time_point endTime;
        std::chrono::system_clock::time_point phaseStartTime;   // When current phase started
        std::chrono::system_clock::time_point phaseEndTime;     // When current phase should end
        std::vector<ObjectGuid> spawnedBots;
        std::vector<std::pair<ObjectGuid, uint8>> spawnedObjects; // {GUID, despawnPhase} for spawned GameObjects
        std::vector<ObjectGuid> spawnedCreatures;   // Creatures spawned for this event
        std::map<ObjectGuid, EventParticipant> participants;
        uint32 totalKills;
        uint32 escalationLevel;  // How many times the event has escalated
        uint32 lastWarningMinutes;  // Last warning broadcast (to avoid spam)

        // Capturable buildings tracking (AV-style towers/bunkers)
        std::map<uint32, SpawnedBuilding> buildings;  // objectDbId -> SpawnedBuilding
        int32 attackerScore;    // Points for attacking faction
        int32 defenderScore;    // Points for defending faction

        // Reinforcement waves (zeppelin, boat, portal arrivals)
        std::vector<PendingReinforcement> pendingReinforcements;
        uint32 phaseElapsedMs = 0;  // Milliseconds since phase started (for reinforcement timing)

        // Auto-escalate tracking
        uint32 timeWithoutDefendersMs;  // Accumulated time with no defenders in zone
        uint32 defenderCount;           // Current number of defender-faction players in zone

        // Victory/defeat outcome (determined when transitioning to ENDING or at event end)
        EventOutcome outcome;

        // Deferred bot teleports (processed in batches to avoid freezing)
        std::deque<PendingBotTeleport> pendingBotTeleports;
        std::set<ObjectGuid> eventBotStrategiesApplied; // Applied after far-teleport acknowledgement
        std::set<ObjectGuid> raidAttackersReleased;
        uint32 raidAdvanceElapsedMs = 0;

        RaidAssault::Controller raidAssaultController;
        bool raidObjectivesAttempted = false;
        uint64 raidObjectiveElapsedMs = 0;
        std::map<RaidAssault::Id, ObjectGuid> raidAssaultResidentSources;
        std::map<RaidAssault::Id, uint32> raidAssaultBuildingSources;
        std::set<ObjectGuid> raidAssaultHeld;

        // Runtime combat telemetry for spawned event NPCs
        uint32 combatScanElapsedMs = 0;      // Time since last forced aggro scan
        uint32 combatStatusElapsedMs = 0;    // Time since last alive/engaged status log
        uint32 combatIdleWanderElapsedMs = 0; // Time since idle NPCs were given a wander nudge
        uint32 aliveCombatants = 0;          // Last counted alive military NPCs
        uint32 engagedCombatants = 0;        // Last counted military NPCs currently in combat
        bool aliveWorldEndNotified = false;  // Prevent duplicate leave/memory records

        // RAID settlement attrition. Snapshot once at ACTIVE start so the
        // victory denominator cannot move as grids unload or players arrive.
        uint32 settlementId = 0;
        uint32 townPopulationStart = 0;
        uint32 townCasualties = 0;
        std::set<ObjectGuid> townPopulation;
        std::set<ObjectGuid> townCasualtiesRecorded;
        uint8 casualtyReinforcementStage = 0;
        bool raidSacking = false;
        uint32 raidSackingElapsedMs = 0;
        std::set<uint32> raidSackTargetsStarted;
    };

    struct SettlementState
    {
        uint32 id = 0;
        std::string name;
        uint32 mapId = 0;
        uint32 zoneId = 0;
        uint32 areaId = 0;
        float centerX = 0.0f;
        float centerY = 0.0f;
        float centerZ = 0.0f;
        float populationRadius = 120.0f;
        EventFaction faction = EventFaction::NEUTRAL;
        uint64 treasury = 0;
        uint16 developmentLevel = 0;
        uint8 damagePct = 0;
        uint32 incomePerHour = 0;
    };

    // Main event manager class (singleton)
    class WorldEventManager
    {
    private:
        WorldEventManager();
        ~WorldEventManager();

        // Event definitions loaded from DB
        std::map<uint32, EventDefinition> m_eventDefinitions;

        // Currently active events
        std::map<uint32, ActiveEvent> m_activeEvents;

        // Bot state captured before world-event recruitment. Rows are also
        // persisted in the characters database for restart recovery.
        std::map<ObjectGuid, BotReturnContext> m_botReturnContexts;

        struct SettlementLink
        {
            uint32 sourceSettlementId = 0;
            uint32 targetSettlementId = 0;
            uint8 transferPct = 0;
            std::string linkType;
        };

        // Persistent settlement economy/progression loaded from world DB.
        std::map<uint32, SettlementState> m_settlements;
        std::vector<SettlementLink> m_settlementLinks;
        uint32 m_settlementEconomyElapsedMs = 0;
        uint32 m_settlementEconomyIntervalMs = 60000;

        // Event cooldowns (eventId -> when cooldown expires)
        std::map<uint32, std::chrono::system_clock::time_point> m_eventCooldowns;

        // Scheduled events (pending start)
        struct ScheduledEvent
        {
            uint32 scheduleId;
            uint32 zoneId;
            uint8 faction;  // 0=Horde, 1=Alliance
            uint32 remainingMs;
            bool isDefenseEvent;
        };
        std::map<uint32, ScheduledEvent> m_scheduledEvents;
        uint32 m_nextScheduleId = 1;

        // Next instance ID
        uint32 m_nextInstanceId;

        // Configuration
        bool m_enabled;
        uint32 m_updateIntervalMs;
        uint32 m_maxActiveEvents;

        // New config features (2026-01)
        bool m_announceToWorld;              // Broadcast server-wide on event start/end
        uint32 m_minParticipationForReward;  // Minimum participation % to receive any reward (0-100)
        bool m_autoEscalateEnabled;          // Auto-escalate if defenders are losing
        uint32 m_autoEscalateMinutes;        // Minutes without defenders before escalation
        bool m_logToDatabase;                // Log event history to database
        uint32 m_eventCooldownMinutes;       // Default cooldown after event completion

        // Event start staggering (avoids all scheduled events firing at once)
        uint32 m_eventStaggerMinutesMin;     // Min random delay for deferred events
        uint32 m_eventStaggerMinutesMax;     // Max random delay for deferred events

        // Spawning performance (batched to avoid freezing world thread)
        uint32 m_botHardCap;                 // Max bots per event
        bool m_raidDefenderBotsEnabled;      // Allow defender-side bot recruitment during RAID events
        uint32 m_raidDefenderBotSharePct;    // Desired % of recruited RAID bots that are defenders (0-100)
        uint32 m_maxCreatureSpawnsPerTick;   // Max creatures to spawn per update tick
        uint32 m_deferredSpawnStaggerMs;     // Ms between batches of deferred spawns
        uint32 m_maxBotTeleportsPerTick;     // Max bots to teleport per update tick

        // Timing
        uint32 m_lastUpdateTime;
        uint32 m_lastScheduleCheckMs = 0;       // Accumulator for schedule DB query throttle
        uint32 m_scheduleCheckIntervalMs = 30000; // Only query DB every 30s (Issue #10)
        bool m_scheduleCapacityDeferred = false; // Deduplicate the full-capacity scheduler state

        // Player zone tracking (for leave detection - Issue #5/#26)
        std::map<ObjectGuid, uint32> m_playerZones;

        // Defender count cache (Issue #6 - avoid iterating all sessions every tick)
        std::map<uint32, uint32> m_defenderCountCache;  // instanceId -> cached count
        uint32 m_defenderCountCacheAge = 0;              // ms since last recount
        static constexpr uint32 kDefenderCountCacheIntervalMs = 10000; // Recount every 10s

        // LogToGame rate limiting (Issue #18)
        uint32 m_logToGameAccumMs = 0;
        std::vector<std::string> m_pendingGameLogs;

        // Internal methods
        void LoadEventDefinitions();
        void LoadSettlements();
        void ProcessSettlementEconomy(uint32 diff);
        void SnapshotRaidTownPopulation(ActiveEvent& event, EventDefinition const& def);
        void ProcessRaidTownCasualty(ActiveEvent& event, EventDefinition const& def, Creature* creature);
        void EvaluateRaidTownAttrition(ActiveEvent& event, EventDefinition const& def);
        void LoadEventRewards(uint32 eventId, EventDefinition& def);
        void LoadEventObjects(uint32 eventId, EventDefinition& def);
        void LoadEventCreatures(uint32 eventId, EventDefinition& def);
        void CheckScheduledEvents();
        void ProcessScheduledDefenseEvents(uint32 diff);
        uint32 FindDefenseEventForZone(uint32 zoneId, uint8 faction) const;
        void UpdateActiveEvents(uint32 diff);

        // Phase management
        void TransitionToPhase(ActiveEvent& event, EventPhase newPhase);
        void UpdatePhaseTimers(ActiveEvent& event, uint32 diff);
        void BroadcastPhaseWarning(ActiveEvent& event, uint32 remainingMinutes);

        // Bot spawning
        void SpawnEventBots(ActiveEvent& event);
        void SpawnAdditionalBots(ActiveEvent& event, uint32 count, bool defendersOnly = false);
        void DespawnEventBots(ActiveEvent& event);
        bool CanRecruitEventBot(Player* bot) const;
        bool CaptureBotReturnContext(Player* bot, const ActiveEvent& event);
        void LoadBotReturnContexts();
        void ProcessBotReturnContexts();

        // GameObject spawning (barricades, fortifications)
        void SpawnEventGameObjects(ActiveEvent& event, EventPhase forPhase);
        void DespawnEventGameObjects(ActiveEvent& event, EventPhase forPhase);
        void DespawnAllEventGameObjects(ActiveEvent& event);

        // Creature spawning (named NPCs, elite guards)
        void SpawnEventCreatures(ActiveEvent& event, EventPhase forPhase);
        void DespawnEventCreatures(ActiveEvent& event, EventPhase forPhase);
        void DespawnAllEventCreatures(ActiveEvent& event);

        // Reinforcement waves (zeppelin, boat, portal arrivals)
        void SetupReinforcementWaves(ActiveEvent& event, EventPhase forPhase);
        void ProcessReinforcements(ActiveEvent& event, uint32 diff);
        void SpawnReinforcementCreature(ActiveEvent& event, const EventCreature& creatureDef);
        void AnnounceReinforcementArrival(const ActiveEvent& event, ArrivalMethod method, uint8 wave, uint32 count);
        static std::string ArrivalMethodToString(ArrivalMethod method);

        // Deferred bot teleports (batched to avoid freezing)
        void ProcessPendingBotTeleports(ActiveEvent& event);
        void ProcessEventBotCombat(ActiveEvent& event);
        void ProcessRaidAttackerAdvance(ActiveEvent& event, uint32 diff);
        void ProcessRaidSacking(ActiveEvent& event, uint32 diff);
        void ProcessEventCreatureCombat(ActiveEvent& event, uint32 diff);

        // Building capture mechanics (AV-style towers/bunkers)
        void UpdateBuildingCaptures(ActiveEvent& event, uint32 diff);
        void StartBuildingCapture(ActiveEvent& event, uint32 objectDbId, Player* capturer);
        void CancelBuildingCapture(ActiveEvent& event, uint32 objectDbId);
        void CompleteBuildingCapture(ActiveEvent& event, SpawnedBuilding& building);
        void TransitionBuildingState(ActiveEvent& event, SpawnedBuilding& building, BuildingState newState);
        SpawnedBuilding* FindBuildingByGuid(ActiveEvent& event, ObjectGuid guid);
        void BroadcastBuildingEvent(const ActiveEvent& event, const SpawnedBuilding& building, const std::string& action);
        void AlertBuildingGuards(ActiveEvent& event, uint32 buildingObjectId, Player* attacker);
        void ProcessBuildingGuardRespawns(ActiveEvent& event, uint32 diff);

        // Auto-escalate helpers
        uint32 CountDefendersInZone(const ActiveEvent& event, const EventDefinition& def) const;
        void ProcessAutoEscalate(ActiveEvent& event, const EventDefinition& def, uint32 instanceId, uint32 diff);

        // Victory/defeat determination
        EventOutcome DetermineOutcome(const ActiveEvent& event) const;
        void CheckEarlyVictory(ActiveEvent& event, uint32 instanceId);

        void DistributeRewards(ActiveEvent& event);
        void BroadcastEventMessage(const std::string& message, EventFaction targetFaction = EventFaction::NEUTRAL);
        void LogEventToDatabase(const ActiveEvent& event);
        void SaveParticipant(uint32 instanceId, const EventParticipant& participant);

        // AliveWorld integration
        void AutoJoinTier1BotsInZone(ActiveEvent& event, const EventDefinition& def);
        void NotifyAliveWorldBotJoined(uint64 botGuid, const ActiveEvent& event, const EventDefinition& def, bool voluntary, const std::string& role);
        void NotifyAliveWorldEventEnded(ActiveEvent& event, bool successful);
        float CalculatePersonalityEventAffinity(uint64 botGuid, EventType eventType) const;

    public:
        static WorldEventManager* Instance();

        // Initialization & lifecycle
        void Initialize();
        void Shutdown();
        void LoadFromConfig();
        void ReloadEvents();

        // Update (called from WorldScript::OnUpdate)
        void Update(uint32 diff);

        // Event control (called from commands)
        bool StartEvent(uint32 eventId, Player* initiator = nullptr);
        bool StopEvent(uint32 instanceId, Player* initiator = nullptr);
        bool EscalateEvent(uint32 instanceId);

        // Scheduled events (called from guard system for elite response)
        uint32 ScheduleDefenseEvent(uint32 zoneId, uint8 faction, uint32 delayMs);
        void CancelScheduledEvent(uint32 scheduledId);

        // Event queries
        const EventDefinition* GetEventDefinition(uint32 eventId) const;
        const ActiveEvent* GetActiveEvent(uint32 instanceId) const;
        std::vector<const ActiveEvent*> GetAllActiveEvents() const;
        std::vector<const EventDefinition*> GetAllEventDefinitions() const;

        // Participant tracking (called from PlayerScript hooks)
        void OnPlayerEnterZone(Player* player, uint32 zoneId, uint32 areaId);
        void OnPlayerLeaveZone(Player* player, uint32 oldZoneId, uint32 newZoneId);
        void OnPlayerKillCreature(Player* player, Creature* creature);
        void OnPlayerDeath(Player* player, Player* killer);
        void OnCreatureDeath(Creature* creature, Player* killer);

        // Boss tracking (called from creature AI hooks)
        void OnBossEngaged(Creature* boss, Player* engager);

        // Building interaction (called from GameObjectScript hooks)
        bool OnBuildingInteract(Player* player, GameObject* building);
        bool IsBuildingCapturable(GameObject* building) const;

        // Status
        bool IsEnabled() const { return m_enabled; }
        uint32 GetActiveEventCount() const { return static_cast<uint32>(m_activeEvents.size()); }

        // Debug control
        void SetDebugMode(bool enabled);
        bool IsDebugMode() const { return m_debugEnabled; }
        void SetDebugLogLevel(uint8 level);
        uint8 GetDebugLogLevel() const { return m_debugLogLevel; }
        void QueueGameLog(const std::string& message);  // Buffered debug log to GMs

        // Debug output
        void DebugDumpState() const;
        void DebugDumpEvent(uint32 instanceId) const;
        void DebugDumpParticipants(uint32 instanceId) const;

        // Utility
        static std::string EventStateToString(EventState state);
        static std::string EventPhaseToString(EventPhase phase);
        static std::string EventTypeToString(EventType type);
        static std::string EventFactionToString(EventFaction faction);
        static std::string BuildingStateToString(BuildingState state);
        static std::string EventOutcomeToString(EventOutcome outcome);

    private:
        // Debug settings
        bool m_debugEnabled;
        uint8 m_debugLogLevel;
    };

    #define sECWorldEventMgr EC::WorldEventManager::Instance()

} // namespace EC

#endif // EC_WORLD_EVENT_MANAGER_H
