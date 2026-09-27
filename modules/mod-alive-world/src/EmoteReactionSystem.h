/*
 * mod-alive-world - Emote Reaction System
 * Copyright (C) 2025 EC
 *
 * Makes bots react to world events with contextual emotes based on
 * their personality traits.
 *
 * EXPANDED: Now uses all 169+ text emotes from WoW 3.3.5
 * IDs match TEXT_EMOTE_* enum in SharedDefines.h
 */

#ifndef ALIVE_WORLD_EMOTE_REACTION_SYSTEM_H
#define ALIVE_WORLD_EMOTE_REACTION_SYSTEM_H

#include "AliveWorldTypes.h"

#include <map>
#include <vector>
#include <set>

class Player;
class Unit;

namespace AliveWorld
{
    class BotMemoryManager;
    class WorldStateManager;

    // =========================================================================
    // TEXT EMOTE IDs (from SharedDefines.h TEXT_EMOTE_* enum)
    // These are the correct IDs for HandleEmoteCommand()
    // =========================================================================
    namespace TextEmote
    {
        // Social/Greeting emotes
        constexpr uint32 WAVE           = 101;
        constexpr uint32 HELLO          = 55;
        constexpr uint32 GREET          = 48;
        constexpr uint32 WELCOME        = 102;
        constexpr uint32 BOW            = 17;
        constexpr uint32 CURTSEY        = 33;
        constexpr uint32 SALUTE         = 78;
        constexpr uint32 BYE            = 19;
        constexpr uint32 INTRODUCE      = 114;

        // Positive emotes
        constexpr uint32 APPLAUD        = 5;
        constexpr uint32 CHEER          = 21;
        constexpr uint32 CLAP           = 24;
        constexpr uint32 CONGRATULATE   = 26;
        constexpr uint32 VICTORY        = 100;
        constexpr uint32 PRAISE         = 122;
        constexpr uint32 THANK          = 97;
        constexpr uint32 HAPPY          = 54;
        constexpr uint32 SMILE          = 163;

        // Aggressive/Combat emotes
        constexpr uint32 ANGRY          = 3;
        constexpr uint32 ROAR           = 75;
        constexpr uint32 FLEX           = 41;
        constexpr uint32 THREATEN       = 98;
        constexpr uint32 GROWL          = 204;
        constexpr uint32 CHARGE         = 197;
        constexpr uint32 SNARL          = 88;
        constexpr uint32 GLOAT          = 47;
        constexpr uint32 MOCK           = 119;
        constexpr uint32 INSULT         = 113;

        // Fear/Caution emotes
        constexpr uint32 COWER          = 28;
        constexpr uint32 SCARED         = 223;  // Added in WotLK
        constexpr uint32 PANIC          = 69;
        constexpr uint32 CRINGE         = 30;
        constexpr uint32 FLEE           = 209;
        constexpr uint32 SURRENDER      = 92;
        constexpr uint32 HELPME         = 303;  // Help text emote
        constexpr uint32 PLEAD          = 71;

        // Curiosity emotes
        constexpr uint32 CURIOUS        = 32;
        constexpr uint32 PEER           = 70;
        constexpr uint32 GAZE           = 44;
        constexpr uint32 STARE          = 90;
        constexpr uint32 PONDER         = 120;
        constexpr uint32 PUZZLE         = 124;
        constexpr uint32 LISTEN         = 117;

        // Sadness emotes
        constexpr uint32 CRY            = 31;
        constexpr uint32 MOURN          = 65;
        constexpr uint32 SIGH           = 85;
        constexpr uint32 WHINE          = 103;
        constexpr uint32 GROAN          = 50;
        constexpr uint32 MOAN           = 63;

        // Surprise emotes
        constexpr uint32 GASP           = 43;
        constexpr uint32 SURPRISED      = 91;
        constexpr uint32 BOGGLE         = 107;
        constexpr uint32 AMAZE          = 2;

        // Social interaction emotes
        constexpr uint32 HUG            = 56;
        constexpr uint32 KISS           = 58;
        constexpr uint32 COMFORT        = 110;
        constexpr uint32 CUDDLE         = 111;
        constexpr uint32 PAT            = 224;
        constexpr uint32 POKE           = 73;

        // Humor emotes
        constexpr uint32 LAUGH          = 60;
        constexpr uint32 GIGGLE         = 45;
        constexpr uint32 CHUCKLE        = 23;
        constexpr uint32 GUFFAW         = 52;
        constexpr uint32 ROFL           = 76;
        constexpr uint32 CACKLE         = 20;

        // Neutral/Idle emotes
        constexpr uint32 SHRUG          = 83;
        constexpr uint32 NOD            = 67;
        constexpr uint32 NO             = 66;
        constexpr uint32 AGREE          = 1;
        constexpr uint32 BORED          = 14;
        constexpr uint32 YAWN           = 106;
        constexpr uint32 TIRED          = 99;

        // Expression emotes
        constexpr uint32 POINT          = 72;
        constexpr uint32 BECKON         = 7;
        constexpr uint32 TALK           = 93;
        constexpr uint32 SHOUT          = 82;
        constexpr uint32 WHISTLE        = 104;
        constexpr uint32 READY          = 126;

        // Activity emotes
        constexpr uint32 DANCE          = 34;
        constexpr uint32 SIT            = 86;
        constexpr uint32 KNEEL          = 59;
        constexpr uint32 PRAY           = 74;
        constexpr uint32 SLEEP          = 87;
        constexpr uint32 LAYDOWN        = 61;
        constexpr uint32 WORK           = 105;
        constexpr uint32 EAT            = 37;
        constexpr uint32 DRINK          = 35;

        // Misc emotes
        constexpr uint32 BRB            = 16;
        constexpr uint32 BONK           = 13;
        constexpr uint32 BASHFUL        = 6;
        constexpr uint32 SHY            = 84;
        constexpr uint32 BLUSH          = 12;
        constexpr uint32 FROWN          = 42;
        constexpr uint32 GLARE          = 46;
        constexpr uint32 GRIN           = 49;
        constexpr uint32 RUDE           = 77;
        constexpr uint32 BEG            = 8;
        constexpr uint32 GROVEL         = 51;
        constexpr uint32 COLD           = 109;
        constexpr uint32 HOT            = 306;
        constexpr uint32 CALM           = 108;
    }

    // Emote categories for organization
    enum class EmoteCategory : uint8
    {
        GREETING,       // Wave, hello, bow, salute
        CELEBRATION,    // Cheer, applaud, victory
        AGGRESSIVE,     // Roar, flex, threaten, angry
        FEARFUL,        // Cower, panic, scared
        CURIOUS,        // Peer, ponder, puzzle
        SAD,            // Cry, mourn, sigh
        SURPRISED,      // Gasp, amaze, boggle
        SOCIAL,         // Hug, comfort, pat
        HUMOR,          // Laugh, giggle, rofl
        NEUTRAL,        // Shrug, nod, bored
        ACTIVITY        // Dance, sit, eat
    };

    // Reaction context
    struct EmoteReactionContext
    {
        WorldEventType eventType;
        uint64 subjectGuid;        // Who/what caused the event
        std::string subjectName;
        uint32 zoneId;
        uint8 intensity;           // 1-10, how strong the reaction should be
    };

    /**
     * System for making bots react to events with contextual emotes.
     * EXPANDED: Now uses 80+ emotes (up from ~29) with correct TEXT_EMOTE_* IDs
     */
    class EmoteReactionSystem
    {
    public:
        EmoteReactionSystem();
        ~EmoteReactionSystem() = default;

        // Initialize with dependencies
        void Initialize(BotMemoryManager* memoryManager, WorldStateManager* worldStateManager);

        // React to events
        void ReactToEvent(const WorldEvent& event);
        void ReactToCrime(Unit* criminal, uint32 zoneId, uint8 wantedLevel);
        void ReactToBossKill(const std::string& bossName, uint32 zoneId, uint64 killerGuid);
        void ReactToRareSpawn(const std::string& creatureName, uint32 zoneId);

        // Trigger emote for a specific bot
        void TriggerEmote(Player* bot, uint32 emoteId, bool animate = true);
        void TriggerEmoteWithSay(Player* bot, uint32 emoteId, const std::string& message);

        // Get emotes by category
        std::vector<uint32> GetEmotesByCategory(EmoteCategory category) const;

        // Get random emote from category
        uint32 GetRandomEmote(EmoteCategory category) const;

        // Configuration
        void SetEnabled(bool enabled) { m_enabled = enabled; }
        bool IsEnabled() const { return m_enabled; }
        void SetReactionChance(uint32 chance) { m_reactionChance = chance; }
        void SetMaxReactingBots(uint32 max) { m_maxReactingBots = max; }

        // Stats
        uint32 GetAvailableEmoteCount() const { return static_cast<uint32>(m_allEmotes.size()); }

    private:
        // Get appropriate emote based on personality and event
        uint32 SelectEmoteForReaction(const BotPersonality& personality,
                                      WorldEventType eventType,
                                      uint8 intensity) const;

        // Get message for emote (optional speech)
        std::string GetReactionMessage(WorldEventType eventType,
                                       const BotPersonality& personality,
                                       const std::string& subjectName) const;

        // Find nearby bots that should react
        std::vector<Player*> GetReactingBots(uint32 zoneId, float maxDistance = 100.0f) const;

        // Check if bot should react based on personality and chance
        bool ShouldReact(const BotPersonality& personality, WorldEventType eventType) const;

        // Dependencies
        BotMemoryManager* m_memoryManager;
        WorldStateManager* m_worldStateManager;

        // Configuration
        bool m_enabled;
        uint32 m_reactionChance;     // Base chance (0-100)
        uint32 m_maxReactingBots;    // Max bots that react per event
        float m_reactionDistance;    // How far away bots can react

        // All available emote IDs
        std::set<uint32> m_allEmotes;

        // Emotes organized by category
        std::map<EmoteCategory, std::vector<uint32>> m_emotesByCategory;

        // Emote mappings by personality type and event (expanded)
        std::map<WorldEventType, std::vector<uint32>> m_aggressiveEmotes;
        std::map<WorldEventType, std::vector<uint32>> m_helpfulEmotes;
        std::map<WorldEventType, std::vector<uint32>> m_cautiousEmotes;
        std::map<WorldEventType, std::vector<uint32>> m_curiousEmotes;     // NEW: For curious personalities
        std::map<WorldEventType, std::vector<uint32>> m_socialEmotes;      // NEW: For social personalities
        std::map<WorldEventType, std::vector<uint32>> m_neutralEmotes;

        void InitializeEmoteMappings();
        void InitializeEmoteCategories();
    };

} // namespace AliveWorld

#endif // ALIVE_WORLD_EMOTE_REACTION_SYSTEM_H
