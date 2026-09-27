/*
 * mod-ec-realtime-bridge - Playerbots Bridge Adapter
 * Copyright (C) 2025 EC
 *
 * ============================================================================
 * PLAYERBOTS INTEGRATION
 * ============================================================================
 *
 * This adapter bridges mod-playerbots to the Node.js backend via the unified
 * TCP socket. It implements ActionExecutionListener to hook into the bot AI
 * decision engine and emit events when bots:
 *
 *   - Execute actions (attack, cast spell, move, loot, etc.)
 *   - Change strategies (combat, grind, quest, travel, etc.)
 *   - Change state (combat, non-combat, dead)
 *   - Select targets
 *
 * Event Types Emitted (namespace: "bots"):
 *
 *   bot_action_executed  - When a bot performs an action
 *   bot_action_failed    - When an action fails
 *   bot_strategy_changed - When strategies are added/removed
 *   bot_state_changed    - When bot state changes (combat/non-combat/dead)
 *   bot_target_changed   - When bot's target changes
 *   bot_spawned          - When a random bot spawns
 *   bot_despawned        - When a random bot despawns
 *
 * ============================================================================
 */

#ifndef PLAYERBOTS_BRIDGE_ADAPTER_H
#define PLAYERBOTS_BRIDGE_ADAPTER_H

#include <atomic>
#include <string>
#include <unordered_set>
#include <mutex>

// Forward declarations from mod-playerbots
class Action;
class Event;
class Player;
class PlayerbotAI;
class Engine;

// Check if dependencies are available
#ifdef MOD_EC_REALTIME_BRIDGE
#include "RealtimeBridgeSocket.h"
#include "RealtimeBridgeTypes.h"
#endif

// Must match mod-playerbots' BotState enum
enum class BridgeBotState : uint8_t
{
    COMBAT = 0,
    NON_COMBAT = 1,
    DEAD = 2
};

namespace PlayerbotsBridge
{

// ============================================================================
// ACTION EXECUTION LISTENER
// ============================================================================
// Implements mod-playerbots' ActionExecutionListener interface

class BridgeActionListener
{
public:
    virtual ~BridgeActionListener() = default;

    // Called before action execution
    virtual bool Before(Action* action, Event event) = 0;

    // Called to check if action should execute
    virtual bool AllowExecution(Action* action, Event event) = 0;

    // Called after action execution (where we emit events)
    virtual void After(Action* action, bool executed, Event event) = 0;

    // Called to allow result override
    virtual bool OverrideResult(Action* action, bool executed, Event event) = 0;
};

// ============================================================================
// BRIDGE ADAPTER
// ============================================================================

class BridgeAdapter : public BridgeActionListener
{
public:
    static BridgeAdapter* Instance();

    // ========================================================================
    // LIFECYCLE
    // ========================================================================

    void Initialize();
    void Shutdown();

    // ========================================================================
    // ACTION LISTENER INTERFACE
    // ========================================================================

    bool Before(Action* action, Event event) override;
    bool AllowExecution(Action* action, Event event) override;
    void After(Action* action, bool executed, Event event) override;
    bool OverrideResult(Action* action, bool executed, Event event) override;

    // ========================================================================
    // EVENT EMISSION (C++ → Node.js)
    // ========================================================================

    // Action Events
    void EmitBotActionExecuted(Player* bot, const std::string& actionName,
                                float relevance, const std::string& eventSource);
    void EmitBotActionFailed(Player* bot, const std::string& actionName,
                              const std::string& reason);

    // Strategy Events
    void EmitBotStrategyChanged(Player* bot, const std::string& strategy,
                                 bool added, BridgeBotState state);
    void EmitBotStrategiesSnapshot(Player* bot, const std::vector<std::string>& strategies,
                                    BridgeBotState state);

    // State Events
    void EmitBotStateChanged(Player* bot, BridgeBotState oldState, BridgeBotState newState);

    // Target Events
    void EmitBotTargetChanged(Player* bot, uint64_t targetGuid,
                               const std::string& targetName, bool isHostile);

    // Lifecycle Events
    void EmitBotSpawned(Player* bot, uint32_t zoneId, uint32_t areaId);
    void EmitBotDespawned(Player* bot, const std::string& reason);

    // ========================================================================
    // REGISTRATION
    // ========================================================================

    /// Register this adapter as a listener on a bot's engine
    void RegisterWithEngine(Engine* engine);

    /// Unregister from an engine
    void UnregisterFromEngine(Engine* engine);

    // ========================================================================
    // SAMPLING CONTROL
    // ========================================================================
    // We can't emit every single action (too much traffic), so we sample

    /// Set action sampling rate (0.0 = none, 1.0 = all)
    void SetActionSampleRate(float rate) { m_actionSampleRate = rate; }

    /// Only emit for bots in these zones (empty = all zones)
    void SetMonitoredZones(const std::unordered_set<uint32_t>& zones);

    /// Only emit for these specific bot GUIDs (empty = all bots)
    void SetMonitoredBots(const std::unordered_set<uint64_t>& guids);

    // ========================================================================
    // STATISTICS
    // ========================================================================

    uint64_t GetActionsEmitted() const { return m_actionsEmitted.load(); }
    uint64_t GetStrategiesEmitted() const { return m_strategiesEmitted.load(); }
    uint64_t GetActionsSkipped() const { return m_actionsSkipped.load(); }

private:
    BridgeAdapter();
    ~BridgeAdapter();

    // Disable copy
    BridgeAdapter(const BridgeAdapter&) = delete;
    BridgeAdapter& operator=(const BridgeAdapter&) = delete;

    // ========================================================================
    // HELPERS
    // ========================================================================

    /// Check if we should emit for this bot (based on filters)
    bool ShouldEmitForBot(Player* bot);

    /// Check if we should sample this action (based on rate)
    bool ShouldSampleAction();

    /// Build standard bot JSON fields
    std::string BuildBotJson(Player* bot);

    /// Escape JSON string
    std::string EscapeJson(const std::string& input);

    /// Get bot state string
    const char* GetBotStateString(BridgeBotState state);

    // ========================================================================
    // STATE
    // ========================================================================

    bool m_initialized = false;

    // Sampling
    float m_actionSampleRate = 0.1f;  // Default: emit 10% of actions
    std::unordered_set<uint32_t> m_monitoredZones;
    std::unordered_set<uint64_t> m_monitoredBots;
    std::mutex m_filterMutex;

    // Statistics
    std::atomic<uint64_t> m_actionsEmitted{0};
    std::atomic<uint64_t> m_strategiesEmitted{0};
    std::atomic<uint64_t> m_actionsSkipped{0};

    // Random sampling
    uint32_t m_sampleCounter = 0;
};

#define sPlayerbotsBridge PlayerbotsBridge::BridgeAdapter::Instance()

} // namespace PlayerbotsBridge

#endif // PLAYERBOTS_BRIDGE_ADAPTER_H
