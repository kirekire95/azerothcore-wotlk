void WorldEventManager::DistributeRewards(ActiveEvent& event)
{
    const EventDefinition* def = GetEventDefinition(event.eventId);
    if (!def)
    {
        EC_LOG_ERROR("DistributeRewards: No definition for event %u", event.eventId);
        return;
    }

    EC_LOG_INFO("Distributing rewards for event '%s' (Instance %u)",
        def->name.c_str(), event.instanceId);
    EC_LOG_DEBUG("  Total kills: %u", event.totalKills);
    EC_LOG_DEBUG("  Participants: %zu", event.participants.size());
    EC_LOG_DEBUG("  Rewards defined: %zu", def->rewards.size());
    EC_LOG_DEBUG("  MinParticipationForReward: %u%%", m_minParticipationForReward);

    // Calculate total activity for participation scoring
    uint64 totalDamage = 0;
    uint64 totalHealing = 0;
    for (const auto& pair : event.participants)
    {
        totalDamage += pair.second.damageDealt;
        totalHealing += pair.second.healingDone;
    }
    uint64 totalActivity = totalDamage + totalHealing + (event.totalKills * 10000); // Weight kills

    uint32 rewardsGiven = 0;
    uint32 skippedLowParticipation = 0;

    for (auto& pair : event.participants)
    {
        EventParticipant& participant = pair.second;

        EC_LOG_DEBUG("Processing participant: %s (GUID: %u)",
            participant.playerName.c_str(), participant.playerGuid.GetCounter());
        EC_LOG_TRACE("  Kills: %u, Deaths: %u, Damage: %llu, Healing: %llu",
            participant.kills, participant.deaths, participant.damageDealt, participant.healingDone);

        if (participant.rewarded)
        {
            EC_LOG_DEBUG("  Already rewarded, skipping");
            continue;
        }

        // MinParticipationForReward: Global check before any rewards
        if (m_minParticipationForReward > 0 && totalActivity > 0)
        {
            uint64 playerActivity = participant.damageDealt + participant.healingDone +
                                    (participant.kills * 10000);
            float participationPercent = (static_cast<float>(playerActivity) / totalActivity) * 100.0f;

            if (participationPercent < static_cast<float>(m_minParticipationForReward))
            {
                EC_LOG_DEBUG("  Participation %.1f%% below minimum %u%% - no rewards",
                    participationPercent, m_minParticipationForReward);
                skippedLowParticipation++;
                SaveParticipant(event.instanceId, participant);
                continue;
            }
        }

        Player* player = ObjectAccessor::FindPlayer(participant.playerGuid);
        bool isOffline = (player == nullptr);

        // Determine winning faction (works for both online and offline players)
        bool isWinningFaction = false;
        {
            EventFaction playerFaction = EventFaction::NEUTRAL;

            if (player)
            {
                // Online: use live TeamId
                playerFaction = (player->GetTeamId() == TEAM_ALLIANCE)
                    ? EventFaction::ALLIANCE : EventFaction::HORDE;
            }
            else
            {
                // Offline: look up race from characters DB
                QueryResult raceResult = CharacterDatabase.Query(
                    "SELECT race FROM characters WHERE guid = {}",
                    participant.playerGuid.GetCounter()
                );
                if (raceResult)
                {
                    uint8 race = raceResult->Fetch()[0].Get<uint8>();
                    // Alliance: Human(1), Dwarf(3), NightElf(4), Gnome(7), Draenei(11)
                    bool isAlliance = (race == 1 || race == 3 || race == 4 || race == 7 || race == 11);
                    playerFaction = isAlliance ? EventFaction::ALLIANCE : EventFaction::HORDE;
                }
            }

            if (event.outcome == EventOutcome::ATTACKER_WIN)
                isWinningFaction = (playerFaction == def->attackerFaction);
            else if (event.outcome == EventOutcome::DEFENDER_WIN)
                isWinningFaction = (playerFaction == def->defenderFaction);
            // DRAW: nobody wins, but all participants receive condition=0 (any) and condition=2 (loser) rewards
            if (event.outcome == EventOutcome::DRAW)
                EC_LOG_DEBUG("  DRAW outcome: %s receives any-participant and consolation rewards",
                    participant.playerName.c_str());
        }

        // Offline players: deliver rewards via mail
        if (isOffline)
        {
            EC_LOG_DEBUG("  Player %s is offline - delivering rewards via mail",
                participant.playerName.c_str());

            uint32 totalMailGold = 0;
            std::vector<std::pair<uint32, uint32>> mailItems; // {itemId, count}

            for (const EventReward& reward : def->rewards)
            {
                if (reward.rewardCondition == 1 && !isWinningFaction) continue;
                if (reward.rewardCondition == 2 && isWinningFaction) continue;
                if (participant.kills < reward.minKills) continue;

                float participation = event.totalKills > 0
                    ? static_cast<float>(participant.kills) / event.totalKills : 0.0f;
                if (participation < reward.participation) continue;

                switch (reward.type)
                {
                    case RewardType::GOLD:
                        totalMailGold += reward.value;
                        break;
                    case RewardType::ITEM:
                    {
                        ItemTemplate const* itemTpl = sObjectMgr->GetItemTemplate(reward.value);
                        if (itemTpl)
                            mailItems.push_back({reward.value, reward.itemCount > 0 ? reward.itemCount : 1});
                        break;
                    }
                    case RewardType::HONOR:
                        EC_LOG_DEBUG("    Skipping honor for offline player (cannot mail)");
                        break;
                    case RewardType::TITLE:
                        EC_LOG_DEBUG("    Skipping title for offline player (needs client)");
                        break;
                    default:
                        break;
                }
            }

            if (totalMailGold > 0 || !mailItems.empty())
            {
                CharacterDatabaseTransaction trans = CharacterDatabase.BeginTransaction();
                MailSender sender(MAIL_NORMAL, 0, MAIL_STATIONERY_GM);
                MailDraft draft("EC World Event Reward",
                    Acore::StringFormat("Thank you for participating in %s! Here are your rewards.",
                        def->name.c_str()));

                if (totalMailGold > 0)
                    draft.AddMoney(totalMailGold);

                for (const auto& [itemId, count] : mailItems)
                {
                    Item* mailItem = Item::CreateItem(itemId, count);
                    if (mailItem)
                    {
                        mailItem->SaveToDB(trans);
                        draft.AddItem(mailItem);
                    }
                }

                draft.SendMailTo(trans,
                    MailReceiver(participant.playerGuid.GetCounter()), sender);
                CharacterDatabase.CommitTransaction(trans);

                EC_LOG_INFO("  Mailed offline rewards to %s: %ug, %zu items",
                    participant.playerName.c_str(), totalMailGold / 10000, mailItems.size());
                rewardsGiven++;
            }

            participant.rewarded = true;
            SaveParticipant(event.instanceId, participant);

            // Note: Bridge emission skipped for offline players (EmitParticipantRewarded
            // requires Player* for BuildPlayerJson). Offline rewards are logged above.
            continue;
        }

        // Track actual rewards given for bridge emission
        uint32 totalGoldGiven = 0;
        uint32 totalHonorGiven = 0;
        uint32 lastItemGiven = 0;

        for (const EventReward& reward : def->rewards)
        {
            // Check reward condition (faction/outcome filter)
            if (reward.rewardCondition == 1 && !isWinningFaction)
            {
                EC_LOG_TRACE("    Reward %u: Winners only, player is on losing faction - SKIP",
                    reward.rewardId);
                continue;
            }
            if (reward.rewardCondition == 2 && isWinningFaction)
            {
                EC_LOG_TRACE("    Reward %u: Losers only, player is on winning faction - SKIP",
                    reward.rewardId);
                continue;
            }

            // Check kill requirement
            if (participant.kills < reward.minKills)
            {
                EC_LOG_TRACE("    Reward %u: Needs %u kills, has %u - SKIP",
                    reward.rewardId, reward.minKills, participant.kills);
                continue;
            }

            // Calculate participation percentage
            float participation = event.totalKills > 0
                ? static_cast<float>(participant.kills) / event.totalKills
                : 0.0f;

            if (participation < reward.participation)
            {
                EC_LOG_TRACE("    Reward %u: Needs %.1f%% participation, has %.1f%% - SKIP",
                    reward.rewardId, reward.participation * 100.0f, participation * 100.0f);
                continue;
            }

            EC_LOG_DEBUG("    Granting reward %u (type: %u, value: %u)",
                reward.rewardId, static_cast<uint8>(reward.type), reward.value);

            // Grant reward
            switch (reward.type)
            {
                case RewardType::GOLD:
                {
                    player->ModifyMoney(reward.value);
                    totalGoldGiven += reward.value;
                    uint32 goldAmount = reward.value / 10000;
                    ChatHandler(player->GetSession()).PSendSysMessage(
                        "|cff00ff00[EC World Events]|r You received %u gold for participating in %s!",
                        goldAmount, def->name.c_str());
                    EC_LOG_INFO("  Gave %u gold to %s", goldAmount, player->GetName().c_str());
                    rewardsGiven++;
                    break;
                }

                case RewardType::HONOR:
                {
                    player->ModifyHonorPoints(reward.value);
                    totalHonorGiven += reward.value;
                    ChatHandler(player->GetSession()).PSendSysMessage(
                        "|cff00ff00[EC World Events]|r You received %u honor for participating in %s!",
                        reward.value, def->name.c_str());
                    EC_LOG_INFO("  Gave %u honor to %s", reward.value, player->GetName().c_str());
                    rewardsGiven++;
                    break;
                }

                case RewardType::ITEM:
                {
                    uint32 itemId = reward.value;
                    uint32 itemCount = reward.itemCount > 0 ? reward.itemCount : 1;

                    // Check if item exists in database
                    ItemTemplate const* itemTemplate = sObjectMgr->GetItemTemplate(itemId);
                    if (!itemTemplate)
                    {
                        EC_LOG_ERROR("  Item %u does not exist!", itemId);
                        break;
                    }

                    // Add item to player's inventory
                    ItemPosCountVec dest;
                    InventoryResult result = player->CanStoreNewItem(NULL_BAG, NULL_SLOT, dest, itemId, itemCount);

                    if (result == EQUIP_ERR_OK)
                    {
                        Item* item = player->StoreNewItem(dest, itemId, true);
                        if (item)
                        {
                            player->SendNewItem(item, itemCount, true, false);
                            lastItemGiven = itemId;
                            ChatHandler(player->GetSession()).PSendSysMessage(
                                "|cff00ff00[EC World Events]|r You received %s x%u for participating in %s!",
                                itemTemplate->Name1.c_str(), itemCount, def->name.c_str());
                            EC_LOG_INFO("  Gave item %u (%s) x%u to %s",
                                itemId, itemTemplate->Name1.c_str(), itemCount, player->GetName().c_str());
                            rewardsGiven++;
                        }
                        else
                        {
                            EC_LOG_ERROR("  Failed to create item %u for %s", itemId, player->GetName().c_str());
                        }
                    }
                    else
                    {
                        // Try to send via mail if inventory is full
                        EC_LOG_DEBUG("  Inventory full for %s, sending item %u via mail", player->GetName().c_str(), itemId);

                        // Create the item
                        Item* mailItem = Item::CreateItem(itemId, itemCount);
                        if (mailItem)
                        {
                            CharacterDatabaseTransaction trans = CharacterDatabase.BeginTransaction();
                            mailItem->SaveToDB(trans);

                            MailSender sender(MAIL_NORMAL, 0, MAIL_STATIONERY_GM);
                            MailDraft draft("EC World Event Reward",
                                Acore::StringFormat("Thank you for participating in %s! Here is your reward.", def->name.c_str()));
                            draft.AddItem(mailItem);
                            draft.SendMailTo(trans, MailReceiver(player, player->GetGUID().GetCounter()), sender);

                            CharacterDatabase.CommitTransaction(trans);

                            ChatHandler(player->GetSession()).PSendSysMessage(
                                "|cff00ff00[EC World Events]|r Your reward (%s) was sent to your mailbox!",
                                itemTemplate->Name1.c_str());
                            lastItemGiven = itemId;
                            EC_LOG_INFO("  Mailed item %u (%s) x%u to %s",
                                itemId, itemTemplate->Name1.c_str(), itemCount, player->GetName().c_str());
                            rewardsGiven++;
                        }
                        else
                        {
                            EC_LOG_ERROR("  Failed to create mail item %u for %s", itemId, player->GetName().c_str());
                        }
                    }
                    break;
                }

                case RewardType::TITLE:
                {
                    CharTitlesEntry const* titleEntry = sCharTitlesStore.LookupEntry(reward.value);
                    if (titleEntry)
                    {
                        player->SetTitle(titleEntry);
                        ChatHandler(player->GetSession()).PSendSysMessage(
                            "|cff00ff00[EC World Events]|r You earned the title '%s' for participating in %s!",
                            titleEntry->nameMale[0], def->name.c_str());
                        EC_LOG_INFO("  Granted title %u to %s", reward.value, player->GetName().c_str());
                        rewardsGiven++;
                    }
                    else
                    {
                        EC_LOG_ERROR("  Title %u does not exist!", reward.value);
                    }
                    break;
                }

                default:
                    EC_LOG_WARN("  Unknown reward type: %u", static_cast<uint8>(reward.type));
                    break;
            }
        }

        participant.rewarded = true;
        SaveParticipant(event.instanceId, participant);

        // Emit participant rewarded to bridge
        sWorldEventBridge->EmitParticipantRewarded(
            event.eventId,
            event.instanceId,
            player,
            totalGoldGiven,
            totalHonorGiven,
            lastItemGiven
        );
    }

    EC_LOG_INFO("Reward distribution complete: %u rewards given to %zu participants",
        rewardsGiven, event.participants.size());
}

// =============================================================================
// Broadcasting
// =============================================================================

void WorldEventManager::BroadcastEventMessage(const std::string& message, EventFaction targetFaction)
{
    EC_LOG_DEBUG("Broadcasting message to %s: %s",
        EventFactionToString(targetFaction).c_str(), message.c_str());

    if (targetFaction == EventFaction::NEUTRAL)
    {
        // Broadcast to everyone
        sWorldSessionMgr->SendServerMessage(SERVER_MSG_STRING, message);
    }
    else
    {
        // Filter by faction: only send to matching players
        TeamId targetTeam = (targetFaction == EventFaction::ALLIANCE) ? TEAM_ALLIANCE : TEAM_HORDE;
        for (auto const& [accountId, session] : sWorldSessionMgr->GetAllSessions())
        {
            Player* player = session->GetPlayer();
            if (player && player->IsInWorld() && player->GetTeamId() == targetTeam)
            {
                ChatHandler(session).SendSysMessage(message.c_str());
            }
        }
    }
}

// =============================================================================
// Database Logging
// =============================================================================

void WorldEventManager::LogEventToDatabase(const ActiveEvent& event)
{
    // Check if database logging is enabled
    if (!m_logToDatabase)
    {
        EC_LOG_DEBUG("LogEventToDatabase: Skipping - LogToDatabase is disabled");
        return;
    }

    auto startTimeT = std::chrono::system_clock::to_time_t(event.startTime);
    auto endTimeT = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());

    EC_LOG_DEBUG("Logging event %u to database...", event.instanceId);
    EC_LOG_TRACE("  Event ID: %u, State: %s", event.eventId, EventStateToString(event.state).c_str());
    EC_LOG_TRACE("  Start: %lld, End: %lld", startTimeT, endTimeT);
    EC_LOG_TRACE("  Kills: %u, Escalation: %u, Participants: %zu",
        event.totalKills, event.escalationLevel, event.participants.size());

    CharacterDatabase.Execute(
        "INSERT INTO ec_world_event_logs (event_id, instance_id, state, start_time, end_time, "
        "total_kills, escalation_level, participant_count, outcome) VALUES ({}, {}, {}, FROM_UNIXTIME({}), "
        "FROM_UNIXTIME({}), {}, {}, {}, {})",
        event.eventId,
        event.instanceId,
        static_cast<uint8>(event.state),
        startTimeT,
        endTimeT,
        event.totalKills,
        event.escalationLevel,
        event.participants.size(),
        static_cast<uint8>(event.outcome)
    );

    EC_LOG_DEBUG("Event logged to ec_world_event_logs");
}

void WorldEventManager::SaveParticipant(uint32 instanceId, const EventParticipant& participant)
{
    auto joinTimeT = std::chrono::system_clock::to_time_t(participant.joinTime);

    EC_LOG_TRACE("Saving participant %s (GUID: %u) for instance %u",
        participant.playerName.c_str(), participant.playerGuid.GetCounter(), instanceId);

    // Escape player name to prevent SQL injection
    std::string escapedName = participant.playerName;
    CharacterDatabase.EscapeString(escapedName);

    CharacterDatabase.Execute(
        "INSERT INTO ec_world_event_participants (instance_id, player_guid, player_name, kills, "
        "deaths, damage_dealt, healing_done, join_time, rewarded) VALUES ({}, {}, '{}', {}, {}, {}, "
        "{}, FROM_UNIXTIME({}), {}) ON DUPLICATE KEY UPDATE kills = VALUES(kills), "
        "deaths = VALUES(deaths), damage_dealt = VALUES(damage_dealt), "
        "healing_done = VALUES(healing_done), rewarded = VALUES(rewarded)",
        instanceId,
        participant.playerGuid.GetCounter(),
        escapedName,
        participant.kills,
        participant.deaths,
        participant.damageDealt,
        participant.healingDone,
        joinTimeT,
        participant.rewarded ? 1 : 0
    );
}

// =============================================================================
// Player Event Hooks
// =============================================================================

