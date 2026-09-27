/*
 * mod-alive-world - Bridge Adapter
 * Copyright (C) 2025 EC
 *
 * ============================================================================
 * UNIFIED COMMUNICATION LAYER
 * ============================================================================
 *
 * This adapter provides ALL communication between mod-alive-world and the
 * Node.js backend (wow-control-backend). It replaces the old HTTP server
 * approach with a single TCP socket via mod-ec-realtime-bridge.
 *
 * Communication Patterns:
 *
 *   1. EVENTS (C++ → Node.js, fire-and-forget):
 *      - Player login/logout, chat, emotes, zone changes
 *      - Bot tier changes, proximity to players
 *      - Combat start/end, kills, deaths
 *      - Group/duel/trade activities
 *      - Rare spawns, boss kills, world events
 *      - Gossip spreading, goal progress
 *      - AI queue status, zone state updates
 *
 *   2. QUERIES (Node.js → C++, request/response):
 *      - get_stats: System statistics
 *      - get_zones: All zone states
 *      - get_zone: Specific zone by ID
 *      - get_gossip: Recent gossip feed
 *      - get_gossip_item: Specific gossip by ID
 *      - get_events: Recent world events
 *      - get_bots: Bot summary with tier counts
 *      - get_bot: Specific bot with full memory
 *      - get_tiers: Tier distribution
 *      - get_ai_queue: AI engine status
 *
 *   3. COMMANDS (Node.js → C++, action execution):
 *      - bot_say, bot_emote, bot_whisper: Chat actions
 *      - bot_follow, bot_stay, bot_move_to: Movement
 *      - bot_attack, bot_flee, bot_assist: Combat
 *      - set_goal, cancel_goal: Goal management
 *      - set_debug: Toggle debug mode
 *
 * Request/Response Protocol:
 *   - Node.js sends: { "ns": "alive", "type": "get_bots", "requestId": "abc123", ... }
 *   - C++ responds:  { "ns": "alive", "type": "response", "requestId": "abc123", "data": {...} }
 *   - Node.js matches response by requestId
 *
 * ============================================================================
 */

#ifndef ALIVE_WORLD_BRIDGE_ADAPTER_H
#define ALIVE_WORLD_BRIDGE_ADAPTER_H

#include "AliveWorldTypes.h"

#include <atomic>
#include <string>

// Check if the shared bridge is available
#ifdef MOD_EC_REALTIME_BRIDGE
#include "RealtimeBridgeSocket.h"
#include "RealtimeBridgeTypes.h"
#endif

class Player;
class Unit;

namespace AliveWorld
{
    // Forward declarations
    class AIDecisionEngine;
    class BotMemoryManager;
    class EventCollector;
    class GossipManager;
    class IntelligenceTierManager;
    class WorldStateManager;

// ============================================================================
// BRIDGE ADAPTER
// ============================================================================

class BridgeAdapter
{
public:
    static BridgeAdapter* Instance();

    // ========================================================================
    // LIFECYCLE
    // ========================================================================

    /// Initialize and register with shared bridge
    void Initialize();

    /// Clean shutdown
    void Shutdown();

    /// Set manager references (called after managers are created)
    void SetManagers(EventCollector* eventCollector,
                     WorldStateManager* worldStateManager,
                     BotMemoryManager* memoryManager,
                     IntelligenceTierManager* tierManager,
                     AIDecisionEngine* aiEngine,
                     GossipManager* gossipManager);

    // ========================================================================
    // CONNECTION STATUS
    // ========================================================================

    /// Is the TCP socket connected to Node.js?
    bool IsConnected() const;

    /// Is the connection authenticated?
    bool IsDaemonAvailable() const;

    // ========================================================================
    // EVENT EMISSION (C++ → Node.js)
    // ========================================================================
    // These methods push events to Node.js. Fire-and-forget, no response expected.

    // Player Events
    void EmitPlayerLogin(Player* player);
    void EmitPlayerLogout(Player* player);
    void EmitPlayerChat(Player* player, uint32 type, const std::string& message);
    void EmitPlayerEmote(Player* player, uint32 emoteId);
    void EmitPlayerZoneChange(Player* player, uint32 newZoneId, uint32 newAreaId);

    // Bot Events
    void EmitBotTierChanged(uint32 botGuid, BotTier oldTier, BotTier newTier, const std::string& reason);
    void EmitBotNearPlayer(uint32 botGuid, uint32 playerGuid, float distance);

    // Combat Events
    void EmitCombatStart(uint32 botGuid, Unit* target);
    // EmitCombatEnd removed - OnUnitLeaveCombat hook doesn't exist in AzerothCore UnitScript
    void EmitCreatureKilled(Player* killer, uint32 creatureEntry);
    void EmitPlayerKilled(Player* killer, Player* victim);
    void EmitPlayerDeath(Player* player, Unit* killer);
    void EmitBotLowHealth(uint32 botGuid, float healthPct);
    void EmitBotLowMana(uint32 botGuid, float manaPct);

    // Social Events
    void EmitGroupFormed(Player* leader);
    void EmitGroupDisbanded(Player* leader);
    void EmitDuelStart(Player* player1, Player* player2);
    void EmitDuelEnd(Player* winner, Player* loser);
    void EmitTradeStarted(Player* player1, Player* player2);

    // World Events
    void EmitRareSpawn(uint32 creatureEntry, const std::string& creatureName, uint32 zoneId);
    void EmitBossEngaged(uint32 creatureEntry, const std::string& bossName, Player* engager);
    void EmitBossKilled(uint32 creatureEntry, const std::string& bossName, Player* killer);
    void EmitWorldEventStart(uint32 eventId, const std::string& eventName);
    void EmitWorldEventEnd(uint32 eventId, const std::string& eventName);

    // Gossip Events
    void EmitGossipSpread(const std::string& gossipType, const std::string& content, uint32 zoneId);

    // Goal System Events
    void EmitGoalCreated(uint64 botGuid, uint32 goalId, uint8 goalType, const std::string& targetName, uint8 priority);
    void EmitGoalProgress(uint64 botGuid, uint32 goalId, float progress, uint32 current, uint32 target);
    void EmitGoalCompleted(uint64 botGuid, uint32 goalId, const std::string& targetName);
    void EmitGoalAbandoned(uint64 botGuid, uint32 goalId, const std::string& reason);

    // AI Queue Events
    void EmitAIRequestQueued(uint64 botGuid, uint32 requestId, uint8 requestType, uint8 priority);
    void EmitAIRequestCompleted(uint64 botGuid, uint32 requestId, bool success, uint32 responseTimeMs);
    void EmitAICacheHit(uint64 botGuid, const std::string& cacheKey);

    // Zone State Events
    void EmitZoneStateUpdated(uint32 zoneId, uint32 mapId, uint32 alliancePlayers, uint32 hordePlayers,
                              uint32 allianceBots, uint32 hordeBots, uint8 dangerLevel, uint8 activityLevel);

    // ========================================================================
    // COMMAND PROCESSING
    // ========================================================================

    /// Process pending commands from the main thread (thread-safe)
    void ProcessPendingCommands();

    // ========================================================================
    // STATISTICS
    // ========================================================================

    uint64 GetEventsSent() const { return m_eventsSent.load(); }
    uint64 GetCommandsReceived() const { return m_commandsReceived.load(); }
    uint64 GetQueriesHandled() const { return m_queriesHandled.load(); }

private:
    BridgeAdapter();
    ~BridgeAdapter();

    // Disable copy
    BridgeAdapter(const BridgeAdapter&) = delete;
    BridgeAdapter& operator=(const BridgeAdapter&) = delete;

    // ========================================================================
    // COMMAND HANDLER (Node.js → C++)
    // ========================================================================

#ifdef MOD_EC_REALTIME_BRIDGE
    /// Main command router - called when Node.js sends a message
    void HandleBridgeCommand(const ECRealtimeBridge::BridgeCommand& cmd);

    /// Send a response back to Node.js with matching requestId
    void SendResponse(const std::string& requestId, const std::string& jsonData);

    /// Send an error response
    void SendErrorResponse(const std::string& requestId, const std::string& error, int code = 400);
#endif

    // ========================================================================
    // QUERY HANDLERS
    // ========================================================================
    // Each returns JSON string with the requested data

    std::string HandleGetStats();
    std::string HandleGetZones();
    std::string HandleGetZone(uint32 zoneId);
    std::string HandleGetGossip(uint32 limit = 50);
    std::string HandleGetGossipItem(uint32 gossipId);
    std::string HandleGetEvents(uint32 limit = 100);
    std::string HandleGetBots();
    std::string HandleGetBot(uint64 botGuid);
    std::string HandleGetTiers();
    std::string HandleGetAIQueue();

    // ========================================================================
    // ACTION HANDLERS
    // ========================================================================
    // Execute commands from Node.js

    bool HandleBotSay(uint64 botGuid, const std::string& message);
    bool HandleBotEmote(uint64 botGuid, const std::string& emote);
    bool HandleBotWhisper(uint64 botGuid, uint64 targetGuid, const std::string& message);
    bool HandleBotFollow(uint64 botGuid, uint64 targetGuid);
    bool HandleBotStay(uint64 botGuid);
    bool HandleBotMoveTo(uint64 botGuid, float x, float y, float z);
    bool HandleBotAttack(uint64 botGuid, uint64 targetGuid);
    bool HandleBotFlee(uint64 botGuid);
    bool HandleSetGoal(uint64 botGuid, uint8 goalType, const std::string& target, uint8 priority);
    bool HandleCancelGoal(uint64 botGuid, uint32 goalId);

    // Behavior controller commands
    bool HandleSetBotStance(uint64 botGuid, const std::string& stance);
    bool HandleSetBotBehaviorProfile(uint64 botGuid, const std::string& profileJson);
    bool HandleToggleBotDebugThoughts(uint64 botGuid, bool enabled);
    std::string HandleGetBotBehavior(uint64 botGuid);

    // ========================================================================
    // JSON HELPERS
    // ========================================================================

    std::string BuildPlayerJson(Player* player);
    std::string BuildUnitJson(Unit* unit);
    std::string EscapeJson(const std::string& input);

    // ========================================================================
    // STATE
    // ========================================================================

    // Manager references
    EventCollector* m_eventCollector = nullptr;
    WorldStateManager* m_worldStateManager = nullptr;
    BotMemoryManager* m_memoryManager = nullptr;
    IntelligenceTierManager* m_tierManager = nullptr;
    AIDecisionEngine* m_aiEngine = nullptr;
    GossipManager* m_gossipManager = nullptr;

    // Statistics
    std::atomic<uint64> m_eventsSent{0};
    std::atomic<uint64> m_commandsReceived{0};
    std::atomic<uint64> m_queriesHandled{0};

    // Flags
    bool m_initialized = false;
    bool m_debugMode = false;
};

#define sAliveWorldBridge AliveWorld::BridgeAdapter::Instance()

} // namespace AliveWorld

#endif // ALIVE_WORLD_BRIDGE_ADAPTER_H
