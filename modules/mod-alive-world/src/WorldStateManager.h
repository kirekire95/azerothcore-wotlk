/*
 * mod-alive-world - World State Manager
 * Copyright (C) 2025 EC
 *
 * Maintains global world state including zone tracking, gossip pool,
 * and bot social graphs. This is the "shared memory" for all bots.
 */

#ifndef ALIVE_WORLD_STATE_MANAGER_H
#define ALIVE_WORLD_STATE_MANAGER_H

#include "AliveWorldTypes.h"
#include "DatabaseEnvFwd.h"

#include <atomic>
#include <mutex>
#include <queue>
#include <shared_mutex>

namespace AliveWorld
{
    class EventCollector;
    class GossipTemplateManager;
    class BotMemoryManager;

    class WorldStateManager
    {
    public:
        WorldStateManager();
        ~WorldStateManager();

        // Initialization
        void Initialize(EventCollector* eventCollector,
                        GossipTemplateManager* templateManager = nullptr);
        void SetMemoryManager(BotMemoryManager* memoryManager) { m_memoryManager = memoryManager; }
        void LoadConfig();

        // Zone state management
        void UpdateZoneStates();
        ZoneState GetZoneState(uint32 zoneId) const;
        std::vector<ZoneState> GetAllZoneStates() const;
        std::vector<ZoneState> GetHotZones() const;
        std::vector<ZoneState> GetZonesWithPlayers() const;
        bool IsHotZone(uint32 zoneId) const;
        uint8 GetZoneDangerLevel(uint32 zoneId) const;
        uint8 GetZoneActivityLevel(uint32 zoneId) const;

        // Weather tracking
        void UpdateZoneWeather(uint32 zoneId, uint8 weatherType, float grade);
        ZoneWeatherState GetZoneWeather(uint32 zoneId) const;
        std::vector<uint32> GetZonesWithBadWeather() const;
        bool HasBadWeather(uint32 zoneId) const;

        // Time tracking
        void UpdateTimeState();
        const TimeState& GetTimeState() const { return m_timeState; }
        bool CheckTimeTransition();  // Returns true if time period changed
        TimeTransitionType GetCurrentTimePeriod() const { return m_timeState.currentPeriod; }
        Season GetCurrentSeason() const { return m_timeState.currentSeason; }

        // Player/bot tracking
        void OnPlayerEnterZone(uint64 guid, uint32 zoneId, bool isAlliance, bool isBot);
        void OnPlayerLeaveZone(uint64 guid, uint32 zoneId, bool isAlliance, bool isBot);
        uint32 GetPlayersInZone(uint32 zoneId, bool includeBot = false) const;
        uint32 GetBotsInZone(uint32 zoneId) const;

        // Gossip system
        void AddGossip(const GossipItem& gossip);
        GossipItem CreateGossipFromEvent(const WorldEvent& event);
        std::vector<GossipItem> GetGossipForBot(uint64 botGuid, uint32 maxCount = 5) const;
        std::vector<GossipItem> GetRecentGossip(uint32 count = 10) const;
        std::vector<GossipItem> GetGossipByZone(uint32 zoneId) const;
        void MarkGossipKnown(uint32 gossipId, uint64 botGuid);
        void SpreadGossip(uint64 sourceBotGuid, uint64 targetBotGuid, uint32 gossipId);
        void PropagateGossip();  // Called periodically to spread gossip
        void CleanupExpiredGossip();
        uint32 GetGossipPoolSize() const;

        // Gossip persistence
        void LoadGossipFromDatabase();
        void PersistGossipToDatabase();
        bool IsGossipPersistenceEnabled() const { return m_gossipPersistEnabled; }

        // Social graph
        void RecordSocialConnection(uint64 botGuid, uint64 otherGuid, const std::string& type);
        std::set<uint64> GetBotConnections(uint64 botGuid) const;
        std::vector<uint64> GetNearbyBots(uint64 botGuid, float distance = 50.0f) const;
        bool AreSociallyConnected(uint64 guid1, uint64 guid2) const;

        // Event integration
        void ProcessNewEvents();

        // Update (called periodically)
        void Update(uint32 diff);

        // Stats
        APIStats GetStats() const;

        // Settings
        bool IsEnabled() const { return m_enabled; }
        void SetEnabled(bool enabled) { m_enabled = enabled; }

    private:
        // Generate gossip headline from event
        std::string GenerateHeadline(const WorldEvent& event);
        std::string GenerateDetails(const WorldEvent& event);

        // Calculate zone activity/danger from events
        void RecalculateZoneMetrics(uint32 zoneId);

        // Configuration
        bool m_enabled;
        uint32 m_updateIntervalMs;
        uint8 m_hotZoneThreshold;
        uint32 m_maxGossipPoolSize;
        uint32 m_gossipTTLSeconds;
        uint32 m_gossipSpreadChance;
        float m_gossipSpreadDistance;
        uint8 m_minGossipImportance;

        // Gossip persistence config
        bool m_gossipPersistEnabled;
        uint32 m_gossipPersistIntervalMs;
        uint32 m_gossipPersistBatchSize;

        // Zone states
        std::map<uint32, ZoneState> m_zoneStates;
        mutable std::shared_mutex m_zoneMutex;

        // Weather tracking per zone
        std::map<uint32, ZoneWeatherState> m_zoneWeather;
        mutable std::shared_mutex m_weatherMutex;

        // Global time state
        TimeState m_timeState;
        uint8 m_lastTrackedHour;  // For detecting hour changes

        // Gossip pool
        std::deque<GossipItem> m_gossipPool;
        std::unordered_map<uint32, size_t> m_gossipIndex;
        mutable std::shared_mutex m_gossipMutex;
        std::atomic<uint32> m_nextGossipId;

        // Gossip persistence queue
        std::queue<GossipItem> m_gossipPersistQueue;
        std::mutex m_gossipPersistMutex;

        // Social graph: botGuid -> set of connected GUIDs
        std::map<uint64, std::set<uint64>> m_socialGraph;
        mutable std::shared_mutex m_socialMutex;

        // Player/bot tracking per zone: zoneId -> set of GUIDs
        std::map<uint32, std::set<uint64>> m_playersInZone;
        std::map<uint32, std::set<uint64>> m_botsInZone;
        mutable std::shared_mutex m_trackingMutex;

        // Reference to event collector
        EventCollector* m_eventCollector;
        uint32 m_lastProcessedEventId;

        // Reference to gossip template manager
        GossipTemplateManager* m_templateManager;

        // Reference to bot memory manager
        BotMemoryManager* m_memoryManager;

        // Timing
        uint32 m_zoneUpdateTimer;
        uint32 m_gossipPropagateTimer;
        uint32 m_gossipCleanupTimer;
        uint32 m_gossipPersistTimer;
        static constexpr uint32 GOSSIP_PROPAGATE_INTERVAL_MS = 30000;  // 30 seconds
        static constexpr uint32 GOSSIP_CLEANUP_INTERVAL_MS = 300000;   // 5 minutes
        static constexpr uint32 DEFAULT_GOSSIP_PERSIST_INTERVAL_MS = 10000;  // 10 seconds
        static constexpr uint32 DEFAULT_GOSSIP_PERSIST_BATCH_SIZE = 20;
    };

} // namespace AliveWorld

#endif // ALIVE_WORLD_STATE_MANAGER_H
