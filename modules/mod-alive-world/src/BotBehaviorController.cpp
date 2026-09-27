/*
 * mod-alive-world - Bot Behavior Controller
 * Copyright (C) 2025 EC
 */

#include "BotBehaviorController.h"

#include "Chat.h"
#include "Group.h"
#include "Log.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "Unit.h"
#include "WorldPacket.h"

#ifdef MOD_EC_REALTIME_BRIDGE
#include "RealtimeBridgeSocket.h"
#include "RealtimeBridgeTypes.h"
#endif

// Playerbots header for detecting bot players
#ifdef HAVE_PLAYERBOTS
#include "Playerbots.h"
#endif

namespace AliveWorld
{

// =========================================================================
// BEHAVIOR PROFILE SERIALIZATION
// =========================================================================

std::string BotBehaviorProfile::ToJson() const
{
    std::ostringstream ss;
    ss << "{"
       << "\"stance\":\"" << BotStanceToString(stance) << "\","
       << "\"attackNeutrals\":" << (attackNeutrals ? "true" : "false") << ","
       << "\"assistParty\":" << (assistParty ? "true" : "false") << ","
       << "\"autoLoot\":" << (autoLoot ? "true" : "false") << ","
       << "\"followDistance\":" << followDistance << ","
       << "\"aggroRadius\":" << aggroRadius << ","
       << "\"debugThoughts\":" << (debugThoughts ? "true" : "false") << ","
       << "\"ownerGuid\":" << ownerGuid << ","
       << "\"lastModifiedMs\":" << lastModifiedMs
       << "}";
    return ss.str();
}

BotBehaviorProfile BotBehaviorProfile::FromJson(const std::string& /* json */)
{
    // Minimal implementation - full JSON parsing is done on the Node.js side
    // and sent as individual fields via bridge commands.
    BotBehaviorProfile profile;
    return profile;
}

// =========================================================================
// SINGLETON
// =========================================================================

BotBehaviorController* BotBehaviorController::Instance()
{
    static BotBehaviorController instance;
    return &instance;
}

// =========================================================================
// STANCE MANAGEMENT
// =========================================================================

void BotBehaviorController::SetStance(uint64 botGuid, BotStance stance, const std::string& reason)
{
    BotStance oldStance;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_profiles.find(botGuid);
        if (it != m_profiles.end())
        {
            oldStance = it->second.stance;
            it->second.stance = stance;
            it->second.lastModifiedMs = NowMs();
        }
        else
        {
            oldStance = BotStance::DEFENSIVE;
            BotBehaviorProfile profile;
            profile.stance = stance;
            profile.lastModifiedMs = NowMs();
            m_profiles[botGuid] = profile;
        }
    }

    if (oldStance == stance)
        return;

    // Emit stance change event via bridge
#ifdef MOD_EC_REALTIME_BRIDGE
    if (auto* bridge = ECRealtimeBridge::RealtimeBridgeSocket::Instance())
    {
        if (bridge->IsConnected())
        {
            Player* bot = ObjectAccessor::FindPlayer(ObjectGuid::Create<HighGuid::Player>(botGuid));
            std::string botName = bot ? bot->GetName() : "Unknown";

            std::ostringstream ss;
            ss << "{"
               << "\"botGuid\":" << botGuid << ","
               << "\"botName\":\"" << ECRealtimeBridge::EscapeJsonString(botName) << "\","
               << "\"previousStance\":\"" << BotStanceToString(oldStance) << "\","
               << "\"newStance\":\"" << BotStanceToString(stance) << "\","
               << "\"reason\":\"" << ECRealtimeBridge::EscapeJsonString(reason) << "\""
               << "}";

            bridge->EmitEvent(
                ECRealtimeBridge::EventNamespace::ALIVE_WORLD,
                "bot_stance_changed",
                ss.str()
            );
        }
    }
#endif

    LOG_INFO("module", "[BotBehavior] Bot {} stance changed: {} -> {} ({})",
             botGuid, BotStanceToString(oldStance), BotStanceToString(stance), reason);
}

BotStance BotBehaviorController::GetStance(uint64 botGuid) const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_profiles.find(botGuid);
    if (it != m_profiles.end())
        return it->second.stance;
    return BotStance::DEFENSIVE;  // Default stance
}

// =========================================================================
// PROFILE MANAGEMENT
// =========================================================================

void BotBehaviorController::SetProfile(uint64 botGuid, const BotBehaviorProfile& profile,
                                        const std::string& reason)
{
    BotStance oldStance;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_profiles.find(botGuid);
        oldStance = (it != m_profiles.end()) ? it->second.stance : BotStance::DEFENSIVE;
        m_profiles[botGuid] = profile;
        m_profiles[botGuid].lastModifiedMs = NowMs();
    }

    // Emit profile change event via bridge
#ifdef MOD_EC_REALTIME_BRIDGE
    if (auto* bridge = ECRealtimeBridge::RealtimeBridgeSocket::Instance())
    {
        if (bridge->IsConnected())
        {
            Player* bot = ObjectAccessor::FindPlayer(ObjectGuid::Create<HighGuid::Player>(botGuid));
            std::string botName = bot ? bot->GetName() : "Unknown";

            std::ostringstream ss;
            ss << "{"
               << "\"botGuid\":" << botGuid << ","
               << "\"botName\":\"" << ECRealtimeBridge::EscapeJsonString(botName) << "\","
               << "\"previousStance\":\"" << BotStanceToString(oldStance) << "\","
               << "\"newStance\":\"" << BotStanceToString(profile.stance) << "\","
               << "\"reason\":\"" << ECRealtimeBridge::EscapeJsonString(reason) << "\","
               << "\"profile\":" << profile.ToJson()
               << "}";

            bridge->EmitEvent(
                ECRealtimeBridge::EventNamespace::ALIVE_WORLD,
                "bot_behavior_profile_changed",
                ss.str()
            );
        }
    }
#endif

    LOG_INFO("module", "[BotBehavior] Bot {} profile updated: stance={} neutrals={} ({})",
             botGuid, BotStanceToString(profile.stance),
             profile.attackNeutrals ? "yes" : "no", reason);
}

BotBehaviorProfile BotBehaviorController::GetProfile(uint64 botGuid) const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_profiles.find(botGuid);
    if (it != m_profiles.end())
        return it->second;

    // Return default profile
    BotBehaviorProfile defaultProfile;
    return defaultProfile;
}

bool BotBehaviorController::HasProfile(uint64 botGuid) const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_profiles.find(botGuid) != m_profiles.end();
}

void BotBehaviorController::RemoveProfile(uint64 botGuid)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_profiles.erase(botGuid);
}

// =========================================================================
// TARGET FILTERING
// =========================================================================

bool BotBehaviorController::ShouldAttackTarget(Player* bot, Unit* target) const
{
    if (!bot || !target)
        return false;

    uint64 botGuid = bot->GetGUID().GetCounter();

    // If no custom profile, use default behavior (don't interfere)
    if (!HasProfile(botGuid))
        return true;  // Let the default playerbots logic decide

    BotBehaviorProfile profile = GetProfile(botGuid);

    switch (profile.stance)
    {
        case BotStance::PASSIVE:
            // Never attack anything in passive stance
            return false;

        case BotStance::DEFENSIVE:
            // Only attack if target is threatening us or our party
            return IsTargetThreateningParty(bot, target);

        case BotStance::AGGRESSIVE:
            // Attack hostiles always; neutrals only if profile allows
            if (bot->IsHostileTo(target))
                return true;

            if (profile.attackNeutrals)
                return true;

            // Even in aggressive mode, attack if target is threatening party
            return IsTargetThreateningParty(bot, target);
    }

    return true;
}

bool BotBehaviorController::AllowsNeutralAttacks(uint64 botGuid) const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_profiles.find(botGuid);
    if (it != m_profiles.end())
        return it->second.attackNeutrals;
    return false;
}

bool BotBehaviorController::IsInPartyWithRealPlayer(Player* bot)
{
    if (!bot)
        return false;

    Group* group = bot->GetGroup();
    if (!group)
        return false;

    Group::MemberSlotList const& members = group->GetMemberSlots();
    for (auto const& slot : members)
    {
        if (slot.guid == bot->GetGUID())
            continue;

        Player* member = ObjectAccessor::FindPlayer(slot.guid);
        if (!member)
            continue;

#ifdef HAVE_PLAYERBOTS
        // Check if the member is a real player (not a bot)
        if (!GET_PLAYERBOT_AI(member))
            return true;
#else
        // Without playerbots module, all players are real
        return true;
#endif
    }

    return false;
}

bool BotBehaviorController::IsTargetThreateningParty(Player* bot, Unit* target)
{
    if (!bot || !target)
        return false;

    // Target is attacking the bot directly
    if (target->GetVictim() == bot)
        return true;

    // Target is in combat with the bot
    if (target->IsInCombatWith(bot))
        return true;

    // Check if target is attacking any party member
    Group* group = bot->GetGroup();
    if (group)
    {
        Group::MemberSlotList const& members = group->GetMemberSlots();
        for (auto const& slot : members)
        {
            Player* member = ObjectAccessor::FindPlayer(slot.guid);
            if (!member || member == bot)
                continue;

            if (target->GetVictim() == member || target->IsInCombatWith(member))
                return true;
        }
    }

    return false;
}

// =========================================================================
// DEBUG THOUGHT BROADCASTING
// =========================================================================

void BotBehaviorController::SetDebugThoughts(uint64 botGuid, bool enabled)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_profiles.find(botGuid);
    if (it != m_profiles.end())
    {
        it->second.debugThoughts = enabled;
    }
    else
    {
        BotBehaviorProfile profile;
        profile.debugThoughts = enabled;
        m_profiles[botGuid] = profile;
    }
}

bool BotBehaviorController::HasDebugThoughts(uint64 botGuid) const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_profiles.find(botGuid);
    if (it != m_profiles.end())
        return it->second.debugThoughts;
    return false;
}

void BotBehaviorController::BroadcastThought(Player* bot, const std::string& thought,
                                              const std::string& context)
{
    if (!bot)
        return;

    uint64 botGuid = bot->GetGUID().GetCounter();

    if (!HasDebugThoughts(botGuid))
        return;

    // Rate limit: max 1 thought per second per bot
    if (ShouldThrottleThought(botGuid))
        return;

    // Update last thought timestamp
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_lastThoughtTime[botGuid] = NowMs();
    }

    // 1. In-game: Send as monster party chat so it appears from the bot
    Group* group = bot->GetGroup();
    if (group)
    {
        // Build colored thought message
        std::string msg = "|cFF88AAFF[Think]|r " + thought;

        // Use ChatHandler to send to party as a system message from the bot
        Group::MemberSlotList const& members = group->GetMemberSlots();
        for (auto const& slot : members)
        {
            Player* member = ObjectAccessor::FindPlayer(slot.guid);
            if (!member)
                continue;

            // Only send to real players (not other bots)
#ifdef HAVE_PLAYERBOTS
            if (GET_PLAYERBOT_AI(member))
                continue;
#endif
            ChatHandler(member->GetSession()).PSendSysMessage(
                "|cFF88AAFF[%s Think]|r %s", bot->GetName().c_str(), thought.c_str());
        }
    }

    // 2. Bridge: Emit to Node.js for frontend panel
#ifdef MOD_EC_REALTIME_BRIDGE
    if (auto* bridge = ECRealtimeBridge::RealtimeBridgeSocket::Instance())
    {
        if (bridge->IsConnected())
        {
            std::ostringstream ss;
            ss << "{"
               << "\"botGuid\":" << botGuid << ","
               << "\"botName\":\"" << ECRealtimeBridge::EscapeJsonString(bot->GetName()) << "\","
               << "\"thought\":\"" << ECRealtimeBridge::EscapeJsonString(thought) << "\","
               << "\"context\":\"" << ECRealtimeBridge::EscapeJsonString(context) << "\""
               << "}";

            bridge->EmitEvent(
                ECRealtimeBridge::EventNamespace::ALIVE_WORLD,
                "bot_thought",
                ss.str()
            );
        }
    }
#endif
}

bool BotBehaviorController::ShouldThrottleThought(uint64 botGuid) const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_lastThoughtTime.find(botGuid);
    if (it == m_lastThoughtTime.end())
        return false;

    uint64 now = NowMs();
    return (now - it->second) < 1000;  // 1 second throttle
}

// =========================================================================
// STATS & CLEANUP
// =========================================================================

uint32 BotBehaviorController::GetProfileCount() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return static_cast<uint32>(m_profiles.size());
}

void BotBehaviorController::CleanupStaleProfiles()
{
    std::lock_guard<std::mutex> lock(m_mutex);

    auto it = m_profiles.begin();
    while (it != m_profiles.end())
    {
        Player* bot = ObjectAccessor::FindPlayer(ObjectGuid::Create<HighGuid::Player>(it->first));
        if (!bot || !bot->IsInWorld())
        {
            it = m_profiles.erase(it);
        }
        else
        {
            ++it;
        }
    }

    // Also clean up thought timestamps
    auto tit = m_lastThoughtTime.begin();
    while (tit != m_lastThoughtTime.end())
    {
        if (m_profiles.find(tit->first) == m_profiles.end())
            tit = m_lastThoughtTime.erase(tit);
        else
            ++tit;
    }
}

} // namespace AliveWorld
