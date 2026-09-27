/*
 * mod-ec-world-events - Bridge Integration
 * Copyright (C) 2025 EC
 *
 * Emits world event system events to the shared realtime bridge.
 */

#ifndef EC_WORLD_EVENT_BRIDGE_H
#define EC_WORLD_EVENT_BRIDGE_H

#include <string>
#include <cstdint>

class Player;
class Creature;

namespace ECWorldEvents
{

// Forward declarations
struct WorldEventInstance;

// ============================================================================
// WORLD EVENT BRIDGE
// ============================================================================

class WorldEventBridge
{
public:
    static WorldEventBridge* Instance();

    // Initialize bridge connection
    void Initialize();
    void Shutdown();

    // Event lifecycle
    void EmitEventScheduled(uint32_t eventId, uint32_t instanceId, const std::string& eventName,
                           const std::string& eventType, uint32_t mapId, uint32_t zoneId,
                           uint64_t startTime);
    void EmitEventStarting(uint32_t eventId, uint32_t instanceId, const std::string& eventName);
    void EmitEventStarted(uint32_t eventId, uint32_t instanceId, const std::string& eventName,
                         const std::string& eventType, uint32_t mapId, uint32_t zoneId);
    void EmitEventEnding(uint32_t eventId, uint32_t instanceId, const std::string& eventName);
    void EmitEventEnded(uint32_t eventId, uint32_t instanceId, const std::string& eventName,
                       uint32_t duration, uint32_t participantCount, const std::string& result);
    void EmitEventCancelled(uint32_t eventId, uint32_t instanceId, const std::string& eventName,
                           const std::string& reason);

    // Participant events
    void EmitParticipantJoined(uint32_t eventId, uint32_t instanceId, Player* player, bool isBot);
    void EmitParticipantLeft(uint32_t eventId, uint32_t instanceId, Player* player, bool isBot);
    void EmitParticipantDied(uint32_t eventId, uint32_t instanceId, Player* player, bool isBot);
    void EmitParticipantRewarded(uint32_t eventId, uint32_t instanceId, Player* player,
                                uint32_t gold, uint32_t honor, uint32_t itemId);

    // Boss combat
    void EmitBossEngaged(uint32_t eventId, uint32_t instanceId, Creature* boss, Player* engager);
    void EmitBossKilled(uint32_t eventId, uint32_t instanceId, Creature* boss, Player* killer);
    void EmitObjectiveCompleted(uint32_t eventId, uint32_t instanceId, uint32_t objectiveId,
                               const std::string& objectiveName);

    // Bot management
    void EmitBotsRecruited(uint32_t eventId, uint32_t instanceId, uint32_t botCount,
                          const std::string& faction);
    void EmitBotsDismissed(uint32_t eventId, uint32_t instanceId, uint32_t botCount);

    // Auto-escalation
    void EmitAutoEscalate(uint32_t eventId, uint32_t instanceId, const std::string& eventName,
                         uint32_t escalationLevel, uint32_t minutesWithoutDefenders);

    // Building capture events (AV-style towers/bunkers)
    void EmitBuildingAssault(uint32_t instanceId, uint32_t objectDbId, const std::string& buildingName,
                            uint64_t capturerGuid, const std::string& capturerName, uint8_t capturerFaction);
    void EmitBuildingDefended(uint32_t instanceId, uint32_t objectDbId, const std::string& buildingName);
    void EmitBuildingDestroyed(uint32_t instanceId, uint32_t objectDbId, const std::string& buildingName,
                              uint64_t capturerGuid, const std::string& capturerName, uint8_t capturerFaction,
                              int32_t points);

    // Statistics
    uint64_t GetEventsSent() const { return m_eventsSent; }

private:
    WorldEventBridge();
    ~WorldEventBridge();

    // Helpers
    std::string BuildPlayerJson(Player* player);
    std::string BuildCreatureJson(Creature* creature);

    uint64_t m_eventsSent;
    bool m_initialized;
};

#define sWorldEventBridge ECWorldEvents::WorldEventBridge::Instance()

} // namespace ECWorldEvents

#endif // EC_WORLD_EVENT_BRIDGE_H
