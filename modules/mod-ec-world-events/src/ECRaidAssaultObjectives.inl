/**
 * @file ECRaidAssaultObjectives.inl
 * @brief Staged raid-objective adapter.
 * @date 2026-09-26
 */
#ifdef MOD_PLAYERBOTS
#include "PathGenerator.h"

namespace
{
    constexpr uint64 kRaidBuildingSourceBit = uint64(1) << 63;
    constexpr uint64 kRaidResourceSourceBit = uint64(1) << 62;
    constexpr float kRaidObjectiveDefenseRadius = 24.0f;

    EC::RaidAssault::Point RaidPoint(WorldObject const* unit)
    {
        return {unit->GetPositionX(), unit->GetPositionY(), unit->GetPositionZ()};
    }

    bool RaidOwnsBot(ActiveEvent const& event, BotReturnContext const& checkpoint,
        Player* bot, EventDefinition const& def)
    {
        if (!bot || checkpoint.eventInstanceId != event.instanceId || !bot->IsInWorld() ||
            bot->IsDuringRemoveFromWorld() || bot->IsBeingTeleported() || !bot->GetSession() ||
            bot->GetSession()->IsLoggingOut() || bot->GetMapId() != def.location.mapId ||
            !TeamMatchesEventFaction(bot->GetTeamId(), def.attackerFaction) ||
            bot->GetGroup() || bot->InBattleground() || bot->InBattlegroundQueue() ||
            bot->IsInFlight() || bot->GetVehicle() || bot->GetTransport() ||
            sLFGMgr->GetState(bot->GetGUID()) != lfg::LFG_STATE_NONE)
            return false;
        PlayerbotAI* ai = sPlayerbotsMgr.GetPlayerbotAI(bot);
        return ai && !IsRealPlayer(bot) && !ai->HasGameClientMaster();
    }

    class RaidLocalDefenderCollector
    {
    public:
        explicit RaidLocalDefenderCollector(EventFaction faction) : _faction(faction) { }
        std::vector<Unit*> units;
        void Visit(std::unordered_map<ObjectGuid, Creature*>& creatures)
        {
            for (auto const& [guid, creature] : creatures)
            {
                (void)guid;
                if (creature && creature->IsInWorld() && !creature->IsDuringRemoveFromWorld() &&
                    creature->IsAlive() && !creature->IsCritter() && !creature->IsPet() &&
                    !creature->IsTotem() && !creature->IsTrigger() &&
                    GetUnitEventFaction(creature) == _faction)
                    units.push_back(creature);
            }
        }
        template<class T> void Visit(std::unordered_map<ObjectGuid, T*>&) { }
    private:
        EventFaction _faction;
    };
}
#endif

void WorldEventManager::ProcessRaidAttackerAdvance(ActiveEvent& event, uint32 diff)
{
#ifdef MOD_PLAYERBOTS
    if (event.phase != EventPhase::ACTIVE)
        return;
    EventDefinition const* def = GetEventDefinition(event.eventId);
    if (!def || def->type != EventType::RAID)
        return;
    event.raidObjectiveElapsedMs += diff;
    auto const now = event.raidObjectiveElapsedMs;
    Map* map = sMapMgr->FindBaseNonInstanceMap(def->location.mapId);
    if (!map)
        return;

    if (!event.raidObjectivesAttempted)
    {
        event.raidObjectivesAttempted = true;
        auto settlement = m_settlements.find(event.settlementId);
        if (settlement == m_settlements.end() || event.townPopulation.empty())
        {
            LOG_ERROR("module.ec-events", "Raid {} has no verified settlement roster; staged advance blocked",
                event.instanceId);
            return;
        }
        auto const& town = settlement->second;
        EC::RaidAssault::Point center{town.centerX, town.centerY, town.centerZ};
        auto tier = [&](EC::RaidAssault::Point p)
        {
            double const r2 = (p.x-center.x)*(p.x-center.x) + (p.y-center.y)*(p.y-center.y);
            if (r2 >= town.populationRadius*town.populationRadius*0.36)
                return EC::RaidAssault::Tier::Outer;
            if (r2 <= town.populationRadius*town.populationRadius*0.0625)
                return EC::RaidAssault::Tier::Inner;
            return EC::RaidAssault::Tier::Military;
        };
        std::vector<EC::RaidAssault::Candidate> candidates;
        for (ObjectGuid const& stored : event.townPopulation)
        {
            uint32 const spawnId = stored.GetCounter();
            CreatureData const* data = sObjectMgr->GetCreatureData(spawnId);
            if (!data || data->mapid != def->location.mapId)
            {
                LOG_ERROR("module.ec-events", "Raid {} resident source {} is missing; plan blocked",
                    event.instanceId, stored.ToString());
                event.raidAssaultResidentSources.clear();
                return;
            }
            EC::RaidAssault::Point p{data->posX, data->posY, data->posZ};
            candidates.push_back({spawnId, p, tier(p), EC::RaidAssault::Source::ResidentSpawn});
            event.raidAssaultResidentSources.emplace(spawnId, stored);
        }
        QueryResult resourceNodes = WorldDatabase.Query(Acore::StringFormat(
            "SELECT id, position_x, position_y, position_z FROM ec_world_settlement_nodes "
            "WHERE settlement_id = {} AND state = 0", event.settlementId));
        if (resourceNodes)
        {
            do
            {
                Field* fields = resourceNodes->Fetch();
                uint32 nodeId = fields[0].Get<uint32>();
                uint64 source = kRaidResourceSourceBit | nodeId;
                EC::RaidAssault::Point p{fields[1].Get<float>(), fields[2].Get<float>(), fields[3].Get<float>()};
                candidates.push_back({source, p, tier(p), EC::RaidAssault::Source::AssignedBuilding});
                event.raidAssaultResourceSources.emplace(source, nodeId);
            } while (resourceNodes->NextRow());
        }

        for (auto const& obj : def->objects)
        {
            if (!obj.isCapturable)
                continue;
            bool defended = std::any_of(def->creatures.begin(), def->creatures.end(), [&](auto const& c)
            {
                if (c.assignedBuildingId != obj.objectId)
                    return false;
                auto const* creatureTemplate = sObjectMgr->GetCreatureTemplate(c.creatureEntry);
                auto const* faction = creatureTemplate ?
                    sFactionTemplateStore.LookupEntry(creatureTemplate->faction) : nullptr;
                uint32 mask = def->defenderFaction == EventFaction::ALLIANCE ?
                    FACTION_MASK_ALLIANCE : FACTION_MASK_HORDE;
                return faction && (faction->ourMask & mask) != 0;
            });
            if (!defended)
                continue;
            uint64 source = kRaidBuildingSourceBit | obj.objectId;
            EC::RaidAssault::Point p{def->location.x + obj.offsetX, def->location.y + obj.offsetY,
                def->location.z + obj.offsetZ};
            candidates.push_back({source, p, tier(p), EC::RaidAssault::Source::AssignedBuilding});
            event.raidAssaultBuildingSources.emplace(source, obj.objectId);
        }
        try
        {
            event.raidAssaultController.Start(EC::RaidAssault::BuildObjectives(std::move(candidates), center), 0);
            LOG_INFO("module.ec-events", "Raid {} built {} staged objectives from verified content sources",
                event.instanceId, event.raidAssaultController.Objectives().size());
        }
        catch (std::exception const& error)
        {
            LOG_ERROR("module.ec-events", "Raid {} objective plan rejected: {}", event.instanceId, error.what());
            return;
        }
    }
    if (!event.raidAssaultController.IsActive())
        return;

    RaidLocalDefenderCollector defenders(def->defenderFaction);
    TypeContainerVisitor<RaidLocalDefenderCollector, MapStoredObjectTypesContainer> visitor(defenders);
    visitor.Visit(map->GetObjectsStore());
    for (auto const& ref : map->GetPlayers())
    {
        Player* player = ref.GetSource();
        if (player && player->IsInWorld() && !player->IsDuringRemoveFromWorld() && player->IsAlive() &&
            !player->IsGameMaster() && TeamMatchesEventFaction(player->GetTeamId(), def->defenderFaction))
            defenders.units.push_back(player);
    }

    std::map<EC::RaidAssault::Id, EC::RaidAssault::Observation> observations;
    for (auto const& [id, objective] : event.raidAssaultController.Objectives())
    {
        EC::RaidAssault::Observation state{true, 0, false};
        for (uint64 source : objective.sources)
        {
            auto resident = event.raidAssaultResidentSources.find(source);
            if (resident != event.raidAssaultResidentSources.end())
            {
                bool observed = event.townCasualtiesRecorded.count(resident->second) != 0;
                auto [first, last] = map->GetCreatureBySpawnIdStore().equal_range(static_cast<uint32>(source));
                bool living = false;
                for (auto it = first; it != last; ++it)
                {
                    Creature* creature = it->second;
                    if (!creature || !creature->IsInWorld() || creature->IsDuringRemoveFromWorld())
                        continue;
                    observed = true;
                    living = living || (creature->IsAlive() &&
                        EC::RaidAssault::DistanceSquared(RaidPoint(creature), objective.position) <=
                            kRaidObjectiveDefenseRadius*kRaidObjectiveDefenseRadius);
                }
                state.known = state.known && observed;
                state.livingDefenders += living ? 1 : 0;
            }
            else
            {
                auto resourceSource = event.raidAssaultResourceSources.find(source);
                if (resourceSource != event.raidAssaultResourceSources.end())
                {
                    state.known = true;
                    continue;
                }
                auto buildingSource = event.raidAssaultBuildingSources.find(source);
                if (buildingSource == event.raidAssaultBuildingSources.end())
                {
                    state.known = false;
                    continue;
                }
                auto building = event.buildings.find(buildingSource->second);
                state.known = state.known && building != event.buildings.end() &&
                    (building->second.state == BuildingState::DESTROYED ||
                        map->GetGameObject(building->second.objectGuid) != nullptr);
            }
        }
        for (Unit* defender : defenders.units)
            if (EC::RaidAssault::DistanceSquared(RaidPoint(defender), objective.position) <=
                kRaidObjectiveDefenseRadius*kRaidObjectiveDefenseRadius)
                state.contested = true;
        observations.emplace(id, state);
    }

    std::vector<EC::RaidAssault::Actor> actors;
    std::map<EC::RaidAssault::Id, Player*> bots;
    for (ObjectGuid const& guid : event.spawnedBots)
    {
        auto checkpoint = m_botReturnContexts.find(guid);
        Player* bot = ObjectAccessor::FindPlayer(guid);
        if (checkpoint == m_botReturnContexts.end() || !RaidOwnsBot(event, checkpoint->second, bot, *def))
            continue;
        uint64 const id = guid.GetRawValue();
        bots.emplace(id, bot);
        actors.push_back({id, RaidPoint(bot), true, bot->IsAlive(), bot->IsInCombat(),
            bot->HasUnitState(UNIT_STATE_NOT_MOVE) || bot->IsMovementPreventedByCasting(), !bot->IsStopped()});
    }
    auto commands = event.raidAssaultController.Update(now, true, actors, observations);
    uint32 pathBudget = 4;
    for (auto const& command : commands)
    {
        if (command.kind == EC::RaidAssault::CommandKind::ObjectiveCleared)
        {
            LOG_INFO("module.ec-events", "Raid {} secured objective {} after physical clearance and occupation",
                event.instanceId, command.objective);
            auto resource = event.raidAssaultResourceSources.find(command.objective);
            if (resource != event.raidAssaultResourceSources.end() &&
                event.raidResourceNodesDestroyed.insert(resource->second).second)
            {
                WorldDatabase.Execute(
                    "UPDATE ec_world_settlement_nodes SET state = 2, health_pct = 0, rebuild_progress = 0 WHERE id = {}",
                    resource->second);
                LOG_INFO("module.ec-events", "Raid {} destroyed strategic resource node {} during active assault",
                    event.instanceId, resource->second);
            }
            continue;
        }
        auto found = bots.find(command.actor);
        if (found == bots.end())
            continue;
        Player* bot = found->second;
        PlayerbotAI* ai = sPlayerbotsMgr.GetPlayerbotAI(bot);
        if (!ai || bot->IsInCombat() || bot->IsMovementPreventedByCasting())
            continue;
        if (command.kind == EC::RaidAssault::CommandKind::Hold)
        {
            if (event.raidAssaultHeld.insert(bot->GetGUID()).second)
            {
                ai->ChangeStrategy("-pvp,+stay,-rpg,-new rpg,-travel,-grind", BOT_STATE_NON_COMBAT);
                bot->GetMotionMaster()->Clear();
                bot->StopMoving();
            }
            continue;
        }
        if (command.kind == EC::RaidAssault::CommandKind::Blocked)
        {
            LOG_ERROR("module.ec-events", "Raid {} bot {} stalled at objective {}; no forced route or teleport",
                event.instanceId, bot->GetGUID().ToString(), command.objective);
            continue;
        }
        ai->ChangeStrategy("-pvp,-stay,-rpg,-new rpg,-travel,-grind", BOT_STATE_NON_COMBAT);
        event.raidAssaultHeld.erase(bot->GetGUID());
        if (command.kind == EC::RaidAssault::CommandKind::Engage)
        {
            Unit* nearest = nullptr;
            double distance = kRaidObjectiveDefenseRadius*kRaidObjectiveDefenseRadius;
            for (Unit* defender : defenders.units)
            {
                double d = EC::RaidAssault::DistanceSquared(RaidPoint(bot), RaidPoint(defender));
                if (d < distance && bot->IsValidAttackTarget(defender) && bot->IsWithinLOSInMap(defender))
                {
                    nearest = defender;
                    distance = d;
                }
            }
            if (nearest)
                bot->Attack(nearest, true);
            continue;
        }
        if (command.kind != EC::RaidAssault::CommandKind::Move)
            continue;
        if (!pathBudget)
        {
            event.raidAssaultController.DeferPath(command.actor, now);
            continue;
        }
        --pathBudget;
        PathGenerator path(bot);
        auto const& p = command.destination;
        bool valid = path.CalculatePath(static_cast<float>(p.x), static_cast<float>(p.y),
            static_cast<float>(p.z), false);
        valid = valid && path.GetPathType() == PATHFIND_NORMAL && path.GetPath().size() >= 2;
        if (valid)
        {
            auto const& end = path.GetActualEndPosition();
            valid = EC::RaidAssault::DistanceSquared({end.x, end.y, end.z}, p) <= 64;
        }
        if (valid)
        {
            auto corridor = path.GetPath();
            bot->GetMotionMaster()->MoveSplinePath(&corridor, FORCED_MOVEMENT_RUN);
            valid = bot->GetMotionMaster()->GetCurrentMovementGeneratorType() == ESCORT_MOTION_TYPE;
        }
        event.raidAssaultController.ReportPath(command.actor,
            valid ? EC::RaidAssault::PathResult::Success : EC::RaidAssault::PathResult::Unreachable, now);
        if (!valid)
            LOG_ERROR("module.ec-events", "Raid {} bot {} cannot reach objective {} (path type {}); retry bounded",
                event.instanceId, bot->GetGUID().ToString(), command.objective, static_cast<uint32>(path.GetPathType()));
    }
#else
    (void)event;
    (void)diff;
#endif
}
