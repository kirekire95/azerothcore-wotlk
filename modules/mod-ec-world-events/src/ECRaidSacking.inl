/**
 * @file ECRaidSacking.inl
 * @brief Physical post-defeat RAID sacking.
 * @date 2026-09-27
 *
 * Custom gameplay design: 80% attrition breaks the settlement's organized
 * defense, but persistent damage is earned only by physically destroying
 * capturable event structures. No timer destroys a structure by itself.
 */
void WorldEventManager::ProcessRaidSacking(ActiveEvent& event, uint32 diff)
{
#ifdef MOD_PLAYERBOTS
    if (!event.raidSacking || event.phase != EventPhase::ACTIVE)
        return;

    EventDefinition const* def = GetEventDefinition(event.eventId);
    auto settlementIt = m_settlements.find(event.settlementId);
    if (!def || def->type != EventType::RAID || settlementIt == m_settlements.end())
        return;

    Map* map = sMapMgr->FindBaseNonInstanceMap(def->location.mapId);
    if (!map)
        return;

    event.raidSackingElapsedMs += diff;
    constexpr uint32 kSackStartDelayMs = 5000;
    constexpr uint32 kSackTargetIntervalMs = 12000;
    constexpr float kSackArrivalRadius = 7.0f;
    if (event.raidSackingElapsedMs < kSackStartDelayMs)
        return;

    std::vector<uint32> targets;
    for (auto const& [objectId, building] : event.buildings)
        if (building.state == BuildingState::INTACT)
            targets.push_back(objectId);
    std::sort(targets.begin(), targets.end());

    uint32 allowedTargets = 1 + (event.raidSackingElapsedMs - kSackStartDelayMs) / kSackTargetIntervalMs;
    uint32 started = static_cast<uint32>(event.raidSackTargetsStarted.size());
    for (uint32 objectId : targets)
    {
        if (started >= allowedTargets)
            break;
        SpawnedBuilding& building = event.buildings.at(objectId);
        GameObject* object = map->GetGameObject(building.objectGuid);
        if (!object)
            continue;

        Player* nearest = nullptr;
        float nearestDistance = std::numeric_limits<float>::max();
        for (ObjectGuid const& guid : event.spawnedBots)
        {
            Player* bot = ObjectAccessor::FindPlayer(guid);
            auto checkpoint = m_botReturnContexts.find(guid);
            if (!bot || checkpoint == m_botReturnContexts.end() ||
                !RaidOwnsBot(event, checkpoint->second, bot, *def) || !bot->IsAlive())
                continue;
            float distance = bot->GetDistance(object);
            if (distance < nearestDistance)
            {
                nearest = bot;
                nearestDistance = distance;
            }
        }
        if (!nearest)
            continue;

        if (nearestDistance <= kSackArrivalRadius)
        {
            StartBuildingCapture(event, objectId, nearest);
            if (building.state == BuildingState::BURNING)
            {
                event.raidSackTargetsStarted.insert(objectId);
                ++started;
            }
            continue;
        }

        if (!nearest->IsInCombat() && !nearest->IsMovementPreventedByCasting())
        {
            PathGenerator path(nearest);
            bool valid = path.CalculatePath(object->GetPositionX(), object->GetPositionY(), object->GetPositionZ(), false);
            valid = valid && path.GetPathType() == PATHFIND_NORMAL && path.GetPath().size() >= 2;
            if (valid)
            {
                auto corridor = path.GetPath();
                nearest->GetMotionMaster()->MoveSplinePath(&corridor, FORCED_MOVEMENT_RUN);
            }
        }
    }

    uint32 destroyed = 0;
    uint32 capturable = 0;
    for (auto const& [objectId, building] : event.buildings)
    {
        (void)objectId;
        ++capturable;
        if (building.state == BuildingState::DESTROYED)
            ++destroyed;
    }

    if (capturable == 0)
    {
        LOG_WARN("module.ec-events", "Raid {} has no physical sack targets; no abstract settlement damage applied",
            event.instanceId);
        event.raidSacking = false;
        TransitionToPhase(event, EventPhase::ENDING);
        return;
    }

    if (destroyed == capturable)
    {
        SettlementState& settlement = settlementIt->second;
        uint32 physicalDamage = std::min<uint32>(80, destroyed * 80 / capturable);
        settlement.damagePct = std::max<uint8>(settlement.damagePct, static_cast<uint8>(physicalDamage));

        QueryResult nodeResult = WorldDatabase.Query(Acore::StringFormat(
            "SELECT id, position_x, position_y, radius FROM ec_world_settlement_nodes "
            "WHERE settlement_id = {} AND state <> 2", settlement.id));
        if (nodeResult)
        {
            do
            {
                Field* fields = nodeResult->Fetch();
                uint32 nodeId = fields[0].Get<uint32>();
                float nodeX = fields[1].Get<float>();
                float nodeY = fields[2].Get<float>();
                float nodeRadius = fields[3].Get<float>();
                bool physicallyReached = false;
                for (ObjectGuid const& guid : event.spawnedBots)
                {
                    Player* attacker = ObjectAccessor::FindPlayer(guid);
                    auto checkpoint = m_botReturnContexts.find(guid);
                    if (!attacker || checkpoint == m_botReturnContexts.end() ||
                        !RaidOwnsBot(event, checkpoint->second, attacker, *def) || !attacker->IsAlive())
                        continue;
                    float dx = attacker->GetPositionX() - nodeX;
                    float dy = attacker->GetPositionY() - nodeY;
                    if (dx * dx + dy * dy <= nodeRadius * nodeRadius)
                    {
                        physicallyReached = true;
                        break;
                    }
                }
                if (physicallyReached)
                {
                    WorldDatabase.Execute(
                        "UPDATE ec_world_settlement_nodes SET state = 2, health_pct = 0, rebuild_progress = 0 WHERE id = {}",
                        nodeId);
                    LOG_INFO("module.ec-events", "Raid {} physically destroyed settlement resource node {}",
                        event.instanceId, nodeId);
                }
            } while (nodeResult->NextRow());
        }
        WorldDatabase.Execute(
            "UPDATE ec_world_settlements SET damage_pct = {}, last_economy_update = CURRENT_TIMESTAMP WHERE id = {}",
            settlement.damagePct, settlement.id);
        LOG_INFO("module.ec-events", "Raid {} sack complete: {}/{} structures physically destroyed; settlement damage {}%",
            event.instanceId, destroyed, capturable, settlement.damagePct);
        event.raidSacking = false;
        TransitionToPhase(event, EventPhase::ENDING);
    }
#else
    (void)event;
    (void)diff;
#endif
}
