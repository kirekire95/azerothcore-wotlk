/*
 * mod-alive-world - Behavior Executor Implementation
 * Copyright (C) 2025 EC
 */

#include "BehaviorExecutor.h"
#include "BotMemoryManager.h"
#include "WorldStateManager.h"
#include "AliveWorldManager.h"
#include "GossipManager.h"
#include "GameTime.h"

#include "Player.h"
#include "ObjectAccessor.h"
#include "ObjectMgr.h"
#include "World.h"
#include "SpellMgr.h"
#include "Group.h"
#include "Guild.h"
#include "Log.h"

// Playerbots includes
#include "PlayerbotAI.h"
#include "PlayerbotMgr.h"
#include "ChatHelper.h"

// Movement includes
#include "MotionMaster.h"

#include <algorithm>  // For std::clamp
#include <functional> // For std::hash

namespace AliveWorld
{

// ============================================================================
// LOGGING MACRO
// ============================================================================

// Use LOG_DEBUG which respects global log level configuration
#define BEHAVIOR_LOG_DEBUG(fmt, ...) \
    LOG_DEBUG("module.alive-world", "[BehaviorExecutor] " fmt, ##__VA_ARGS__)

#define BEHAVIOR_LOG_ERROR(fmt, ...) \
    LOG_ERROR("module.alive-world", "[BehaviorExecutor] " fmt, ##__VA_ARGS__)

// ============================================================================
// CONSTRUCTOR / DESTRUCTOR
// ============================================================================

BehaviorExecutor::BehaviorExecutor()
{
}

BehaviorExecutor::~BehaviorExecutor()
{
    Shutdown();
}

// ============================================================================
// INITIALIZATION
// ============================================================================

void BehaviorExecutor::Initialize(BotMemoryManager* memoryMgr,
                                  WorldStateManager* worldStateMgr,
                                  GossipManager* gossipMgr)
{
    m_memoryManager = memoryMgr;
    m_worldStateManager = worldStateMgr;
    m_gossipManager = gossipMgr;

    LoadConfig();

    LOG_INFO("module.alive-world", "[BehaviorExecutor] Initialized");
}

void BehaviorExecutor::LoadConfig()
{
    m_enabled = sConfigMgr->GetOption<bool>("AliveWorld.BehaviorExecutor.Enable", true);
    m_maxBehaviorsPerTick = sConfigMgr->GetOption<uint32>("AliveWorld.BehaviorExecutor.MaxPerTick", 50);
    m_behaviorCooldownMs = sConfigMgr->GetOption<uint32>("AliveWorld.BehaviorExecutor.CooldownMs", 100);
}

void BehaviorExecutor::Shutdown()
{
    std::lock_guard<std::mutex> lock(m_queueMutex);
    while (!m_queue.empty())
        m_queue.pop();
}

// ============================================================================
// QUEUE BEHAVIOR
// ============================================================================

uint32 BehaviorExecutor::QueueBehavior(const BehaviorRequest& request)
{
    if (!m_enabled)
        return 0;

    BehaviorRequest req = request;
    req.requestId = m_nextRequestId++;
    req.createdAt = GameTime::GetGameTimeMS().count();

    {
        std::lock_guard<std::mutex> lock(m_queueMutex);
        m_queue.push(req);
    }

    BEHAVIOR_LOG_DEBUG("Queued behavior %u type %u for bot %llu",
        req.requestId, static_cast<uint8>(req.type), req.botGuid);

    return req.requestId;
}

// ============================================================================
// CONVENIENCE METHODS - CHAT
// ============================================================================

uint32 BehaviorExecutor::Say(uint64 botGuid, const std::string& message, BehaviorPriority priority)
{
    BehaviorRequest req;
    req.botGuid = botGuid;
    req.type = BehaviorType::CHAT_SAY;
    req.priority = priority;
    req.message = message;
    return QueueBehavior(req);
}

uint32 BehaviorExecutor::Emote(uint64 botGuid, const std::string& emote, BehaviorPriority priority)
{
    BehaviorRequest req;
    req.botGuid = botGuid;
    req.type = BehaviorType::CHAT_EMOTE;
    req.priority = priority;
    req.message = emote;
    return QueueBehavior(req);
}

uint32 BehaviorExecutor::Whisper(uint64 botGuid, uint64 targetGuid, const std::string& message)
{
    BehaviorRequest req;
    req.botGuid = botGuid;
    req.type = BehaviorType::CHAT_WHISPER;
    req.targetGuid = targetGuid;
    req.message = message;
    return QueueBehavior(req);
}

uint32 BehaviorExecutor::SpreadGossip(uint64 botGuid, const std::string& gossip, uint32 gossipId)
{
    BehaviorRequest req;
    req.botGuid = botGuid;
    req.type = BehaviorType::GOSSIP_SPREAD;
    req.message = gossip;
    req.itemId = gossipId; // Reuse itemId for gossipId
    req.priority = BehaviorPriority::LOW;
    return QueueBehavior(req);
}

// ============================================================================
// CONVENIENCE METHODS - MOVEMENT
// ============================================================================

uint32 BehaviorExecutor::MoveToZone(uint64 botGuid, uint32 zoneId, BehaviorPriority priority)
{
    BehaviorRequest req;
    req.botGuid = botGuid;
    req.type = BehaviorType::MOVE_TO_ZONE;
    req.targetZoneId = zoneId;
    req.priority = priority;
    return QueueBehavior(req);
}

uint32 BehaviorExecutor::MoveToPosition(uint64 botGuid, float x, float y, float z)
{
    BehaviorRequest req;
    req.botGuid = botGuid;
    req.type = BehaviorType::MOVE_TO_POSITION;
    req.targetX = x;
    req.targetY = y;
    req.targetZ = z;
    return QueueBehavior(req);
}

uint32 BehaviorExecutor::MoveNear(uint64 botGuid, uint64 targetGuid, float distance)
{
    BehaviorRequest req;
    req.botGuid = botGuid;
    req.type = BehaviorType::MOVE_NEAR_ENTITY;
    req.targetGuid = targetGuid;
    req.targetX = distance; // Reuse for distance
    return QueueBehavior(req);
}

uint32 BehaviorExecutor::Flee(uint64 botGuid, uint64 threatGuid)
{
    BehaviorRequest req;
    req.botGuid = botGuid;
    req.type = BehaviorType::MOVE_FLEE;
    req.targetGuid = threatGuid;
    req.priority = BehaviorPriority::CRITICAL;
    return QueueBehavior(req);
}

uint32 BehaviorExecutor::Follow(uint64 botGuid, uint64 targetGuid)
{
    BehaviorRequest req;
    req.botGuid = botGuid;
    req.type = BehaviorType::MOVE_FOLLOW;
    req.targetGuid = targetGuid;
    return QueueBehavior(req);
}

// ============================================================================
// CONVENIENCE METHODS - COMBAT
// ============================================================================

uint32 BehaviorExecutor::SetCombatStyle(uint64 botGuid, CombatStyle style)
{
    BehaviorRequest req;
    req.botGuid = botGuid;
    req.type = BehaviorType::COMBAT_SET_STYLE;
    req.spellId = static_cast<uint32>(style); // Reuse spellId for style
    return QueueBehavior(req);
}

uint32 BehaviorExecutor::SetTarget(uint64 botGuid, uint64 targetGuid)
{
    BehaviorRequest req;
    req.botGuid = botGuid;
    req.type = BehaviorType::COMBAT_SET_TARGET;
    req.targetGuid = targetGuid;
    req.priority = BehaviorPriority::HIGH;
    return QueueBehavior(req);
}

uint32 BehaviorExecutor::UseAbility(uint64 botGuid, uint32 spellId, uint64 targetGuid)
{
    BehaviorRequest req;
    req.botGuid = botGuid;
    req.type = BehaviorType::COMBAT_USE_ABILITY;
    req.spellId = spellId;
    req.targetGuid = targetGuid;
    req.priority = BehaviorPriority::HIGH;
    return QueueBehavior(req);
}

uint32 BehaviorExecutor::CallForHelp(uint64 botGuid)
{
    BehaviorRequest req;
    req.botGuid = botGuid;
    req.type = BehaviorType::COMBAT_CALL_HELP;
    req.priority = BehaviorPriority::HIGH;
    return QueueBehavior(req);
}

// ============================================================================
// CONVENIENCE METHODS - ECONOMIC
// ============================================================================

uint32 BehaviorExecutor::OfferTrade(uint64 botGuid, uint64 targetGuid, uint32 itemId, uint32 quantity)
{
    BehaviorRequest req;
    req.botGuid = botGuid;
    req.type = BehaviorType::ECON_TRADE_OFFER;
    req.targetGuid = targetGuid;
    req.itemId = itemId;
    req.quantity = quantity;
    return QueueBehavior(req);
}

uint32 BehaviorExecutor::ListOnAH(uint64 botGuid, uint32 itemId, uint32 quantity, uint32 buyoutGold)
{
    BehaviorRequest req;
    req.botGuid = botGuid;
    req.type = BehaviorType::ECON_AH_LIST;
    req.itemId = itemId;
    req.quantity = quantity;
    req.gold = buyoutGold;
    return QueueBehavior(req);
}

// ============================================================================
// CONVENIENCE METHODS - SOCIAL
// ============================================================================

uint32 BehaviorExecutor::RequestGroup(uint64 botGuid, uint64 leaderGuid)
{
    BehaviorRequest req;
    req.botGuid = botGuid;
    req.type = BehaviorType::SOCIAL_REQUEST_GROUP;
    req.targetGuid = leaderGuid;
    return QueueBehavior(req);
}

uint32 BehaviorExecutor::InviteToGroup(uint64 botGuid, uint64 targetGuid)
{
    BehaviorRequest req;
    req.botGuid = botGuid;
    req.type = BehaviorType::SOCIAL_INVITE_GROUP;
    req.targetGuid = targetGuid;
    return QueueBehavior(req);
}

uint32 BehaviorExecutor::LeaveGroup(uint64 botGuid)
{
    BehaviorRequest req;
    req.botGuid = botGuid;
    req.type = BehaviorType::SOCIAL_LEAVE_GROUP;
    return QueueBehavior(req);
}

// ============================================================================
// CANCEL BEHAVIOR
// ============================================================================
// Cancels a pending behavior request by ID.
//
// Implementation Note:
// -------------------
// std::priority_queue doesn't support removal, so we use a "lazy deletion"
// approach: cancelled IDs are tracked in m_cancelledRequests set, and
// skipped during Update() when they're popped from the queue.
//
// This is efficient for sparse cancellations (common case) because:
// - Cancel is O(1) - just insert into set
// - Cleanup is amortized O(1) - done during normal queue processing
// - Memory overhead is minimal for sparse cancellations
//
// For frequent cancellations, consider switching to std::set with
// custom comparator (O(log n) insertion/removal but O(log n) cancel).
// ============================================================================

bool BehaviorExecutor::CancelBehavior(uint32 requestId)
{
    if (requestId == 0)
    {
        BEHAVIOR_LOG_DEBUG("CancelBehavior: Invalid request ID 0");
        return false;
    }

    {
        std::lock_guard<std::mutex> lock(m_queueMutex);

        // Check if already cancelled
        if (m_cancelledRequests.count(requestId) > 0)
        {
            BEHAVIOR_LOG_DEBUG("CancelBehavior: Request %u already cancelled", requestId);
            return true;
        }

        // Mark as cancelled - will be skipped during Update()
        m_cancelledRequests.insert(requestId);
    }

    m_cancelledCount++;
    BEHAVIOR_LOG_DEBUG("CancelBehavior: Request %u marked for cancellation", requestId);
    return true;
}

// ============================================================================
// UPDATE
// ============================================================================
// Main processing loop - called every tick from AliveWorldManager::Update().
//
// Processing Flow:
// 1. Pop highest priority request from queue
// 2. Check if request was cancelled (skip if so)
// 3. Check per-bot cooldown (re-queue if too soon)
// 4. Execute the behavior via appropriate handler
// 5. Update statistics and notify callback
//
// Performance:
// - Processes up to m_maxBehaviorsPerTick per call (default: 50)
// - Per-bot cooldown prevents spam (default: 100ms)
// - Cancelled requests are cleaned up during processing
// ============================================================================

void BehaviorExecutor::Update(uint32 /*diff*/)
{
    if (!m_enabled)
        return;

    uint32 processed = 0;
    uint64 now = GameTime::GetGameTimeMS().count();

    while (processed < m_maxBehaviorsPerTick)
    {
        BehaviorRequest request;
        bool wasCancelled = false;

        {
            std::lock_guard<std::mutex> lock(m_queueMutex);
            if (m_queue.empty())
                break;

            request = m_queue.top();
            m_queue.pop();

            // Check if this request was cancelled
            auto cancelIt = m_cancelledRequests.find(request.requestId);
            if (cancelIt != m_cancelledRequests.end())
            {
                // Remove from cancelled set (cleanup)
                m_cancelledRequests.erase(cancelIt);
                wasCancelled = true;
            }
        }

        // Skip cancelled requests
        if (wasCancelled)
        {
            BEHAVIOR_LOG_DEBUG("Update: Skipping cancelled request %u", request.requestId);
            NotifyCompletion(request, false, "Cancelled");
            continue;
        }

        // Check cooldown for this bot
        auto cooldownIt = m_lastBehaviorTime.find(request.botGuid);
        if (cooldownIt != m_lastBehaviorTime.end())
        {
            if (now - cooldownIt->second < m_behaviorCooldownMs)
            {
                // Re-queue for later
                std::lock_guard<std::mutex> lock(m_queueMutex);
                m_queue.push(request);
                continue;
            }
        }

        // Execute the behavior
        bool success = ExecuteBehavior(request);

        // Update stats
        if (success)
            m_executedCount++;
        else
            m_failedCount++;

        // Update cooldown
        m_lastBehaviorTime[request.botGuid] = now;

        // Notify callback
        NotifyCompletion(request, success, success ? "OK" : "Failed");

        processed++;
    }
}

uint32 BehaviorExecutor::GetPendingCount() const
{
    std::lock_guard<std::mutex> lock(m_queueMutex);
    return static_cast<uint32>(m_queue.size());
}

// ============================================================================
// EXECUTE BEHAVIOR
// ============================================================================

bool BehaviorExecutor::ExecuteBehavior(const BehaviorRequest& request)
{
    Player* bot = ObjectAccessor::FindPlayer(ObjectGuid(request.botGuid));
    if (!bot)
    {
        BEHAVIOR_LOG_DEBUG("Bot %llu not found", request.botGuid);
        return false;
    }

    PlayerbotAI* ai = GetPlayerbotAI(bot);
    if (!ai)
    {
        BEHAVIOR_LOG_DEBUG("Bot %s has no PlayerbotAI", bot->GetName().c_str());
        return false;
    }

    if (!CanExecuteBehavior(bot, request))
    {
        BEHAVIOR_LOG_DEBUG("Bot %s cannot execute behavior type %u",
            bot->GetName().c_str(), static_cast<uint8>(request.type));
        return false;
    }

    // Route to appropriate handler
    uint8 typeVal = static_cast<uint8>(request.type);

    if (typeVal <= static_cast<uint8>(BehaviorType::GOSSIP_SPREAD))
        return ExecuteChatBehavior(bot, ai, request);

    if (typeVal <= static_cast<uint8>(BehaviorType::MOVE_FOLLOW))
        return ExecuteMovementBehavior(bot, ai, request);

    if (typeVal <= static_cast<uint8>(BehaviorType::COMBAT_TAUNT))
        return ExecuteCombatBehavior(bot, ai, request);

    if (typeVal <= static_cast<uint8>(BehaviorType::ECON_VENDOR_BUY))
        return ExecuteEconomicBehavior(bot, ai, request);

    if (typeVal <= static_cast<uint8>(BehaviorType::SOCIAL_GUILD_CHAT))
        return ExecuteSocialBehavior(bot, ai, request);

    return ExecuteMetaBehavior(bot, ai, request);
}

// ============================================================================
// CHAT BEHAVIORS
// ============================================================================

bool BehaviorExecutor::ExecuteChatBehavior(Player* bot, PlayerbotAI* ai, const BehaviorRequest& request)
{
    switch (request.type)
    {
        case BehaviorType::CHAT_SAY:
            ai->Say(request.message);
            return true;

        case BehaviorType::CHAT_YELL:
            ai->Yell(request.message);
            return true;

        case BehaviorType::CHAT_EMOTE:
        {
            // Try to find text emote by name
            uint32 emoteId = 0;
            std::string emoteName = request.message;
            std::transform(emoteName.begin(), emoteName.end(), emoteName.begin(), ::tolower);

            // Map common emote names to IDs
            static const std::unordered_map<std::string, uint32> emoteMap = {
                {"wave", TEXT_EMOTE_WAVE},
                {"bow", TEXT_EMOTE_BOW},
                {"dance", TEXT_EMOTE_DANCE},
                {"cheer", TEXT_EMOTE_CHEER},
                {"laugh", TEXT_EMOTE_LAUGH},
                {"cry", TEXT_EMOTE_CRY},
                {"salute", TEXT_EMOTE_SALUTE},
                {"roar", TEXT_EMOTE_ROAR},
                {"cower", TEXT_EMOTE_COWER},
                {"threaten", TEXT_EMOTE_THREATEN},
                {"sigh", TEXT_EMOTE_SIGH},
                {"thank", TEXT_EMOTE_THANK},
                {"point", TEXT_EMOTE_POINT},
                {"flex", TEXT_EMOTE_FLEX},
                {"apologize", TEXT_EMOTE_APOLOGIZE},
            };

            auto it = emoteMap.find(emoteName);
            if (it != emoteMap.end())
                emoteId = it->second;

            if (emoteId > 0)
            {
                bot->HandleEmoteCommand(emoteId);
                return true;
            }

            // Fallback: do /me text
            ai->Say("/me " + request.message);
            return true;
        }

        case BehaviorType::CHAT_WHISPER:
        {
            Player* target = ObjectAccessor::FindPlayer(ObjectGuid(request.targetGuid));
            if (!target)
                return false;
            ai->Whisper(request.message, target->GetName());
            return true;
        }

        case BehaviorType::CHAT_PARTY:
            if (!bot->GetGroup())
                return false;
            ai->SayToParty(request.message);
            return true;

        case BehaviorType::CHAT_GUILD:
            if (!bot->GetGuild())
                return false;
            ai->SayToGuild(request.message);
            return true;

        case BehaviorType::GOSSIP_SPREAD:
        {
            // Generate gossip ID from message hash for memory tracking
            uint32 gossipId = static_cast<uint32>(std::hash<std::string>{}(request.message) & 0xFFFFFFFF);

            // Find nearby bots to tell (who don't already know this gossip)
            std::vector<Player*> nearbyBots;
            float range = 50.0f;

            for (auto& pair : bot->GetMap()->GetPlayers())
            {
                Player* other = pair.GetSource();
                if (other == bot)
                    continue;

                if (!GetPlayerbotAI(other))
                    continue; // Only spread to other bots

                if (!bot->IsWithinDist(other, range, false))
                    continue;

                // Check if this bot already knows the gossip
                if (m_memoryManager && m_memoryManager->KnowsGossip(other->GetGUID().GetRawValue(), gossipId))
                    continue; // Skip bots who already know this gossip

                nearbyBots.push_back(other);
            }

            if (nearbyBots.empty())
            {
                BEHAVIOR_LOG_DEBUG("Bot %s: No nearby bots to spread gossip to (all know it or none nearby)",
                    bot->GetName().c_str());
                return false;
            }

            // Pick a random bot to tell
            Player* target = nearbyBots[urand(0, nearbyBots.size() - 1)];

            // Say the gossip
            ai->Say(request.message);

            // Record that target now knows this gossip
            if (m_memoryManager)
            {
                m_memoryManager->LearnGossip(target->GetGUID().GetRawValue(), gossipId);
            }

            BEHAVIOR_LOG_DEBUG("Bot %s spread gossip (id=%u) to %s: %.50s...",
                bot->GetName().c_str(), gossipId, target->GetName().c_str(), request.message.c_str());

            return true;
        }

        default:
            return false;
    }
}

// ============================================================================
// MOVEMENT BEHAVIORS
// ============================================================================

bool BehaviorExecutor::ExecuteMovementBehavior(Player* bot, PlayerbotAI* ai, const BehaviorRequest& request)
{
    switch (request.type)
    {
        case BehaviorType::MOVE_TO_ZONE:
        {
            // Get a position in the target zone
            // This is simplified - a full implementation would find safe spots
            AreaTableEntry const* area = sAreaTableStore.LookupEntry(request.targetZoneId);
            if (!area)
                return false;

            // For now, just set the bot's intention - actual teleport would need
            // either GM commands or navigation
            BEHAVIOR_LOG_DEBUG("Bot %s wants to move to zone %u (%s)",
                bot->GetName().c_str(), request.targetZoneId, area->area_name[0]);

            // Use playerbots teleport if available
            // ai->TeleportTo(zonePosition);

            return true;
        }

        case BehaviorType::MOVE_TO_POSITION:
        {
            // Validate coordinates
            if (request.targetX == 0.0f && request.targetY == 0.0f && request.targetZ == 0.0f)
            {
                BEHAVIOR_LOG_DEBUG("Bot %s: Invalid position (0,0,0) for MOVE_TO_POSITION", bot->GetName().c_str());
                return false;
            }

            // Check if already close enough (within 3 yards)
            float distance = bot->GetDistance(request.targetX, request.targetY, request.targetZ);
            if (distance < 3.0f)
            {
                BEHAVIOR_LOG_DEBUG("Bot %s: Already at target position (dist=%.1f)", bot->GetName().c_str(), distance);
                return true;
            }

            // Use MotionMaster to move to position
            // Point ID 1 is arbitrary, used for tracking movement completion
            bot->GetMotionMaster()->MovePoint(1, request.targetX, request.targetY, request.targetZ);

            BEHAVIOR_LOG_DEBUG("Bot %s moving to position (%.1f, %.1f, %.1f), distance=%.1f",
                bot->GetName().c_str(), request.targetX, request.targetY, request.targetZ, distance);
            return true;
        }

        case BehaviorType::MOVE_NEAR_ENTITY:
        {
            // Try to find target as player first, then as any unit
            Unit* target = ObjectAccessor::FindPlayer(ObjectGuid(request.targetGuid));
            if (!target)
                target = ObjectAccessor::GetUnit(*bot, ObjectGuid(request.targetGuid));

            if (!target)
            {
                BEHAVIOR_LOG_DEBUG("Bot %s: Target not found for MOVE_NEAR_ENTITY", bot->GetName().c_str());
                return false;
            }

            float desiredDistance = request.targetX > 0 ? request.targetX : 5.0f;

            // Check if already close enough
            float currentDistance = bot->GetDistance(target);
            if (currentDistance <= desiredDistance + 1.0f)
            {
                BEHAVIOR_LOG_DEBUG("Bot %s: Already near %s (dist=%.1f, desired=%.1f)",
                    bot->GetName().c_str(), target->GetName().c_str(), currentDistance, desiredDistance);
                return true;
            }

            // Calculate position near target
            float angle = target->GetAngle(bot); // Angle from target to bot
            float x = target->GetPositionX() + cos(angle) * desiredDistance;
            float y = target->GetPositionY() + sin(angle) * desiredDistance;
            float z = target->GetPositionZ();

            bot->GetMotionMaster()->MovePoint(2, x, y, z);

            BEHAVIOR_LOG_DEBUG("Bot %s moving near %s (dist=%.1f -> %.1f)",
                bot->GetName().c_str(), target->GetName().c_str(), currentDistance, desiredDistance);
            return true;
        }

        case BehaviorType::MOVE_FLEE:
        {
            // Calculate direction away from threat
            Unit* threat = nullptr;
            if (request.targetGuid)
                threat = ObjectAccessor::GetUnit(*bot, ObjectGuid(request.targetGuid));

            if (!threat)
                threat = bot->GetVictim();

            if (!threat)
            {
                BEHAVIOR_LOG_DEBUG("Bot %s: No threat to flee from", bot->GetName().c_str());
                return false;
            }

            // Calculate flee position - move away from threat
            float fleeDistance = 20.0f; // Default flee distance
            float angle = threat->GetAngle(bot); // Angle from threat to bot (direction we should flee)
            float x = bot->GetPositionX() + cos(angle) * fleeDistance;
            float y = bot->GetPositionY() + sin(angle) * fleeDistance;
            float z = bot->GetPositionZ();

            // Update Z to ground level if possible
            bot->UpdateGroundPositionZ(x, y, z);

            bot->GetMotionMaster()->MovePoint(3, x, y, z);

            BEHAVIOR_LOG_DEBUG("Bot %s fleeing from %s (%.1f yards away)",
                bot->GetName().c_str(), threat->GetName().c_str(), fleeDistance);
            return true;
        }

        case BehaviorType::MOVE_PATROL:
        {
            // Use playerbots' "move random" action for patrol behavior
            // This makes the bot move to random nearby positions
            bool result = ai->DoSpecificAction("move random", Event(), true);

            if (result)
            {
                BEHAVIOR_LOG_DEBUG("Bot %s starting patrol (move random)", bot->GetName().c_str());
            }
            else
            {
                // Fallback: move to a random position near current location
                float angle = frand(0, 2 * M_PI);
                float distance = frand(5.0f, 15.0f);
                float x = bot->GetPositionX() + cos(angle) * distance;
                float y = bot->GetPositionY() + sin(angle) * distance;
                float z = bot->GetPositionZ();

                bot->UpdateGroundPositionZ(x, y, z);
                bot->GetMotionMaster()->MovePoint(4, x, y, z);

                BEHAVIOR_LOG_DEBUG("Bot %s patrol fallback - moving random direction", bot->GetName().c_str());
                result = true;
            }

            return result;
        }

        case BehaviorType::MOVE_FOLLOW:
        {
            // Try to find target as player first, then as any unit
            Unit* target = ObjectAccessor::FindPlayer(ObjectGuid(request.targetGuid));
            if (!target)
                target = ObjectAccessor::GetUnit(*bot, ObjectGuid(request.targetGuid));

            if (!target)
            {
                BEHAVIOR_LOG_DEBUG("Bot %s: Target not found for MOVE_FOLLOW", bot->GetName().c_str());
                return false;
            }

            // Use targetX as follow distance if provided, otherwise default to 5 yards
            float followDistance = request.targetX > 0 ? request.targetX : 5.0f;

            // Use MotionMaster's MoveFollow for persistent following
            // Second param is distance, third is angle (behind the target)
            bot->GetMotionMaster()->MoveFollow(target, followDistance, M_PI); // Follow behind

            BEHAVIOR_LOG_DEBUG("Bot %s now following %s at %.1f yards",
                bot->GetName().c_str(), target->GetName().c_str(), followDistance);
            return true;
        }

        default:
            return false;
    }
}

// ============================================================================
// COMBAT BEHAVIORS
// ============================================================================

bool BehaviorExecutor::ExecuteCombatBehavior(Player* bot, PlayerbotAI* ai, const BehaviorRequest& request)
{
    switch (request.type)
    {
        case BehaviorType::COMBAT_SET_STYLE:
        {
            CombatStyle style = static_cast<CombatStyle>(request.spellId);

            // Translate to playerbots strategy
            switch (style)
            {
                case CombatStyle::AGGRESSIVE:
                    ai->ChangeStrategy("+aggressive,-passive,-defensive", BOT_STATE_COMBAT);
                    break;
                case CombatStyle::DEFENSIVE:
                    ai->ChangeStrategy("+defensive,-aggressive,-passive", BOT_STATE_COMBAT);
                    break;
                case CombatStyle::PASSIVE:
                    ai->ChangeStrategy("+passive,-aggressive,-defensive", BOT_STATE_COMBAT);
                    break;
                case CombatStyle::BERSERKER:
                    ai->ChangeStrategy("+aggressive,+dps,-tank,-heal", BOT_STATE_COMBAT);
                    break;
                case CombatStyle::CAUTIOUS:
                    ai->ChangeStrategy("+defensive,+flee,-aggressive", BOT_STATE_COMBAT);
                    break;
            }

            // Update bot memory with combat style preferences
            if (m_memoryManager)
            {
                uint64 botGuid = bot->GetGUID().GetRawValue();
                BotPersonality& personality = m_memoryManager->GetPersonality(botGuid);

                // Adjust personality traits based on chosen combat style
                switch (style)
                {
                    case CombatStyle::AGGRESSIVE:
                        personality.aggression = std::min(1.0f, personality.aggression + 0.1f);
                        personality.caution = std::max(0.0f, personality.caution - 0.05f);
                        break;
                    case CombatStyle::DEFENSIVE:
                        personality.caution = std::min(1.0f, personality.caution + 0.1f);
                        personality.aggression = std::max(0.0f, personality.aggression - 0.05f);
                        break;
                    case CombatStyle::PASSIVE:
                        personality.aggression = std::max(0.0f, personality.aggression - 0.1f);
                        personality.caution = std::min(1.0f, personality.caution + 0.1f);
                        break;
                    case CombatStyle::BERSERKER:
                        personality.aggression = std::min(1.0f, personality.aggression + 0.15f);
                        personality.caution = std::max(0.0f, personality.caution - 0.1f);
                        break;
                    case CombatStyle::CAUTIOUS:
                        personality.caution = std::min(1.0f, personality.caution + 0.15f);
                        personality.aggression = std::max(0.0f, personality.aggression - 0.1f);
                        break;
                }

                BEHAVIOR_LOG_DEBUG("Bot %s combat style set to %d (aggression=%.2f, caution=%.2f)",
                    bot->GetName().c_str(), static_cast<int>(style),
                    personality.aggression, personality.caution);
            }

            return true;
        }

        case BehaviorType::COMBAT_SET_TARGET:
        {
            Unit* target = ObjectAccessor::GetUnit(*bot, ObjectGuid(request.targetGuid));
            if (!target)
                return false;

            // Set bot's target via standard WoW mechanism
            bot->SetSelection(target->GetGUID());
            // Note: PlayerbotAI doesn't have SetCurrentTarget, using bot's SetSelection instead
            return true;
        }

        case BehaviorType::COMBAT_USE_ABILITY:
        {
            SpellInfo const* spellInfo = sSpellMgr->GetSpellInfo(request.spellId);
            if (!spellInfo)
                return false;

            Unit* target = nullptr;
            if (request.targetGuid)
                target = ObjectAccessor::GetUnit(*bot, ObjectGuid(request.targetGuid));
            else
                target = bot->GetVictim();

            if (!target)
                target = bot; // Self-cast

            return ai->CastSpell(request.spellId, target);
        }

        case BehaviorType::COMBAT_FLEE:
        {
            // Disengage from combat
            bot->CombatStop(true);

            // Then flee
            BehaviorRequest fleeReq;
            fleeReq.type = BehaviorType::MOVE_FLEE;
            fleeReq.botGuid = request.botGuid;
            return ExecuteMovementBehavior(bot, ai, fleeReq);
        }

        case BehaviorType::COMBAT_CALL_HELP:
        {
            // Yell for help
            ai->Yell("Help! I'm under attack!");

            // Alert nearby allies
            float range = 40.0f;
            for (auto& pair : bot->GetMap()->GetPlayers())
            {
                Player* other = pair.GetSource();
                if (other == bot)
                    continue;

                PlayerbotAI* otherAI = GetPlayerbotAI(other);
                if (!otherAI)
                    continue;

                if (!bot->IsWithinDist(other, range, false))
                    continue;

                // Same faction check
                if (bot->GetTeamId() != other->GetTeamId())
                    continue;

                // Alert this bot to assist
                if (bot->GetVictim())
                {
                    // Set the other bot's target to help
                    other->SetSelection(bot->GetVictim()->GetGUID());
                    BEHAVIOR_LOG_DEBUG("Bot %s responding to %s's call for help",
                        other->GetName().c_str(), bot->GetName().c_str());
                }
            }

            return true;
        }

        case BehaviorType::COMBAT_TAUNT:
        {
            // Use taunt ability if available
            // This is class-specific
            uint32 tauntSpell = 0;
            switch (bot->getClass())
            {
                case CLASS_WARRIOR: tauntSpell = 355; break;   // Taunt
                case CLASS_PALADIN: tauntSpell = 62124; break; // Hand of Reckoning
                case CLASS_DRUID:   tauntSpell = 6795; break;  // Growl
                case CLASS_DEATH_KNIGHT: tauntSpell = 56222; break; // Dark Command
            }

            if (tauntSpell)
            {
                Unit* target = bot->GetVictim();
                if (target)
                    return ai->CastSpell(tauntSpell, target);
            }

            return false;
        }

        default:
            return false;
    }
}

// ============================================================================
// ECONOMIC BEHAVIORS
// ============================================================================

bool BehaviorExecutor::ExecuteEconomicBehavior(Player* bot, PlayerbotAI* ai, const BehaviorRequest& request)
{
    switch (request.type)
    {
        case BehaviorType::ECON_GATHER:
        {
            // Use playerbots gathering actions
            // First, try to reveal nearby gathering nodes
            bool revealed = ai->DoSpecificAction("reveal gathering item", Event(), true);

            if (revealed)
            {
                BEHAVIOR_LOG_DEBUG("Bot %s revealed gathering nodes, now moving to loot",
                    bot->GetName().c_str());

                // Then move to and loot the node
                bool movedToLoot = ai->DoSpecificAction("move to loot", Event(), true);
                if (movedToLoot)
                {
                    // Add loot to our gathering targets
                    ai->DoSpecificAction("add gathering loot", Event(), true);
                    BEHAVIOR_LOG_DEBUG("Bot %s gathering node", bot->GetName().c_str());
                    return true;
                }
            }

            // Fallback: try to find any lootable object nearby
            bool anyLoot = ai->DoSpecificAction("add all loot", Event(), true);
            if (anyLoot)
            {
                ai->DoSpecificAction("move to loot", Event(), true);
                ai->DoSpecificAction("open loot", Event(), true);
                BEHAVIOR_LOG_DEBUG("Bot %s found and looted nearby object", bot->GetName().c_str());
                return true;
            }

            BEHAVIOR_LOG_DEBUG("Bot %s: No gathering nodes found nearby", bot->GetName().c_str());
            return false;
        }

        case BehaviorType::ECON_CRAFT:
        {
            // Use playerbots crafting actions
            // If we have a specific item ID, try to craft it
            if (request.itemId > 0)
            {
                // Build craft command with item ID
                // Playerbots uses "craft <itemlink>" or "craft random item"
                std::string craftCmd = "craft " + std::to_string(request.itemId);

                // Try the rpg craft action which handles finding recipes
                bool crafted = ai->DoSpecificAction("rpg craft", Event(), true);
                if (crafted)
                {
                    BEHAVIOR_LOG_DEBUG("Bot %s crafting via rpg craft", bot->GetName().c_str());
                    return true;
                }
            }

            // Fallback: use random recipe if bot has one
            bool usedRecipe = ai->DoSpecificAction("use random recipe", Event(), true);
            if (usedRecipe)
            {
                BEHAVIOR_LOG_DEBUG("Bot %s used random recipe", bot->GetName().c_str());
                return true;
            }

            // Last resort: craft random item
            bool craftedRandom = ai->DoSpecificAction("craft random item", Event(), true);
            if (craftedRandom)
            {
                BEHAVIOR_LOG_DEBUG("Bot %s crafted random item", bot->GetName().c_str());
                return true;
            }

            BEHAVIOR_LOG_DEBUG("Bot %s: Unable to craft (no recipes/materials)", bot->GetName().c_str());
            return false;
        }

        case BehaviorType::ECON_TRADE_OFFER:
        {
            Player* target = ObjectAccessor::FindPlayer(ObjectGuid(request.targetGuid));
            if (!target)
            {
                BEHAVIOR_LOG_DEBUG("Bot %s: Trade target not found", bot->GetName().c_str());
                return false;
            }

            // Use rpg trade action to offer useful items
            bool traded = ai->DoSpecificAction("rpg trade useful", Event(), true);
            if (traded)
            {
                BEHAVIOR_LOG_DEBUG("Bot %s offered trade to %s",
                    bot->GetName().c_str(), target->GetName().c_str());
                return true;
            }

            // Alternative: suggest trade via chat
            ai->Whisper("Would you like to trade?", target->GetName());
            BEHAVIOR_LOG_DEBUG("Bot %s suggested trade to %s",
                bot->GetName().c_str(), target->GetName().c_str());
            return true;
        }

        case BehaviorType::ECON_TRADE_ACCEPT:
        {
            // Accept pending trade if any
            // This is typically handled automatically by playerbots
            BEHAVIOR_LOG_DEBUG("Bot %s: Trade accept (handled by playerbots)", bot->GetName().c_str());
            return true;
        }

        case BehaviorType::ECON_AH_LIST:
        {
            // Bot needs to be at an auctioneer
            // Use rpg system to find and interact with auctioneer
            // First check if we're already at an NPC
            bool atNpc = ai->DoSpecificAction("rpg", Event(), true);

            // The actual AH interaction requires being near an auctioneer
            // For now, log intent and let RPG system handle it
            BEHAVIOR_LOG_DEBUG("Bot %s wants to list item %u on AH (quantity: %u, price: %u gold)",
                bot->GetName().c_str(), request.itemId, request.quantity, request.gold);

            // Enable AH strategy if not already
            ai->ChangeStrategy("+ah", BOT_STATE_NON_COMBAT);
            return true;
        }

        case BehaviorType::ECON_AH_BUY:
        {
            BEHAVIOR_LOG_DEBUG("Bot %s wants to buy item %u from AH",
                bot->GetName().c_str(), request.itemId);

            // Enable AH strategy
            ai->ChangeStrategy("+ah", BOT_STATE_NON_COMBAT);
            return true;
        }

        case BehaviorType::ECON_VENDOR_SELL:
        {
            // Use rpg sell action - bot will find nearest vendor
            bool sold = ai->DoSpecificAction("rpg sell", Event(), true);
            if (sold)
            {
                BEHAVIOR_LOG_DEBUG("Bot %s selling items to vendor", bot->GetName().c_str());
                return true;
            }

            // If not near vendor, try to move to one via rpg system
            ai->DoSpecificAction("choose rpg target", Event(), true);
            ai->DoSpecificAction("move to rpg target", Event(), true);
            BEHAVIOR_LOG_DEBUG("Bot %s moving to vendor to sell", bot->GetName().c_str());
            return true;
        }

        case BehaviorType::ECON_VENDOR_BUY:
        {
            // Use rpg buy action
            bool bought = ai->DoSpecificAction("rpg buy", Event(), true);
            if (bought)
            {
                BEHAVIOR_LOG_DEBUG("Bot %s bought items from vendor", bot->GetName().c_str());
                return true;
            }

            // If not near vendor, move to one
            ai->DoSpecificAction("choose rpg target", Event(), true);
            ai->DoSpecificAction("move to rpg target", Event(), true);
            BEHAVIOR_LOG_DEBUG("Bot %s moving to vendor to buy", bot->GetName().c_str());
            return true;
        }

        default:
            return false;
    }
}

// ============================================================================
// SOCIAL BEHAVIORS
// ============================================================================

bool BehaviorExecutor::ExecuteSocialBehavior(Player* bot, PlayerbotAI* ai, const BehaviorRequest& request)
{
    switch (request.type)
    {
        case BehaviorType::SOCIAL_REQUEST_GROUP:
        {
            Player* leader = ObjectAccessor::FindPlayer(ObjectGuid(request.targetGuid));
            if (!leader)
                return false;

            // Bot asks to join
            ai->Whisper("Hey, can I join your group?", leader->GetName());

            // The actual join would happen if the player accepts
            return true;
        }

        case BehaviorType::SOCIAL_INVITE_GROUP:
        {
            Player* target = ObjectAccessor::FindPlayer(ObjectGuid(request.targetGuid));
            if (!target)
                return false;

            // Create group if needed
            Group* group = bot->GetGroup();
            if (!group)
            {
                group = new Group();
                group->Create(bot);
                // Group is auto-managed by the core
            }

            // Invite target
            group->AddInvite(target);
            WorldPacket data;
            // ... build invite packet

            return true;
        }

        case BehaviorType::SOCIAL_LEAVE_GROUP:
        {
            Group* group = bot->GetGroup();
            if (!group)
                return false;

            group->RemoveMember(bot->GetGUID());
            return true;
        }

        case BehaviorType::SOCIAL_DUEL_REQUEST:
        {
            Player* target = ObjectAccessor::FindPlayer(ObjectGuid(request.targetGuid));
            if (!target)
            {
                BEHAVIOR_LOG_DEBUG("Bot %s: Duel target not found", bot->GetName().c_str());
                return false;
            }

            // Check if target is valid for dueling
            if (target->IsInCombat() || target->InBattleground() || target->InArena())
            {
                BEHAVIOR_LOG_DEBUG("Bot %s: Target %s not available for duel",
                    bot->GetName().c_str(), target->GetName().c_str());
                return false;
            }

            // Use playerbots rpg duel action
            bool dueled = ai->DoSpecificAction("rpg duel", Event(), true);
            if (dueled)
            {
                BEHAVIOR_LOG_DEBUG("Bot %s challenged %s to a duel via rpg duel",
                    bot->GetName().c_str(), target->GetName().c_str());
                return true;
            }

            // Fallback: Set target and emote a challenge
            bot->SetSelection(target->GetGUID());
            ai->Say("I challenge you to a duel!");

            // Request duel via game mechanics
            bot->CastSpell(target, 7266, false); // Duel spell ID

            BEHAVIOR_LOG_DEBUG("Bot %s challenged %s to a duel",
                bot->GetName().c_str(), target->GetName().c_str());
            return true;
        }

        case BehaviorType::SOCIAL_GUILD_CHAT:
        {
            if (!bot->GetGuild())
                return false;

            ai->SayToGuild(request.message);
            return true;
        }

        default:
            return false;
    }
}

// ============================================================================
// META BEHAVIORS
// ============================================================================

bool BehaviorExecutor::ExecuteMetaBehavior(Player* bot, PlayerbotAI* /*ai*/, const BehaviorRequest& request)
{
    if (!m_memoryManager)
        return false;

    auto* memory = m_memoryManager->GetBotMemory(bot->GetGUID().GetRawValue());
    if (!memory)
        return false;

    switch (request.type)
    {
        case BehaviorType::META_CHANGE_GOAL:
        {
            // Store goal as AI decision for persistence
            std::string goalRecord = "GOAL: " + request.message;
            m_memoryManager->RecordAIDecision(bot->GetGUID().GetRawValue(), goalRecord);

            BEHAVIOR_LOG_DEBUG("Bot %s new goal set: %s", bot->GetName().c_str(), request.message.c_str());
            return true;
        }

        case BehaviorType::META_ADAPT_PERSONALITY:
        {
            // Parse personality changes from message
            // Format: "aggression:+0.1,caution:-0.05,sociability:+0.03"
            BotPersonality& personality = m_memoryManager->GetPersonality(bot->GetGUID().GetRawValue());

            // Parse comma-separated trait:value pairs
            std::string input = request.message;
            size_t pos = 0;
            while ((pos = input.find(',')) != std::string::npos || !input.empty())
            {
                std::string token = (pos != std::string::npos) ? input.substr(0, pos) : input;

                // Find the colon separator
                size_t colonPos = token.find(':');
                if (colonPos != std::string::npos)
                {
                    std::string trait = token.substr(0, colonPos);
                    std::string valueStr = token.substr(colonPos + 1);

                    // Parse the value (supports +0.1, -0.05, or just 0.5)
                    float delta = 0.0f;
                    try {
                        delta = std::stof(valueStr);
                    } catch (...) {
                        BEHAVIOR_LOG_ERROR("Bot %s: Failed to parse personality value: %s",
                            bot->GetName().c_str(), valueStr.c_str());
                    }

                    // Apply to the appropriate trait
                    if (trait == "aggression")
                        personality.aggression = std::clamp(personality.aggression + delta, 0.0f, 1.0f);
                    else if (trait == "caution")
                        personality.caution = std::clamp(personality.caution + delta, 0.0f, 1.0f);
                    else if (trait == "sociability")
                        personality.sociability = std::clamp(personality.sociability + delta, 0.0f, 1.0f);
                    else if (trait == "loyalty")
                        personality.loyalty = std::clamp(personality.loyalty + delta, 0.0f, 1.0f);
                    else if (trait == "curiosity")
                        personality.curiosity = std::clamp(personality.curiosity + delta, 0.0f, 1.0f);
                    else if (trait == "vengefulness")
                        personality.vengefulness = std::clamp(personality.vengefulness + delta, 0.0f, 1.0f);
                    else if (trait == "helpfulness")
                        personality.helpfulness = std::clamp(personality.helpfulness + delta, 0.0f, 1.0f);
                    else if (trait == "greed")
                        personality.greed = std::clamp(personality.greed + delta, 0.0f, 1.0f);
                }

                if (pos == std::string::npos)
                    break;
                input.erase(0, pos + 1);
            }

            BEHAVIOR_LOG_DEBUG("Bot %s personality adapted: aggr=%.2f caut=%.2f soci=%.2f",
                bot->GetName().c_str(), personality.aggression, personality.caution, personality.sociability);
            return true;
        }

        case BehaviorType::META_REMEMBER_EVENT:
        {
            // Store significant event as AI decision (provides persistent memory)
            std::string eventRecord = "EVENT: " + request.message;
            m_memoryManager->RecordAIDecision(bot->GetGUID().GetRawValue(), eventRecord);

            // Also record in social memory if this involves another player
            if (request.targetGuid)
            {
                // Record social interaction for memory tracking
                m_memoryManager->RecordSocialInteraction(
                    bot->GetGUID().GetRawValue(),
                    request.targetGuid,
                    "", // Target name unknown here
                    "significant_event",
                    0.0f // Neutral sentiment delta
                );
            }

            BEHAVIOR_LOG_DEBUG("Bot %s remembered event: %s", bot->GetName().c_str(), request.message.c_str());
            return true;
        }

        default:
            return false;
    }
}

// ============================================================================
// HELPERS
// ============================================================================

PlayerbotAI* BehaviorExecutor::GetPlayerbotAI(Player* bot)
{
    if (!bot)
        return nullptr;
    return sPlayerbotsMgr.GetPlayerbotAI(bot);
}

bool BehaviorExecutor::CanExecuteBehavior(Player* bot, const BehaviorRequest& request)
{
    // Check if bot is alive (unless it's a meta behavior)
    if (!bot->IsAlive() && request.type < BehaviorType::META_CHANGE_GOAL)
        return false;

    // Check combat restrictions
    if (bot->IsInCombat())
    {
        // Only allow combat and critical behaviors during combat
        if (request.priority > BehaviorPriority::HIGH &&
            request.type < BehaviorType::COMBAT_SET_STYLE)
            return false;
    }

    return true;
}

void BehaviorExecutor::NotifyCompletion(const BehaviorRequest& request, bool success, const std::string& result)
{
    if (request.callback)
    {
        try
        {
            request.callback(success, result);
        }
        catch (const std::exception& e)
        {
            BEHAVIOR_LOG_ERROR("Callback exception for request %u: %s", request.requestId, e.what());
        }
    }
}

} // namespace AliveWorld
