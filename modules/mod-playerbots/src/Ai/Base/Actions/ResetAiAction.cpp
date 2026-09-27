/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "ResetAiAction.h"
#include "Event.h"
#include "Group.h"
#include "ObjectGuid.h"
#include "PlayerbotRepository.h"
#include "Playerbots.h"
#include "SuggestArenaAction.h"
#include "WorldPacket.h"

#include <algorithm>
#include <ctime>
#include <mutex>

bool ResetAiAction::Execute(Event event)
{
    if (!event.getPacket().empty())
    {
        if (event.getPacket().GetOpcode() == SMSG_GROUP_LIST)
        {
            // Group updates are noisy and may arrive while arena flows are still being established.
            // Only reset when the bot is truly solo and not in an arena invite/requeue flow.
            if (bot->GetGroup() || bot->InBattleground() || bot->InBattlegroundQueue())
            {
                return false;
            }

            {
                std::lock_guard<std::mutex> lock(g_pendingArenaInvitesMutex);
                time_t now = time(nullptr);
                auto it = std::find_if(
                    g_pendingArenaInvites.begin(), g_pendingArenaInvites.end(),
                    [this, now](PendingArenaInvite const& invite)
                    {
                        return invite.botGuid == bot->GetGUID() && now < invite.expireTime;
                    });

                if (it != g_pendingArenaInvites.end())
                {
                    return false;
                }
            }

            {
                std::lock_guard<std::mutex> lock(g_recentArenaPartnersMutex);
                auto it = g_recentArenaPartners.find(bot->GetGUID());
                if (it != g_recentArenaPartners.end())
                {
                    // matchEndTime == 0 means we're in queue/match for this partner.
                    if (it->second.matchEndTime == 0)
                    {
                        return false;
                    }

                    // Keep requeue context alive for 10 minutes after match end.
                    if (time(nullptr) - it->second.matchEndTime < 600)
                    {
                        return false;
                    }
                }
            }

            if (botAI->HasGameClientMaster())
            {
                return false;
            }
        }
    }
    if (Player* master = botAI->GetMaster())
    {
        Group* botGroup = bot->GetGroup();
        Group* masterGroup = master->GetGroup();
        if (botGroup && (!masterGroup || masterGroup != botGroup))
            botAI->SetMaster(nullptr);
    }
    if (sRandomPlayerbotMgr.IsRandomBot(bot) && !bot->InBattleground())
    {
        if (bot->GetGroup() && (!botAI->GetMaster() || GET_PLAYERBOT_AI(botAI->GetMaster())))
        {
            if (Player* newMaster = botAI->FindNewMaster())
                botAI->SetMaster(newMaster);
        }
    }
    PlayerbotRepository::instance().Reset(botAI);
    botAI->ResetStrategies(false);
    botAI->TellMaster("AI was reset to defaults");
    return true;
}
