/*
 * mod-alive-world - Behavior Executor
 * Copyright (C) 2025 EC
 *
 * Executes AI decisions as actual bot behaviors. This is the bridge between
 * "what should the bot do" (AIDecisionEngine) and "make the bot do it" (Playerbots).
 *
 * Behavior Categories:
 * 1. CHAT      - Say, emote, whisper, guild chat, gossip spreading
 * 2. MOVEMENT  - Zone travel, position adjustment, flee, pursue
 * 3. COMBAT    - Target priority, aggression, ability usage, stance
 * 4. ECONOMIC  - Gather, craft, trade, auction house
 * 5. SOCIAL    - Group formation, friend/foe tracking, reputation
 */

#ifndef ALIVE_WORLD_BEHAVIOR_EXECUTOR_H
#define ALIVE_WORLD_BEHAVIOR_EXECUTOR_H

#include "AliveWorldTypes.h"
#include <atomic>
#include <functional>
#include <queue>
#include <mutex>
#include <unordered_map>
#include <unordered_set>

class Player;
class PlayerbotAI;

namespace AliveWorld
{
    class BotMemoryManager;
    class WorldStateManager;
    class GossipManager;

    // ========================================================================
    // BEHAVIOR TYPES
    // ========================================================================

    enum class BehaviorType : uint8
    {
        // Chat behaviors
        CHAT_SAY = 0,
        CHAT_YELL,
        CHAT_EMOTE,
        CHAT_WHISPER,
        CHAT_PARTY,
        CHAT_GUILD,
        GOSSIP_SPREAD,       // Tell nearby bot some gossip

        // Movement behaviors
        MOVE_TO_ZONE,        // Teleport/travel to zone
        MOVE_TO_POSITION,    // Walk to specific coordinates
        MOVE_NEAR_ENTITY,    // Move toward player/NPC
        MOVE_FLEE,           // Run away from danger
        MOVE_PATROL,         // Wander in area
        MOVE_FOLLOW,         // Follow target

        // Combat behaviors
        COMBAT_SET_STYLE,    // Aggressive/defensive/passive
        COMBAT_SET_TARGET,   // Priority target selection
        COMBAT_USE_ABILITY,  // Cast specific spell/ability
        COMBAT_FLEE,         // Disengage and run
        COMBAT_CALL_HELP,    // Alert nearby allies
        COMBAT_TAUNT,        // Draw aggro

        // Economic behaviors
        ECON_GATHER,         // Harvest node
        ECON_CRAFT,          // Create item
        ECON_TRADE_OFFER,    // Initiate trade with player
        ECON_TRADE_ACCEPT,   // Accept/reject trade
        ECON_AH_LIST,        // List item on AH
        ECON_AH_BUY,         // Buy from AH
        ECON_VENDOR_SELL,    // Sell to vendor
        ECON_VENDOR_BUY,     // Buy from vendor

        // Social behaviors
        SOCIAL_REQUEST_GROUP,    // Ask to join group
        SOCIAL_INVITE_GROUP,     // Invite to group
        SOCIAL_LEAVE_GROUP,      // Leave current group
        SOCIAL_FRIEND_ADD,       // Add to friends
        SOCIAL_FRIEND_REMOVE,    // Remove from friends
        SOCIAL_DUEL_REQUEST,     // Challenge to duel
        SOCIAL_GUILD_CHAT,       // Participate in guild discussion

        // Meta behaviors
        META_CHANGE_GOAL,        // Update current objective
        META_ADAPT_PERSONALITY,  // Shift personality traits
        META_REMEMBER_EVENT,     // Store important memory
    };

    enum class BehaviorPriority : uint8
    {
        CRITICAL = 0,    // Life-threatening, immediate
        HIGH = 1,        // Combat, active threat
        NORMAL = 2,      // Regular activities
        LOW = 3,         // Background tasks
        IDLE = 4,        // When nothing else to do
    };

    // ========================================================================
    // BEHAVIOR REQUEST
    // ========================================================================

    struct BehaviorRequest
    {
        uint32 requestId = 0;
        uint64 botGuid = 0;
        BehaviorType type = BehaviorType::CHAT_SAY;
        BehaviorPriority priority = BehaviorPriority::NORMAL;
        uint64 createdAt = 0;

        // Common parameters
        std::string message;           // For chat behaviors
        uint64 targetGuid = 0;         // Target player/NPC/object
        uint32 targetZoneId = 0;       // For zone travel
        float targetX = 0, targetY = 0, targetZ = 0;  // Position
        uint32 spellId = 0;            // For ability usage
        uint32 itemId = 0;             // For economic actions
        uint32 quantity = 1;           // Item quantity
        uint32 gold = 0;               // Currency amount

        // Callback when behavior completes
        std::function<void(bool success, const std::string& result)> callback;

        // For priority queue ordering
        bool operator<(const BehaviorRequest& other) const
        {
            if (priority != other.priority)
                return priority > other.priority; // Lower number = higher priority
            return createdAt > other.createdAt;   // Earlier = higher priority
        }
    };

    // ========================================================================
    // COMBAT STYLE
    // ========================================================================

    enum class CombatStyle : uint8
    {
        AGGRESSIVE = 0,  // Attack on sight, pursue fleeing enemies
        DEFENSIVE = 1,   // Only attack if attacked first
        PASSIVE = 2,     // Never initiate combat
        BERSERKER = 3,   // All-out attack, ignore defense
        CAUTIOUS = 4,    // Prioritize survival, flee if low HP
    };

    // ========================================================================
    // BEHAVIOR EXECUTOR
    // ========================================================================

    class BehaviorExecutor
    {
    public:
        BehaviorExecutor();
        ~BehaviorExecutor();

        // Initialization
        void Initialize(BotMemoryManager* memoryMgr,
                       WorldStateManager* worldStateMgr,
                       GossipManager* gossipMgr);
        void LoadConfig();
        void Shutdown();

        // Queue a behavior for execution
        uint32 QueueBehavior(const BehaviorRequest& request);

        // Convenience methods for common behaviors
        uint32 Say(uint64 botGuid, const std::string& message, BehaviorPriority priority = BehaviorPriority::NORMAL);
        uint32 Emote(uint64 botGuid, const std::string& emote, BehaviorPriority priority = BehaviorPriority::NORMAL);
        uint32 Whisper(uint64 botGuid, uint64 targetGuid, const std::string& message);
        uint32 SpreadGossip(uint64 botGuid, const std::string& gossip, uint32 gossipId);

        uint32 MoveToZone(uint64 botGuid, uint32 zoneId, BehaviorPriority priority = BehaviorPriority::NORMAL);
        uint32 MoveToPosition(uint64 botGuid, float x, float y, float z);
        uint32 MoveNear(uint64 botGuid, uint64 targetGuid, float distance = 5.0f);
        uint32 Flee(uint64 botGuid, uint64 threatGuid);
        uint32 Follow(uint64 botGuid, uint64 targetGuid);

        uint32 SetCombatStyle(uint64 botGuid, CombatStyle style);
        uint32 SetTarget(uint64 botGuid, uint64 targetGuid);
        uint32 UseAbility(uint64 botGuid, uint32 spellId, uint64 targetGuid = 0);
        uint32 CallForHelp(uint64 botGuid);

        uint32 OfferTrade(uint64 botGuid, uint64 targetGuid, uint32 itemId, uint32 quantity);
        uint32 ListOnAH(uint64 botGuid, uint32 itemId, uint32 quantity, uint32 buyoutGold);

        uint32 RequestGroup(uint64 botGuid, uint64 leaderGuid);
        uint32 InviteToGroup(uint64 botGuid, uint64 targetGuid);
        uint32 LeaveGroup(uint64 botGuid);

        // Cancel pending behavior
        bool CancelBehavior(uint32 requestId);

        // Update - processes queued behaviors
        void Update(uint32 diff);

        // Stats
        uint32 GetPendingCount() const;
        uint32 GetExecutedCount() const { return m_executedCount; }
        uint32 GetFailedCount() const { return m_failedCount; }

        // Configuration
        bool IsEnabled() const { return m_enabled; }
        void SetEnabled(bool enabled) { m_enabled = enabled; }
        uint32 GetMaxBehaviorsPerTick() const { return m_maxBehaviorsPerTick; }

    private:
        // Execute a single behavior
        bool ExecuteBehavior(const BehaviorRequest& request);

        // Category executors
        bool ExecuteChatBehavior(Player* bot, PlayerbotAI* ai, const BehaviorRequest& request);
        bool ExecuteMovementBehavior(Player* bot, PlayerbotAI* ai, const BehaviorRequest& request);
        bool ExecuteCombatBehavior(Player* bot, PlayerbotAI* ai, const BehaviorRequest& request);
        bool ExecuteEconomicBehavior(Player* bot, PlayerbotAI* ai, const BehaviorRequest& request);
        bool ExecuteSocialBehavior(Player* bot, PlayerbotAI* ai, const BehaviorRequest& request);
        bool ExecuteMetaBehavior(Player* bot, PlayerbotAI* ai, const BehaviorRequest& request);

        // Helpers
        PlayerbotAI* GetPlayerbotAI(Player* bot);
        bool CanExecuteBehavior(Player* bot, const BehaviorRequest& request);
        void NotifyCompletion(const BehaviorRequest& request, bool success, const std::string& result);

        // Configuration
        bool m_enabled = true;
        uint32 m_maxBehaviorsPerTick = 50;    // Max behaviors to process per update
        uint32 m_behaviorCooldownMs = 100;    // Minimum time between behaviors per bot

        // ====================================================================
        // BEHAVIOR QUEUE
        // ====================================================================
        // We use a priority_queue for O(log n) insertion and O(1) top access.
        // Since priority_queue doesn't support removal, we track cancelled
        // request IDs in a separate set and skip them during processing.
        // This is an efficient approach for sparse cancellations.
        // ====================================================================
        std::priority_queue<BehaviorRequest> m_queue;
        mutable std::mutex m_queueMutex;

        // Cancelled request IDs - checked during Update() to skip cancelled behaviors
        // Entries are cleaned up after the request is popped from the queue
        std::unordered_set<uint32> m_cancelledRequests;

        // Per-bot cooldowns - prevents behavior spam from same bot
        // Key: bot GUID, Value: timestamp of last behavior execution
        std::unordered_map<uint64, uint64> m_lastBehaviorTime;

        // Request ID generator - atomic for thread safety
        std::atomic<uint32> m_nextRequestId{1};

        // Statistics for monitoring and debugging
        std::atomic<uint32> m_executedCount{0};
        std::atomic<uint32> m_failedCount{0};
        std::atomic<uint32> m_cancelledCount{0};

        // References
        BotMemoryManager* m_memoryManager = nullptr;
        WorldStateManager* m_worldStateManager = nullptr;
        GossipManager* m_gossipManager = nullptr;
    };

} // namespace AliveWorld

#endif // ALIVE_WORLD_BEHAVIOR_EXECUTOR_H
