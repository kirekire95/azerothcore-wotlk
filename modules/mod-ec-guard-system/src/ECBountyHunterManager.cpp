/*
 * EC Guard System - Bounty Hunter Manager Implementation
 */

#include "ECBountyHunterManager.h"
#include "ECGuardSystemManager.h"
#include "ECGuardSystemBridge.h"
#include "Config.h"
#include "DatabaseEnv.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "Creature.h"
#include "CreatureAI.h"
#include "Map.h"
#include "MapMgr.h"
#include "World.h"
#include "WorldSession.h"
#include "WorldSessionMgr.h"
#include "Chat.h"
#include "ChannelMgr.h"
#include "Channel.h"
#include "DBCStores.h"
#include <sstream>
#include <set>
#include <optional>
#include <algorithm>
#include <unordered_map>
#include <cmath>
#include <ctime>
#include <limits>

namespace ECGuardSystem
{
namespace
{
    uint64 ToUnixSeconds(std::chrono::system_clock::time_point value)
    {
        if (value == std::chrono::system_clock::time_point{})
            return 0;

        std::time_t const raw = std::chrono::system_clock::to_time_t(value);
        return raw > 0 ? static_cast<uint64>(raw) : 0;
    }

    std::chrono::system_clock::time_point FromUnixSeconds(uint64 value)
    {
        return std::chrono::system_clock::from_time_t(static_cast<std::time_t>(value));
    }

    std::string SqlTimestampOr(uint64 value, char const* fallback)
    {
        if (value == 0)
            return fallback;

        return "FROM_UNIXTIME(" + std::to_string(value) + ")";
    }
}

ECBountyHunterManager* ECBountyHunterManager::instance()
{
    static ECBountyHunterManager _instance;
    return &_instance;
}

void ECBountyHunterManager::LoadFromConfig()
{
    m_enabled = sConfigMgr->GetOption<bool>("ECGuard.BountyHunterEnabled", true);

    // Minimum wanted levels
    uint32 minForBounty = sConfigMgr->GetOption<uint32>("ECGuard.BountyMinWantedLevel", 3);
    uint32 minForHunter = sConfigMgr->GetOption<uint32>("ECGuard.HunterMinWantedLevel", 4);
    m_minWantedForBounty = static_cast<WantedStars>(std::min(minForBounty, 5u));
    m_minWantedForHunter = static_cast<WantedStars>(std::min(minForHunter, 5u));

    // Timing
    m_hunterSpawnDelayMs = sConfigMgr->GetOption<uint32>("ECGuard.HunterSpawnDelayMs", 300000);
    m_hunterRespawnDelayMs = sConfigMgr->GetOption<uint32>("ECGuard.HunterRespawnDelayMs", 180000);
    m_bountyDurationMs = sConfigMgr->GetOption<uint32>("ECGuard.BountyDurationMs", 3600000);
    m_hunterDespawnTimeMs = sConfigMgr->GetOption<uint32>("ECGuard.HunterDespawnTimeMs", 600000);
    m_maxHuntersPerTarget = sConfigMgr->GetOption<uint32>("ECGuard.MaxHuntersPerTarget", 2);

    // Rewards (in copper)
    m_rewardConfig.oneStar = sConfigMgr->GetOption<uint32>("ECGuard.BountyReward1Star", 500000);
    m_rewardConfig.twoStar = sConfigMgr->GetOption<uint32>("ECGuard.BountyReward2Star", 1500000);
    m_rewardConfig.threeStar = sConfigMgr->GetOption<uint32>("ECGuard.BountyReward3Star", 5000000);
    m_rewardConfig.fourStar = sConfigMgr->GetOption<uint32>("ECGuard.BountyReward4Star", 15000000);
    m_rewardConfig.fiveStar = sConfigMgr->GetOption<uint32>("ECGuard.BountyReward5Star", 50000000);

    // Hunter creature entries (comma-separated list)
    std::string hunterEntries = sConfigMgr->GetOption<std::string>("ECGuard.BountyHunterEntries", "");
    m_hunterEntries.clear();
    if (!hunterEntries.empty())
    {
        std::stringstream ss(hunterEntries);
        std::string item;
        while (std::getline(ss, item, ','))
        {
            // Skip empty entries (handles "100,,200" gracefully)
            if (item.empty())
                continue;

            try
            {
                uint32 entry = std::stoul(item);
                if (entry > 0)
                    m_hunterEntries.push_back(entry);
            }
            catch (const std::exception& e)
            {
                ECG_LOG_ERROR("Invalid hunter entry in config: '%s' - %s", item.c_str(), e.what());
            }
        }
    }
    m_eliteHunterEntry = sConfigMgr->GetOption<uint32>("ECGuard.EliteBountyHunterEntry", 0);

    // Mercenary enhancements
    m_crossZoneTracking = sConfigMgr->GetOption<bool>("ECGuard.HunterCrossZoneTracking", false);
    m_statMultiplierPerStar = sConfigMgr->GetOption<float>("ECGuard.HunterStatMultiplierPerStar", 0.25f);
    m_localDefenceAnnounce = sConfigMgr->GetOption<bool>("ECGuard.HunterLocalDefenceAnnounce", true);

    ECG_LOG_INFO("Bounty Hunter system loaded - Enabled: %s, MinBounty: %u stars, MinHunter: %u stars",
        m_enabled ? "true" : "false",
        static_cast<uint32>(m_minWantedForBounty),
        static_cast<uint32>(m_minWantedForHunter));
}

void ECBountyHunterManager::Initialize()
{
    if (!m_enabled)
    {
        ECG_LOG_INFO("Bounty Hunter system is disabled");
        return;
    }

    // IDs are generated in memory, so derive the next value from every row,
    // including history. Looking only at active rows allowed a later insert to
    // collide with (and, under REPLACE, delete) an old claimed/expired bounty.
    if (QueryResult maxIdResult = CharacterDatabase.Query(
        "SELECT COALESCE(MAX(id), 0) FROM ec_bounties"))
    {
        uint32 const maxId = maxIdResult->Fetch()[0].Get<uint32>();
        if (maxId == std::numeric_limits<uint32>::max())
        {
            ECG_LOG_ERROR("Bounty table exhausted its uint32 primary-key space");
            return;
        }
        m_nextBountyId = maxId + 1;
    }
    else
    {
        ECG_LOG_ERROR("Cannot determine next bounty id; initialization aborted to prevent primary-key collisions");
        return;
    }

    {
        std::lock_guard<std::mutex> lock(m_bountyMutex);
        m_bounties.clear();
    }

    // Load timestamps as Unix seconds rather than depending on SELECT * column
    // positions or leaving chrono values at their epoch defaults.
    QueryResult result = CharacterDatabase.Query(
        "SELECT id, target_guid, target_name, wanted_level, reward_gold, "
        "UNIX_TIMESTAMP(posted_time), UNIX_TIMESTAMP(expires_time), "
        "last_known_zone, claimed_by_guid, UNIX_TIMESTAMP(claimed_time) "
        "FROM ec_bounties WHERE status = 0 ORDER BY id DESC");
    if (result)
    {
        uint32 loadedCount = 0;
        uint32 normalizedExpiryCount = 0;
        uint32 expiredAtLoadCount = 0;
        uint32 invalidCount = 0;
        uint32 duplicateCount = 0;
        std::vector<Bounty> normalizedBounties;
        do
        {
            Field* fields = result->Fetch();
            uint32 const targetCounter = fields[1].Get<uint32>();
            uint64 const postedSeconds = fields[5].IsNull() ? 0 : fields[5].Get<uint64>();
            uint64 const expiresSeconds = fields[6].IsNull() ? 0 : fields[6].Get<uint64>();

            if (targetCounter == 0 || postedSeconds == 0)
            {
                invalidCount++;
                ECG_LOG_ERROR("Skipping active bounty id=%u with invalid target/post time (target=%u posted=%llu); repair migration required",
                    fields[0].Get<uint32>(), targetCounter,
                    static_cast<unsigned long long>(postedSeconds));
                continue;
            }

            Bounty bounty;
            bounty.bountyId = fields[0].Get<uint32>();
            bounty.targetGuid = ObjectGuid::Create<HighGuid::Player>(targetCounter);
            bounty.targetName = fields[2].Get<std::string>();
            bounty.wantedLevel = static_cast<WantedStars>(fields[3].Get<uint8>());
            bounty.rewardGold = fields[4].Get<uint32>();
            bounty.postedTime = FromUnixSeconds(postedSeconds);
            if (expiresSeconds > 0)
                bounty.expiresTime = FromUnixSeconds(expiresSeconds);
            else
            {
                // Older writes omitted expires_time. Reconstruct the intended
                // deadline from the original posting time, never from startup.
                bounty.expiresTime = bounty.postedTime +
                    std::chrono::milliseconds(m_bountyDurationMs);
                normalizedExpiryCount++;
            }
            bounty.status = BountyStatus::ACTIVE;
            bounty.lastKnownZoneId = fields[7].Get<uint32>();
            bounty.lastKnownZoneName = GetZoneName(bounty.lastKnownZoneId);
            if (!fields[8].IsNull() && fields[8].Get<uint32>() != 0)
                bounty.claimedByGuid = ObjectGuid::Create<HighGuid::Player>(fields[8].Get<uint32>());
            if (!fields[9].IsNull() && fields[9].Get<uint64>() != 0)
                bounty.claimedTime = FromUnixSeconds(fields[9].Get<uint64>());

            // Do not expose an already-expired status-0 row through
            // HasActiveBounty during the first update interval. Persist the
            // terminal state immediately so login repair sees a clean map.
            if (std::chrono::system_clock::now() >= bounty.expiresTime)
            {
                bounty.status = BountyStatus::EXPIRED;
                LogBountyToDatabase(bounty);
                expiredAtLoadCount++;
                continue;
            }

            bool inserted = false;
            {
                std::lock_guard<std::mutex> lock(m_bountyMutex);
                inserted = m_bounties.emplace(bounty.targetGuid, bounty).second;
            }
            if (!inserted)
            {
                // ORDER BY id DESC makes this the older duplicate. Keep only
                // the newest row in memory; the repair migration archives and
                // expires older active duplicates in the database.
                duplicateCount++;
                continue;
            }

            if (expiresSeconds == 0)
                normalizedBounties.push_back(bounty);
            loadedCount++;
        } while (result->NextRow());

        // Self-heal omitted expiry timestamps with a non-destructive upsert.
        // Rows whose reconstructed deadline is already past are marked expired
        // by the normal Update path on its next tick.
        for (Bounty const& bounty : normalizedBounties)
            LogBountyToDatabase(bounty);

        ECG_LOG_INFO("Loaded %u active bounties (normalized expiry=%u, expired at load=%u, invalid=%u, older duplicates skipped=%u, next id=%u)",
            loadedCount, normalizedExpiryCount, expiredAtLoadCount, invalidCount, duplicateCount,
            m_nextBountyId);
    }

    ECG_LOG_INFO("Bounty Hunter system initialized");
}

void ECBountyHunterManager::Update(uint32 diff)
{
    if (!m_enabled)
        return;

    m_updateTimer += diff;
    if (m_updateTimer < m_updateIntervalMs)
        return;

    m_updateTimer = 0;

    auto now = std::chrono::system_clock::now();

    // Snapshot active targets under the bounty lock. Wanted state belongs to a
    // different manager, so reconcile it only after releasing this lock.
    // This prevents stale database rows from keeping phantom bounties (and
    // their hunters) alive after the authoritative wanted state is removed.
    std::vector<ObjectGuid> activeTargets;
    std::vector<ObjectGuid> expiredTargets;
    {
        std::lock_guard<std::mutex> lock(m_bountyMutex);
        for (auto const& pair : m_bounties)
        {
            Bounty const& bounty = pair.second;
            if (bounty.status != BountyStatus::ACTIVE)
                continue;

            activeTargets.push_back(bounty.targetGuid);
            if (now >= bounty.expiresTime)
                expiredTargets.push_back(bounty.targetGuid);
        }
    }

    // ClearBounty persists the terminal status, erases the in-memory entry,
    // cancels delayed spawns, and despawns hunters outside the bounty lock.
    for (const ObjectGuid& targetGuid : expiredTargets)
    {
        ECG_LOG_INFO("Bounty on %s has expired", targetGuid.ToString().c_str());
        ClearBounty(targetGuid, BountyStatus::EXPIRED);
    }

    // Offline human wanted states are loaded into memory on login, so a
    // missing in-memory state is not, by itself, proof that an offline bounty
    // is stale. Once per minute, take one authoritative DB snapshot for the
    // missing/offline case. Online state remains authoritative immediately.
    static std::chrono::steady_clock::time_point nextReconciliation;
    auto const steadyNow = std::chrono::steady_clock::now();
    if (steadyNow >= nextReconciliation)
    {
        nextReconciliation = steadyNow + std::chrono::minutes(1);

        std::unordered_map<uint32, WantedStars> persistedWanted;
        QueryResult persistedResult = CharacterDatabase.Query(
            "SELECT bounty.target_guid, COALESCE(wanted.stars, 0) "
            "FROM ec_bounties AS bounty "
            "LEFT JOIN ec_guard_wanted_state AS wanted ON wanted.guid = bounty.target_guid "
            "WHERE bounty.status = 0");

        if (!persistedResult && !activeTargets.empty())
        {
            ECG_LOG_WARN("Skipping offline bounty reconciliation: active-bounty DB snapshot unavailable");
        }
        else
        {
            if (persistedResult)
            {
                do
                {
                    Field* fields = persistedResult->Fetch();
                    persistedWanted[fields[0].Get<uint32>()] =
                        static_cast<WantedStars>(fields[1].Get<uint8>());
                }
                while (persistedResult->NextRow());
            }

            for (const ObjectGuid& targetGuid : activeTargets)
            {
                if (std::find(expiredTargets.begin(), expiredTargets.end(), targetGuid) != expiredTargets.end())
                    continue;

                std::optional<WantedState> wanted = sECGuardSystemMgr->GetWantedStateCopy(targetGuid);
                bool stale = wanted && (!wanted->IsWanted() || wanted->stars < m_minWantedForBounty);

                if (!wanted)
                {
                    Player* onlineTarget = ObjectAccessor::FindPlayer(targetGuid);
                    if (onlineTarget && onlineTarget->IsInWorld())
                    {
                        stale = true;
                    }
                    else
                    {
                        auto persistedIt = persistedWanted.find(targetGuid.GetCounter());
                        stale = persistedIt != persistedWanted.end() &&
                            persistedIt->second < m_minWantedForBounty;
                    }
                }

                if (stale)
                {
                    ECG_LOG_INFO("Expiring stale bounty on %s: no matching wanted state at the bounty threshold",
                        targetGuid.ToString().c_str());
                    ClearBounty(targetGuid, BountyStatus::EXPIRED);
                }
            }
        }
    }

    // Process any pending hunter spawns
    ProcessPendingSpawns();

    // Update hunter tracking
    m_trackingTimer += m_updateIntervalMs;
    if (m_trackingTimer >= m_trackingIntervalMs)
    {
        m_trackingTimer = 0;
        UpdateHunterTracking(m_trackingIntervalMs);
    }
}

void ECBountyHunterManager::OnWantedLevelChanged(Player* player, WantedStars oldLevel, WantedStars newLevel)
{
    if (!m_enabled || !player)
        return;

    ObjectGuid playerGuid = player->GetGUID();

    // Check if we should post a bounty
    if (newLevel >= m_minWantedForBounty && oldLevel < m_minWantedForBounty)
    {
        if (!HasActiveBounty(playerGuid))
        {
            PostBounty(player);
        }
    }

    // Check if we should spawn a bounty hunter
    if (newLevel >= m_minWantedForHunter && oldLevel < m_minWantedForHunter)
    {
        // Schedule hunter spawn after delay
        ECG_LOG_INFO("Player %s reached %u stars - bounty hunter incoming in %u seconds",
            player->GetName().c_str(),
            static_cast<uint32>(newLevel),
            m_hunterSpawnDelayMs / 1000);

        ScheduleHunterSpawn(playerGuid, false);
    }

    // Update and persist an existing bounty. The old path changed only the
    // in-memory reward/zone, so a restart restored stale values.
    std::optional<Bounty> bountyToPersist;
    {
        std::lock_guard<std::mutex> lock(m_bountyMutex);
        auto it = m_bounties.find(playerGuid);
        if (newLevel >= m_minWantedForBounty &&
            it != m_bounties.end() && it->second.status == BountyStatus::ACTIVE)
        {
            WantedStars oldBountyLevel = it->second.wantedLevel;
            uint32 const oldReward = it->second.rewardGold;
            uint32 const oldZone = it->second.lastKnownZoneId;
            it->second.wantedLevel = newLevel;
            it->second.rewardGold = GetRewardForStars(newLevel);
            it->second.lastKnownZoneId = player->GetZoneId();
            it->second.lastKnownZoneName = GetZoneName(it->second.lastKnownZoneId);

            if (oldBountyLevel != it->second.wantedLevel ||
                oldReward != it->second.rewardGold ||
                oldZone != it->second.lastKnownZoneId)
            {
                bountyToPersist = it->second;
            }

            if (newLevel > oldBountyLevel)
            {
                ECG_LOG_INFO("Bounty on %s increased: %u -> %u stars, new reward: %u gold",
                    player->GetName().c_str(),
                    static_cast<uint32>(oldBountyLevel),
                    static_cast<uint32>(newLevel),
                    it->second.rewardGold / 10000);
            }
        }
    }

    if (bountyToPersist)
        LogBountyToDatabase(*bountyToPersist);

    // If wanted level dropped below minimum, expire the bounty
    if (newLevel < m_minWantedForBounty && oldLevel >= m_minWantedForBounty)
    {
        ClearBounty(playerGuid, BountyStatus::EXPIRED);
    }
}

void ECBountyHunterManager::PostBounty(Player* target)
{
    if (!target)
        return;

    WantedStars stars = sECGuardSystemMgr->GetPlayerWantedStars(target->GetGUID());
    if (stars < m_minWantedForBounty)
        return;

    Bounty bounty;
    bounty.targetGuid = target->GetGUID();
    bounty.targetName = target->GetName();
    bounty.wantedLevel = stars;
    bounty.rewardGold = GetRewardForStars(stars);
    bounty.postedTime = std::chrono::system_clock::now();
    bounty.expiresTime = bounty.postedTime + std::chrono::milliseconds(m_bountyDurationMs);
    bounty.status = BountyStatus::ACTIVE;
    bounty.lastKnownZoneId = target->GetZoneId();
    bounty.lastKnownZoneName = GetZoneName(bounty.lastKnownZoneId);

    {
        std::lock_guard<std::mutex> lock(m_bountyMutex);
        auto const existing = m_bounties.find(target->GetGUID());
        if (existing != m_bounties.end() && existing->second.status == BountyStatus::ACTIVE)
            return;

        bounty.bountyId = m_nextBountyId++;
        m_bounties[target->GetGUID()] = bounty;
    }

    LogBountyToDatabase(bounty);
    BroadcastBountyPosted(bounty);

    // Emit to bridge
    sGuardSystemBridge->EmitBountyPosted(
        bounty.bountyId,
        target->GetGUID().GetCounter(),
        bounty.targetName,
        static_cast<uint32>(bounty.wantedLevel),
        bounty.rewardGold,
        bounty.lastKnownZoneId,
        bounty.lastKnownZoneName
    );

    ECG_LOG_INFO("Bounty posted on %s (%u stars) - Reward: %u gold",
        target->GetName().c_str(),
        static_cast<uint32>(stars),
        bounty.rewardGold / 10000);
}

void ECBountyHunterManager::ClearBounty(ObjectGuid targetGuid, BountyStatus reason)
{
    // Store data we need for external calls
    uint32 bountyId = 0;
    std::string targetName;
    bool found = false;

    {
        std::lock_guard<std::mutex> lock(m_bountyMutex);
        auto it = m_bounties.find(targetGuid);
        if (it != m_bounties.end())
        {
            found = true;
            bountyId = it->second.bountyId;
            targetName = it->second.targetName;

            it->second.status = reason;
            LogBountyToDatabase(it->second);

            ECG_LOG_INFO("Bounty on %s cleared (reason: %u)",
                targetName.c_str(),
                static_cast<uint32>(reason));

            // Erase from map to prevent memory leak
            m_bounties.erase(it);
        }
    }

    // Cancel delayed spawns even if the persisted bounty row was missing. A
    // cleared wanted level must be a terminal condition for every hunter path.
    {
        std::lock_guard<std::mutex> lock(m_pendingMutex);
        m_pendingSpawns.erase(
            std::remove_if(m_pendingSpawns.begin(), m_pendingSpawns.end(),
                [targetGuid](PendingHunterSpawn const& pending)
                {
                    return pending.targetGuid == targetGuid;
                }),
            m_pendingSpawns.end());
    }

    // External calls outside bounty/pending locks to avoid potential deadlock.
    DespawnAllHuntersForTarget(targetGuid);

    if (found)
    {
        sGuardSystemBridge->EmitBountyCancelled(
            bountyId,
            targetGuid.GetCounter(),
            targetName,
            reason == BountyStatus::EXPIRED ? "expired" :
            reason == BountyStatus::CANCELLED ? "cancelled" : "claimed"
        );
    }
}

void ECBountyHunterManager::ClaimBounty(Player* claimer, Player* target)
{
    if (!claimer || !target)
        return;

    ObjectGuid targetGuid = target->GetGUID();
    ObjectGuid claimerGuid = claimer->GetGUID();
    uint32 bountyId = 0;
    std::string targetName;
    uint32 rewardGold = 0;

    // Update bounty status under lock
    {
        std::lock_guard<std::mutex> lock(m_bountyMutex);
        auto it = m_bounties.find(targetGuid);
        if (it == m_bounties.end() || it->second.status != BountyStatus::ACTIVE)
            return;

        Bounty& bounty = it->second;
        bounty.status = BountyStatus::CLAIMED;
        bounty.claimedByGuid = claimerGuid;
        bounty.claimedTime = std::chrono::system_clock::now();

        // Copy data for use outside lock
        bountyId = bounty.bountyId;
        targetName = bounty.targetName;
        rewardGold = bounty.rewardGold;

        // Log to database (under lock since we need bounty data)
        LogBountyToDatabase(bounty);
    }

    // Award gold to claimer (outside lock)
    claimer->ModifyMoney(rewardGold);

    // Broadcast (outside lock)
    std::ostringstream ss;
    ss << "|cff00ff00[BOUNTY CLAIMED]|r |cffffd700" << claimer->GetName()
       << "|r has collected the bounty on |cffff0000" << targetName
       << "|r! Reward: |cffffd700" << (rewardGold / 10000) << " gold|r.";
    sWorldSessionMgr->SendServerMessage(SERVER_MSG_STRING, ss.str());

    // Emit to bridge (outside lock)
    sGuardSystemBridge->EmitBountyClaimed(
        bountyId,
        targetGuid.GetCounter(),
        targetName,
        claimerGuid.GetCounter(),
        claimer->GetName(),
        rewardGold
    );

    ECG_LOG_INFO("Bounty on %s claimed by %s - Reward: %u gold",
        target->GetName().c_str(),
        claimer->GetName().c_str(),
        rewardGold / 10000);

    // Despawn hunters (has its own lock)
    DespawnAllHuntersForTarget(targetGuid);
}

bool ECBountyHunterManager::HasActiveBounty(ObjectGuid targetGuid) const
{
    std::lock_guard<std::mutex> lock(m_bountyMutex);
    auto it = m_bounties.find(targetGuid);
    return it != m_bounties.end() && it->second.status == BountyStatus::ACTIVE;
}

std::optional<Bounty> ECBountyHunterManager::GetBounty(ObjectGuid targetGuid) const
{
    std::lock_guard<std::mutex> lock(m_bountyMutex);
    auto it = m_bounties.find(targetGuid);
    if (it != m_bounties.end())
        return it->second;  // Returns copy, safe after lock release
    return std::nullopt;
}

std::vector<Bounty> ECBountyHunterManager::GetAllActiveBounties() const
{
    std::vector<Bounty> result;
    std::lock_guard<std::mutex> lock(m_bountyMutex);
    for (const auto& pair : m_bounties)
    {
        if (pair.second.status == BountyStatus::ACTIVE)
            result.push_back(pair.second);
    }
    return result;
}

uint32 ECBountyHunterManager::GetRewardForStars(WantedStars stars) const
{
    switch (stars)
    {
        case WantedStars::ONE_STAR:   return m_rewardConfig.oneStar;
        case WantedStars::TWO_STAR:   return m_rewardConfig.twoStar;
        case WantedStars::THREE_STAR: return m_rewardConfig.threeStar;
        case WantedStars::FOUR_STAR:  return m_rewardConfig.fourStar;
        case WantedStars::FIVE_STAR:  return m_rewardConfig.fiveStar;
        default: return 0;
    }
}

void ECBountyHunterManager::ScheduleHunterSpawn(ObjectGuid targetGuid, bool isRespawn,
                                                 float hintX, float hintY, float hintZ)
{
    Player* target = ObjectAccessor::FindPlayer(targetGuid);
    std::optional<WantedState> wanted = sECGuardSystemMgr->GetWantedStateCopy(targetGuid);
    ZoneGuardConfig const* jurisdictionConfig = wanted
        ? sECGuardSystemMgr->GetExactZoneConfig(wanted->crimeZoneId, wanted->crimeFaction)
        : nullptr;
    if (!target || !target->IsInWorld() || !wanted || !wanted->IsWanted() ||
        wanted->crimeZoneId == 0 || wanted->crimeAreaId == 0 ||
        target->GetZoneId() != wanted->crimeZoneId ||
        target->GetAreaId() != wanted->crimeAreaId || !jurisdictionConfig ||
        (jurisdictionConfig->areaId != 0 &&
         jurisdictionConfig->areaId != wanted->crimeAreaId))
    {
        ECG_LOG_DEBUG("Not scheduling bounty hunter for %s outside its active crime jurisdiction",
            targetGuid.ToString().c_str());
        return;
    }

    auto scheduledTime = std::chrono::steady_clock::now() +
        std::chrono::milliseconds(isRespawn ? m_hunterRespawnDelayMs : m_hunterSpawnDelayMs);

    PendingHunterSpawn pending;
    pending.targetGuid = targetGuid;
    pending.scheduledTime = scheduledTime;
    pending.isRespawn = isRespawn;
    pending.crimeZoneId = wanted->crimeZoneId;
    pending.crimeAreaId = wanted->crimeAreaId;
    pending.crimeFaction = wanted->crimeFaction;
    pending.mapId = target->GetMapId();
    pending.instanceId = target->GetInstanceId();

    // Store pre-computed position for cross-zone respawns
    if (hintX != 0 || hintY != 0 || hintZ != 0)
    {
        pending.hasHintPos = true;
        pending.hintX = hintX;
        pending.hintY = hintY;
        pending.hintZ = hintZ;
    }

    {
        std::lock_guard<std::mutex> lock(m_pendingMutex);
        // Check if already pending for this target
        for (const auto& p : m_pendingSpawns)
        {
            if (p.targetGuid == targetGuid)
                return;  // Already scheduled
        }
        m_pendingSpawns.push_back(pending);
    }

    ECG_LOG_DEBUG("Scheduled bounty hunter spawn for target %u in %u ms",
        targetGuid.GetCounter(), isRespawn ? m_hunterRespawnDelayMs : m_hunterSpawnDelayMs);
}

void ECBountyHunterManager::ProcessPendingSpawns()
{
    auto now = std::chrono::steady_clock::now();
    std::vector<PendingHunterSpawn> toSpawn;

    {
        std::lock_guard<std::mutex> lock(m_pendingMutex);
        auto it = m_pendingSpawns.begin();
        while (it != m_pendingSpawns.end())
        {
            if (now >= it->scheduledTime)
            {
                toSpawn.push_back(*it);
                it = m_pendingSpawns.erase(it);
            }
            else
            {
                ++it;
            }
        }
    }

    // Spawn hunters outside the lock
    for (const auto& pending : toSpawn)
    {
        Player* target = ObjectAccessor::FindPlayer(pending.targetGuid);
        if (target && target->IsInWorld())
        {
            // Verify target still meets criteria
            std::optional<WantedState> wanted = sECGuardSystemMgr->GetWantedStateCopy(pending.targetGuid);
            if (wanted && wanted->IsWanted() && wanted->stars >= m_minWantedForHunter &&
                wanted->crimeZoneId == pending.crimeZoneId &&
                wanted->crimeAreaId == pending.crimeAreaId &&
                wanted->crimeFaction == pending.crimeFaction &&
                target->GetZoneId() == pending.crimeZoneId &&
                target->GetAreaId() == pending.crimeAreaId &&
                target->GetMapId() == pending.mapId &&
                target->GetInstanceId() == pending.instanceId)
            {
                SpawnBountyHunter(target, pending.hintX, pending.hintY, pending.hintZ);
            }
            else
            {
                ECG_LOG_DEBUG("Discarding stale bounty hunter spawn for %s after jurisdiction change",
                    pending.targetGuid.ToString().c_str());
            }
        }
    }
}

void ECBountyHunterManager::ApplyCCImmunity(Creature* hunter)
{
    if (!hunter)
        return;

    // Apply mechanic immunity flags - hunter can still be attacked but immune to CC
    // These are the standard mechanics for CC effects
    hunter->ApplySpellImmune(0, IMMUNITY_MECHANIC, MECHANIC_STUN, true);
    hunter->ApplySpellImmune(0, IMMUNITY_MECHANIC, MECHANIC_ROOT, true);
    hunter->ApplySpellImmune(0, IMMUNITY_MECHANIC, MECHANIC_FREEZE, true);
    hunter->ApplySpellImmune(0, IMMUNITY_MECHANIC, MECHANIC_POLYMORPH, true);
    hunter->ApplySpellImmune(0, IMMUNITY_MECHANIC, MECHANIC_FEAR, true);
    hunter->ApplySpellImmune(0, IMMUNITY_MECHANIC, MECHANIC_SLEEP, true);
    hunter->ApplySpellImmune(0, IMMUNITY_MECHANIC, MECHANIC_CHARM, true);
    hunter->ApplySpellImmune(0, IMMUNITY_MECHANIC, MECHANIC_BANISH, true);
    hunter->ApplySpellImmune(0, IMMUNITY_MECHANIC, MECHANIC_SAPPED, true);
    hunter->ApplySpellImmune(0, IMMUNITY_MECHANIC, MECHANIC_HORROR, true);

    ECG_LOG_DEBUG("Applied CC immunity to bounty hunter %s", hunter->GetName().c_str());
}

void ECBountyHunterManager::SpawnBountyHunter(Player* target, float hintX, float hintY, float hintZ)
{
    if (!target)
        return;

    // A hunter may only appear in the exact jurisdiction that issued the
    // wanted state. This also rejects delayed spawns after portals/teleports.
    std::optional<WantedState> wanted = sECGuardSystemMgr->GetWantedStateCopy(target->GetGUID());
    const ZoneGuardConfig* jurisdictionConfig = wanted
        ? sECGuardSystemMgr->GetExactZoneConfig(wanted->crimeZoneId, wanted->crimeFaction)
        : nullptr;
    if (!wanted || !wanted->IsWanted() || wanted->crimeZoneId == 0 ||
        wanted->crimeAreaId == 0 || target->GetZoneId() != wanted->crimeZoneId ||
        target->GetAreaId() != wanted->crimeAreaId || !jurisdictionConfig ||
        (jurisdictionConfig->areaId != 0 &&
         jurisdictionConfig->areaId != wanted->crimeAreaId))
    {
        ECG_LOG_DEBUG("Skipping bounty hunter spawn for %s outside a matching guard jurisdiction",
            target->GetName().c_str());
        return;
    }

    WantedStars stars = wanted->stars;

    // Non-neutral jurisdictions use only their exact local response templates.
    // Global configured entries are allowed only as neutral mercenaries.
    if (jurisdictionConfig->eliteEntries.empty() &&
        (wanted->crimeFaction != TEAM_NEUTRAL || m_hunterEntries.empty()) &&
        stars < WantedStars::FIVE_STAR)
        return;

    // Check if we've hit the max hunters for this target
    uint32 activeHunters = GetActiveHunterCountForTarget(target->GetGUID());
    if (activeHunters >= m_maxHuntersPerTarget)
        return;

    // Select hunter entry based on wanted level and target faction
    uint32 hunterEntry = 0;

    if (stars >= WantedStars::FIVE_STAR && activeHunters == 0)
    {
        if (!jurisdictionConfig->eliteEntries.empty())
        {
            hunterEntry = jurisdictionConfig->eliteEntries[
                urand(0, jurisdictionConfig->eliteEntries.size() - 1)];
        }
        else
        {
            // Five-star fallback assassins represent the offended local faction.
            if (wanted->crimeFaction == TEAM_ALLIANCE)
                hunterEntry = AssassinEntries::SHADOWLEAF_ASSASSIN;
            else if (wanted->crimeFaction == TEAM_HORDE)
                hunterEntry = AssassinEntries::BLOODFANG_ASSASSIN;
        }

        ECG_LOG_INFO("5-star bounty: Dispatching %s (entry %u) to hunt %s %s",
            (hunterEntry == AssassinEntries::BLOODFANG_ASSASSIN) ? "Bloodfang Assassin" : "Shadowleaf Assassin",
            hunterEntry, wanted->crimeFaction == TEAM_ALLIANCE ? "Alliance" : "Horde",
            target->GetName().c_str());
    }
    else if (!jurisdictionConfig->eliteEntries.empty() && stars >= WantedStars::FOUR_STAR)
    {
        hunterEntry = jurisdictionConfig->eliteEntries[
            urand(0, jurisdictionConfig->eliteEntries.size() - 1)];
    }
    else if (wanted->crimeFaction == TEAM_NEUTRAL &&
        m_eliteHunterEntry > 0 && stars >= WantedStars::FOUR_STAR)
    {
        hunterEntry = m_eliteHunterEntry;
    }
    else if (wanted->crimeFaction == TEAM_NEUTRAL && !m_hunterEntries.empty())
    {
        hunterEntry = m_hunterEntries[urand(0, m_hunterEntries.size() - 1)];
    }

    if (hunterEntry == 0)
        return;

    Map* map = target->GetMap();
    if (!map)
        return;

    // Try a still-valid tracking hint first, then bounded collision-safe
    // positions around the target. Every candidate must remain inside the
    // exact jurisdiction that issued the wanted state.
    uint32 invalidCandidates = 0;
    uint32 wrongJurisdictionCandidates = 0;
    uint32 summonFailures = 0;
    Creature* hunter = nullptr;
    Position spawnPosition;

    auto trySpawn = [&](Position const& candidate)
    {
        if (!candidate.IsPositionValid() ||
            std::fabs(candidate.GetPositionZ() - target->GetPositionZ()) > 30.0f)
        {
            ++invalidCandidates;
            return;
        }

        uint32 candidateZoneId = 0;
        uint32 candidateAreaId = 0;
        map->GetZoneAndAreaId(target->GetPhaseMask(), candidateZoneId, candidateAreaId,
            candidate.GetPositionX(), candidate.GetPositionY(), candidate.GetPositionZ());
        if (candidateZoneId != wanted->crimeZoneId ||
            candidateAreaId != wanted->crimeAreaId)
        {
            ++wrongJurisdictionCandidates;
            return;
        }

        hunter = target->SummonCreature(
            hunterEntry,
            candidate.GetPositionX(),
            candidate.GetPositionY(),
            candidate.GetPositionZ(),
            target->GetOrientation(),
            TEMPSUMMON_TIMED_OR_DEAD_DESPAWN,
            m_hunterDespawnTimeMs);
        if (hunter)
            spawnPosition = candidate;
        else
            ++summonFailures;
    };

    if (hintX != 0.0f || hintY != 0.0f || hintZ != 0.0f)
    {
        Position hintPosition;
        hintPosition.Relocate(hintX, hintY, hintZ, target->GetOrientation());
        trySpawn(hintPosition);
    }

    constexpr uint32 maxCollisionAttempts = 8;
    for (uint32 attempt = 0; attempt < maxCollisionAttempts && !hunter; ++attempt)
    {
        float angle = frand(0.0f, 2.0f * static_cast<float>(M_PI));
        float distance = 50.0f - 5.0f * static_cast<float>(attempt);
        trySpawn(target->GetFirstCollisionPosition(distance, angle));
    }

    if (!hunter)
    {
        ECG_LOG_WARN("Failed to summon bounty hunter %u for %s after bounded collision-safe placement (invalid=%u, jurisdiction=%u, summon=%u, map=%u, zone=%u, area=%u)",
            hunterEntry, target->GetName().c_str(), invalidCandidates,
            wrongJurisdictionCandidates, summonFailures, map->GetId(),
            target->GetZoneId(), target->GetAreaId());
        return;
    }

    float const x = spawnPosition.GetPositionX();
    float const y = spawnPosition.GetPositionY();
    float const z = spawnPosition.GetPositionZ();

    // Make hunter aggressive toward target
    hunter->SetFaction(14);  // Hostile to all

    // Apply stat buff based on target's wanted level (mercenary scaling)
    ApplyStatBuff(hunter, stars);

    // Apply CC immunity for initial engagement
    ApplyCCImmunity(hunter);

    // Start combat
    if (hunter->AI())
        hunter->AI()->AttackStart(target);

    // Apply HUNTED mark on the target (crosshairs debuff)
    if (!target->HasAura(GuardSpells::HUNTED_MARK))
    {
        target->AddAura(GuardSpells::HUNTED_MARK, target);
        ECG_LOG_DEBUG("Applied HUNTED_MARK aura to %s", target->GetName().c_str());
    }

    // Track the hunter
    BountyHunter bh;
    bh.hunterGuid = hunter->GetGUID();
    bh.targetGuid = target->GetGUID();
    bh.spawnTime = std::chrono::steady_clock::now();
    bh.lastTrackTime = bh.spawnTime;
    bh.ccImmunityEndTime = bh.spawnTime + std::chrono::milliseconds(m_ccImmunityDurationMs);
    bh.status = HunterStatus::HUNTING;
    bh.mapId = map->GetId();
    bh.instanceId = map->GetInstanceId();
    bh.spawnZoneId = wanted->crimeZoneId;
    bh.crimeAreaId = wanted->crimeAreaId;
    bh.crimeFaction = wanted->crimeFaction;
    bh.x = x;
    bh.y = y;
    bh.z = z;

    {
        std::lock_guard<std::mutex> lock(m_hunterMutex);
        m_hunters[hunter->GetGUID()] = bh;
    }

    // Announce arrival
    std::string hunterName = hunter->GetName();
    std::string targetName = target->GetName();
    std::string announcement = "|cffff0000[Bounty Hunter]|r " + hunterName +
        " has arrived to hunt " + targetName + "!";
    sWorldSessionMgr->SendServerMessage(SERVER_MSG_STRING, announcement);

    // Also broadcast in LocalDefence channel for the target's zone
    if (m_localDefenceAnnounce)
    {
        std::string ldMsg = "A bounty hunter has been dispatched to " +
            GetZoneName(target->GetZoneId()) + " to pursue the criminal " + targetName + "!";
        BroadcastLocalDefence(target, ldMsg);
    }

    // Emit to bridge
    sGuardSystemBridge->EmitBountyHunterSpawned(
        hunter->GetGUID().GetCounter(),
        target->GetGUID().GetCounter(),
        target->GetName(),
        hunterEntry,
        hunterName
    );

    ECG_LOG_INFO("Bounty hunter %s spawned to hunt %s",
        hunterName.c_str(), targetName.c_str());
}

void ECBountyHunterManager::OnBountyHunterDeath(Creature* hunter, Player* killer)
{
    if (!hunter)
        return;

    uint32 hunterGuidCounter = hunter->GetGUID().GetCounter();
    uint32 targetGuidCounter = 0;
    std::string killerName;
    std::string killerType;
    BountyHunter bhCopy;
    bool found = false;

    {
        std::lock_guard<std::mutex> lock(m_hunterMutex);
        auto it = m_hunters.find(hunter->GetGUID());
        if (it == m_hunters.end())
            return;

        found = true;
        BountyHunter& bh = it->second;
        bh.status = HunterStatus::WAS_KILLED;
        targetGuidCounter = bh.targetGuid.GetCounter();
        bhCopy = bh;

        // Erase from map now that we've copied the data (prevents memory leak)
        m_hunters.erase(it);

        // Check if killed by target
        if (killer && killer->GetGUID() == bhCopy.targetGuid)
        {
            killerName = killer->GetName();
            killerType = "target";
            ECG_LOG_INFO("Bounty hunter killed by target %s", killer->GetName().c_str());
        }
        else
        {
            killerName = killer ? killer->GetName() : "Unknown";
            killerType = "other";
        }
    }

    // External calls outside lock
    if (found)
    {
        LogHunterToDatabase(bhCopy);

        sGuardSystemBridge->EmitBountyHunterKilled(
            hunterGuidCounter,
            targetGuidCounter,
            killerName,
            killerType
        );

        // Remove HUNTED mark if no more active hunters for this target
        if (GetActiveHunterCountForTarget(bhCopy.targetGuid) == 0)
        {
            Player* target = ObjectAccessor::FindPlayer(bhCopy.targetGuid);
            if (target && target->HasAura(GuardSpells::HUNTED_MARK))
            {
                target->RemoveAura(GuardSpells::HUNTED_MARK);
                ECG_LOG_DEBUG("Removed HUNTED_MARK from %s (all hunters dead)", target->GetName().c_str());
            }
        }
    }
}

void ECBountyHunterManager::OnTargetKilledByHunter(Player* target, Creature* hunter)
{
    if (!target || !hunter)
        return;

    ObjectGuid targetGuid = target->GetGUID();
    bool wasHunterTarget = false;

    {
        std::lock_guard<std::mutex> lock(m_hunterMutex);
        auto it = m_hunters.find(hunter->GetGUID());
        if (it == m_hunters.end())
            return;

        // Verify this hunter was actually hunting this target
        if (it->second.targetGuid != targetGuid)
        {
            ECG_LOG_DEBUG("Bounty hunter killed player %s but was hunting different target",
                target->GetName().c_str());
            return;
        }

        it->second.status = HunterStatus::KILLED_TARGET;
        LogHunterToDatabase(it->second);
        wasHunterTarget = true;

        // Erase from map to prevent memory leak
        m_hunters.erase(it);
    }

    // Clear the bounty as "collected by hunter" (outside lock to avoid deadlock)
    if (wasHunterTarget)
    {
        ClearBounty(targetGuid, BountyStatus::CLAIMED);
        ECG_LOG_INFO("Bounty hunter killed target %s", target->GetName().c_str());
    }
}

void ECBountyHunterManager::UpdateHunterTracking(uint32 /*diff*/)
{
    auto now = std::chrono::steady_clock::now();
    std::vector<ObjectGuid> toRemove;

    std::lock_guard<std::mutex> lock(m_hunterMutex);
    for (auto& pair : m_hunters)
    {
        BountyHunter& bh = pair.second;
        if (bh.status != HunterStatus::HUNTING)
            continue;

        // Check despawn timer
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - bh.spawnTime).count();
        if (elapsed >= m_hunterDespawnTimeMs)
        {
            bh.status = HunterStatus::DESPAWNED;
            toRemove.push_back(bh.hunterGuid);
            continue;
        }

        // Update tracking - make hunter follow target
        Map* map = sMapMgr->FindMap(bh.mapId, bh.instanceId);
        Creature* hunter = map ? map->GetCreature(bh.hunterGuid) : nullptr;
        Player* target = ObjectAccessor::FindPlayer(bh.targetGuid);

        if (!hunter || !hunter->IsAlive())
        {
            toRemove.push_back(bh.hunterGuid);
            continue;
        }

        // Check CC immunity expiration
        if (bh.ccImmunityEndTime.time_since_epoch().count() > 0 && now >= bh.ccImmunityEndTime)
        {
            // Remove mechanic immunities
            hunter->ApplySpellImmune(0, IMMUNITY_MECHANIC, MECHANIC_STUN, false);
            hunter->ApplySpellImmune(0, IMMUNITY_MECHANIC, MECHANIC_ROOT, false);
            hunter->ApplySpellImmune(0, IMMUNITY_MECHANIC, MECHANIC_FREEZE, false);
            hunter->ApplySpellImmune(0, IMMUNITY_MECHANIC, MECHANIC_POLYMORPH, false);
            hunter->ApplySpellImmune(0, IMMUNITY_MECHANIC, MECHANIC_FEAR, false);
            hunter->ApplySpellImmune(0, IMMUNITY_MECHANIC, MECHANIC_SLEEP, false);
            hunter->ApplySpellImmune(0, IMMUNITY_MECHANIC, MECHANIC_CHARM, false);
            hunter->ApplySpellImmune(0, IMMUNITY_MECHANIC, MECHANIC_BANISH, false);
            hunter->ApplySpellImmune(0, IMMUNITY_MECHANIC, MECHANIC_SAPPED, false);
            hunter->ApplySpellImmune(0, IMMUNITY_MECHANIC, MECHANIC_HORROR, false);
            ECG_LOG_DEBUG("CC immunity expired for bounty hunter %s", hunter->GetName().c_str());

            // Clear the immunity time so we don't check again
            bh.ccImmunityEndTime = std::chrono::steady_clock::time_point{};
        }

        std::optional<WantedState> wanted = sECGuardSystemMgr->GetWantedStateCopy(bh.targetGuid);
        if (target && target->IsAlive() && target->IsInWorld() &&
            wanted && wanted->IsWanted() &&
            wanted->crimeZoneId == bh.spawnZoneId &&
            wanted->crimeAreaId == bh.crimeAreaId &&
            wanted->crimeFaction == bh.crimeFaction &&
            target->GetZoneId() == bh.spawnZoneId &&
            target->GetAreaId() == bh.crimeAreaId)
        {
            // Crossing a map or instance boundary terminates this local response.
            if (hunter->GetMapId() != target->GetMapId() ||
                bh.instanceId != target->GetInstanceId())
            {
                if (m_crossZoneTracking)
                {
                    ECG_LOG_WARN("HunterCrossZoneTracking requested for %s, but jurisdiction boundary prevents re-dispatch",
                        target->GetName().c_str());
                }
                ECG_LOG_DEBUG("Target %s crossed a map/instance boundary, despawning hunter",
                    target->GetName().c_str());
                bh.status = HunterStatus::DESPAWNED;
                hunter->DespawnOrUnsummon();
                toRemove.push_back(bh.hunterGuid);
                continue;
            }

            // Make sure hunter is pursuing
            if (!hunter->GetVictim() || hunter->GetVictim() != target)
            {
                hunter->AI()->AttackStart(target);
            }
        }
        else
        {
            // Target offline or dead
            ECG_LOG_DEBUG("Bounty target offline/dead, despawning hunter");
            bh.status = HunterStatus::DESPAWNED;
            hunter->DespawnOrUnsummon();
            toRemove.push_back(bh.hunterGuid);
        }
    }

    // Collect target GUIDs from hunters being removed (before erasing)
    std::set<ObjectGuid> affectedTargets;
    for (const ObjectGuid& guid : toRemove)
    {
        auto it = m_hunters.find(guid);
        if (it != m_hunters.end())
            affectedTargets.insert(it->second.targetGuid);
    }

    // Clean up finished hunters
    for (const ObjectGuid& guid : toRemove)
    {
        auto it = m_hunters.find(guid);
        if (it != m_hunters.end())
        {
            LogHunterToDatabase(it->second);
            m_hunters.erase(it);
        }
    }

    // Check HUNTED_MARK for affected targets (count remaining hunters while holding lock)
    for (const ObjectGuid& targetGuid : affectedTargets)
    {
        uint32 remainingHunters = 0;
        for (const auto& pair : m_hunters)
        {
            if (pair.second.targetGuid == targetGuid && pair.second.status == HunterStatus::HUNTING)
                remainingHunters++;
        }

        if (remainingHunters == 0)
        {
            Player* target = ObjectAccessor::FindPlayer(targetGuid);
            if (target && target->HasAura(GuardSpells::HUNTED_MARK))
            {
                target->RemoveAura(GuardSpells::HUNTED_MARK);
                ECG_LOG_DEBUG("UpdateHunterTracking: Removed HUNTED_MARK from %s (no more active hunters)",
                    target->GetName().c_str());
            }
        }
    }
}

void ECBountyHunterManager::DespawnAllHuntersForTarget(ObjectGuid targetGuid)
{
    std::lock_guard<std::mutex> lock(m_hunterMutex);

    // Collect hunters to remove (can't erase while iterating)
    std::vector<ObjectGuid> toErase;

    for (auto& pair : m_hunters)
    {
        if (pair.second.targetGuid == targetGuid && pair.second.status == HunterStatus::HUNTING)
        {
            pair.second.status = HunterStatus::DESPAWNED;

            Map* map = sMapMgr->FindMap(pair.second.mapId, pair.second.instanceId);
            Creature* hunter = map ? map->GetCreature(pair.second.hunterGuid) : nullptr;
            if (hunter)
                hunter->DespawnOrUnsummon();

            LogHunterToDatabase(pair.second);
            toErase.push_back(pair.first);
        }
    }

    // Erase processed entries to prevent memory leak
    for (const ObjectGuid& guid : toErase)
        m_hunters.erase(guid);

    // Remove HUNTED_MARK aura since all hunters are being despawned
    Player* target = ObjectAccessor::FindPlayer(targetGuid);
    if (target && target->HasAura(GuardSpells::HUNTED_MARK))
    {
        target->RemoveAura(GuardSpells::HUNTED_MARK);
        ECG_LOG_DEBUG("DespawnAllHunters: Removed HUNTED_MARK from %s", target->GetName().c_str());
    }
}

uint32 ECBountyHunterManager::GetActiveHunterCountForTarget(ObjectGuid targetGuid) const
{
    uint32 count = 0;
    std::lock_guard<std::mutex> lock(m_hunterMutex);

    for (const auto& pair : m_hunters)
    {
        if (pair.second.targetGuid == targetGuid && pair.second.status == HunterStatus::HUNTING)
            count++;
    }
    return count;
}

void ECBountyHunterManager::GMClearBounty(ObjectGuid targetGuid)
{
    ClearBounty(targetGuid, BountyStatus::CANCELLED);
}

void ECBountyHunterManager::GMListBounties(Player* gm)
{
    if (!gm)
        return;

    ChatHandler handler(gm->GetSession());
    handler.PSendSysMessage("|cff00ff00=== Active Bounties ===|r");

    auto bounties = GetAllActiveBounties();
    if (bounties.empty())
    {
        handler.PSendSysMessage("No active bounties.");
        return;
    }

    for (const Bounty& b : bounties)
    {
        handler.PSendSysMessage("  |cffffd700%s|r - %u stars - %ug reward - Zone: %s",
            b.targetName.c_str(),
            static_cast<uint32>(b.wantedLevel),
            b.rewardGold / 10000,
            b.lastKnownZoneName.c_str());
    }
}

void ECBountyHunterManager::LogBountyToDatabase(const Bounty& bounty)
{
    uint64 const postedSeconds = ToUnixSeconds(bounty.postedTime);
    uint64 const expiresSeconds = ToUnixSeconds(bounty.expiresTime);
    uint64 const claimedSeconds = ToUnixSeconds(bounty.claimedTime);

    std::string targetName = bounty.targetName;
    CharacterDatabase.EscapeString(targetName);

    // Render missing timestamps as SQL NULL/current time rather than ever
    // converting zero into an epoch value (or failing under strict TIMESTAMP
    // modes). ON DUPLICATE KEY UPDATE preserves row identity
    // and avoids REPLACE's implicit delete/insert cycle.
    std::string const postedSql = SqlTimestampOr(postedSeconds, "CURRENT_TIMESTAMP");
    std::string const expiresSql = SqlTimestampOr(expiresSeconds, "NULL");
    std::string const claimedSql = SqlTimestampOr(claimedSeconds, "NULL");
    std::string const claimedBySql = bounty.claimedByGuid.IsEmpty()
        ? "NULL" : std::to_string(bounty.claimedByGuid.GetCounter());

    CharacterDatabase.Execute(
        "INSERT INTO ec_bounties (id, target_guid, target_name, wanted_level, reward_gold, "
        "posted_time, expires_time, last_known_zone, claimed_by_guid, claimed_time, status) "
        "VALUES ({}, {}, '{}', {}, {}, {}, {}, {}, {}, {}, {}) "
        "ON DUPLICATE KEY UPDATE "
        "target_guid=VALUES(target_guid), target_name=VALUES(target_name), "
        "wanted_level=VALUES(wanted_level), reward_gold=VALUES(reward_gold), "
        "posted_time=VALUES(posted_time), expires_time=VALUES(expires_time), "
        "last_known_zone=VALUES(last_known_zone), claimed_by_guid=VALUES(claimed_by_guid), "
        "claimed_time=VALUES(claimed_time), status=VALUES(status)",
        bounty.bountyId,
        bounty.targetGuid.GetCounter(),
        targetName,
        static_cast<uint32>(bounty.wantedLevel),
        bounty.rewardGold,
        postedSql,
        expiresSql,
        bounty.lastKnownZoneId,
        claimedBySql,
        claimedSql,
        static_cast<uint32>(bounty.status)
    );
}

void ECBountyHunterManager::LogHunterToDatabase(const BountyHunter& hunter)
{
    auto spawnTime = std::chrono::steady_clock::now();  // Using current time as approximation
    auto spawnTimeT = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());

    CharacterDatabase.Execute(
        "INSERT INTO ec_bounty_hunters (hunter_guid, target_guid, spawn_time, status) "
        "VALUES ({}, {}, FROM_UNIXTIME({}), {})",
        hunter.hunterGuid.GetCounter(),
        hunter.targetGuid.GetCounter(),
        spawnTimeT,
        static_cast<uint8>(hunter.status)
    );
}

std::string ECBountyHunterManager::GetZoneName(uint32 zoneId) const
{
    AreaTableEntry const* area = sAreaTableStore.LookupEntry(zoneId);
    if (area)
        return area->area_name[0];  // English name
    return "Unknown";
}

void ECBountyHunterManager::BroadcastBountyPosted(const Bounty& bounty)
{
    std::ostringstream ss;
    ss << "|cffff6600[BOUNTY]|r A bounty of |cffffd700"
       << (bounty.rewardGold / 10000) << " gold|r has been posted on |cffff0000"
       << bounty.targetName << "|r (" << static_cast<uint32>(bounty.wantedLevel)
       << " stars)! Last seen in " << bounty.lastKnownZoneName << ".";

    sWorldSessionMgr->SendServerMessage(SERVER_MSG_STRING, ss.str());
}

void ECBountyHunterManager::BroadcastBountyClaimed(const Bounty& bounty, Player* claimer)
{
    std::ostringstream ss;
    ss << "|cff00ff00[BOUNTY CLAIMED]|r |cffffd700" << claimer->GetName()
       << "|r has collected the bounty on |cffff0000" << bounty.targetName
       << "|r! Reward: |cffffd700" << (bounty.rewardGold / 10000) << " gold|r.";

    sWorldSessionMgr->SendServerMessage(SERVER_MSG_STRING, ss.str());
}

void ECBountyHunterManager::ApplyStatBuff(Creature* hunter, WantedStars targetStars)
{
    if (!hunter || m_statMultiplierPerStar <= 0.0f)
        return;

    uint8 stars = static_cast<uint8>(targetStars);
    float multiplier = 1.0f + (m_statMultiplierPerStar * stars);

    // Scale health
    uint32 baseHealth = hunter->GetMaxHealth();
    uint32 newHealth = static_cast<uint32>(baseHealth * multiplier);
    hunter->SetMaxHealth(newHealth);
    hunter->SetHealth(newHealth);

    // Scale damage via melee damage modifier
    hunter->SetStatPctModifier(UNIT_MOD_DAMAGE_MAINHAND, BASE_PCT, multiplier);
    hunter->UpdateDamagePhysical(BASE_ATTACK);

    ECG_LOG_DEBUG("Bounty hunter %s buffed: %.0f%% HP (%u -> %u), %.0f%% damage for %u-star target",
        hunter->GetName().c_str(),
        (multiplier - 1.0f) * 100.0f,
        baseHealth, newHealth,
        (multiplier - 1.0f) * 100.0f,
        stars);
}

void ECBountyHunterManager::BroadcastLocalDefence(Player* target, const std::string& message)
{
    if (!target)
        return;

    TeamId faction = target->GetTeamId();
    if (ChannelMgr* channelMgr = ChannelMgr::forTeam(faction))
    {
        if (Channel* localDefense = channelMgr->GetChannel("LocalDefense", target))
        {
            WorldPacket data;
            ChatHandler::BuildChatPacket(data, CHAT_MSG_CHANNEL, LANG_UNIVERSAL,
                nullptr, nullptr, message, 0, "LocalDefense", DEFAULT_LOCALE);

            Map* map = target->GetMap();
            if (map)
            {
                uint32 zoneId = target->GetZoneId();
                map->DoForAllPlayers([&](Player* player)
                {
                    if (player->GetZoneId() == zoneId && player->GetTeamId() == faction)
                    {
                        player->SendDirectMessage(&data);
                    }
                });
            }
        }
    }
}

} // namespace ECGuardSystem
