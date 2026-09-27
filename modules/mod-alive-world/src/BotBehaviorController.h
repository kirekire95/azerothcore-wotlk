/*
 * mod-alive-world - Bot Behavior Controller
 * Copyright (C) 2025 EC
 *
 * Pet-like behavior system for playerbots when partied with a real player.
 * Supports passive/defensive/aggressive stances, configurable target filtering,
 * and debug thought broadcasting (in-game + bridge).
 */

#ifndef BOT_BEHAVIOR_CONTROLLER_H
#define BOT_BEHAVIOR_CONTROLLER_H

#include "AliveWorldTypes.h"
#include "Common.h"
#include "ObjectGuid.h"

#include <mutex>
#include <string>
#include <unordered_map>

class Player;
class Unit;
class Group;

namespace AliveWorld
{

// =========================================================================
// ENUMS
// =========================================================================

enum class BotStance : uint8
{
    PASSIVE    = 0,  // Won't attack anything, follow only
    DEFENSIVE  = 1,  // Attack only if attacked or party member attacked
    AGGRESSIVE = 2   // Attack hostiles in range, optionally neutrals, grind
};

inline const char* BotStanceToString(BotStance stance)
{
    switch (stance)
    {
        case BotStance::PASSIVE:    return "passive";
        case BotStance::DEFENSIVE:  return "defensive";
        case BotStance::AGGRESSIVE: return "aggressive";
        default: return "unknown";
    }
}

inline BotStance StringToBotStance(const std::string& str)
{
    if (str == "passive")    return BotStance::PASSIVE;
    if (str == "defensive")  return BotStance::DEFENSIVE;
    if (str == "aggressive") return BotStance::AGGRESSIVE;
    return BotStance::DEFENSIVE;  // Default
}

// =========================================================================
// BEHAVIOR PROFILE
// =========================================================================

struct BotBehaviorProfile
{
    BotStance stance = BotStance::DEFENSIVE;
    bool attackNeutrals   = false;   // Attack neutral NPCs (non-hostile)
    bool assistParty      = true;    // Assist party members in combat
    bool autoLoot         = true;    // Auto-loot kills
    float followDistance   = 10.0f;  // Follow distance (yards)
    float aggroRadius     = 30.0f;  // Target scan radius (yards)
    bool debugThoughts    = false;   // Broadcast thoughts in-game + bridge
    std::string customInstructions;  // LLM-parsed instructions (text)
    uint64 ownerGuid      = 0;      // Player who set this profile
    uint64 lastModifiedMs = 0;      // Timestamp of last change

    // Serialize to JSON string (for bridge events)
    std::string ToJson() const;

    // Parse from JSON payload fields
    static BotBehaviorProfile FromJson(const std::string& json);
};

// =========================================================================
// BEHAVIOR CONTROLLER (SINGLETON)
// =========================================================================

class BotBehaviorController
{
public:
    static BotBehaviorController* Instance();

    // ---- Stance Management ----
    void SetStance(uint64 botGuid, BotStance stance, const std::string& reason = "");
    BotStance GetStance(uint64 botGuid) const;

    // ---- Full Profile Management ----
    void SetProfile(uint64 botGuid, const BotBehaviorProfile& profile, const std::string& reason = "");
    BotBehaviorProfile GetProfile(uint64 botGuid) const;
    bool HasProfile(uint64 botGuid) const;
    void RemoveProfile(uint64 botGuid);

    // ---- Target Filtering (called from GrindTargetValue) ----
    // Returns true if the bot should attack this target given its behavior profile.
    bool ShouldAttackTarget(Player* bot, Unit* target) const;

    // Returns true if the bot's profile allows attacking neutral targets.
    bool AllowsNeutralAttacks(uint64 botGuid) const;

    // Returns true if the bot is in a party with a real player.
    static bool IsInPartyWithRealPlayer(Player* bot);

    // ---- Defensive Stance Helper ----
    // Returns true if the target is attacking the bot or any of its party members.
    static bool IsTargetThreateningParty(Player* bot, Unit* target);

    // ---- Debug Thought Broadcasting ----
    void SetDebugThoughts(uint64 botGuid, bool enabled);
    bool HasDebugThoughts(uint64 botGuid) const;

    // Broadcast a thought both in-game (party chat) and via bridge.
    // context: "target_selection", "combat", "stance_change", "llm_instruction", etc.
    void BroadcastThought(Player* bot, const std::string& thought,
                          const std::string& context = "general");

    // ---- Stats ----
    uint32 GetProfileCount() const;

    // ---- Cleanup ----
    // Remove profiles for bots no longer in world (called periodically).
    void CleanupStaleProfiles();

private:
    BotBehaviorController() = default;
    ~BotBehaviorController() = default;
    BotBehaviorController(const BotBehaviorController&) = delete;
    BotBehaviorController& operator=(const BotBehaviorController&) = delete;

    // Rate-limit debug thoughts (max 1 per second per bot)
    bool ShouldThrottleThought(uint64 botGuid) const;

    mutable std::mutex m_mutex;
    std::unordered_map<uint64, BotBehaviorProfile> m_profiles;
    mutable std::unordered_map<uint64, uint64> m_lastThoughtTime;  // botGuid -> timestamp ms
};

} // namespace AliveWorld

#define sBotBehaviorController AliveWorld::BotBehaviorController::Instance()

#endif // BOT_BEHAVIOR_CONTROLLER_H
