/*
 * mod-alive-world - Event Collector
 * Copyright (C) 2025 EC
 *
 * Collects and buffers world events from game hooks.
 * This is the "sensory input" for the alive world system.
 */

#ifndef ALIVE_WORLD_EVENT_COLLECTOR_H
#define ALIVE_WORLD_EVENT_COLLECTOR_H

#include "AliveWorldTypes.h"
#include "DatabaseEnvFwd.h"

#include <atomic>
#include <mutex>
#include <queue>
#include <unordered_map>

class Player;
class Creature;
class Unit;

namespace AliveWorld
{
    class EventCollector
    {
    public:
        EventCollector();
        ~EventCollector();

        // Initialization
        void Initialize();
        void LoadConfig();

        // Event recording (called from hooks)
        void RecordCombatKill(Unit* killer, Unit* victim, uint32 zoneId, uint32 areaId);
        void RecordPlayerDeath(Player* player, Unit* killer);
        void RecordBossEngaged(Creature* boss, Player* engager);
        void RecordBossKilled(Creature* boss, Player* killer);
        void RecordChat(Player* sender, const std::string& message, uint32 zoneId);
        void RecordEmote(Player* player, uint32 emoteId, uint32 zoneId);
        void RecordTrade(Player* player1, Player* player2, uint32 zoneId);
        void RecordGroupEvent(Player* leader, bool formed, uint32 zoneId);
        void RecordDuel(Player* player1, Player* player2, bool started, Player* winner = nullptr);
        void RecordZoneChange(Player* player, uint32 oldZone, uint32 newZone, bool isBot);
        void RecordRareSpawn(Creature* creature, uint32 zoneId);
        void RecordWorldBossSpawn(Creature* creature, uint32 zoneId);
        void RecordGuardCalled(Unit* criminal, uint32 zoneId, uint8 wantedLevel);
        void RecordWorldEvent(uint32 eventId, const std::string& eventName, bool started);

        // Custom event recording
        void RecordCustomEvent(WorldEventType type, ObjectGuid primary, ObjectGuid secondary,
                               const std::string& details, uint8 importance,
                               uint32 zoneId, uint32 areaId, uint32 mapId);

        // Event retrieval
        std::vector<WorldEvent> GetRecentEvents(uint32 count = 50) const;
        std::vector<WorldEvent> GetEventsByZone(uint32 zoneId, uint32 count = 20) const;
        std::vector<WorldEvent> GetEventsByType(WorldEventType type, uint32 count = 20) const;
        std::vector<WorldEvent> GetEventsAfter(uint64 timestamp) const;
        std::vector<WorldEvent> GetUnprocessedEvents() const;
        const WorldEvent* GetEvent(uint32 eventId) const;

        // Event processing
        void MarkEventProcessed(uint32 eventId);
        void CleanupExpiredEvents();

        // Stats
        uint32 GetEventCount() const;
        uint32 GetUnprocessedCount() const;
        std::map<WorldEventType, uint32> GetEventTypeCounts() const;

        // Update (called periodically)
        void Update(uint32 diff);

        // Persistence
        void LoadEventsFromDatabase();
        void PersistEventsToDatabase();
        bool IsPersistenceEnabled() const { return m_persistEnabled; }

        // Settings
        bool IsEnabled() const { return m_enabled; }
        void SetEnabled(bool enabled) { m_enabled = enabled; }

    private:
        // Add event to buffer (thread-safe)
        void AddEvent(WorldEvent event);

        // Calculate event importance
        uint8 CalculateImportance(WorldEventType type, Unit* primary, Unit* secondary);

        // Check if unit is a bot
        bool IsBot(Unit* unit) const;

        // Get zone and area from position
        void GetZoneAndArea(Unit* unit, uint32& zoneId, uint32& areaId, uint32& mapId);

        // Configuration
        bool m_enabled;
        bool m_persistEnabled;
        uint32 m_maxBufferSize;
        uint32 m_eventTTLSeconds;
        uint32 m_persistIntervalMs;
        uint32 m_persistBatchSize;

        // Event storage
        std::deque<WorldEvent> m_eventBuffer;
        std::unordered_map<uint32, size_t> m_eventIndex;  // eventId -> buffer index
        mutable std::mutex m_bufferMutex;

        // Persistence queue
        std::queue<WorldEvent> m_persistQueue;
        std::mutex m_persistMutex;

        // Event ID generator
        std::atomic<uint32> m_nextEventId;

        // Timing
        uint32 m_cleanupTimer;
        uint32 m_persistTimer;
        static constexpr uint32 CLEANUP_INTERVAL_MS = 60000;  // 1 minute
        static constexpr uint32 DEFAULT_PERSIST_INTERVAL_MS = 5000;  // 5 seconds
        static constexpr uint32 DEFAULT_PERSIST_BATCH_SIZE = 50;
    };

} // namespace AliveWorld

#endif // ALIVE_WORLD_EVENT_COLLECTOR_H
