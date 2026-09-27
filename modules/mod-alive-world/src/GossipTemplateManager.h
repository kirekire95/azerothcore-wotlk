/*
 * mod-alive-world - Gossip Template Manager
 * Copyright (C) 2025 EC
 *
 * Loads gossip templates from the database and provides randomized
 * headline generation with placeholder substitution.
 */

#ifndef ALIVE_WORLD_GOSSIP_TEMPLATE_MANAGER_H
#define ALIVE_WORLD_GOSSIP_TEMPLATE_MANAGER_H

#include "AliveWorldTypes.h"

#include <atomic>
#include <functional>
#include <mutex>
#include <random>
#include <shared_mutex>
#include <unordered_map>
#include <vector>

namespace AliveWorld
{
    /**
     * A gossip template loaded from the database.
     */
    struct GossipTemplate
    {
        uint32 id;              // Template ID
        WorldEventType eventType;
        std::string templateText;  // Text with placeholders: {primary}, {secondary}, {zone}
        uint32 weight;          // Selection probability weight
        uint8 minImportance;    // Minimum importance for this template
        uint8 maxImportance;    // Maximum importance for this template
        bool isExclamation;     // Ends with ! (more urgent)
        std::string category;   // Optional categorization (combat, social, world)
    };

    /**
     * Manages gossip templates loaded from the database.
     * Templates are selected based on event type and importance, with
     * weighted random selection for variety.
     */
    class GossipTemplateManager
    {
    public:
        GossipTemplateManager();
        ~GossipTemplateManager() = default;

        // Initialize and load templates
        void Initialize();
        void LoadTemplatesFromDB();
        void LoadBroadcastPatterns();  // Load from broadcast_text extraction
        void LoadDefaultTemplates();  // Fallback if DB is empty

        // Generate a headline for an event
        std::string GenerateHeadline(const WorldEvent& event) const;
        std::string GenerateHeadline(WorldEventType type,
                                     const std::string& primaryName,
                                     const std::string& secondaryName,
                                     const std::string& zoneName,
                                     uint8 importance) const;

        // Template management
        void AddTemplate(const GossipTemplate& tmpl);
        uint32 GetTemplateCount() const;
        uint32 GetTemplateCountForType(WorldEventType type) const;

        // AI Tweaking (uses Ollama to vary templates)
        void SetAITweakEnabled(bool enabled) { m_aiTweakEnabled = enabled; }
        bool IsAITweakEnabled() const { return m_aiTweakEnabled; }
        void SetAITweakChance(uint32 chance) { m_aiTweakChance = chance; }  // 0-100
        uint32 GetAITweakChance() const { return m_aiTweakChance; }

        // Generate with optional AI tweaking (async callback)
        void GenerateHeadlineWithTweak(const WorldEvent& event,
                                       const BotPersonality& personality,
                                       std::function<void(const std::string&, bool)> callback);

        // Stats
        bool IsLoaded() const { return m_loaded; }
        uint32 GetTweakedCount() const { return m_tweakedCount; }

    private:
        // Select a template for the given event type and importance
        const GossipTemplate* SelectTemplate(WorldEventType type, uint8 importance) const;

        // Replace placeholders in template
        std::string SubstitutePlaceholders(const std::string& tmpl,
                                           const std::string& primary,
                                           const std::string& secondary,
                                           const std::string& zone) const;

        // Map event type enum to string for DB queries
        static std::string EventTypeToString(WorldEventType type);
        static WorldEventType StringToEventType(const std::string& str);

        // Templates indexed by event type
        std::unordered_map<WorldEventType, std::vector<GossipTemplate>> m_templates;
        mutable std::shared_mutex m_mutex;

        // Random number generator for weighted selection
        mutable std::mt19937 m_rng;

        bool m_loaded;
        uint32 m_totalTemplates;

        // AI Tweaking configuration
        bool m_aiTweakEnabled;
        uint32 m_aiTweakChance;           // 0-100 percentage
        mutable std::atomic<uint32> m_tweakedCount;

        // Reference to AI engine (set externally)
        class AIDecisionEngine* m_aiEngine;

    public:
        // Set the AI engine reference for tweaking
        void SetAIEngine(AIDecisionEngine* engine) { m_aiEngine = engine; }
    };

} // namespace AliveWorld

#endif // ALIVE_WORLD_GOSSIP_TEMPLATE_MANAGER_H
