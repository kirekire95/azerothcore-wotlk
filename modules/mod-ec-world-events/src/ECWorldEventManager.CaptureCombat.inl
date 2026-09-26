void WorldEventManager::UpdateBuildingCaptures(ActiveEvent& event, uint32 /*diff*/)
{
    auto now = std::chrono::system_clock::now();

    for (auto& pair : event.buildings)
    {
        SpawnedBuilding& building = pair.second;

        // Only check buildings that are burning (being captured)
        if (building.state != BuildingState::BURNING)
            continue;

        // Check if capture timer has completed
        if (now >= building.captureEndTime)
        {
            CompleteBuildingCapture(event, building);
        }
    }
}

void WorldEventManager::StartBuildingCapture(ActiveEvent& event, uint32 objectDbId, Player* capturer)
{
    auto it = event.buildings.find(objectDbId);
    if (it == event.buildings.end())
    {
        EC_LOG_WARN("StartBuildingCapture: Building %u not found in event %u", objectDbId, event.instanceId);
        return;
    }

    SpawnedBuilding& building = it->second;

    // Can only capture intact buildings
    if (building.state != BuildingState::INTACT)
    {
        EC_LOG_DEBUG("Building '%s' is already %s, cannot capture",
            building.buildingName.c_str(), BuildingStateToString(building.state).c_str());
        return;
    }

    const EventDefinition* def = GetEventDefinition(event.eventId);
    if (!def)
        return;

    // Find the object definition to get capture time
    const EventObject* objDef = nullptr;
    for (const EventObject& obj : def->objects)
    {
        if (obj.objectId == objectDbId)
        {
            objDef = &obj;
            break;
        }
    }

    if (!objDef || !objDef->isCapturable)
    {
        EC_LOG_WARN("StartBuildingCapture: Object %u is not capturable", objectDbId);
        return;
    }

    // Determine faction
    EventFaction capturerFaction = EventFaction::NEUTRAL;
    if (capturer->GetTeamId() == TEAM_ALLIANCE)
        capturerFaction = EventFaction::ALLIANCE;
    else if (capturer->GetTeamId() == TEAM_HORDE)
        capturerFaction = EventFaction::HORDE;

    // RAID hardening: defender infrastructure should not begin burning while
    // defender NPC presence still exists in the objective area.
    if (def->type == EventType::RAID && capturerFaction == def->attackerFaction)
    {
        Map* map = capturer->GetMap();
        if (!map)
            map = sMapMgr->FindBaseNonInstanceMap(def->location.mapId);

        uint32 aliveDefendersNearCity = CountAliveRaidDefendersNearObjective(map, *def);

        if (aliveDefendersNearCity > 0)
        {
            EC_LOG_INFO("StartBuildingCapture blocked for '%s': %u defender NPCs still alive within %.0f yards",
                building.buildingName.c_str(), aliveDefendersNearCity, kRaidTakeoverClearRadiusYards);

            if (WorldSession* session = capturer->GetSession())
            {
                ChatHandler(session).PSendSysMessage(
                    "Cannot burn %s yet. %u defender NPCs are still alive in the area.",
                    building.buildingName.c_str(), aliveDefendersNearCity
                );
            }
            return;
        }
    }

    // Set up capture state
    building.state = BuildingState::BURNING;
    building.capturerGuid = capturer->GetGUID();
    building.capturingFaction = capturerFaction;
    building.captureStartTime = std::chrono::system_clock::now();
    building.captureEndTime = building.captureStartTime + std::chrono::seconds(objDef->captureTimeSeconds);

    EC_LOG_INFO("Building '%s' capture started by %s (%s) - %u seconds to complete",
        building.buildingName.c_str(), capturer->GetName().c_str(),
        EventFactionToString(capturerFaction).c_str(), objDef->captureTimeSeconds);

    // Transition to burning visual
    TransitionBuildingState(event, building, BuildingState::BURNING);

    // Broadcast event
    BroadcastBuildingEvent(event, building, "is being assaulted");

    // Emit to bridge
    sWorldEventBridge->EmitBuildingAssault(event.instanceId, objectDbId, building.buildingName,
        capturer->GetGUID().GetCounter(), capturer->GetName(), static_cast<uint8>(capturerFaction));

    // Alert guards assigned to this building - they'll attack the intruder!
    AlertBuildingGuards(event, objectDbId, capturer);
}

void WorldEventManager::CancelBuildingCapture(ActiveEvent& event, uint32 objectDbId)
{
    auto it = event.buildings.find(objectDbId);
    if (it == event.buildings.end())
        return;

    SpawnedBuilding& building = it->second;

    if (building.state != BuildingState::BURNING)
        return;

    EC_LOG_INFO("Building '%s' capture cancelled - returning to intact state",
        building.buildingName.c_str());

    // Revert to intact state
    building.state = BuildingState::INTACT;
    building.capturerGuid = ObjectGuid::Empty;
    building.capturingFaction = EventFaction::NEUTRAL;

    TransitionBuildingState(event, building, BuildingState::INTACT);

    // Broadcast
    BroadcastBuildingEvent(event, building, "assault has been repelled");

    // Emit to bridge
    sWorldEventBridge->EmitBuildingDefended(event.instanceId, objectDbId, building.buildingName);
}

void WorldEventManager::CompleteBuildingCapture(ActiveEvent& event, SpawnedBuilding& building)
{
    const EventDefinition* def = GetEventDefinition(event.eventId);
    if (!def)
        return;

    EC_LOG_INFO("Building '%s' has been DESTROYED by %s! Points: %d",
        building.buildingName.c_str(),
        EventFactionToString(building.capturingFaction).c_str(),
        building.pointsValue);

    // Award points to capturing faction
    if (building.capturingFaction == def->attackerFaction)
        event.attackerScore += building.pointsValue;
    else if (building.capturingFaction == def->defenderFaction)
        event.defenderScore += building.pointsValue;

    // Transition to destroyed visual
    TransitionBuildingState(event, building, BuildingState::DESTROYED);

    // Get capturer name if still online
    std::string capturerName = "Unknown";
    if (Player* capturer = ObjectAccessor::FindPlayer(building.capturerGuid))
        capturerName = capturer->GetName();

    // Broadcast
    std::stringstream msg;
    msg << "has been DESTROYED by " << capturerName << "!";
    BroadcastBuildingEvent(event, building, msg.str());

    // Emit to bridge
    sWorldEventBridge->EmitBuildingDestroyed(event.instanceId, building.objectDbId,
        building.buildingName, building.capturerGuid.GetCounter(), capturerName,
        static_cast<uint8>(building.capturingFaction), building.pointsValue);

    // Building capture counts as an objective completion
    sWorldEventBridge->EmitObjectiveCompleted(event.eventId, event.instanceId,
        building.objectDbId, building.buildingName);

    // Check if this capture triggers early victory (all buildings destroyed)
    CheckEarlyVictory(event, event.instanceId);
}

void WorldEventManager::TransitionBuildingState(ActiveEvent& event, SpawnedBuilding& building, BuildingState newState)
{
    const EventDefinition* def = GetEventDefinition(event.eventId);
    if (!def)
        return;

    // Find the object definition for visual entries
    const EventObject* objDef = nullptr;
    for (const EventObject& obj : def->objects)
    {
        if (obj.objectId == building.objectDbId)
        {
            objDef = &obj;
            break;
        }
    }

    if (!objDef)
        return;

    Map* map = sMapMgr->FindBaseNonInstanceMap(def->location.mapId);
    if (!map)
        return;

    building.state = newState;

    // Spawn/despawn visual GameObjects based on state
    switch (newState)
    {
        case BuildingState::INTACT:
        {
            // Despawn burning visual if exists
            if (building.burningGuid)
            {
                if (GameObject* go = map->GetGameObject(building.burningGuid))
                    go->Delete();
                building.burningGuid = ObjectGuid::Empty;
            }
            break;
        }

        case BuildingState::BURNING:
        {
            // Spawn burning visual if entry exists
            if (objDef->burningEntry)
            {
                // Get position from intact building
                GameObject* intact = map->GetGameObject(building.objectGuid);
                if (intact)
                {
                    float x = intact->GetPositionX();
                    float y = intact->GetPositionY();
                    float z = intact->GetPositionZ();
                    float o = intact->GetOrientation();

                    GameObject* burning = new GameObject();
                    if (burning->Create(
                        map->GenerateLowGuid<HighGuid::GameObject>(),
                        objDef->burningEntry, map, PHASEMASK_NORMAL, x, y, z, o,
                        G3D::Quat(0, 0, 0, 1), 255, GO_STATE_READY))
                    {
                        burning->SetRespawnTime(0);
                        map->AddToMap(burning);
                        building.burningGuid = burning->GetGUID();
                        event.spawnedObjects.push_back({burning->GetGUID(), 0}); // despawnPhase 0 = managed by building lifecycle
                        EC_LOG_DEBUG("Spawned burning visual for '%s'", building.buildingName.c_str());
                    }
                    else
                    {
                        delete burning;
                    }
                }
            }
            break;
        }

        case BuildingState::DESTROYED:
        {
            // Despawn intact building
            if (building.objectGuid)
            {
                if (GameObject* go = map->GetGameObject(building.objectGuid))
                    go->Delete();
            }

            // Despawn burning visual
            if (building.burningGuid)
            {
                if (GameObject* go = map->GetGameObject(building.burningGuid))
                    go->Delete();
                building.burningGuid = ObjectGuid::Empty;
            }

            // Spawn destroyed visual if entry exists
            if (objDef->destroyedEntry)
            {
                float x = def->location.x + objDef->offsetX;
                float y = def->location.y + objDef->offsetY;
                float z = def->location.z + objDef->offsetZ;

                GameObject* destroyed = new GameObject();
                if (destroyed->Create(
                    map->GenerateLowGuid<HighGuid::GameObject>(),
                    objDef->destroyedEntry, map, PHASEMASK_NORMAL, x, y, z, objDef->orientation,
                    G3D::Quat(0, 0, 0, 1), 255, GO_STATE_READY))
                {
                    destroyed->SetRespawnTime(0);
                    map->AddToMap(destroyed);
                    building.destroyedGuid = destroyed->GetGUID();
                    event.spawnedObjects.push_back({destroyed->GetGUID(), 0}); // despawnPhase 0 = managed by building lifecycle
                    EC_LOG_DEBUG("Spawned destroyed visual for '%s'", building.buildingName.c_str());
                }
                else
                {
                    delete destroyed;
                }
            }
            break;
        }
    }
}

SpawnedBuilding* WorldEventManager::FindBuildingByGuid(ActiveEvent& event, ObjectGuid guid)
{
    for (auto& pair : event.buildings)
    {
        if (pair.second.objectGuid == guid)
            return &pair.second;
    }
    return nullptr;
}

void WorldEventManager::BroadcastBuildingEvent(const ActiveEvent& event, const SpawnedBuilding& building, const std::string& action)
{
    const EventDefinition* def = GetEventDefinition(event.eventId);
    if (!def)
        return;

    std::stringstream msg;
    msg << "|cffff0000[" << def->name << "]|r " << building.buildingName << " " << action;

    BroadcastEventMessage(msg.str(), EventFaction::NEUTRAL);
}

void WorldEventManager::AlertBuildingGuards(ActiveEvent& event, uint32 buildingObjectId, Player* attacker)
{
    const EventDefinition* def = GetEventDefinition(event.eventId);
    if (!def || !attacker)
        return;

    EC_LOG_DEBUG("AlertBuildingGuards: Alerting guards assigned to building %u, attacker: %s",
        buildingObjectId, attacker->GetName().c_str());

    // Find all creatures assigned to this building
    std::vector<uint32> assignedCreatureEntries;
    for (const EventCreature& creatureDef : def->creatures)
    {
        if (creatureDef.assignedBuildingId == buildingObjectId)
        {
            assignedCreatureEntries.push_back(creatureDef.creatureEntry);
        }
    }

    if (assignedCreatureEntries.empty())
    {
        EC_LOG_DEBUG("  No guards assigned to building %u", buildingObjectId);
        return;
    }

    // Find the spawned creatures and alert them
    Map* map = sMapMgr->FindMap(def->location.mapId, 0);
    if (!map)
        return;

    uint32 alertedCount = 0;
    for (const ObjectGuid& guid : event.spawnedCreatures)
    {
        Creature* creature = map->GetCreature(guid);
        if (!creature || !creature->IsAlive())
            continue;

        // Check if this creature is assigned to the building
        bool isAssigned = false;
        for (uint32 entry : assignedCreatureEntries)
        {
            if (creature->GetEntry() == entry)
            {
                isAssigned = true;
                break;
            }
        }

        if (!isAssigned)
            continue;

        // ALERT! Guards respond to the attack

        // Stop current patrol movement
        creature->GetMotionMaster()->Clear();

        // Yell an alert based on faction
        if (rand() % 2 == 0)  // 50% chance to yell
        {
            bool isHordeGuard = IsHordeAlignedUnit(creature);

            if (isHordeGuard)
            {
                static const char* hordeAlerts[] = {
                    "INTRUDER! Defend the tower!",
                    "Enemy spotted! Attack!",
                    "To arms! The enemy approaches!",
                    "LOK'TAR! Protect the fortification!"
                };
                creature->Yell(hordeAlerts[rand() % 4], LANG_UNIVERSAL);
            }
            else
            {
                static const char* allianceAlerts[] = {
                    "ALARM! The tower is under attack!",
                    "Enemy sighted! To your posts!",
                    "Defend the bunker! Charge!",
                    "For the Alliance! Stop them!"
                };
                creature->Yell(allianceAlerts[rand() % 4], LANG_UNIVERSAL);
            }
        }

        // Battle roar emote
        creature->HandleEmoteCommand(EMOTE_ONESHOT_BATTLE_ROAR);

        // Start running (not walking) toward the attacker
        creature->SetWalk(false);

        // Attack the intruder!
        if (creature->AI())
            creature->AI()->AttackStart(attacker);

        EC_LOG_DEBUG("  Guard %s (entry %u) alerted and attacking %s",
            creature->GetName().c_str(), creature->GetEntry(), attacker->GetName().c_str());

        ++alertedCount;
    }

    EC_LOG_INFO("Alerted %u guards assigned to building %u to attack %s",
        alertedCount, buildingObjectId, attacker->GetName().c_str());
}

void WorldEventManager::ProcessBuildingGuardRespawns(ActiveEvent& event, uint32 diff)
{
    if (event.phase != EventPhase::ACTIVE)
        return;

    // RAID town defenders are a finite force for the duration of the assault.
    // Do not manufacture replacement guards while the 80% attrition objective is active.
    if (event.townPopulationStart > 0)
        return;

    const EventDefinition* def = GetEventDefinition(event.eventId);
    if (!def)
        return;

    Map* map = sMapMgr->FindBaseNonInstanceMap(def->location.mapId);
    if (!map)
        return;

    for (auto& [objectDbId, building] : event.buildings)
    {
        // Only INTACT buildings produce guards (barracks-style)
        if (building.state != BuildingState::INTACT)
            continue;

        // Only buildings with guard respawn enabled
        if (building.guardRespawnMs == 0 || building.maxGuards == 0)
            continue;

        building.lastGuardRespawnMs += diff;

        if (building.lastGuardRespawnMs < building.guardRespawnMs)
            continue;

        // Timer expired — reset and check if we need to spawn
        building.lastGuardRespawnMs = 0;

        // Count alive guards belonging to this building, prune dead ones
        uint32 aliveGuards = 0;
        auto it = building.guardGuids.begin();
        while (it != building.guardGuids.end())
        {
            Creature* guard = map->GetCreature(*it);
            if (guard && guard->IsAlive())
            {
                ++aliveGuards;
                ++it;
            }
            else
            {
                it = building.guardGuids.erase(it);
            }
        }

        if (aliveGuards >= building.maxGuards)
        {
            EC_LOG_TRACE("Building '%s': %u/%u guards alive, at cap",
                building.buildingName.c_str(), aliveGuards, building.maxGuards);
            continue;
        }

        // Find a guard creature template assigned to this building
        const EventCreature* guardTemplate = nullptr;
        for (const EventCreature& creatureDef : def->creatures)
        {
            if (creatureDef.assignedBuildingId == objectDbId &&
                !creatureDef.isBoss && !creatureDef.isLeader)
            {
                guardTemplate = &creatureDef;
                break;
            }
        }

        if (!guardTemplate)
            continue;

        // Find the building's object definition for position
        const EventObject* objDef = nullptr;
        for (const EventObject& obj : def->objects)
        {
            if (obj.objectId == objectDbId)
            {
                objDef = &obj;
                break;
            }
        }
        if (!objDef)
            continue;

        // Spawn at building location with random offset
        float randomAngle = (rand() % 360) * M_PI / 180.0f;
        float randomDist = 3.0f + (rand() % 5);
        float worldX = def->location.x + objDef->offsetX + cosf(randomAngle) * randomDist;
        float worldY = def->location.y + objDef->offsetY + sinf(randomAngle) * randomDist;
        float worldZ = def->location.z + objDef->offsetZ;

        TempSummon* summon = map->SummonCreature(
            guardTemplate->creatureEntry,
            {worldX, worldY, worldZ, guardTemplate->orientation},
            nullptr, 0, nullptr, 0
        );

        if (!summon)
        {
            EC_LOG_ERROR("Failed to respawn guard for building '%s'",
                building.buildingName.c_str());
            continue;
        }

        summon->SetReactState(REACT_AGGRESSIVE);

        // Set patrol behavior around building
        float patrolRadius = guardTemplate->patrolRadius > 0 ? guardTemplate->patrolRadius : 10.0f;
        summon->SetWalk(true);
        summon->GetMotionMaster()->MoveRandom(patrolRadius);

        // Track the guard
        building.guardGuids.push_back(summon->GetGUID());
        event.spawnedCreatures.push_back(summon->GetGUID());
        building.guardsProduced++;

        EC_LOG_INFO("Building '%s' produced guard '%s' (%u/%u alive)",
            building.buildingName.c_str(), guardTemplate->name.c_str(),
            aliveGuards + 1, building.maxGuards);
    }
}

void WorldEventManager::ProcessEventCreatureCombat(ActiveEvent& event, uint32 diff)
{
    if (event.phase != EventPhase::ACTIVE)
        return;

    event.combatScanElapsedMs += diff;
    event.combatStatusElapsedMs += diff;
    event.combatIdleWanderElapsedMs += diff;
    if (event.combatScanElapsedMs < kEventNpcAggroScanIntervalMs)
        return;

    event.combatScanElapsedMs = 0;
    bool issueIdleWanderNudge = event.combatIdleWanderElapsedMs >= kEventNpcIdleWanderIntervalMs;

    const EventDefinition* def = GetEventDefinition(event.eventId);
    if (!def)
        return;

    Map* map = sMapMgr->FindBaseNonInstanceMap(def->location.mapId);
    if (!map)
        return;

    uint32 aliveCombatants = 0;
    uint32 engagedCombatants = 0;
    uint32 targetsAcquired = 0;
    uint32 removedStale = 0;
    uint32 idleWanderNudges = 0;

    for (auto it = event.spawnedCreatures.begin(); it != event.spawnedCreatures.end();)
    {
        Creature* creature = map->GetCreature(*it);
        if (!creature || !creature->IsAlive())
        {
            it = event.spawnedCreatures.erase(it);
            ++removedStale;
            continue;
        }

        ++it;

        // Workers are visual setup actors; they should not enter combat loops.
        if (IsConstructionWorkerName(creature->GetName()))
            continue;

        ++aliveCombatants;
        if (creature->IsInCombat())
        {
            ++engagedCombatants;
            continue;
        }

        Unit* target = creature->SelectNearestTarget(kEventNpcAggroRangeYards);
        if (!target || !target->IsAlive())
        {
            EventFaction creatureFaction = GetUnitEventFaction(creature);
            EventFaction preferredTargetFaction = OpposingFaction(creatureFaction);
            bool includeNeutralTargets = (def->type == EventType::RAID && creatureFaction == def->attackerFaction);

            if (preferredTargetFaction != EventFaction::NEUTRAL || includeNeutralTargets)
            {
                target = FindRaidFallbackTarget(creature, map, preferredTargetFaction,
                    includeNeutralTargets, kEventNpcAggroRangeYards, def->location.zoneId);
            }
        }

        if (!target || !target->IsAlive())
        {
            if (issueIdleWanderNudge)
            {
                float rangeSpan = kEventNpcIdleWanderRadiusMaxYards - kEventNpcIdleWanderRadiusMinYards;
                float wanderRadius = kEventNpcIdleWanderRadiusMinYards +
                    (static_cast<float>(rand()) / static_cast<float>(RAND_MAX)) * rangeSpan;
                creature->SetWalk(false);
                creature->GetMotionMaster()->MoveRandom(wanderRadius);
                ++idleWanderNudges;
            }
            continue;
        }

        creature->SetWalk(false);
        if (creature->AI())
            creature->AI()->AttackStart(target);
        else
            creature->Attack(target, true);

        ++targetsAcquired;
        ++engagedCombatants;
    }

    event.aliveCombatants = aliveCombatants;
    event.engagedCombatants = engagedCombatants;
    if (issueIdleWanderNudge)
        event.combatIdleWanderElapsedMs = 0;

    if (event.combatStatusElapsedMs >= kEventNpcStatusLogIntervalMs)
    {
        event.combatStatusElapsedMs = 0;
        uint32 idleCombatants = (aliveCombatants > engagedCombatants)
            ? (aliveCombatants - engagedCombatants) : 0;
        EC_LOG_INFO("Combat status for '%s' (Instance %u): alive=%u engaged=%u idle=%u idle_wander_nudges=%u",
            def->name.c_str(), event.instanceId, aliveCombatants, engagedCombatants, idleCombatants, idleWanderNudges);
    }
    else if (targetsAcquired > 0 || removedStale > 0 || idleWanderNudges > 0)
    {
        EC_LOG_DEBUG("ProcessEventCreatureCombat: '%s' (Instance %u) acquired=%u stale_removed=%u idle_wander_nudges=%u",
            def->name.c_str(), event.instanceId, targetsAcquired, removedStale, idleWanderNudges);
    }
}

bool WorldEventManager::OnBuildingInteract(Player* player, GameObject* building)
{
    if (!player || !building)
        return false;

    // Find which active event this building belongs to
    for (auto& pair : m_activeEvents)
    {
        ActiveEvent& event = pair.second;

        // Check if this building is capturable in this event
        SpawnedBuilding* spawnedBuilding = FindBuildingByGuid(event, building->GetGUID());
        if (!spawnedBuilding)
            continue;

        // Found it - try to start capture
        if (spawnedBuilding->state == BuildingState::INTACT)
        {
            StartBuildingCapture(event, spawnedBuilding->objectDbId, player);
            return true;
        }
        else if (spawnedBuilding->state == BuildingState::BURNING)
        {
            // If same faction is clicking, maybe show progress
            // If opposite faction is clicking, cancel the capture
            EventFaction playerFaction = (player->GetTeamId() == TEAM_ALLIANCE) ? EventFaction::ALLIANCE : EventFaction::HORDE;
            if (playerFaction != spawnedBuilding->capturingFaction)
            {
                CancelBuildingCapture(event, spawnedBuilding->objectDbId);
                return true;
            }
        }
        break;
    }

    return false;
}

bool WorldEventManager::IsBuildingCapturable(GameObject* building) const
{
    if (!building)
        return false;

    for (const auto& pair : m_activeEvents)
    {
        const ActiveEvent& event = pair.second;

        for (const auto& bPair : event.buildings)
        {
            if (bPair.second.objectGuid == building->GetGUID())
                return bPair.second.state == BuildingState::INTACT;
        }
    }

    return false;
}

std::string WorldEventManager::BuildingStateToString(BuildingState state)
{
    switch (state)
    {
        case BuildingState::INTACT:    return "INTACT";
        case BuildingState::BURNING:   return "BURNING";
        case BuildingState::DESTROYED: return "DESTROYED";
        default:                       return "UNKNOWN";
    }
}

// =============================================================================
// Victory/Defeat Determination
// =============================================================================
