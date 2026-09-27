#ifndef MOD_OLLAMA_CHAT_EVENTS_H
#define MOD_OLLAMA_CHAT_EVENTS_H

#include "ScriptMgr.h"
#include "Player.h"
#include "Battleground.h"
#include <string>

// PvP/Arena/BG context data — extracted from Battleground* on main thread before async dispatch.
// Value type: safe to copy into lambda captures with zero pointer risk.
struct PvPContext
{
    bool isInPvP = false;
    bool isArena = false;
    bool isRated = false;
    uint8 arenaType = 0;           // 2, 3, or 5
    std::string matchType;         // "2v2 Arena", "Warsong Gulch", etc.
    uint32 allyScore = 0;
    uint32 hordeScore = 0;
    uint32 allyAlive = 0;
    uint32 hordeAlive = 0;
    uint32 matchDurationSec = 0;
    TeamId botTeam = TEAM_NEUTRAL;
    std::string enemyComp;         // "Nilenette (Priest), Jeldor (Mage)"
    std::string friendlyComp;      // "Haabugu (Warrior), Aad (Paladin)"
    std::string deadPlayers;       // "Nilenette (Priest) is dead"
    uint32 arenaRating = 0;        // team MMR
    std::string botCombatState;    // "DPS at 85% HP, 40% mana. Targeting Jeldor (Mage)"

    std::string BuildContextString() const;
};

class OllamaBotEventChatter
{
public:
    void DispatchGameEvent(Player* source, std::string type, std::string detail, PvPContext ctx = {});
    void QueueEvent(Player* bot, std::string type, std::string detail, std::string actorName, bool isGuildEvent = false, PvPContext ctx = {});
    std::string BuildPrompt(Player* bot, std::string promptTemplate, std::string eventType, std::string eventDetail, std::string actorName, PvPContext ctx = {});
};

class ChatOnKill : public PlayerScript
{
public:
    ChatOnKill();
    void OnPlayerCreatureKill(Player* killer, Creature* victim);
    void OnPlayerPVPKill(Player* killer, Player* killed);
    void OnPlayerCreatureKilledByPet(Player* owner, Creature* victim);
};

class ChatOnLoot : public PlayerScript
{
public:
    ChatOnLoot();
    void OnPlayerStoreNewItem(Player* player, Item* item, uint32 count);
};

class ChatOnDeath : public PlayerScript
{
public:
    ChatOnDeath();
    void OnPlayerJustDied(Player* player);
};

class ChatOnQuest : public PlayerScript
{
public:
    ChatOnQuest();
    void OnPlayerCompleteQuest(Player* player, Quest const* quest);
};

class ChatOnLearn : public PlayerScript
{
public:
    ChatOnLearn();
    void OnPlayerLearnSpell(Player* player, uint32 spellID);
};

class ChatOnDuel : public PlayerScript
{
public:
    ChatOnDuel();
    void OnPlayerDuelRequest(Player* target, Player* challenger);
    void OnPlayerDuelStart(Player* player1, Player* player2);
    void OnPlayerDuelEnd(Player* winner, Player* loser, DuelCompleteType type);
};

// Extra events:
class ChatOnLevelUp : public PlayerScript
{
public:
    ChatOnLevelUp();
    void OnPlayerLevelChanged(Player* player, uint8 oldLevel);
};

class ChatOnAchievement : public PlayerScript
{
public:
    ChatOnAchievement();
    void OnPlayerCompleteAchievement(Player* player, AchievementEntry const* achievement);
};

class ChatOnGameObjectUse : public PlayerScript
{
public:
    ChatOnGameObjectUse();
    void OnGameObjectUse(Player* player, GameObject* go);
};

class ChatOnGuildMemberChange : public PlayerScript
{
public:
    ChatOnGuildMemberChange();
    void OnGuildMemberJoin(Player* player, Guild* guild);
    void OnGuildMemberLeave(Player* player, Guild* guild);
    void OnGuildMemberRankChange(Player* player, Guild* guild, uint8 oldRank, uint8 newRank);
    void OnGuildMemberLogin(Player* player, Guild* guild);
};

// Arena/BG event hooks
class ChatOnArenaStart : public ArenaScript
{
public:
    ChatOnArenaStart();
    void OnArenaStart(Battleground* bg) override;
};

class ChatOnBGLifecycle : public AllBattlegroundScript
{
public:
    ChatOnBGLifecycle();
    void OnBattlegroundStart(Battleground* bg) override;
    void OnBattlegroundEnd(Battleground* bg, TeamId winnerTeam) override;
};

#endif // MOD_OLLAMA_CHAT_EVENTS_H
