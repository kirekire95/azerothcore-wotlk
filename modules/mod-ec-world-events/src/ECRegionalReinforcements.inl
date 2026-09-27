/**
 * @file ECRegionalReinforcements.inl
 * @brief Physical settlement-to-settlement military support.
 * @date 2026-09-27
 *
 * Custom persistent-world adaptation of AV's strategic reinforcement/resource
 * dependency. Units originate at the source settlement and must survive a real
 * path to the target. No teleport-on-failure fallback exists.
 */
void WorldEventManager::DispatchRegionalReinforcements(ActiveEvent& event)
{
    EventDefinition const* def = GetEventDefinition(event.eventId);
    auto target = m_settlements.find(event.settlementId);
    if (!def || def->type != EventType::RAID || target == m_settlements.end())
        return;

    Map* map = sMapMgr->FindBaseNonInstanceMap(target->second.mapId);
    if (!map)
        return;

    for (SettlementLink const& link : m_settlementLinks)
    {
        if (link.targetSettlementId != event.settlementId || link.linkType != "supplies")
            continue;

        auto source = m_settlements.find(link.sourceSettlementId);
        if (source == m_settlements.end() || source->second.mapId != target->second.mapId ||
            source->second.damagePct >= 80)
            continue;

        QueryResult operational = WorldDatabase.Query(Acore::StringFormat(
            "SELECT COUNT(*) FROM ec_world_settlement_nodes WHERE settlement_id = {} "
            "AND node_type = 'supplies' AND state = 0 AND health_pct > 0", source->first));
        if (!operational || operational->Fetch()[0].Get<uint32>() == 0)
        {
            EC_LOG_INFO("Regional military support from '%s' unavailable: supply node offline",
                source->second.name.c_str());
            continue;
        }

        uint32 guardEntry = 0;
        if (!source->second.guardTiers.empty())
        {
            auto tier = source->second.guardTiers.upper_bound(source->second.developmentLevel);
            if (tier != source->second.guardTiers.begin())
                guardEntry = (--tier)->second;
        }
        if (!guardEntry || !sObjectMgr->GetCreatureTemplate(guardEntry))
            continue;

        ActiveEvent::RegionalColumn column;
        column.sourceSettlementId = source->first;
        column.targetSettlementId = target->first;
        column.dispatched = true;

        uint32 count = 3 + std::min<uint32>(5, source->second.developmentLevel);
        for (uint32 i = 0; i < count; ++i)
        {
            float angle = float(i) / std::max<uint32>(1, count) * 2.0f * float(M_PI);
            float x = source->second.centerX + std::cos(angle) * 6.0f;
            float y = source->second.centerY + std::sin(angle) * 6.0f;
            TempSummon* unit = map->SummonCreature(
                guardEntry, {x, y, source->second.centerZ, 0.0f}, nullptr, 0, nullptr, 0);
            if (!unit)
                continue;

            unit->SetReactState(REACT_AGGRESSIVE);
            PathGenerator path(unit);
            bool valid = path.CalculatePath(target->second.centerX, target->second.centerY,
                target->second.centerZ, false);
            valid = valid && path.GetPathType() == PATHFIND_NORMAL && path.GetPath().size() >= 2;
            if (!valid)
            {
                unit->DespawnOrUnsummon();
                continue;
            }
            auto corridor = path.GetPath();
            unit->GetMotionMaster()->MoveSplinePath(&corridor, FORCED_MOVEMENT_RUN);
            column.units.push_back(unit->GetGUID());
            event.spawnedCreatures.push_back(unit->GetGUID());
        }

        if (!column.units.empty())
        {
            EC_LOG_INFO("Regional column dispatched: '%s' -> '%s', %zu troops physically marching",
                source->second.name.c_str(), target->second.name.c_str(), column.units.size());
            event.regionalColumns.push_back(std::move(column));
        }
    }
}

void WorldEventManager::ProcessRegionalReinforcements(ActiveEvent& event)
{
    if (event.phase != EventPhase::ACTIVE)
        return;

    auto target = m_settlements.find(event.settlementId);
    if (target == m_settlements.end())
        return;
    Map* map = sMapMgr->FindBaseNonInstanceMap(target->second.mapId);
    if (!map)
        return;

    constexpr float kArrivalRadius = 35.0f;
    for (ActiveEvent::RegionalColumn& column : event.regionalColumns)
    {
        if (!column.dispatched || column.arrived || column.failed)
            continue;

        uint32 alive = 0;
        uint32 arrived = 0;
        for (ObjectGuid const& guid : column.units)
        {
            Creature* unit = map->GetCreature(guid);
            if (!unit || !unit->IsAlive())
                continue;
            ++alive;
            float dx = unit->GetPositionX() - target->second.centerX;
            float dy = unit->GetPositionY() - target->second.centerY;
            if (dx * dx + dy * dy <= kArrivalRadius * kArrivalRadius)
                ++arrived;
        }

        if (alive == 0)
        {
            column.failed = true;
            EC_LOG_INFO("Regional reinforcement column from settlement %u was destroyed/intercepted before arrival",
                column.sourceSettlementId);
            continue;
        }

        if (arrived == alive)
        {
            column.arrived = true;
            EC_LOG_INFO("Regional reinforcement column from settlement %u reached '%s' with %u/%zu survivors",
                column.sourceSettlementId, target->second.name.c_str(), alive, column.units.size());
            for (ObjectGuid const& guid : column.units)
            {
                if (Creature* unit = map->GetCreature(guid); unit && unit->IsAlive() && !unit->IsInCombat())
                    unit->GetMotionMaster()->MoveRandom(18.0f);
            }
        }
    }
}
