#include "mod-ollama-chat_events.h"
#include "mod-ollama-chat_config.h"
#include "mod-ollama-chat_random.h"
#include "mod-ollama-chat_api.h"
#include "mod-ollama-chat-utilities.h"
#include "mod-ollama-chat_personality.h"
#include "mod-ollama-chat_sentiment.h"
#include "Player.h"
#include "ObjectAccessor.h"
#include "Guild.h"
#include "PlayerbotAI.h"
#include "PlayerbotMgr.h"
#include "AiFactory.h"
#include "SpellMgr.h"
#include "AchievementMgr.h"
#include "GameObject.h"
#include "Battleground.h"
#include "AiObjectContext.h"
#include <vector>
#include <thread>
#include <random>
#include <fmt/core.h>
#include <unordered_map>
#include <chrono>
#include <sstream>

static OllamaBotEventChatter eventChatter;
static std::unordered_map<Player*, std::chrono::steady_clock::time_point> botEventCooldowns;

// --- PvP Context ---

std::string PvPContext::BuildContextString() const
{
    if (!isInPvP)
        return "";

    std::ostringstream oss;

    if (isArena)
    {
        oss << "Arena: " << matchType;
        if (isRated)
            oss << " (Rated, " << arenaRating << " MMR)";
        oss << ". Alive: " << allyAlive << "v" << hordeAlive << ".";
        if (!friendlyComp.empty())
            oss << " Your team: " << friendlyComp << ".";
        if (!enemyComp.empty())
            oss << " Enemy team: " << enemyComp << ".";
        if (!deadPlayers.empty())
            oss << " Dead: " << deadPlayers << ".";
        oss << " Duration: " << matchDurationSec << "s.";
    }
    else
    {
        oss << "Battleground: " << matchType << ".";
        oss << " Score: Alliance " << allyScore << " - Horde " << hordeScore << ".";
        oss << " Duration: " << matchDurationSec << "s.";
    }

    if (!botCombatState.empty())
        oss << " You are " << botCombatState << ".";

    return oss.str();
}

static std::string GetClassName(uint8 classId)
{
    switch (classId)
    {
        case CLASS_WARRIOR:     return "Warrior";
        case CLASS_PALADIN:     return "Paladin";
        case CLASS_HUNTER:      return "Hunter";
        case CLASS_ROGUE:       return "Rogue";
        case CLASS_PRIEST:      return "Priest";
        case CLASS_DEATH_KNIGHT: return "Death Knight";
        case CLASS_SHAMAN:      return "Shaman";
        case CLASS_MAGE:        return "Mage";
        case CLASS_WARLOCK:     return "Warlock";
        case CLASS_DRUID:       return "Druid";
        default:                return "Unknown";
    }
}

// Extract PvP context from a Battleground — MUST be called on main thread before async dispatch.
// Returns a value type that is safe to copy into lambda captures.
static PvPContext ExtractPvPContext(Battleground* bg, Player* bot)
{
    PvPContext ctx;
    if (!bg || !bot)
    {
        if (g_DebugEnabled)
            LOG_INFO("server.loading", "[OllamaChat] ExtractPvPContext: null bg or bot, returning empty context");
        return ctx;
    }

    if (g_DebugEnabled)
        LOG_INFO("server.loading", "[OllamaChat] ExtractPvPContext: extracting for bot {} in BG type {}", bot->GetName(), bg->GetBgTypeID());

    ctx.isInPvP = true;
    ctx.isArena = bg->isArena();
    ctx.isRated = bg->isRated();
    ctx.arenaType = bg->GetArenaType();
    ctx.botTeam = bot->GetTeamId();

    // Duration in seconds
    uint32 startTime = bg->GetStartTime();
    ctx.matchDurationSec = startTime / 1000;

    // Match type string
    if (ctx.isArena)
    {
        ctx.matchType = fmt::format("{}v{}", ctx.arenaType, ctx.arenaType);
    }
    else
    {
        switch (bg->GetBgTypeID())
        {
            case BATTLEGROUND_WS: ctx.matchType = "Warsong Gulch"; break;
            case BATTLEGROUND_AB: ctx.matchType = "Arathi Basin"; break;
            case BATTLEGROUND_AV: ctx.matchType = "Alterac Valley"; break;
            case BATTLEGROUND_EY: ctx.matchType = "Eye of the Storm"; break;
            case BATTLEGROUND_SA: ctx.matchType = "Strand of the Ancients"; break;
            case BATTLEGROUND_IC: ctx.matchType = "Isle of Conquest"; break;
            default:              ctx.matchType = "Battleground"; break;
        }
    }

    // Scores / alive counts
    if (ctx.isArena)
    {
        ctx.allyAlive = bg->GetAlivePlayersCountByTeam(TEAM_ALLIANCE);
        ctx.hordeAlive = bg->GetAlivePlayersCountByTeam(TEAM_HORDE);
    }
    else
    {
        ctx.allyScore = bg->GetTeamScore(TEAM_ALLIANCE);
        ctx.hordeScore = bg->GetTeamScore(TEAM_HORDE);
    }

    // Arena MMR
    if (ctx.isArena)
    {
        ctx.arenaRating = bg->GetArenaMatchmakerRating(ctx.botTeam);
    }

    if (g_DebugEnabled)
    {
        if (ctx.isArena)
            LOG_INFO("server.loading", "[OllamaChat] ExtractPvPContext: {} | {} | rated={} | alive={}v{} | MMR={}",
                bot->GetName(), ctx.matchType, ctx.isRated, ctx.allyAlive, ctx.hordeAlive, ctx.arenaRating);
        else
            LOG_INFO("server.loading", "[OllamaChat] ExtractPvPContext: {} | {} | score=A{}-H{} | duration={}s",
                bot->GetName(), ctx.matchType, ctx.allyScore, ctx.hordeScore, ctx.matchDurationSec);
    }

    // Bot combat state: role, health/mana, current target
    {
        PlayerbotAI* botAI = sPlayerbotsMgr.GetPlayerbotAI(bot);
        if (botAI)
        {
            // Role
            std::string role;
            if (PlayerbotAI::IsTank(bot))
                role = "Tank";
            else if (PlayerbotAI::IsHeal(bot))
                role = "Healer";
            else
                role = "DPS";

            // Health/Mana
            float healthPct = bot->GetHealthPct();
            uint32 maxMana = bot->GetMaxPower(POWER_MANA);
            std::string resourceStr;
            if (maxMana > 0)
            {
                float manaPct = (float)bot->GetPower(POWER_MANA) / (float)maxMana * 100.0f;
                resourceStr = fmt::format("{} at {:.0f}% HP, {:.0f}% mana", role, healthPct, manaPct);
            }
            else
            {
                resourceStr = fmt::format("{} at {:.0f}% HP", role, healthPct);
            }

            // Current target
            AiObjectContext* aiCtx = botAI->GetAiObjectContext();
            if (aiCtx)
            {
                auto* targetValue = aiCtx->GetValue<Unit*>("current target");
                Unit* target = targetValue ? targetValue->Get() : nullptr;
                if (target && target->IsAlive())
                {
                    Player* targetPlayer = target->ToPlayer();
                    if (targetPlayer)
                        resourceStr += fmt::format(". Targeting {} ({})", targetPlayer->GetName(), GetClassName(targetPlayer->getClass()));
                    else
                        resourceStr += fmt::format(". Targeting {}", target->GetName());
                }
            }

            ctx.botCombatState = resourceStr;

            if (g_DebugEnabled)
                LOG_INFO("server.loading", "[OllamaChat] ExtractPvPContext: {} combat state: {}", bot->GetName(), resourceStr);
        }
        else if (g_DebugEnabled)
        {
            LOG_INFO("server.loading", "[OllamaChat] ExtractPvPContext: {} is not a bot, skipping combat state", bot->GetName());
        }
    }

    // Player rosters with names, classes, and death tracking (arena)
    if (ctx.isArena)
    {
        TeamId enemyTeam = (ctx.botTeam == TEAM_ALLIANCE) ? TEAM_HORDE : TEAM_ALLIANCE;
        std::vector<std::string> enemies;
        std::vector<std::string> allies;
        std::vector<std::string> dead;

        for (auto const& [guid, player] : bg->GetPlayers())
        {
            if (!player)
                continue;

            std::string entry = fmt::format("{} ({})", player->GetName(), GetClassName(player->getClass()));

            if (player->GetTeamId() == enemyTeam)
            {
                enemies.push_back(entry);
                if (!player->IsAlive())
                    dead.push_back(entry);
            }
            else
            {
                // Friendly team (skip self)
                if (player != bot)
                    allies.push_back(entry);
                if (!player->IsAlive())
                    dead.push_back(entry);
            }
        }

        // Build comma-separated strings
        for (size_t i = 0; i < enemies.size(); ++i)
        {
            if (i > 0) ctx.enemyComp += ", ";
            ctx.enemyComp += enemies[i];
        }
        for (size_t i = 0; i < allies.size(); ++i)
        {
            if (i > 0) ctx.friendlyComp += ", ";
            ctx.friendlyComp += allies[i];
        }
        for (size_t i = 0; i < dead.size(); ++i)
        {
            if (i > 0) ctx.deadPlayers += ", ";
            ctx.deadPlayers += dead[i];
        }

        if (g_DebugEnabled)
            LOG_INFO("server.loading", "[OllamaChat] ExtractPvPContext: {} roster | friendly=[{}] | enemy=[{}] | dead=[{}]",
                bot->GetName(), ctx.friendlyComp, ctx.enemyComp, ctx.deadPlayers);
    }

    if (g_DebugEnabled)
        LOG_INFO("server.loading", "[OllamaChat] ExtractPvPContext: final context string: \"{}\"", ctx.BuildContextString());

    return ctx;
}

// Helper function to check if a bot is allowed to respond in restricted mode
static bool IsBotAllowedForRestrictedMode(Player* bot, Player* source)
{
    if (!g_RestrictBotsToPartyMembers)
        return true;
        
    if (!bot || !source)
        return false;
        
    // Get groups for both bot and source
    Group* botGroup = bot->GetGroup();
    Group* sourceGroup = source->GetGroup();
    
    // Both must be in a group
    if (!botGroup || !sourceGroup)
        return false;
        
    // Must be the same group
    if (botGroup != sourceGroup)
        return false;
        
    // Group must not be a raid (battleground raids are allowed)
    if (botGroup->isRaidGroup() && !botGroup->isBGGroup())
        return false;
        
    // At least one real player must be in the group
    bool hasRealPlayer = false;
    for (GroupReference* ref = botGroup->GetFirstMember(); ref; ref = ref->next())
    {
        Player* member = ref->GetSource();
        if (member && !sPlayerbotsMgr.GetPlayerbotAI(member))
        {
            hasRealPlayer = true;
            break;
        }
    }
    
    return hasRealPlayer;
}

void OllamaBotEventChatter::DispatchGameEvent(Player* source, std::string type, std::string detail, PvPContext ctx)
{
    if (!g_Enable || !g_EnableEventChatter)
        return;
    
    if (!source)
    {
       return;
    }

    bool isSourceBot = sPlayerbotsMgr.GetPlayerbotAI(source) != nullptr;
    bool hasNearbyRealPlayer = false;
    bool isGuildEvent = false;

    // Only set isGuildEvent for specific event types
    if (source->GetGuild() && g_EnableGuildEventChatter) {
        // List of event types that are considered guild events
        if (
            type == g_GuildEventTypeGuildJoin ||
            type == g_GuildEventTypeGuildLeave ||
            type == g_GuildEventTypeGuildPromotion ||
            type == g_GuildEventTypeGuildDemotion ||
            type == g_GuildEventTypeLevelUp ||
            type == g_GuildEventTypeEpicGear ||
            type == g_GuildEventTypeRareGear ||
            type == g_GuildEventTypeDungeonComplete ||
            type == g_GuildEventTypeGuildAchievement ||
            type == g_GuildEventTypeGuildLogin
        ) {
            // Check if there are real players in the guild
            Guild* guild = source->GetGuild();
            for (auto const& pair : ObjectAccessor::GetPlayers()) {
                Player* player = pair.second;
                if (!player || !player->IsInWorld())
                    continue;
                if (sPlayerbotsMgr.GetPlayerbotAI(player))
                    continue;
                if (player->GetGuild() && player->GetGuild()->GetId() == guild->GetId()) {
                    isGuildEvent = true;
                    break;
                }
            }
        }
    }

    // Check for nearby real players (original logic)
    for (auto const& pair : source->GetMap()->GetPlayers())
    {
        Player* player = pair.GetSource();
        if (player == source)
            continue;

        if (!sPlayerbotsMgr.GetPlayerbotAI(player) && player->IsWithinDist(source, g_EventChatterRealPlayerDistance, false))
        {
            hasNearbyRealPlayer = true;
            break;
        }
    }

    if (isSourceBot && !hasNearbyRealPlayer && !isGuildEvent)
    {
        if (g_DebugEnabled)
        {
            //LOG_INFO("server.loading", "[OllamaChat] Skipping bot source {} - no real players nearby", source->GetName());
        }
        return;
    }

    if (g_DebugEnabled)
        LOG_INFO("server.loading", "[OllamaChat] DispatchGameEvent from {} | type={} | detail={}", source->GetName(), type, detail);

    float maxDist = g_EventChatterRealPlayerDistance;
    bool disableInCombat = g_DisableRepliesInCombat;

    std::vector<Player*> candidateBots;
    
    if (isGuildEvent)
    {
        // For guild events, get guild bots
        Guild* guild = source->GetGuild();
        for (auto const& pair : ObjectAccessor::GetPlayers())
        {
            Player* player = pair.second;
            if (!player || !player->IsInWorld())
                continue;
                
            PlayerbotAI* ai = sPlayerbotsMgr.GetPlayerbotAI(player);
            if (!ai)
                continue;
                
            if (!player->GetGuild() || player->GetGuild()->GetId() != guild->GetId())
                continue;
                
            // For guild events, just check if there are any real players online in the guild
            bool hasRealPlayerInGuild = false;
            for (auto const& pair2 : ObjectAccessor::GetPlayers())
            {
                Player* guildPlayer = pair2.second;
                if (!guildPlayer || !guildPlayer->IsInWorld())
                    continue;
                    
                if (sPlayerbotsMgr.GetPlayerbotAI(guildPlayer))
                    continue;
                    
                if (guildPlayer->GetGuild() && guildPlayer->GetGuild()->GetId() == guild->GetId())
                {
                    hasRealPlayerInGuild = true;
                    break;
                }
            }
            
            if (hasRealPlayerInGuild)
                candidateBots.push_back(player);
        }
    }
    else
    {
        // For regular events, get nearby bots
        for (auto const& pair : source->GetMap()->GetPlayers())
        {
            Player* player = pair.GetSource();
            if (player->IsWithinDist(source, maxDist, false)) {
                candidateBots.push_back(player);
                if (g_DebugEnabled)
                    LOG_INFO("server.loading", "[OllamaChat] Nearby player {} within {:.1f} yards", player->GetName(), maxDist);
            }
        }
    }

    // Check cooldown for bots
    auto now = std::chrono::steady_clock::now();
    for (auto it = candidateBots.begin(); it != candidateBots.end(); ) {
        Player* bot = *it;
        
        // Apply party restriction only for real player events
        if (!isSourceBot && !IsBotAllowedForRestrictedMode(bot, source)) {
            it = candidateBots.erase(it);
            continue;
        }
        
        auto lastEventTime = botEventCooldowns[bot]; // Track last event time for any event
        if (std::chrono::duration_cast<std::chrono::seconds>(now - lastEventTime).count() < g_EventCooldownTime) {
            it = candidateBots.erase(it); // Remove bot if still in cooldown
        } else {
            botEventCooldowns[bot] = now; // Update last event time
            ++it;
        }
    }

    // Check event-specific chance
    float eventChance = 0.0f;
    if (type == g_EventTypeLearnedSpell) {
        eventChance = g_EventTypeLearnedSpell_Chance;
    } else if (type == g_EventTypeDefeated) {
        eventChance = g_EventTypeDefeated_Chance;
    } else if (type == g_EventTypeDefeatedPlayer) {
        eventChance = g_EventTypeDefeatedPlayer_Chance;
    } else if (type == g_EventTypePetDefeated) {
        eventChance = g_EventTypePetDefeated_Chance;
    } else if (type == g_EventTypeGotItem) {
        eventChance = g_EventTypeGotItem_Chance;
    } else if (type == g_EventTypeDied) {
        eventChance = g_EventTypeDied_Chance;
    } else if (type == g_EventTypeCompletedQuest) {
        eventChance = g_EventTypeCompletedQuest_Chance;
    } else if (type == g_EventTypeRequestedDuel) {
        eventChance = g_EventTypeRequestedDuel_Chance;
    } else if (type == g_EventTypeStartedDueling) {
        eventChance = g_EventTypeStartedDueling_Chance;
    } else if (type == g_EventTypeWonDuel) {
        eventChance = g_EventTypeWonDuel_Chance;
    } else if (type == g_EventTypeLeveledUp) {
        eventChance = g_EventTypeLeveledUp_Chance;
    } else if (type == g_EventTypeAchievement) {
        eventChance = g_EventTypeAchievement_Chance;
    } else if (type == g_EventTypeUsedObject) {
        eventChance = g_EventTypeUsedObject_Chance;
    }

    // Add chance checks for all GuildEventType entries
    if (type == g_GuildEventTypeEpicGear) {
        eventChance = g_GuildEventTypeEpicGear_Chance;
    } else if (type == g_GuildEventTypeRareGear) {
        eventChance = g_GuildEventTypeRareGear_Chance;
    } else if (type == g_GuildEventTypeGuildJoin) {
        eventChance = g_GuildEventTypeGuildJoin_Chance;
    } else if (type == g_GuildEventTypeGuildLogin) {
        eventChance = g_GuildEventTypeGuildLogin_Chance;
    } else if (type == g_GuildEventTypeGuildLeave) {
        eventChance = g_GuildEventTypeGuildLeave_Chance;
    } else if (type == g_GuildEventTypeGuildPromotion) {
        eventChance = g_GuildEventTypeGuildPromotion_Chance;
    } else if (type == g_GuildEventTypeGuildDemotion) {
        eventChance = g_GuildEventTypeGuildDemotion_Chance;
    } else if (type == g_GuildEventTypeGuildAchievement) {
        eventChance = g_GuildEventTypeGuildAchievement_Chance;
    } else if (type == g_GuildEventTypeLevelUp) {
        eventChance = g_GuildEventTypeLevelUp_Chance;
    } else if (type == g_GuildEventTypeDungeonComplete) {
        eventChance = g_GuildEventTypeDungeonComplete_Chance;
    }

    // Arena/BG event chances
    if (type == g_EventTypeArenaStart) {
        eventChance = g_EventTypeArenaStart_Chance;
    } else if (type == g_EventTypeArenaWin) {
        eventChance = g_EventTypeArenaWin_Chance;
    } else if (type == g_EventTypeArenaLoss) {
        eventChance = g_EventTypeArenaLoss_Chance;
    } else if (type == g_EventTypeArenaKill) {
        eventChance = g_EventTypeArenaKill_Chance;
    } else if (type == g_EventTypeBGStart) {
        eventChance = g_EventTypeBGStart_Chance;
    } else if (type == g_EventTypeBGWin) {
        eventChance = g_EventTypeBGWin_Chance;
    } else if (type == g_EventTypeBGLoss) {
        eventChance = g_EventTypeBGLoss_Chance;
    }

    if (urand(0, 100) > eventChance) {
        return;
    }

    // Update cooldowns
    for (Player* bot : candidateBots) {
        botEventCooldowns[bot] = now;
    }

    uint32_t responses = 0;

    for (Player* bot : candidateBots)
    {
        PlayerbotAI* ai = sPlayerbotsMgr.GetPlayerbotAI(bot);
        if (!ai) {
            if (g_DebugEnabled)
                LOG_INFO("server.loading", "[OllamaChat] Skipping {} - not a bot", bot->GetName());
            continue;
        }

        if (disableInCombat && bot->IsInCombat()) {
            if (g_DebugEnabled)
                LOG_INFO("server.loading", "[OllamaChat] Skipping {} - in combat", bot->GetName());
            continue;
        }

        uint32_t chance = 0;
        if(source == bot)
        {
            chance = g_EventChatterBotSelfCommentChance;
        } 
        else if (isGuildEvent)
        {
            chance = g_GuildChatterBotCommentChance;
        }
        else 
        {
            chance = g_EventChatterBotCommentChance;
        }

        if (urand(1, 100) > chance) {
            if (g_DebugEnabled)
                LOG_INFO("server.loading", "[OllamaChat] Skipping {} - failed chance roll", bot->GetName());
            continue;
        }

        if (g_DebugEnabled)
            LOG_INFO("server.loading", "[OllamaChat] Queueing event for bot {}", bot->GetName());

        QueueEvent(bot, type, detail, source->GetName(), isGuildEvent, ctx);

        responses++;
        uint32_t maxBots = isGuildEvent ? g_GuildChatterMaxBotsPerEvent : g_EventChatterMaxBotsPerPlayer;
        if (maxBots > 0 && responses >= maxBots)
            break;
    }

    if (g_DebugEnabled)
        LOG_INFO("server.loading", "[OllamaChat] Dispatch complete. {} bots responded", responses);
}


void OllamaBotEventChatter::QueueEvent(Player* bot, std::string type, std::string detail, std::string actorName, bool isGuildEvent, PvPContext ctx)
{
    if (!g_Enable || !g_EnableEventChatter || !bot)
        return;

    uint64_t botGuid = bot->GetGUID().GetRawValue();

    std::thread([this, botGuid, type, detail, actorName, isGuildEvent, ctx]()
    {
        try
        {
            Player* botPtr = ObjectAccessor::FindPlayer(ObjectGuid(botGuid));
            if (!botPtr) return;

            std::string prompt = BuildPrompt(botPtr, g_EventChatterPromptTemplate, type, detail, actorName, ctx);
            if (prompt.empty()) return;

            std::string response = QueryOllamaAPI(prompt);
            if (response.empty()) return;

            // reacquire pointers before use
            botPtr = ObjectAccessor::FindPlayer(ObjectGuid(botGuid));
            if (!botPtr) return;
            PlayerbotAI* botAI = sPlayerbotsMgr.GetPlayerbotAI(botPtr);
            if (!botAI) return;

            if (isGuildEvent && botPtr->GetGuild())
                botAI->SayToGuild(response);
            else if (botPtr->GetGroup())
                botAI->SayToParty(response);
            else
                botAI->Say(response);
        }
        catch (const std::exception& e)
        {
            LOG_ERROR("server.loading", "[OllamaChat] Exception in QueueEvent thread: {}", e.what());
        }
    }).detach();
}


std::string OllamaBotEventChatter::BuildPrompt(Player* bot, std::string promptTemplate, std::string eventType, std::string eventDetail, std::string actorName, PvPContext ctx)
{
    if (!bot) return "";

    PlayerbotAI* ai = sPlayerbotsMgr.GetPlayerbotAI(bot);
    if (!ai) return "";

    std::string personality = GetBotPersonality(bot);
    std::string personalityPrompt = GetPersonalityPromptAddition(personality);
    std::string botName = bot->GetName();
    std::string botClass = ai->GetChatHelper() ? ai->GetChatHelper()->FormatClass(bot->getClass()) : "UnknownClass";
    uint32_t botLevel = bot->GetLevel();
    std::string botRace = ai->GetChatHelper() ? ai->GetChatHelper()->FormatRace(bot->getRace()) : "UnknownRace";
    std::string botRole = ai->GetChatHelper() ? ChatHelper::FormatClass(bot, AiFactory::GetPlayerSpecTab(bot)) : "UnknownRole";
    std::string botGender = bot->getGender() == GENDER_MALE ? "Male" : "Female";
    std::string botFaction = bot->GetTeamId() == TEAM_ALLIANCE ? "Alliance" : "Horde";

    AreaTableEntry const* botCurrentArea = ai->GetCurrentArea();
    AreaTableEntry const* botCurrentZone = ai->GetCurrentZone();
    std::string botAreaName = botCurrentArea ? ai->GetLocalizedAreaName(botCurrentArea) : "UnknownArea";
    std::string botZoneName = botCurrentZone ? ai->GetLocalizedAreaName(botCurrentZone) : "UnknownZone";
    std::string botMapName = bot->GetMap() ? bot->GetMap()->GetMapName() : "UnknownMap";

    // Try to get sentiment information if the actor is a player
    std::string sentimentInfo = "";
    if (g_EnableSentimentTracking && !actorName.empty())
    {
        // Try to find the actor player by name
        Player* actorPlayer = ObjectAccessor::FindPlayerByName(actorName);
        if (actorPlayer)
        {
            sentimentInfo = GetSentimentPromptAddition(bot, actorPlayer);
        }
    }

    // PvP context string (empty for non-PvP events)
    std::string pvpContextStr = ctx.BuildContextString();

    if (g_DebugEnabled && !pvpContextStr.empty())
        LOG_INFO("server.loading", "[OllamaChat] BuildPrompt: {} pvp_context injected: \"{}\"", botName, pvpContextStr);

    return SafeFormat(
        promptTemplate,
        fmt::arg("bot_name", botName),
        fmt::arg("bot_level", botLevel),
        fmt::arg("bot_class", botClass),
        fmt::arg("bot_race", botRace),
        fmt::arg("bot_gender", botGender),
        fmt::arg("bot_role", botRole),
        fmt::arg("bot_faction", botFaction),
        fmt::arg("bot_area", botAreaName),
        fmt::arg("bot_zone", botZoneName),
        fmt::arg("bot_map", botMapName),
        fmt::arg("bot_personality", personalityPrompt),
        fmt::arg("bot_personality_name", personality),
        fmt::arg("event_type", eventType),
        fmt::arg("event_detail", eventDetail),
        fmt::arg("actor_name", actorName),
        fmt::arg("sentiment_info", sentimentInfo),
        fmt::arg("pvp_context", pvpContextStr)
    );
}

// === Script Hooks ===

ChatOnKill::ChatOnKill() : PlayerScript("ChatOnKill") {}

void ChatOnKill::OnPlayerCreatureKill(Player* killer, Creature* victim)
{
    if (!killer || !victim)
    {
        return;
    }
    eventChatter.DispatchGameEvent(killer, g_EventTypeDefeated, victim->GetName());
}

void ChatOnKill::OnPlayerPVPKill(Player* killer, Player* killed)
{
    if (!killer || !killed)
        return;

    // Check if this kill happened in arena/BG — extract context and use arena-specific event type
    Battleground* bg = killer->GetBattleground();
    PvPContext ctx;
    std::string eventType = g_EventTypeDefeatedPlayer;

    if (bg)
    {
        if (g_DebugEnabled)
            LOG_INFO("server.loading", "[OllamaChat] OnPlayerPVPKill: {} killed {} in BG (arena={}, bgType={})",
                killer->GetName(), killed->GetName(), bg->isArena(), bg->GetBgTypeID());

        ctx = ExtractPvPContext(bg, killer);
        if (bg->isArena() && !g_EventTypeArenaKill.empty())
        {
            eventType = g_EventTypeArenaKill;
            if (g_DebugEnabled)
                LOG_INFO("server.loading", "[OllamaChat] OnPlayerPVPKill: arena kill detected, event type changed to '{}'", eventType);
        }
    }
    else if (g_DebugEnabled)
    {
        LOG_INFO("server.loading", "[OllamaChat] OnPlayerPVPKill: {} killed {} in open world (no BG context)",
            killer->GetName(), killed->GetName());
    }

    eventChatter.DispatchGameEvent(killer, eventType, killed->GetName(), ctx);
}

void ChatOnKill::OnPlayerCreatureKilledByPet(Player* owner, Creature* victim)
{
    if (!owner || !victim)
    {
        return;
    }
    eventChatter.DispatchGameEvent(owner, g_EventTypePetDefeated, victim->GetName());
}

ChatOnLoot::ChatOnLoot() : PlayerScript("ChatOnLoot") {}

void ChatOnLoot::OnPlayerStoreNewItem(Player* player, Item* item, uint32 /*count*/)
{
    if (!player || !item || !item->GetTemplate())
    {
        return;
    }
    if (item->GetTemplate()->Quality >= ITEM_QUALITY_UNCOMMON)
    {
        eventChatter.DispatchGameEvent(player, g_EventTypeGotItem, item->GetTemplate()->Name1);
    }
    
    // Guild-specific gear events
    if (player->GetGuild() && g_EnableGuildEventChatter)
    {
        if (item->GetTemplate()->Quality == ITEM_QUALITY_EPIC && !g_GuildEventTypeEpicGear.empty())
        {
            eventChatter.DispatchGameEvent(player, g_GuildEventTypeEpicGear, item->GetTemplate()->Name1);
        }
        else if (item->GetTemplate()->Quality == ITEM_QUALITY_RARE && !g_GuildEventTypeRareGear.empty())
        {
            // Only announce rare gear if it's equipment (not consumables, etc.)
            if (item->GetTemplate()->Class == ITEM_CLASS_WEAPON || 
                item->GetTemplate()->Class == ITEM_CLASS_ARMOR)
            {
                eventChatter.DispatchGameEvent(player, g_GuildEventTypeRareGear, item->GetTemplate()->Name1);
            }
        }
    }
}

ChatOnDeath::ChatOnDeath() : PlayerScript("ChatOnDeath") {}

void ChatOnDeath::OnPlayerJustDied(Player* player)
{
    if (!player)
    {
        return;
    }
    eventChatter.DispatchGameEvent(player, g_EventTypeDied, "");
}

ChatOnQuest::ChatOnQuest() : PlayerScript("ChatOnQuest") {}

void ChatOnQuest::OnPlayerCompleteQuest(Player* player, Quest const* quest)
{
    if (!player || !quest)
    {
        return;
    }
    eventChatter.DispatchGameEvent(player, g_EventTypeCompletedQuest, quest->GetTitle());
    
    // Guild-specific dungeon completion events
    if (player->GetGuild() && g_EnableGuildEventChatter && !g_GuildEventTypeDungeonComplete.empty())
    {
        // Check if this is a dungeon quest
        if (player->GetMap() && player->GetMap()->IsDungeon())
        {
            std::string dungeonInfo = SafeFormat("{} in {}", quest->GetTitle(), player->GetMap()->GetMapName());
            eventChatter.DispatchGameEvent(player, g_GuildEventTypeDungeonComplete, dungeonInfo);
        }
    }
}

ChatOnLearn::ChatOnLearn() : PlayerScript("ChatOnLearn") {}

void ChatOnLearn::OnPlayerLearnSpell(Player* player, uint32 spellID)
{
    if (!player)
    {
        return;
    }
    SpellInfo const* spellInfo = sSpellMgr->GetSpellInfo(spellID);
    if (spellInfo)
    {
        eventChatter.DispatchGameEvent(player, g_EventTypeLearnedSpell, spellInfo->SpellName[0]);
    }
    else
    {
        eventChatter.DispatchGameEvent(player, g_EventTypeLearnedSpell, std::to_string(spellID));
    }
}

ChatOnDuel::ChatOnDuel() : PlayerScript("ChatOnDuel") {}

void ChatOnDuel::OnPlayerDuelRequest(Player* target, Player* challenger)
{
    if (!challenger || !target)
    {
        return;
    }
    eventChatter.DispatchGameEvent(challenger, g_EventTypeRequestedDuel, target->GetName());
}

void ChatOnDuel::OnPlayerDuelStart(Player* player1, Player* player2)
{
    if (!player1 || !player2)
    {
        return;
    }
    eventChatter.DispatchGameEvent(player1, g_EventTypeStartedDueling, player2->GetName());
}

void ChatOnDuel::OnPlayerDuelEnd(Player* winner, Player* loser, DuelCompleteType /*type*/)
{
    if (!winner || !loser)
    {
        return;
    }
    eventChatter.DispatchGameEvent(winner, g_EventTypeWonDuel, loser->GetName());
}

ChatOnLevelUp::ChatOnLevelUp() : PlayerScript("ChatOnLevelUp") {}

void ChatOnLevelUp::OnPlayerLevelChanged(Player* player, uint8 /*oldLevel*/)
{
    if (!player)
    {
        return;
    }
    eventChatter.DispatchGameEvent(player, g_EventTypeLeveledUp, std::to_string(player->GetLevel()));
    
    // Guild-specific level up events
    if (player->GetGuild() && g_EnableGuildEventChatter && !g_GuildEventTypeLevelUp.empty())
    {
        eventChatter.DispatchGameEvent(player, g_GuildEventTypeLevelUp, std::to_string(player->GetLevel()));
    }
}

ChatOnAchievement::ChatOnAchievement() : PlayerScript("ChatOnAchievement") {}


void ChatOnAchievement::OnPlayerCompleteAchievement(Player* player, AchievementEntry const* achievement)
{
    if (!player || !achievement)
    {
        return;
    }
    eventChatter.DispatchGameEvent(player, g_EventTypeAchievement, achievement->name[0]);

    // Guild-specific achievement event for real players only
    if (player->GetGuild() && g_EnableGuildEventChatter && !g_GuildEventTypeGuildAchievement.empty())
    {
        if (!sPlayerbotsMgr.GetPlayerbotAI(player)) // Only real players
            eventChatter.DispatchGameEvent(player, g_GuildEventTypeGuildAchievement, achievement->name[0]);
    }
}

ChatOnGameObjectUse::ChatOnGameObjectUse() : PlayerScript("ChatOnGameObjectUse") {}

void ChatOnGameObjectUse::OnGameObjectUse(Player* player, GameObject* go)
{
    if (!player || !go || !go->GetGOInfo())
    {
        return;
    }
    eventChatter.DispatchGameEvent(player, g_EventTypeUsedObject, go->GetGOInfo()->name);
}

ChatOnGuildMemberChange::ChatOnGuildMemberChange() : PlayerScript("ChatOnGuildMemberChange") {}

void ChatOnGuildMemberChange::OnGuildMemberJoin(Player* player, Guild* /*guild*/)
{
    if (!player || !player->GetGuild() || !g_EnableGuildEventChatter)
        return;
        
    if (!g_GuildEventTypeGuildJoin.empty())
        eventChatter.DispatchGameEvent(player, g_GuildEventTypeGuildJoin, player->GetGuild()->GetName());
}

void ChatOnGuildMemberChange::OnGuildMemberLeave(Player* player, Guild* guild)
{
    if (!player || !guild || !g_EnableGuildEventChatter)
        return;
        
    if (!g_GuildEventTypeGuildLeave.empty())
        eventChatter.DispatchGameEvent(player, g_GuildEventTypeGuildLeave, guild->GetName());
}

void ChatOnGuildMemberChange::OnGuildMemberRankChange(Player* player, Guild* /*guild*/, uint8 /*oldRank*/, uint8 newRank)
{
    if (!player || !player->GetGuild() || !g_EnableGuildEventChatter)
        return;
        
    if (newRank < player->GetGuild()->GetMember(player->GetGUID())->GetRankId())
    {
        // Promotion
        if (!g_GuildEventTypeGuildPromotion.empty())
            eventChatter.DispatchGameEvent(player, g_GuildEventTypeGuildPromotion, std::to_string(newRank));
    }
    else
    {
        // Demotion
        if (!g_GuildEventTypeGuildDemotion.empty())
            eventChatter.DispatchGameEvent(player, g_GuildEventTypeGuildDemotion, std::to_string(newRank));
    }
}

// Add new event for non-bot guild member login
void ChatOnGuildMemberChange::OnGuildMemberLogin(Player* player, Guild* guild)
{
    if (!player || !guild || !g_EnableGuildEventChatter)
        return;
    if (sPlayerbotsMgr.GetPlayerbotAI(player))
        return; // Only real players
    if (!g_GuildEventTypeGuildLogin.empty())
        eventChatter.DispatchGameEvent(player, g_GuildEventTypeGuildLogin, guild->GetName());
}

// === Arena/BG Event Hooks ===

ChatOnArenaStart::ChatOnArenaStart() : ArenaScript("ChatOnArenaStart") {}

void ChatOnArenaStart::OnArenaStart(Battleground* bg)
{
    if (!bg || g_EventTypeArenaStart.empty())
    {
        if (g_DebugEnabled)
            LOG_INFO("server.loading", "[OllamaChat] OnArenaStart: skipped (bg={}, eventType='{}')",
                bg != nullptr, g_EventTypeArenaStart);
        return;
    }

    if (g_DebugEnabled)
        LOG_INFO("server.loading", "[OllamaChat] OnArenaStart: arena started | type={}v{} | rated={} | {} players",
            bg->GetArenaType(), bg->GetArenaType(), bg->isRated(), bg->GetPlayers().size());

    uint32 botCount = 0;
    for (auto const& [guid, player] : bg->GetPlayers())
    {
        if (!player || !player->IsInWorld())
            continue;
        if (!sPlayerbotsMgr.GetPlayerbotAI(player))
            continue;

        PvPContext ctx = ExtractPvPContext(bg, player);
        std::string detail = ctx.matchType;
        if (ctx.isRated)
            detail += " (rated)";

        if (g_DebugEnabled)
            LOG_INFO("server.loading", "[OllamaChat] OnArenaStart: dispatching to bot {} | detail='{}'", player->GetName(), detail);

        eventChatter.DispatchGameEvent(player, g_EventTypeArenaStart, detail, ctx);
        botCount++;
    }

    if (g_DebugEnabled)
        LOG_INFO("server.loading", "[OllamaChat] OnArenaStart: dispatched to {} bots", botCount);
}

ChatOnBGLifecycle::ChatOnBGLifecycle() : AllBattlegroundScript("ChatOnBGLifecycle") {}

void ChatOnBGLifecycle::OnBattlegroundStart(Battleground* bg)
{
    if (!bg || bg->isArena() || g_EventTypeBGStart.empty())
    {
        if (g_DebugEnabled)
            LOG_INFO("server.loading", "[OllamaChat] OnBattlegroundStart: skipped (bg={}, isArena={}, eventType='{}')",
                bg != nullptr, bg ? bg->isArena() : false, g_EventTypeBGStart);
        return;
    }

    if (g_DebugEnabled)
        LOG_INFO("server.loading", "[OllamaChat] OnBattlegroundStart: BG started | bgType={} | {} players",
            bg->GetBgTypeID(), bg->GetPlayers().size());

    uint32 botCount = 0;
    for (auto const& [guid, player] : bg->GetPlayers())
    {
        if (!player || !player->IsInWorld())
            continue;
        if (!sPlayerbotsMgr.GetPlayerbotAI(player))
            continue;

        PvPContext ctx = ExtractPvPContext(bg, player);

        if (g_DebugEnabled)
            LOG_INFO("server.loading", "[OllamaChat] OnBattlegroundStart: dispatching to bot {} | match='{}'", player->GetName(), ctx.matchType);

        eventChatter.DispatchGameEvent(player, g_EventTypeBGStart, ctx.matchType, ctx);
        botCount++;
    }

    if (g_DebugEnabled)
        LOG_INFO("server.loading", "[OllamaChat] OnBattlegroundStart: dispatched to {} bots", botCount);
}

void ChatOnBGLifecycle::OnBattlegroundEnd(Battleground* bg, TeamId winnerTeam)
{
    if (!bg)
        return;

    bool isArena = bg->isArena();
    std::string winType = isArena ? g_EventTypeArenaWin : g_EventTypeBGWin;
    std::string lossType = isArena ? g_EventTypeArenaLoss : g_EventTypeBGLoss;

    if (winType.empty() && lossType.empty())
    {
        if (g_DebugEnabled)
            LOG_INFO("server.loading", "[OllamaChat] OnBattlegroundEnd: skipped (both win/loss event types empty, isArena={})", isArena);
        return;
    }

    if (g_DebugEnabled)
        LOG_INFO("server.loading", "[OllamaChat] OnBattlegroundEnd: {} ended | isArena={} | winner={} | {} players",
            isArena ? "Arena" : "BG", isArena, winnerTeam == TEAM_ALLIANCE ? "Alliance" : "Horde", bg->GetPlayers().size());

    uint32 botCount = 0;
    for (auto const& [guid, player] : bg->GetPlayers())
    {
        if (!player || !player->IsInWorld())
            continue;
        if (!sPlayerbotsMgr.GetPlayerbotAI(player))
            continue;

        PvPContext ctx = ExtractPvPContext(bg, player);
        bool won = (player->GetTeamId() == winnerTeam);
        std::string eventType = won ? winType : lossType;
        if (eventType.empty())
            continue;

        std::string detail = ctx.matchType;
        if (!isArena)
            detail += fmt::format(" ({} - {})", ctx.allyScore, ctx.hordeScore);

        if (g_DebugEnabled)
            LOG_INFO("server.loading", "[OllamaChat] OnBattlegroundEnd: bot {} {} | type='{}' | detail='{}'",
                player->GetName(), won ? "WON" : "LOST", eventType, detail);

        eventChatter.DispatchGameEvent(player, eventType, detail, ctx);
        botCount++;
    }

    if (g_DebugEnabled)
        LOG_INFO("server.loading", "[OllamaChat] OnBattlegroundEnd: dispatched to {} bots", botCount);
}

