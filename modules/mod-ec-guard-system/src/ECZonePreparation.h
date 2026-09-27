/*
 * EC Guard System - Zone Preparation Phase
 * "Shatter Before the Storm" - Foreshadowing & Buildup System
 *
 * This system manages the preparation/foreshadowing phase before attacks:
 * - NPC dialogues and gossip (merchants, guards, civilians chatting)
 * - Militia preparation behaviors (carrying crates, running, gathering)
 * - Town crier announcements
 * - Barricade and gameobject spawning
 * - Banner/flag changes based on zone control
 * - Ambient tension building
 */

#ifndef EC_GUARD_SYSTEM_ZONE_PREPARATION_H
#define EC_GUARD_SYSTEM_ZONE_PREPARATION_H

#include "ECZoneState.h"
#include "ObjectGuid.h"
#include "Position.h"
#include <vector>
#include <map>
#include <set>
#include <chrono>
#include <functional>

class Creature;
class GameObject;
class Map;
class Player;

namespace ECGuardSystem
{
    // ==========================================================================
    // PREPARATION ACTIVITY TYPES
    // ==========================================================================
    enum class PreparationActivity : uint8
    {
        IDLE            = 0,    // Normal state
        GOSSIPING       = 1,    // NPCs chatting nervously
        CARRYING_CRATE  = 2,    // Militia carrying supplies
        RUNNING_ERRANDS = 3,    // Running between locations
        BOARDING_UP     = 4,    // Boarding windows (emote)
        GATHERING       = 5,    // Gathering in groups
        PATROLLING_FAST = 6,    // Guards patrolling quickly
        WATCHING_ROAD   = 7,    // Looking toward threat direction
        PRAYING         = 8,    // At shrines/churches
        HIDING_GOODS    = 9,    // Merchants hiding wares
        CONSTRUCTING    = 10    // Peons building structures (carry lumber -> work -> done)
    };

    // ==========================================================================
    // TOWN CRIER STATE
    // ==========================================================================
    enum class CrierUrgency : uint8
    {
        CALM        = 0,    // Normal announcements
        ALERT       = 1,    // Warning tone
        URGENT      = 2,    // Urgent warnings
        PANIC       = 3     // Evacuation calls
    };

    // ==========================================================================
    // GAMEOBJECT SPAWN TYPES
    // ==========================================================================
    enum class PrepGameObjectType : uint8
    {
        BARRICADE       = 0,    // Wooden barricades
        SUPPLY_CRATE    = 1,    // Supply crates stacked
        WEAPON_RACK     = 2,    // Weapons being distributed
        TORCH_STAND     = 3,    // Additional lighting
        WARNING_SIGN    = 4,    // Warning signs
        FACTION_BANNER  = 5,    // Faction banners (changes on control)
        CAMPFIRE        = 6,    // Guard campfires
        TENT            = 7,    // Emergency tents
        DEBRIS          = 8,    // Battle debris (aftermath)
        FIRE_DAMAGE     = 9,    // Fire/destruction (impact)
        CAPTURE_POINT   = 10,   // Type 29 - faction control flag
        DESTRUCTIBLE_BUILD = 11, // Type 33 - attackable structures
        AREA_DAMAGE     = 12,   // Type 12 - AoE defensive hazards
        TRAP_OBJECT     = 13,   // Type 6 - proximity trigger traps
        GUARD_POST      = 14,   // Type 21 - guard spawner anchors
        WATCHTOWER      = 15,   // Built by peons - tall lookout
        BUNKER          = 16    // Built by peons - supply shelter
    };

    // ==========================================================================
    // NPC DIALOGUE ENTRY
    // ==========================================================================
    struct NPCDialogue
    {
        uint32 id = 0;
        uint32 zoneId = 0;                  // 0 = any zone
        ZonePhase minPhase = ZonePhase::QUIET;
        ZonePhase maxPhase = ZonePhase::AFTERMATH;

        std::string speakerType;            // "guard", "merchant", "civilian", "militia", "crier"
        std::string dialogue;               // The actual text
        std::string responseDialogue;       // Optional response from nearby NPC
        float responseChance = 0.5f;        // Chance of response

        uint32 cooldownMs = 60000;          // Per-NPC cooldown
        float triggerRadius = 30.0f;        // Distance from players to trigger
        bool isYell = false;                // /yell vs /say
        bool requiresAudience = true;       // Needs players nearby

        // Faction specificity
        uint8 faction = 3;                  // 0=Neutral, 1=Alliance, 2=Horde, 3=Any

        // Time of day restrictions (0-24, 0 = any)
        uint8 minHour = 0;
        uint8 maxHour = 0;

        // Location filter (for zones with multiple towns like Zone 17)
        std::string locationName;           // "Crossroads", "Ratchet", "Camp Taurajo", etc.

        bool enabled = true;
    };

    // ==========================================================================
    // NPC DIALOGUE PAIR (Two NPCs talking)
    // ==========================================================================
    struct NPCDialoguePair
    {
        uint32 id = 0;
        uint32 zoneId = 0;
        ZonePhase minPhase = ZonePhase::UNEASY;
        ZonePhase maxPhase = ZonePhase::MOBILIZING;

        std::string speakerAType;           // First speaker type
        std::string speakerBType;           // Second speaker type

        std::vector<std::string> exchanges; // Alternating A, B, A, B...
        uint32 delayBetweenLinesMs = 3000;  // Delay between lines

        uint32 cooldownMs = 120000;         // 2 minutes between repeats
        float maxDistance = 10.0f;          // Max distance between speakers
        bool requiresAudience = true;

        uint8 faction = 3;

        // Location filter (for zones with multiple towns like Zone 17)
        std::string locationName;           // "Crossroads", "Ratchet", "Camp Taurajo", etc.

        bool enabled = true;
    };

    // ==========================================================================
    // MILITIA SPAWN POINT
    // ==========================================================================
    struct MilitiaSpawnPoint
    {
        uint32 id = 0;
        uint32 zoneId = 0;
        uint32 creatureEntry = 0;           // Militia NPC entry

        Position spawnPos;
        Position waypoint1;                 // First waypoint (e.g., supply depot)
        Position waypoint2;                 // Second waypoint (e.g., barricade)

        PreparationActivity activity = PreparationActivity::CARRYING_CRATE;
        ZonePhase minPhase = ZonePhase::MOBILIZING;

        uint32 spawnDelayMs = 0;            // Delay after phase starts
        uint32 despawnOnPhase = 0;          // Despawn when this phase ends (0 = never)

        uint8 faction = 3;
        bool enabled = true;
    };

    // ==========================================================================
    // TOWN CRIER CONFIGURATION
    // ==========================================================================
    struct TownCrierConfig
    {
        uint32 id = 0;
        uint32 zoneId = 0;
        uint32 crierEntry = 0;              // Town crier creature entry

        Position crierPosition;             // Where the crier stands

        // Announcements by urgency level
        std::vector<std::string> calmAnnouncements;
        std::vector<std::string> alertAnnouncements;
        std::vector<std::string> urgentAnnouncements;
        std::vector<std::string> panicAnnouncements;

        uint32 announceIntervalMs = 45000;  // Time between announcements
        float yellRadius = 100.0f;          // How far the yell travels

        uint8 faction = 3;

        // Location filter (for zones with multiple towns like Zone 17)
        std::string locationName;           // "Crossroads", "Ratchet", "Camp Taurajo", etc.

        bool enabled = true;
    };

    // ==========================================================================
    // PREPARATION GAMEOBJECT TEMPLATE
    // ==========================================================================
    struct PrepGameObjectTemplate
    {
        uint32 id = 0;
        uint32 zoneId = 0;
        uint32 gameObjectEntry = 0;

        PrepGameObjectType type = PrepGameObjectType::BARRICADE;
        Position spawnPos;

        ZonePhase spawnOnPhase = ZonePhase::MOBILIZING;
        ZonePhase despawnOnPhase = ZonePhase::QUIET;

        uint32 spawnDelayMs = 0;            // Delay after phase starts
        uint32 despawnDelayMs = 0;          // Delay after phase ends
        uint32 respawnTimeMs = 0;           // 0 = no respawn

        uint8 faction = 3;                  // 0=Neutral, 1=Alliance, 2=Horde, 3=Any
        uint8 factionOverride = 0;          // If set, spawn this GO when zone controlled by faction

        bool enabled = true;
    };

    // ==========================================================================
    // FACTION BANNER CONFIG
    // ==========================================================================
    struct FactionBannerConfig
    {
        uint32 id = 0;
        uint32 zoneId = 0;
        Position position;

        uint32 allianceBannerEntry = 0;     // GO entry for Alliance control
        uint32 hordeBannerEntry = 0;        // GO entry for Horde control
        uint32 neutralBannerEntry = 0;      // GO entry for neutral/contested
        uint32 fallenBannerEntry = 0;       // GO entry for fallen zone (torn/burning)

        bool enabled = true;
    };

    // ==========================================================================
    // SCHEDULED DIALOGUE LINE (For multi-line conversations)
    // ==========================================================================
    struct ScheduledDialogueLine
    {
        ObjectGuid speakerGuid;                     // Who says this line
        std::string text;                           // The dialogue text
        std::chrono::steady_clock::time_point executeTime;  // When to say it
        bool isYell = false;                        // Yell vs Say
        uint32 zoneId = 0;                          // Zone for event emission
        uint32 conversationId = 0;                  // Links lines from same conversation
    };

    // ==========================================================================
    // PENDING SPAWN (For delayed militia/gameobject spawns)
    // ==========================================================================
    struct PendingSpawn
    {
        enum Type { MILITIA, GAMEOBJECT };
        Type type = MILITIA;
        uint32 entry = 0;                           // creature/GO entry ID
        Position pos;                               // spawn position
        PreparationActivity activity = PreparationActivity::CARRYING_CRATE;  // For militia only
        std::chrono::steady_clock::time_point spawnTime;  // When to spawn
    };

    // ==========================================================================
    // CONSTRUCTION SITE (Built by peons/peasants)
    // ==========================================================================
    struct ConstructionSite
    {
        uint32 id = 0;
        uint32 zoneId = 0;
        std::string siteName;
        uint32 structureEntry = 0;      // GO entry spawned when complete
        uint8 structureType = 0;        // PrepGameObjectType value
        Position buildPos;              // Where the structure gets built
        Position lumberPos;             // Where peons pick up lumber
        uint8 tripsRequired = 3;        // Lumber trips to complete
        ZonePhase minPhase = ZonePhase::UNEASY;
        uint8 workersNeeded = 2;
        uint8 faction = 3;              // 0=neutral, 1=alliance, 2=horde, 3=any
        uint8 priority = 5;             // Build order (lower first)
        bool enabled = true;
    };

    // ==========================================================================
    // WORKER TEMPLATE (Peon/Peasant voice lines & visuals)
    // ==========================================================================
    struct WorkerTemplate
    {
        uint32 id = 0;
        uint8 faction = 0;             // 1=alliance, 2=horde
        uint32 creatureEntry = 0;       // Peon/Peasant creature entry
        uint32 lumberDisplayId = 0;     // Item display for carrying lumber
        uint32 toolDisplayId = 0;       // Item display for hammer/axe
        float walkSpeed = 1.0f;
        std::vector<std::string> sayStart;    // On spawn
        std::vector<std::string> sayWorking;  // While building
        std::vector<std::string> sayDone;     // Construction complete
        std::vector<std::string> sayLumber;   // Picking up lumber
        std::vector<std::string> sayFlee;     // Fleeing during IMPACT
        bool enabled = true;
    };

    // ==========================================================================
    // INTERACTIVE OBJECT TEMPLATE (Capture points, traps, etc.)
    // ==========================================================================
    struct InteractiveObjectTemplate
    {
        uint32 id = 0;
        uint32 zoneId = 0;
        uint32 gameObjectEntry = 0;
        uint8 gameObjectType = 0;       // WoW GO type (6, 12, 21, 29, 33)
        Position spawnPos;
        uint8 spawnOnPhase = 0;         // 0=always
        uint8 despawnOnPhase = 0;       // 0=never
        uint32 linkedConstructionId = 0; // If built by peons
        uint8 faction = 3;
        std::string description;
        bool enabled = true;
    };

    // ==========================================================================
    // WORKER STATE (Runtime state for a single peon)
    // ==========================================================================
    struct WorkerState
    {
        ObjectGuid guid;
        uint32 constructionSiteId = 0;
        uint8 tripsCompleted = 0;
        bool carryingLumber = false;    // Walking to build site with lumber
        bool isWorking = false;         // Playing work animation
        uint32 workTimerMs = 0;         // Time spent working before next trip
        bool reachedDestination = false; // For movement completion check
    };

    // ==========================================================================
    // ACTIVE CONSTRUCTION (Runtime state for a construction site)
    // ==========================================================================
    struct ActiveConstruction
    {
        uint32 siteId = 0;
        uint8 totalTripsCompleted = 0;
        uint8 tripsRequired = 3;
        bool complete = false;
        ObjectGuid structureGuid;       // GO guid once spawned
        std::vector<ObjectGuid> assignedWorkers;

        // Decay system - structures lose HP when zone cools down
        bool isDecaying = false;
        float decayHealth = 100.0f;     // Current health (0-100)
    };

    // ==========================================================================
    // ACTIVE PREPARATION STATE (Per Zone)
    // ==========================================================================
    struct ZonePreparationState
    {
        uint32 zoneId = 0;
        ZonePhase lastProcessedPhase = ZonePhase::QUIET;

        // Active entities
        std::set<ObjectGuid> activeMilitia;
        std::set<ObjectGuid> activeGameObjects;
        std::map<ObjectGuid, PreparationActivity> npcActivities;
        std::map<ObjectGuid, Position> npcDestinations;   // Current movement destination
        std::map<ObjectGuid, Position> npcOrigins;        // Original position (for back-and-forth)

        // Scheduled dialogue lines (for multi-line conversations)
        std::vector<ScheduledDialogueLine> scheduledDialogues;
        uint32 nextConversationId = 1;

        // Crier state
        ObjectGuid crierGuid;
        CrierUrgency crierUrgency = CrierUrgency::CALM;
        std::chrono::steady_clock::time_point lastAnnouncement;

        // Dialogue cooldowns (NPC GUID -> last dialogue time)
        std::map<ObjectGuid, std::chrono::steady_clock::time_point> dialogueCooldowns;
        std::map<uint32, std::chrono::steady_clock::time_point> dialoguePairCooldowns;

        // Banner state
        std::map<uint32, ObjectGuid> activeBanners; // banner config id -> current GO

        // Pending delayed spawns (spawnDelayMs support)
        std::vector<PendingSpawn> pendingSpawns;

        // Construction system state
        std::map<uint32, ActiveConstruction> activeConstructions; // siteId -> state
        std::map<ObjectGuid, WorkerState> workerStates;          // worker GUID -> state
        std::set<ObjectGuid> activeInteractiveObjects;           // spawned interactive GOs

        // Timing
        std::chrono::steady_clock::time_point phaseStartTime;
        uint32 elapsedInPhaseMs = 0;

        void Reset()
        {
            activeMilitia.clear();
            activeGameObjects.clear();
            npcActivities.clear();
            npcDestinations.clear();
            npcOrigins.clear();
            scheduledDialogues.clear();
            pendingSpawns.clear();
            activeConstructions.clear();
            workerStates.clear();
            activeInteractiveObjects.clear();
            crierGuid.Clear();
            crierUrgency = CrierUrgency::CALM;
            dialogueCooldowns.clear();
            dialoguePairCooldowns.clear();
            elapsedInPhaseMs = 0;
        }
    };

    // ==========================================================================
    // PREPARATION CONFIG
    // ==========================================================================
    struct PreparationConfig
    {
        // General settings
        bool enabled = true;
        bool dialoguesEnabled = true;
        bool militiaEnabled = true;
        bool crierEnabled = true;
        bool gameObjectsEnabled = true;
        bool bannersEnabled = true;

        // Timing
        uint32 dialogueCheckIntervalMs = 5000;      // Check for dialogue triggers
        uint32 militiaUpdateIntervalMs = 2000;      // Update militia behaviors
        uint32 gameObjectCheckIntervalMs = 10000;   // Check for GO spawns/despawns

        // Dialogue settings
        float dialogueTriggerRadius = 40.0f;        // Distance to trigger dialogues
        uint32 minDialogueCooldownMs = 30000;       // Min time between any dialogue
        uint32 dialoguePairDelayMs = 3000;          // Delay between conversation lines
        float dialogueResponseChance = 0.4f;        // Chance of NPC response

        // Militia settings
        uint32 maxMilitiaPerZone = 10;
        float militiaWalkSpeed = 1.0f;              // Walk speed multiplier
        float militiaRunSpeed = 1.5f;               // Run speed multiplier

        // Crier settings
        uint32 crierBaseIntervalMs = 45000;         // Base time between announcements
        uint32 crierUrgentIntervalMs = 20000;       // Time between urgent announcements
        uint32 crierPanicIntervalMs = 10000;        // Time between panic announcements

        // Gameobject settings
        uint32 maxBarricadesPerZone = 15;
        uint32 maxDebrisPerZone = 20;
        uint32 barricadeRespawnMs = 300000;         // 5 minutes

        // Banner settings
        bool instantBannerSwap = false;             // Instant or animated swap
        uint32 bannerSwapDelayMs = 5000;            // Delay for animated swap

        // Construction settings
        bool constructionEnabled = true;
        bool interactiveObjectsEnabled = true;
        uint32 constructionUpdateIntervalMs = 1500; // Worker behavior update rate
        uint32 workDurationMs = 4000;               // Time spent working before next lumber trip
        uint32 maxWorkersPerZone = 8;

        // Construction decay settings (when zone cools down)
        float constructionDecayRatePerSec = 5.0f;   // HP lost per second while decaying
        uint32 constructionDecayCheckMs = 2000;      // How often to check decay
    };

    // ==========================================================================
    // ZONE PREPARATION MANAGER
    // ==========================================================================
    class ZonePreparationManager
    {
    public:
        static ZonePreparationManager* instance();

        // Initialization
        void Initialize();
        void LoadFromConfig();
        void LoadDialogues();
        void LoadDialoguePairs();
        void LoadMilitiaSpawnPoints();
        void LoadTownCriers();
        void LoadGameObjectTemplates();
        void LoadFactionBanners();
        void LoadConstructionSites();
        void LoadWorkerTemplates();
        void LoadInteractiveObjects();

        // Update loop
        void Update(uint32 diff);

        // Phase change handlers (called by ZoneStateManager)
        void OnPhaseChanged(uint32 zoneId, ZonePhase oldPhase, ZonePhase newPhase);
        void OnZoneControlChanged(uint32 zoneId, ControlState oldControl, ControlState newControl);
        void OnZoneFactionChanged(uint32 zoneId, TeamId oldFaction, TeamId newFaction);

        // Manual triggers
        void TriggerDialogue(uint32 zoneId, ZonePhase phase);
        void TriggerDialoguePair(uint32 zoneId, ZonePhase phase);
        void SpawnMilitia(uint32 zoneId, ZonePhase phase);
        void DespawnMilitia(uint32 zoneId);
        void UpdateCrierUrgency(uint32 zoneId, ZonePhase phase);
        void MakeCrierAnnouncement(uint32 zoneId);

        // Gameobject management
        void SpawnPhaseGameObjects(uint32 zoneId, ZonePhase phase);
        void DespawnPhaseGameObjects(uint32 zoneId, ZonePhase phase);
        void SpawnBarricades(uint32 zoneId);
        void DespawnBarricades(uint32 zoneId);
        void UpdateBanners(uint32 zoneId, ControlState control, TeamId faction);

        // Construction system
        void SpawnConstructionWorkers(uint32 zoneId, ZonePhase phase);
        void UpdateConstructionWorkers(uint32 zoneId, uint32 diff);
        void CompleteConstruction(uint32 zoneId, uint32 siteId);
        void DespawnConstructionWorkers(uint32 zoneId, ZonePhase phase);
        void StartConstructionDecay(uint32 zoneId);
        void DecayConstructions(uint32 zoneId, uint32 diffMs);

        // Interactive objects (capture points, traps, etc.)
        void SpawnInteractiveObjects(uint32 zoneId, ZonePhase phase);
        void DespawnInteractiveObjects(uint32 zoneId, ZonePhase phase);

        // NPC behaviors
        void AssignPreparationActivity(Creature* npc, PreparationActivity activity);
        void UpdateMilitiaBehaviors(uint32 diff);
        void MakeNPCCarryCrate(Creature* npc, Position const& destination);
        void MakeNPCRun(Creature* npc, Position const& destination);
        void MakeNPCGather(Creature* npc, Position const& gatherPoint);
        void MakeNPCWatch(Creature* npc, float angle);

        // State access
        ZonePreparationState* GetPreparationState(uint32 zoneId);
        const PreparationConfig& GetConfig() const { return m_config; }

        // Utility
        static const char* ActivityToString(PreparationActivity activity);
        static const char* CrierUrgencyToString(CrierUrgency urgency);

    private:
        ZonePreparationManager() = default;
        ~ZonePreparationManager() = default;

        // Loaded data
        std::vector<NPCDialogue> m_dialogues;
        std::vector<NPCDialoguePair> m_dialoguePairs;
        std::vector<MilitiaSpawnPoint> m_militiaSpawns;
        std::vector<TownCrierConfig> m_crierConfigs;
        std::vector<PrepGameObjectTemplate> m_gameObjectTemplates;
        std::vector<FactionBannerConfig> m_bannerConfigs;
        std::vector<ConstructionSite> m_constructionSites;
        std::vector<WorkerTemplate> m_workerTemplates;
        std::vector<InteractiveObjectTemplate> m_interactiveObjects;

        // Active state per zone
        std::map<uint32, ZonePreparationState> m_zoneStates;

        // Configuration
        PreparationConfig m_config;

        // Timers
        uint32 m_dialogueTimer = 0;
        uint32 m_militiaTimer = 0;
        uint32 m_gameObjectTimer = 0;
        uint32 m_crierTimer = 0;
        uint32 m_constructionTimer = 0;
        uint32 m_decayTimer = 0;

        // Internal methods
        ZonePreparationState& GetOrCreateState(uint32 zoneId);
        std::vector<NPCDialogue*> GetDialoguesForZone(uint32 zoneId, ZonePhase phase, uint8 dbFaction);
        std::vector<NPCDialoguePair*> GetDialoguePairsForZone(uint32 zoneId, ZonePhase phase, uint8 dbFaction);
        TownCrierConfig* GetCrierForZone(uint32 zoneId);
        std::vector<TownCrierConfig*> GetCriersForZone(uint32 zoneId, uint8 dbFaction);  // Multi-crier support
        std::vector<MilitiaSpawnPoint*> GetMilitiaForZone(uint32 zoneId, ZonePhase phase);
        std::vector<PrepGameObjectTemplate*> GetGameObjectsForZone(uint32 zoneId, ZonePhase phase);
        std::vector<FactionBannerConfig*> GetBannersForZone(uint32 zoneId);

        // Construction helpers
        std::vector<ConstructionSite*> GetConstructionSitesForZone(uint32 zoneId, ZonePhase phase);
        std::vector<InteractiveObjectTemplate*> GetInteractiveObjectsForZone(uint32 zoneId, uint8 phase);
        WorkerTemplate* GetWorkerTemplateForFaction(uint8 faction);
        std::string GetRandomLine(const std::vector<std::string>& lines);
        static std::vector<std::string> SplitPipeDelimited(const std::string& input);
        void WorkerSay(Creature* worker, const std::vector<std::string>& lines);
        void EquipWorkerItem(Creature* worker, uint32 displayId);
        void ClearWorkerEquipment(Creature* worker);

        Creature* FindNPCOfType(uint32 zoneId, const std::string& type, Player* centerPlayer,
                                uint8 dbFaction, float radius);
        std::vector<Creature*> FindNPCsOfType(uint32 zoneId, const std::string& type,
                                              Player* centerPlayer, uint8 dbFaction, float radius);
        Creature* FindConfiguredCrier(const TownCrierConfig& config, Player* centerPlayer,
                                      uint8 dbFaction);
        Creature* FindCreatureByGuid(ObjectGuid guid);

        // Location detection for zones with multiple towns (Zone 17: Crossroads/Ratchet/Camp Taurajo)
        std::string GetLocationNameForPosition(uint32 zoneId, Position const& pos);
        bool CanTriggerDialogue(const NPCDialogue& dialogue, const ZonePreparationState& state);
        bool CanTriggerDialoguePair(const NPCDialoguePair& pair, const ZonePreparationState& state);

        void ExecuteDialogue(Creature* speaker, const NPCDialogue& dialogue);
        void ExecuteDialoguePair(Creature* speakerA, Creature* speakerB, const NPCDialoguePair& pair);

        // Scheduled dialogue management
        void ScheduleDialogueLine(uint32 zoneId, ObjectGuid speakerGuid, const std::string& text,
                                  uint32 delayMs, bool isYell, uint32 conversationId);
        void ProcessScheduledDialogues(uint32 diff);
        void CancelConversation(uint32 zoneId, uint32 conversationId);

        // Delayed spawn management (spawnDelayMs support)
        void ProcessPendingSpawns(uint32 diff);
        Player* FindPlayerInZone(uint32 zoneId);

        void EmitPreparationEvent(uint32 zoneId, const std::string& eventType,
                                   const std::map<std::string, std::string>& data);

        // Default data loading (fallbacks when DB tables are empty)
        void LoadDefaultDialogues();
        void LoadDefaultDialoguePairs();
    };

    // ==========================================================================
    // NPC TYPE DEFINITIONS
    // ==========================================================================
    struct NPCTypeDefinition
    {
        // Maps NPC type strings to creature flags/entries
        static bool IsGuard(Creature* creature);
        static bool IsMerchant(Creature* creature);
        static bool IsCivilian(Creature* creature);
        static bool IsMilitia(Creature* creature);
        static bool IsCrier(Creature* creature);
        static bool IsInnkeeper(Creature* creature);

        static std::string GetNPCType(Creature* creature);
    };

    // ==========================================================================
    // DIALOGUE FLAVOR TEXT DEFAULTS
    // ==========================================================================
    struct DefaultDialogues
    {
        // Phase 1: UNEASY - Nervous gossip
        static const std::vector<std::string>& GetUneasyGuardDialogues();
        static const std::vector<std::string>& GetUneasyMerchantDialogues();
        static const std::vector<std::string>& GetUneasyCivilianDialogues();

        // Phase 2: MOBILIZING - Urgent preparation
        static const std::vector<std::string>& GetMobilizingGuardDialogues();
        static const std::vector<std::string>& GetMobilizingMerchantDialogues();
        static const std::vector<std::string>& GetMobilizingCivilianDialogues();

        // Town Crier announcements by urgency
        static const std::vector<std::string>& GetCrierCalmAnnouncements();
        static const std::vector<std::string>& GetCrierAlertAnnouncements();
        static const std::vector<std::string>& GetCrierUrgentAnnouncements();
        static const std::vector<std::string>& GetCrierPanicAnnouncements();

        // Conversation pairs (alternating lines)
        static const std::vector<std::vector<std::string>>& GetGuardToGuardConversations();
        static const std::vector<std::vector<std::string>>& GetMerchantToMerchantConversations();
        static const std::vector<std::vector<std::string>>& GetCivilianToGuardConversations();
    };
}

#define sECZonePreparationMgr ECGuardSystem::ZonePreparationManager::instance()

#endif // EC_GUARD_SYSTEM_ZONE_PREPARATION_H
