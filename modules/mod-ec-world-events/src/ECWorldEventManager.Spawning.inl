bool WorldEventManager::CanRecruitEventBot(Player* bot) const
{
#ifdef MOD_PLAYERBOTS
    if (!bot || !bot->GetSession() || !bot->IsInWorld() || bot->IsDuringRemoveFromWorld() ||
        !bot->IsAlive() || bot->IsBeingTeleported() || bot->GetSession()->IsLoggingOut())
        return false;

    PlayerbotAI* botAI = sPlayerbotsMgr.GetPlayerbotAI(bot);
    if (!botAI || IsRealPlayer(bot) || botAI->HasGameClientMaster())
        return false;

    if (m_botReturnContexts.find(bot->GetGUID()) != m_botReturnContexts.end())
        return false;

    if (bot->GetGroup() || bot->InBattleground() || bot->InBattlegroundQueue() || bot->IsInFlight() ||
        bot->IsInCombat() || bot->GetVehicle() || bot->GetTransport())
        return false;

    if (sLFGMgr->GetState(bot->GetGUID()) != lfg::LFG_STATE_NONE)
        return false;

    Map* map = bot->GetMap();
    if (!map || map->IsDungeon())
        return false;

    return MapMgr::IsValidMapCoord(bot->GetMapId(), bot->GetPositionX(), bot->GetPositionY(),
        bot->GetPositionZ(), bot->GetOrientation());
#else
    (void)bot;
    return false;
#endif
}

bool WorldEventManager::CaptureBotReturnContext(Player* bot, const ActiveEvent& event)
{
#ifdef MOD_PLAYERBOTS
    if (!CanRecruitEventBot(bot))
        return false;

    PlayerbotAI* botAI = sPlayerbotsMgr.GetPlayerbotAI(bot);
    if (!botAI)
        return false;

    BotReturnContext context;
    context.botGuid = bot->GetGUID();
    context.eventInstanceId = event.instanceId;
    context.eventId = event.eventId;
    context.mapId = bot->GetMapId();
    context.x = bot->GetPositionX();
    context.y = bot->GetPositionY();
    context.z = bot->GetPositionZ();
    context.orientation = bot->GetOrientation();
    context.combatStrategies = FormatBotStrategies(botAI->GetStrategies(BOT_STATE_COMBAT));
    context.nonCombatStrategies = FormatBotStrategies(botAI->GetStrategies(BOT_STATE_NON_COMBAT));
    context.deadStrategies = FormatBotStrategies(botAI->GetStrategies(BOT_STATE_DEAD));

    // Far teleports call PlayerbotAI::Reset(true), which expires these values.
    // Keep a live-process copy in addition to the durable location/strategies.
    context.runtimeActivity = std::make_shared<BotRuntimeActivitySnapshot>();
    context.runtimeActivity->capturedAtMs = getMSTime();
    context.runtimeActivity->rpgInfo = botAI->rpgInfo;
    AiObjectContext* aiContext = botAI->GetAiObjectContext();
    if (aiContext)
    {
        context.runtimeActivity->rpgTarget = aiContext->GetValue<GuidPosition>("rpg target")->Get();
        if (TravelTarget* travelTarget = aiContext->GetValue<TravelTarget*>("travel target")->Get())
        {
            context.runtimeActivity->HoldTravelTarget(
                travelTarget->getDestination(), travelTarget->getPosition());
            context.runtimeActivity->travelStatus = travelTarget->getStatus();
            if (context.runtimeActivity->travelStatus == TRAVEL_STATUS_TRAVEL ||
                context.runtimeActivity->travelStatus == TRAVEL_STATUS_WORK ||
                context.runtimeActivity->travelStatus == TRAVEL_STATUS_COOLDOWN)
            {
                context.runtimeActivity->travelTimeLeftMs = travelTarget->getTimeLeft();
            }
            context.runtimeActivity->travelGroupCopy = travelTarget->isGroupCopy();
            context.runtimeActivity->travelForced = travelTarget->isForced();
            context.runtimeActivity->travelRadius = travelTarget->getRadius();
            context.runtimeActivity->travelMoveRetries = travelTarget->getRetryCount(true);
            context.runtimeActivity->travelExtendRetries = travelTarget->getRetryCount(false);
        }
    }

    std::string combatEsc = context.combatStrategies;
    std::string nonCombatEsc = context.nonCombatStrategies;
    std::string deadEsc = context.deadStrategies;
    CharacterDatabase.EscapeString(combatEsc);
    CharacterDatabase.EscapeString(nonCombatEsc);
    CharacterDatabase.EscapeString(deadEsc);

    const uint32 botGuid = context.botGuid.GetCounter();
    CharacterDatabase.DirectExecute(
        "INSERT INTO ec_world_event_bot_return_context "
        "(bot_guid, event_instance_id, event_id, map_id, position_x, position_y, position_z, orientation, "
        "combat_strategies, non_combat_strategies, dead_strategies) "
        "VALUES ({}, {}, {}, {}, {}, {}, {}, {}, '{}', '{}', '{}')",
        botGuid, context.eventInstanceId, context.eventId, context.mapId,
        context.x, context.y, context.z, context.orientation,
        combatEsc, nonCombatEsc, deadEsc);

    // DirectExecute has no success return. Verify the exact marker before the
    // bot is allowed to move so a database error can never strand it.
    QueryResult marker = CharacterDatabase.Query(
        "SELECT event_instance_id FROM ec_world_event_bot_return_context WHERE bot_guid = {}",
        botGuid);
    if (!marker || marker->Fetch()[0].Get<uint32>() != context.eventInstanceId)
    {
        EC_LOG_ERROR("Refusing to recruit bot %s: durable return checkpoint could not be verified",
            bot->GetName().c_str());
        return false;
    }

    auto [it, inserted] = m_botReturnContexts.emplace(context.botGuid, std::move(context));
    if (!inserted)
    {
        EC_LOG_ERROR("Refusing to recruit bot %s: a return checkpoint is already active",
            bot->GetName().c_str());
        return false;
    }

    sRandomPlayerbotMgr.SetWorldEventProtected(botGuid, true);
    EC_LOG_DEBUG("Captured pre-event state for bot %s at map %u (%.2f, %.2f, %.2f)",
        bot->GetName().c_str(), it->second.mapId, it->second.x, it->second.y, it->second.z);
    return true;
#else
    (void)bot;
    (void)event;
    return false;
#endif
}

void WorldEventManager::LoadBotReturnContexts()
{
#ifdef MOD_PLAYERBOTS
    m_botReturnContexts.clear();

    // A deleted character can never consume its return marker. Remove only
    // those provably orphaned rows; live/offline characters retain durable
    // recovery state until their normal restore transaction completes.
    QueryResult orphanResult = CharacterDatabase.Query(
        "SELECT COUNT(*) FROM ec_world_event_bot_return_context r "
        "LEFT JOIN characters c ON c.guid = r.bot_guid WHERE c.guid IS NULL");
    if (orphanResult)
    {
        const uint64 orphanCount = orphanResult->Fetch()[0].Get<uint64>();
        if (orphanCount > 0)
        {
            CharacterDatabase.DirectExecute(
                "DELETE r FROM ec_world_event_bot_return_context r "
                "LEFT JOIN characters c ON c.guid = r.bot_guid WHERE c.guid IS NULL");

            QueryResult remainingOrphans = CharacterDatabase.Query(
                "SELECT COUNT(*) FROM ec_world_event_bot_return_context r "
                "LEFT JOIN characters c ON c.guid = r.bot_guid WHERE c.guid IS NULL");
            if (remainingOrphans && remainingOrphans->Fetch()[0].Get<uint64>() == 0)
                EC_LOG_INFO("Removed %llu orphaned event bot return checkpoint(s)",
                    static_cast<unsigned long long>(orphanCount));
            else
                EC_LOG_ERROR("Could not verify removal of %llu orphaned event bot return checkpoint(s)",
                    static_cast<unsigned long long>(orphanCount));
        }
    }

    QueryResult result = CharacterDatabase.Query(
        "SELECT bot_guid, event_instance_id, event_id, map_id, position_x, position_y, position_z, orientation, "
        "combat_strategies, non_combat_strategies, dead_strategies "
        "FROM ec_world_event_bot_return_context");
    if (!result)
    {
        EC_LOG_INFO("No pending event bot return checkpoints found");
        return;
    }

    uint32 loaded = 0;
    do
    {
        Field* fields = result->Fetch();
        BotReturnContext context;
        const uint32 botGuid = fields[0].Get<uint32>();
        context.botGuid = ObjectGuid::Create<HighGuid::Player>(botGuid);
        context.eventInstanceId = fields[1].Get<uint32>();
        context.eventId = fields[2].Get<uint32>();
        context.mapId = fields[3].Get<uint32>();
        context.x = fields[4].Get<float>();
        context.y = fields[5].Get<float>();
        context.z = fields[6].Get<float>();
        context.orientation = fields[7].Get<float>();
        context.combatStrategies = fields[8].Get<std::string>();
        context.nonCombatStrategies = fields[9].Get<std::string>();
        context.deadStrategies = fields[10].Get<std::string>();

        if (!MapMgr::IsValidMapCoord(context.mapId, context.x, context.y, context.z, context.orientation))
        {
            EC_LOG_ERROR("Loaded invalid event return location for bot %u (map %u, %.2f, %.2f, %.2f); "
                "checkpoint retained for manual recovery", botGuid, context.mapId, context.x, context.y, context.z);
        }

        // A crash may occur before the event log row is written. Never reuse
        // an instance ID that is still referenced by a return checkpoint.
        m_nextInstanceId = std::max(m_nextInstanceId, context.eventInstanceId + 1);
        m_botReturnContexts[context.botGuid] = std::move(context);
        sRandomPlayerbotMgr.SetWorldEventProtected(botGuid, true);
        ++loaded;
    } while (result->NextRow());

    EC_LOG_INFO("Loaded %u pending event bot return checkpoint(s); next instance ID is %u",
        loaded, m_nextInstanceId);
#endif
}

void WorldEventManager::ProcessBotReturnContexts()
{
#ifdef MOD_PLAYERBOTS
    if (m_botReturnContexts.empty())
        return;

    const auto now = std::chrono::steady_clock::now();
    uint32 teleportsStarted = 0;
    uint32 returnsFinalized = 0;

    for (auto it = m_botReturnContexts.begin(); it != m_botReturnContexts.end();)
    {
        BotReturnContext& context = it->second;

        // The bot still belongs to a live event. Keep RandomPlayerbotMgr from
        // independently randomizing, moving, or logging it out.
        auto activeEventIt = m_activeEvents.find(context.eventInstanceId);
        if (activeEventIt != m_activeEvents.end() && activeEventIt->second.phase != EventPhase::ENDING)
        {
            sRandomPlayerbotMgr.SetWorldEventProtected(context.botGuid.GetCounter(), true);
            ++it;
            continue;
        }

        // Player::SaveToDB appends statements that are intentionally prepared
        // only on the asynchronous CharacterDatabase worker. After queuing the
        // atomic character-save + marker-delete transaction, poll its durable
        // completion callback instead of attempting to execute that transaction
        // on a sync connection. This also lets an offline bot finish cleanly.
        if (context.finalizeQueued)
        {
            if (context.nextAttemptAt != std::chrono::steady_clock::time_point{} && now < context.nextAttemptAt)
            {
                ++it;
                continue;
            }

            if (context.finalizeCallback && !context.finalizeCompleted)
                context.finalizeCallback->InvokeIfReady();

            if (!context.finalizeCompleted)
            {
                if (!context.finalizeDelayWarned && now - context.finalizeQueuedAt >=
                    std::chrono::seconds(kBotReturnFinalizeTimeoutSeconds))
                {
                    // The callback is still pending, so the transaction may be
                    // backlogged. Never enqueue a duplicate while it can still
                    // execute and overwrite newer character state.
                    context.finalizeDelayWarned = true;
                    EC_LOG_ERROR("Return checkpoint finalization for bot %u has been pending for %u seconds; "
                        "keeping the bot protected and the durable marker intact",
                        context.botGuid.GetCounter(), kBotReturnFinalizeTimeoutSeconds);
                }
                context.nextAttemptAt = now + std::chrono::seconds(
                    context.finalizeDelayWarned ? kBotReturnRetrySeconds : 1);
                ++it;
                continue;
            }

            if (!context.finalizeSucceeded)
            {
                // The worker has definitively completed and rolled back this
                // transaction, so a retry cannot race an older queued save.
                ++context.restoreAttempts;
                context.finalizeQueued = false;
                context.finalizeDelayWarned = false;
                context.finalizeCompleted = false;
                context.finalizeCallback.reset();
                context.nextAttemptAt = now + std::chrono::seconds(kBotReturnRetrySeconds);
                EC_LOG_ERROR("Character return transaction for bot %u failed; safe retry %u queued",
                    context.botGuid.GetCounter(), context.restoreAttempts);
                ++it;
                continue;
            }

            QueryResult marker = CharacterDatabase.Query(
                "SELECT COUNT(*) FROM ec_world_event_bot_return_context "
                "WHERE bot_guid = {} AND event_instance_id = {}",
                context.botGuid.GetCounter(), context.eventInstanceId);
            if (marker && marker->Fetch()[0].Get<uint64>() == 0)
            {
                sRandomPlayerbotMgr.SetWorldEventProtected(context.botGuid.GetCounter(), false);

                if (Player* finalizedBot = ObjectAccessor::FindPlayer(context.botGuid))
                {
                    if (WorldSession* botSession = finalizedBot->GetSession())
                    {
                        ChatHandler(botSession).SendSysMessage(
                            "|cff00ff00[EC World Events]|r Your pre-event location and activity have been restored.");
                    }
                }

                EC_LOG_INFO("Finalized restored bot %u at map %u (%.2f, %.2f, %.2f) with exact pre-event strategies%s",
                    context.botGuid.GetCounter(), context.mapId, context.x, context.y, context.z,
                    context.runtimeActivity ? " and activity target" : " (restart recovery)");
                it = m_botReturnContexts.erase(it);
                ++returnsFinalized;
                if (returnsFinalized >= m_maxBotTeleportsPerTick)
                    break;
                continue;
            }

            if (!context.finalizeDelayWarned)
            {
                context.finalizeDelayWarned = true;
                EC_LOG_ERROR("Character return transaction for bot %u reported success but checkpoint removal "
                    "could not be verified; keeping the bot protected for manual/restart recovery",
                    context.botGuid.GetCounter());
            }
            context.nextAttemptAt = now + std::chrono::seconds(kBotReturnRetrySeconds);
            ++it;
            continue;
        }

        Player* bot = ObjectAccessor::FindPlayer(context.botGuid);
        if (!bot || !bot->GetSession() || !bot->IsInWorld() || bot->IsDuringRemoveFromWorld())
        {
            ++it; // Offline bots keep their durable row and resume on login.
            continue;
        }

        PlayerbotAI* botAI = sPlayerbotsMgr.GetPlayerbotAI(bot);
        if (!botAI || IsRealPlayer(bot) || botAI->HasGameClientMaster())
        {
            ++it;
            continue;
        }

        sRandomPlayerbotMgr.SetWorldEventProtected(context.botGuid.GetCounter(), true);

        // Apply retry backoff to every restore stage, including a failed
        // synchronous Playerbots strategy save after teleport acknowledgement.
        if (context.nextAttemptAt != std::chrono::steady_clock::time_point{} && now < context.nextAttemptAt)
        {
            ++it;
            continue;
        }

        const float dx = bot->GetPositionX() - context.x;
        const float dy = bot->GetPositionY() - context.y;
        const float dz = bot->GetPositionZ() - context.z;
        const bool atReturnLocation = bot->GetMapId() == context.mapId &&
            (dx * dx + dy * dy + dz * dz) <= 4.0f;

        if (context.teleportStarted)
        {
            if (bot->IsBeingTeleported())
            {
                ++it;
                continue;
            }

            if (!atReturnLocation)
            {
                if (now - context.teleportStartedAt >=
                    std::chrono::seconds(kBotReturnTeleportTimeoutSeconds))
                {
                    ++context.restoreAttempts;
                    context.teleportStarted = false;
                    context.nextAttemptAt = now + std::chrono::seconds(kBotReturnRetrySeconds);
                    EC_LOG_WARN("Return teleport for bot %s did not reach its checkpoint; retry %u queued",
                        bot->GetName().c_str(), context.restoreAttempts);
                }
                ++it;
                continue;
            }

            auto restoreStrategies = [botAI](BotState state, const std::string& strategies)
            {
                botAI->ClearStrategies(state);
                if (!strategies.empty())
                    botAI->ChangeStrategy(strategies, state);
            };
            restoreStrategies(BOT_STATE_COMBAT, context.combatStrategies);
            restoreStrategies(BOT_STATE_NON_COMBAT, context.nonCombatStrategies);
            restoreStrategies(BOT_STATE_DEAD, context.deadStrategies);

            if (context.runtimeActivity)
            {
                AiObjectContext* aiContext = botAI->GetAiObjectContext();
                if (aiContext)
                {
                    BotRuntimeActivitySnapshot& activity = *context.runtimeActivity;
                    if (activity.travelDestination && activity.travelPosition)
                    {
                        if (TravelTarget* target = aiContext->GetValue<TravelTarget*>("travel target")->Get())
                        {
                            target->setTarget(activity.travelDestination, activity.travelPosition,
                                activity.travelGroupCopy);
                            target->setForced(activity.travelForced);
                            target->setRadius(activity.travelRadius);
                            target->setRetry(true, activity.travelMoveRetries);
                            target->setRetry(false, activity.travelExtendRetries);
                            target->setStatus(activity.travelStatus);
                            if (activity.travelStatus == TRAVEL_STATUS_TRAVEL ||
                                activity.travelStatus == TRAVEL_STATUS_WORK ||
                                activity.travelStatus == TRAVEL_STATUS_COOLDOWN)
                            {
                                target->setExpireIn(activity.travelTimeLeftMs);
                            }
                        }
                    }
                    aiContext->GetValue<GuidPosition>("rpg target")->Set(activity.rpgTarget);

                    // Treat the event as a pause, not elapsed activity time.
                    // NewRpgInfo owns a non-trivial anonymous union, so use
                    // its supported assignment path instead of copy construction.
                    NewRpgInfo restoredRpgInfo;
                    restoredRpgInfo = activity.rpgInfo;
                    const uint32 pausedMs = getMSTimeDiff(activity.capturedAtMs, getMSTime());
                    if (restoredRpgInfo.startT)
                        restoredRpgInfo.startT += pausedMs;
                    if (restoredRpgInfo.stuckTs)
                        restoredRpgInfo.stuckTs += pausedMs;
                    // Compat upstream variant NewRpgInfo: adjust the
                    // per-state dwell timestamps of whichever state is active.
                    if (auto* wanderNpc = std::get_if<NewRpgInfo::WanderNpc>(&restoredRpgInfo.data))
                        if (wanderNpc->lastReach)
                            wanderNpc->lastReach += pausedMs;
                    if (auto* doQuest = std::get_if<NewRpgInfo::DoQuest>(&restoredRpgInfo.data))
                        if (doQuest->lastReachPOI)
                            doQuest->lastReachPOI += pausedMs;
                    botAI->rpgInfo = restoredRpgInfo;
                }
            }

            // Player::SaveToDB does not include Playerbot strategy storage.
            // The playerbots database is a separate connection pool, so its
            // exact strategy set must commit and verify before the character
            // checkpoint may be removed. A failure leaves recovery replayable.
            if (!PlayerbotRepository::instance().SaveSync(botAI))
            {
                ++context.restoreAttempts;
                context.nextAttemptAt = now + std::chrono::seconds(kBotReturnRetrySeconds);
                EC_LOG_ERROR("Playerbot strategy save for bot %s was not durable; retry %u queued",
                    bot->GetName().c_str(), context.restoreAttempts);
                ++it;
                continue;
            }

            // Save the returned character and delete the marker atomically on
            // the CharacterDatabase async worker. If the server crashes,
            // either both changes commit or neither does.
            CharacterDatabaseTransaction trans = CharacterDatabase.BeginTransaction();
            bot->SaveToDB(trans, false, false);
            trans->Append(
                "DELETE FROM ec_world_event_bot_return_context "
                "WHERE bot_guid = {} AND event_instance_id = {}",
                context.botGuid.GetCounter(), context.eventInstanceId);
            context.finalizeCompleted = false;
            context.finalizeSucceeded = false;
            context.finalizeDelayWarned = false;
            context.finalizeCallback = std::make_shared<TransactionCallback>(
                CharacterDatabase.AsyncCommitTransaction(trans));
            BotReturnContext* queuedContext = &context;
            context.finalizeCallback->AfterComplete([queuedContext](bool success)
            {
                queuedContext->finalizeSucceeded = success;
                queuedContext->finalizeCompleted = true;
            });
            context.finalizeQueued = true;
            context.finalizeQueuedAt = now;
            context.nextAttemptAt = now + std::chrono::seconds(1);
            EC_LOG_DEBUG("Queued atomic character save and return-checkpoint finalization for bot %s",
                bot->GetName().c_str());
            ++it;
            continue;
        }

        if (atReturnLocation)
        {
            // Handles the crash window after marker creation but before the
            // original event teleport, without performing a redundant move.
            context.teleportStarted = true;
            context.teleportStartedAt = now;
            continue;
        }

        if (teleportsStarted >= m_maxBotTeleportsPerTick || bot->IsBeingTeleported() ||
            bot->GetSession()->IsLoggingOut() || bot->GetGroup() ||
            bot->InBattleground() || bot->InBattlegroundQueue() || bot->IsInFlight() ||
            bot->GetVehicle() || bot->GetTransport() ||
            sLFGMgr->GetState(bot->GetGUID()) != lfg::LFG_STATE_NONE ||
            botAI->HasGameClientMaster() || (bot->GetMap() && bot->GetMap()->IsDungeon()))
        {
            ++it;
            continue;
        }

        if (!MapMgr::IsValidMapCoord(context.mapId, context.x, context.y, context.z, context.orientation))
        {
            if (context.restoreAttempts == 0)
                EC_LOG_ERROR("Cannot restore bot %s: saved map coordinates are invalid; checkpoint retained",
                    bot->GetName().c_str());
            ++context.restoreAttempts;
            context.nextAttemptAt = now + std::chrono::seconds(kBotReturnRetrySeconds);
            ++it;
            continue;
        }

        if (!bot->IsAlive())
        {
            bot->ResurrectPlayer(1.0f);
            bot->SpawnCorpseBones();
        }

        // A completed/cancelled event owns this checkpoint, so incidental
        // town-guard combat must not pin the bot at the event forever. Break
        // combat immediately before the bounded return teleport; all other
        // safety gates above (group/BG/LFG/flight/vehicle/real master) remain.
        if (bot->IsInCombat())
            bot->CombatStop(true);
        if (bot->GetMotionMaster())
            bot->GetMotionMaster()->Clear();
        bot->StopMoving();

        if (bot->TeleportTo(context.mapId, context.x, context.y, context.z, context.orientation))
        {
            context.teleportStarted = true;
            context.teleportStartedAt = now;
            ++teleportsStarted;
            EC_LOG_INFO("Returning bot %s to its pre-event checkpoint (map %u)",
                bot->GetName().c_str(), context.mapId);
        }
        else
        {
            ++context.restoreAttempts;
            context.nextAttemptAt = now + std::chrono::seconds(kBotReturnRetrySeconds);
            EC_LOG_WARN("Failed to start return teleport for bot %s; retry %u queued",
                bot->GetName().c_str(), context.restoreAttempts);
        }
        ++it;
    }
#endif
}

void WorldEventManager::SpawnEventBots(ActiveEvent& event)
{
    const EventDefinition* def = GetEventDefinition(event.eventId);
    if (!def)
    {
        EC_LOG_ERROR("SpawnEventBots: No definition for event %u", event.eventId);
        return;
    }

    EC_LOG_INFO("SpawnEventBots for event '%s'", def->name.c_str());
    EC_LOG_DEBUG("  Location: %s (%.2f, %.2f, %.2f) on map %u",
        def->location.name.c_str(),
        def->location.x, def->location.y, def->location.z,
        def->location.mapId);
    EC_LOG_DEBUG("  Bot count: %u - %u", def->minBots, def->maxBots);
    EC_LOG_DEBUG("  Level range: %u - %u", def->minLevel, def->maxLevel);
    EC_LOG_DEBUG("  Attacker faction: %s", EventFactionToString(def->attackerFaction).c_str());
    EC_LOG_DEBUG("  Defender faction: %s", EventFactionToString(def->defenderFaction).c_str());

#ifdef MOD_PLAYERBOTS
    // Calculate how many bots to recruit
    std::random_device rd;
    std::mt19937 gen(rd());
    uint32 cappedMaxBots = std::min(def->maxBots, m_botHardCap);
    uint32 cappedMinBots = std::min(def->minBots, cappedMaxBots);

    if (cappedMaxBots == 0)
    {
        EC_LOG_WARN("Bot cap for event '%s' resolved to 0 (min=%u max=%u hardCap=%u), skipping recruitment",
            def->name.c_str(), def->minBots, def->maxBots, m_botHardCap);
        return;
    }

    if (def->maxBots > m_botHardCap)
    {
        EC_LOG_WARN("Event '%s' bot cap clamped from %u to %u to prevent overload",
            def->name.c_str(), def->maxBots, m_botHardCap);
    }

    if (cappedMinBots == 0)
        cappedMinBots = 1;

    std::uniform_int_distribution<uint32> botCountDist(cappedMinBots, cappedMaxBots);
    uint32 targetBotCount = botCountDist(gen);

    EC_LOG_INFO("Attempting to recruit %u bots for event", targetBotCount);

    bool useRaidDefenderMix = UseRaidDefenderMix(*def, m_raidDefenderBotsEnabled, m_raidDefenderBotSharePct);
    uint32 targetDefenderCount = 0;
    if (useRaidDefenderMix)
    {
        targetDefenderCount = (targetBotCount * m_raidDefenderBotSharePct + 50) / 100;
        targetDefenderCount = std::min(targetDefenderCount, targetBotCount);
    }
    uint32 targetAttackerCount = targetBotCount - targetDefenderCount;

    if (useRaidDefenderMix)
    {
        EC_LOG_INFO("Raid bot mix enabled: target attackers=%u defenders=%u (%u%% defenders)",
            targetAttackerCount, targetDefenderCount, m_raidDefenderBotSharePct);
    }

    // Get all available bots from RandomPlayerbotMgr
    PlayerBotMap allBots = sRandomPlayerbotMgr.GetAllBots();
    EC_LOG_DEBUG("  Total available bots in RandomPlayerbotMgr: %zu", allBots.size());

    if (allBots.empty())
    {
        EC_LOG_WARN("No bots available from RandomPlayerbotMgr! Event will run without bot participants.");
        return;
    }

    // Filter bots by level and faction buckets
    std::vector<Player*> attackerEligibleBots;
    std::vector<Player*> defenderEligibleBots;

    // Build a set of all bot GUIDs already in other events (O(N) instead of O(N²))
    std::set<ObjectGuid> botsInOtherEvents;
    for (const auto& eventPair : m_activeEvents)
    {
        if (eventPair.first == event.instanceId)
            continue;
        for (const ObjectGuid& spawnedGuid : eventPair.second.spawnedBots)
            botsInOtherEvents.insert(spawnedGuid);
        for (const PendingBotTeleport& pending : eventPair.second.pendingBotTeleports)
            botsInOtherEvents.insert(pending.botGuid);
    }

    for (auto& botPair : allBots)
    {
        Player* bot = botPair.second;
        if (!CanRecruitEventBot(bot))
            continue;

        // Check level range
        uint32 botLevel = bot->GetLevel();
        if (botLevel < def->minLevel || botLevel > def->maxLevel)
            continue;

        // Check if bot is already participating in another event (O(1) set lookup)
        if (botsInOtherEvents.count(bot->GetGUID()))
            continue;

        bool matchesAttacker = TeamMatchesEventFaction(bot->GetTeamId(), def->attackerFaction);
        bool matchesDefender = TeamMatchesEventFaction(bot->GetTeamId(), def->defenderFaction);

        if (useRaidDefenderMix)
        {
            if (matchesAttacker)
            {
                attackerEligibleBots.push_back(bot);
                continue;
            }

            if (matchesDefender)
            {
                defenderEligibleBots.push_back(bot);
                continue;
            }

            continue;
        }

        if (!matchesAttacker)
            continue;

        attackerEligibleBots.push_back(bot);
    }

    uint32 totalEligible = static_cast<uint32>(attackerEligibleBots.size() + defenderEligibleBots.size());
    EC_LOG_INFO("Found %u eligible bots for event (attackers=%zu defenders=%zu)",
        totalEligible, attackerEligibleBots.size(), defenderEligibleBots.size());

    if (totalEligible == 0)
    {
        EC_LOG_WARN("No eligible bots found! Check faction and level requirements.");
        return;
    }

    // Shuffle to randomize selection
    std::shuffle(attackerEligibleBots.begin(), attackerEligibleBots.end(), gen);
    std::shuffle(defenderEligibleBots.begin(), defenderEligibleBots.end(), gen);

    // Queue bots for deferred teleport (batched to avoid freezing the world thread).
    // ProcessPendingBotTeleports will teleport m_maxBotTeleportsPerTick bots per update tick.
    uint32 botsQueued = 0;
    uint32 attackersQueued = 0;
    uint32 defendersQueued = 0;
    float spreadRadius = 15.0f;
    std::uniform_real_distribution<float> offsetDist(-spreadRadius, spreadRadius);
    std::set<ObjectGuid> queuedGuids;

    auto queueFromPool = [&](std::vector<Player*>& pool, size_t& index, uint32 desiredCount, const std::string& role) -> uint32
    {
        uint32 queued = 0;
        while (index < pool.size() && queued < desiredCount && botsQueued < targetBotCount)
        {
            Player* bot = pool[index++];
            if (!bot)
                continue;
            if (!queuedGuids.insert(bot->GetGUID()).second)
                continue;

            float offsetX = offsetDist(gen);
            float offsetY = offsetDist(gen);

            PendingBotTeleport pending;
            pending.botGuid = bot->GetGUID();

            bool isRaidAttacker = def->type == EventType::RAID &&
                TeamMatchesEventFaction(bot->GetTeamId(), def->attackerFaction);
            if (isRaidAttacker)
            {
                // Rally attackers outside the settlement instead of materializing
                // in its streets. The initial facing points toward the objective.
                constexpr float kRaidRallyDistance = 220.0f;
                float approachAngle = def->location.orientation + static_cast<float>(M_PI);
                pending.x = def->location.x + std::cos(approachAngle) * kRaidRallyDistance + offsetX;
                pending.y = def->location.y + std::sin(approachAngle) * kRaidRallyDistance + offsetY;
                pending.z = def->location.z;
                pending.orientation = std::atan2(def->location.y - pending.y, def->location.x - pending.x);
            }
            else
            {
                pending.x = def->location.x + offsetX;
                pending.y = def->location.y + offsetY;
                pending.z = def->location.z;
                pending.orientation = def->location.orientation;
            }
            pending.mapId = def->location.mapId;
            pending.botName = bot->GetName();
            pending.botLevel = bot->GetLevel();
            pending.eventName = def->name;
            pending.role = role;

            event.pendingBotTeleports.push_back(std::move(pending));
            ++botsQueued;
            ++queued;
        }
        return queued;
    };

    size_t attackerIndex = 0;
    size_t defenderIndex = 0;

    if (useRaidDefenderMix)
    {
        attackersQueued += queueFromPool(attackerEligibleBots, attackerIndex, targetAttackerCount, "attacker");
        defendersQueued += queueFromPool(defenderEligibleBots, defenderIndex, targetDefenderCount, "defender");

        // Backfill from whichever side still has candidates if one side runs out.
        if (botsQueued < targetBotCount)
            attackersQueued += queueFromPool(attackerEligibleBots, attackerIndex, targetBotCount - botsQueued, "attacker");
        if (botsQueued < targetBotCount)
            defendersQueued += queueFromPool(defenderEligibleBots, defenderIndex, targetBotCount - botsQueued, "defender");

        if (attackersQueued < targetAttackerCount || defendersQueued < targetDefenderCount)
        {
            EC_LOG_WARN("Could not satisfy requested raid bot mix for '%s' (wanted A:%u D:%u, queued A:%u D:%u)",
                def->name.c_str(), targetAttackerCount, targetDefenderCount, attackersQueued, defendersQueued);
        }
    }
    else
    {
        std::string role = "participant";
        if (def->type == EventType::RAID)
            role = "attacker";
        else if (def->type == EventType::DEFENSE)
            role = "defender";

        attackersQueued += queueFromPool(attackerEligibleBots, attackerIndex, targetBotCount, role);
    }

    EC_LOG_INFO("Queued %u/%u bots for deferred teleport to event '%s' (will process %u per tick)",
        botsQueued, targetBotCount, def->name.c_str(), m_maxBotTeleportsPerTick);

    if (botsQueued < cappedMinBots)
    {
        EC_LOG_WARN("Only %u eligible bots available, minimum required after cap is %u. Event may be unbalanced!",
            botsQueued, cappedMinBots);
    }

#else
    EC_LOG_ERROR("MOD_PLAYERBOTS not defined! Bot spawning disabled.");
    EC_LOG_ERROR("  Rebuild with: -DMOD_PLAYERBOTS=1 or ensure mod-playerbots is installed.");
#endif
}

void WorldEventManager::ProcessPendingBotTeleports(ActiveEvent& event)
{
    if (event.pendingBotTeleports.empty())
        return;

    const EventDefinition* def = GetEventDefinition(event.eventId);

#ifdef MOD_PLAYERBOTS
    uint32 processed = 0;

    while (!event.pendingBotTeleports.empty() && processed < m_maxBotTeleportsPerTick)
    {
        PendingBotTeleport pending = std::move(event.pendingBotTeleports.front());
        event.pendingBotTeleports.pop_front();  // O(1) with deque (Issue #8)

        // Find the bot player by GUID
        Player* bot = ObjectAccessor::FindPlayer(pending.botGuid);
        if (!bot || !bot->IsInWorld())
        {
            EC_LOG_WARN("  Bot %s no longer available for teleport, skipping", pending.botName.c_str());
            continue;
        }

        // Eligibility may have changed while this bot waited in the deferred
        // queue. Capture and verify the durable checkpoint immediately before
        // teleporting; never move a bot whose original state is not recoverable.
        if (!CaptureBotReturnContext(bot, event))
        {
            EC_LOG_WARN("  Bot %s is no longer eligible or could not be checkpointed, skipping",
                pending.botName.c_str());
            continue;
        }

        bool teleportSuccess = bot->TeleportTo(
            pending.mapId, pending.x, pending.y, pending.z, pending.orientation
        );

        if (teleportSuccess)
        {
            event.spawnedBots.push_back(pending.botGuid);

            EC_LOG_INFO("  Recruited bot %s (L%u) for event - teleporting to %s",
                pending.botName.c_str(), pending.botLevel,
                def ? def->location.name.c_str() : "event");

            if (WorldSession* botSession = bot->GetSession())
            {
                ChatHandler(botSession).PSendSysMessage(
                    "|cffff0000[EC World Events]|r You have been summoned to participate in %s!",
                    pending.eventName.c_str());
            }

            EventParticipant participant;
            participant.playerGuid = pending.botGuid;
            participant.playerName = pending.botName;
            participant.kills = 0;
            participant.deaths = 0;
            participant.damageDealt = 0;
            participant.healingDone = 0;
            participant.joinTime = std::chrono::system_clock::now();
            participant.rewarded = false;
            event.participants[pending.botGuid] = participant;

            if (def)
                NotifyAliveWorldBotJoined(pending.botGuid.GetCounter(), event, *def, false, pending.role);
        }
        else
        {
            EC_LOG_WARN("  Failed to teleport bot %s to event location!", pending.botName.c_str());

            CharacterDatabase.DirectExecute(
                "DELETE FROM ec_world_event_bot_return_context "
                "WHERE bot_guid = {} AND event_instance_id = {}",
                pending.botGuid.GetCounter(), event.instanceId);
            m_botReturnContexts.erase(pending.botGuid);
            sRandomPlayerbotMgr.SetWorldEventProtected(pending.botGuid.GetCounter(), false);
        }

        ++processed;
    }

    // Emit bridge event when all bots have been teleported
    if (event.pendingBotTeleports.empty() && !event.spawnedBots.empty() && def)
    {
        std::string faction = EventFactionToString(def->attackerFaction);
        uint32 allianceBots = 0;
        uint32 hordeBots = 0;
        for (const ObjectGuid& guid : event.spawnedBots)
        {
            if (Player* spawnedBot = ObjectAccessor::FindPlayer(guid))
            {
                if (spawnedBot->GetTeamId() == TEAM_ALLIANCE)
                    ++allianceBots;
                else if (spawnedBot->GetTeamId() == TEAM_HORDE)
                    ++hordeBots;
            }
        }

        if (allianceBots > 0 && hordeBots > 0)
            faction = "Mixed";
        else if (allianceBots > 0)
            faction = "Alliance";
        else if (hordeBots > 0)
            faction = "Horde";

        sWorldEventBridge->EmitBotsRecruited(
            event.eventId,
            event.instanceId,
            static_cast<uint32>(event.spawnedBots.size()),
            faction
        );
        EC_LOG_INFO("All bot teleports complete for event '%s': %zu bots recruited",
            def->name.c_str(), event.spawnedBots.size());
    }
#endif
}

void WorldEventManager::ProcessEventBotCombat(ActiveEvent& event)
{
#ifdef MOD_PLAYERBOTS
    if (event.phase != EventPhase::ACTIVE)
        return;

    const EventDefinition* def = GetEventDefinition(event.eventId);
    if (!def || (def->type != EventType::RAID && def->type != EventType::DEFENSE &&
        def->type != EventType::PVP_ZONE))
    {
        return;
    }

    uint32 applied = 0;
    for (auto const& participantPair : event.participants)
    {
        ObjectGuid const& guid = participantPair.first;
        if (event.eventBotStrategiesApplied.count(guid) != 0)
            continue;

        auto returnContextIt = m_botReturnContexts.find(guid);
        if (returnContextIt == m_botReturnContexts.end() ||
            returnContextIt->second.eventInstanceId != event.instanceId)
        {
            continue; // Real participant, or a bot owned by a different event.
        }

        Player* bot = ObjectAccessor::FindPlayer(guid);
        if (!bot || !bot->IsInWorld() || bot->IsDuringRemoveFromWorld() || bot->IsBeingTeleported() ||
            bot->GetMapId() != def->location.mapId || bot->GetZoneId() != def->location.zoneId)
        {
            continue;
        }

        PlayerbotAI* botAI = sPlayerbotsMgr.GetPlayerbotAI(bot);
        if (!botAI || IsRealPlayer(bot) || botAI->HasGameClientMaster())
            continue;

        // CaptureBotReturnContext already saved the exact pre-event strategy
        // set. Suspend autonomous roaming and enable the existing enemy-player
        // trigger for the battle; restoration replays the captured set exactly.
        botAI->ChangeStrategy("+pvp,-rpg,-new rpg,-travel,-grind", BOT_STATE_NON_COMBAT);
        event.eventBotStrategiesApplied.insert(guid);
        ++applied;
    }

    if (applied > 0)
    {
        EC_LOG_INFO("Applied temporary event combat behavior to %u bot(s) for '%s' (Instance %u)",
            applied, def->name.c_str(), event.instanceId);
    }
#else
    (void)event;
#endif
}

#include "ECRaidAssaultObjectives.inl"
#include "ECRaidSacking.inl"
#include "ECRegionalReinforcements.inl"

void WorldEventManager::SpawnAdditionalBots(ActiveEvent& event, uint32 count, bool defendersOnly)
{
    const EventDefinition* def = GetEventDefinition(event.eventId);
    if (!def)
    {
        EC_LOG_ERROR("SpawnAdditionalBots: No definition for event %u", event.eventId);
        return;
    }

    EC_LOG_INFO("SpawnAdditionalBots: Recruiting %u additional bots for event '%s' escalation",
        count, def->name.c_str());

#ifdef MOD_PLAYERBOTS
    uint32 currentBotCount = static_cast<uint32>(event.spawnedBots.size());
    if (currentBotCount >= m_botHardCap)
    {
        EC_LOG_WARN("SpawnAdditionalBots: event '%s' already at hard cap (%u bots), skipping escalation spawn",
            def->name.c_str(), m_botHardCap);
        return;
    }

    uint32 remainingSlots = m_botHardCap - currentBotCount;
    if (count > remainingSlots)
    {
        EC_LOG_WARN("SpawnAdditionalBots: requested %u bots but only %u slots left before cap, clamping",
            count, remainingSlots);
        count = remainingSlots;
    }

    if (count == 0)
        return;

    // Get all available bots from RandomPlayerbotMgr
    PlayerBotMap allBots = sRandomPlayerbotMgr.GetAllBots();
    if (allBots.empty())
    {
        EC_LOG_WARN("No bots available for escalation reinforcements!");
        return;
    }

    bool useRaidDefenderMix = UseRaidDefenderMix(*def, m_raidDefenderBotsEnabled, m_raidDefenderBotSharePct);
    uint32 targetDefenderCount = defendersOnly ? count : 0;
    if (!defendersOnly && useRaidDefenderMix)
    {
        targetDefenderCount = (count * m_raidDefenderBotSharePct + 50) / 100;
        targetDefenderCount = std::min(targetDefenderCount, count);
    }
    uint32 targetAttackerCount = defendersOnly ? 0 : count - targetDefenderCount;

    // Filter bots by level and faction (similar to SpawnEventBots)
    std::vector<Player*> attackerEligibleBots;
    std::vector<Player*> defenderEligibleBots;

    for (auto& botPair : allBots)
    {
        Player* bot = botPair.second;
        if (!CanRecruitEventBot(bot))
            continue;

        uint32 botLevel = bot->GetLevel();
        if (botLevel < def->minLevel || botLevel > def->maxLevel)
            continue;

        // Check if already queued or present in this event
        bool alreadyInEvent = false;
        for (const ObjectGuid& spawnedGuid : event.spawnedBots)
        {
            if (spawnedGuid == bot->GetGUID())
            {
                alreadyInEvent = true;
                break;
            }
        }
        if (!alreadyInEvent)
        {
            for (const PendingBotTeleport& pending : event.pendingBotTeleports)
            {
                if (pending.botGuid == bot->GetGUID())
                {
                    alreadyInEvent = true;
                    break;
                }
            }
        }
        if (alreadyInEvent)
            continue;

        bool matchesAttacker = TeamMatchesEventFaction(bot->GetTeamId(), def->attackerFaction);
        bool matchesDefender = TeamMatchesEventFaction(bot->GetTeamId(), def->defenderFaction);

        if (useRaidDefenderMix)
        {
            if (matchesAttacker)
            {
                attackerEligibleBots.push_back(bot);
                continue;
            }

            if (matchesDefender)
            {
                defenderEligibleBots.push_back(bot);
                continue;
            }

            continue;
        }

        if (!matchesAttacker)
            continue;

        attackerEligibleBots.push_back(bot);
    }

    uint32 totalEligible = static_cast<uint32>(attackerEligibleBots.size() + defenderEligibleBots.size());
    if (totalEligible == 0)
    {
        EC_LOG_WARN("No eligible bots available for escalation!");
        return;
    }

    // Shuffle and queue bots for deferred teleport
    std::random_device rd;
    std::mt19937 g(rd());
    std::shuffle(attackerEligibleBots.begin(), attackerEligibleBots.end(), g);
    std::shuffle(defenderEligibleBots.begin(), defenderEligibleBots.end(), g);

    // Issue #4: Use deferred teleport pattern (same as SpawnEventBots) to avoid freezing world thread
    uint32 botsQueued = 0;
    uint32 attackersQueued = 0;
    uint32 defendersQueued = 0;
    std::uniform_real_distribution<float> offsetDist(-15.0f, 15.0f);
    std::set<ObjectGuid> queuedGuids;

    auto queueFromPool = [&](std::vector<Player*>& pool, size_t& index, uint32 desiredCount, const std::string& role) -> uint32
    {
        uint32 queued = 0;
        while (index < pool.size() && queued < desiredCount && botsQueued < count)
        {
            Player* bot = pool[index++];
            if (!bot)
                continue;
            if (!queuedGuids.insert(bot->GetGUID()).second)
                continue;

            PendingBotTeleport pending;
            pending.botGuid = bot->GetGUID();
            pending.x = def->location.x + offsetDist(g);
            pending.y = def->location.y + offsetDist(g);
            pending.z = def->location.z;
            pending.orientation = def->location.orientation;
            pending.mapId = def->location.mapId;
            pending.botName = bot->GetName();
            pending.botLevel = bot->GetLevel();
            pending.eventName = def->name;
            pending.role = role;

            event.pendingBotTeleports.push_back(std::move(pending));
            ++botsQueued;
            ++queued;
        }
        return queued;
    };

    size_t attackerIndex = 0;
    size_t defenderIndex = 0;

    if (useRaidDefenderMix)
    {
        attackersQueued += queueFromPool(attackerEligibleBots, attackerIndex, targetAttackerCount, "attacker_reinforcement");
        defendersQueued += queueFromPool(defenderEligibleBots, defenderIndex, targetDefenderCount, "defender_reinforcement");

        if (botsQueued < count)
            attackersQueued += queueFromPool(attackerEligibleBots, attackerIndex, count - botsQueued, "attacker_reinforcement");
        if (botsQueued < count)
            defendersQueued += queueFromPool(defenderEligibleBots, defenderIndex, count - botsQueued, "defender_reinforcement");

        if (attackersQueued < targetAttackerCount || defendersQueued < targetDefenderCount)
        {
            EC_LOG_WARN("Escalation mix target not fully met for '%s' (wanted A:%u D:%u, queued A:%u D:%u)",
                def->name.c_str(), targetAttackerCount, targetDefenderCount, attackersQueued, defendersQueued);
        }
    }
    else
    {
        attackersQueued += queueFromPool(attackerEligibleBots, attackerIndex, count, "reinforcement");
    }

    EC_LOG_INFO("Queued %u/%u reinforcement bots for deferred teleport (escalation)", botsQueued, count);
#else
    EC_LOG_DEBUG("MOD_PLAYERBOTS not available - cannot spawn additional bots");
#endif
}

// =============================================================================
// AliveWorld Integration
// =============================================================================

void WorldEventManager::AutoJoinTier1BotsInZone(ActiveEvent& event, const EventDefinition& def)
{
#if defined(MOD_ALIVE_WORLD) && defined(MOD_PLAYERBOTS)
    if (!sAliveWorldMgr->IsEnabled())
        return;

    auto* tierMgr = sAliveWorldMgr->GetTierManager();
    auto* memoryMgr = sAliveWorldMgr->GetBotMemoryManager();
    if (!tierMgr || !memoryMgr)
        return;

    // Get all Tier 1 bots (active bots near players)
    auto tier1Bots = tierMgr->GetTier1Bots();
    EC_LOG_DEBUG("Found %zu Tier 1 bots to check for event zone %u", tier1Bots.size(), def.location.zoneId);

    uint32 autoJoined = 0;
    for (uint64 botGuid : tier1Bots)
    {
        Player* bot = ObjectAccessor::FindPlayer(ObjectGuid::Create<HighGuid::Player>(botGuid));
        if (!CanRecruitEventBot(bot))
            continue;

        // Check if bot is in the event zone
        if (bot->GetZoneId() != def.location.zoneId)
            continue;

        // Check faction compatibility
        bool canJoin = false;
        std::string role = "participant";
        bool matchesAttacker = TeamMatchesEventFaction(bot->GetTeamId(), def.attackerFaction);
        bool matchesDefender = TeamMatchesEventFaction(bot->GetTeamId(), def.defenderFaction);

        if (def.type == EventType::RAID)
        {
            bool allowRaidDefenderJoin = UseRaidDefenderMix(def, m_raidDefenderBotsEnabled, m_raidDefenderBotSharePct);

            if (matchesAttacker)
            {
                canJoin = true;
                role = "attacker";
            }
            else if (allowRaidDefenderJoin && matchesDefender)
            {
                canJoin = true;
                role = "defender";
            }
        }
        else if (def.type == EventType::DEFENSE)
        {
            // For defense, defenders join
            canJoin = matchesDefender;
            if (canJoin)
                role = "defender";
        }
        else
        {
            // For other events, both factions can join
            canJoin = true;
        }

        if (!canJoin)
            continue;

        // Check personality affinity for this event type
        float affinity = CalculatePersonalityEventAffinity(botGuid, def.type);
        if (affinity < 0.3f)
        {
            EC_LOG_DEBUG("Bot %s personality doesn't match event type (affinity %.2f)",
                bot->GetName().c_str(), affinity);
            continue;
        }

        // Bot is eligible - add as participant
        ObjectGuid objGuid = bot->GetGUID();

        // Don't add if already a participant
        if (event.participants.find(objGuid) != event.participants.end())
            continue;

        if (!CaptureBotReturnContext(bot, event))
        {
            EC_LOG_WARN("Tier 1 bot %s could not be checkpointed and will not auto-join event %u",
                bot->GetName().c_str(), event.instanceId);
            continue;
        }

        EventParticipant participant;
        participant.playerGuid = objGuid;
        participant.playerName = bot->GetName();
        participant.kills = 0;
        participant.deaths = 0;
        participant.damageDealt = 0;
        participant.healingDone = 0;
        participant.joinTime = std::chrono::system_clock::now();
        participant.rewarded = false;

        event.participants[objGuid] = participant;

        // Notify AliveWorld
        NotifyAliveWorldBotJoined(botGuid, event, def, true, role);

        // Notify bot in-game
        if (WorldSession* botSession = bot->GetSession())
        {
            ChatHandler(botSession).PSendSysMessage(
                "|cff00ff00[EC World Events]|r You've joined the %s event!",
                def.name.c_str());
        }

        ++autoJoined;
        EC_LOG_DEBUG("Tier 1 bot %s auto-joined event (affinity %.2f)",
            bot->GetName().c_str(), affinity);
    }

    if (autoJoined > 0)
    {
        EC_LOG_INFO("AliveWorld: %u Tier 1 bots auto-joined event '%s'", autoJoined, def.name.c_str());
    }
#else
    (void)event;
    (void)def;
#endif
}

void WorldEventManager::NotifyAliveWorldBotJoined(uint64 botGuid, const ActiveEvent& event,
                                                   const EventDefinition& def, bool voluntary,
                                                   const std::string& role)
{
#ifdef MOD_ALIVE_WORLD
    if (!sAliveWorldMgr->IsEnabled())
        return;

    auto* tierMgr = sAliveWorldMgr->GetTierManager();
    auto* memoryMgr = sAliveWorldMgr->GetBotMemoryManager();

    if (tierMgr)
    {
        // Notify tier manager that bot is in a world event
        tierMgr->OnWorldEventJoin(botGuid, event.eventId);
    }

    if (memoryMgr)
    {
        // Record event participation in bot memory
        memoryMgr->RecordEventJoin(
            botGuid,
            event.eventId,
            event.instanceId,
            def.name,
            static_cast<uint8>(def.type),
            def.location.zoneId,
            voluntary,
            role
        );
    }

    EC_LOG_TRACE("NotifyAliveWorldBotJoined: bot %llu joined event %u as %s (voluntary=%s)",
        botGuid, event.instanceId, role.c_str(), voluntary ? "yes" : "no");
#else
    (void)botGuid;
    (void)event;
    (void)def;
    (void)voluntary;
    (void)role;
#endif
}

void WorldEventManager::NotifyAliveWorldEventEnded(ActiveEvent& event, bool successful)
{
#ifdef MOD_ALIVE_WORLD
    if (!sAliveWorldMgr->IsEnabled())
        return;

    if (event.aliveWorldEndNotified)
        return;
    event.aliveWorldEndNotified = true;

    auto* tierMgr = sAliveWorldMgr->GetTierManager();
    auto* memoryMgr = sAliveWorldMgr->GetBotMemoryManager();
    auto* worldStateMgr = sAliveWorldMgr->GetWorldStateManager();

    EC_LOG_INFO("NotifyAliveWorldEventEnded: instance %u, %zu participants, success=%s",
        event.instanceId, event.participants.size(), successful ? "yes" : "no");

    // Notify each participant
    for (const auto& pair : event.participants)
    {
        uint64 botGuid = pair.first.GetCounter();

        if (tierMgr)
        {
            tierMgr->OnWorldEventLeave(botGuid, event.eventId);
        }

        if (memoryMgr)
        {
            // Update final stats
            memoryMgr->UpdateEventStats(
                botGuid,
                event.instanceId,
                pair.second.kills,
                pair.second.deaths,
                pair.second.damageDealt,
                pair.second.healingDone
            );

            // Record event end
            memoryMgr->RecordEventLeave(botGuid, event.instanceId, successful);

            // Evolve personality based on event outcome
            if (successful)
            {
                // Success increases confidence/aggression slightly
                memoryMgr->EvolvePersonality(botGuid, "event_success", 0.05f);
            }
            else
            {
                // Failure might increase caution
                memoryMgr->EvolvePersonality(botGuid, "event_failure", 0.03f);
            }
        }
    }

    // Generate gossip about the event
    if (worldStateMgr)
    {
        const EventDefinition* def = GetEventDefinition(event.eventId);
        if (def)
        {
            std::stringstream headline;
            if (successful)
            {
                if (def->type == EventType::RAID)
                    headline << def->name << " raid was victorious!";
                else if (def->type == EventType::DEFENSE)
                    headline << def->location.name << " successfully defended!";
                else if (def->type == EventType::WORLD_BOSS)
                    headline << "World boss defeated at " << def->location.name << "!";
                else
                    headline << def->name << " event completed!";
            }
            else
            {
                if (def->type == EventType::RAID)
                    headline << def->name << " raid was repelled.";
                else if (def->type == EventType::DEFENSE)
                    headline << def->location.name << " fell to attackers.";
                else
                    headline << def->name << " event failed.";
            }

            std::stringstream details;
            details << "Participants: " << event.participants.size() << ". ";
            details << "Kills: " << event.totalKills << ". ";
            details << (successful ? "Victory!" : "Defeat.");

            // Create gossip that will spread among bots
            AliveWorld::GossipItem gossip;
            gossip.headline = headline.str();
            gossip.details = details.str();
            gossip.originZoneId = def->location.zoneId;
            gossip.importance = successful ? 7 : 5;  // Victory is more gossip-worthy

            worldStateMgr->AddGossip(gossip);

            EC_LOG_INFO("Generated event gossip: %s", headline.str().c_str());
        }
    }

#else
    (void)event;
    (void)successful;
#endif
}

float WorldEventManager::CalculatePersonalityEventAffinity(uint64 botGuid, EventType eventType) const
{
#ifdef MOD_ALIVE_WORLD
    if (!sAliveWorldMgr->IsEnabled())
        return 0.5f;  // Neutral affinity if AliveWorld not enabled

    auto* memoryMgr = sAliveWorldMgr->GetBotMemoryManager();
    if (!memoryMgr)
        return 0.5f;

    AliveWorld::BotPersonality& personality = memoryMgr->GetPersonality(botGuid);

    float affinity = 0.5f;  // Base affinity

    switch (eventType)
    {
        case EventType::RAID:
            // Aggressive, vengeful bots love raids
            affinity = personality.aggression * 0.5f +
                      personality.vengefulness * 0.2f +
                      (1.0f - personality.caution) * 0.3f;
            break;

        case EventType::DEFENSE:
            // Helpful, loyal, cautious bots prefer defense
            affinity = personality.helpfulness * 0.4f +
                      personality.loyalty * 0.3f +
                      personality.caution * 0.2f +
                      (1.0f - personality.aggression) * 0.1f;
            break;

        case EventType::WORLD_BOSS:
            // Aggressive and curious bots want boss fights
            affinity = personality.aggression * 0.4f +
                      personality.curiosity * 0.3f +
                      (1.0f - personality.caution) * 0.3f;
            break;

        case EventType::GATHERING:
            // Curious, greedy bots like gathering events
            affinity = personality.curiosity * 0.4f +
                      personality.greed * 0.3f +
                      (1.0f - personality.aggression) * 0.3f;
            break;

        case EventType::PVP_ZONE:
            // Aggressive, vengeful bots thrive in PvP
            affinity = personality.aggression * 0.6f +
                      personality.vengefulness * 0.3f +
                      (1.0f - personality.caution) * 0.1f;
            break;

        default:
            affinity = 0.5f;
            break;
    }

    return std::clamp(affinity, 0.0f, 1.0f);
#else
    (void)botGuid;
    (void)eventType;
    return 0.5f;
#endif
}

void WorldEventManager::DespawnEventBots(ActiveEvent& event)
{
#ifdef MOD_PLAYERBOTS
    if (EventDefinition const* raidDef = GetEventDefinition(event.eventId))
    {
        for (ObjectGuid const& guid : event.spawnedBots)
        {
            auto checkpoint = m_botReturnContexts.find(guid);
            Player* bot = ObjectAccessor::FindPlayer(guid);
            if (checkpoint != m_botReturnContexts.end() &&
                RaidOwnsBot(event, checkpoint->second, bot, *raidDef))
            {
                bot->GetMotionMaster()->Clear();
                bot->StopMoving();
            }
        }
    }
#endif
    event.raidAssaultController = RaidAssault::Controller{};
    event.raidObjectivesAttempted = false;
    event.raidObjectiveElapsedMs = 0;
    event.raidAssaultResidentSources.clear();
    event.raidAssaultBuildingSources.clear();
    event.raidAssaultResourceSources.clear();
    event.raidResourceNodesDestroyed.clear();
    event.regionalColumns.clear();
    event.raidAssaultHeld.clear();
    EC_LOG_INFO("DespawnEventBots for instance %u (%zu teleported bots, %zu pending teleports)",
        event.instanceId, event.spawnedBots.size(), event.pendingBotTeleports.size());

#ifdef MOD_PLAYERBOTS
    const EventDefinition* def = GetEventDefinition(event.eventId);
    std::set<ObjectGuid> botsToReturn(event.spawnedBots.begin(), event.spawnedBots.end());
    for (const auto& [guid, context] : m_botReturnContexts)
    {
        if (context.eventInstanceId == event.instanceId)
            botsToReturn.insert(guid);
    }

    for (const ObjectGuid& guid : botsToReturn)
    {
        Player* bot = ObjectAccessor::FindPlayer(guid);
        if (!bot)
        {
            EC_LOG_DEBUG("  Bot %s is offline; durable return remains queued", guid.ToString().c_str());
            continue;
        }

        EC_LOG_DEBUG("  Queuing pre-event state restore for bot %s", bot->GetName().c_str());

        if (WorldSession* botSession = bot->GetSession())
        {
            ChatHandler(botSession).PSendSysMessage(
                "|cff00ff00[EC World Events]|r The %s event has ended. Returning you to your previous activity.",
                def ? def->name.c_str() : "event");
        }
    }

    uint32 releasedCount = static_cast<uint32>(botsToReturn.size());
    event.spawnedBots.clear();
    event.pendingBotTeleports.clear();

    // Emit to realtime bridge
    if (releasedCount > 0)
    {
        sWorldEventBridge->EmitBotsDismissed(
            event.eventId,
            event.instanceId,
            releasedCount
        );
    }

    EC_LOG_INFO("Queued %u bot(s) from event instance %u for exact pre-event restore",
        releasedCount, event.instanceId);

#else
    EC_LOG_WARN("MOD_PLAYERBOTS not defined - clearing bot lists only");
    event.spawnedBots.clear();
    event.pendingBotTeleports.clear();
#endif
}

// =============================================================================
// GameObject Spawning (Barricades, Fortifications)
// =============================================================================

void WorldEventManager::SpawnEventGameObjects(ActiveEvent& event, EventPhase forPhase)
{
    const EventDefinition* def = GetEventDefinition(event.eventId);
    if (!def)
    {
        EC_LOG_ERROR("SpawnEventGameObjects: No definition for event %u", event.eventId);
        return;
    }

    uint8 targetPhase = static_cast<uint8>(forPhase);
    EC_LOG_INFO("SpawnEventGameObjects for event '%s', phase=%s",
        def->name.c_str(), EventPhaseToString(forPhase).c_str());

    // Get the map for spawning
    Map* map = sMapMgr->FindBaseNonInstanceMap(def->location.mapId);
    if (!map)
    {
        EC_LOG_ERROR("  Failed to find map %u for object spawning!", def->location.mapId);
        return;
    }

    uint32 spawnedCount = 0;
    bool defenderClearCountComputed = false;
    uint32 aliveDefendersNearCity = 0;

    for (const EventObject& obj : def->objects)
    {
        // Only spawn objects matching this phase
        if (obj.spawnPhase != targetPhase)
            continue;

        // Check spawn condition (for ENDING phase victory/defeat objects)
        if (obj.spawnCondition != 0)
        {
            if (obj.spawnCondition == 1 && event.outcome != EventOutcome::ATTACKER_WIN)
                continue;
            if (obj.spawnCondition == 2 && event.outcome != EventOutcome::DEFENDER_WIN)
                continue;
        }

        // For RAID events, attacker-faction banners should only appear after actual takeover:
        // attacker must have won and defender NPCs near the objective must be cleared.
        bool isRaidAttackerBanner = (def->type == EventType::RAID) &&
            IsBannerForFaction(obj.gameObjectEntry, def->attackerFaction);

        if (isRaidAttackerBanner)
        {
            if (event.outcome != EventOutcome::ATTACKER_WIN)
                continue;

            if (!defenderClearCountComputed)
            {
                aliveDefendersNearCity = CountAliveRaidDefendersNearObjective(map, *def);
                defenderClearCountComputed = true;
            }

            if (aliveDefendersNearCity > 0)
            {
                EC_LOG_INFO("  Delaying takeover banner '%s': %u defender NPCs still alive within %.0f yards",
                    obj.name.c_str(), aliveDefendersNearCity, kRaidTakeoverClearRadiusYards);
                continue;
            }
        }

        // Calculate world position from event center + offset
        float worldX = def->location.x + obj.offsetX;
        float worldY = def->location.y + obj.offsetY;
        float worldZ = def->location.z + obj.offsetZ;

        EC_LOG_DEBUG("  Spawning GameObject '%s' (entry %u) at (%.2f, %.2f, %.2f)",
            obj.name.c_str(), obj.gameObjectEntry, worldX, worldY, worldZ);

        // Create a temporary GameObject (despawns on server restart)
        GameObject* go = new GameObject();
        if (!go->Create(
            map->GenerateLowGuid<HighGuid::GameObject>(),
            obj.gameObjectEntry,
            map,
            PHASEMASK_NORMAL,  // phaseMask
            worldX,
            worldY,
            worldZ,
            obj.orientation,
            G3D::Quat(0, 0, 0, 1),  // Rotation quaternion
            255,  // GO state active
            GO_STATE_READY     // GO state
        ))
        {
            EC_LOG_WARN("    Failed to create GameObject entry %u", obj.gameObjectEntry);
            delete go;
            continue;
        }

        // Set the despawn delay to match event duration (no auto-despawn during event)
        go->SetRespawnTime(0);  // Don't respawn - we manage despawn manually

        // Add to the world
        map->AddToMap(go);

        // Track for cleanup (store GUID + despawnPhase for selective despawning)
        event.spawnedObjects.push_back({go->GetGUID(), obj.despawnPhase});
        ++spawnedCount;

        // If capturable, add to buildings tracking
        if (obj.isCapturable)
        {
            SpawnedBuilding building;
            building.objectDbId = obj.objectId;
            building.objectGuid = go->GetGUID();
            building.burningGuid = ObjectGuid::Empty;
            building.destroyedGuid = ObjectGuid::Empty;
            building.state = BuildingState::INTACT;
            building.capturerGuid = ObjectGuid::Empty;
            building.capturingFaction = EventFaction::NEUTRAL;
            building.buildingName = obj.name;
            building.pointsValue = obj.pointsOnCapture;

            // Barracks-style guard production. Persistent settlement
            // development increases capacity and response speed, while damage
            // removes those bonuses. Base object values remain authoritative.
            building.lastGuardRespawnMs = 0;
            building.guardsProduced = 0;
            building.maxGuards = obj.maxGuards;
            building.guardRespawnMs = obj.guardRespawnSeconds * 1000;

            auto settlementIt = m_settlements.find(event.settlementId);
            if (settlementIt != m_settlements.end() && settlementIt->second.damagePct == 0)
            {
                uint16 development = settlementIt->second.developmentLevel;
                building.maxGuards += development;
                uint32 capacityPct = std::max<int32>(0,
                    settlementIt->second.strategicEffects["guard_capacity_pct"]);
                building.maxGuards += building.maxGuards * capacityPct / 100;
                if (building.guardRespawnMs > 0 && development > 0)
                {
                    uint32 speedPct = std::min<uint32>(50, development * 10);
                    speedPct = std::min<uint32>(75, speedPct + std::max<int32>(0,
                        settlementIt->second.strategicEffects["guard_response_pct"]));
                    building.guardRespawnMs = building.guardRespawnMs * (100 - speedPct) / 100;
                }
            }

            event.buildings[obj.objectId] = building;

            EC_LOG_DEBUG("    Capturable building '%s' tracked (DbId: %u, Points: %d)",
                obj.name.c_str(), obj.objectId, obj.pointsOnCapture);
        }

        EC_LOG_DEBUG("    Spawned GameObject %s (GUID: %s)",
            obj.name.c_str(), go->GetGUID().ToString().c_str());
    }

    EC_LOG_INFO("Spawned %u GameObjects for event instance %u (phase %s), %zu capturable buildings",
        spawnedCount, event.instanceId, EventPhaseToString(forPhase).c_str(), event.buildings.size());
}

void WorldEventManager::DespawnEventGameObjects(ActiveEvent& event, EventPhase forPhase)
{
    const EventDefinition* def = GetEventDefinition(event.eventId);
    if (!def)
        return;

    uint8 targetPhase = static_cast<uint8>(forPhase);
    EC_LOG_INFO("DespawnEventGameObjects for event '%s', despawn phase=%s",
        def->name.c_str(), EventPhaseToString(forPhase).c_str());

    uint32 despawnedCount = 0;
    auto it = event.spawnedObjects.begin();

    // Get the map from the event definition (def is already available from above)
    Map* map = sMapMgr->FindMap(def->location.mapId, 0);

    while (it != event.spawnedObjects.end())
    {
        // Each spawned object stores its own despawnPhase — match directly
        if (it->second != targetPhase)
        {
            ++it;
            continue;
        }

        GameObject* go = map ? map->GetGameObject(it->first) : nullptr;
        if (go)
        {
            EC_LOG_DEBUG("  Despawning GameObject entry %u (GUID: %s)",
                go->GetEntry(), go->GetGUID().ToString().c_str());
            go->Delete();
        }

        it = event.spawnedObjects.erase(it);
        ++despawnedCount;
    }

    EC_LOG_INFO("Despawned %u GameObjects at phase %s", despawnedCount, EventPhaseToString(forPhase).c_str());
}

void WorldEventManager::DespawnAllEventGameObjects(ActiveEvent& event)
{
    EC_LOG_INFO("DespawnAllEventGameObjects for instance %u (%zu objects)",
        event.instanceId, event.spawnedObjects.size());

    if (event.spawnedObjects.empty())
        return;

    // Get the map from the event definition
    const EventDefinition* def = GetEventDefinition(event.eventId);
    Map* map = def ? sMapMgr->FindMap(def->location.mapId, 0) : nullptr;

    uint32 despawnedCount = 0;
    for (const auto& [guid, phase] : event.spawnedObjects)
    {
        GameObject* go = map ? map->GetGameObject(guid) : nullptr;
        if (go)
        {
            EC_LOG_DEBUG("  Despawning GameObject entry %u", go->GetEntry());
            go->Delete();
            ++despawnedCount;
        }
        else
        {
            EC_LOG_TRACE("  GameObject %s not found (already despawned?)", guid.ToString().c_str());
        }
    }

    event.spawnedObjects.clear();
    EC_LOG_INFO("Despawned %u GameObjects total", despawnedCount);
}

// =============================================================================
// Creature Spawning (Named NPCs, Elite Guards)
// =============================================================================

void WorldEventManager::SpawnEventCreatures(ActiveEvent& event, EventPhase forPhase)
{
    const EventDefinition* def = GetEventDefinition(event.eventId);
    if (!def)
    {
        EC_LOG_ERROR("SpawnEventCreatures: No definition for event %u", event.eventId);
        return;
    }

    uint8 targetPhase = static_cast<uint8>(forPhase);
    EC_LOG_INFO("SpawnEventCreatures for event '%s', phase=%s",
        def->name.c_str(), EventPhaseToString(forPhase).c_str());

    // Get the map for spawning
    Map* map = sMapMgr->FindBaseNonInstanceMap(def->location.mapId);
    if (!map)
    {
        EC_LOG_ERROR("  Failed to find map %u for creature spawning!", def->location.mapId);
        return;
    }

    uint32 spawnedCount = 0;
    uint32 bossCount = 0;
    uint32 leaderCount = 0;
    uint32 skippedPhase = 0;
    uint32 skippedDelay = 0;
    uint32 totalCreatures = static_cast<uint32>(def->creatures.size());

    EC_LOG_INFO("  Event has %u total creature definitions, filtering for phase %u", totalCreatures, targetPhase);

    uint32 deferredBatch = 0;  // Counter for stagger delay on deferred immediate spawns

    for (size_t creatureIdx = 0; creatureIdx < def->creatures.size(); ++creatureIdx)
    {
        const EventCreature& creatureDef = def->creatures[creatureIdx];

        // Only spawn creatures matching this phase
        if (creatureDef.spawnPhase != targetPhase)
        {
            ++skippedPhase;
            continue;
        }

        // Skip creatures with spawn delays - they'll be handled by ProcessReinforcements
        if (creatureDef.spawnDelaySeconds > 0)
        {
            ++skippedDelay;
            EC_LOG_INFO("  [DELAYED] '%s' (entry %u) - will spawn in %u seconds (wave %u)",
                creatureDef.name.c_str(), creatureDef.creatureEntry, creatureDef.spawnDelaySeconds, creatureDef.reinforcementWave);
            continue;
        }

        // Batch limit: defer excess immediate spawns to avoid freezing the world thread.
        // They get queued as reinforcements with small staggered delays.
        if (spawnedCount >= m_maxCreatureSpawnsPerTick)
        {
            PendingReinforcement pending;
            pending.creatureDefId = static_cast<uint32>(creatureIdx);
            pending.spawnTimeMs = (deferredBatch / m_maxCreatureSpawnsPerTick) * m_deferredSpawnStaggerMs + m_deferredSpawnStaggerMs;
            pending.wave = 0;  // Not a real wave, just deferred immediate
            pending.spawned = false;
            event.pendingReinforcements.push_back(pending);
            ++deferredBatch;
            EC_LOG_INFO("  [DEFERRED] '%s' (entry %u) - queued for spawn at %u ms (batch limit reached)",
                creatureDef.name.c_str(), creatureDef.creatureEntry, pending.spawnTimeMs);
            continue;
        }

        // Calculate world position - either from assigned building or event center + offset
        float worldX, worldY, worldZ;
        const EventObject* assignedBuilding = nullptr;

        if (creatureDef.assignedBuildingId > 0)
        {
            // Find the building this guard is assigned to
            for (const EventObject& obj : def->objects)
            {
                if (obj.objectId == creatureDef.assignedBuildingId)
                {
                    assignedBuilding = &obj;
                    break;
                }
            }

            if (assignedBuilding)
            {
                // Spawn at building location with small random offset to avoid stacking
                static thread_local std::mt19937 s_rng(std::random_device{}());
                float randomAngle = std::uniform_real_distribution<float>(0.0f, 2.0f * M_PI)(s_rng);
                float randomDist = std::uniform_real_distribution<float>(3.0f, 8.0f)(s_rng);
                worldX = def->location.x + assignedBuilding->offsetX + cosf(randomAngle) * randomDist;
                worldY = def->location.y + assignedBuilding->offsetY + sinf(randomAngle) * randomDist;
                worldZ = def->location.z + assignedBuilding->offsetZ;

                EC_LOG_DEBUG("  Guard '%s' assigned to building '%s' (id %u)",
                    creatureDef.name.c_str(), assignedBuilding->name.c_str(), assignedBuilding->objectId);
            }
            else
            {
                // Building not found, fall back to creature offset
                EC_LOG_WARN("  Guard '%s' assigned to non-existent building id %u, using default position",
                    creatureDef.name.c_str(), creatureDef.assignedBuildingId);
                worldX = def->location.x + creatureDef.offsetX;
                worldY = def->location.y + creatureDef.offsetY;
                worldZ = def->location.z + creatureDef.offsetZ;
            }
        }
        else
        {
            // No building assignment - use standard event center + offset
            worldX = def->location.x + creatureDef.offsetX;
            worldY = def->location.y + creatureDef.offsetY;
            worldZ = def->location.z + creatureDef.offsetZ;
        }

        EC_LOG_INFO("  [SPAWN] '%s' (entry %u) at (%.2f, %.2f, %.2f) offset=(%.1f, %.1f, %.1f) %s%s",
            creatureDef.name.c_str(), creatureDef.creatureEntry, worldX, worldY, worldZ,
            creatureDef.offsetX, creatureDef.offsetY, creatureDef.offsetZ,
            creatureDef.isBoss ? "[BOSS]" : "",
            creatureDef.isLeader ? "[LEADER]" : "");

        // Use SummonCreature-style spawning for temporary creatures
        TempSummon* summon = map->SummonCreature(
            creatureDef.creatureEntry,
            {worldX, worldY, worldZ, creatureDef.orientation},
            nullptr,  // No summoner
            0,        // No duration (we manage despawn manually)
            nullptr,  // No specific position data
            0         // No spawn flags
        );

        if (!summon)
        {
            EC_LOG_ERROR("  [SPAWN FAILED] '%s' entry %u at (%.2f, %.2f, %.2f) - SummonCreature returned null!",
                creatureDef.name.c_str(), creatureDef.creatureEntry, worldX, worldY, worldZ);
            continue;
        }
        EC_LOG_INFO("  [SPAWN OK] '%s' entry %u GUID=%s alive=%s",
            creatureDef.name.c_str(), creatureDef.creatureEntry,
            summon->GetGUID().ToString().c_str(), summon->IsAlive() ? "YES" : "NO");

        bool summonIsHordeSide = IsHordeAlignedUnit(summon);

        // ========== CREATURE BEHAVIOR: Salute, emote, yell ==========

        // Check if this is a construction worker.
        bool isWorker = IsConstructionWorkerName(creatureDef.name);

        if (isWorker)
        {
            // Workers are non-combatants: immune to NPC and PC aggro
            summon->SetUnitFlag(UNIT_FLAG_IMMUNE_TO_NPC);
            summon->SetUnitFlag(UNIT_FLAG_IMMUNE_TO_PC);

            // Workers do construction emotes instead of military behavior
            static const Emote workEmotes[] = {
                EMOTE_STATE_WORK,               // Continuous hammering
                EMOTE_STATE_WORK_CHOPWOOD,      // Chopping wood animation
                EMOTE_STATE_USE_STANDING,       // Standing work animation
                EMOTE_ONESHOT_WORK_CHOPWOOD     // One-shot chop swing
            };
            summon->HandleEmoteCommand(workEmotes[rand() % 4]);

            // Workers announce what they're building using their own faction side.
            if (summonIsHordeSide)
            {
                static const char* peonSays[] = {
                    "Work work...",
                    "Zug zug!",
                    "Something need doing?",
                    "Me busy! Leave me alone!",
                    "Ready to work!"
                };
                summon->Say(peonSays[rand() % 5], LANG_ORCISH);
            }
            else
            {
                static const char* workerSays[] = {
                    "Almost got this barricade up!",
                    "Hand me that lumber!",
                    "These fortifications won't build themselves!",
                    "Keep working, they'll be here soon!",
                    "Reinforcing the defenses!"
                };
                summon->Say(workerSays[rand() % 5], LANG_COMMON);
            }

            // Workers walk slowly and patrol tight radius around their assigned spot
            summon->SetWalk(true);
            float patrolRadius = creatureDef.patrolRadius > 0 ? creatureDef.patrolRadius : 5.0f;
            summon->GetMotionMaster()->MoveRandom(patrolRadius);

            // Track for cleanup
            event.spawnedCreatures.push_back(summon->GetGUID());
            ++spawnedCount;
            EC_LOG_INFO("  [WORKER] '%s' doing construction work at (%.2f, %.2f, %.2f)",
                creatureDef.name.c_str(), worldX, worldY, worldZ);
            continue;  // Skip normal military behavior
        }

        // Military NPCs should actively engage nearby hostiles.
        summon->SetReactState(REACT_AGGRESSIVE);

        // All military creatures salute when spawning (military discipline!)
        summon->HandleEmoteCommand(EMOTE_ONESHOT_SALUTE);

        // If leader, add special effects/emotes
        if (creatureDef.isLeader)
        {
            // Make leaders yell an announcement
            std::string announcement;
            if (summonIsHordeSide)
            {
                // Orcish battle cries
                static const char* hordeYells[] = {
                    "LOK'TAR OGAR! Victory or death!",
                    "For the Horde! Attack!",
                    "Blood and thunder! To battle!",
                    "The Warchief commands - no mercy!",
                    "Strength and honor! Crush them!"
                };
                announcement = hordeYells[rand() % 5];
            }
            else
            {
                // Alliance battle cries
                static const char* allianceYells[] = {
                    "FOR THE ALLIANCE! Charge!",
                    "By the Light! We shall prevail!",
                    "In the name of the King, attack!",
                    "No quarter! Show them our resolve!",
                    "Glory to the Alliance! Forward!"
                };
                announcement = allianceYells[rand() % 5];
            }

            summon->Yell(announcement, LANG_UNIVERSAL);
            summon->HandleEmoteCommand(EMOTE_ONESHOT_BATTLE_ROAR);
            ++leaderCount;
        }
        else
        {
            // All grunts/guards announce themselves with their role name
            // First: every creature says their role so we can identify them
            char roleMsg[128];
            snprintf(roleMsg, sizeof(roleMsg), "%s reporting for duty!", creatureDef.name.c_str());
            summon->Say(roleMsg, LANG_UNIVERSAL);

            // Then some add flavor
            if (rand() % 2 == 0)  // 50% chance for extra flavor
            {
                if (summonIsHordeSide)
                {
                    static const char* gruntYells[] = {
                        "Zug zug!",
                        "For the Horde!",
                        "Ready for battle!",
                        "Lok'tar!"
                    };
                    summon->Say(gruntYells[rand() % 4], LANG_ORCISH);
                }
                else
                {
                    static const char* guardYells[] = {
                        "For the King!",
                        "Hold the line!",
                        "Stay vigilant!",
                        "Ready and able!"
                    };
                    summon->Say(guardYells[rand() % 4], LANG_COMMON);
                }
            }
        }

        if (creatureDef.isBoss)
            ++bossCount;

        // ========== CREATURE BEHAVIOR: Movement & Patrol ==========

        // Give creatures some movement - patrol behavior depends on assignment
        if (!creatureDef.isBoss && !creatureDef.isLeader)
        {
            if (assignedBuilding)
            {
                // Building guards patrol a tight radius around their assigned building
                // Use the configured patrol radius from database, default 10 yards
                float patrolRadius = creatureDef.patrolRadius > 0 ? creatureDef.patrolRadius : 10.0f;

                // Guards walk while on patrol (looks more professional)
                summon->SetWalk(true);
                summon->GetMotionMaster()->MoveRandom(patrolRadius);

                EC_LOG_INFO("  [PATROL] '%s' patrolling building '%s' (radius: %.1f)",
                    creatureDef.name.c_str(), assignedBuilding->name.c_str(), patrolRadius);
            }
            else
            {
                // Regular soldiers patrol between their spawn point and the event center
                // Use configured radius or random 15-35 yards
                float patrolRadius = creatureDef.patrolRadius > 0 ? creatureDef.patrolRadius : (15.0f + (rand() % 20));

                // Set to wander/patrol
                summon->SetWalk(false);  // Run
                summon->GetMotionMaster()->MoveRandom(patrolRadius);
            }
        }
        else if (creatureDef.isLeader)
        {
            // Leaders walk towards the event center with purpose
            summon->SetWalk(true);
            summon->GetMotionMaster()->MovePoint(0, def->location.x, def->location.y, def->location.z);
        }

        // Track for cleanup
        event.spawnedCreatures.push_back(summon->GetGUID());
        ++spawnedCount;
    }

    EC_LOG_INFO("=== SpawnEventCreatures SUMMARY for '%s' phase=%s ===", def->name.c_str(), EventPhaseToString(forPhase).c_str());
    EC_LOG_INFO("  Total creature defs: %u | Skipped (wrong phase): %u | Skipped (delayed): %u | Spawned: %u | Deferred: %u",
        totalCreatures, skippedPhase, skippedDelay, spawnedCount, deferredBatch);
    EC_LOG_INFO("  Bosses: %u | Leaders: %u | Tracked GUIDs: %zu",
        bossCount, leaderCount, event.spawnedCreatures.size());
}

void WorldEventManager::DespawnEventCreatures(ActiveEvent& event, EventPhase forPhase)
{
    const EventDefinition* def = GetEventDefinition(event.eventId);
    if (!def)
        return;

    uint8 targetPhase = static_cast<uint8>(forPhase);
    EC_LOG_INFO("DespawnEventCreatures for event '%s', despawn phase=%s",
        def->name.c_str(), EventPhaseToString(forPhase).c_str());

    // Build list of creature entries to despawn at this phase
    std::set<uint32> entriesToDespawn;
    for (const EventCreature& creatureDef : def->creatures)
    {
        if (creatureDef.despawnPhase == targetPhase)
        {
            entriesToDespawn.insert(creatureDef.creatureEntry);
        }
    }

    if (entriesToDespawn.empty())
    {
        EC_LOG_DEBUG("  No creatures to despawn at this phase");
        return;
    }

    uint32 despawnedCount = 0;
    auto it = event.spawnedCreatures.begin();

    // Get the map from the event definition (def is already available from above)
    Map* map = sMapMgr->FindMap(def->location.mapId, 0);

    while (it != event.spawnedCreatures.end())
    {
        Creature* creature = map ? map->GetCreature(*it) : nullptr;

        if (creature && entriesToDespawn.count(creature->GetEntry()) > 0)
        {
            EC_LOG_DEBUG("  Despawning Creature entry %u (GUID: %s)",
                creature->GetEntry(), creature->GetGUID().ToString().c_str());

            creature->DespawnOrUnsummon();
            it = event.spawnedCreatures.erase(it);
            ++despawnedCount;
        }
        else
        {
            ++it;
        }
    }

    EC_LOG_INFO("Despawned %u Creatures at phase %s", despawnedCount, EventPhaseToString(forPhase).c_str());
}

void WorldEventManager::DespawnAllEventCreatures(ActiveEvent& event)
{
    EC_LOG_INFO("DespawnAllEventCreatures for instance %u (%zu creatures)",
        event.instanceId, event.spawnedCreatures.size());

    if (event.spawnedCreatures.empty())
        return;

    // Get the map from the event definition
    const EventDefinition* def = GetEventDefinition(event.eventId);
    Map* map = def ? sMapMgr->FindMap(def->location.mapId, 0) : nullptr;

    uint32 despawnedCount = 0;
    for (const ObjectGuid& guid : event.spawnedCreatures)
    {
        Creature* creature = map ? map->GetCreature(guid) : nullptr;
        if (creature)
        {
            EC_LOG_DEBUG("  Despawning Creature entry %u", creature->GetEntry());
            creature->DespawnOrUnsummon();
            ++despawnedCount;
        }
        else
        {
            EC_LOG_TRACE("  Creature %s not found (already despawned?)", guid.ToString().c_str());
        }
    }

    event.spawnedCreatures.clear();
    EC_LOG_INFO("Despawned %u Creatures total", despawnedCount);
}

// =============================================================================
// Reinforcement Waves (zeppelin, boat, portal arrivals)
// =============================================================================

std::string WorldEventManager::ArrivalMethodToString(ArrivalMethod method)
{
    switch (method)
    {
        case ArrivalMethod::MARCH:       return "march";
        case ArrivalMethod::ZEPPELIN:    return "zeppelin";
        case ArrivalMethod::BOAT:        return "boat";
        case ArrivalMethod::PORTAL:      return "portal";
        case ArrivalMethod::FLIGHT_PATH: return "flight_path";
        case ArrivalMethod::MOUNT:       return "mount";
        default:                         return "unknown";
    }
}

void WorldEventManager::SetupReinforcementWaves(ActiveEvent& event, EventPhase forPhase)
{
    const EventDefinition* def = GetEventDefinition(event.eventId);
    if (!def)
        return;

    uint8 targetPhase = static_cast<uint8>(forPhase);

    // Clear any existing pending reinforcements
    event.pendingReinforcements.clear();
    event.phaseElapsedMs = 0;

    // Find all creatures with spawn delay for this phase
    for (size_t i = 0; i < def->creatures.size(); ++i)
    {
        const EventCreature& creatureDef = def->creatures[i];

        // Only creatures for this phase with a delay
        if (creatureDef.spawnPhase != targetPhase)
            continue;

        if (creatureDef.spawnDelaySeconds == 0)
            continue;  // Immediate spawns handled by SpawnEventCreatures

        PendingReinforcement pending;
        pending.creatureDefId = static_cast<uint32>(i);
        pending.spawnTimeMs = creatureDef.spawnDelaySeconds * 1000;
        pending.wave = creatureDef.reinforcementWave;
        pending.spawned = false;

        event.pendingReinforcements.push_back(pending);

        EC_LOG_INFO("  [REINFORCEMENT SCHEDULED] '%s' (entry %u) at %u ms (wave %u, %s) arrival=(%.1f, %.1f, %.1f) dest_offset=(%.1f, %.1f, %.1f)",
            creatureDef.name.c_str(), creatureDef.creatureEntry,
            pending.spawnTimeMs, pending.wave,
            ArrivalMethodToString(creatureDef.arrivalMethod).c_str(),
            creatureDef.hasArrivalPoint ? creatureDef.arrivalX : 0.0f,
            creatureDef.hasArrivalPoint ? creatureDef.arrivalY : 0.0f,
            creatureDef.hasArrivalPoint ? creatureDef.arrivalZ : 0.0f,
            creatureDef.offsetX, creatureDef.offsetY, creatureDef.offsetZ);
    }

    EC_LOG_INFO("=== SetupReinforcementWaves SUMMARY for event %u phase=%s: %zu reinforcements queued ===",
        event.instanceId, EventPhaseToString(forPhase).c_str(), event.pendingReinforcements.size());
}

void WorldEventManager::ProcessReinforcements(ActiveEvent& event, uint32 diff)
{
    if (event.pendingReinforcements.empty())
        return;

    const EventDefinition* def = GetEventDefinition(event.eventId);
    if (!def)
        return;

    event.phaseElapsedMs += diff;

    // Track waves being spawned this tick for announcements
    std::map<std::pair<ArrivalMethod, uint8>, uint32> waveSpawnCounts;

    for (PendingReinforcement& pending : event.pendingReinforcements)
    {
        if (pending.spawned)
            continue;

        if (event.phaseElapsedMs >= pending.spawnTimeMs)
        {
            // Time to spawn this reinforcement!
            if (pending.creatureDefId < def->creatures.size())
            {
                const EventCreature& creatureDef = def->creatures[pending.creatureDefId];

                EC_LOG_INFO("[REINFORCEMENT TRIGGER] '%s' (entry %u) wave %u - elapsed %u ms >= scheduled %u ms",
                    creatureDef.name.c_str(), creatureDef.creatureEntry, pending.wave,
                    event.phaseElapsedMs, pending.spawnTimeMs);

                SpawnReinforcementCreature(event, creatureDef);

                // Track for wave announcement
                auto key = std::make_pair(creatureDef.arrivalMethod, pending.wave);
                waveSpawnCounts[key]++;
            }
            else
            {
                EC_LOG_ERROR("[REINFORCEMENT ERROR] creatureDefId %u out of range (max %zu)",
                    pending.creatureDefId, def->creatures.size());
            }

            pending.spawned = true;
        }
    }

    // Announce waves that just spawned
    for (const auto& pair : waveSpawnCounts)
    {
        AnnounceReinforcementArrival(event, pair.first.first, pair.first.second, pair.second);
    }
}

void WorldEventManager::SpawnReinforcementCreature(ActiveEvent& event, const EventCreature& creatureDef)
{
    const EventDefinition* def = GetEventDefinition(event.eventId);
    if (!def)
        return;

    Map* map = sMapMgr->FindBaseNonInstanceMap(def->location.mapId);
    if (!map)
    {
        EC_LOG_ERROR("Failed to find map %u for reinforcement spawning!", def->location.mapId);
        return;
    }

    // Calculate spawn position - either arrival point or final destination
    float spawnX, spawnY, spawnZ;
    float destX, destY, destZ;
    bool hasArrival = creatureDef.hasArrivalPoint;

    // Final destination (event center + offset, or building location)
    const EventObject* assignedBuilding = nullptr;
    if (creatureDef.assignedBuildingId > 0)
    {
        for (const EventObject& obj : def->objects)
        {
            if (obj.objectId == creatureDef.assignedBuildingId)
            {
                assignedBuilding = &obj;
                break;
            }
        }
    }

    if (assignedBuilding)
    {
        destX = def->location.x + assignedBuilding->offsetX;
        destY = def->location.y + assignedBuilding->offsetY;
        destZ = def->location.z + assignedBuilding->offsetZ;
    }
    else
    {
        destX = def->location.x + creatureDef.offsetX;
        destY = def->location.y + creatureDef.offsetY;
        destZ = def->location.z + creatureDef.offsetZ;
    }

    if (hasArrival)
    {
        // Spawn at arrival point, will walk to destination
        spawnX = creatureDef.arrivalX;
        spawnY = creatureDef.arrivalY;
        spawnZ = creatureDef.arrivalZ;
    }
    else
    {
        // No arrival point - spawn directly at destination
        spawnX = destX;
        spawnY = destY;
        spawnZ = destZ;
    }

    EC_LOG_INFO("[REINFORCEMENT SPAWN] '%s' (entry %u) spawn=(%.1f, %.1f, %.1f) dest=(%.1f, %.1f, %.1f) hasArrival=%s method=%s",
        creatureDef.name.c_str(), creatureDef.creatureEntry,
        spawnX, spawnY, spawnZ, destX, destY, destZ,
        hasArrival ? "YES" : "NO",
        ArrivalMethodToString(creatureDef.arrivalMethod).c_str());

    // Spawn the creature
    TempSummon* summon = map->SummonCreature(
        creatureDef.creatureEntry,
        {spawnX, spawnY, spawnZ, creatureDef.orientation},
        nullptr, 0, nullptr, 0
    );

    if (!summon)
    {
        EC_LOG_ERROR("[REINFORCEMENT SPAWN FAILED] '%s' (entry %u) at (%.1f, %.1f, %.1f) - SummonCreature returned null!",
            creatureDef.name.c_str(), creatureDef.creatureEntry, spawnX, spawnY, spawnZ);
        return;
    }
    EC_LOG_INFO("[REINFORCEMENT SPAWN OK] '%s' GUID=%s alive=%s",
        creatureDef.name.c_str(), summon->GetGUID().ToString().c_str(), summon->IsAlive() ? "YES" : "NO");

    // Reinforcements are combatants and should aggressively acquire targets.
    summon->SetReactState(REACT_AGGRESSIVE);

    // Reinforcement arrival behavior!
    bool isHordeSide = IsHordeAlignedUnit(summon);

    // Every reinforcement announces their role so they're identifiable in-game
    {
        char roleMsg[128];
        snprintf(roleMsg, sizeof(roleMsg), "%s reporting for duty!", creatureDef.name.c_str());
        summon->Say(roleMsg, LANG_UNIVERSAL);
    }

    // Arrival emotes and yells based on method
    switch (creatureDef.arrivalMethod)
    {
        case ArrivalMethod::ZEPPELIN:
        {
            // Epic zeppelin arrival!
            if (creatureDef.isLeader)
            {
                if (isHordeSide)
                {
                    static const char* thrallArrivals[] = {
                        "The zeppelin has arrived! FOR THE HORDE!",
                        "I have come to defend our lands! LOK'TAR OGAR!",
                        "No one threatens our people! ATTACK!"
                    };
                    summon->Yell(thrallArrivals[rand() % 3], LANG_UNIVERSAL);
                }
                else
                {
                    static const char* allianceArrivals[] = {
                        "Reinforcements have arrived by airship!",
                        "We stand with our allies! FOR THE ALLIANCE!",
                        "The cavalry is here! Charge!"
                    };
                    summon->Yell(allianceArrivals[rand() % 3], LANG_UNIVERSAL);
                }
                summon->HandleEmoteCommand(EMOTE_ONESHOT_BATTLE_ROAR);
            }
            else
            {
                // Regular guards from zeppelin
                summon->HandleEmoteCommand(EMOTE_ONESHOT_SALUTE);
                if (rand() % 3 == 0)
                {
                    summon->Say(isHordeSide ? "Reporting for duty!" : "Ready to fight!", LANG_UNIVERSAL);
                }
            }
            break;
        }

        case ArrivalMethod::BOAT:
        {
            if (creatureDef.isLeader)
            {
                summon->Yell("We sailed through storms to reach you! TO BATTLE!", LANG_UNIVERSAL);
                summon->HandleEmoteCommand(EMOTE_ONESHOT_BATTLE_ROAR);
            }
            else
            {
                summon->HandleEmoteCommand(EMOTE_ONESHOT_SALUTE);
            }
            break;
        }

        case ArrivalMethod::PORTAL:
        {
            if (creatureDef.isLeader)
            {
                summon->Yell("The portal is open! Reinforcements incoming!", LANG_UNIVERSAL);
                summon->HandleEmoteCommand(EMOTE_ONESHOT_SPELL_CAST);
            }
            else
            {
                summon->HandleEmoteCommand(EMOTE_ONESHOT_SPELL_CAST);
            }
            break;
        }

        case ArrivalMethod::FLIGHT_PATH:
        {
            // Flight path arrival (wyvern/gryphon/bat/hippogryph) - landing animation!
            if (creatureDef.isLeader)
            {
                if (isHordeSide)
                {
                    static const char* hordeFlightArrivals[] = {
                        "I flew as fast as the wind carries! LOK'TAR OGAR!",
                        "The Warchief has sent me! For the Horde!",
                        "Reinforcements from Orgrimmar! We will not fall!"
                    };
                    summon->Yell(hordeFlightArrivals[rand() % 3], LANG_UNIVERSAL);
                }
                else
                {
                    static const char* allianceFlightArrivals[] = {
                        "I flew from Stormwind as fast as I could!",
                        "The King has sent reinforcements! FOR THE ALLIANCE!",
                        "By gryphon we come! To battle!"
                    };
                    summon->Yell(allianceFlightArrivals[rand() % 3], LANG_UNIVERSAL);
                }
                summon->HandleEmoteCommand(EMOTE_ONESHOT_BATTLE_ROAR);
            }
            else
            {
                // Regular soldiers landing from flight
                summon->HandleEmoteCommand(EMOTE_ONESHOT_LAND);  // Landing animation
                if (rand() % 3 == 0)
                {
                    summon->Say(isHordeSide ? "Flew in from Orgrimmar!" : "Flew in from Stormwind!", LANG_UNIVERSAL);
                }
            }
            break;
        }

        case ArrivalMethod::MOUNT:
        {
            // Ground mount arrival - galloping in!
            if (creatureDef.isLeader)
            {
                summon->Yell("The cavalry has arrived! CHARGE!", LANG_UNIVERSAL);
                summon->HandleEmoteCommand(EMOTE_ONESHOT_BATTLE_ROAR);
            }
            else
            {
                // Dismount and salute
                summon->HandleEmoteCommand(EMOTE_ONESHOT_SALUTE);
            }
            break;
        }

        default:
            // March/instant - just salute
            summon->HandleEmoteCommand(EMOTE_ONESHOT_SALUTE);
            break;
    }

    // If has arrival point, walk to destination
    if (hasArrival)
    {
        float dist = std::sqrt((destX - spawnX) * (destX - spawnX) +
                               (destY - spawnY) * (destY - spawnY) +
                               (destZ - spawnZ) * (destZ - spawnZ));
        bool isWalking = creatureDef.isLeader;  // Leaders walk with dignity, others run
        summon->SetWalk(isWalking);
        summon->GetMotionMaster()->MovePoint(0, destX, destY, destZ);

        EC_LOG_INFO("[REINFORCEMENT MOVE] '%s' %s from (%.1f, %.1f, %.1f) -> (%.1f, %.1f, %.1f) distance=%.1f yards",
            creatureDef.name.c_str(), isWalking ? "WALKING" : "RUNNING",
            spawnX, spawnY, spawnZ, destX, destY, destZ, dist);
    }
    else
    {
        // No arrival point - start patrolling immediately
        if (!creatureDef.isBoss && !creatureDef.isLeader)
        {
            float patrolRadius = creatureDef.patrolRadius > 0 ? creatureDef.patrolRadius : 15.0f;
            summon->SetWalk(assignedBuilding != nullptr);
            summon->GetMotionMaster()->MoveRandom(patrolRadius);
            EC_LOG_INFO("[REINFORCEMENT PATROL] '%s' patrolling radius=%.1f at (%.1f, %.1f, %.1f)",
                creatureDef.name.c_str(), patrolRadius, spawnX, spawnY, spawnZ);
        }
        else
        {
            EC_LOG_INFO("[REINFORCEMENT IDLE] '%s' standing at (%.1f, %.1f, %.1f) (boss=%s leader=%s)",
                creatureDef.name.c_str(), spawnX, spawnY, spawnZ,
                creatureDef.isBoss ? "YES" : "NO", creatureDef.isLeader ? "YES" : "NO");
        }
    }

    // Track for cleanup
    event.spawnedCreatures.push_back(summon->GetGUID());
}

void WorldEventManager::AnnounceReinforcementArrival(const ActiveEvent& event, ArrivalMethod method, uint8 wave, uint32 count)
{
    const EventDefinition* def = GetEventDefinition(event.eventId);
    if (!def)
        return;

    std::stringstream msg;
    msg << "|cffff0000[" << def->name << "]|r ";

    bool isHordeDefender = (def->defenderFaction == EventFaction::HORDE);

    switch (method)
    {
        case ArrivalMethod::ZEPPELIN:
            if (isHordeDefender)
                msg << "A Horde zeppelin has arrived with " << count << " reinforcements!";
            else
                msg << "An Alliance airship has arrived with " << count << " reinforcements!";
            break;

        case ArrivalMethod::BOAT:
            if (isHordeDefender)
                msg << "A Horde warship has docked with " << count << " reinforcements!";
            else
                msg << "An Alliance transport has arrived with " << count << " reinforcements!";
            break;

        case ArrivalMethod::PORTAL:
            msg << "A portal has opened! " << count << " reinforcements have arrived!";
            break;

        case ArrivalMethod::FLIGHT_PATH:
            if (isHordeDefender)
                msg << count << " reinforcements have landed by wyvern from Orgrimmar!";
            else
                msg << count << " reinforcements have landed by gryphon from Stormwind!";
            break;

        case ArrivalMethod::MOUNT:
            msg << count << " mounted reinforcements have arrived!";
            break;

        default:
            msg << count << " reinforcements have arrived!";
            break;
    }

    if (wave > 0)
        msg << " (Wave " << static_cast<int>(wave) << ")";

    BroadcastEventMessage(msg.str(), EventFaction::NEUTRAL);
}

// =============================================================================
// Building Capture Mechanics (AV-style towers/bunkers)
// =============================================================================
