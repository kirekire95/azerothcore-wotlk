/*
 * mod-alive-world - Gossip Manager
 * Copyright (C) 2025 EC
 *
 * Manages the creation, storage, and spreading of gossip between bots.
 * Gossip represents "world knowledge" that spreads organically through
 * bot-to-bot communication, creating a living news network.
 *
 * Gossip Types:
 * - CRIME: Guard system crimes, wanted levels
 * - BOSS_KILL: World boss / raid boss defeats
 * - RARE_SPAWN: Rare creature appearances
 * - ACHIEVEMENT: Notable player achievements
 * - HOLIDAY: In-game events and holidays
 * - ZONE_EVENT: Raid, defense, world events
 * - GENERAL: Player activities, interesting happenings
 * - WARNING: Danger alerts, enemy presence
 * - REACTION: Emotional responses to events
 */

#ifndef ALIVE_WORLD_GOSSIP_MANAGER_H
#define ALIVE_WORLD_GOSSIP_MANAGER_H

#include "AliveWorldTypes.h"
#include <atomic>
#include <unordered_map>
#include <vector>
#include <mutex>
#include <functional>

class Player;

namespace AliveWorld
{
    class BotMemoryManager;
    class BehaviorExecutor;

    // ========================================================================
    // GOSSIP TYPES
    // ========================================================================

    enum class GossipType : uint8
    {
        CRIME = 0,
        BOSS_KILL,
        RARE_SPAWN,
        ACHIEVEMENT,
        HOLIDAY,
        ZONE_EVENT,
        GENERAL,
        WARNING,
        REACTION,
    };

    // ========================================================================
    // GOSSIP ITEM EXTENSION
    // ========================================================================
    // The base GossipItem is defined in AliveWorldTypes.h
    // This provides helper functions for GossipManager

    inline bool IsGossipExpired(const GossipItem& item)
    {
        return NowMs() >= item.expiresAt;
    }

    inline float GetGossipSpreadChance(const GossipItem& item)
    {
        // Base chance from importance
        float chance = item.importance * 5.0f; // 5-50% base

        // Reduce chance as it spreads more
        chance *= std::max(0.1f, 1.0f - (item.spreadRadius * 0.1f));

        // Boost for recent gossip
        uint64 age = NowMs() - item.timestamp;
        if (age < 60000) // < 1 minute
            chance *= 1.5f;

        return std::min(chance, 75.0f); // Cap at 75%
    }

    // ========================================================================
    // GOSSIP TEMPLATE
    // ========================================================================

    struct GossipTemplate
    {
        uint32 id = 0;
        GossipType type = GossipType::GENERAL;
        std::string pattern;        // Template with {variables}
        uint32 probability = 100;   // Weighted selection
        uint32 factionId = 0;       // 0 = any faction
        std::vector<std::string> requiredTags;
    };

    // ========================================================================
    // GOSSIP MANAGER
    // ========================================================================

    class GossipManager
    {
    public:
        GossipManager();
        ~GossipManager();

        // Initialization
        void Initialize(BotMemoryManager* memoryMgr, BehaviorExecutor* behaviorExec);
        void LoadTemplates();
        void Shutdown();

        // Create new gossip
        uint32 CreateGossip(GossipType type, uint32 zoneId, const std::string& text,
                           const std::string& subject, uint32 importance = 5,
                           uint64 sourceguid = 0, const std::string& sourceName = "",
                           uint32 ttlSeconds = 3600);

        // Generate gossip from template
        uint32 GenerateFromTemplate(GossipType type, uint32 zoneId,
                                    const std::unordered_map<std::string, std::string>& variables,
                                    uint32 importance = 5);

        // Get gossip
        const GossipItem* GetGossip(uint32 gossipId) const;
        std::vector<GossipItem> GetGossipByZone(uint32 zoneId, uint32 limit = 10) const;
        std::vector<GossipItem> GetGossipByType(GossipType type, uint32 limit = 10) const;
        std::vector<GossipItem> GetRecentGossip(uint32 limit = 20) const;
        std::vector<GossipItem> GetHotGossip(uint32 limit = 10) const; // High importance, recent

        // Bot knows/learns gossip
        bool BotKnowsGossip(uint64 botGuid, uint32 gossipId) const;
        void BotLearnGossip(uint64 botGuid, uint32 gossipId);
        std::vector<uint32> GetBotKnownGossip(uint64 botGuid) const;

        // Spreading
        void AttemptSpread(uint64 botGuid);
        void ForceSpread(uint64 botGuid, uint32 gossipId);

        // Update (handles expiration, periodic spreading)
        void Update(uint32 diff);

        // Stats
        uint32 GetTotalGossipCount() const;
        uint32 GetActiveGossipCount() const;
        uint32 GetSpreadCount() const { return m_totalSpreads; }

        // Configuration
        bool IsEnabled() const { return m_enabled; }
        void SetEnabled(bool enabled) { m_enabled = enabled; }
        void SetSpreadInterval(uint32 ms) { m_spreadIntervalMs = ms; }
        void SetSpreadRange(float range) { m_spreadRange = range; }

    private:
        // Template processing
        std::string ApplyTemplate(const GossipTemplate& tmpl,
                                  const std::unordered_map<std::string, std::string>& vars);
        const GossipTemplate* SelectTemplate(GossipType type,
                                             const std::unordered_map<std::string, std::string>& vars);

        // Cleanup expired gossip
        void CleanupExpired();

        // Find spread target
        Player* FindSpreadTarget(Player* source, uint32 gossipId);

        // Configuration
        bool m_enabled = true;
        uint32 m_maxGossipPoolSize = 500;
        uint32 m_spreadIntervalMs = 10000;  // How often to attempt spreading
        float m_spreadRange = 50.0f;        // Max distance for spreading

        // Gossip storage
        std::unordered_map<uint32, GossipItem> m_gossipPool;
        mutable std::mutex m_gossipMutex;
        std::atomic<uint32> m_nextGossipId{1};

        // Per-bot gossip knowledge (botGuid -> set of gossipIds)
        std::unordered_map<uint64, std::set<uint32>> m_botKnowledge;
        mutable std::mutex m_knowledgeMutex;

        // Templates
        std::vector<GossipTemplate> m_templates;

        // Timing
        uint32 m_spreadTimer = 0;
        uint32 m_cleanupTimer = 0;

        // Stats
        std::atomic<uint32> m_totalSpreads{0};

        // References
        BotMemoryManager* m_memoryManager = nullptr;
        BehaviorExecutor* m_behaviorExecutor = nullptr;
    };

} // namespace AliveWorld

#endif // ALIVE_WORLD_GOSSIP_MANAGER_H
