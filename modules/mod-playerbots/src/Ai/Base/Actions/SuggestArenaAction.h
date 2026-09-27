/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 *
 * Copyright (C) 2025 EC <https://github.com/kirekire95> - Bot arena invite system with LLM integration
 */

#ifndef _PLAYERBOT_SUGGESTARENAACTION_H
#define _PLAYERBOT_SUGGESTARENAACTION_H

#include "Action.h"
#include "Battleground.h"  // For ArenaType enum

#include <map>

class PlayerbotAI;

// Bots proactively invite nearby real players to queue arena together.
// Uses LLM (mod-ollama-chat) for natural invitation messages when available.
class SuggestArenaAction : public Action
{
public:
    SuggestArenaAction(PlayerbotAI* botAI) : Action(botAI, "suggest arena") {}

    bool Execute(Event event) override;
    bool isUseful() override;

    // Form group with player and bot teammates, then queue
    // Public because HandleArenaInviteResponse needs to call it
    bool FormGroupAndQueue(Player* partner, ArenaType type);

private:
    // Find an eligible nearby real player for arena
    Player* FindArenaPartner();

    // Pick arena type based on available bot teammates
    ArenaType PickArenaType();

    // Send whisper invitation (hardcoded fallback messages)
    void WhisperArenaInvite(Player* target, ArenaType type);

    // Create arena team for rated mode
    bool CreateArenaTeamForPlayer(Player* partner, ArenaType type);

    // Recruit additional bot teammates for 3v3/5v5
    std::vector<Player*> RecruitBotTeammates(uint32 needed, ArenaType type);

    // Hardcoded invitation messages
    static const std::vector<std::string> ARENA_INVITE_MESSAGES;
};

// Tracks pending arena invitations from bots to players
struct PendingArenaInvite
{
    ObjectGuid botGuid;
    ObjectGuid playerGuid;
    ArenaType arenaType;
    time_t expireTime;
    bool isRated;
};

// Global pending invites (checked by ArenaResponseAction)
extern std::vector<PendingArenaInvite> g_pendingArenaInvites;
extern std::mutex g_pendingArenaInvitesMutex;

// Tracks recent arena partners for automatic re-queueing after matches
struct RecentArenaPartner
{
    ObjectGuid playerGuid;
    ArenaType arenaType;
    bool isRated;
    time_t matchEndTime;  // Set when match ends (LeaveBG); 0 while in queue/match
};

// Maps botGuid → recent arena partner (for post-match re-invite)
extern std::map<ObjectGuid, RecentArenaPartner> g_recentArenaPartners;
extern std::mutex g_recentArenaPartnersMutex;

// Static helper: Check if a whisper to a bot is an arena invite response.
// Called from Playerbots.cpp OnPlayerCanUseChat before normal command parsing.
// Returns true if the whisper was handled as an arena response.
bool HandleArenaWhisperResponse(Player* bot, Player* sender, std::string const& message);

#endif
