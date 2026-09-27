/*
 * mod-alive-world - Speech Pattern Manager
 * Copyright (C) 2025 EC
 *
 * Manages NPC/bot speech patterns extracted from creature_text table.
 * Provides contextual speech for bots based on situation and personality.
 */

#ifndef ALIVE_WORLD_SPEECH_PATTERN_MANAGER_H
#define ALIVE_WORLD_SPEECH_PATTERN_MANAGER_H

#include "AliveWorldTypes.h"

#include <map>
#include <mutex>
#include <random>
#include <shared_mutex>
#include <unordered_map>
#include <vector>

namespace AliveWorld
{
    /**
     * Manages speech patterns loaded from creature_text table.
     * Patterns are categorized by context (aggro, death, idle, etc.)
     * and creature category for appropriate selection.
     */
    class SpeechPatternManager
    {
    public:
        SpeechPatternManager();
        ~SpeechPatternManager() = default;

        // Initialization - loads from creature_text
        void Initialize();
        void LoadPatternsFromDB();
        void LoadCreatureTypesFromDB();

        // Get speech patterns by context
        const SpeechPattern* GetRandomPattern(SpeechContext context) const;
        const SpeechPattern* GetRandomPattern(SpeechContext context, CreatureCategory category) const;
        const SpeechPattern* GetPatternForPersonality(SpeechContext context,
                                                       const BotPersonality& personality) const;

        // Get patterns matching personality traits
        std::vector<const SpeechPattern*> GetAggressivePatterns(uint32 limit = 10) const;
        std::vector<const SpeechPattern*> GetFearfulPatterns(uint32 limit = 10) const;
        std::vector<const SpeechPattern*> GetBoastfulPatterns(uint32 limit = 10) const;
        std::vector<const SpeechPattern*> GetFactionPatterns(const std::string& faction,
                                                              uint32 limit = 10) const;

        // Process pattern text (replace placeholders)
        std::string ProcessPatternText(const std::string& pattern,
                                        const std::string& botName,
                                        const std::string& targetName = "",
                                        const std::string& zoneName = "") const;

        // Statistics
        uint32 GetTotalPatternCount() const;
        uint32 GetPatternCount(SpeechContext context) const;
        uint32 GetPatternCount(CreatureCategory category) const;
        std::map<SpeechContext, uint32> GetPatternCountByContext() const;
        std::map<CreatureCategory, uint32> GetPatternCountByCategory() const;

        // Direct access for iteration
        const std::vector<SpeechPattern>& GetAllPatterns() const { return m_allPatterns; }

    private:
        // Categorize pattern from comment text
        SpeechContext ParseContextFromComment(const std::string& comment) const;
        CreatureCategory GetCreatureCategory(uint32 creatureEntry) const;

        // Check if pattern matches personality
        bool MatchesPersonality(const SpeechPattern& pattern,
                                const BotPersonality& personality) const;

        // All patterns
        std::vector<SpeechPattern> m_allPatterns;

        // Indexed by context for fast lookup
        std::unordered_map<SpeechContext, std::vector<uint32>> m_patternsByContext;

        // Indexed by creature category
        std::unordered_map<CreatureCategory, std::vector<uint32>> m_patternsByCategory;

        // Creature entry -> category mapping (from creature_template)
        std::unordered_map<uint32, CreatureCategory> m_creatureCategories;

        // Faction keyword -> pattern indices (for faction-specific speech)
        std::unordered_map<std::string, std::vector<uint32>> m_patternsByFaction;

        // Pattern ID counter
        uint32 m_nextPatternId;

        // Thread safety
        mutable std::shared_mutex m_mutex;

        // Random number generator
        mutable std::mt19937 m_rng;
    };

} // namespace AliveWorld

#endif // ALIVE_WORLD_SPEECH_PATTERN_MANAGER_H
