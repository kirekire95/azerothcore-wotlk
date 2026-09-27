/*
 * mod-alive-world - Speech Pattern Manager
 * Copyright (C) 2025 EC
 *
 * Loads and manages NPC speech patterns from creature_text table.
 */

#include "SpeechPatternManager.h"
#include "DatabaseEnv.h"
#include "Field.h"
#include "QueryResult.h"
#include "Log.h"

namespace AliveWorld
{
    SpeechPatternManager::SpeechPatternManager()
        : m_nextPatternId(1)
        , m_rng(std::random_device{}())
    {
    }

    void SpeechPatternManager::Initialize()
    {
        LOG_INFO("module.alive-world", "SpeechPatternManager: Initializing...");

        LoadCreatureTypesFromDB();
        LoadPatternsFromDB();

        LOG_INFO("module.alive-world", "SpeechPatternManager: Loaded {} speech patterns",
            GetTotalPatternCount());

        // Log breakdown by context
        auto contextCounts = GetPatternCountByContext();
        for (const auto& [ctx, count] : contextCounts)
        {
            LOG_DEBUG("module.alive-world", "  {} patterns: {}",
                SpeechContextToString(ctx), count);
        }
    }

    void SpeechPatternManager::LoadCreatureTypesFromDB()
    {
        // Load creature types from creature_template for categorization
        // creature_template.type: 1=Beast, 2=Dragonkin, 3=Demon, 4=Elemental,
        //                         5=Giant, 6=Undead, 7=Humanoid, 8=Critter,
        //                         9=Mechanical, 10=NotSpecified, 11=Totem, 12=NonCombatPet
        QueryResult result = WorldDatabase.Query(
            "SELECT entry, type FROM creature_template WHERE entry IN "
            "(SELECT DISTINCT CreatureID FROM creature_text)"
        );

        if (!result)
        {
            LOG_WARN("module.alive-world", "SpeechPatternManager: No creature types found");
            return;
        }

        uint32 count = 0;
        do
        {
            Field* fields = result->Fetch();
            uint32 entry = fields[0].Get<uint32>();
            uint8 type = fields[1].Get<uint8>();

            CreatureCategory category = CreatureCategory::GENERIC;
            switch (type)
            {
                case 1:  category = CreatureCategory::BEAST; break;       // Beast
                case 2:  category = CreatureCategory::DRAGON; break;      // Dragonkin
                case 3:  category = CreatureCategory::DEMON; break;       // Demon
                case 4:  category = CreatureCategory::ELEMENTAL; break;   // Elemental
                case 5:  category = CreatureCategory::GIANT; break;       // Giant
                case 6:  category = CreatureCategory::UNDEAD; break;      // Undead
                case 7:  category = CreatureCategory::HUMANOID; break;    // Humanoid
                case 9:  category = CreatureCategory::MECHANICAL; break;  // Mechanical
                case 10: category = CreatureCategory::ABERRATION; break;  // Not Specified (Old Gods, etc.)
                default: category = CreatureCategory::GENERIC; break;
            }

            m_creatureCategories[entry] = category;
            count++;
        } while (result->NextRow());

        LOG_DEBUG("module.alive-world", "SpeechPatternManager: Loaded {} creature categories", count);
    }

    void SpeechPatternManager::LoadPatternsFromDB()
    {
        std::unique_lock lock(m_mutex);

        m_allPatterns.clear();
        m_patternsByContext.clear();
        m_patternsByCategory.clear();
        m_patternsByFaction.clear();

        // Load from creature_text - we want patterns that are reusable
        // Skip %s patterns (emote format) unless specifically for EMOTE context
        // Skip very long dialogue (event-specific)
        QueryResult result = WorldDatabase.Query(
            "SELECT ct.CreatureID, ct.GroupID, ct.ID, ct.Text, ct.Type, "
            "ct.Language, ct.Probability, ct.Emote, ct.comment, "
            "COALESCE(c.name, '') as creature_name "
            "FROM creature_text ct "
            "LEFT JOIN creature_template c ON ct.CreatureID = c.entry "
            "WHERE ct.Text IS NOT NULL "
            "AND LENGTH(ct.Text) > 3 "
            "AND LENGTH(ct.Text) < 200 "
            "ORDER BY ct.CreatureID, ct.GroupID, ct.ID"
        );

        if (!result)
        {
            LOG_WARN("module.alive-world", "SpeechPatternManager: No patterns found in creature_text");
            return;
        }

        // Faction keywords for categorization
        std::vector<std::pair<std::string, std::string>> factionKeywords = {
            {"Defias", "defias"},
            {"Brotherhood", "defias"},
            {"Scarlet", "scarlet"},
            {"Crusade", "scarlet"},
            {"Scourge", "scourge"},
            {"Horde", "horde"},
            {"Alliance", "alliance"},
            {"Syndicate", "syndicate"},
            {"Blackrock", "blackrock"},
            {"Burning Legion", "legion"},
            {"Twilight", "twilight"},
            {"Argent", "argent"}
        };

        do
        {
            Field* fields = result->Fetch();

            SpeechPattern pattern;
            pattern.patternId = m_nextPatternId++;
            pattern.creatureEntry = fields[0].Get<uint32>();
            pattern.text = fields[3].Get<std::string>();
            pattern.textType = static_cast<CreatureTextType>(fields[4].Get<uint8>());
            pattern.languageId = fields[5].Get<int8>();
            pattern.probability = fields[6].Get<float>();
            pattern.emoteId = fields[7].Get<uint32>();
            pattern.comment = fields[8].Get<std::string>();
            pattern.creatureName = fields[9].Get<std::string>();

            // Determine context from comment
            pattern.context = ParseContextFromComment(pattern.comment);

            // If text starts with %s, it's emote format
            if (pattern.text.find("%s") == 0)
            {
                pattern.context = SpeechContext::EMOTE;
            }

            // Get creature category
            pattern.category = GetCreatureCategory(pattern.creatureEntry);

            // Add to main list
            uint32 index = static_cast<uint32>(m_allPatterns.size());
            m_allPatterns.push_back(pattern);

            // Index by context
            m_patternsByContext[pattern.context].push_back(index);

            // Index by category
            m_patternsByCategory[pattern.category].push_back(index);

            // Index by faction keywords
            for (const auto& [keyword, faction] : factionKeywords)
            {
                if (pattern.text.find(keyword) != std::string::npos ||
                    pattern.comment.find(keyword) != std::string::npos ||
                    pattern.creatureName.find(keyword) != std::string::npos)
                {
                    m_patternsByFaction[faction].push_back(index);
                    break; // Only one faction per pattern
                }
            }

        } while (result->NextRow());

        LOG_INFO("module.alive-world", "SpeechPatternManager: Indexed {} patterns", m_allPatterns.size());
    }

    SpeechContext SpeechPatternManager::ParseContextFromComment(const std::string& comment) const
    {
        std::string lower = comment;
        std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);

        // Combat-related
        if (lower.find("aggro") != std::string::npos ||
            lower.find("on aggro") != std::string::npos ||
            lower.find("enter combat") != std::string::npos)
        {
            return SpeechContext::AGGRO;
        }

        if (lower.find("death") != std::string::npos ||
            lower.find("on death") != std::string::npos ||
            lower.find("dying") != std::string::npos)
        {
            return SpeechContext::DEATH;
        }

        if (lower.find("kill") != std::string::npos ||
            lower.find("slay") != std::string::npos)
        {
            return SpeechContext::KILL;
        }

        if (lower.find("enrage") != std::string::npos ||
            lower.find("berserk") != std::string::npos ||
            lower.find("anger") != std::string::npos)
        {
            return SpeechContext::THREAT;
        }

        if (lower.find("fear") != std::string::npos ||
            lower.find("flee") != std::string::npos ||
            lower.find("run") != std::string::npos)
        {
            return SpeechContext::FEAR;
        }

        // Event-related
        if (lower.find("event") != std::string::npos ||
            lower.find("script") != std::string::npos ||
            lower.find("wp_") != std::string::npos ||
            lower.find("say_") != std::string::npos)
        {
            return SpeechContext::EVENT;
        }

        // Emote text
        if (lower.find("emote") != std::string::npos)
        {
            return SpeechContext::EMOTE;
        }

        // Greetings
        if (lower.find("greet") != std::string::npos ||
            lower.find("hello") != std::string::npos ||
            lower.find("welcome") != std::string::npos)
        {
            return SpeechContext::GREETING;
        }

        // Default to combat for most patterns (since creature_text is mainly combat dialogue)
        return SpeechContext::COMBAT;
    }

    CreatureCategory SpeechPatternManager::GetCreatureCategory(uint32 creatureEntry) const
    {
        auto it = m_creatureCategories.find(creatureEntry);
        return (it != m_creatureCategories.end()) ? it->second : CreatureCategory::GENERIC;
    }

    const SpeechPattern* SpeechPatternManager::GetRandomPattern(SpeechContext context) const
    {
        std::shared_lock lock(m_mutex);

        auto it = m_patternsByContext.find(context);
        if (it == m_patternsByContext.end() || it->second.empty())
            return nullptr;

        std::uniform_int_distribution<size_t> dist(0, it->second.size() - 1);
        uint32 index = it->second[dist(m_rng)];

        return (index < m_allPatterns.size()) ? &m_allPatterns[index] : nullptr;
    }

    const SpeechPattern* SpeechPatternManager::GetRandomPattern(SpeechContext context,
                                                                 CreatureCategory category) const
    {
        std::shared_lock lock(m_mutex);

        auto ctxIt = m_patternsByContext.find(context);
        if (ctxIt == m_patternsByContext.end() || ctxIt->second.empty())
            return nullptr;

        // Filter by category
        std::vector<uint32> matching;
        for (uint32 idx : ctxIt->second)
        {
            if (idx < m_allPatterns.size() && m_allPatterns[idx].category == category)
            {
                matching.push_back(idx);
            }
        }

        // Fall back to any pattern if no category match
        if (matching.empty())
        {
            std::uniform_int_distribution<size_t> dist(0, ctxIt->second.size() - 1);
            uint32 index = ctxIt->second[dist(m_rng)];
            return (index < m_allPatterns.size()) ? &m_allPatterns[index] : nullptr;
        }

        std::uniform_int_distribution<size_t> dist(0, matching.size() - 1);
        uint32 index = matching[dist(m_rng)];
        return &m_allPatterns[index];
    }

    const SpeechPattern* SpeechPatternManager::GetPatternForPersonality(
        SpeechContext context, const BotPersonality& personality) const
    {
        std::shared_lock lock(m_mutex);

        auto ctxIt = m_patternsByContext.find(context);
        if (ctxIt == m_patternsByContext.end() || ctxIt->second.empty())
            return nullptr;

        // Collect patterns that match personality
        std::vector<uint32> matching;
        for (uint32 idx : ctxIt->second)
        {
            if (idx < m_allPatterns.size() && MatchesPersonality(m_allPatterns[idx], personality))
            {
                matching.push_back(idx);
            }
        }

        // Fall back to any pattern if no personality match
        if (matching.empty())
        {
            return GetRandomPattern(context);
        }

        std::uniform_int_distribution<size_t> dist(0, matching.size() - 1);
        uint32 index = matching[dist(m_rng)];
        return &m_allPatterns[index];
    }

    bool SpeechPatternManager::MatchesPersonality(const SpeechPattern& pattern,
                                                   const BotPersonality& personality) const
    {
        std::string lower = pattern.text;
        std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);

        // High aggression matches threatening speech
        if (personality.aggression > 0.7f)
        {
            if (lower.find("die") != std::string::npos ||
                lower.find("kill") != std::string::npos ||
                lower.find("destroy") != std::string::npos ||
                lower.find("crush") != std::string::npos ||
                lower.find("meat") != std::string::npos ||
                lower.find("blood") != std::string::npos)
            {
                return true;
            }
        }

        // High caution matches fearful speech
        if (personality.caution > 0.7f)
        {
            if (lower.find("run") != std::string::npos ||
                lower.find("flee") != std::string::npos ||
                lower.find("no kill") != std::string::npos ||
                lower.find("spare") != std::string::npos)
            {
                return true;
            }
        }

        // High loyalty matches faction speech
        if (personality.loyalty > 0.7f)
        {
            if (lower.find("brotherhood") != std::string::npos ||
                lower.find("crusade") != std::string::npos ||
                lower.find("horde") != std::string::npos ||
                lower.find("alliance") != std::string::npos ||
                lower.find("for the") != std::string::npos)
            {
                return true;
            }
        }

        // High vengefulness matches revenge speech
        if (personality.vengefulness > 0.7f)
        {
            if (lower.find("revenge") != std::string::npos ||
                lower.find("pay") != std::string::npos ||
                lower.find("suffer") != std::string::npos ||
                lower.find("monster") != std::string::npos)
            {
                return true;
            }
        }

        // Low aggression, high sociability - avoid violent speech
        if (personality.aggression < 0.3f && personality.sociability > 0.5f)
        {
            // Prefer greetings and less violent speech
            if (lower.find("greet") != std::string::npos ||
                lower.find("hello") != std::string::npos ||
                lower.find("friend") != std::string::npos ||
                lower.find("welcome") != std::string::npos)
            {
                return true;
            }
            // Avoid very violent speech
            if (lower.find("die") != std::string::npos ||
                lower.find("blood") != std::string::npos ||
                lower.find("flesh") != std::string::npos)
            {
                return false;
            }
        }

        // Default: some chance to match any pattern
        return (std::uniform_real_distribution<float>(0, 1)(m_rng) < 0.3f);
    }

    std::vector<const SpeechPattern*> SpeechPatternManager::GetAggressivePatterns(uint32 limit) const
    {
        std::shared_lock lock(m_mutex);
        std::vector<const SpeechPattern*> result;

        for (const auto& pattern : m_allPatterns)
        {
            std::string lower = pattern.text;
            std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);

            if (lower.find("die") != std::string::npos ||
                lower.find("kill") != std::string::npos ||
                lower.find("destroy") != std::string::npos ||
                lower.find("crush") != std::string::npos ||
                lower.find("taste blade") != std::string::npos)
            {
                result.push_back(&pattern);
                if (result.size() >= limit) break;
            }
        }
        return result;
    }

    std::vector<const SpeechPattern*> SpeechPatternManager::GetFearfulPatterns(uint32 limit) const
    {
        std::shared_lock lock(m_mutex);
        std::vector<const SpeechPattern*> result;

        for (const auto& pattern : m_allPatterns)
        {
            std::string lower = pattern.text;
            std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);

            if (lower.find("run") != std::string::npos ||
                lower.find("flee") != std::string::npos ||
                lower.find("no kill") != std::string::npos ||
                lower.find("spare") != std::string::npos ||
                lower.find("mercy") != std::string::npos)
            {
                result.push_back(&pattern);
                if (result.size() >= limit) break;
            }
        }
        return result;
    }

    std::vector<const SpeechPattern*> SpeechPatternManager::GetBoastfulPatterns(uint32 limit) const
    {
        std::shared_lock lock(m_mutex);
        std::vector<const SpeechPattern*> result;

        for (const auto& pattern : m_allPatterns)
        {
            std::string lower = pattern.text;
            std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);

            if (lower.find("power") != std::string::npos ||
                lower.find("mighty") != std::string::npos ||
                lower.find("fool") != std::string::npos ||
                lower.find("weak") != std::string::npos ||
                lower.find("puny") != std::string::npos ||
                lower.find("cannot defeat") != std::string::npos)
            {
                result.push_back(&pattern);
                if (result.size() >= limit) break;
            }
        }
        return result;
    }

    std::vector<const SpeechPattern*> SpeechPatternManager::GetFactionPatterns(
        const std::string& faction, uint32 limit) const
    {
        std::shared_lock lock(m_mutex);
        std::vector<const SpeechPattern*> result;

        std::string lowerFaction = faction;
        std::transform(lowerFaction.begin(), lowerFaction.end(), lowerFaction.begin(), ::tolower);

        auto it = m_patternsByFaction.find(lowerFaction);
        if (it == m_patternsByFaction.end())
            return result;

        for (uint32 idx : it->second)
        {
            if (idx < m_allPatterns.size())
            {
                result.push_back(&m_allPatterns[idx]);
                if (result.size() >= limit) break;
            }
        }
        return result;
    }

    std::string SpeechPatternManager::ProcessPatternText(const std::string& pattern,
                                                          const std::string& botName,
                                                          const std::string& targetName,
                                                          const std::string& zoneName) const
    {
        std::string result = pattern;

        // Replace WoW placeholders
        // $n, $N = player name (target)
        // $c, $C = player class
        // $r, $R = player race
        // %s = creature name (at start of emote text)

        size_t pos;

        // Replace %s at start with bot name (for emote format)
        if (result.find("%s ") == 0)
        {
            result = botName + result.substr(2);
        }
        else
        {
            // Replace %s elsewhere
            while ((pos = result.find("%s")) != std::string::npos)
            {
                result.replace(pos, 2, botName);
            }
        }

        // Replace $n/$N with target name
        while ((pos = result.find("$n")) != std::string::npos)
        {
            result.replace(pos, 2, targetName.empty() ? "you" : targetName);
        }
        while ((pos = result.find("$N")) != std::string::npos)
        {
            result.replace(pos, 2, targetName.empty() ? "You" : targetName);
        }

        // Replace zone placeholders
        while ((pos = result.find("{zone}")) != std::string::npos)
        {
            result.replace(pos, 6, zoneName.empty() ? "these lands" : zoneName);
        }

        return result;
    }

    uint32 SpeechPatternManager::GetTotalPatternCount() const
    {
        std::shared_lock lock(m_mutex);
        return static_cast<uint32>(m_allPatterns.size());
    }

    uint32 SpeechPatternManager::GetPatternCount(SpeechContext context) const
    {
        std::shared_lock lock(m_mutex);
        auto it = m_patternsByContext.find(context);
        return (it != m_patternsByContext.end()) ? static_cast<uint32>(it->second.size()) : 0;
    }

    uint32 SpeechPatternManager::GetPatternCount(CreatureCategory category) const
    {
        std::shared_lock lock(m_mutex);
        auto it = m_patternsByCategory.find(category);
        return (it != m_patternsByCategory.end()) ? static_cast<uint32>(it->second.size()) : 0;
    }

    std::map<SpeechContext, uint32> SpeechPatternManager::GetPatternCountByContext() const
    {
        std::shared_lock lock(m_mutex);
        std::map<SpeechContext, uint32> result;
        for (const auto& [ctx, indices] : m_patternsByContext)
        {
            result[ctx] = static_cast<uint32>(indices.size());
        }
        return result;
    }

    std::map<CreatureCategory, uint32> SpeechPatternManager::GetPatternCountByCategory() const
    {
        std::shared_lock lock(m_mutex);
        std::map<CreatureCategory, uint32> result;
        for (const auto& [cat, indices] : m_patternsByCategory)
        {
            result[cat] = static_cast<uint32>(indices.size());
        }
        return result;
    }

} // namespace AliveWorld
