/*
 * EC Guard System - Bounty Hunter Manager
 * Spawns bounty hunters to chase high-wanted players
 * Allows players to claim bounties on wanted criminals
 */

#ifndef EC_BOUNTY_HUNTER_MANAGER_H
#define EC_BOUNTY_HUNTER_MANAGER_H

#include "Common.h"
#include "ObjectGuid.h"
#include "ECWantedLevel.h"
#include <map>
#include <unordered_map>
#include <vector>
#include <mutex>
#include <chrono>
#include <optional>

class Player;
class Creature;

namespace ECGuardSystem
{
    // Bounty status
    enum class BountyStatus : uint8
    {
        ACTIVE    = 0,  // Bounty is active and claimable
        CLAIMED   = 1,  // Bounty was claimed by another player
        EXPIRED   = 2,  // Bounty expired (target cleared wanted level)
        CANCELLED = 3   // Bounty was cancelled by GM
    };

    // Bounty hunter NPC status
    enum class HunterStatus : uint8
    {
        HUNTING       = 0,  // Actively hunting target
        KILLED_TARGET = 1,  // Successfully killed the target
        WAS_KILLED    = 2,  // Hunter was killed by target
        DESPAWNED     = 3   // Hunter despawned (timeout/target escaped)
    };

    // Active bounty on a player
    struct Bounty
    {
        uint32 bountyId = 0;
        ObjectGuid targetGuid;
        std::string targetName;
        WantedStars wantedLevel;
        uint32 rewardGold = 0;          // Gold reward in copper
        std::chrono::system_clock::time_point postedTime;
        std::chrono::system_clock::time_point expiresTime;
        ObjectGuid claimedByGuid;       // Player who claimed (if any)
        std::chrono::system_clock::time_point claimedTime;
        BountyStatus status = BountyStatus::ACTIVE;
        uint32 lastKnownZoneId = 0;     // Last known zone for display
        std::string lastKnownZoneName;
    };

    // Active bounty hunter NPC
    struct BountyHunter
    {
        ObjectGuid hunterGuid;          // Creature GUID
        ObjectGuid targetGuid;          // Player being hunted
        uint32 bountyId = 0;            // Associated bounty
        std::chrono::steady_clock::time_point spawnTime;
        std::chrono::steady_clock::time_point lastTrackTime;  // Last time we updated tracking
        std::chrono::steady_clock::time_point ccImmunityEndTime;  // CC immunity expires
        HunterStatus status = HunterStatus::HUNTING;
        uint32 mapId = 0;
        uint32 instanceId = 0;
        uint32 spawnZoneId = 0;
        uint32 crimeAreaId = 0;
        TeamId crimeFaction = TEAM_NEUTRAL;
        float x = 0, y = 0, z = 0;      // Spawn position
    };

    // Pending hunter spawn (delayed spawn)
    struct PendingHunterSpawn
    {
        ObjectGuid targetGuid;
        std::chrono::steady_clock::time_point scheduledTime;
        bool isRespawn = false;         // Is this a respawn after previous hunter died
        bool hasHintPos = false;        // Has a pre-computed spawn position
        float hintX = 0, hintY = 0, hintZ = 0;
        uint32 crimeZoneId = 0;         // Jurisdiction at scheduling time
        uint32 crimeAreaId = 0;
        TeamId crimeFaction = TEAM_NEUTRAL;
        uint32 mapId = 0;
        uint32 instanceId = 0;
    };

    // Configuration for bounty rewards per star level
    struct BountyRewardConfig
    {
        uint32 oneStar   = 500000;      // 50g
        uint32 twoStar   = 1500000;     // 150g
        uint32 threeStar = 5000000;     // 500g
        uint32 fourStar  = 15000000;    // 1500g
        uint32 fiveStar  = 50000000;    // 5000g
    };

    // ==========================================================================
    // BOUNTY HUNTER MANAGER
    // ==========================================================================
    class ECBountyHunterManager
    {
    public:
        static ECBountyHunterManager* instance();

        // Initialization
        void LoadFromConfig();
        void Initialize();
        void Update(uint32 diff);

        // Bounty management
        void OnWantedLevelChanged(Player* player, WantedStars oldLevel, WantedStars newLevel);
        void PostBounty(Player* target);
        void ClearBounty(ObjectGuid targetGuid, BountyStatus reason);
        void ClaimBounty(Player* claimer, Player* target);
        bool HasActiveBounty(ObjectGuid targetGuid) const;
        std::optional<Bounty> GetBounty(ObjectGuid targetGuid) const;
        std::vector<Bounty> GetAllActiveBounties() const;
        uint32 GetRewardForStars(WantedStars stars) const;

        // Bounty hunter NPC management
        void ScheduleHunterSpawn(ObjectGuid targetGuid, bool isRespawn = false,
                               float hintX = 0, float hintY = 0, float hintZ = 0);
        void SpawnBountyHunter(Player* target, float hintX = 0, float hintY = 0, float hintZ = 0);
        void ProcessPendingSpawns();
        void OnBountyHunterDeath(Creature* hunter, Player* killer);
        void OnTargetKilledByHunter(Player* target, Creature* hunter);
        void UpdateHunterTracking(uint32 diff);
        void DespawnAllHuntersForTarget(ObjectGuid targetGuid);
        uint32 GetActiveHunterCountForTarget(ObjectGuid targetGuid) const;
        void ApplyCCImmunity(Creature* hunter);

        // Configuration
        bool IsEnabled() const { return m_enabled; }
        WantedStars GetMinWantedForBounty() const { return m_minWantedForBounty; }
        WantedStars GetMinWantedForHunter() const { return m_minWantedForHunter; }

        // GM commands
        void GMClearBounty(ObjectGuid targetGuid);
        void GMListBounties(Player* gm);

    private:
        ECBountyHunterManager() = default;
        ~ECBountyHunterManager() = default;

        // Thread safety
        mutable std::mutex m_bountyMutex;
        mutable std::mutex m_hunterMutex;

        // Active bounties (targetGuid -> Bounty)
        std::unordered_map<ObjectGuid, Bounty> m_bounties;

        // Active bounty hunter NPCs (hunterGuid -> BountyHunter)
        std::unordered_map<ObjectGuid, BountyHunter> m_hunters;

        // Pending hunter spawns (delayed spawns)
        std::vector<PendingHunterSpawn> m_pendingSpawns;
        mutable std::mutex m_pendingMutex;

        // Next bounty ID
        uint32 m_nextBountyId = 1;

        // Configuration
        bool m_enabled = true;
        WantedStars m_minWantedForBounty = WantedStars::THREE_STAR;   // Post bounty at 3+ stars
        WantedStars m_minWantedForHunter = WantedStars::FOUR_STAR;    // Spawn hunter at 4+ stars
        uint32 m_hunterSpawnDelayMs = 300000;       // 5 minutes delay before first hunter
        uint32 m_hunterRespawnDelayMs = 180000;     // 3 minutes between hunter spawns
        uint32 m_bountyDurationMs = 3600000;        // 1 hour bounty duration
        uint32 m_hunterDespawnTimeMs = 600000;      // 10 minutes before hunter despawns
        uint32 m_maxHuntersPerTarget = 2;           // Max simultaneous hunters per target
        uint32 m_ccImmunityDurationMs = 10000;      // 10 seconds CC immunity on spawn

        // Mercenary enhancements
        bool m_crossZoneTracking = false;            // Off by default; jurisdiction checks still apply
        float m_statMultiplierPerStar = 0.25f;       // +25% HP/damage per wanted star
        bool m_localDefenceAnnounce = true;           // Announce in LocalDefence channel

        // Bounty hunter creature entries (will be loaded from config/DB)
        std::vector<uint32> m_hunterEntries;
        uint32 m_eliteHunterEntry = 0;              // Elite hunter for 5-star

        // Reward config
        BountyRewardConfig m_rewardConfig;

        // Update timers
        uint32 m_updateTimer = 0;
        uint32 m_updateIntervalMs = 5000;  // Check every 5 seconds
        uint32 m_trackingTimer = 0;
        uint32 m_trackingIntervalMs = 10000;  // Update hunter positions every 10s

        // Helper methods
        void LogBountyToDatabase(const Bounty& bounty);
        void LogHunterToDatabase(const BountyHunter& hunter);
        std::string GetZoneName(uint32 zoneId) const;
        void BroadcastBountyPosted(const Bounty& bounty);
        void BroadcastBountyClaimed(const Bounty& bounty, Player* claimer);
        void BroadcastLocalDefence(Player* target, const std::string& message);
        void ApplyStatBuff(Creature* hunter, WantedStars targetStars);
    };
}

#define sECBountyHunterMgr ECGuardSystem::ECBountyHunterManager::instance()

#endif // EC_BOUNTY_HUNTER_MANAGER_H
