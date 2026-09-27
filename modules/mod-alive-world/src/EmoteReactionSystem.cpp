/*
 * mod-alive-world - Emote Reaction System Implementation
 * Copyright (C) 2025 EC
 *
 * EXPANDED: Now uses 80+ emotes with correct TEXT_EMOTE_* IDs
 */

#include "EmoteReactionSystem.h"
#include "AliveWorldManager.h"
#include "BotMemoryManager.h"
#include "WorldStateManager.h"

#include "Chat.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "World.h"

#include <algorithm>
#include <random>

#ifdef MOD_PLAYERBOTS
#include "PlayerbotAI.h"
#include "RandomPlayerbotMgr.h"
#endif

namespace AliveWorld
{

// Static random generator
static std::random_device s_rd;
static std::mt19937 s_gen(s_rd());

EmoteReactionSystem::EmoteReactionSystem()
    : m_memoryManager(nullptr)
    , m_worldStateManager(nullptr)
    , m_enabled(true)
    , m_reactionChance(30)
    , m_maxReactingBots(5)
    , m_reactionDistance(100.0f)
{
    InitializeEmoteCategories();
    InitializeEmoteMappings();
}

void EmoteReactionSystem::Initialize(BotMemoryManager* memoryManager, WorldStateManager* worldStateManager)
{
    m_memoryManager = memoryManager;
    m_worldStateManager = worldStateManager;

    ALIVE_LOG_INFO("Emote Reaction System initialized");
    ALIVE_LOG_INFO("  Available Emotes: %u (expanded from ~29)", GetAvailableEmoteCount());
    ALIVE_LOG_INFO("  Reaction Chance: %u%%", m_reactionChance);
    ALIVE_LOG_INFO("  Max Reacting Bots: %u", m_maxReactingBots);
}

void EmoteReactionSystem::InitializeEmoteCategories()
{
    using namespace TextEmote;

    // Greeting emotes
    m_emotesByCategory[EmoteCategory::GREETING] = {
        WAVE, HELLO, GREET, WELCOME, BOW, CURTSEY, SALUTE, BYE, INTRODUCE
    };

    // Celebration emotes
    m_emotesByCategory[EmoteCategory::CELEBRATION] = {
        APPLAUD, CHEER, CLAP, CONGRATULATE, VICTORY, PRAISE, THANK, HAPPY
    };

    // Aggressive emotes
    m_emotesByCategory[EmoteCategory::AGGRESSIVE] = {
        ANGRY, ROAR, FLEX, THREATEN, SNARL, GLOAT, MOCK, INSULT, GLARE
    };

    // Fearful emotes
    m_emotesByCategory[EmoteCategory::FEARFUL] = {
        COWER, PANIC, CRINGE, SURRENDER, PLEAD, CRY
    };

    // Curious emotes
    m_emotesByCategory[EmoteCategory::CURIOUS] = {
        CURIOUS, PEER, GAZE, STARE, PONDER, PUZZLE, LISTEN
    };

    // Sad emotes
    m_emotesByCategory[EmoteCategory::SAD] = {
        CRY, MOURN, SIGH, WHINE, GROAN, MOAN, FROWN
    };

    // Surprise emotes
    m_emotesByCategory[EmoteCategory::SURPRISED] = {
        GASP, SURPRISED, BOGGLE, AMAZE
    };

    // Social emotes
    m_emotesByCategory[EmoteCategory::SOCIAL] = {
        HUG, KISS, COMFORT, CUDDLE, POKE
    };

    // Humor emotes
    m_emotesByCategory[EmoteCategory::HUMOR] = {
        LAUGH, GIGGLE, CHUCKLE, GUFFAW, ROFL, CACKLE
    };

    // Neutral emotes
    m_emotesByCategory[EmoteCategory::NEUTRAL] = {
        SHRUG, NOD, NO, AGREE, BORED, YAWN, TIRED
    };

    // Activity emotes
    m_emotesByCategory[EmoteCategory::ACTIVITY] = {
        DANCE, SIT, KNEEL, PRAY, SLEEP, LAYDOWN, WORK, EAT, DRINK
    };

    // Collect all emotes into the master set
    for (const auto& pair : m_emotesByCategory)
    {
        for (uint32 emote : pair.second)
        {
            m_allEmotes.insert(emote);
        }
    }

    // Add additional emotes not in categories
    m_allEmotes.insert({
        BRB, BONK, BASHFUL, SHY, BLUSH, GRIN, RUDE, BEG, GROVEL,
        COLD, CALM, POINT, BECKON, TALK, SHOUT, WHISTLE, READY
    });
}

void EmoteReactionSystem::InitializeEmoteMappings()
{
    using namespace TextEmote;

    // =========================================================================
    // AGGRESSIVE PERSONALITY EMOTES
    // =========================================================================
    m_aggressiveEmotes[WorldEventType::BOSS_KILLED] = {
        ROAR, FLEX, VICTORY, GLOAT, LAUGH
    };
    m_aggressiveEmotes[WorldEventType::GUARD_CALLED] = {
        ANGRY, THREATEN, ROAR, SNARL, GLARE
    };
    m_aggressiveEmotes[WorldEventType::PLAYER_KILLED_PLAYER] = {
        ROAR, FLEX, LAUGH, MOCK, GLOAT, VICTORY
    };
    m_aggressiveEmotes[WorldEventType::RARE_SPAWN] = {
        POINT, ROAR, FLEX, READY
    };
    m_aggressiveEmotes[WorldEventType::PLAYER_DIED] = {
        MOCK, LAUGH, SHRUG
    };
    m_aggressiveEmotes[WorldEventType::GROUP_FORMED] = {
        FLEX, READY, NOD
    };
    m_aggressiveEmotes[WorldEventType::DUEL_STARTED] = {
        ROAR, FLEX, THREATEN, GLOAT
    };
    m_aggressiveEmotes[WorldEventType::DUEL_ENDED] = {
        VICTORY, FLEX, ROAR, GLOAT
    };

    // =========================================================================
    // HELPFUL/SOCIAL PERSONALITY EMOTES
    // =========================================================================
    m_helpfulEmotes[WorldEventType::BOSS_KILLED] = {
        CHEER, APPLAUD, CLAP, CONGRATULATE, PRAISE, HAPPY
    };
    m_helpfulEmotes[WorldEventType::GUARD_CALLED] = {
        POINT, GASP, COMFORT
    };
    m_helpfulEmotes[WorldEventType::PLAYER_KILLED_PLAYER] = {
        MOURN, CRY, SIGH, COMFORT
    };
    m_helpfulEmotes[WorldEventType::RARE_SPAWN] = {
        POINT, BECKON, WAVE, SHOUT
    };
    m_helpfulEmotes[WorldEventType::PLAYER_DIED] = {
        MOURN, CRY, COMFORT, SIGH, KNEEL
    };
    m_helpfulEmotes[WorldEventType::GROUP_FORMED] = {
        CHEER, WAVE, WELCOME, HUG
    };
    m_helpfulEmotes[WorldEventType::DUEL_STARTED] = {
        CHEER, CLAP, APPLAUD
    };
    m_helpfulEmotes[WorldEventType::DUEL_ENDED] = {
        CLAP, CONGRATULATE, APPLAUD, CHEER
    };

    // =========================================================================
    // CAUTIOUS/TIMID PERSONALITY EMOTES
    // =========================================================================
    m_cautiousEmotes[WorldEventType::BOSS_KILLED] = {
        CHEER, SIGH, PRAY, THANK
    };
    m_cautiousEmotes[WorldEventType::GUARD_CALLED] = {
        COWER, PANIC, GASP, CRINGE
    };
    m_cautiousEmotes[WorldEventType::PLAYER_KILLED_PLAYER] = {
        COWER, CRY, PANIC, CRINGE
    };
    m_cautiousEmotes[WorldEventType::RARE_SPAWN] = {
        GASP, COWER, CRINGE
    };
    m_cautiousEmotes[WorldEventType::PLAYER_DIED] = {
        CRY, MOURN, CRINGE, PANIC
    };
    m_cautiousEmotes[WorldEventType::GROUP_FORMED] = {
        SHY, BASHFUL, NOD, WAVE
    };
    m_cautiousEmotes[WorldEventType::DUEL_STARTED] = {
        GASP, COWER, CRINGE
    };
    m_cautiousEmotes[WorldEventType::DUEL_ENDED] = {
        SIGH, GASP
    };

    // =========================================================================
    // CURIOUS PERSONALITY EMOTES (NEW)
    // =========================================================================
    m_curiousEmotes[WorldEventType::BOSS_KILLED] = {
        CURIOUS, PEER, PONDER, APPLAUD
    };
    m_curiousEmotes[WorldEventType::GUARD_CALLED] = {
        CURIOUS, PEER, GAZE, PONDER
    };
    m_curiousEmotes[WorldEventType::PLAYER_KILLED_PLAYER] = {
        CURIOUS, PEER, PONDER, GASP
    };
    m_curiousEmotes[WorldEventType::RARE_SPAWN] = {
        CURIOUS, PEER, GAZE, AMAZE, POINT
    };
    m_curiousEmotes[WorldEventType::PLAYER_DIED] = {
        CURIOUS, PEER, PONDER
    };
    m_curiousEmotes[WorldEventType::GROUP_FORMED] = {
        CURIOUS, PEER, WAVE
    };
    m_curiousEmotes[WorldEventType::DUEL_STARTED] = {
        CURIOUS, PEER, GAZE
    };
    m_curiousEmotes[WorldEventType::DUEL_ENDED] = {
        CURIOUS, PEER, PONDER
    };

    // =========================================================================
    // SOCIAL PERSONALITY EMOTES (NEW)
    // =========================================================================
    m_socialEmotes[WorldEventType::BOSS_KILLED] = {
        CHEER, HUG, APPLAUD, CONGRATULATE, HAPPY
    };
    m_socialEmotes[WorldEventType::GUARD_CALLED] = {
        GASP, TALK, SHOUT
    };
    m_socialEmotes[WorldEventType::PLAYER_KILLED_PLAYER] = {
        GASP, TALK, COMFORT
    };
    m_socialEmotes[WorldEventType::RARE_SPAWN] = {
        SHOUT, TALK, WAVE, BECKON
    };
    m_socialEmotes[WorldEventType::PLAYER_DIED] = {
        COMFORT, HUG, MOURN
    };
    m_socialEmotes[WorldEventType::GROUP_FORMED] = {
        HUG, WAVE, HELLO, WELCOME, HAPPY
    };
    m_socialEmotes[WorldEventType::DUEL_STARTED] = {
        CHEER, WAVE, TALK
    };
    m_socialEmotes[WorldEventType::DUEL_ENDED] = {
        CHEER, CLAP, CONGRATULATE, COMFORT
    };

    // =========================================================================
    // NEUTRAL PERSONALITY EMOTES
    // =========================================================================
    m_neutralEmotes[WorldEventType::BOSS_KILLED] = {
        CLAP, WAVE, SALUTE, NOD, APPLAUD
    };
    m_neutralEmotes[WorldEventType::GUARD_CALLED] = {
        GASP, SHRUG, POINT, PEER
    };
    m_neutralEmotes[WorldEventType::PLAYER_KILLED_PLAYER] = {
        GASP, SHRUG, SIGH
    };
    m_neutralEmotes[WorldEventType::RARE_SPAWN] = {
        POINT, GASP, PEER, NOD
    };
    m_neutralEmotes[WorldEventType::PLAYER_DIED] = {
        SIGH, SHRUG, MOURN
    };
    m_neutralEmotes[WorldEventType::GROUP_FORMED] = {
        WAVE, NOD, GREET
    };
    m_neutralEmotes[WorldEventType::DUEL_STARTED] = {
        NOD, SHRUG, PEER
    };
    m_neutralEmotes[WorldEventType::DUEL_ENDED] = {
        CLAP, NOD, SHRUG
    };
}

std::vector<uint32> EmoteReactionSystem::GetEmotesByCategory(EmoteCategory category) const
{
    auto it = m_emotesByCategory.find(category);
    if (it != m_emotesByCategory.end())
        return it->second;
    return {};
}

uint32 EmoteReactionSystem::GetRandomEmote(EmoteCategory category) const
{
    auto it = m_emotesByCategory.find(category);
    if (it == m_emotesByCategory.end() || it->second.empty())
        return TextEmote::WAVE;  // Default fallback

    std::uniform_int_distribution<size_t> dist(0, it->second.size() - 1);
    return it->second[dist(s_gen)];
}

void EmoteReactionSystem::ReactToEvent(const WorldEvent& event)
{
    if (!m_enabled)
        return;

    // Get bots that should react
    std::vector<Player*> reactingBots = GetReactingBots(event.zoneId, m_reactionDistance);

    if (reactingBots.empty())
        return;

    uint32 reactCount = 0;

    for (Player* bot : reactingBots)
    {
        if (reactCount >= m_maxReactingBots)
            break;

        uint64 botGuid = bot->GetGUID().GetCounter();
        BotPersonality personality;

        if (m_memoryManager)
        {
            personality = m_memoryManager->GetPersonality(botGuid);
        }

        if (!ShouldReact(personality, event.type))
            continue;

        uint32 emote = SelectEmoteForReaction(personality, event.type, event.importance);
        TriggerEmote(bot, emote);
        reactCount++;

        ALIVE_LOG_DEBUG("Bot %s reacted to %s with emote %u",
            bot->GetName().c_str(),
            EventTypeToString(event.type),
            emote);
    }

    if (reactCount > 0)
    {
        ALIVE_LOG_DEBUG("Emote reactions: %u bots reacted to %s",
            reactCount, EventTypeToString(event.type));
    }
}

void EmoteReactionSystem::ReactToCrime(Unit* criminal, uint32 zoneId, uint8 wantedLevel)
{
    if (!m_enabled || !criminal)
        return;

    WorldEvent fakeEvent;
    fakeEvent.type = WorldEventType::GUARD_CALLED;
    fakeEvent.primaryName = criminal->GetName();
    fakeEvent.zoneId = zoneId;
    fakeEvent.importance = std::min<uint8>(wantedLevel * 2, 10);

    ReactToEvent(fakeEvent);
}

void EmoteReactionSystem::ReactToBossKill(const std::string& bossName, uint32 zoneId, uint64 killerGuid)
{
    if (!m_enabled)
        return;

    WorldEvent fakeEvent;
    fakeEvent.type = WorldEventType::BOSS_KILLED;
    fakeEvent.secondaryName = bossName;
    fakeEvent.zoneId = zoneId;
    fakeEvent.importance = 8;

    ReactToEvent(fakeEvent);
}

void EmoteReactionSystem::ReactToRareSpawn(const std::string& creatureName, uint32 zoneId)
{
    if (!m_enabled)
        return;

    WorldEvent fakeEvent;
    fakeEvent.type = WorldEventType::RARE_SPAWN;
    fakeEvent.primaryName = creatureName;
    fakeEvent.zoneId = zoneId;
    fakeEvent.importance = 6;

    ReactToEvent(fakeEvent);
}

void EmoteReactionSystem::TriggerEmote(Player* bot, uint32 emoteId, bool animate)
{
    if (!bot)
        return;

#ifdef MOD_PLAYERBOTS
    // Check if this is actually a playerbot
    if (PlayerbotAI* ai = sPlayerbotsMgr.GetPlayerbotAI(bot))
    {
        // Use the bot's built-in emote method
        bot->HandleEmoteCommand(emoteId);
    }
#else
    // Fallback: directly trigger emote animation
    bot->HandleEmoteCommand(emoteId);
#endif
}

void EmoteReactionSystem::TriggerEmoteWithSay(Player* bot, uint32 emoteId, const std::string& message)
{
    if (!bot)
        return;

    TriggerEmote(bot, emoteId);

    // Also say the message in /say
    if (!message.empty())
    {
        bot->Say(message, LANG_UNIVERSAL);
    }
}

uint32 EmoteReactionSystem::SelectEmoteForReaction(const BotPersonality& personality,
                                                   WorldEventType eventType,
                                                   uint8 intensity) const
{
    // Determine dominant personality trait and select appropriate emote list
    const std::vector<uint32>* emoteList = nullptr;

    // Priority order: most distinctive trait wins
    if (personality.aggression > 0.7f)
    {
        auto it = m_aggressiveEmotes.find(eventType);
        if (it != m_aggressiveEmotes.end())
            emoteList = &it->second;
    }
    else if (personality.helpfulness > 0.65f)
    {
        auto it = m_helpfulEmotes.find(eventType);
        if (it != m_helpfulEmotes.end())
            emoteList = &it->second;
    }
    else if (personality.curiosity > 0.65f)
    {
        // NEW: Curious personalities get their own emotes
        auto it = m_curiousEmotes.find(eventType);
        if (it != m_curiousEmotes.end())
            emoteList = &it->second;
    }
    else if (personality.sociability > 0.65f)
    {
        // NEW: Social personalities get their own emotes
        auto it = m_socialEmotes.find(eventType);
        if (it != m_socialEmotes.end())
            emoteList = &it->second;
    }
    else if (personality.aggression < 0.3f && personality.caution > 0.5f)
    {
        // Cautious = low aggression + high caution
        auto it = m_cautiousEmotes.find(eventType);
        if (it != m_cautiousEmotes.end())
            emoteList = &it->second;
    }

    // Fall back to neutral
    if (!emoteList)
    {
        auto it = m_neutralEmotes.find(eventType);
        if (it != m_neutralEmotes.end())
            emoteList = &it->second;
    }

    if (!emoteList || emoteList->empty())
        return TextEmote::WAVE;  // Default fallback

    // Select random emote from the list
    std::uniform_int_distribution<size_t> dist(0, emoteList->size() - 1);
    return (*emoteList)[dist(s_gen)];
}

std::string EmoteReactionSystem::GetReactionMessage(WorldEventType eventType,
                                                     const BotPersonality& personality,
                                                     const std::string& subjectName) const
{
    // Most reactions don't need speech, but some high-sociability bots might speak
    if (personality.sociability < 0.6f)
        return "";

    // Only speak 20% of the time even for chatty bots
    std::uniform_int_distribution<int> dist(1, 100);
    if (dist(s_gen) > 20)
        return "";

    switch (eventType)
    {
        case WorldEventType::BOSS_KILLED:
            if (personality.aggression > 0.7f)
                return "Victory!";
            else if (personality.helpfulness > 0.6f)
                return "Well done, everyone!";
            else if (personality.curiosity > 0.6f)
                return "Fascinating...";
            return "";

        case WorldEventType::GUARD_CALLED:
            if (personality.helpfulness > 0.6f)
                return "Guards! Help!";
            else if (personality.aggression < 0.3f)
                return "What's happening?!";
            else if (personality.curiosity > 0.6f)
                return "What's going on over there?";
            return "";

        case WorldEventType::RARE_SPAWN:
            if (personality.curiosity > 0.6f)
                return "Look at that!";
            else if (personality.greed > 0.6f)
                return "That's mine!";
            return "";

        case WorldEventType::GROUP_FORMED:
            if (personality.sociability > 0.7f)
                return "Great to have you!";
            return "";

        default:
            return "";
    }
}

std::vector<Player*> EmoteReactionSystem::GetReactingBots(uint32 zoneId, float maxDistance) const
{
    std::vector<Player*> result;

    if (!m_worldStateManager)
        return result;

    // Get all bots in the zone
    auto botsInZone = m_worldStateManager->GetBotsInZone(zoneId);

    for (auto it = ObjectAccessor::GetPlayers().begin();
         it != ObjectAccessor::GetPlayers().end(); ++it)
    {
        Player* player = it->second;
        if (!player || !player->IsInWorld())
            continue;

        if (player->GetZoneId() != zoneId)
            continue;

#ifdef MOD_PLAYERBOTS
        // Only consider playerbots
        if (!sPlayerbotsMgr.GetPlayerbotAI(player))
            continue;
#endif

        result.push_back(player);

        // Limit the number of candidates
        if (result.size() >= m_maxReactingBots * 3)
            break;
    }

    // Shuffle to randomize which bots react
    std::shuffle(result.begin(), result.end(), s_gen);

    return result;
}

bool EmoteReactionSystem::ShouldReact(const BotPersonality& personality, WorldEventType eventType) const
{
    // Base chance
    float chance = static_cast<float>(m_reactionChance);

    // Modify by personality
    // High sociability = more likely to react
    chance *= (0.5f + personality.sociability);

    // Certain events trigger more reactions
    switch (eventType)
    {
        case WorldEventType::BOSS_KILLED:
            chance *= 1.5f;  // Boss kills are exciting
            break;
        case WorldEventType::GUARD_CALLED:
            chance *= 1.2f;  // Crime is notable
            break;
        case WorldEventType::RARE_SPAWN:
            // Curiosity affects rare spawn reactions
            chance *= (0.5f + personality.curiosity);
            break;
        case WorldEventType::PLAYER_KILLED_PLAYER:
            // Aggression and curiosity both affect PvP reactions
            chance *= (0.3f + personality.aggression * 0.4f + personality.curiosity * 0.3f);
            break;
        case WorldEventType::GROUP_FORMED:
            // Sociability heavily affects group reactions
            chance *= (0.3f + personality.sociability * 0.7f);
            break;
        default:
            break;
    }

    // Roll
    std::uniform_int_distribution<int> dist(1, 100);
    return dist(s_gen) < static_cast<int>(chance);
}

} // namespace AliveWorld
