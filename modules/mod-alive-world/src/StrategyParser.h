/*
 * mod-alive-world - Strategy Parser
 * Copyright (C) 2025 EC
 *
 * Parses structured AI/LLM responses into actionable bot behaviors.
 * Extracts [STRATEGY], [CHAT], [EMOTE] blocks and queues appropriate
 * behaviors via BehaviorExecutor.
 *
 * Expected AI Response Format:
 * ===========================
 * [STRATEGY]
 * style: cautious
 * target: nearest_enemy
 * action: flee_if_health_low
 * [CHAT]
 * "This doesn't look good... maybe we should retreat?"
 * [EMOTE]
 * worried
 *
 * If no blocks are found, the entire response is treated as a chat message.
 */

#ifndef ALIVE_WORLD_STRATEGY_PARSER_H
#define ALIVE_WORLD_STRATEGY_PARSER_H

#include "AliveWorldTypes.h"
#include "BehaviorExecutor.h"

#include <map>
#include <optional>
#include <string>
#include <vector>

namespace AliveWorld
{
    class BehaviorExecutor;

    // =========================================================================
    // PARSED STRATEGY BLOCK
    // =========================================================================

    struct StrategyBlock
    {
        // Combat style
        std::optional<CombatStyle> style;

        // Target selection
        std::string targetType;     // "nearest_enemy", "lowest_health", "highest_threat", etc.
        uint64 targetGuid = 0;      // Specific target if resolved

        // Conditional actions
        std::string action;         // "flee_if_health_low", "attack", "defend", "call_help", etc.
        float healthThreshold = 0.3f;  // For health-based conditions

        // Movement intent
        bool shouldFlee = false;
        bool shouldAdvance = false;
        float fleeDistance = 30.0f;

        // Utility
        bool isEmpty() const
        {
            return !style.has_value() && targetType.empty() && action.empty();
        }
    };

    // =========================================================================
    // PARSED CHAT BLOCK
    // =========================================================================

    struct ChatBlock
    {
        std::string message;        // The actual chat text
        bool isYell = false;        // Should yell instead of say
        uint64 whisperTarget = 0;   // If set, whisper to this target

        bool isEmpty() const { return message.empty(); }
    };

    // =========================================================================
    // PARSED EMOTE BLOCK
    // =========================================================================

    struct EmoteBlock
    {
        std::string emoteName;      // "worried", "cheer", "dance", "flex", etc.
        uint32 emoteId = 0;         // Resolved emote ID (0 = text emote)

        bool isEmpty() const { return emoteName.empty() && emoteId == 0; }
    };

    // =========================================================================
    // COMPLETE PARSED RESPONSE
    // =========================================================================

    struct ParsedAIResponse
    {
        StrategyBlock strategy;
        ChatBlock chat;
        EmoteBlock emote;
        std::string rawResponse;    // Original unparsed response
        bool wasStructured = false; // True if any blocks were found

        bool hasStrategy() const { return !strategy.isEmpty(); }
        bool hasChat() const { return !chat.isEmpty(); }
        bool hasEmote() const { return !emote.isEmpty(); }
        bool isEmpty() const { return !hasStrategy() && !hasChat() && !hasEmote(); }
    };

    // =========================================================================
    // STRATEGY PARSER
    // =========================================================================

    class StrategyParser
    {
    public:
        StrategyParser();
        ~StrategyParser() = default;

        // Initialization
        void Initialize(BehaviorExecutor* behaviorExecutor);
        void LoadEmoteMap();

        // Parse raw AI response into structured blocks
        ParsedAIResponse Parse(const std::string& response);

        // Execute parsed response as bot behaviors
        // Returns number of behaviors queued
        uint32 Execute(uint64 botGuid, const ParsedAIResponse& parsed,
                      BehaviorPriority priority = BehaviorPriority::NORMAL);

        // Convenience: Parse and execute in one call
        uint32 ParseAndExecute(uint64 botGuid, const std::string& response,
                               BehaviorPriority priority = BehaviorPriority::NORMAL);

        // Parse individual blocks (exposed for testing)
        StrategyBlock ParseStrategyBlock(const std::string& blockContent);
        ChatBlock ParseChatBlock(const std::string& blockContent);
        EmoteBlock ParseEmoteBlock(const std::string& blockContent);

        // Style string to enum conversion
        std::optional<CombatStyle> ParseCombatStyle(const std::string& style);

        // Resolve emote name to ID
        uint32 ResolveEmoteId(const std::string& emoteName);

        // Statistics
        uint32 GetTotalParsed() const { return m_totalParsed; }
        uint32 GetStructuredCount() const { return m_structuredCount; }
        uint32 GetFallbackCount() const { return m_fallbackCount; }

    private:
        // Extract block content between [BLOCKNAME] and next [BLOCK] or end
        std::string ExtractBlock(const std::string& response,
                                 const std::string& blockName);

        // Parse key: value pairs from block content
        std::map<std::string, std::string> ParseKeyValuePairs(const std::string& content);

        // Trim whitespace and quotes from string
        std::string Trim(const std::string& str);
        std::string TrimQuotes(const std::string& str);

        // Reference to behavior executor
        BehaviorExecutor* m_behaviorExecutor = nullptr;

        // Emote name -> ID mapping
        std::map<std::string, uint32> m_emoteMap;

        // Combat style name -> enum mapping
        std::map<std::string, CombatStyle> m_styleMap;

        // Statistics
        uint32 m_totalParsed = 0;
        uint32 m_structuredCount = 0;
        uint32 m_fallbackCount = 0;
    };

} // namespace AliveWorld

#endif // ALIVE_WORLD_STRATEGY_PARSER_H
