/*
 * mod-alive-world - Strategy Parser Implementation
 * Copyright (C) 2025 EC
 */

#include "StrategyParser.h"
#include "AliveWorldManager.h"
#include "BehaviorExecutor.h"

#include <algorithm>
#include <cctype>
#include <regex>
#include <sstream>

namespace AliveWorld
{

StrategyParser::StrategyParser()
{
    // Initialize style mapping
    m_styleMap["aggressive"] = CombatStyle::AGGRESSIVE;
    m_styleMap["defensive"] = CombatStyle::DEFENSIVE;
    m_styleMap["passive"] = CombatStyle::PASSIVE;
    m_styleMap["berserker"] = CombatStyle::BERSERKER;
    m_styleMap["cautious"] = CombatStyle::CAUTIOUS;

    // Common aliases
    m_styleMap["attack"] = CombatStyle::AGGRESSIVE;
    m_styleMap["defend"] = CombatStyle::DEFENSIVE;
    m_styleMap["careful"] = CombatStyle::CAUTIOUS;
    m_styleMap["berserk"] = CombatStyle::BERSERKER;
    m_styleMap["rage"] = CombatStyle::BERSERKER;
    m_styleMap["safe"] = CombatStyle::CAUTIOUS;
    m_styleMap["retreat"] = CombatStyle::CAUTIOUS;
}

void StrategyParser::Initialize(BehaviorExecutor* behaviorExecutor)
{
    ALIVE_LOG_INFO("StrategyParser initializing...");

    m_behaviorExecutor = behaviorExecutor;
    LoadEmoteMap();

    ALIVE_LOG_INFO("StrategyParser initialized with %zu emotes, %zu styles",
                   m_emoteMap.size(), m_styleMap.size());
}

void StrategyParser::LoadEmoteMap()
{
    // Common emotes with their IDs
    // These are text emotes that work with /emote command
    m_emoteMap["wave"] = 21;        // EMOTE_ONESHOT_WAVE
    m_emoteMap["bow"] = 2;          // EMOTE_ONESHOT_BOW
    m_emoteMap["cheer"] = 4;        // EMOTE_ONESHOT_CHEER
    m_emoteMap["cry"] = 18;         // EMOTE_ONESHOT_CRY
    m_emoteMap["dance"] = 10;       // EMOTE_STATE_DANCE
    m_emoteMap["eat"] = 7;          // EMOTE_ONESHOT_EAT
    m_emoteMap["flex"] = 11;        // EMOTE_ONESHOT_FLEX
    m_emoteMap["kiss"] = 17;        // EMOTE_ONESHOT_KISS
    m_emoteMap["kneel"] = 16;       // EMOTE_ONESHOT_KNEEL
    m_emoteMap["laugh"] = 15;       // EMOTE_ONESHOT_LAUGH
    m_emoteMap["point"] = 25;       // EMOTE_ONESHOT_POINT
    m_emoteMap["roar"] = 22;        // EMOTE_ONESHOT_ROAR
    m_emoteMap["salute"] = 66;      // EMOTE_ONESHOT_SALUTE
    m_emoteMap["shrug"] = 94;       // EMOTE_ONESHOT_SHRUG
    m_emoteMap["talk"] = 1;         // EMOTE_ONESHOT_TALK
    m_emoteMap["yes"] = 185;        // EMOTE_ONESHOT_YES
    m_emoteMap["no"] = 274;         // EMOTE_ONESHOT_NO
    m_emoteMap["question"] = 65;    // EMOTE_ONESHOT_QUESTION
    m_emoteMap["exclamation"] = 5;  // EMOTE_ONESHOT_EXCLAMATION
    m_emoteMap["applaud"] = 41;     // EMOTE_ONESHOT_APPLAUD
    m_emoteMap["beg"] = 61;         // EMOTE_ONESHOT_BEG
    m_emoteMap["cower"] = 20;       // EMOTE_STATE_COWER
    m_emoteMap["rude"] = 14;        // EMOTE_ONESHOT_RUDE
    m_emoteMap["shy"] = 93;         // EMOTE_ONESHOT_SHY
    m_emoteMap["sleep"] = 6;        // EMOTE_STATE_SLEEP
    m_emoteMap["train"] = 171;      // EMOTE_ONESHOT_TRAIN
    m_emoteMap["chicken"] = 82;     // EMOTE_ONESHOT_CHICKEN
    m_emoteMap["attackunarmed"] = 26; // EMOTE_ONESHOT_ATTACKUNARMED

    // Aliases for mood-based selection
    m_emoteMap["worried"] = 65;     // question (looks around worried)
    m_emoteMap["sad"] = 18;         // cry
    m_emoteMap["happy"] = 4;        // cheer
    m_emoteMap["angry"] = 22;       // roar
    m_emoteMap["scared"] = 20;      // cower
    m_emoteMap["confused"] = 94;    // shrug
    m_emoteMap["excited"] = 4;      // cheer
    m_emoteMap["tired"] = 6;        // sleep
    m_emoteMap["proud"] = 11;       // flex
    m_emoteMap["grateful"] = 2;     // bow
    m_emoteMap["aggressive"] = 22;  // roar
    m_emoteMap["friendly"] = 21;    // wave
    m_emoteMap["dismissive"] = 94;  // shrug
    m_emoteMap["submissive"] = 16;  // kneel
    m_emoteMap["celebratory"] = 10; // dance
    m_emoteMap["pleading"] = 61;    // beg
    m_emoteMap["mocking"] = 82;     // chicken
    m_emoteMap["respectful"] = 66;  // salute
    m_emoteMap["nervous"] = 65;     // question
    m_emoteMap["defiant"] = 14;     // rude
}

ParsedAIResponse StrategyParser::Parse(const std::string& response)
{
    m_totalParsed++;

    ParsedAIResponse parsed;
    parsed.rawResponse = response;

    // Try to extract structured blocks
    std::string strategyContent = ExtractBlock(response, "STRATEGY");
    std::string chatContent = ExtractBlock(response, "CHAT");
    std::string emoteContent = ExtractBlock(response, "EMOTE");

    // Parse each block if found
    if (!strategyContent.empty())
    {
        parsed.strategy = ParseStrategyBlock(strategyContent);
        parsed.wasStructured = true;
    }

    if (!chatContent.empty())
    {
        parsed.chat = ParseChatBlock(chatContent);
        parsed.wasStructured = true;
    }

    if (!emoteContent.empty())
    {
        parsed.emote = ParseEmoteBlock(emoteContent);
        parsed.wasStructured = true;
    }

    // Track statistics
    if (parsed.wasStructured)
    {
        m_structuredCount++;
        ALIVE_LOG_DEBUG("StrategyParser: Parsed structured response (strategy=%d, chat=%d, emote=%d)",
                        parsed.hasStrategy(), parsed.hasChat(), parsed.hasEmote());
    }
    else
    {
        // Fallback: treat entire response as chat message
        m_fallbackCount++;
        parsed.chat.message = Trim(response);
        ALIVE_LOG_DEBUG("StrategyParser: Using fallback (raw text as chat)");
    }

    return parsed;
}

uint32 StrategyParser::Execute(uint64 botGuid, const ParsedAIResponse& parsed,
                               BehaviorPriority priority)
{
    if (!m_behaviorExecutor)
    {
        ALIVE_LOG_WARN("StrategyParser::Execute - No BehaviorExecutor available");
        return 0;
    }

    uint32 behaviorsQueued = 0;

    // Execute strategy changes
    if (parsed.hasStrategy())
    {
        const StrategyBlock& strat = parsed.strategy;

        // Apply combat style
        if (strat.style.has_value())
        {
            m_behaviorExecutor->SetCombatStyle(botGuid, *strat.style);
            behaviorsQueued++;
            ALIVE_LOG_DEBUG("StrategyParser: Set combat style for bot %llu", botGuid);
        }

        // Handle flee action
        if (strat.shouldFlee || strat.action == "flee" || strat.action == "flee_if_health_low")
        {
            // Queue flee behavior with distance
            BehaviorRequest request;
            request.botGuid = botGuid;
            request.type = BehaviorType::MOVE_FLEE;
            request.priority = BehaviorPriority::HIGH;  // Fleeing is urgent
            request.targetX = strat.fleeDistance;       // Use as distance parameter
            m_behaviorExecutor->QueueBehavior(request);
            behaviorsQueued++;
            ALIVE_LOG_DEBUG("StrategyParser: Queued flee for bot %llu", botGuid);
        }

        // Handle call for help
        if (strat.action == "call_help" || strat.action == "call_for_help")
        {
            m_behaviorExecutor->CallForHelp(botGuid);
            behaviorsQueued++;
            ALIVE_LOG_DEBUG("StrategyParser: Queued call for help for bot %llu", botGuid);
        }

        // Handle target changes
        if (strat.targetGuid != 0)
        {
            m_behaviorExecutor->SetTarget(botGuid, strat.targetGuid);
            behaviorsQueued++;
        }
    }

    // Execute chat
    if (parsed.hasChat())
    {
        const ChatBlock& chat = parsed.chat;

        if (chat.whisperTarget != 0)
        {
            m_behaviorExecutor->Whisper(botGuid, chat.whisperTarget, chat.message);
        }
        else if (chat.isYell)
        {
            BehaviorRequest request;
            request.botGuid = botGuid;
            request.type = BehaviorType::CHAT_YELL;
            request.message = chat.message;
            request.priority = priority;
            m_behaviorExecutor->QueueBehavior(request);
        }
        else
        {
            m_behaviorExecutor->Say(botGuid, chat.message, priority);
        }
        behaviorsQueued++;
        ALIVE_LOG_DEBUG("StrategyParser: Queued chat for bot %llu: \"%s\"",
                        botGuid, chat.message.c_str());
    }

    // Execute emote
    if (parsed.hasEmote())
    {
        const EmoteBlock& emote = parsed.emote;

        if (emote.emoteId != 0)
        {
            // Use emote ID directly
            BehaviorRequest request;
            request.botGuid = botGuid;
            request.type = BehaviorType::CHAT_EMOTE;
            request.spellId = emote.emoteId;  // Reuse spellId for emote ID
            request.message = emote.emoteName;
            request.priority = priority;
            m_behaviorExecutor->QueueBehavior(request);
        }
        else
        {
            // Use text emote
            m_behaviorExecutor->Emote(botGuid, emote.emoteName, priority);
        }
        behaviorsQueued++;
        ALIVE_LOG_DEBUG("StrategyParser: Queued emote for bot %llu: %s",
                        botGuid, emote.emoteName.c_str());
    }

    return behaviorsQueued;
}

uint32 StrategyParser::ParseAndExecute(uint64 botGuid, const std::string& response,
                                       BehaviorPriority priority)
{
    ParsedAIResponse parsed = Parse(response);
    return Execute(botGuid, parsed, priority);
}

StrategyBlock StrategyParser::ParseStrategyBlock(const std::string& blockContent)
{
    StrategyBlock block;

    auto kvPairs = ParseKeyValuePairs(blockContent);

    // Parse style
    auto it = kvPairs.find("style");
    if (it != kvPairs.end())
    {
        block.style = ParseCombatStyle(it->second);
    }

    // Parse target
    it = kvPairs.find("target");
    if (it != kvPairs.end())
    {
        block.targetType = it->second;
    }

    // Parse action
    it = kvPairs.find("action");
    if (it != kvPairs.end())
    {
        block.action = it->second;

        // Detect flee actions
        if (block.action.find("flee") != std::string::npos ||
            block.action.find("retreat") != std::string::npos ||
            block.action.find("run") != std::string::npos)
        {
            block.shouldFlee = true;
        }

        // Detect advance actions
        if (block.action.find("advance") != std::string::npos ||
            block.action.find("charge") != std::string::npos ||
            block.action.find("attack") != std::string::npos)
        {
            block.shouldAdvance = true;
        }
    }

    // Parse health threshold
    it = kvPairs.find("health_threshold");
    if (it != kvPairs.end())
    {
        try
        {
            block.healthThreshold = std::stof(it->second);
        }
        catch (...)
        {
            block.healthThreshold = 0.3f;
        }
    }

    // Parse flee distance
    it = kvPairs.find("flee_distance");
    if (it != kvPairs.end())
    {
        try
        {
            block.fleeDistance = std::stof(it->second);
        }
        catch (...)
        {
            block.fleeDistance = 30.0f;
        }
    }

    return block;
}

ChatBlock StrategyParser::ParseChatBlock(const std::string& blockContent)
{
    ChatBlock block;

    std::string content = Trim(blockContent);

    // Check for YELL prefix
    if (content.substr(0, 5) == "YELL:")
    {
        block.isYell = true;
        content = Trim(content.substr(5));
    }
    else if (content.substr(0, 8) == "WHISPER:")
    {
        // Format: WHISPER:<guid>: <message>
        size_t colonPos = content.find(':', 8);
        if (colonPos != std::string::npos)
        {
            try
            {
                block.whisperTarget = std::stoull(content.substr(8, colonPos - 8));
                content = Trim(content.substr(colonPos + 1));
            }
            catch (...)
            {
                // Failed to parse GUID, just use the content as message
                content = Trim(content.substr(8));
            }
        }
    }

    // Remove surrounding quotes if present
    block.message = TrimQuotes(content);

    return block;
}

EmoteBlock StrategyParser::ParseEmoteBlock(const std::string& blockContent)
{
    EmoteBlock block;

    std::string emoteName = Trim(blockContent);

    // Convert to lowercase for lookup
    std::transform(emoteName.begin(), emoteName.end(), emoteName.begin(), ::tolower);

    // Remove common prefixes
    if (emoteName.substr(0, 1) == "/")
        emoteName = emoteName.substr(1);

    if (emoteName.substr(0, 6) == "emote_")
        emoteName = emoteName.substr(6);

    block.emoteName = emoteName;
    block.emoteId = ResolveEmoteId(emoteName);

    return block;
}

std::optional<CombatStyle> StrategyParser::ParseCombatStyle(const std::string& style)
{
    std::string lowerStyle = style;
    std::transform(lowerStyle.begin(), lowerStyle.end(), lowerStyle.begin(), ::tolower);

    auto it = m_styleMap.find(lowerStyle);
    if (it != m_styleMap.end())
    {
        return it->second;
    }

    return std::nullopt;
}

uint32 StrategyParser::ResolveEmoteId(const std::string& emoteName)
{
    auto it = m_emoteMap.find(emoteName);
    if (it != m_emoteMap.end())
    {
        return it->second;
    }

    return 0;  // Unknown emote
}

std::string StrategyParser::ExtractBlock(const std::string& response,
                                         const std::string& blockName)
{
    // Look for [BLOCKNAME]
    std::string blockStart = "[" + blockName + "]";
    size_t startPos = response.find(blockStart);

    if (startPos == std::string::npos)
    {
        // Also try lowercase
        std::string lowerBlockStart = "[" + blockName + "]";
        std::transform(lowerBlockStart.begin(), lowerBlockStart.end(),
                       lowerBlockStart.begin(), ::tolower);

        std::string lowerResponse = response;
        std::transform(lowerResponse.begin(), lowerResponse.end(),
                       lowerResponse.begin(), ::tolower);

        startPos = lowerResponse.find(lowerBlockStart);
        if (startPos == std::string::npos)
            return "";
    }

    // Move past the block header
    startPos += blockStart.length();

    // Find the next block or end of string
    size_t endPos = std::string::npos;

    // Look for any next block marker
    std::vector<std::string> markers = {"[STRATEGY]", "[CHAT]", "[EMOTE]", "[END]",
                                         "[strategy]", "[chat]", "[emote]", "[end]"};
    for (const auto& marker : markers)
    {
        size_t pos = response.find(marker, startPos);
        if (pos != std::string::npos && (endPos == std::string::npos || pos < endPos))
        {
            endPos = pos;
        }
    }

    if (endPos == std::string::npos)
    {
        endPos = response.length();
    }

    return response.substr(startPos, endPos - startPos);
}

std::map<std::string, std::string> StrategyParser::ParseKeyValuePairs(const std::string& content)
{
    std::map<std::string, std::string> pairs;

    std::istringstream stream(content);
    std::string line;

    while (std::getline(stream, line))
    {
        line = Trim(line);
        if (line.empty())
            continue;

        size_t colonPos = line.find(':');
        if (colonPos != std::string::npos)
        {
            std::string key = Trim(line.substr(0, colonPos));
            std::string value = Trim(line.substr(colonPos + 1));

            // Convert key to lowercase
            std::transform(key.begin(), key.end(), key.begin(), ::tolower);

            // Remove underscores for more flexible matching
            key.erase(std::remove(key.begin(), key.end(), ' '), key.end());

            pairs[key] = value;
        }
    }

    return pairs;
}

std::string StrategyParser::Trim(const std::string& str)
{
    if (str.empty())
        return str;

    size_t start = 0;
    size_t end = str.length() - 1;

    while (start <= end && std::isspace(static_cast<unsigned char>(str[start])))
        start++;

    while (end >= start && std::isspace(static_cast<unsigned char>(str[end])))
        end--;

    if (start > end)
        return "";

    return str.substr(start, end - start + 1);
}

std::string StrategyParser::TrimQuotes(const std::string& str)
{
    std::string result = Trim(str);

    if (result.length() >= 2)
    {
        // Remove surrounding double quotes
        if (result.front() == '"' && result.back() == '"')
        {
            result = result.substr(1, result.length() - 2);
        }
        // Remove surrounding single quotes
        else if (result.front() == '\'' && result.back() == '\'')
        {
            result = result.substr(1, result.length() - 2);
        }
    }

    return result;
}

} // namespace AliveWorld
