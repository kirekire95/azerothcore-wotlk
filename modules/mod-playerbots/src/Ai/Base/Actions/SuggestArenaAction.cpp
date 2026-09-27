/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 *
 * Copyright (C) 2025 EC <https://github.com/kirekire95> - Bot arena invite system with LLM integration
 */

#include "SuggestArenaAction.h"

#include "AiFactory.h"
#include "ArenaTeam.h"
#include "ArenaTeamMgr.h"
#include "BattlegroundMgr.h"
#include "Config.h"
#include "GroupMgr.h"
#include "PlayerbotAI.h"
#include "PlayerbotAIConfig.h"
#include "Playerbots.h"

// LLM integration: use mod-ollama-chat HTTP client when available
#ifdef MOD_OLLAMA_CHAT
#include "mod-ollama-chat_httpclient.h"
#include <thread>
#include <sstream>
#endif

std::vector<PendingArenaInvite> g_pendingArenaInvites;
std::mutex g_pendingArenaInvitesMutex;

std::map<ObjectGuid, RecentArenaPartner> g_recentArenaPartners;
std::mutex g_recentArenaPartnersMutex;

const std::vector<std::string> SuggestArenaAction::ARENA_INVITE_MESSAGES = {
    "Hey! Want to do some arena? I think we'd make a good team!",
    "I'm looking for an arena partner. Interested?",
    "Feel like doing some arena matches? Could be fun!",
    "Wanna queue up for arena? I've got some time.",
    "I could use an arena partner right about now. You in?",
    "How about some arena? I promise I won't let you die... much.",
    "Arena? I'll bring the heals if you bring the kills.",
    "Looking for someone to arena with. Want to give it a shot?",
    "Hey, I've been wanting to do some PvP. Arena sounds good?",
    "You look like you know how to fight. Arena with me?"
};

bool SuggestArenaAction::isUseful()
{
    if (!sPlayerbotAIConfig.botSuggestArena)
        return false;

    if (!sPlayerbotAIConfig.allowBotsInPlayerArena)
        return false;

    // Must be a random bot (not a player alt)
    if (!sRandomPlayerbotMgr.IsRandomBot(bot))
        return false;

    // Must be level 70+ for arena
    if (bot->GetLevel() < 70)
        return false;

    // Not in combat, BG, or queue
    if (bot->IsInCombat() || bot->InBattleground() || bot->InBattlegroundQueue())
        return false;

    // Not in dungeon/instance
    Map* map = bot->GetMap();
    if (map && map->Instanceable())
        return false;

    // Check for recent arena partner (allows re-queue while still grouped)
    bool hasRecentPartner = false;
    {
        std::lock_guard<std::mutex> lock(g_recentArenaPartnersMutex);
        auto it = g_recentArenaPartners.find(bot->GetGUID());
        if (it != g_recentArenaPartners.end() && it->second.matchEndTime > 0)
        {
            time_t elapsed = time(nullptr) - it->second.matchEndTime;
            if (elapsed < 600)
                hasRecentPartner = true;
            else
                g_recentArenaPartners.erase(it);
        }
    }

    // Not in a group already — unless we have a recent arena partner (post-match re-queue)
    if (bot->GetGroup() && !hasRecentPartner)
        return false;

    // Cooldown check — use shorter cooldown for re-queue
    std::string qualifier = "suggest arena";
    time_t lastSaid = AI_VALUE2(time_t, "last said", qualifier);
    int32 cooldown = hasRecentPartner ? 30 : (int32)sPlayerbotAIConfig.botSuggestArenaInterval;

    return (time(nullptr) - lastSaid) > cooldown;
}

bool SuggestArenaAction::Execute(Event event)
{
    // Check for recent arena partner — automatic re-invite after match
    {
        Player* recentPartner = nullptr;
        ArenaType recentType = ArenaType(0);
        bool recentIsRated = false;

        {
            std::lock_guard<std::mutex> lock(g_recentArenaPartnersMutex);
            auto it = g_recentArenaPartners.find(bot->GetGUID());
            if (it != g_recentArenaPartners.end() && it->second.matchEndTime > 0)
            {
                time_t elapsed = time(nullptr) - it->second.matchEndTime;
                if (elapsed < 600)
                {
                    Player* partner = ObjectAccessor::FindPlayer(it->second.playerGuid);
                    if (partner && partner->IsInWorld() && !partner->InBattleground() &&
                        !partner->InBattlegroundQueue() && !partner->IsInCombat() &&
                        !partner->isAFK())
                    {
                        recentPartner = partner;
                        recentType = it->second.arenaType;
                        recentIsRated = it->second.isRated;
                    }
                    else
                    {
                        g_recentArenaPartners.erase(it);
                    }
                }
                else
                {
                    g_recentArenaPartners.erase(it);
                }
            }
        }

        if (recentPartner)
        {
            std::string typeStr = recentType == ARENA_TYPE_2v2 ? "2v2" :
                                  recentType == ARENA_TYPE_3v3 ? "3v3" : "5v5";

            bot->Whisper("GG! Want to queue " + typeStr + " again?", LANG_UNIVERSAL, recentPartner);

            // Store pending invite so player can accept/decline
            {
                std::lock_guard<std::mutex> lock(g_pendingArenaInvitesMutex);

                time_t now = time(nullptr);
                g_pendingArenaInvites.erase(
                    std::remove_if(g_pendingArenaInvites.begin(), g_pendingArenaInvites.end(),
                        [now](PendingArenaInvite const& inv) { return now > inv.expireTime; }),
                    g_pendingArenaInvites.end());

                PendingArenaInvite invite;
                invite.botGuid = bot->GetGUID();
                invite.playerGuid = recentPartner->GetGUID();
                invite.arenaType = recentType;
                invite.expireTime = now + 120;
                invite.isRated = recentIsRated;
                g_pendingArenaInvites.push_back(invite);
            }

            std::string qualifier = "suggest arena";
            botAI->GetAiObjectContext()->GetValue<time_t>("last said", qualifier)->Set(time(nullptr));

            LOG_INFO("playerbots", "Bot {} <{}>: Re-inviting recent arena partner {} <{}> for {} {}",
                     bot->GetGUID().ToString().c_str(), bot->GetName(),
                     recentPartner->GetGUID().ToString().c_str(), recentPartner->GetName(),
                     recentIsRated ? "rated" : "skirmish", typeStr.c_str());

            return true;
        }
    }

    // Standard flow: find new partner
    Player* partner = FindArenaPartner();
    if (!partner)
        return false;

    ArenaType type = PickArenaType();
    if (type == ArenaType(0))
        return false;

    // Determine if rated or skirmish
    bool isRated = false;
    uint32 mode = sPlayerbotAIConfig.botArenaMode;
    if (mode == 1)
        isRated = true;
    else if (mode == 2)
        isRated = urand(0, 1) == 1;

    // Send whisper invitation
    WhisperArenaInvite(partner, type);

    // Store pending invite
    {
        std::lock_guard<std::mutex> lock(g_pendingArenaInvitesMutex);

        // Clean up expired invites
        time_t now = time(nullptr);
        g_pendingArenaInvites.erase(
            std::remove_if(g_pendingArenaInvites.begin(), g_pendingArenaInvites.end(),
                [now](PendingArenaInvite const& inv) { return now > inv.expireTime; }),
            g_pendingArenaInvites.end());

        // Add new invite
        PendingArenaInvite invite;
        invite.botGuid = bot->GetGUID();
        invite.playerGuid = partner->GetGUID();
        invite.arenaType = type;
        invite.expireTime = now + 120;  // 2 minute timeout
        invite.isRated = isRated;
        g_pendingArenaInvites.push_back(invite);
    }

    // Update cooldown
    std::string qualifier = "suggest arena";
    botAI->GetAiObjectContext()->GetValue<time_t>("last said", qualifier)->Set(time(nullptr));

    LOG_INFO("playerbots", "Bot {} <{}> invited player {} <{}> to {} {} arena",
             bot->GetGUID().ToString().c_str(), bot->GetName(),
             partner->GetGUID().ToString().c_str(), partner->GetName(),
             isRated ? "rated" : "skirmish",
             type == ARENA_TYPE_2v2 ? "2v2" : type == ARENA_TYPE_3v3 ? "3v3" : "5v5");

    return true;
}

Player* SuggestArenaAction::FindArenaPartner()
{
    // Get nearby real (non-bot) players
    GuidVector nearbyPlayers = AI_VALUE(GuidVector, "nearest non bot players");

    for (ObjectGuid const& guid : nearbyPlayers)
    {
        Player* player = ObjectAccessor::FindPlayer(guid);
        if (!player)
            continue;

        // Must be level 70+
        if (player->GetLevel() < 70)
            continue;

        // Not in combat, BG, or queue
        if (player->IsInCombat() || player->InBattleground() || player->InBattlegroundQueue())
            continue;

        // Not AFK/DND
        if (player->isAFK() || player->isDND())
            continue;

        // Same faction
        if (player->GetTeamId() != bot->GetTeamId())
            continue;

        // Within configured distance
        if (bot->GetDistance(player) > sPlayerbotAIConfig.botSuggestArenaDistance)
            continue;

        // Check if we already have a pending invite to this player
        {
            std::lock_guard<std::mutex> lock(g_pendingArenaInvitesMutex);
            bool alreadyInvited = false;
            for (auto const& inv : g_pendingArenaInvites)
            {
                if (inv.playerGuid == player->GetGUID() && time(nullptr) < inv.expireTime)
                {
                    alreadyInvited = true;
                    break;
                }
            }
            if (alreadyInvited)
                continue;
        }

        return player;
    }

    return nullptr;
}

ArenaType SuggestArenaAction::PickArenaType()
{
    // Try 2v2 first (most common, easiest), then 3v3, then 5v5
    // For 2v2, bot + player = 2 (no additional bots needed)
    // For 3v3, need 1 more bot
    // For 5v5, need 3 more bots

    std::vector<ArenaType> available;
    available.push_back(ARENA_TYPE_2v2);  // Always available (just bot + player)

    // Check if we can find enough bots for 3v3
    std::vector<Player*> extraBots = RecruitBotTeammates(1, ARENA_TYPE_3v3);
    if (extraBots.size() >= 1)
        available.push_back(ARENA_TYPE_3v3);

    // Check if we can find enough bots for 5v5
    extraBots = RecruitBotTeammates(3, ARENA_TYPE_5v5);
    if (extraBots.size() >= 3)
        available.push_back(ARENA_TYPE_5v5);

    if (available.empty())
        return ArenaType(0);

    // Random pick from available types
    return available[urand(0, available.size() - 1)];
}

#ifdef MOD_OLLAMA_CHAT
// Helper: extract "response" field from Ollama JSON response
static std::string ExtractOllamaResponse(std::string const& json)
{
    // Simple extraction of "response":"..." from JSON
    std::string key = "\"response\":\"";
    auto pos = json.find(key);
    if (pos == std::string::npos)
        return "";

    pos += key.length();
    std::string result;
    for (size_t i = pos; i < json.length(); ++i)
    {
        if (json[i] == '"' && (i == 0 || json[i - 1] != '\\'))
            break;
        if (json[i] == '\\' && i + 1 < json.length())
        {
            ++i;
            if (json[i] == 'n') result += '\n';
            else if (json[i] == '"') result += '"';
            else if (json[i] == '\\') result += '\\';
            else result += json[i];
        }
        else
        {
            result += json[i];
        }
    }
    return result;
}

// Async LLM whisper: spawns thread to call Ollama, then whispers the result
static void SendLLMArenaWhisper(uint64 botGuid, uint64 targetGuid, std::string typeStr)
{
    std::string host = sConfigMgr->GetOption<std::string>("OllamaChat.Host", "127.0.0.1");
    uint32 port = sConfigMgr->GetOption<uint32>("OllamaChat.Port", 11434);
    std::string model = sConfigMgr->GetOption<std::string>("OllamaChat.Model", "llama3.2");

    std::thread([botGuid, targetGuid, typeStr, host, port, model]()
    {
        try
        {
            Player* botPtr = ObjectAccessor::FindPlayer(ObjectGuid(botGuid));
            Player* targetPtr = ObjectAccessor::FindPlayer(ObjectGuid(targetGuid));
            if (!botPtr || !targetPtr)
                return;

            PlayerbotAI* ai = GET_PLAYERBOT_AI(botPtr);
            std::string botClass = ai && ai->GetChatHelper() ?
                ai->GetChatHelper()->FormatClass(botPtr->getClass()) : "warrior";
            std::string targetClass = ai && ai->GetChatHelper() ?
                ai->GetChatHelper()->FormatClass(targetPtr->getClass()) : "warrior";

            // Build the prompt
            std::ostringstream promptStream;
            promptStream << "You are " << botPtr->GetName() << ", a level " << botPtr->GetLevel()
                         << " " << botClass << " in World of Warcraft."
                         << " A player named " << targetPtr->GetName() << " (level "
                         << targetPtr->GetLevel() << " " << targetClass
                         << ") is nearby. You want to do " << typeStr << " arena PvP with them."
                         << " Whisper them a casual, in-character message asking if they want to"
                         << " queue " << typeStr << " arena with you."
                         << " Keep it under 40 words. Be natural, not formal."
                         << " Do not use quotation marks. Do not include your name.";

            // Build JSON request
            std::ostringstream json;
            // Escape the prompt for JSON
            std::string prompt = promptStream.str();
            std::string escaped;
            for (char c : prompt)
            {
                if (c == '"') escaped += "\\\"";
                else if (c == '\\') escaped += "\\\\";
                else if (c == '\n') escaped += "\\n";
                else escaped += c;
            }

            json << "{\"model\":\"" << model << "\","
                 << "\"prompt\":\"" << escaped << "\","
                 << "\"stream\":false,"
                 << "\"options\":{\"temperature\":0.8,\"num_predict\":60}}";

            std::string url = "http://" + host + ":" + std::to_string(port) + "/api/generate";

            OllamaHttpClient client;
            client.SetTimeout(15);
            std::string response = client.Post(url, json.str());
            std::string llmMessage = ExtractOllamaResponse(response);

            // Reacquire pointers after async call
            botPtr = ObjectAccessor::FindPlayer(ObjectGuid(botGuid));
            targetPtr = ObjectAccessor::FindPlayer(ObjectGuid(targetGuid));
            if (!botPtr || !targetPtr || llmMessage.empty())
                return;

            // Clean up: remove quotes, truncate if too long
            if (llmMessage.length() > 255)
                llmMessage = llmMessage.substr(0, 252) + "...";

            botPtr->Whisper(llmMessage, LANG_UNIVERSAL, targetPtr);

            LOG_INFO("playerbots", "Bot {} <{}>: LLM arena invite to {} <{}>: {}",
                     botPtr->GetGUID().ToString().c_str(), botPtr->GetName(),
                     targetPtr->GetGUID().ToString().c_str(), targetPtr->GetName(),
                     llmMessage.c_str());
        }
        catch (const std::exception& e)
        {
            LOG_ERROR("playerbots", "SuggestArenaAction LLM thread exception: {}", e.what());
        }
    }).detach();
}
#endif // MOD_OLLAMA_CHAT

void SuggestArenaAction::WhisperArenaInvite(Player* target, ArenaType type)
{
    std::string typeStr = type == ARENA_TYPE_2v2 ? "2v2" : type == ARENA_TYPE_3v3 ? "3v3" : "5v5";

#ifdef MOD_OLLAMA_CHAT
    // Use LLM for natural invitation when enabled
    if (sPlayerbotAIConfig.botSuggestArenaUseLLM)
    {
        // Send async LLM whisper (non-blocking)
        SendLLMArenaWhisper(bot->GetGUID().GetRawValue(), target->GetGUID().GetRawValue(), typeStr);

        // Also send a quick hardcoded message immediately so the player knows what's happening
        // The LLM message will follow shortly after
        return;
    }
#endif

    // Hardcoded fallback messages
    std::string message = ARENA_INVITE_MESSAGES[urand(0, ARENA_INVITE_MESSAGES.size() - 1)];
    message += " (" + typeStr + ")";

    bot->Whisper(message, LANG_UNIVERSAL, target);
}

bool SuggestArenaAction::FormGroupAndQueue(Player* partner, ArenaType type)
{
    Group* group = bot->GetGroup();

    // Reuse existing group if bot and partner are already grouped together
    bool alreadyGrouped = group && group->IsMember(partner->GetGUID()) &&
                          group->GetMembersCount() >= (uint32)type;

    if (!alreadyGrouped)
    {
        // Need a new group — disband any existing one.
        // Defer to the world thread via packet queue because Group::Disband
        // modifies all members' state, and members may be on different maps.
        // Return false so the caller retries after the disband is processed.
        if (group)
        {
            bot->GetSession()->QueuePacket(new WorldPacket(CMSG_GROUP_DISBAND));
            return false;
        }

        group = new Group();
        if (!group->Create(bot))
        {
            delete group;
            return false;
        }
        sGroupMgr->AddGroup(group);

        // Add the real player — only safe if on the same map (same thread).
        // Group::AddMember modifies the partner's Player state; if they're on
        // a different map, this races with that map's worker thread.
        if (partner->GetMapId() != bot->GetMapId())
        {
            group->Disband();
            return false;
        }

        if (!group->AddMember(partner))
        {
            group->Disband();
            return false;
        }

        // For 3v3/5v5, add bot teammates
        uint32 needed = (uint32)type - 2;  // 3v3 needs 1 more, 5v5 needs 3 more
        if (needed > 0)
        {
            std::vector<Player*> teammates = RecruitBotTeammates(needed, type);
            for (Player* teammate : teammates)
            {
                if (group->GetMembersCount() >= (uint32)type)
                    break;

                // Skip cross-map teammates — can't safely modify their state
                // from this map's worker thread (data race on Group/Unit internals).
                if (teammate->GetMapId() != bot->GetMapId())
                    continue;

                if (teammate->GetGroup())
                    teammate->GetGroup()->RemoveMember(teammate->GetGUID());

                PlayerbotAI* teammateAI = GET_PLAYERBOT_AI(teammate);
                if (teammateAI)
                    teammateAI->Reset();

                group->AddMember(teammate);

                // Same-map teleport is safe (same worker thread).
                teammate->RemoveAurasWithInterruptFlags(AURA_INTERRUPT_FLAG_TELEPORTED | AURA_INTERRUPT_FLAG_CHANGE_MAP);
                teammate->TeleportTo(bot->GetMapId(), bot->GetPositionX(), bot->GetPositionY(), bot->GetPositionZ(), 0);
            }
        }

        // Verify group size matches arena type
        if (group->GetMembersCount() < (uint32)type)
        {
            LOG_INFO("playerbots", "Bot {} <{}>: Not enough members for {} arena (have {}, need {})",
                     bot->GetGUID().ToString().c_str(), bot->GetName(),
                     type == ARENA_TYPE_2v2 ? "2v2" : type == ARENA_TYPE_3v3 ? "3v3" : "5v5",
                     group->GetMembersCount(), (uint32)type);
            group->Disband();
            return false;
        }
    }

    // Keep arena behavior anchored to the invited real player.
    if (!GET_PLAYERBOT_AI(partner))
        botAI->SetMaster(partner);

    // Queue for arena — preserve rated/skirmish from previous match if re-queueing
    bool isRated = false;
    {
        std::lock_guard<std::mutex> lock(g_recentArenaPartnersMutex);
        auto it = g_recentArenaPartners.find(bot->GetGUID());
        if (it != g_recentArenaPartners.end() && it->second.matchEndTime > 0 &&
            it->second.playerGuid == partner->GetGUID())
        {
            isRated = it->second.isRated;  // Keep same mode as last match
        }
        else
        {
            // First match — roll from config
            uint32 mode = sPlayerbotAIConfig.botArenaMode;
            if (mode == 1)
                isRated = true;
            else if (mode == 2)
                isRated = urand(0, 1) == 1;
        }
    }

    // For rated, we need an arena team
    if (isRated)
    {
        if (!CreateArenaTeamForPlayer(partner, type))
        {
            isRated = false;  // Fall back to skirmish
            LOG_INFO("playerbots", "Bot {} <{}>: Failed to create rated arena team, falling back to skirmish",
                     bot->GetGUID().ToString().c_str(), bot->GetName());
        }
    }

    // Send arena queue packet
    uint8 arenaSlot = 0;
    switch (type)
    {
        case ARENA_TYPE_2v2: arenaSlot = 0; break;
        case ARENA_TYPE_3v3: arenaSlot = 1; break;
        case ARENA_TYPE_5v5: arenaSlot = 2; break;
        default: break;
    }

    WorldPacket data(CMSG_BATTLEMASTER_JOIN_ARENA, 20);
    data << bot->GetGUID();
    data << arenaSlot;
    data << uint8(1);       // as group
    data << uint8(isRated);
    bot->GetSession()->HandleBattlemasterJoinArena(data);

    LOG_INFO("playerbots", "Bot {} <{}> queued for {} {} arena with player {} <{}>",
             bot->GetGUID().ToString().c_str(), bot->GetName(),
             isRated ? "rated" : "skirmish",
             type == ARENA_TYPE_2v2 ? "2v2" : type == ARENA_TYPE_3v3 ? "3v3" : "5v5",
             partner->GetGUID().ToString().c_str(), partner->GetName());

    // Store as recent arena partner for post-match re-queue
    {
        std::lock_guard<std::mutex> lock(g_recentArenaPartnersMutex);
        RecentArenaPartner recent;
        recent.playerGuid = partner->GetGUID();
        recent.arenaType = type;
        recent.isRated = isRated;
        recent.matchEndTime = 0;  // Still in queue/match
        g_recentArenaPartners[bot->GetGUID()] = recent;
    }

    return true;
}

bool SuggestArenaAction::CreateArenaTeamForPlayer(Player* partner, ArenaType type)
{
    // Check if bot already has an arena team for this type
    ArenaTeam* existingTeam = sArenaTeamMgr->GetArenaTeamByCaptain(bot->GetGUID(), type);
    if (existingTeam)
    {
        // Check if player is already a member
        if (existingTeam->IsMember(partner->GetGUID()))
            return true;

        // Try to add player
        if (existingTeam->GetMembersSize() < (uint32)type + 2)  // Arena teams can have extra members
        {
            existingTeam->AddMember(partner->GetGUID());
            return true;
        }
        return true;  // Team exists, player will queue with it
    }

    // Create new arena team
    std::string teamName = bot->GetName();
    teamName += "'s Arena Team";

    ArenaTeam* team = new ArenaTeam();
    if (!team->Create(bot->GetGUID(), type, teamName, 0, 0, 0, 0, 0))
    {
        delete team;
        return false;
    }

    team->SetCaptain(bot->GetGUID());
    team->AddMember(partner->GetGUID());

    // Set initial rating
    uint32 rating = urand(sPlayerbotAIConfig.randomBotArenaTeamMinRating,
                          sPlayerbotAIConfig.randomBotArenaTeamMaxRating);
    team->SetRatingForAll(rating);

    team->SaveToDB();
    sArenaTeamMgr->AddArenaTeam(team);

    LOG_INFO("playerbots", "Bot {} <{}>: Created arena team '{}' (type {}) with player {} <{}>",
             bot->GetGUID().ToString().c_str(), bot->GetName(),
             teamName.c_str(), (uint32)type,
             partner->GetGUID().ToString().c_str(), partner->GetName());

    return true;
}

std::vector<Player*> SuggestArenaAction::RecruitBotTeammates(uint32 needed, ArenaType type)
{
    std::vector<Player*> result;

    // Find nearby bots that can join
    GuidVector nearbyPlayers = AI_VALUE(GuidVector, "nearest friendly players");

    for (ObjectGuid const& guid : nearbyPlayers)
    {
        if (result.size() >= needed)
            break;

        Player* player = ObjectAccessor::FindPlayer(guid);
        if (!player || player->GetGUID() == bot->GetGUID())
            continue;

        // Must be a bot
        PlayerbotAI* playerAI = GET_PLAYERBOT_AI(player);
        if (!playerAI)
            continue;

        // Must be level 70+
        if (player->GetLevel() < 70)
            continue;

        // Not busy
        if (player->IsInCombat() || player->InBattleground() || player->InBattlegroundQueue())
            continue;

        // Same faction
        if (player->GetTeamId() != bot->GetTeamId())
            continue;

        // Not already in a group
        if (player->GetGroup())
            continue;

        result.push_back(player);
    }

    return result;
}

// ============= Arena Whisper Response Handler =============
// Free function called from Playerbots.cpp OnPlayerCanUseChat
// to intercept player responses to bot arena invitations.

static bool IsPositiveResponse(std::string const& msg)
{
    std::string lower = msg;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);

    static const std::vector<std::string> positiveWords = {
        "yes", "yeah", "yep", "yea", "sure", "ok", "okay", "let's go",
        "lets go", "let's do it", "lets do it", "down", "i'm in", "im in",
        "alright", "all right", "sounds good", "arena", "ready", "go",
        "absolutely", "definitely", "bet", "aight", "word"
    };

    for (auto const& word : positiveWords)
    {
        if (lower.find(word) != std::string::npos)
            return true;
    }

    return false;
}

static bool IsNegativeResponse(std::string const& msg)
{
    std::string lower = msg;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);

    static const std::vector<std::string> negativeWords = {
        "no", "nah", "nope", "busy", "later", "not now", "can't",
        "cant", "pass", "maybe later", "another time"
    };

    for (auto const& word : negativeWords)
    {
        if (lower.find(word) != std::string::npos)
            return true;
    }

    return false;
}

bool HandleArenaWhisperResponse(Player* bot, Player* sender, std::string const& message)
{
    if (!bot || !sender)
        return false;

    // Check if there's a pending invite from this bot to this player
    PendingArenaInvite foundInvite;
    bool found = false;

    {
        std::lock_guard<std::mutex> lock(g_pendingArenaInvitesMutex);
        for (auto it = g_pendingArenaInvites.begin(); it != g_pendingArenaInvites.end(); ++it)
        {
            if (it->botGuid == bot->GetGUID() && it->playerGuid == sender->GetGUID() &&
                time(nullptr) < it->expireTime)
            {
                foundInvite = *it;
                found = true;
                g_pendingArenaInvites.erase(it);
                break;
            }
        }
    }

    if (!found)
    {
        // No pending invite — check for "again" / "requeue" command with recent arena partner
        std::string lower = message;
        std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);

        bool isRequeueCommand = (lower.find("again") != std::string::npos ||
                                 lower.find("requeue") != std::string::npos ||
                                 lower.find("rematch") != std::string::npos ||
                                 lower.find("re-queue") != std::string::npos);

        if (isRequeueCommand)
        {
            RecentArenaPartner recent;
            bool hasRecent = false;

            {
                std::lock_guard<std::mutex> lock(g_recentArenaPartnersMutex);
                auto it = g_recentArenaPartners.find(bot->GetGUID());
                if (it != g_recentArenaPartners.end() && it->second.playerGuid == sender->GetGUID())
                {
                    recent = it->second;
                    hasRecent = true;
                }
            }

            if (hasRecent)
            {
                PlayerbotAI* requeueAI = GET_PLAYERBOT_AI(bot);
                if (!requeueAI)
                    return false;

                bot->Whisper("Let's do it! Queueing up now.", LANG_UNIVERSAL, sender);

                SuggestArenaAction suggestAction(requeueAI);
                if (!suggestAction.FormGroupAndQueue(sender, recent.arenaType))
                {
                    bot->Whisper("Something went wrong, maybe try again in a moment.", LANG_UNIVERSAL, sender);
                }

                LOG_INFO("playerbots", "Bot {} <{}>: Player {} <{}> requested arena requeue via whisper",
                         bot->GetGUID().ToString().c_str(), bot->GetName(),
                         sender->GetGUID().ToString().c_str(), sender->GetName());

                return true;
            }
        }

        return false;
    }

    PlayerbotAI* botAI = GET_PLAYERBOT_AI(bot);
    if (!botAI)
        return false;

    if (IsPositiveResponse(message))
    {
        bot->Whisper("Let's go! Forming the group now.", LANG_UNIVERSAL, sender);

        // Form group and queue
        SuggestArenaAction suggestAction(botAI);
        if (!suggestAction.FormGroupAndQueue(sender, foundInvite.arenaType))
        {
            bot->Whisper("Hmm, something went wrong. Maybe later!", LANG_UNIVERSAL, sender);
            return true;  // Still handled, just failed
        }

        LOG_INFO("playerbots", "Bot {} <{}>: Player {} <{}> accepted arena invite, queuing for {}",
                 bot->GetGUID().ToString().c_str(), bot->GetName(),
                 sender->GetGUID().ToString().c_str(), sender->GetName(),
                 foundInvite.arenaType == ARENA_TYPE_2v2 ? "2v2" :
                 foundInvite.arenaType == ARENA_TYPE_3v3 ? "3v3" : "5v5");

        return true;
    }
    else if (IsNegativeResponse(message))
    {
        bot->Whisper("No worries, maybe next time!", LANG_UNIVERSAL, sender);

        LOG_INFO("playerbots", "Bot {} <{}>: Player {} <{}> declined arena invite",
                 bot->GetGUID().ToString().c_str(), bot->GetName(),
                 sender->GetGUID().ToString().c_str(), sender->GetName());

        return true;
    }

    // Unrecognized response — put the invite back so they can try again
    {
        std::lock_guard<std::mutex> lock(g_pendingArenaInvitesMutex);
        g_pendingArenaInvites.push_back(foundInvite);
    }

    return false;
}
