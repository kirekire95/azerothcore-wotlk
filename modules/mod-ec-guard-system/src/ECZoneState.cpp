/*
 * EC Guard System - Zone State Tracking Implementation
 * GTA-style heat/threat system with phase progression
 */

#include "ECZoneState.h"
#include "ECZoneEnvironment.h"
#include "ECZonePreparation.h"
#include "ECGuardSystemManager.h"
#include "ECGuardSystemBridge.h"
#ifdef MOD_EC_WORLD_EVENTS
#include "ECWorldEventManager.h"
#endif
#include "Player.h"
#include "Creature.h"
#include "CreatureAI.h"
#include "Map.h"
#include "MapMgr.h"
#include "ObjectMgr.h"
#include "SpellAuras.h"
#include "DatabaseEnv.h"
#include "WorldSession.h"
#include "WorldSessionMgr.h"
#include "World.h"
#include "Chat.h"
#include "Config.h"
#include "DBCStores.h"
#include "ObjectAccessor.h"
#include <chrono>
#include <cstdarg>
#include <cmath>
#include <unordered_map>

#ifdef MOD_PLAYERBOTS
#include "PlayerbotAI.h"
#include "PlayerbotMgr.h"
#include "RandomPlayerbotMgr.h"
#endif

namespace ECGuardSystem
{
    namespace
    {
        // Retained only for the one-time persisted phantom-state recovery below.
        constexpr uint32 ASTRANAAR_ZONE_ID = 331;

        bool MatchesStrategicWantedJurisdiction(ZoneState const& zone,
                                                WantedState const& wanted,
                                                Player const* player)
        {
            if (!wanted.IsWanted() || wanted.crimeZoneId != zone.zoneId ||
                wanted.crimeFaction != zone.owningFaction || !player ||
                player->GetZoneId() != zone.zoneId)
            {
                return false;
            }

            // A legacy strategic state is keyed only by zone/faction. When its
            // corresponding dispatch row names a settlement area, enforce the
            // same area here so Razor Hill, Astranaar, Crossroads, and every
            // other mapped town cannot drive events elsewhere in the zone.
            ZoneGuardConfig const* localConfig =
                sECGuardSystemMgr->GetExactZoneConfig(zone.zoneId, zone.owningFaction);
            if (localConfig && localConfig->areaId != 0)
                return wanted.crimeAreaId == localConfig->areaId &&
                    player->GetAreaId() == localConfig->areaId;

            return true;
        }
    }

    // ==========================================================================
    // SINGLETON
    // ==========================================================================

    ZoneStateManager* ZoneStateManager::instance()
    {
        static ZoneStateManager instance;
        return &instance;
    }

    // ==========================================================================
    // INITIALIZATION
    // ==========================================================================

    void ZoneStateManager::Initialize()
    {
        ECG_LOG_INFO("ZoneStateManager initializing...");
        LoadFromConfig();
        LoadZoneStates();            // Load zone configurations from world DB
        LoadFactionResponseGroups(); // Load elite response groups
        LoadSavedZoneStates();       // Restore persisted zone states from characters DB

        // Initialize the Preparation Phase system ("Shatter Before the Storm")
        sECZonePreparationMgr->Initialize();

        ECG_LOG_INFO("ZoneStateManager initialized with %zu zones tracked", m_zoneStates.size());
    }

    void ZoneStateManager::LoadFromConfig()
    {
        // Load configuration from worldserver.conf
        m_config.heatDecayIntervalMs = sConfigMgr->GetOption<uint32>("ECGuardSystem.Zone.HeatDecayIntervalMs", 10000);
        m_config.heatDecayDelayMs = sConfigMgr->GetOption<uint32>("ECGuardSystem.Zone.HeatDecayDelayMs", 60000);
        m_config.heatDecayRatePerSec = sConfigMgr->GetOption<uint32>("ECGuardSystem.Zone.HeatDecayRatePerSec", 50);

        m_config.uneasyThreshold = sConfigMgr->GetOption<uint32>("ECGuardSystem.Zone.UneasyThreshold", 300);
        m_config.mobilizingThreshold = sConfigMgr->GetOption<uint32>("ECGuardSystem.Zone.MobilizingThreshold", 750);
        m_config.impactThreshold = sConfigMgr->GetOption<uint32>("ECGuardSystem.Zone.ImpactThreshold", 1500);
        m_config.maxHeatPoints = std::max(m_config.impactThreshold,
            sConfigMgr->GetOption<uint32>("ECGuardSystem.Zone.MaxHeatPoints", 5000));
        m_config.phaseBroadcastsEnabled = sConfigMgr->GetOption<bool>(
            "ECGuardSystem.Zone.PhaseBroadcastsEnabled", false);

        m_config.contestedThreshold = sConfigMgr->GetOption<float>("ECGuardSystem.Zone.ContestedThreshold", 0.5f);
        m_config.fallenThreshold = sConfigMgr->GetOption<float>("ECGuardSystem.Zone.FallenThreshold", 0.1f);

        m_config.bountyHuntersEnabled = sConfigMgr->GetOption<bool>("ECGuardSystem.Zone.BountyHuntersEnabled", false);
        m_config.bountyHunterSpawnStars = sConfigMgr->GetOption<uint32>("ECGuardSystem.Zone.BountyHunterSpawnStars", 3);
        m_config.maxBountyHuntersPerPlayer = sConfigMgr->GetOption<uint32>("ECGuardSystem.Zone.MaxBountyHuntersPerPlayer", 2);
        m_config.bountyHunterDespawnMs = sConfigMgr->GetOption<uint32>("ECGuardSystem.Zone.BountyHunterDespawnMs", 600000);

        m_config.eliteResponseEnabled = sConfigMgr->GetOption<bool>("ECGuardSystem.Zone.EliteResponseEnabled", true);
        m_config.eliteResponseMinDelayMs = sConfigMgr->GetOption<uint32>("ECGuardSystem.Zone.EliteResponseMinDelayMs", 180000);
        m_config.eliteResponseMaxDelayMs = sConfigMgr->GetOption<uint32>("ECGuardSystem.Zone.EliteResponseMaxDelayMs", 900000);
        m_config.guardsKilledForFastResponse = sConfigMgr->GetOption<uint32>("ECGuardSystem.Zone.GuardsKilledForFastResponse", 20);
        m_config.massacreSeverityMultiplier = sConfigMgr->GetOption<float>("ECGuardSystem.Zone.MassacreSeverityMultiplier", 0.5f);

        ECG_LOG_DEBUG("Zone config loaded: decay=%ums, thresholds=%u/%u/%u, maxHeat=%u, synthetic broadcasts=%s",
            m_config.heatDecayIntervalMs, m_config.uneasyThreshold,
            m_config.mobilizingThreshold, m_config.impactThreshold, m_config.maxHeatPoints,
            m_config.phaseBroadcastsEnabled ? "enabled" : "disabled");
    }

    void ZoneStateManager::LoadZoneStates()
    {
        // Load initial zone configurations from database
        QueryResult result = WorldDatabase.Query(
            "SELECT zone_id, faction, initial_guard_count, zone_name "
            "FROM ec_guard_zone_config WHERE enabled = 1");

        if (!result)
        {
            ECG_LOG_WARN("No zone configurations found in ec_guard_zone_config table");
            return;
        }

        uint32 count = 0;
        do
        {
            Field* fields = result->Fetch();
            uint32 zoneId = fields[0].Get<uint32>();
            uint8 faction = fields[1].Get<uint8>();
            uint32 guardCount = fields[2].Get<uint32>();
            std::string zoneName = fields[3].Get<std::string>();

            ZoneState& zone = m_zoneStates[zoneId];
            zone.zoneId = zoneId;
            zone.owningFaction = GuardDbFactionToTeamId(faction);
            zone.initialGuardCount = guardCount;
            zone.guardsAlive = guardCount;
            zone.zoneName = zoneName;

            ECG_LOG_DEBUG("Loaded zone %u (%s): faction=%u, guards=%u",
                zoneId, zoneName.c_str(), faction, guardCount);

            count++;
        } while (result->NextRow());

        ECG_LOG_INFO("Loaded %u zone configurations", count);
    }

    // ==========================================================================
    // UPDATE
    // ==========================================================================

    void ZoneStateManager::Update(uint32 diff)
    {
        // Heat decay processing
        m_heatDecayTimer += diff;
        if (m_heatDecayTimer >= m_config.heatDecayIntervalMs)
        {
            ProcessHeatDecay(m_heatDecayTimer);
            m_heatDecayTimer = 0;
        }

        // Bounty hunter updates
        m_bountyHunterUpdateTimer += diff;
        if (m_bountyHunterUpdateTimer >= 5000) // Every 5 seconds
        {
            UpdateBountyHunters(m_bountyHunterUpdateTimer);
            m_bountyHunterUpdateTimer = 0;
        }

        // Phase transition checks
        m_phaseCheckTimer += diff;
        if (m_phaseCheckTimer >= 2000) // Every 2 seconds
        {
            CheckPhaseTransitions(m_phaseCheckTimer);
            m_phaseCheckTimer = 0;
        }

        // Update phase elapsed time for all active zones
        for (auto& [zoneId, zone] : m_zoneStates)
        {
            if (zone.IsUnderThreat())
            {
                zone.phaseElapsedMs += diff;
            }
        }

        // Update Preparation Phase system (dialogues, militia, criers, etc.)
        sECZonePreparationMgr->Update(diff);

        // Periodic persistence
        m_persistenceTimer += diff;
        if (m_persistenceTimer >= PERSISTENCE_INTERVAL_MS)
        {
            SaveAllZoneStates();
            m_persistenceTimer = 0;
        }
    }

    // ==========================================================================
    // ZONE STATE ACCESS
    // ==========================================================================

    ZoneState* ZoneStateManager::GetZoneState(uint32 zoneId)
    {
        std::lock_guard<std::recursive_mutex> lock(m_stateMutex);
        auto it = m_zoneStates.find(zoneId);
        if (it != m_zoneStates.end())
            return &it->second;
        return nullptr;
    }

    const ZoneState* ZoneStateManager::GetZoneState(uint32 zoneId) const
    {
        std::lock_guard<std::recursive_mutex> lock(m_stateMutex);
        auto it = m_zoneStates.find(zoneId);
        if (it != m_zoneStates.end())
            return &it->second;
        return nullptr;
    }

    ZoneState* ZoneStateManager::GetOrCreateZoneState(uint32 zoneId, TeamId faction)
    {
        std::lock_guard<std::recursive_mutex> lock(m_stateMutex);
        auto it = m_zoneStates.find(zoneId);
        if (it != m_zoneStates.end())
            return &it->second;

        // Create new zone state
        ZoneState& zone = m_zoneStates[zoneId];
        zone.zoneId = zoneId;
        zone.owningFaction = faction;
        zone.initialGuardCount = 10; // Default guard count
        zone.guardsAlive = 10;

        // Try to get zone name from AreaTableEntry
        if (AreaTableEntry const* area = sAreaTableStore.LookupEntry(zoneId))
        {
            zone.zoneName = area->area_name[0]; // English name
        }
        else
        {
            zone.zoneName = "Unknown Zone";
        }

        ECG_LOG_DEBUG("Created dynamic zone state for zone %u (%s)",
            zoneId, zone.zoneName.c_str());

        return &zone;
    }

    std::vector<const ZoneState*> ZoneStateManager::GetAllActiveZones() const
    {
        std::lock_guard<std::recursive_mutex> lock(m_stateMutex);
        std::vector<const ZoneState*> active;
        for (const auto& [zoneId, zone] : m_zoneStates)
        {
            if (zone.IsUnderThreat())
                active.push_back(&zone);
        }
        return active;
    }

    std::vector<const ZoneState*> ZoneStateManager::GetFallenZones() const
    {
        std::lock_guard<std::recursive_mutex> lock(m_stateMutex);
        std::vector<const ZoneState*> fallen;
        for (const auto& [zoneId, zone] : m_zoneStates)
        {
            if (zone.IsFallen())
                fallen.push_back(&zone);
        }
        return fallen;
    }

    std::vector<ZoneState> ZoneStateManager::GetAllActiveZonesCopy() const
    {
        std::lock_guard<std::recursive_mutex> lock(m_stateMutex);
        std::vector<ZoneState> active;
        for (const auto& [zoneId, zone] : m_zoneStates)
        {
            if (zone.IsUnderThreat())
                active.push_back(zone);  // Copy
        }
        return active;
    }

    std::vector<ZoneState> ZoneStateManager::GetFallenZonesCopy() const
    {
        std::lock_guard<std::recursive_mutex> lock(m_stateMutex);
        std::vector<ZoneState> fallen;
        for (const auto& [zoneId, zone] : m_zoneStates)
        {
            if (zone.IsFallen())
                fallen.push_back(zone);  // Copy
        }
        return fallen;
    }

    // ==========================================================================
    // ZONE EVENTS
    // ==========================================================================

    void ZoneStateManager::OnZoneCrime(uint32 zoneId, ObjectGuid criminalGuid, uint32 heatPoints)
    {
        TeamId faction = sECGuardSystemMgr->GetZoneFaction(zoneId);
        ZoneState* zone = GetOrCreateZoneState(zoneId, faction);
        if (!zone) return;

        ZonePhase oldPhase = zone->currentPhase;

        zone->AddCriminal(criminalGuid);

        // Track old heat before adding
        uint32 oldHeat = zone->heatPoints;
        zone->AddHeat(heatPoints, m_config.maxHeatPoints);

        // Emit heat change to bridge for UI updates
        if (zone->heatPoints != oldHeat)
        {
            sGuardSystemBridge->EmitZoneHeatChanged(
                zoneId, zone->zoneName, oldHeat, zone->heatPoints,
                static_cast<uint8>(zone->currentPhase), static_cast<uint8>(zone->alarmState));
        }

        // Track crime position for civilian flee behavior
        if (Player* criminal = ObjectAccessor::FindPlayer(criminalGuid))
        {
            zone->lastCrimePosition = criminal->GetPosition();
            zone->hasValidCrimePosition = true;
        }

        ECG_LOG_DEBUG("Zone %u: Crime by %s, +%u heat (total: %u, threat: %u)",
            zoneId, criminalGuid.ToString().c_str(), heatPoints,
            zone->heatPoints, zone->threatLevel);

        // Update alarm state based on heat
        UpdateAlarmState(*zone);

        // Check for phase transition
        ZonePhase newPhase = DeterminePhase(*zone);
        if (newPhase != oldPhase)
        {
            TransitionPhase(zoneId, newPhase);
        }

        // Check if we should spawn bounty hunters
        if (m_config.bountyHuntersEnabled)
        {
            WantedState* wanted = sECGuardSystemMgr->GetWantedState(criminalGuid);
            if (wanted && static_cast<uint8>(wanted->stars) >= m_config.bountyHunterSpawnStars)
            {
                auto hunters = GetAllBountyHuntersForTarget(criminalGuid);
                if (hunters.size() < m_config.maxBountyHuntersPerPlayer)
                {
                    SpawnBountyHunter(criminalGuid, zoneId);
                }
            }
        }
    }

    void ZoneStateManager::OnZoneGuardKilled(uint32 zoneId, ObjectGuid killerGuid)
    {
        ZoneState* zone = GetZoneState(zoneId);
        if (!zone) return;

        ZonePhase oldPhase = zone->currentPhase;
        ControlState oldControl = zone->controlState;

        zone->OnGuardKilled();

        ECG_LOG_INFO("Zone %u: Guard killed by %s (alive: %u/%u, survival: %.1f%%)",
            zoneId, killerGuid.ToString().c_str(),
            zone->guardsAlive, zone->initialGuardCount,
            zone->GetGuardSurvivalRate() * 100.0f);

        // Update alarm and control state
        UpdateAlarmState(*zone);
        UpdateControlState(*zone);

        // Check for control state change
        if (zone->controlState != oldControl)
        {
            ECG_LOG_WARN("Zone %u: Control state changed from %s to %s",
                zoneId, ControlStateToString(oldControl),
                ControlStateToString(zone->controlState));

            if (zone->controlState == ControlState::FALLEN)
            {
                // Zone has fallen! Trigger elite response after delay
                ECG_LOG_WARN("Zone %u (%s) has FALLEN! Elite response scheduled",
                    zoneId, zone->zoneName.c_str());

                // Trigger elite response (bridges to World Events module)
                if (m_config.eliteResponseEnabled)
                {
                    // This will be bridged to World Events module
                    TriggerEliteResponse(zoneId);
                }
            }
        }

        // Check for phase transition
        ZonePhase newPhase = DeterminePhase(*zone);
        if (newPhase != oldPhase)
        {
            TransitionPhase(zoneId, newPhase);
        }
    }

    void ZoneStateManager::OnZoneGuardSpawned(uint32 zoneId, ObjectGuid guardGuid)
    {
        ZoneState* zone = GetZoneState(zoneId);
        if (!zone) return;

        zone->OnGuardSpawned();

        ECG_LOG_DEBUG("Zone %u: Guard spawned (alive: %u/%u)",
            zoneId, zone->guardsAlive, zone->initialGuardCount);

        // Update control state (guards returning might restore control)
        ControlState oldControl = zone->controlState;
        UpdateControlState(*zone);

        if (zone->controlState != oldControl)
        {
            ECG_LOG_INFO("Zone %u: Control state improved from %s to %s",
                zoneId, ControlStateToString(oldControl),
                ControlStateToString(zone->controlState));
        }
    }

    void ZoneStateManager::OnPlayerEnterZone(ObjectGuid playerGuid, uint32 zoneId)
    {
        // Track only criminals wanted by this exact settlement jurisdiction. A
        // global wanted flag must not enroll the player in every zone they enter.
        std::optional<WantedState> wanted = sECGuardSystemMgr->GetWantedStateCopy(playerGuid);
        Player* player = ObjectAccessor::FindPlayer(playerGuid);
        if (wanted)
        {
            // Hold lock for entire operation to prevent data races
            std::lock_guard<std::recursive_mutex> lock(m_stateMutex);

            auto it = m_zoneStates.find(zoneId);
            if (it != m_zoneStates.end() &&
                MatchesStrategicWantedJurisdiction(it->second, *wanted, player))
            {
                it->second.AddCriminal(playerGuid);
                ECG_LOG_DEBUG("Wanted player %s entered matching jurisdiction: zone=%u faction=%u",
                    playerGuid.ToString().c_str(), zoneId,
                    static_cast<uint8>(it->second.owningFaction));

                // Update bounty hunter tracking (safe - we hold the lock)
                for (auto& [hunterGuid, hunter] : m_bountyHunters)
                {
                    if (hunter.targetGuid == playerGuid)
                    {
                        hunter.lastKnownZoneId = zoneId;
                    }
                }
            }
            else if (it != m_zoneStates.end() && wanted->IsWanted())
            {
                ECG_LOG_DEBUG("Wanted player %s not enrolled in strategic state: zone=%u currentArea=%u crimeZone=%u crimeArea=%u crimeFaction=%u owner=%u",
                    playerGuid.ToString().c_str(), zoneId, player ? player->GetAreaId() : 0,
                    wanted->crimeZoneId, wanted->crimeAreaId,
                    static_cast<uint8>(wanted->crimeFaction),
                    static_cast<uint8>(it->second.owningFaction));
            }
        }
    }

    void ZoneStateManager::OnPlayerLeaveZone(ObjectGuid playerGuid, uint32 zoneId)
    {
        ZoneState* zone = GetZoneState(zoneId);
        if (zone)
        {
            zone->RemoveCriminal(playerGuid);

            // If no more criminals and zone is under threat, start recovery
            if (zone->activeCriminals.empty() && zone->IsUnderThreat())
            {
                ECG_LOG_DEBUG("Zone %u: Last criminal left, beginning recovery",
                    zoneId);
            }
        }
    }

    // ==========================================================================
    // BOUNTY HUNTER MANAGEMENT
    // ==========================================================================

    void ZoneStateManager::SpawnBountyHunter(ObjectGuid targetGuid, uint32 zoneId)
    {
        Player* target = ObjectAccessor::FindPlayer(targetGuid);
        std::optional<WantedState> wanted = sECGuardSystemMgr->GetWantedStateCopy(targetGuid);
        if (!target || !target->IsInWorld() || !target->IsAlive() ||
            !wanted || !wanted->IsWanted() || target->GetZoneId() != zoneId ||
            wanted->crimeZoneId != zoneId)
        {
            ECG_LOG_DEBUG("Skipping stale bounty hunter spawn for %s: requested zone %u",
                targetGuid.ToString().c_str(), zoneId);
            return;
        }

        std::lock_guard<std::recursive_mutex> lock(m_stateMutex);

        // A criminal can generate several crime callbacks per second. If a
        // placement is temporarily impossible, bound both the retry rate and
        // its logging instead of retrying and emitting an error on every crime.
        static std::unordered_map<uint32, std::chrono::steady_clock::time_point>
            spawnFailureBackoff;
        auto now = std::chrono::steady_clock::now();
        uint32 targetCounter = targetGuid.GetCounter();
        auto backoffIt = spawnFailureBackoff.find(targetCounter);
        if (backoffIt != spawnFailureBackoff.end())
        {
            if (now < backoffIt->second)
                return;
            spawnFailureBackoff.erase(backoffIt);
        }

        ZoneState* zone = GetZoneState(zoneId);
        if (!zone) return;

        if (!MatchesStrategicWantedJurisdiction(*zone, *wanted, target))
        {
            ECG_LOG_WARN("Skipping mismatched bounty hunter spawn for %s: zone=%u currentArea=%u crimeZone=%u crimeArea=%u crimeFaction=%u owner=%u",
                targetGuid.ToString().c_str(), zoneId, target->GetAreaId(),
                wanted->crimeZoneId, wanted->crimeAreaId,
                static_cast<uint8>(wanted->crimeFaction), static_cast<uint8>(zone->owningFaction));
            return;
        }

        // Get bounty hunter creature entry based on faction
        uint32 hunterEntry = 0;
        const ZoneGuardConfig* config = sECGuardSystemMgr->GetZoneConfig(zoneId, zone->owningFaction);
        if (config && !config->eliteEntries.empty())
        {
            // Use elite guard as bounty hunter for now
            hunterEntry = config->eliteEntries[urand(0, config->eliteEntries.size() - 1)];
        }

        if (hunterEntry == 0)
        {
            ECG_LOG_WARN("No bounty hunter entry configured for zone %u", zoneId);
            return;
        }

        Map* map = target->GetMap();
        if (!map) return;

        // Raw radial X/Y plus GetHeight can produce INVALID_HEIGHT on cliffs,
        // buildings, unloaded terrain, and settlement edges. Ask the core for
        // collision-safe positions, keep the result in the issuing
        // jurisdiction, and progressively try nearer candidates.
        constexpr uint32 maxSpawnAttempts = 8;
        uint32 invalidCandidates = 0;
        uint32 wrongJurisdictionCandidates = 0;
        uint32 summonFailures = 0;
        Creature* hunter = nullptr;

        for (uint32 attempt = 0; attempt < maxSpawnAttempts && !hunter; ++attempt)
        {
            float angle = frand(0.0f, 2.0f * static_cast<float>(M_PI));
            float distance = 45.0f - 5.0f * static_cast<float>(attempt);
            Position spawnPosition = target->GetFirstCollisionPosition(distance, angle);

            if (!spawnPosition.IsPositionValid() ||
                std::fabs(spawnPosition.GetPositionZ() - target->GetPositionZ()) > 30.0f)
            {
                ++invalidCandidates;
                continue;
            }

            uint32 candidateZoneId = 0;
            uint32 candidateAreaId = 0;
            map->GetZoneAndAreaId(target->GetPhaseMask(), candidateZoneId, candidateAreaId,
                spawnPosition.GetPositionX(), spawnPosition.GetPositionY(),
                spawnPosition.GetPositionZ());
            if (candidateZoneId != zoneId ||
                (config->areaId != 0 && candidateAreaId != config->areaId))
            {
                ++wrongJurisdictionCandidates;
                continue;
            }

            hunter = target->SummonCreature(
                hunterEntry,
                spawnPosition.GetPositionX(),
                spawnPosition.GetPositionY(),
                spawnPosition.GetPositionZ(),
                target->GetOrientation(),
                TEMPSUMMON_TIMED_OR_DEAD_DESPAWN,
                m_config.bountyHunterDespawnMs);
            if (!hunter)
                ++summonFailures;
        }

        if (!hunter)
        {
            spawnFailureBackoff[targetCounter] = now + std::chrono::seconds(30);
            ECG_LOG_WARN("Failed to spawn bounty hunter %u for target %s after %u collision-safe candidates (invalid=%u, jurisdiction=%u, summon=%u, map=%u, zone=%u, area=%u); retry muted for 30s",
                hunterEntry, targetGuid.ToString().c_str(), maxSpawnAttempts,
                invalidCandidates, wrongJurisdictionCandidates, summonFailures,
                map->GetId(), zoneId, target->GetAreaId());
            return;
        }

        spawnFailureBackoff.erase(targetCounter);

        // Track the bounty hunter
        ZoneBountyHunter& bh = m_bountyHunters[hunter->GetGUID()];
        bh.hunterGuid = hunter->GetGUID();
        bh.targetGuid = targetGuid;
        bh.hunterEntry = hunterEntry;
        bh.spawnZoneId = zoneId;
        bh.spawnCrimeAreaId = wanted->crimeAreaId;
        bh.spawnCrimeFaction = wanted->crimeFaction;
        bh.spawnMapId = map->GetId();
        bh.spawnInstanceId = map->GetInstanceId();
        bh.lastKnownZoneId = zoneId;
        bh.lastKnownX = target->GetPositionX();
        bh.lastKnownY = target->GetPositionY();
        bh.lastKnownZ = target->GetPositionZ();
        bh.spawnTime = std::chrono::steady_clock::now();
        bh.lastUpdateTime = bh.spawnTime;

        zone->activeBountyHunters.push_back(hunter->GetGUID());

        // Make hunter aggressive and start chasing
        hunter->SetReactState(REACT_AGGRESSIVE);
        hunter->AI()->AttackStart(target);

        // Announce the bounty hunter
        if (hunter->IsInWorld() && !hunter->IsDuringRemoveFromWorld() && hunter->IsAlive())
            hunter->Yell("I've found you, criminal! Your bounty is mine!", LANG_UNIVERSAL);

        // Emit to bridge for UI
        sGuardSystemBridge->EmitBountyHunterSpawned(
            hunter->GetGUID().GetCounter(),  // hunterGuid
            targetGuid.GetCounter(),         // targetGuid
            target->GetName(),               // targetName
            hunterEntry,                     // hunterEntry
            hunter->GetName()                // hunterName
        );

        ECG_LOG_INFO("Bounty hunter %u spawned for target %s in zone %u",
            hunterEntry, target->GetName().c_str(), zoneId);
    }

    void ZoneStateManager::UpdateBountyHunters(uint32 diff)
    {
        std::lock_guard<std::recursive_mutex> lock(m_stateMutex);
        auto now = std::chrono::steady_clock::now();
        std::vector<ObjectGuid> toRemove;

        for (auto& [hunterGuid, hunter] : m_bountyHunters)
        {
            // Check for despawn timeout
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                now - hunter.spawnTime).count();
            if (elapsed >= m_config.bountyHunterDespawnMs)
            {
                ECG_LOG_DEBUG("Bounty hunter %s timed out after %lu ms",
                    hunterGuid.ToString().c_str(), elapsed);
                toRemove.push_back(hunterGuid);
                continue;
            }

            // Find the target player first (null check required)
            Player* target = ObjectAccessor::FindPlayer(hunter.targetGuid);
            if (!target)
            {
                ECG_LOG_DEBUG("Bounty hunter %s target player not found",
                    hunterGuid.ToString().c_str());
                toRemove.push_back(hunterGuid);
                continue;
            }

            // Clearing or changing the originating jurisdiction must terminate
            // this pursuit. Otherwise a legacy hunter can survive a new crime
            // state or follow from Astranaar/Crossroads into another same-zone
            // settlement.
            std::optional<WantedState> wanted =
                sECGuardSystemMgr->GetWantedStateCopy(hunter.targetGuid);
            ZoneState* responseZone = GetZoneState(hunter.spawnZoneId);
            if (!wanted || wanted->crimeAreaId != hunter.spawnCrimeAreaId ||
                wanted->crimeFaction != hunter.spawnCrimeFaction || !responseZone ||
                !MatchesStrategicWantedJurisdiction(*responseZone, *wanted, target))
            {
                ECG_LOG_DEBUG("Bounty hunter %s target left issuing jurisdiction: spawnZone=%u spawnArea=%u spawnFaction=%u currentZone=%u currentArea=%u",
                    hunterGuid.ToString().c_str(), hunter.spawnZoneId,
                    hunter.spawnCrimeAreaId, static_cast<uint8>(hunter.spawnCrimeFaction),
                    target->GetZoneId(), target->GetAreaId());
                toRemove.push_back(hunterGuid);
                continue;
            }

            // A response belongs to the jurisdiction and map where it spawned.
            // It must never follow or teleport into another city or instance.
            if (target->GetZoneId() != hunter.spawnZoneId ||
                target->GetMapId() != hunter.spawnMapId ||
                target->GetInstanceId() != hunter.spawnInstanceId)
            {
                ECG_LOG_DEBUG("Bounty hunter %s target left jurisdiction: zone %u->%u, map %u/%u->%u/%u",
                    hunterGuid.ToString().c_str(), hunter.spawnZoneId, target->GetZoneId(),
                    hunter.spawnMapId, hunter.spawnInstanceId,
                    target->GetMapId(), target->GetInstanceId());
                toRemove.push_back(hunterGuid);
                continue;
            }

            // Find the creature
            Map* hunterMap = sMapMgr->FindMap(hunter.spawnMapId, hunter.spawnInstanceId);
            Creature* creature = hunterMap ? hunterMap->GetCreature(hunterGuid) : nullptr;

            if (!creature || !creature->IsInWorld() || creature->IsDuringRemoveFromWorld() ||
                !creature->IsAlive())
            {
                ECG_LOG_DEBUG("Bounty hunter %s is dead or missing",
                    hunterGuid.ToString().c_str());
                toRemove.push_back(hunterGuid);
                continue;
            }

            // Update target tracking
            if (target && target->IsAlive())
            {
                hunter.lastKnownX = target->GetPositionX();
                hunter.lastKnownY = target->GetPositionY();
                hunter.lastKnownZ = target->GetPositionZ();
                hunter.lastKnownZoneId = target->GetZoneId();
                hunter.lastUpdateTime = now;

            }
        }

        // Remove dead/expired hunters
        for (const auto& guid : toRemove)
        {
            DespawnBountyHunter(guid);
        }
    }

    void ZoneStateManager::DespawnBountyHunter(ObjectGuid hunterGuid)
    {
        std::lock_guard<std::recursive_mutex> lock(m_stateMutex);
        auto it = m_bountyHunters.find(hunterGuid);
        if (it == m_bountyHunters.end()) return;

        // Remove from zone tracking
        ZoneState* zone = GetZoneState(it->second.spawnZoneId);
        if (zone)
        {
            auto& hunters = zone->activeBountyHunters;
            hunters.erase(std::remove(hunters.begin(), hunters.end(), hunterGuid), hunters.end());
        }

        // Resolve the creature on its original map. Looking it up relative to the
        // target fails after a portal/teleport and used to leave orphaned hunters.
        if (Map* hunterMap = sMapMgr->FindMap(it->second.spawnMapId, it->second.spawnInstanceId))
        {
            if (Creature* creature = hunterMap->GetCreature(hunterGuid))
            {
                creature->DespawnOrUnsummon();
            }
        }

        // Emit to bridge before erasing
        sGuardSystemBridge->EmitBountyHunterDespawned(hunterGuid, it->second.targetGuid, "timeout_or_death");

        m_bountyHunters.erase(it);
        ECG_LOG_DEBUG("Bounty hunter %s despawned", hunterGuid.ToString().c_str());
    }

    // DEPRECATED: Returns raw pointer that can dangle after lock release.
    // Use GetBountyHunterForTargetCopy() for thread-safe access.
    // Only safe for INTERNAL use where caller holds m_stateMutex.
    ZoneBountyHunter* ZoneStateManager::GetBountyHunterForTarget(ObjectGuid targetGuid)
    {
        std::lock_guard<std::recursive_mutex> lock(m_stateMutex);
        for (auto& [hunterGuid, hunter] : m_bountyHunters)
        {
            if (hunter.targetGuid == targetGuid)
                return &hunter;
        }
        return nullptr;
    }

    // DEPRECATED: Returns raw pointers that can dangle after lock release.
    // Use GetAllBountyHuntersForTargetCopy() for thread-safe access.
    // Only safe for INTERNAL use where caller holds m_stateMutex.
    std::vector<ZoneBountyHunter*> ZoneStateManager::GetAllBountyHuntersForTarget(ObjectGuid targetGuid)
    {
        std::lock_guard<std::recursive_mutex> lock(m_stateMutex);
        std::vector<ZoneBountyHunter*> result;
        for (auto& [hunterGuid, hunter] : m_bountyHunters)
        {
            if (hunter.targetGuid == targetGuid)
                result.push_back(&hunter);
        }
        return result;
    }

    std::optional<ZoneBountyHunter> ZoneStateManager::GetBountyHunterForTargetCopy(ObjectGuid targetGuid) const
    {
        std::lock_guard<std::recursive_mutex> lock(m_stateMutex);
        for (const auto& [hunterGuid, hunter] : m_bountyHunters)
        {
            if (hunter.targetGuid == targetGuid)
                return hunter;  // Return copy
        }
        return std::nullopt;
    }

    std::vector<ZoneBountyHunter> ZoneStateManager::GetAllBountyHuntersForTargetCopy(ObjectGuid targetGuid) const
    {
        std::lock_guard<std::recursive_mutex> lock(m_stateMutex);
        std::vector<ZoneBountyHunter> result;
        for (const auto& [hunterGuid, hunter] : m_bountyHunters)
        {
            if (hunter.targetGuid == targetGuid)
                result.push_back(hunter);  // Copy
        }
        return result;
    }

    // ==========================================================================
    // PHASE MANAGEMENT
    // ==========================================================================

    void ZoneStateManager::CheckPhaseTransitions(uint32 diff)
    {
        for (auto& [zoneId, zone] : m_zoneStates)
        {
            if (!zone.IsUnderThreat() && zone.currentPhase == ZonePhase::QUIET)
                continue;

            ZonePhase newPhase = DeterminePhase(zone);
            if (newPhase != zone.currentPhase)
            {
                // Check if we've been in current phase long enough
                bool canTransition = true;

                switch (zone.currentPhase)
                {
                    case ZonePhase::QUIET:
                        canTransition = zone.phaseElapsedMs >= m_config.quietToUneasyMs;
                        break;
                    case ZonePhase::UNEASY:
                        canTransition = zone.phaseElapsedMs >= m_config.uneasyToMobilizingMs;
                        break;
                    case ZonePhase::MOBILIZING:
                        canTransition = zone.phaseElapsedMs >= m_config.mobilizingToImpactMs;
                        break;
                    case ZonePhase::IMPACT:
                        canTransition = zone.phaseElapsedMs >= m_config.impactToAftermathMs;
                        break;
                    case ZonePhase::AFTERMATH:
                        canTransition = zone.phaseElapsedMs >= m_config.aftermathToQuietMs;
                        break;
                }

                if (canTransition)
                {
                    TransitionPhase(zoneId, newPhase);
                }
            }
        }
    }

    void ZoneStateManager::TransitionPhase(uint32 zoneId, ZonePhase newPhase)
    {
        ZoneState* zone = GetZoneState(zoneId);
        if (!zone) return;

        ZonePhase oldPhase = zone->currentPhase;
        zone->previousPhase = oldPhase;
        zone->currentPhase = newPhase;
        zone->phaseChangeTime = std::chrono::steady_clock::now();
        zone->phaseElapsedMs = 0;

        ECG_LOG_INFO("Zone %u (%s): Phase transition %s -> %s",
            zoneId, zone->zoneName.c_str(),
            ZonePhaseToString(oldPhase), ZonePhaseToString(newPhase));

        BroadcastPhaseChange(zoneId, oldPhase, newPhase);
        ApplyPhaseEffects(zoneId, newPhase);

        // Emit after applying the phase so consumers receive the recovered
        // heat/alarm/control snapshot rather than the stale incident values.
        sGuardSystemBridge->EmitZonePhaseChanged(
            zoneId, zone->zoneName,
            static_cast<uint8>(oldPhase), static_cast<uint8>(newPhase),
            zone->heatPoints, zone->threatLevel,
            static_cast<uint8>(zone->alarmState), static_cast<uint8>(zone->controlState));

        // Apply environmental effects (weather, civilian behavior, etc.)
        sECZoneEnvironmentMgr->OnPhaseChanged(zoneId, oldPhase, newPhase);

        // Apply preparation phase effects (dialogues, militia, barricades, etc.)
        sECZonePreparationMgr->OnPhaseChanged(zoneId, oldPhase, newPhase);

        // Handle zone fallen/recovered states
        if (zone->IsFallen() && oldPhase != ZonePhase::IMPACT)
        {
            sECZoneEnvironmentMgr->OnZoneFallen(zoneId);
        }
        else if (!zone->IsFallen() && zone->previousPhase == ZonePhase::AFTERMATH && newPhase == ZonePhase::QUIET)
        {
            sECZoneEnvironmentMgr->OnZoneRecovered(zoneId);

            // Emit zone recovered to bridge for UI updates
            sGuardSystemBridge->EmitZoneRecovered(zoneId, zone->zoneName);
        }
    }

    void ZoneStateManager::ApplyPhaseEffects(uint32 zoneId, ZonePhase phase)
    {
        ZoneState* zone = GetZoneState(zoneId);
        if (!zone) return;

        switch (phase)
        {
            case ZonePhase::QUIET:
                // A completed incident must not retain enough persisted heat to
                // jump straight back into IMPACT with nobody attacking.
                zone->heatPoints = 0;
                zone->threatLevel = 0;
                zone->guardsAlive = zone->initialGuardCount;
                zone->guardsKilledRecently = 0;
                zone->guardsSpawnedThisIncident = 0;

                // Use the normal update paths so realtime consumers and banner
                // state are told that the settlement recovered.
                UpdateControlState(*zone);
                UpdateAlarmState(*zone);
                break;

            case ZonePhase::UNEASY:
                // Nervous English whispers/emotes/sounds are custom ambience,
                // not native WotLK settlement behavior. Keep them behind the
                // same explicit opt-in as synthetic phase broadcasts.
                if (m_config.phaseBroadcastsEnabled)
                {
                    ECG_LOG_DEBUG("Zone %u: Applying opt-in UNEASY ambience", zoneId);
                    ApplyUneasyEffects(zoneId);
                }
                break;

            case ZonePhase::MOBILIZING:
                // Extra guards spawn, barricades go up
                ECG_LOG_DEBUG("Zone %u: Applying MOBILIZING effects - spawning reinforcements", zoneId);
                // Spawn additional guards
                {
                    std::vector<ObjectGuid> staleCriminals;
                    for (const auto& criminalGuid : zone->activeCriminals)
                    {
                        Player* criminal = ObjectAccessor::FindPlayer(criminalGuid);
                        std::optional<WantedState> wanted =
                            sECGuardSystemMgr->GetWantedStateCopy(criminalGuid);
                        if (!criminal || !wanted ||
                            !MatchesStrategicWantedJurisdiction(*zone, *wanted, criminal))
                        {
                            staleCriminals.push_back(criminalGuid);
                            continue;
                        }

                        sECGuardSystemMgr->SpawnGuards(criminal, 2, false,
                            zone->owningFaction, zoneId);
                    }
                    for (ObjectGuid const& criminalGuid : staleCriminals)
                        zone->RemoveCriminal(criminalGuid);
                }
                break;

            case ZonePhase::IMPACT:
                // Full combat mode, spawn elite guards
                ECG_LOG_DEBUG("Zone %u: Applying IMPACT effects - elite response", zoneId);
                {
                    std::vector<ObjectGuid> staleCriminals;
                    for (const auto& criminalGuid : zone->activeCriminals)
                    {
                        Player* criminal = ObjectAccessor::FindPlayer(criminalGuid);
                        std::optional<WantedState> wanted =
                            sECGuardSystemMgr->GetWantedStateCopy(criminalGuid);
                        if (!criminal || !wanted ||
                            !MatchesStrategicWantedJurisdiction(*zone, *wanted, criminal))
                        {
                            staleCriminals.push_back(criminalGuid);
                            continue;
                        }

                        sECGuardSystemMgr->SpawnGuards(criminal, 2, true,
                            zone->owningFaction, zoneId); // Elite guards
                    }
                    for (ObjectGuid const& criminalGuid : staleCriminals)
                        zone->RemoveCriminal(criminalGuid);
                }
                break;

            case ZonePhase::AFTERMATH:
                // Cleanup phase, spawn healers, dead body cleanup
                ECG_LOG_DEBUG("Zone %u: Applying AFTERMATH effects - recovery", zoneId);
                break;
        }
    }

    ZonePhase ZoneStateManager::DeterminePhase(const ZoneState& zone) const
    {
        // Check for recovery first
        if (zone.activeCriminals.empty())
        {
            if (zone.currentPhase == ZonePhase::IMPACT ||
                zone.currentPhase == ZonePhase::MOBILIZING)
            {
                return ZonePhase::AFTERMATH;
            }
            if (zone.currentPhase == ZonePhase::AFTERMATH)
            {
                return ZonePhase::QUIET;
            }
            if (zone.currentPhase == ZonePhase::UNEASY)
            {
                return ZonePhase::QUIET;
            }

            // Persisted heat and guard-loss counters describe a past incident,
            // not a current assault. Never re-enter a threat phase until a live
            // criminal is enrolled in this exact jurisdiction.
            return ZonePhase::QUIET;
        }

        // Determine phase based on heat
        if (zone.heatPoints >= m_config.impactThreshold)
            return ZonePhase::IMPACT;
        if (zone.heatPoints >= m_config.mobilizingThreshold)
            return ZonePhase::MOBILIZING;
        if (zone.heatPoints >= m_config.uneasyThreshold)
            return ZonePhase::UNEASY;

        // Also check control state
        if (zone.controlState == ControlState::FALLEN)
            return ZonePhase::IMPACT;
        if (zone.controlState == ControlState::CONTESTED)
            return ZonePhase::MOBILIZING;

        return ZonePhase::QUIET;
    }

    void ZoneStateManager::BroadcastPhaseChange(uint32 zoneId, ZonePhase oldPhase, ZonePhase newPhase)
    {
        if (!m_config.phaseBroadcastsEnabled)
        {
            ECG_LOG_DEBUG("Zone %u phase changed %s -> %s; synthetic player/NPC broadcast disabled",
                zoneId, ZonePhaseToString(oldPhase), ZonePhaseToString(newPhase));
            return;
        }

        ZoneState* zone = GetZoneState(zoneId);
        if (!zone) return;

        // Get appropriate bark for the phase
        std::string message;
        const std::vector<std::string>* barks = nullptr;

        switch (newPhase)
        {
            case ZonePhase::UNEASY:
                barks = &ZonePhaseFlavorText::GetUneasyBarks();
                break;
            case ZonePhase::MOBILIZING:
                barks = &ZonePhaseFlavorText::GetMobilizingBarks();
                break;
            case ZonePhase::IMPACT:
                if (zone->IsFallen())
                    barks = &ZonePhaseFlavorText::GetFallenZoneBarks();
                else
                    barks = &ZonePhaseFlavorText::GetImpactBarks();
                break;
            case ZonePhase::AFTERMATH:
                barks = &ZonePhaseFlavorText::GetAftermathBarks();
                break;
            default:
                break;
        }

        if (barks && !barks->empty())
        {
            message = (*barks)[urand(0, barks->size() - 1)];
        }

        // Broadcast to players in zone
        if (!message.empty())
        {
            ECG_LOG_DEBUG("Zone %u broadcast: %s", zoneId, message.c_str());

            // Build announcement message with zone name prefix
            std::string announcement = "[" + zone->zoneName + "] " + message;

            // Find and notify all players in this zone
            WorldSessionMgr::SessionMap const& sessions = sWorldSessionMgr->GetAllSessions();
            for (auto const& itr : sessions)
            {
                if (Player* player = itr.second->GetPlayer())
                {
                    if (player->IsInWorld() && player->IsAlive() && player->GetZoneId() == zoneId)
                    {
                        // Send as system message
                        WorldPacket data;
                        ChatHandler::BuildChatPacket(data, CHAT_MSG_SYSTEM, LANG_UNIVERSAL,
                            nullptr, nullptr, announcement);
                        player->SendDirectMessage(&data);

                        // For dramatic phases, also send a raid warning style message
                        if (newPhase == ZonePhase::IMPACT || zone->IsFallen())
                        {
                            ChatHandler::BuildChatPacket(data, CHAT_MSG_RAID_WARNING, LANG_UNIVERSAL,
                                nullptr, nullptr, announcement);
                            player->SendDirectMessage(&data);
                        }
                    }
                }
            }

            // Make a random nearby NPC say the bark
            BroadcastNPCBark(zoneId, message);
        }
    }

    void ZoneStateManager::BroadcastNPCBark(uint32 zoneId, const std::string& message)
    {
        // Find a random civilian NPC in the zone to say the bark
        // This makes the world feel more alive - NPCs react to events

        // Get all players in zone first, then find NPCs near any of them
        std::vector<Player*> playersInZone;
        WorldSessionMgr::SessionMap const& sessions = sWorldSessionMgr->GetAllSessions();
        for (auto const& itr : sessions)
        {
            if (Player* player = itr.second->GetPlayer())
            {
                if (player->IsInWorld() && player->IsAlive() && player->GetZoneId() == zoneId)
                {
                    playersInZone.push_back(player);
                }
            }
        }

        if (playersInZone.empty())
            return;

        // Pick a random player and find an NPC near them
        Player* randomPlayer = playersInZone[urand(0, playersInZone.size() - 1)];
        if (!randomPlayer || !randomPlayer->GetMap())
            return;

        // Search for nearby friendly NPCs that aren't guards
        std::vector<Creature*> nearbyNPCs;
        float searchRadius = 50.0f;

        for (auto const& pair : randomPlayer->GetMap()->GetCreatureBySpawnIdStore())
        {
            Creature* creature = pair.second;
            if (!creature || !creature->IsInWorld() || creature->IsDuringRemoveFromWorld() ||
                !creature->IsAlive())
                continue;

            // Skip guards and hostile NPCs
            if (sECGuardSystemMgr->IsCreatureGuard(creature))
                continue;
            if (creature->IsHostileTo(randomPlayer))
                continue;

            // Check distance
            if (creature->GetDistance(randomPlayer) > searchRadius)
                continue;

            // Check if it's a "civilian" type NPC (vendor, questgiver, etc.)
            if (creature->IsVendor() || creature->IsQuestGiver() ||
                creature->IsInnkeeper() || creature->IsTrainer())
            {
                nearbyNPCs.push_back(creature);
            }
        }

        if (!nearbyNPCs.empty())
        {
            // Pick a random NPC to say the bark
            Creature* barker = nearbyNPCs[urand(0, nearbyNPCs.size() - 1)];
            if (barker && barker->IsInWorld() && !barker->IsDuringRemoveFromWorld() &&
                barker->IsAlive())
            {
                barker->Say(message, LANG_UNIVERSAL);
                ECG_LOG_DEBUG("NPC %s (%u) barked: %s",
                    barker->GetName().c_str(), barker->GetEntry(), message.c_str());
            }
        }

#ifdef MOD_PLAYERBOTS
        // Also have a random playerbot in the zone react
        // This makes the world feel more alive - even bots respond to events
        std::vector<Player*> botsInZone;
        for (Player* player : playersInZone)
        {
            // Check if this is a playerbot (not a real player)
            if (sPlayerbotsMgr.GetPlayerbotAI(player) ||
                sRandomPlayerbotMgr.IsRandomBot(player))
            {
                botsInZone.push_back(player);
            }
        }

        if (!botsInZone.empty() && urand(0, 100) < 40) // 40% chance for bot reaction
        {
            // Pick a random bot to say something
            Player* botBarker = botsInZone[urand(0, botsInZone.size() - 1)];
            if (botBarker && botBarker->IsInWorld() &&
                !botBarker->IsDuringRemoveFromWorld() && botBarker->IsAlive())
            {
                // Bot reactions - context-aware responses
                static const std::vector<std::string> botReactions = {
                    "Did you hear that?",
                    "Something's happening...",
                    "Stay alert!",
                    "Be careful out there.",
                    "I have a bad feeling about this.",
                    "This doesn't look good.",
                    "Watch your back!",
                    "Keep your eyes open.",
                };
                std::string botMessage = botReactions[urand(0, botReactions.size() - 1)];
                botBarker->Say(botMessage, LANG_UNIVERSAL);
                ECG_LOG_DEBUG("Playerbot %s reacted: %s",
                    botBarker->GetName().c_str(), botMessage.c_str());
            }
        }
#endif
    }

    void ZoneStateManager::ApplyUneasyEffects(uint32 zoneId)
    {
        ZoneState* zone = GetZoneState(zoneId);
        if (!zone) return;

        ECG_LOG_DEBUG("Zone %u: Applying UNEASY visual/audio effects", zoneId);

        // Find players in zone to apply effects around them
        WorldSessionMgr::SessionMap const& sessions = sWorldSessionMgr->GetAllSessions();
        for (auto const& itr : sessions)
        {
            if (Player* player = itr.second->GetPlayer())
            {
                if (!player->IsInWorld() || player->GetZoneId() != zoneId)
                    continue;

                Map* map = player->GetMap();
                if (!map)
                    continue;

                // 1. Make nearby NPCs do nervous emotes
                float nervousRadius = 40.0f;
                for (auto const& pair : map->GetCreatureBySpawnIdStore())
                {
                    Creature* creature = pair.second;
                    if (!creature || !creature->IsInWorld() ||
                        creature->IsDuringRemoveFromWorld() || !creature->IsAlive())
                        continue;

                    // Skip guards and hostile NPCs
                    if (sECGuardSystemMgr->IsCreatureGuard(creature))
                        continue;
                    if (creature->IsHostileTo(player))
                        continue;

                    // Check distance to player
                    if (creature->GetDistance(player) > nervousRadius)
                        continue;

                    // Apply nervous emotes to civilians with random chance
                    if (urand(0, 100) < 25) // 25% of nearby NPCs show nervous behavior
                    {
                        // Pick a random nervous emote
                        switch (urand(0, 4))
                        {
                            case 0:
                                creature->HandleEmoteCommand(EMOTE_ONESHOT_QUESTION);
                                break;
                            case 1:
                                creature->HandleEmoteCommand(EMOTE_ONESHOT_EXCLAMATION);
                                break;
                            case 2:
                                creature->HandleEmoteCommand(EMOTE_STATE_READY_UNARMED);
                                break;
                            case 3:
                                creature->HandleEmoteCommand(EMOTE_ONESHOT_NO);
                                break;
                            case 4:
                                creature->HandleEmoteCommand(EMOTE_ONESHOT_TALK);
                                break;
                        }

                        // Some NPCs whisper worried lines
                        if (urand(0, 100) < 30) // 30% chance to whisper
                        {
                            static const std::vector<std::string> nervousWhispers = {
                                "What was that noise?",
                                "I don't like this...",
                                "Something feels wrong.",
                                "Did you see that?",
                                "*glances around nervously*",
                                "Keep your voice down...",
                                "We should be careful.",
                            };
                            creature->Whisper(nervousWhispers[urand(0, nervousWhispers.size() - 1)],
                                LANG_UNIVERSAL, player);
                        }
                    }
                }

                // 2. Apply subtle visual cues to the player
                // Play an ambient warning sound (using zone-appropriate sounds)
                // Sound ID 8571 = Ambient warning/tension sound
                if (urand(0, 100) < 20) // 20% chance per player
                {
                    player->PlayDistanceSound(8571, player);
                }

                // Only need to process one player per zone for the NPC effects
                break;
            }
        }

        // 3. Emit event to realtime bridge for frontend awareness
        std::map<std::string, std::string> eventData;
        eventData["effectType"] = "uneasy_atmosphere";
        eventData["threatLevel"] = std::to_string(zone->threatLevel);
        eventData["activeCriminals"] = std::to_string(zone->activeCriminals.size());
        sECZoneEnvironmentMgr->EmitEnvironmentEvent(zoneId, "uneasy_effects", eventData);
    }

    // ==========================================================================
    // HEAT DECAY
    // ==========================================================================

    void ZoneStateManager::ProcessHeatDecay(uint32 diff)
    {
        auto now = std::chrono::steady_clock::now();

        for (auto& [zoneId, zone] : m_zoneStates)
        {
            if (zone.heatPoints == 0) continue;

            // Check if enough time has passed since last crime
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                now - zone.lastCrimeTime).count();

            if (elapsed < m_config.heatDecayDelayMs)
                continue;

            // Calculate decay amount
            uint32 decayAmount = (m_config.heatDecayRatePerSec * diff) / 1000;
            if (decayAmount > 0)
            {
                uint32 oldHeat = zone.heatPoints;
                zone.DecayHeat(decayAmount);

                if (zone.heatPoints != oldHeat)
                {
                    ECG_LOG_TRACE("Zone %u: Heat decayed %u -> %u",
                        zoneId, oldHeat, zone.heatPoints);

                    // Emit heat change to bridge for UI updates
                    sGuardSystemBridge->EmitZoneHeatChanged(
                        zoneId, zone.zoneName, oldHeat, zone.heatPoints,
                        static_cast<uint8>(zone.currentPhase), static_cast<uint8>(zone.alarmState));
                }
            }
        }
    }

    // ==========================================================================
    // ELITE RESPONSE
    // ==========================================================================

    void ZoneStateManager::TriggerEliteResponse(uint32 zoneId)
    {
        ZoneState* zone = GetZoneState(zoneId);
        if (!zone) return;

        // Get the best faction response for this zone
        const FactionResponseGroup* response = GetBestResponseForZone(zoneId);

        // Calculate dynamic response delay based on massacre severity
        uint32 baseDelay = zone->CalculateEliteResponseDelay(m_config);
        uint32 responseDelay = response ? std::max(baseDelay, response->spawnDelayMs) : baseDelay;

        // Convert to human-readable time
        uint32 delayMinutes = responseDelay / 60000;
        uint32 delaySeconds = (responseDelay % 60000) / 1000;

        ECG_LOG_WARN("Zone %u (%s) has FALLEN! Elite response in %u min %u sec",
            zoneId, zone->zoneName.c_str(), delayMinutes, delaySeconds);

        ECG_LOG_INFO("  Response factors: guards killed=%u, criminals=%zu, civilians=%u",
            zone->totalGuardDeaths, zone->activeCriminals.size(), zone->totalCivilianDeaths);

        // Get faction-specific response info from database or defaults
        const char* factionName = zone->owningFaction == TEAM_ALLIANCE ? "Alliance" : "Horde";
        std::string eliteForce = response ? response->responseName :
            (zone->owningFaction == TEAM_ALLIANCE ? "Stormwind Royal Guard" : "Thrall's Kor'kron Elite");

        ECG_LOG_WARN("  %s leadership notified. %s dispatched!", factionName, eliteForce.c_str());

        // Database response announcements are also custom English barks and
        // must respect the native-safe speech opt-in.
        if (m_config.phaseBroadcastsEnabled && response && !response->announcementText.empty())
        {
            BroadcastNPCBark(zoneId, response->announcementText);
        }

        // Emit bridge events for UI updates
        sGuardSystemBridge->EmitZoneFallen(zoneId, zone->zoneName,
            zone->totalGuardDeaths, static_cast<uint32>(zone->activeCriminals.size()),
            responseDelay);
        sGuardSystemBridge->EmitEliteResponseTriggered(zoneId, zone->zoneName,
            responseDelay, eliteForce);

        // Schedule full elite response via World Events module
#ifdef MOD_EC_WORLD_EVENTS
        uint8 faction = TeamIdToGuardDbFaction(zone->owningFaction);
        if (faction != 0)
        {
            uint32 scheduledId = sECWorldEventMgr->ScheduleDefenseEvent(zoneId, faction, responseDelay);
            ECG_LOG_INFO("  Scheduled World Event defense (ID: %u) for zone %u faction %u in %u ms",
                scheduledId, zoneId, faction, responseDelay);
        }
        else
        {
            ECG_LOG_WARN("  Skipping World Event defense scheduling for neutral zone %u", zoneId);
        }
#endif

        // Calculate immediate spawn count from database or defaults
        uint32 eliteCount = 2;
        if (response)
        {
            // Use database-defined spawn counts
            eliteCount = urand(response->spawnCountMin, response->spawnCountMax);
            ECG_LOG_INFO("  Using faction response '%s' (tier %u): spawning %u elites",
                response->responseName.c_str(), response->responseTier, eliteCount);
        }
        else
        {
            // Fallback: scale by severity
            if (zone->totalGuardDeaths >= 20) eliteCount = 6;
            else if (zone->totalGuardDeaths >= 10) eliteCount = 4;
            else if (zone->totalGuardDeaths >= 5) eliteCount = 3;
        }

        ECG_LOG_INFO("  Spawning %u elite guards immediately (full response in %u ms)",
            eliteCount, responseDelay);

        // Emit elite forces arriving to bridge for UI updates
        sGuardSystemBridge->EmitEliteForcesArriving(zoneId, zone->zoneName, eliteCount);

        std::vector<ObjectGuid> staleCriminals;
        for (const auto& criminalGuid : zone->activeCriminals)
        {
            Player* criminal = ObjectAccessor::FindPlayer(criminalGuid);
            if (!criminal)
            {
                staleCriminals.push_back(criminalGuid);
                continue;
            }

            std::optional<WantedState> wanted = sECGuardSystemMgr->GetWantedStateCopy(criminalGuid);
            if (!wanted ||
                !MatchesStrategicWantedJurisdiction(*zone, *wanted, criminal))
            {
                ECG_LOG_DEBUG("Skipping stale elite response target %s: response zone %u, current zone %u, valid jurisdiction=%s",
                    criminal->GetName().c_str(), zoneId, criminal->GetZoneId(),
                    wanted && MatchesStrategicWantedJurisdiction(*zone, *wanted, criminal) ? "yes" : "no");
                staleCriminals.push_back(criminalGuid);
                continue;
            }

            Map* map = criminal->GetMap();
            if (!map)
                continue;

            // Use specific creature entries from database if available
            if (response && !response->creatureEntries.empty())
            {
                ECG_LOG_DEBUG("  Using %zu custom elite templates from faction response",
                    response->creatureEntries.size());

                for (uint32 i = 0; i < eliteCount; ++i)
                {
                    // Random entry from response's creature templates
                    uint32 entry = response->creatureEntries[urand(0, response->creatureEntries.size() - 1)];

                    // Spawn position: random point around criminal
                    float angle = frand(0.0f, 2.0f * static_cast<float>(M_PI));
                    float dist = frand(15.0f, 30.0f);  // 15-30 yards away
                    float x = criminal->GetPositionX() + cos(angle) * dist;
                    float y = criminal->GetPositionY() + sin(angle) * dist;
                    float z = criminal->GetPositionZ();

                    // Adjust Z to ground level
                    criminal->UpdateAllowedPositionZ(x, y, z);

                    // Spawn the elite with 5-minute despawn timer
                    if (TempSummon* guard = map->SummonCreature(entry, {x, y, z, 0.0f}, nullptr, 300000))
                    {
                        guard->SetTempSummonType(TEMPSUMMON_TIMED_DESPAWN_OUT_OF_COMBAT);
                        guard->AI()->AttackStart(criminal);

                        // Track via guard system
                        sECGuardSystemMgr->RegisterSpawnedGuard(guard->GetGUID(), criminal->GetGUID());

                        ECG_LOG_DEBUG("  Spawned elite template %u targeting %s",
                            entry, criminal->GetName().c_str());
                    }
                }
            }
            else
            {
                // Fallback to generic elite spawn from zone config
                sECGuardSystemMgr->SpawnGuards(criminal, eliteCount, true,
                    zone->owningFaction, zoneId);
            }
        }

        for (ObjectGuid const& criminalGuid : staleCriminals)
            zone->RemoveCriminal(criminalGuid);

        // Emit elite forces arrived to bridge for UI updates
        sGuardSystemBridge->EmitEliteForcesArrived(zoneId, zone->zoneName, eliteCount);
    }

    // ==========================================================================
    // INTERNAL STATE UPDATES
    // ==========================================================================

    void ZoneStateManager::UpdateAlarmState(ZoneState& zone)
    {
        AlarmState oldState = zone.alarmState;

        if (zone.controlState == ControlState::FALLEN)
        {
            zone.alarmState = AlarmState::OCCUPIED;
        }
        else if (zone.currentPhase == ZonePhase::IMPACT)
        {
            zone.alarmState = AlarmState::ATTACKED;
        }
        else if (zone.currentPhase == ZonePhase::MOBILIZING)
        {
            zone.alarmState = AlarmState::MOBILIZING;
        }
        else if (zone.currentPhase == ZonePhase::UNEASY)
        {
            zone.alarmState = AlarmState::UNEASY;
        }
        else if (zone.currentPhase == ZonePhase::AFTERMATH)
        {
            zone.alarmState = AlarmState::RECOVERING;
        }
        else
        {
            zone.alarmState = AlarmState::QUIET;
        }

        if (zone.alarmState != oldState)
        {
            ECG_LOG_DEBUG("Zone %u: Alarm state changed %s -> %s",
                zone.zoneId, AlarmStateToString(oldState),
                AlarmStateToString(zone.alarmState));

            // Emit zone state change to bridge
            sGuardSystemBridge->EmitZoneStateChanged(
                zone.zoneId, zone.zoneName,
                static_cast<uint8>(zone.alarmState), static_cast<uint8>(zone.controlState),
                zone.GetGuardSurvivalRate(), static_cast<uint32>(zone.activeCriminals.size()));
        }
    }

    void ZoneStateManager::UpdateControlState(ZoneState& zone)
    {
        ControlState oldState = zone.controlState;
        float survival = zone.GetGuardSurvivalRate();

        if (survival < m_config.fallenThreshold)
        {
            zone.controlState = ControlState::FALLEN;
        }
        else if (survival < m_config.contestedThreshold)
        {
            zone.controlState = ControlState::CONTESTED;
        }
        else
        {
            zone.controlState = ControlState::STABLE;
        }

        if (zone.controlState != oldState)
        {
            ECG_LOG_DEBUG("Zone %u: Control state changed %s -> %s (survival: %.1f%%)",
                zone.zoneId, ControlStateToString(oldState),
                ControlStateToString(zone.controlState), survival * 100.0f);

            // Emit zone state change to bridge
            sGuardSystemBridge->EmitZoneStateChanged(
                zone.zoneId, zone.zoneName,
                static_cast<uint8>(zone.alarmState), static_cast<uint8>(zone.controlState),
                survival, static_cast<uint32>(zone.activeCriminals.size()));

            // Notify preparation system (for banner changes, etc.)
            sECZonePreparationMgr->OnZoneControlChanged(zone.zoneId, oldState, zone.controlState);
        }
    }

    // ==========================================================================
    // UTILITY
    // ==========================================================================

    const char* ZoneStateManager::AlarmStateToString(AlarmState state)
    {
        switch (state)
        {
            case AlarmState::QUIET:      return "QUIET";
            case AlarmState::UNEASY:     return "UNEASY";
            case AlarmState::MOBILIZING: return "MOBILIZING";
            case AlarmState::ATTACKED:   return "ATTACKED";
            case AlarmState::OCCUPIED:   return "OCCUPIED";
            case AlarmState::RECOVERING: return "RECOVERING";
            default:                     return "UNKNOWN";
        }
    }

    const char* ZoneStateManager::ControlStateToString(ControlState state)
    {
        switch (state)
        {
            case ControlState::STABLE:    return "STABLE";
            case ControlState::CONTESTED: return "CONTESTED";
            case ControlState::FALLEN:    return "FALLEN";
            default:                      return "UNKNOWN";
        }
    }

    const char* ZoneStateManager::ZonePhaseToString(ZonePhase phase)
    {
        switch (phase)
        {
            case ZonePhase::QUIET:      return "QUIET";
            case ZonePhase::UNEASY:     return "UNEASY";
            case ZonePhase::MOBILIZING: return "MOBILIZING";
            case ZonePhase::IMPACT:     return "IMPACT";
            case ZonePhase::AFTERMATH:  return "AFTERMATH";
            default:                    return "UNKNOWN";
        }
    }

    // ==========================================================================
    // DATABASE PERSISTENCE
    // ==========================================================================

    void ZoneStateManager::SaveAllZoneStates()
    {
        ECG_LOG_INFO("Saving all zone states to database...");

        uint32 savedCount = 0;
        for (const auto& [zoneId, zone] : m_zoneStates)
        {
            // Only save zones that have been modified (non-default state)
            if (zone.heatPoints > 0 || zone.threatLevel > 0 ||
                zone.controlState != ControlState::STABLE ||
                zone.totalGuardDeaths > 0 || zone.totalCivilianDeaths > 0)
            {
                SaveZoneState(zoneId);
                savedCount++;
            }
        }

        ECG_LOG_INFO("Saved %u zone states to database", savedCount);
    }

    void ZoneStateManager::SaveZoneState(uint32 zoneId)
    {
        ZoneState* zone = GetZoneState(zoneId);
        if (!zone) return;

        // Use REPLACE INTO for upsert behavior
        CharacterDatabase.Execute(
            "REPLACE INTO ec_guard_zone_state "
            "(zone_id, threat_level, heat_points, guards_alive, guards_killed_total, "
            "civilians_killed_total, alarm_state, control_state, current_phase, "
            "owning_faction, last_update) VALUES "
            "({}, {}, {}, {}, {}, {}, {}, {}, {}, {}, NOW())",
            zoneId,
            zone->threatLevel,
            zone->heatPoints,
            zone->guardsAlive,
            zone->totalGuardDeaths,
            zone->totalCivilianDeaths,
            static_cast<uint8>(zone->alarmState),
            static_cast<uint8>(zone->controlState),
            static_cast<uint8>(zone->currentPhase),
            static_cast<uint8>(zone->owningFaction)
        );

        ECG_LOG_DEBUG("Saved zone %u state: threat=%u, heat=%u, control=%s",
            zoneId, zone->threatLevel, zone->heatPoints,
            ControlStateToString(zone->controlState));
    }

    void ZoneStateManager::LoadSavedZoneStates()
    {
        ECG_LOG_INFO("Loading saved zone states from database...");

        QueryResult result = CharacterDatabase.Query(
            "SELECT zone_id, threat_level, heat_points, guards_alive, guards_killed_total, "
            "civilians_killed_total, alarm_state, control_state, current_phase, owning_faction "
            "FROM ec_guard_zone_state WHERE heat_points > 0 OR control_state > 0 "
            "OR guards_killed_total > 0 OR civilians_killed_total > 0");

        if (!result)
        {
            ECG_LOG_DEBUG("No saved zone states found in database");
            return;
        }

        uint32 loadedCount = 0;
        do
        {
            Field* fields = result->Fetch();
            uint32 zoneId = fields[0].Get<uint32>();

            // Get or create the zone state
            ZoneState* zone = GetZoneState(zoneId);
            if (!zone)
            {
                // Zone not in config, create dynamically
                TeamId faction = static_cast<TeamId>(fields[9].Get<uint8>());
                zone = GetOrCreateZoneState(zoneId, faction);
            }

            if (zone)
            {
                uint8 savedThreat = fields[1].Get<uint8>();
                uint32 savedHeat = fields[2].Get<uint32>();
                uint8 savedAlarm = fields[6].Get<uint8>();
                uint8 savedControl = fields[7].Get<uint8>();
                uint8 savedPhase = fields[8].Get<uint8>();
                uint32 savedGuardsAlive = fields[3].Get<uint32>();

                zone->totalGuardDeaths = fields[4].Get<uint32>();
                zone->totalCivilianDeaths = fields[5].Get<uint32>();

                if (zoneId == ASTRANAAR_ZONE_ID)
                {
                    // Astranaar's persisted incident had no runtime actors and
                    // repeatedly re-entered IMPACT after restart. Preserve its
                    // lifetime statistics but clear only that reported phantom.
                    zone->threatLevel = 0;
                    zone->heatPoints = 0;
                    zone->guardsAlive = zone->initialGuardCount;
                    zone->alarmState = AlarmState::QUIET;
                    zone->controlState = ControlState::STABLE;
                    zone->currentPhase = ZonePhase::QUIET;
                    zone->previousPhase = ZonePhase::QUIET;
                    zone->phaseElapsedMs = 0;

                    if (savedThreat != 0 || savedHeat != 0 || savedAlarm != 0 ||
                        savedControl != 0 || savedPhase != 0)
                    {
                        ECG_LOG_INFO("Astranaar: discarded stale persisted incident state "
                            "(threat=%u heat=%u alarm=%u control=%u phase=%u); lifetime deaths preserved",
                            savedThreat, savedHeat, savedAlarm, savedControl, savedPhase);
                    }
                }
                else
                {
                    zone->threatLevel = savedThreat;
                    zone->heatPoints = std::min(savedHeat, m_config.maxHeatPoints);
                    zone->guardsAlive = savedGuardsAlive;
                    zone->alarmState = static_cast<AlarmState>(savedAlarm);
                    zone->controlState = static_cast<ControlState>(savedControl);
                    zone->currentPhase = static_cast<ZonePhase>(savedPhase);
                    zone->previousPhase = zone->currentPhase;
                    zone->phaseElapsedMs = 0;
                }

                loadedCount++;
            }

        } while (result->NextRow());

        ECG_LOG_INFO("Loaded %u saved zone states from database", loadedCount);
    }

    // ==========================================================================
    // FACTION RESPONSE GROUPS
    // ==========================================================================

    void ZoneStateManager::LoadFactionResponseGroups()
    {
        m_factionResponses.clear();

        QueryResult result = WorldDatabase.Query(
            "SELECT id, faction, response_name, response_tier, creature_entry_list, "
            "spawn_count_min, spawn_count_max, spawn_delay_ms, announcement_text, "
            "zone_whitelist, zone_blacklist, min_threat_level, requires_zone_fallen, "
            "priority, enabled FROM ec_guard_faction_response WHERE enabled = 1 ORDER BY priority ASC");

        if (!result)
        {
            ECG_LOG_INFO("No faction response groups found in database, using defaults");
            // Add default Alliance response
            FactionResponseGroup alliance;
            alliance.id = 1;
            alliance.faction = 1;
            alliance.responseName = "Stormwind Royal Guard";
            alliance.responseTier = 1;
            alliance.creatureEntries = {1756, 1976};
            alliance.spawnCountMin = 4;
            alliance.spawnCountMax = 8;
            alliance.spawnDelayMs = 30000;
            alliance.announcementText = "The Stormwind Royal Guard has been dispatched!";
            alliance.minThreatLevel = 4;
            m_factionResponses.push_back(alliance);

            // Add default Horde response
            FactionResponseGroup horde;
            horde.id = 2;
            horde.faction = 2;
            horde.responseName = "Kor'kron Guard";
            horde.responseTier = 1;
            horde.creatureEntries = {5952, 5953};
            horde.spawnCountMin = 4;
            horde.spawnCountMax = 8;
            horde.spawnDelayMs = 30000;
            horde.announcementText = "The Kor'kron have been summoned!";
            horde.minThreatLevel = 4;
            m_factionResponses.push_back(horde);
            return;
        }

        do
        {
            Field* fields = result->Fetch();
            FactionResponseGroup group;

            group.id = fields[0].Get<uint32>();
            group.faction = fields[1].Get<uint8>();
            group.responseName = fields[2].Get<std::string>();
            group.responseTier = fields[3].Get<uint8>();

            // Parse creature entry list
            std::string entryList = fields[4].Get<std::string>();
            std::stringstream ss(entryList);
            std::string token;
            while (std::getline(ss, token, ','))
            {
                if (!token.empty())
                {
                    try {
                        group.creatureEntries.push_back(std::stoul(token));
                    }
                    catch (const std::exception& e) {
                        ECG_LOG_WARN("Failed to parse creature entry '%s' in response group %u: %s",
                            token.c_str(), group.id, e.what());
                    }
                }
            }

            group.spawnCountMin = fields[5].Get<uint32>();
            group.spawnCountMax = fields[6].Get<uint32>();
            group.spawnDelayMs = fields[7].Get<uint32>();
            group.announcementText = fields[8].Get<std::string>();

            // Parse zone whitelist
            std::string whitelist = fields[9].Get<std::string>();
            if (!whitelist.empty())
            {
                std::stringstream wss(whitelist);
                while (std::getline(wss, token, ','))
                {
                    if (!token.empty())
                    {
                        try {
                            group.zoneWhitelist.insert(std::stoul(token));
                        }
                        catch (const std::exception& e) {
                            ECG_LOG_WARN("Failed to parse zone whitelist entry '%s' in response group %u: %s",
                                token.c_str(), group.id, e.what());
                        }
                    }
                }
            }

            // Parse zone blacklist
            std::string blacklist = fields[10].Get<std::string>();
            if (!blacklist.empty())
            {
                std::stringstream bss(blacklist);
                while (std::getline(bss, token, ','))
                {
                    if (!token.empty())
                    {
                        try {
                            group.zoneBlacklist.insert(std::stoul(token));
                        }
                        catch (const std::exception& e) {
                            ECG_LOG_WARN("Failed to parse zone blacklist entry '%s' in response group %u: %s",
                                token.c_str(), group.id, e.what());
                        }
                    }
                }
            }

            group.minThreatLevel = fields[11].Get<uint8>();
            group.requiresZoneFallen = fields[12].Get<bool>();
            group.priority = fields[13].Get<uint32>();
            group.enabled = fields[14].Get<bool>();

            m_factionResponses.push_back(group);

            ECG_LOG_DEBUG("Loaded faction response '%s' (tier %u, faction %u, %zu creatures)",
                group.responseName.c_str(), group.responseTier, group.faction,
                group.creatureEntries.size());

        } while (result->NextRow());

        ECG_LOG_INFO("Loaded %zu faction response groups from database", m_factionResponses.size());
    }

    const FactionResponseGroup* ZoneStateManager::GetBestResponseForZone(uint32 zoneId) const
    {
        const ZoneState* zone = GetZoneState(zoneId);  // Now uses const overload
        if (!zone)
            return nullptr;

        // Find the best matching response (sorted by priority)
        for (const auto& response : m_factionResponses)
        {
            if (response.AppliesToZone(zoneId, zone->owningFaction, zone->threatLevel, zone->IsFallen()))
            {
                return &response;
            }
        }

        return nullptr;
    }
}
