/*
 * mod-alive-world - Intelligence Tier Manager
 * Copyright (C) 2025 EC
 *
 * Manages the intelligence tiering system that determines which bots
 * get full AI access, which are aware, and which are passive.
 *
 * This is critical for scaling - we can't give 500 bots full AI access.
 */

#ifndef ALIVE_WORLD_INTELLIGENCE_TIER_H
#define ALIVE_WORLD_INTELLIGENCE_TIER_H

#include "AliveWorldTypes.h"

#include <cfloat>
#include <mutex>
#include <set>
#include <unordered_map>

class Player;

namespace AliveWorld
{
    class BotMemoryManager;
    class WorldStateManager;

    // Criteria for tier assignment
    struct TierCriteria
    {
        bool nearRealPlayer;        // Within Tier1Distance of a real player
        bool inCombatWithPlayer;    // Currently fighting a real player
        bool recentlySpokenTo;      // Received chat from player in last 5 min
        bool involvedInEvent;       // Part of an active world event
        bool sameZoneAsPlayer;      // In a zone with real players
        bool inPartyWithTier1;      // In party/raid with a Tier 1 bot
        bool nearWorldEvent;        // Near an active world event
        float distanceToNearestPlayer;
        uint64 lastPlayerInteractionTime;

        TierCriteria()
            : nearRealPlayer(false), inCombatWithPlayer(false)
            , recentlySpokenTo(false), involvedInEvent(false)
            , sameZoneAsPlayer(false), inPartyWithTier1(false)
            , nearWorldEvent(false), distanceToNearestPlayer(FLT_MAX)
            , lastPlayerInteractionTime(0)
        {}
    };

    class IntelligenceTierManager
    {
    public:
        IntelligenceTierManager();
        ~IntelligenceTierManager();

        // Initialization
        void Initialize(BotMemoryManager* memoryManager, WorldStateManager* worldStateManager);
        void LoadConfig();

        // Tier management
        void UpdateAllTiers();
        BotTier CalculateBotTier(uint64 botGuid, Player* bot);
        void PromoteToTier1(uint64 botGuid);
        void DemoteFromTier1(uint64 botGuid);
        void PromoteToTier2(uint64 botGuid);
        void DemoteToTier3(uint64 botGuid);

        // Tier queries
        BotTier GetBotTier(uint64 botGuid) const;
        std::vector<uint64> GetTier1Bots() const;
        std::vector<uint64> GetTier2Bots() const;
        uint32 GetTier1Count() const;
        uint32 GetTier2Count() const;
        uint32 GetTier3Count() const;
        bool IsTier1Full() const;
        bool IsTier2Full() const;

        // Player interaction tracking
        void OnPlayerSpoke(uint64 botGuid, uint64 playerGuid);
        void OnCombatStart(uint64 botGuid, uint64 opponentGuid, bool isPlayer);
        void OnCombatEnd(uint64 botGuid);
        void OnWorldEventJoin(uint64 botGuid, uint32 eventId);
        void OnWorldEventLeave(uint64 botGuid, uint32 eventId);

        // Criteria evaluation
        TierCriteria EvaluateCriteria(uint64 botGuid, Player* bot);
        float CalculateTierScore(const TierCriteria& criteria);

        // Update (called periodically)
        void Update(uint32 diff);

        // Stats
        std::map<BotTier, uint32> GetTierDistribution() const;

        // Settings
        uint32 GetMaxTier1() const { return m_maxTier1; }
        uint32 GetMaxTier2() const { return m_maxTier2; }
        float GetTier1Distance() const { return m_tier1Distance; }

    private:
        // Find bots that should be promoted/demoted
        void RebalanceTiers();

        // Get all real players for distance calculations
        std::vector<Player*> GetRealPlayers() const;

        // Check if player is a real player (not a bot)
        bool IsRealPlayer(Player* player) const;

        // Calculate distance to nearest real player
        float GetDistanceToNearestPlayer(Player* bot) const;

        // Configuration
        uint32 m_maxTier1;
        uint32 m_maxTier2;
        float m_tier1Distance;
        uint32 m_tierUpdateIntervalMs;
        static constexpr uint64 RECENT_INTERACTION_THRESHOLD_MS = 300000;  // 5 minutes

        // Tier tracking
        std::set<uint64> m_tier1Bots;
        std::set<uint64> m_tier2Bots;
        mutable std::mutex m_tierMutex;

        // Criteria cache
        std::unordered_map<uint64, TierCriteria> m_criteriaCache;
        mutable std::mutex m_criteriaMutex;

        // Interaction tracking
        std::unordered_map<uint64, uint64> m_lastPlayerInteraction;  // botGuid -> timestamp
        std::set<uint64> m_botsInCombatWithPlayer;
        std::set<uint64> m_botsInWorldEvent;
        mutable std::mutex m_interactionMutex;

        // References
        BotMemoryManager* m_memoryManager;
        WorldStateManager* m_worldStateManager;

        // Timing
        uint32 m_updateTimer;
    };

} // namespace AliveWorld

#endif // ALIVE_WORLD_INTELLIGENCE_TIER_H
