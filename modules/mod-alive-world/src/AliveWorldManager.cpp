/*
 * mod-alive-world - Main Manager Implementation
 * Copyright (C) 2025 EC
 */

#include "AliveWorldManager.h"
#include "EventCollector.h"
#include "WorldStateManager.h"
#include "GossipTemplateManager.h"
#include "BotMemoryManager.h"
#include "IntelligenceTierManager.h"
#include "AIDecisionEngine.h"
#include "BehaviorExecutor.h"
#include "StrategyParser.h"
#include "EmoteReactionSystem.h"
#include "AliveWorldBridgeAdapter.h"

#include "Config.h"
#include "Creature.h"
#include "GameTime.h"
#include "Log.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "World.h"
#include "WorldSession.h"
#include "WorldSessionMgr.h"
#include "Chat.h"

#include <algorithm>
#include <cstdarg>
#include <memory>
#include <random>
#include <sstream>

#ifdef MOD_PLAYERBOTS
#include "PlayerbotAI.h"
#include "PlayerbotMgr.h"
#include "RandomPlayerbotMgr.h"
#endif

namespace AliveWorld
{

// =========================================================================
// LOGGER
// =========================================================================

bool Logger::s_debugEnabled = false;
uint8 Logger::s_logLevel = 2;

void Logger::Log(AWLogLevel level, const char* func, const char* fmt, ...)
{
    if (static_cast<uint8>(level) > s_logLevel && level != AWLogLevel::AW_ERROR)
        return;

    char buffer[1024];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);

    std::stringstream ss;
    ss << "[AliveWorld:" << AWLogLevelToString(level) << "] [" << func << "] " << buffer;
    std::string message = ss.str();

    switch (level)
    {
        case AWLogLevel::AW_ERROR:
            LOG_ERROR("module.alive-world", "{}", message);
            break;
        case AWLogLevel::AW_WARNING:
            LOG_WARN("module.alive-world", "{}", message);
            break;
        case AWLogLevel::AW_INFO:
            LOG_INFO("module.alive-world", "{}", message);
            break;
        case AWLogLevel::AW_DEBUG:
        case AWLogLevel::AW_TRACE:
            LOG_DEBUG("module.alive-world", "{}", message);
            break;
    }

    if (s_debugEnabled)
    {
        LogToGame(message);
    }
}

void Logger::LogToGame(const std::string& message)
{
    WorldSessionMgr::SessionMap const& sessions = sWorldSessionMgr->GetAllSessions();
    for (auto const& pair : sessions)
    {
        if (WorldSession* session = pair.second)
        {
            if (Player* player = session->GetPlayer())
            {
                if (player->IsGameMaster() || player->GetSession()->GetSecurity() >= SEC_GAMEMASTER)
                {
                    ChatHandler(player->GetSession()).SendSysMessage("%s", message.c_str());
                }
            }
        }
    }
}

void Logger::SetDebugEnabled(bool enabled)
{
    s_debugEnabled = enabled;
    LOG_INFO("module.alive-world", "AliveWorld: Debug mode {}", enabled ? "ENABLED" : "DISABLED");
}

void Logger::SetLogLevel(uint8 level)
{
    s_logLevel = level;
    LOG_INFO("module.alive-world", "AliveWorld: Log level set to {}", level);
}

bool Logger::IsDebugEnabled() { return s_debugEnabled; }
uint8 Logger::GetLogLevel() { return s_logLevel; }

// =========================================================================
// ALIVE WORLD MANAGER
// =========================================================================

AliveWorldManager::AliveWorldManager()
    : m_enabled(true)
    , m_updateIntervalMs(1000)
    , m_relentlessHunterEnabled(true)
    , m_relentlessHunterUpdateIntervalMs(1500)
    , m_relentlessHunterCrossZoneEnabled(false)
    , m_relentlessHunterCrossMapTeleportEnabled(false)
    , m_relentlessHunterTeleportCooldownMs(30000)
    , m_debugEnabled(false)
    , m_debugLogLevel(2)
    , m_initialized(false)
    , m_startTime(0)
    , m_updateTimer(0)
    , m_relentlessHunterTimer(0)
{
}

AliveWorldManager::~AliveWorldManager()
{
    Shutdown();
}

AliveWorldManager* AliveWorldManager::Instance()
{
    static AliveWorldManager instance;
    return &instance;
}

void AliveWorldManager::Initialize()
{
    ALIVE_LOG_INFO("========================================");
    ALIVE_LOG_INFO("Alive World System Initializing...");
    ALIVE_LOG_INFO("========================================");

    LoadConfig();

    if (!m_enabled)
    {
        ALIVE_LOG_WARN("Alive World System DISABLED in config");
        return;
    }

    m_startTime = NowMs();

    // Create subsystems
    m_eventCollector = std::make_unique<EventCollector>();
    m_worldStateManager = std::make_unique<WorldStateManager>();
    m_gossipTemplateManager = std::make_unique<GossipTemplateManager>();
    m_memoryManager = std::make_unique<BotMemoryManager>();
    m_tierManager = std::make_unique<IntelligenceTierManager>();
    m_aiEngine = std::make_unique<AIDecisionEngine>();
    m_behaviorExecutor = std::make_unique<BehaviorExecutor>();
    m_strategyParser = std::make_unique<StrategyParser>();
    m_emoteSystem = std::make_unique<EmoteReactionSystem>();

    // Initialize in dependency order
    m_eventCollector->Initialize();
    m_gossipTemplateManager->Initialize();  // Load templates from DB
    m_worldStateManager->Initialize(m_eventCollector.get(), m_gossipTemplateManager.get());
    m_memoryManager->Initialize(m_worldStateManager.get());
    m_worldStateManager->SetMemoryManager(m_memoryManager.get());  // Connect for gossip spreading
    m_tierManager->Initialize(m_memoryManager.get(), m_worldStateManager.get());
    m_aiEngine->Initialize(m_memoryManager.get(), m_worldStateManager.get(), m_tierManager.get());
    m_behaviorExecutor->Initialize(m_memoryManager.get(), m_worldStateManager.get(), nullptr);
    m_strategyParser->Initialize(m_behaviorExecutor.get());
    m_emoteSystem->Initialize(m_memoryManager.get(), m_worldStateManager.get());

    // Initialize bridge adapter for realtime events
#ifdef MOD_EC_REALTIME_BRIDGE
    sAliveWorldBridge->Initialize();
    ALIVE_LOG_INFO("Bridge adapter initialized for realtime events");
#else
    ALIVE_LOG_INFO("Realtime bridge not available - events will not stream to console");
#endif

    m_initialized = true;

    ALIVE_LOG_INFO("========================================");
    ALIVE_LOG_INFO("Alive World System READY");
    ALIVE_LOG_INFO("========================================");
}

void AliveWorldManager::LoadConfig()
{
    m_enabled = sConfigMgr->GetOption<bool>("AliveWorld.Enable", true);
    m_updateIntervalMs = sConfigMgr->GetOption<uint32>("AliveWorld.UpdateIntervalMs", 1000);
    m_relentlessHunterEnabled = sConfigMgr->GetOption<bool>("AliveWorld.Hunter.Enable", true);
    m_relentlessHunterUpdateIntervalMs = sConfigMgr->GetOption<uint32>("AliveWorld.Hunter.UpdateIntervalMs", 1500);
    m_relentlessHunterCrossZoneEnabled = sConfigMgr->GetOption<bool>("AliveWorld.Hunter.CrossZoneEnable", false);
    m_relentlessHunterCrossMapTeleportEnabled = sConfigMgr->GetOption<bool>("AliveWorld.Hunter.CrossMapTeleportEnable", false);
    m_relentlessHunterTeleportCooldownMs = sConfigMgr->GetOption<uint32>("AliveWorld.Hunter.CrossMapTeleportCooldownMs", 30000);
    m_relentlessHunterTeleportCooldownMs = std::clamp(m_relentlessHunterTeleportCooldownMs, 1000u, 600000u);
    m_debugEnabled = sConfigMgr->GetOption<bool>("AliveWorld.Debug", false);
    m_debugLogLevel = sConfigMgr->GetOption<uint8>("AliveWorld.DebugLogLevel", 2);

    Logger::SetDebugEnabled(m_debugEnabled);
    Logger::SetLogLevel(m_debugLogLevel);

    ALIVE_LOG_INFO("Configuration loaded:");
    ALIVE_LOG_INFO("  Enabled: %s", m_enabled ? "YES" : "NO");
    ALIVE_LOG_INFO("  Update Interval: %u ms", m_updateIntervalMs);
    ALIVE_LOG_INFO("  Hunter Profiles: %s (interval %u ms)",
        m_relentlessHunterEnabled ? "ON" : "OFF", m_relentlessHunterUpdateIntervalMs);
    ALIVE_LOG_INFO("  Hunter Cross-Zone: %s", m_relentlessHunterCrossZoneEnabled ? "ON" : "OFF");
    ALIVE_LOG_INFO("  Hunter Cross-Map Teleport: %s (cooldown %u ms)",
        m_relentlessHunterCrossMapTeleportEnabled ? "ON" : "OFF",
        m_relentlessHunterTeleportCooldownMs);
    ALIVE_LOG_INFO("  Debug: %s (level %u)", m_debugEnabled ? "ON" : "OFF", m_debugLogLevel);

    if (m_initialized && m_memoryManager)
    {
        m_memoryManager->LoadConfig();
        m_memoryManager->ReloadPersonalityOverrides();
    }
}

void AliveWorldManager::Shutdown()
{
    if (!m_initialized)
        return;

    ALIVE_LOG_INFO("Alive World System shutting down...");

    // Shutdown bridge adapter
#ifdef MOD_EC_REALTIME_BRIDGE
    sAliveWorldBridge->Shutdown();
#endif

    // Shutdown AI engine (stops worker threads)
    if (m_aiEngine)
        m_aiEngine->Shutdown();

    // Save bot memories
    if (m_memoryManager)
        m_memoryManager->SaveToDatabase();

    // Clear subsystems
    m_emoteSystem.reset();
    m_strategyParser.reset();
    m_behaviorExecutor.reset();
    m_aiEngine.reset();
    m_tierManager.reset();
    m_memoryManager.reset();
    m_worldStateManager.reset();
    m_eventCollector.reset();
    m_hunterNextActionMs.clear();
    m_hunterNextTeleportMs.clear();
    m_relentlessHunterTimer = 0;

    m_initialized = false;

    ALIVE_LOG_INFO("Alive World System shutdown complete");
}

void AliveWorldManager::Update(uint32 diff)
{
    if (!m_enabled || !m_initialized)
        return;

    m_updateTimer += diff;
    if (m_updateTimer < m_updateIntervalMs)
        return;

    m_updateTimer = 0;

    // Update all subsystems
    if (m_eventCollector)
        m_eventCollector->Update(diff);

    if (m_worldStateManager)
        m_worldStateManager->Update(diff);

    if (m_memoryManager)
        m_memoryManager->Update(diff);

    if (m_tierManager)
        m_tierManager->Update(diff);

    if (m_aiEngine)
        m_aiEngine->Update(diff);

    // Check bot proximity to players and health/mana status
    UpdateBotProximityAndHealth();
    UpdateRelentlessHunters(diff);
}

void AliveWorldManager::UpdateBotProximityAndHealth()
{
#ifdef MOD_PLAYERBOTS
    // Configuration thresholds
    static const float PROXIMITY_RANGE = 50.0f;       // Range to detect nearby players
    static const float LOW_HEALTH_PCT = 30.0f;        // Below this = low health
    static const float LOW_MANA_PCT = 20.0f;          // Below this = low mana
    static const uint32 CHECK_INTERVAL_MS = 5000;     // Check every 5 seconds
    static uint32 s_lastCheckTime = 0;

    uint32 now = GameTime::GetGameTimeMS().count();
    if (now - s_lastCheckTime < CHECK_INTERVAL_MS)
        return;
    s_lastCheckTime = now;

    // Get all tier 1 bots (active bots) for checking
    if (!m_tierManager)
        return;

    auto tier1Bots = m_tierManager->GetTier1Bots();
    if (tier1Bots.empty())
        return;

    // Build set of real players for proximity checking
    std::vector<Player*> realPlayers;
    WorldSessionMgr::SessionMap const& sessionMap = sWorldSessionMgr->GetAllSessions();
    for (auto const& [accountId, session] : sessionMap)
    {
        if (!session)
            continue;
        Player* player = session->GetPlayer();
        if (player && player->IsInWorld() && !IsBot(player))
            realPlayers.push_back(player);
    }

    if (realPlayers.empty())
        return;

    // Check each tier 1 bot
    for (uint64 botGuid : tier1Bots)
    {
        Player* bot = ObjectAccessor::FindPlayer(ObjectGuid::Create<HighGuid::Player>(botGuid));
        if (!bot || !bot->IsInWorld())
            continue;

        // Check proximity to real players
        for (Player* player : realPlayers)
        {
            if (!player->IsInWorld() || bot->GetMapId() != player->GetMapId())
                continue;

            float distance = bot->GetDistance(player);
            if (distance <= PROXIMITY_RANGE)
            {
                // Emit proximity event
                sAliveWorldBridge->EmitBotNearPlayer(
                    bot->GetGUID().GetCounter(),
                    player->GetGUID().GetCounter(),
                    distance
                );
            }
        }

        // Check health
        float healthPct = bot->GetHealthPct();
        if (healthPct > 0 && healthPct < LOW_HEALTH_PCT)
        {
            sAliveWorldBridge->EmitBotLowHealth(bot->GetGUID().GetCounter(), healthPct);
        }

        // Check mana (if applicable)
        if (bot->GetMaxPower(POWER_MANA) > 0)
        {
            float manaPct = (100.0f * bot->GetPower(POWER_MANA)) / bot->GetMaxPower(POWER_MANA);
            if (manaPct < LOW_MANA_PCT)
            {
                sAliveWorldBridge->EmitBotLowMana(bot->GetGUID().GetCounter(), manaPct);
            }
        }
    }
#endif
}

void AliveWorldManager::UpdateRelentlessHunters(uint32 diff)
{
#ifdef MOD_PLAYERBOTS
    if (!m_relentlessHunterEnabled || !m_memoryManager || !m_behaviorExecutor)
        return;

    m_relentlessHunterTimer += diff;
    if (m_relentlessHunterTimer < m_relentlessHunterUpdateIntervalMs)
        return;

    m_relentlessHunterTimer = 0;

    std::vector<BotPersonalityOverride> hunters = m_memoryManager->GetRelentlessHunterProfiles();
    if (hunters.empty())
        return;

    uint64 now = NowMs();

    for (const BotPersonalityOverride& hunter : hunters)
    {
        Player* bot = ObjectAccessor::FindPlayer(ObjectGuid::Create<HighGuid::Player>(hunter.botGuid));
        Player* target = ObjectAccessor::FindPlayer(ObjectGuid::Create<HighGuid::Player>(hunter.relentlessTargetGuid));
        if (!bot || !target || !bot->IsInWorld() || !target->IsInWorld())
            continue;

        if (bot == target)
            continue;

        if (!bot->IsAlive())
            continue;

        auto cooldownIt = m_hunterNextActionMs.find(hunter.botGuid);
        if (cooldownIt != m_hunterNextActionMs.end() && now < cooldownIt->second)
            continue;

        bool sameMap = (bot->GetMapId() == target->GetMapId());
        auto teleportCooldownIt = m_hunterNextTeleportMs.find(hunter.botGuid);
        bool canTeleportNow = (teleportCooldownIt == m_hunterNextTeleportMs.end() || now >= teleportCooldownIt->second);

        // Optional cross-map pursuit mode.
        if (!sameMap)
        {
            if (!m_relentlessHunterCrossZoneEnabled || !m_relentlessHunterCrossMapTeleportEnabled || !canTeleportNow)
                continue;

            if (bot->TeleportTo(target->GetMapId(),
                                target->GetPositionX(),
                                target->GetPositionY(),
                                target->GetPositionZ(),
                                target->GetOrientation()))
            {
                m_hunterNextTeleportMs[hunter.botGuid] = now + m_relentlessHunterTeleportCooldownMs;
                m_hunterNextActionMs[hunter.botGuid] = now + std::max<uint32>(hunter.hunterReengageCooldownMs, 1000u);
            }
            continue;
        }

        float pursuitRadius = std::clamp(hunter.hunterPursuitRadius, 5.0f, 500.0f);
        float engageRadius = std::clamp(hunter.hunterEngageRadius, 5.0f, pursuitRadius);
        float distance = bot->GetDistance(target);

        bool issuedAction = false;

        if (distance > pursuitRadius)
        {
            if (!m_relentlessHunterCrossZoneEnabled)
                continue;

            // For far same-map chase, either follow normally or optionally blink
            // close when teleport mode is enabled.
            if (m_relentlessHunterCrossMapTeleportEnabled &&
                distance > pursuitRadius * 3.0f && canTeleportNow)
            {
                if (bot->TeleportTo(target->GetMapId(),
                                    target->GetPositionX(),
                                    target->GetPositionY(),
                                    target->GetPositionZ(),
                                    target->GetOrientation()))
                {
                    issuedAction = true;
                    m_hunterNextTeleportMs[hunter.botGuid] = now + m_relentlessHunterTeleportCooldownMs;
                }
            }
            else
            {
                m_behaviorExecutor->Follow(hunter.botGuid, hunter.relentlessTargetGuid);
                issuedAction = true;
            }
        }

        // Corpse-camp style behavior: stay on top of the target while they are dead.
        else if (!target->IsAlive())
        {
            m_behaviorExecutor->Follow(hunter.botGuid, hunter.relentlessTargetGuid);
            issuedAction = true;
        }
        else if (bot->IsValidAttackTarget(target) && distance <= engageRadius)
        {
            m_behaviorExecutor->SetTarget(hunter.botGuid, hunter.relentlessTargetGuid);

            PlayerbotAI* ai = sPlayerbotsMgr.GetPlayerbotAI(bot);
            if (ai)
            {
                ai->DoSpecificAction("attack", Event(), true);
                issuedAction = true;
            }
        }
        else
        {
            m_behaviorExecutor->Follow(hunter.botGuid, hunter.relentlessTargetGuid);
            issuedAction = true;
        }

        if (issuedAction)
            m_hunterNextActionMs[hunter.botGuid] = now + hunter.hunterReengageCooldownMs;
    }
#else
    (void)diff;
#endif
}

bool AliveWorldManager::IsBot(Unit* unit) const
{
    if (!unit || !unit->IsPlayer())
        return false;

#ifdef MOD_PLAYERBOTS
    Player* player = unit->ToPlayer();
    if (player && sPlayerbotsMgr.GetPlayerbotAI(player))
        return true;
    if (sRandomPlayerbotMgr.IsRandomBot(player))
        return true;
#endif

    return false;
}

BotTier AliveWorldManager::GetBotTier(uint32 botGuid) const
{
    if (!m_tierManager)
        return BotTier::TIER_3_PASSIVE;

    return m_tierManager->GetBotTier(static_cast<uint64>(botGuid));
}

// =========================================================================
// EVENT HOOKS
// =========================================================================

void AliveWorldManager::OnPlayerLogin(Player* player)
{
    if (!m_enabled || !m_initialized || !player)
        return;

    bool isBot = IsBot(player);

    if (isBot)
    {
        OnBotSpawn(player);
    }
    else
    {
        ALIVE_LOG_TRACE("Real player logged in: %s", player->GetName().c_str());
    }
}

void AliveWorldManager::OnPlayerLogout(Player* player)
{
    if (!m_enabled || !m_initialized || !player)
        return;

    bool isBot = IsBot(player);

    if (isBot)
    {
        OnBotDespawn(player);
    }
}

void AliveWorldManager::OnPlayerChat(Player* player, uint32 type, const std::string& message)
{
    if (!m_enabled || !m_initialized || !player || message.empty())
        return;

    if (m_eventCollector)
    {
        m_eventCollector->RecordChat(player, message, player->GetZoneId());
    }
}

void AliveWorldManager::OnPlayerEmote(Player* player, uint32 emoteId)
{
    if (!m_enabled || !m_initialized || !player)
        return;

    if (m_eventCollector)
    {
        m_eventCollector->RecordEmote(player, emoteId, player->GetZoneId());
    }
}

void AliveWorldManager::OnPlayerKillCreature(Player* player, Creature* creature)
{
    if (!m_enabled || !m_initialized || !player || !creature)
        return;

    if (m_eventCollector)
    {
        m_eventCollector->RecordCombatKill(player, creature, player->GetZoneId(), player->GetAreaId());
    }

    // Record combat in bot memory if player is a bot
    if (IsBot(player) && m_memoryManager)
    {
        CombatMemory combat;
        combat.timestamp = NowMs();
        combat.wasVictorious = true;
        combat.opponentType = creature->isElite() ? "elite" : "mob";
        combat.opponentName = creature->GetName();
        combat.opponentGuid = creature->GetGUID();
        combat.zoneId = player->GetZoneId();

        m_memoryManager->RecordCombat(player->GetGUID().GetRawValue(), combat);
    }
}

void AliveWorldManager::OnPlayerKillPlayer(Player* killer, Player* victim)
{
    if (!m_enabled || !m_initialized || !killer || !victim)
        return;

    if (m_eventCollector)
    {
        m_eventCollector->RecordCombatKill(killer, victim, killer->GetZoneId(), killer->GetAreaId());
    }

    // Record combat memory
    if (m_memoryManager)
    {
        bool killerIsBot = IsBot(killer);
        bool victimIsBot = IsBot(victim);

        if (killerIsBot)
        {
            CombatMemory combat;
            combat.timestamp = NowMs();
            combat.wasVictorious = true;
            combat.opponentType = victimIsBot ? "bot" : "player";
            combat.opponentName = victim->GetName();
            combat.opponentGuid = victim->GetGUID();
            combat.zoneId = killer->GetZoneId();

            m_memoryManager->RecordCombat(killer->GetGUID().GetRawValue(), combat);

            // Personality evolution
            m_memoryManager->EvolvePersonality(killer->GetGUID().GetRawValue(), "pvp_win", 0.05f);
        }

        if (victimIsBot)
        {
            CombatMemory combat;
            combat.timestamp = NowMs();
            combat.wasVictorious = false;
            combat.opponentType = killerIsBot ? "bot" : "player";
            combat.opponentName = killer->GetName();
            combat.opponentGuid = killer->GetGUID();
            combat.zoneId = victim->GetZoneId();

            m_memoryManager->RecordCombat(victim->GetGUID().GetRawValue(), combat);

            // Personality evolution
            m_memoryManager->EvolvePersonality(victim->GetGUID().GetRawValue(), "pvp_loss", 0.03f);
        }

        // Record social interaction (negative sentiment for PvP)
        if (killerIsBot && !victimIsBot)
        {
            m_memoryManager->RecordSocialInteraction(
                killer->GetGUID().GetRawValue(),
                victim->GetGUID().GetRawValue(),
                victim->GetName(),
                "combat",
                -0.3f
            );
        }
    }
}

void AliveWorldManager::OnPlayerDeath(Player* player, Unit* killer)
{
    if (!m_enabled || !m_initialized || !player)
        return;

    if (m_eventCollector)
    {
        m_eventCollector->RecordPlayerDeath(player, killer);
    }
}

void AliveWorldManager::OnCreatureDeath(Creature* creature, Unit* killer)
{
    if (!m_enabled || !m_initialized || !creature)
        return;

    // Check if this was a boss
    if (creature->isWorldBoss() || creature->IsDungeonBoss())
    {
        Player* killerPlayer = killer ? killer->ToPlayer() : nullptr;
        if (m_eventCollector)
        {
            m_eventCollector->RecordBossKilled(creature, killerPlayer);
        }
    }
}

void AliveWorldManager::OnPlayerChangeZone(Player* player, uint32 newZoneId, uint32 newAreaId)
{
    if (!m_enabled || !m_initialized || !player)
        return;

    bool isBot = IsBot(player);
    uint32 oldZoneId = player->GetZoneId();  // Note: May need to track this separately

    if (m_eventCollector)
    {
        m_eventCollector->RecordZoneChange(player, oldZoneId, newZoneId, isBot);
    }

    if (m_worldStateManager)
    {
        bool isAlliance = (player->GetTeamId() == TEAM_ALLIANCE);

        if (oldZoneId > 0)
            m_worldStateManager->OnPlayerLeaveZone(player->GetGUID().GetRawValue(), oldZoneId, isAlliance, isBot);

        m_worldStateManager->OnPlayerEnterZone(player->GetGUID().GetRawValue(), newZoneId, isAlliance, isBot);
    }

    if (isBot && m_memoryManager)
    {
        m_memoryManager->SetCurrentZone(player->GetGUID().GetRawValue(), newZoneId);
    }
}

void AliveWorldManager::OnBotSpawn(Player* bot)
{
    if (!m_enabled || !m_initialized || !bot)
        return;

    ALIVE_LOG_TRACE("Bot spawned: %s", bot->GetName().c_str());

    // Create/load bot memory
    if (m_memoryManager)
    {
        m_memoryManager->GetOrCreateBotMemory(bot->GetGUID().GetRawValue(), bot->GetName());
    }
}

void AliveWorldManager::OnBotDespawn(Player* bot)
{
    if (!m_enabled || !m_initialized || !bot)
        return;

    ALIVE_LOG_TRACE("Bot despawned: %s", bot->GetName().c_str());

    // Save bot memory
    if (m_memoryManager)
    {
        m_memoryManager->SaveBotMemory(bot->GetGUID().GetRawValue());
    }
}

void AliveWorldManager::OnGroupFormed(Player* leader)
{
    if (!m_enabled || !m_initialized || !leader)
        return;

    if (m_eventCollector)
    {
        m_eventCollector->RecordGroupEvent(leader, true, leader->GetZoneId());
    }
}

void AliveWorldManager::OnGroupDisbanded(Player* leader)
{
    if (!m_enabled || !m_initialized || !leader)
        return;

    if (m_eventCollector)
    {
        m_eventCollector->RecordGroupEvent(leader, false, leader->GetZoneId());
    }

    // Emit to Node.js backend
    sAliveWorldBridge->EmitGroupDisbanded(leader);
}

void AliveWorldManager::OnTradeComplete(Player* player1, Player* player2)
{
    if (!m_enabled || !m_initialized || !player1 || !player2)
        return;

    if (m_eventCollector)
    {
        m_eventCollector->RecordTrade(player1, player2, player1->GetZoneId());
    }

    // Record social interaction
    if (m_memoryManager)
    {
        if (IsBot(player1))
        {
            m_memoryManager->RecordSocialInteraction(
                player1->GetGUID().GetRawValue(),
                player2->GetGUID().GetRawValue(),
                player2->GetName(),
                "trade",
                0.1f
            );
        }
        if (IsBot(player2))
        {
            m_memoryManager->RecordSocialInteraction(
                player2->GetGUID().GetRawValue(),
                player1->GetGUID().GetRawValue(),
                player1->GetName(),
                "trade",
                0.1f
            );
        }
    }
}

void AliveWorldManager::OnDuelStart(Player* player1, Player* player2)
{
    if (!m_enabled || !m_initialized || !player1 || !player2)
        return;

    if (m_eventCollector)
    {
        m_eventCollector->RecordDuel(player1, player2, true, nullptr);
    }
}

void AliveWorldManager::OnDuelEnd(Player* winner, Player* loser)
{
    if (!m_enabled || !m_initialized || !winner || !loser)
        return;

    if (m_eventCollector)
    {
        m_eventCollector->RecordDuel(winner, loser, false, winner);
    }
}

void AliveWorldManager::OnBossEngaged(Creature* boss, Player* engager)
{
    if (!m_enabled || !m_initialized || !boss)
        return;

    if (m_eventCollector)
    {
        m_eventCollector->RecordBossEngaged(boss, engager);
    }

    // Stream to Node.js backend via realtime bridge
    sAliveWorldBridge->EmitBossEngaged(boss->GetEntry(), boss->GetName(), engager);
}

void AliveWorldManager::OnBossKilled(Creature* boss, Player* killer)
{
    if (!m_enabled || !m_initialized || !boss)
        return;

    if (m_eventCollector)
    {
        m_eventCollector->RecordBossKilled(boss, killer);
    }

    // Trigger emote reactions from nearby bots
    if (m_emoteSystem)
    {
        uint64 killerGuid = killer ? killer->GetGUID().GetRawValue() : 0;
        m_emoteSystem->ReactToBossKill(boss->GetName(), boss->GetZoneId(), killerGuid);
    }

    // Stream to Node.js backend via realtime bridge
    sAliveWorldBridge->EmitBossKilled(boss->GetEntry(), boss->GetName(), killer);
}

void AliveWorldManager::OnRareSpawn(Creature* creature)
{
    if (!m_enabled || !m_initialized || !creature)
        return;

    if (m_eventCollector)
    {
        m_eventCollector->RecordRareSpawn(creature, creature->GetZoneId());
    }

    // Trigger emote reactions from nearby bots
    if (m_emoteSystem)
    {
        m_emoteSystem->ReactToRareSpawn(creature->GetName(), creature->GetZoneId());
    }
}

void AliveWorldManager::OnWorldEventStart(uint32 eventId, const std::string& eventName)
{
    if (!m_enabled || !m_initialized)
        return;

    if (m_eventCollector)
    {
        m_eventCollector->RecordWorldEvent(eventId, eventName, true);
    }

    // Stream to Node.js backend via realtime bridge
    sAliveWorldBridge->EmitWorldEventStart(eventId, eventName);

    ALIVE_LOG_INFO("World event started: %s (ID: %u)", eventName.c_str(), eventId);
}

void AliveWorldManager::OnWorldEventEnd(uint32 eventId, const std::string& eventName)
{
    if (!m_enabled || !m_initialized)
        return;

    if (m_eventCollector)
    {
        m_eventCollector->RecordWorldEvent(eventId, eventName, false);
    }

    // Stream to Node.js backend via realtime bridge
    sAliveWorldBridge->EmitWorldEventEnd(eventId, eventName);

    ALIVE_LOG_INFO("World event ended: %s (ID: %u)", eventName.c_str(), eventId);
}

void AliveWorldManager::OnCrimeCommitted(Unit* criminal, uint32 zoneId, uint8 wantedLevel)
{
    if (!m_enabled || !m_initialized || !criminal)
        return;

    ALIVE_LOG_DEBUG("Crime committed by %s in zone %u (wanted level %u)",
        criminal->GetName().c_str(), zoneId, wantedLevel);

    // Trigger emote reactions from nearby bots
    if (m_emoteSystem)
    {
        m_emoteSystem->ReactToCrime(criminal, zoneId, wantedLevel);
    }

    // Also record this as a world event for gossip
    if (m_eventCollector)
    {
        m_eventCollector->RecordGuardCalled(criminal, zoneId, wantedLevel);
    }

    // Make cautious bots flee from high-wanted criminals
    // Wanted level 3+ (3 stars) triggers flee for cautious bots within 30 yards
    if (m_behaviorExecutor && m_memoryManager && wantedLevel >= 3)
    {
#ifdef MOD_PLAYERBOTS
        const float FLEE_RADIUS = 30.0f;
        uint32 fleeTriggers = 0;

        // Find nearby bots who might flee
        auto allBots = sRandomPlayerbotMgr.GetPlayers();
        for (Player* bot : allBots)
        {
            if (!bot || !bot->IsInWorld())
                continue;

            // Check same map
            if (bot->GetMapId() != criminal->GetMapId())
                continue;

            // Check distance
            if (bot->GetDistance(criminal) > FLEE_RADIUS)
                continue;

            // Check if this is a playerbot
            PlayerbotAI* ai = sPlayerbotsMgr.GetPlayerbotAI(bot);
            if (!ai)
                continue;

            uint64 botGuid = bot->GetGUID().GetRawValue();

            // Get bot's personality - cautious bots flee (scale is 0.0-1.0)
            BotPersonality personality = m_memoryManager->GetPersonality(botGuid);
            bool isCautious = (personality.caution > 0.7f ||
                              personality.aggression < 0.3f);

            // Also check bot tier - Tier 1 bots have more personality-driven reactions
            BotTier tier = GetBotTier(bot->GetGUID().GetCounter());

            if (isCautious && tier != BotTier::TIER_3_PASSIVE)
            {
                // Trigger flee from the criminal
                m_behaviorExecutor->Flee(botGuid, criminal->GetGUID().GetRawValue());
                fleeTriggers++;

                ALIVE_LOG_DEBUG("Bot %s (caution: %.1f, aggression: %.1f) fleeing from criminal %s",
                    bot->GetName().c_str(),
                    personality.caution, personality.aggression,
                    criminal->GetName().c_str());
            }
        }

        if (fleeTriggers > 0)
        {
            ALIVE_LOG_INFO("Crime scene: %u cautious bots fleeing from %s (wanted level %u)",
                fleeTriggers, criminal->GetName().c_str(), wantedLevel);
        }
#endif
    }
}

// =========================================================================
// BOT AI INTEGRATION
// =========================================================================

bool AliveWorldManager::OnBotNeedsDecision(Player* bot, const std::string& situation)
{
    if (!m_enabled || !m_initialized || !bot || !m_aiEngine)
        return false;

    BotTier tier = GetBotTier(bot->GetGUID().GetCounter());

    if (tier == BotTier::TIER_3_PASSIVE)
        return false;  // No AI for passive bots

    uint64 botGuid = bot->GetGUID().GetRawValue();

    // Check if already processing
    if (m_memoryManager && m_memoryManager->IsAIProcessing(botGuid))
        return false;

    m_memoryManager->SetAIProcessing(botGuid, true);

    m_aiEngine->QueueCombatDecision(botGuid, situation,
        [this, botGuid](const std::string& response, bool success) {
            if (m_memoryManager)
                m_memoryManager->SetAIProcessing(botGuid, false);

            if (success && !response.empty())
            {
                ALIVE_LOG_DEBUG("AI decision for bot %llu: %s", botGuid, response.c_str());

                // Apply decision to bot behavior
                ApplyAIDecision(botGuid, response);
            }
        });

    return true;
}

bool AliveWorldManager::OnBotReceivedChat(Player* bot, Player* speaker, const std::string& message)
{
    if (!m_enabled || !m_initialized || !bot || !speaker || !m_aiEngine)
        return false;

    BotTier tier = GetBotTier(bot->GetGUID().GetCounter());

    // Only Tier 1 and 2 respond to chat
    if (tier == BotTier::TIER_3_PASSIVE)
        return false;

    uint64 botGuid = bot->GetGUID().GetRawValue();
    uint64 speakerGuid = speaker->GetGUID().GetRawValue();

    // Record interaction
    if (m_tierManager && !IsBot(speaker))
    {
        m_tierManager->OnPlayerSpoke(botGuid, speakerGuid);
    }

    // Record social interaction
    if (m_memoryManager)
    {
        m_memoryManager->RecordSocialInteraction(
            botGuid, speakerGuid, speaker->GetName(), "chat", 0.05f);
    }

    // Queue AI response
    // Capture GUID instead of raw pointer to avoid use-after-free
    const uint64 botGuidCapture = bot->GetGUID().GetRawValue();
    m_aiEngine->QueueChatResponse(botGuid, message,
        [botGuidCapture](const std::string& response, bool success) {
            if (success && !response.empty())
            {
                // Look up bot by GUID each time (bot may have been deleted)
                if (Player* botPtr = ObjectAccessor::FindPlayer(ObjectGuid(botGuidCapture)))
                {
                    if (botPtr->IsInWorld())
                    {
                        // Send response as /say
                        botPtr->Say(response, LANG_COMMON);
                    }
                }
            }
        });

    return true;
}

void AliveWorldManager::OnBotStrategyChange(Player* bot, const std::string& newStrategy)
{
    if (!m_enabled || !m_initialized || !bot)
        return;

    ALIVE_LOG_TRACE("Bot %s changed strategy to: %s", bot->GetName().c_str(), newStrategy.c_str());

    // Could trigger personality evolution based on strategy changes
}

void AliveWorldManager::ApplyAIDecision(uint64 botGuid, const std::string& decision)
{
#ifdef MOD_PLAYERBOTS
    // Look up the bot by GUID (bot may have been deleted since AI request was queued)
    Player* bot = ObjectAccessor::FindPlayer(ObjectGuid(botGuid));
    if (!bot || !bot->IsInWorld())
    {
        ALIVE_LOG_DEBUG("ApplyAIDecision: Bot %llu no longer in world", botGuid);
        return;
    }

    PlayerbotAI* ai = sPlayerbotsMgr.GetPlayerbotAI(bot);
    if (!ai)
    {
        ALIVE_LOG_DEBUG("ApplyAIDecision: Bot %llu has no PlayerbotAI", botGuid);
        return;
    }

    // Use StrategyParser to parse and execute the AI response
    // This handles structured responses with [STRATEGY], [CHAT], [EMOTE] blocks
    // and falls back to simple text parsing if no blocks found
    if (m_strategyParser)
    {
        uint32 behaviorsQueued = m_strategyParser->ParseAndExecute(botGuid, decision,
                                                                   BehaviorPriority::HIGH);

        ALIVE_LOG_DEBUG("ApplyAIDecision: Bot %s - StrategyParser queued %u behaviors",
                        bot->GetName().c_str(), behaviorsQueued);

        // If no behaviors were queued, fall back to legacy parsing
        if (behaviorsQueued == 0)
        {
            // Normalize the response (lowercase, trim whitespace)
            std::string action = decision;
            std::transform(action.begin(), action.end(), action.begin(), ::tolower);

            // Trim whitespace
            size_t start = action.find_first_not_of(" \t\n\r");
            size_t end = action.find_last_not_of(" \t\n\r");
            if (start != std::string::npos && end != std::string::npos)
                action = action.substr(start, end - start + 1);

            ALIVE_LOG_DEBUG("ApplyAIDecision: Bot %s fallback action=%s",
                            bot->GetName().c_str(), action.c_str());

            // Apply the appropriate action based on AI decision
            if (action == "attack" || action.find("attack") != std::string::npos)
            {
                ai->DoSpecificAction("melee", Event(), true);
                ALIVE_LOG_DEBUG("Bot %s: Executing ATTACK decision", bot->GetName().c_str());
            }
            else if (action == "defend" || action.find("defend") != std::string::npos)
            {
                ai->ChangeStrategy("flee", BotState::BOT_STATE_COMBAT);
                ALIVE_LOG_DEBUG("Bot %s: Executing DEFEND decision (flee strategy)",
                                bot->GetName().c_str());
            }
            else if (action == "flee" || action.find("flee") != std::string::npos ||
                     action == "run" || action.find("run away") != std::string::npos)
            {
                ai->DoSpecificAction("flee", Event(), true);
                ALIVE_LOG_DEBUG("Bot %s: Executing FLEE decision", bot->GetName().c_str());
            }
            else if (action == "call" || action.find("help") != std::string::npos ||
                     action.find("call") != std::string::npos)
            {
                std::string helpMsg = "Help! I'm under attack!";
                bot->Yell(helpMsg, LANG_COMMON);
                ALIVE_LOG_DEBUG("Bot %s: Executing CALL FOR HELP decision",
                                bot->GetName().c_str());
            }
            else
            {
                ALIVE_LOG_WARN("ApplyAIDecision: Unknown action '%s' for bot %s",
                               action.c_str(), bot->GetName().c_str());
            }
        }
    }
    else
    {
        ALIVE_LOG_WARN("ApplyAIDecision: StrategyParser not available for bot %s",
                       bot->GetName().c_str());
    }

    // Record the decision in memory for future context
    if (m_memoryManager)
    {
        m_memoryManager->RecordAIDecision(botGuid, decision);
    }
#else
    ALIVE_LOG_DEBUG("ApplyAIDecision: MOD_PLAYERBOTS not available");
#endif
}

// =========================================================================
// DEBUG & STATS
// =========================================================================

void AliveWorldManager::SetDebugMode(bool enabled)
{
    m_debugEnabled = enabled;
    Logger::SetDebugEnabled(enabled);
}

void AliveWorldManager::SetDebugLogLevel(uint8 level)
{
    m_debugLogLevel = level;
    Logger::SetLogLevel(level);
}

uint64 AliveWorldManager::GetUptimeSeconds() const
{
    if (m_startTime == 0)
        return 0;
    return (NowMs() - m_startTime) / 1000;
}

APIStats AliveWorldManager::GetStats() const
{
    APIStats stats = {};

    if (m_memoryManager)
    {
        stats.totalBots = m_memoryManager->GetTotalBots();
        stats.tier1Count = m_memoryManager->GetTierCount(BotTier::TIER_1_ACTIVE);
        stats.tier2Count = m_memoryManager->GetTierCount(BotTier::TIER_2_AWARE);
        stats.tier3Count = m_memoryManager->GetTierCount(BotTier::TIER_3_PASSIVE);
    }

    if (m_worldStateManager)
    {
        APIStats worldStats = m_worldStateManager->GetStats();
        stats.activeZones = worldStats.activeZones;
        stats.gossipPoolSize = worldStats.gossipPoolSize;
        stats.eventBufferSize = worldStats.eventBufferSize;
    }

    if (m_aiEngine)
    {
        stats.pendingAIRequests = m_aiEngine->GetPendingRequestCount();
        stats.cacheHitRate = static_cast<uint32>(m_aiEngine->GetCacheHitRate());
    }

    stats.uptimeSeconds = GetUptimeSeconds();

    return stats;
}

void AliveWorldManager::DumpState()
{
    APIStats stats = GetStats();

    ALIVE_LOG_INFO("========== ALIVE WORLD STATE DUMP ==========");
    ALIVE_LOG_INFO("Uptime: %llu seconds", stats.uptimeSeconds);
    ALIVE_LOG_INFO("Total Bots: %u", stats.totalBots);
    ALIVE_LOG_INFO("  Tier 1 (Active): %u", stats.tier1Count);
    ALIVE_LOG_INFO("  Tier 2 (Aware): %u", stats.tier2Count);
    ALIVE_LOG_INFO("  Tier 3 (Passive): %u", stats.tier3Count);
    ALIVE_LOG_INFO("Active Zones: %u", stats.activeZones);
    ALIVE_LOG_INFO("Gossip Pool: %u items", stats.gossipPoolSize);
    ALIVE_LOG_INFO("Event Buffer: %u events", stats.eventBufferSize);
    ALIVE_LOG_INFO("Pending AI Requests: %u", stats.pendingAIRequests);
    ALIVE_LOG_INFO("Cache Hit Rate: %u%%", stats.cacheHitRate);
    ALIVE_LOG_INFO("=============================================");
}

// ============================================================================
// WEATHER AND TIME EVENTS
// ============================================================================

void AliveWorldManager::OnWeatherChange(uint32 zoneId, uint8 weatherType, uint32 weatherState, float grade)
{
    if (!m_enabled || !m_worldStateManager)
        return;

    // Update the world state manager
    m_worldStateManager->UpdateZoneWeather(zoneId, weatherType, grade);

    // Record the event
    if (m_eventCollector)
    {
        WorldEvent event;
        event.type = WorldEventType::WEATHER_CHANGE;
        event.zoneId = zoneId;
        event.timestamp = NowMs();
        event.importance = (grade > 0.5f) ? 5 : 3;  // More intense = more important

        ZoneWeatherState weather = m_worldStateManager->GetZoneWeather(zoneId);
        event.details = std::string(WeatherIntensityToString(weather.intensity)) + " " +
                        std::string(WeatherCategoryToString(weather.category));

        // Use RecordCustomEvent for weather changes
        m_eventCollector->RecordCustomEvent(
            WorldEventType::WEATHER_CHANGE,
            ObjectGuid::Empty,  // No primary entity
            ObjectGuid::Empty,  // No secondary entity
            event.details,
            event.importance,
            zoneId,
            0,  // areaId
            0   // mapId
        );
    }

    // Log significant weather changes
    if (grade > 0.3f)
    {
        ZoneWeatherState weather = m_worldStateManager->GetZoneWeather(zoneId);
        ALIVE_LOG_DEBUG("Weather changed in zone %u: %s %s",
                       zoneId,
                       WeatherIntensityToString(weather.intensity),
                       WeatherCategoryToString(weather.category));
    }
}

void AliveWorldManager::CheckTimeTransition()
{
    if (!m_enabled || !m_worldStateManager)
        return;

    bool transitioned = m_worldStateManager->CheckTimeTransition();

    if (transitioned)
    {
        const TimeState& time = m_worldStateManager->GetTimeState();

        // Record the event
        if (m_eventCollector)
        {
            std::string details = std::string("Time: ") + TimeTransitionToString(time.currentPeriod) +
                                  " (Hour " + std::to_string(time.currentHour) + ", " +
                                  SeasonToString(time.currentSeason) + ")";

            // Use RecordCustomEvent for time transitions
            m_eventCollector->RecordCustomEvent(
                WorldEventType::WEATHER_CHANGE,  // Reuse for time (or add TIME_CHANGE)
                ObjectGuid::Empty,
                ObjectGuid::Empty,
                details,
                2,  // importance
                0,  // zoneId (global event)
                0,  // areaId
                0   // mapId
            );
        }

        ALIVE_LOG_DEBUG("Time transition: %s (hour %u, %s)",
                       TimeTransitionToString(time.currentPeriod),
                       time.currentHour,
                       SeasonToString(time.currentSeason));
    }
}

std::string AliveWorldManager::GetWeatherGossip(uint32 zoneId) const
{
    if (!m_worldStateManager)
        return "";

    ZoneWeatherState weather = m_worldStateManager->GetZoneWeather(zoneId);

    // Weather gossip templates based on conditions
    static const std::vector<std::string> rainTemplates = {
        "Ugh, this rain is getting into my armor.",
        "At least the crops will be happy with all this rain.",
        "I hope this weather lets up soon.",
        "Perfect weather for staying indoors.",
        "My boots are soaked through!"
    };

    static const std::vector<std::string> heavyRainTemplates = {
        "This storm is fierce! We should find shelter.",
        "I can barely see through this downpour!",
        "The rain is coming down in sheets!",
        "My gear is completely waterlogged."
    };

    static const std::vector<std::string> snowTemplates = {
        "Brr, it's freezing out here!",
        "Snow again? Time to find a fire.",
        "Beautiful snowfall, isn't it?",
        "I should have brought warmer clothes.",
        "The snow is really coming down now."
    };

    static const std::vector<std::string> stormTemplates = {
        "This sandstorm is brutal!",
        "I can't see a thing in this storm!",
        "We need to take cover!",
        "The wind is howling!"
    };

    static const std::vector<std::string> thunderTemplates = {
        "That thunder is deafening!",
        "Lightning! We should get inside!",
        "The storm is right overhead!",
        "I've never seen lightning this close!"
    };

    static const std::vector<std::string> clearTemplates = {
        "What a beautiful day!",
        "Perfect weather for adventuring.",
        "Not a cloud in the sky.",
        "Couldn't ask for better weather."
    };

    // Select appropriate template
    const std::vector<std::string>* templates = &clearTemplates;

    if (weather.category == WeatherCategory::RAIN)
    {
        templates = (weather.intensity == WeatherIntensity::HEAVY) ? &heavyRainTemplates : &rainTemplates;
    }
    else if (weather.category == WeatherCategory::SNOW)
    {
        templates = &snowTemplates;
    }
    else if (weather.category == WeatherCategory::STORM)
    {
        templates = &stormTemplates;
    }
    else if (weather.category == WeatherCategory::THUNDERS)
    {
        templates = &thunderTemplates;
    }

    if (templates->empty())
        return "";

    // Random selection
    std::random_device rd;
    std::mt19937 rng(rd());
    std::uniform_int_distribution<size_t> dist(0, templates->size() - 1);

    return (*templates)[dist(rng)];
}

std::string AliveWorldManager::GetTimeGossip() const
{
    if (!m_worldStateManager)
        return "";

    const TimeState& time = m_worldStateManager->GetTimeState();

    static const std::vector<std::string> dawnTemplates = {
        "Another day begins...",
        "Good morning! Ready for adventure?",
        "The sun is rising. Time to get moving.",
        "Dawn breaks. A new day of opportunity.",
        "Early bird gets the worm, they say."
    };

    static const std::vector<std::string> morningTemplates = {
        "It's still early. The day is young.",
        "Morning activities are in full swing.",
        "A fine morning for questing."
    };

    static const std::vector<std::string> afternoonTemplates = {
        "The sun is high overhead.",
        "Halfway through the day already.",
        "Afternoon heat is setting in."
    };

    static const std::vector<std::string> duskTemplates = {
        "The sun is setting. Should we make camp?",
        "Beautiful sunset tonight.",
        "Dusk falls. The creatures of the night stir.",
        "We should find shelter before dark."
    };

    static const std::vector<std::string> nightTemplates = {
        "Stay alert, it's dangerous at night.",
        "The stars are beautiful tonight.",
        "The darkness hides many dangers.",
        "Night watch isn't my favorite duty.",
        "Can't see much in this darkness."
    };

    static const std::vector<std::string> midnightTemplates = {
        "The witching hour approaches.",
        "Midnight. The darkest hour.",
        "All sensible folk are asleep by now."
    };

    const std::vector<std::string>* templates = &afternoonTemplates;

    switch (time.currentPeriod)
    {
        case TimeTransitionType::DAWN:
            templates = &dawnTemplates;
            break;
        case TimeTransitionType::MORNING:
            templates = &morningTemplates;
            break;
        case TimeTransitionType::DUSK:
            templates = &duskTemplates;
            break;
        case TimeTransitionType::EVENING:
        case TimeTransitionType::NIGHT:
            templates = &nightTemplates;
            break;
        case TimeTransitionType::MIDNIGHT:
            templates = &midnightTemplates;
            break;
        default:
            templates = &afternoonTemplates;
            break;
    }

    if (templates->empty())
        return "";

    std::random_device rd;
    std::mt19937 rng(rd());
    std::uniform_int_distribution<size_t> dist(0, templates->size() - 1);

    return (*templates)[dist(rng)];
}

std::string AliveWorldManager::GetEnvironmentGossip(uint32 zoneId) const
{
    // Combine weather and time for context-aware gossip
    std::string weatherGossip = GetWeatherGossip(zoneId);
    std::string timeGossip = GetTimeGossip();

    // 70% weather, 30% time
    std::random_device rd;
    std::mt19937 rng(rd());
    std::uniform_int_distribution<int> dist(1, 100);

    if (!weatherGossip.empty() && dist(rng) <= 70)
    {
        return weatherGossip;
    }
    else if (!timeGossip.empty())
    {
        return timeGossip;
    }

    return weatherGossip;  // Fallback to weather if time is empty
}

// ============================================================================
// PERSONALITY-DRIVEN GOSSIP
// ============================================================================

std::string AliveWorldManager::GetWeatherGossip(uint32 zoneId, const BotPersonality& personality) const
{
    if (!m_worldStateManager)
        return "";

    ZoneWeatherState weather = m_worldStateManager->GetZoneWeather(zoneId);

    // Personality-specific weather templates
    // High Caution (>0.6): Safety-focused, worried
    static const std::vector<std::string> cautiousRainTemplates = {
        "We should find shelter before this gets worse.",
        "This weather makes travel dangerous. Be careful.",
        "I don't like this. Visibility is poor.",
        "We could slip on these wet rocks. Watch your step.",
        "The rain could attract unwanted attention. Stay alert."
    };

    static const std::vector<std::string> cautiousStormTemplates = {
        "This storm is dangerous! We need to take cover immediately!",
        "Lightning could strike at any moment. Find shelter!",
        "I've seen storms like this kill travelers. Be very careful.",
        "We should wait this out somewhere safe."
    };

    static const std::vector<std::string> cautiousSnowTemplates = {
        "This cold is dangerous. We need to find warmth soon.",
        "Frostbite sets in quickly in weather like this.",
        "The snow could hide treacherous terrain. Go slowly.",
        "We should make camp before the blizzard worsens."
    };

    // High Sociability (>0.6): Conversational, engaging
    static const std::vector<std::string> socialRainTemplates = {
        "Nothing like a good rain to bring folks together indoors, eh?",
        "This weather reminds me of home. Anyone else?",
        "Say, do you think the rain will let up soon?",
        "At least we can share stories while waiting out the storm!",
        "Rain like this calls for good company and warm drinks."
    };

    static const std::vector<std::string> socialStormTemplates = {
        "Quite a storm! Everyone doing alright?",
        "This is exciting! Have you ever seen lightning this close?",
        "We should huddle together - safety in numbers!",
        "Anyone have good storm stories to share?"
    };

    static const std::vector<std::string> socialSnowTemplates = {
        "Beautiful, isn't it? Anyone want to build a snowman later?",
        "This snow reminds me of Winter Veil celebrations!",
        "Nothing like a snowfall to bring people together.",
        "We should make snowballs! For... defensive purposes, of course."
    };

    // High Curiosity (>0.6): Questioning, wondering
    static const std::vector<std::string> curiousRainTemplates = {
        "I wonder what causes rain in this zone specifically...",
        "Have you noticed the rain patterns change with the seasons?",
        "Fascinating how the wildlife behaves differently in rain.",
        "I wonder if there's magic involved in these weather patterns."
    };

    static const std::vector<std::string> curiousStormTemplates = {
        "The energy in these storms is incredible! What causes it?",
        "I've read that some storms are summoned by shamans...",
        "Do you think the Titans had something to do with weather?",
        "I wonder if we could predict when storms will hit."
    };

    static const std::vector<std::string> curiousSnowTemplates = {
        "Each snowflake is supposedly unique. Imagine that!",
        "I wonder how the wildlife survives in this cold.",
        "The way snow accumulates is fascinating to observe.",
        "Do you know why some zones get snow and others don't?"
    };

    // High Aggression (>0.6): Frustrated, complaining
    static const std::vector<std::string> aggressiveRainTemplates = {
        "This blasted rain is ruining my armor!",
        "Of all the times for rain... just my luck.",
        "Rain won't stop me. Nothing stops me.",
        "I hate fighting in the rain. Everything gets slippery.",
        "This weather is making me irritable. Let's move."
    };

    static const std::vector<std::string> aggressiveStormTemplates = {
        "Damn this storm! I have things to kill!",
        "Even the weather is against us today.",
        "I'll fight through this storm if I have to!",
        "Lightning doesn't scare me. Let's keep moving!"
    };

    static const std::vector<std::string> aggressiveSnowTemplates = {
        "This cold makes my joints ache. I hate it.",
        "Snow slows everything down. So annoying!",
        "The cold just makes me want to hit something.",
        "I'd rather be in battle than freezing out here."
    };

    // High Helpfulness (>0.6): Offering advice, supportive
    static const std::vector<std::string> helpfulRainTemplates = {
        "If anyone needs a dry cloak, I have a spare.",
        "There's an inn not far from here if anyone needs shelter.",
        "Remember to dry your weapons later - rust is the enemy.",
        "Watch the puddles, they can hide holes in the road."
    };

    static const std::vector<std::string> helpfulStormTemplates = {
        "Everyone stay close! We'll get through this together.",
        "If anyone's afraid of storms, just stay near the group.",
        "Don't stand near trees or metal in this lightning!",
        "I know a cave nearby if anyone needs shelter."
    };

    static const std::vector<std::string> helpfulSnowTemplates = {
        "Everyone keep moving - standing still in cold is dangerous.",
        "I can start a fire if anyone needs to warm up.",
        "Rub your hands together to keep the blood flowing.",
        "There's a village nearby with warm beds if anyone needs rest."
    };

    // Clear weather templates by personality
    static const std::vector<std::string> cautiousClearTemplates = {
        "Good weather, but stay alert. Danger doesn't rest.",
        "Clear skies mean we can spot threats from far away.",
        "Perfect conditions for travel. Let's make good time."
    };

    static const std::vector<std::string> socialClearTemplates = {
        "What a glorious day! Perfect for making new friends!",
        "Weather like this puts everyone in a good mood, doesn't it?",
        "Days like this are meant for adventure together!"
    };

    static const std::vector<std::string> curiousClearTemplates = {
        "Look at those cloud formations! What could cause that?",
        "I wonder what we'll discover on a fine day like this.",
        "Clear skies reveal so much of the world to explore."
    };

    static const std::vector<std::string> aggressiveClearTemplates = {
        "Good. Nothing to slow us down. Let's find some action.",
        "Clear weather means clear sightlines for hunting.",
        "Perfect day for a fight. Who's feeling brave?"
    };

    static const std::vector<std::string> helpfulClearTemplates = {
        "Beautiful day! Everyone in good spirits?",
        "Weather's cooperating - let me know if you need anything.",
        "Good conditions for travel. Happy to help anyone along the way."
    };

    // Determine dominant personality trait
    float maxTrait = 0.5f;  // Threshold for personality influence
    const std::vector<std::string>* templates = nullptr;

    // Check which trait is dominant (>0.6 and highest)
    bool isCautious = personality.caution > 0.6f;
    bool isSocial = personality.sociability > 0.6f;
    bool isCurious = personality.curiosity > 0.6f;
    bool isAggressive = personality.aggression > 0.6f;
    bool isHelpful = personality.helpfulness > 0.6f;

    // Find the dominant trait
    float dominantValue = 0.6f;
    int dominantTrait = -1;  // -1 = none, 0=caution, 1=social, 2=curious, 3=aggressive, 4=helpful

    if (isCautious && personality.caution > dominantValue)
    {
        dominantValue = personality.caution;
        dominantTrait = 0;
    }
    if (isSocial && personality.sociability > dominantValue)
    {
        dominantValue = personality.sociability;
        dominantTrait = 1;
    }
    if (isCurious && personality.curiosity > dominantValue)
    {
        dominantValue = personality.curiosity;
        dominantTrait = 2;
    }
    if (isAggressive && personality.aggression > dominantValue)
    {
        dominantValue = personality.aggression;
        dominantTrait = 3;
    }
    if (isHelpful && personality.helpfulness > dominantValue)
    {
        dominantValue = personality.helpfulness;
        dominantTrait = 4;
    }

    // Select templates based on weather and personality
    if (weather.category == WeatherCategory::RAIN || weather.category == WeatherCategory::BLACKRAIN)
    {
        switch (dominantTrait)
        {
            case 0: templates = &cautiousRainTemplates; break;
            case 1: templates = &socialRainTemplates; break;
            case 2: templates = &curiousRainTemplates; break;
            case 3: templates = &aggressiveRainTemplates; break;
            case 4: templates = &helpfulRainTemplates; break;
            default: return GetWeatherGossip(zoneId);  // Fall back to generic
        }
    }
    else if (weather.category == WeatherCategory::STORM || weather.category == WeatherCategory::THUNDERS)
    {
        switch (dominantTrait)
        {
            case 0: templates = &cautiousStormTemplates; break;
            case 1: templates = &socialStormTemplates; break;
            case 2: templates = &curiousStormTemplates; break;
            case 3: templates = &aggressiveStormTemplates; break;
            case 4: templates = &helpfulStormTemplates; break;
            default: return GetWeatherGossip(zoneId);
        }
    }
    else if (weather.category == WeatherCategory::SNOW)
    {
        switch (dominantTrait)
        {
            case 0: templates = &cautiousSnowTemplates; break;
            case 1: templates = &socialSnowTemplates; break;
            case 2: templates = &curiousSnowTemplates; break;
            case 3: templates = &aggressiveSnowTemplates; break;
            case 4: templates = &helpfulSnowTemplates; break;
            default: return GetWeatherGossip(zoneId);
        }
    }
    else  // Clear/Fine weather
    {
        switch (dominantTrait)
        {
            case 0: templates = &cautiousClearTemplates; break;
            case 1: templates = &socialClearTemplates; break;
            case 2: templates = &curiousClearTemplates; break;
            case 3: templates = &aggressiveClearTemplates; break;
            case 4: templates = &helpfulClearTemplates; break;
            default: return GetWeatherGossip(zoneId);
        }
    }

    if (!templates || templates->empty())
        return GetWeatherGossip(zoneId);

    // Random selection from personality-appropriate templates
    std::random_device rd;
    std::mt19937 rng(rd());
    std::uniform_int_distribution<size_t> dist(0, templates->size() - 1);

    return (*templates)[dist(rng)];
}

std::string AliveWorldManager::GetTimeGossip(const BotPersonality& personality) const
{
    if (!m_worldStateManager)
        return "";

    const TimeState& time = m_worldStateManager->GetTimeState();

    // Personality-specific time templates

    // Cautious personality - time awareness
    static const std::vector<std::string> cautiousDawnTemplates = {
        "Dawn is a vulnerable time. Stay alert.",
        "Predators often hunt at first light. Be careful.",
        "Good, it's getting lighter. Easier to spot danger."
    };

    static const std::vector<std::string> cautiousDuskTemplates = {
        "We should find shelter before full dark.",
        "Dusk brings out dangerous creatures. Stay close.",
        "I don't like traveling at twilight. Too many ambush spots."
    };

    static const std::vector<std::string> cautiousNightTemplates = {
        "Night travel is risky. We should make camp.",
        "Stay close to the fire. The darkness hides threats.",
        "Keep your weapons ready. Night is when they hunt."
    };

    // Social personality - time conversations
    static const std::vector<std::string> socialDawnTemplates = {
        "Good morning, everyone! Ready for a new day of adventure?",
        "Rise and shine! Who wants first watch for breakfast?",
        "Dawn together means another day of friendship ahead!"
    };

    static const std::vector<std::string> socialDuskTemplates = {
        "What a day it's been! Anyone up for sharing stories tonight?",
        "Sunset gatherings are my favorite. Who has tales to tell?",
        "Let's celebrate another day survived together!"
    };

    static const std::vector<std::string> socialNightTemplates = {
        "Nothing like good company on a dark night.",
        "Who wants to take turns on watch? I'll go first!",
        "The stars are beautiful. Anyone know the constellations?"
    };

    // Curious personality - time observations
    static const std::vector<std::string> curiousDawnTemplates = {
        "I wonder why the sky turns pink before sunrise...",
        "Have you noticed how the birds change their songs at dawn?",
        "Fascinating how quickly the light changes at this hour."
    };

    static const std::vector<std::string> curiousDuskTemplates = {
        "The way the light fades is mesmerizing. What causes the colors?",
        "I've always wondered why shadows grow so long at dusk.",
        "Do you think the moons affect the twilight colors?"
    };

    static const std::vector<std::string> curiousNightTemplates = {
        "Look at those stars! I wonder how far away they are.",
        "The night sky tells so many stories. Each star has a history.",
        "I wonder what creatures only come out at this hour."
    };

    // Aggressive personality - time frustration
    static const std::vector<std::string> aggressiveDawnTemplates = {
        "Finally, enough light to find something to fight.",
        "Dawn means the cowards can't hide in darkness anymore.",
        "Let's move out. The early warrior catches the enemy."
    };

    static const std::vector<std::string> aggressiveDuskTemplates = {
        "Darkness coming. Perfect for ambushes... ours or theirs.",
        "I hate when the light fades. Harder to spot enemies.",
        "One more hour of good fighting light. Let's use it."
    };

    static const std::vector<std::string> aggressiveNightTemplates = {
        "Night doesn't scare me. I'll fight in any darkness.",
        "The shadows make hunting more... interesting.",
        "Let them think the night protects them. It won't."
    };

    // Helpful personality - time advice
    static const std::vector<std::string> helpfulDawnTemplates = {
        "Morning, everyone! Let me help get the camp packed up.",
        "Early start today - I've got breakfast ready if anyone's hungry.",
        "Dawn's the best time for travel. Everyone well-rested?"
    };

    static const std::vector<std::string> helpfulDuskTemplates = {
        "We should start setting up camp. I'll gather firewood.",
        "Anyone hurt today? Now's a good time to tend wounds.",
        "I know a good spot to camp nearby. Follow me!"
    };

    static const std::vector<std::string> helpfulNightTemplates = {
        "Everyone get some rest. I'll take first watch.",
        "The fire should keep us warm. Let me add more wood.",
        "Need anything before you sleep? I'm here to help."
    };

    // Determine dominant personality trait
    bool isCautious = personality.caution > 0.6f;
    bool isSocial = personality.sociability > 0.6f;
    bool isCurious = personality.curiosity > 0.6f;
    bool isAggressive = personality.aggression > 0.6f;
    bool isHelpful = personality.helpfulness > 0.6f;

    float dominantValue = 0.6f;
    int dominantTrait = -1;

    if (isCautious && personality.caution > dominantValue)
    {
        dominantValue = personality.caution;
        dominantTrait = 0;
    }
    if (isSocial && personality.sociability > dominantValue)
    {
        dominantValue = personality.sociability;
        dominantTrait = 1;
    }
    if (isCurious && personality.curiosity > dominantValue)
    {
        dominantValue = personality.curiosity;
        dominantTrait = 2;
    }
    if (isAggressive && personality.aggression > dominantValue)
    {
        dominantValue = personality.aggression;
        dominantTrait = 3;
    }
    if (isHelpful && personality.helpfulness > dominantValue)
    {
        dominantValue = personality.helpfulness;
        dominantTrait = 4;
    }

    // No dominant trait - fall back to generic
    if (dominantTrait == -1)
        return GetTimeGossip();

    const std::vector<std::string>* templates = nullptr;

    // Select based on time period and personality
    bool isDawnOrMorning = (time.currentPeriod == TimeTransitionType::DAWN ||
                           time.currentPeriod == TimeTransitionType::MORNING);
    bool isDuskOrEvening = (time.currentPeriod == TimeTransitionType::DUSK ||
                           time.currentPeriod == TimeTransitionType::EVENING);
    bool isNight = (time.currentPeriod == TimeTransitionType::NIGHT ||
                   time.currentPeriod == TimeTransitionType::MIDNIGHT);

    if (isDawnOrMorning)
    {
        switch (dominantTrait)
        {
            case 0: templates = &cautiousDawnTemplates; break;
            case 1: templates = &socialDawnTemplates; break;
            case 2: templates = &curiousDawnTemplates; break;
            case 3: templates = &aggressiveDawnTemplates; break;
            case 4: templates = &helpfulDawnTemplates; break;
        }
    }
    else if (isDuskOrEvening)
    {
        switch (dominantTrait)
        {
            case 0: templates = &cautiousDuskTemplates; break;
            case 1: templates = &socialDuskTemplates; break;
            case 2: templates = &curiousDuskTemplates; break;
            case 3: templates = &aggressiveDuskTemplates; break;
            case 4: templates = &helpfulDuskTemplates; break;
        }
    }
    else if (isNight)
    {
        switch (dominantTrait)
        {
            case 0: templates = &cautiousNightTemplates; break;
            case 1: templates = &socialNightTemplates; break;
            case 2: templates = &curiousNightTemplates; break;
            case 3: templates = &aggressiveNightTemplates; break;
            case 4: templates = &helpfulNightTemplates; break;
        }
    }
    else
    {
        // Afternoon/Noon - fall back to generic
        return GetTimeGossip();
    }

    if (!templates || templates->empty())
        return GetTimeGossip();

    std::random_device rd;
    std::mt19937 rng(rd());
    std::uniform_int_distribution<size_t> dist(0, templates->size() - 1);

    return (*templates)[dist(rng)];
}

std::string AliveWorldManager::GetEnvironmentGossip(uint32 zoneId, const BotPersonality& personality) const
{
    // Combine weather and time for personality-aware context gossip
    std::string weatherGossip = GetWeatherGossip(zoneId, personality);
    std::string timeGossip = GetTimeGossip(personality);

    // 70% weather, 30% time (same as non-personality version)
    std::random_device rd;
    std::mt19937 rng(rd());
    std::uniform_int_distribution<int> dist(1, 100);

    if (!weatherGossip.empty() && dist(rng) <= 70)
    {
        return weatherGossip;
    }
    else if (!timeGossip.empty())
    {
        return timeGossip;
    }

    return weatherGossip;
}

// =============================================================================
// FALLBACK BEHAVIOR SYSTEM
// =============================================================================
// When the AI daemon is disconnected or unavailable, bots need to continue
// operating with basic, deterministic behaviors. This ensures the world stays
// alive even without LLM-powered decision making.
//
// Fallback behaviors are personality-driven and context-aware:
// - Aggressive bots attack when threatened
// - Cautious bots flee when in danger
// - Social bots try to find groups
// - Helpful bots assist nearby allies
// =============================================================================

void AliveWorldManager::ExecuteFallbackBehavior(Player* bot, const std::string& situation)
{
    if (!bot || !m_behaviorExecutor || !m_memoryManager)
    {
        ALIVE_LOG_WARN("ExecuteFallbackBehavior: Missing bot or subsystems");
        return;
    }

#ifdef MOD_PLAYERBOTS
    PlayerbotAI* ai = sPlayerbotsMgr.GetPlayerbotAI(bot);
    if (!ai)
    {
        ALIVE_LOG_DEBUG("ExecuteFallbackBehavior: Bot %s has no PlayerbotAI", bot->GetName().c_str());
        return;
    }

    uint64 botGuid = bot->GetGUID().GetRawValue();

    // Get bot's personality for behavior selection
    BotMemoryData* memory = m_memoryManager->GetBotMemory(botGuid);
    BotPersonality personality;
    if (memory)
    {
        personality = memory->personality;
    }

    // Normalize situation string for pattern matching
    std::string sitLower = situation;
    std::transform(sitLower.begin(), sitLower.end(), sitLower.begin(), ::tolower);

    // =======================================================================
    // COMBAT SITUATIONS
    // =======================================================================
    if (sitLower.find("combat") != std::string::npos ||
        sitLower.find("attack") != std::string::npos ||
        sitLower.find("fight") != std::string::npos)
    {
        // Check health status
        float healthPct = bot->GetHealthPct();
        bool lowHealth = healthPct < 30.0f;
        bool criticalHealth = healthPct < 15.0f;

        // Critical health - even aggressive bots should consider fleeing
        if (criticalHealth)
        {
            if (personality.aggression < 0.8f || personality.caution > 0.4f)
            {
                ALIVE_LOG_DEBUG("Fallback: Bot %s fleeing (critical health %.1f%%)",
                               bot->GetName().c_str(), healthPct);
                m_behaviorExecutor->Flee(botGuid, 0);  // Flee from current target
                return;
            }
        }

        // Low health behavior
        if (lowHealth)
        {
            // Cautious bots flee
            if (personality.caution > 0.6f)
            {
                ALIVE_LOG_DEBUG("Fallback: Bot %s (cautious) fleeing at %.1f%% health",
                               bot->GetName().c_str(), healthPct);
                m_behaviorExecutor->Flee(botGuid, 0);
                return;
            }

            // Helpful bots call for help
            if (personality.helpfulness > 0.6f)
            {
                ALIVE_LOG_DEBUG("Fallback: Bot %s calling for help at %.1f%% health",
                               bot->GetName().c_str(), healthPct);
                m_behaviorExecutor->CallForHelp(botGuid);

                // Also try to use healing abilities
                ai->DoSpecificAction("heal", Event(), true);
                return;
            }
        }

        // Standard combat - aggressive bots attack, others defend
        if (personality.aggression > 0.6f)
        {
            ALIVE_LOG_DEBUG("Fallback: Bot %s (aggressive) attacking",
                           bot->GetName().c_str());
            ai->DoSpecificAction("attack", Event(), true);
        }
        else
        {
            // Defensive stance - focus on survival
            ALIVE_LOG_DEBUG("Fallback: Bot %s defending",
                           bot->GetName().c_str());
            ai->DoSpecificAction("stay", Event(), true);
        }
        return;
    }

    // =======================================================================
    // SOCIAL SITUATIONS
    // =======================================================================
    if (sitLower.find("social") != std::string::npos ||
        sitLower.find("player") != std::string::npos ||
        sitLower.find("nearby") != std::string::npos)
    {
        // Social bots try to interact
        if (personality.sociability > 0.6f)
        {
            // Random chance to emote or say something
            std::random_device rd;
            std::mt19937 rng(rd());
            std::uniform_int_distribution<int> actionDist(1, 3);

            switch (actionDist(rng))
            {
                case 1:
                {
                    // Wave emote
                    m_behaviorExecutor->Emote(botGuid, "/wave", BehaviorPriority::LOW);
                    break;
                }
                case 2:
                {
                    // Say a greeting
                    static const std::vector<std::string> greetings = {
                        "Hello!", "Greetings!", "Well met!", "Hey there!",
                        "Good day!", "How's it going?", "Nice to meet you!"
                    };
                    std::uniform_int_distribution<size_t> greetDist(0, greetings.size() - 1);
                    m_behaviorExecutor->Say(botGuid, greetings[greetDist(rng)], BehaviorPriority::LOW);
                    break;
                }
                case 3:
                {
                    // Try to follow the player
                    // Find nearest player that isn't a bot
                    Player* nearestPlayer = nullptr;
                    float nearestDist = 100.0f;

                    Map* map = bot->GetMap();
                    if (map)
                    {
                        for (auto& ref : map->GetPlayers())
                        {
                            Player* player = ref.GetSource();
                            if (player && player != bot && !IsBot(player))
                            {
                                float dist = bot->GetDistance(player);
                                if (dist < nearestDist)
                                {
                                    nearestDist = dist;
                                    nearestPlayer = player;
                                }
                            }
                        }
                    }

                    if (nearestPlayer && nearestDist < 50.0f)
                    {
                        m_behaviorExecutor->Follow(botGuid, nearestPlayer->GetGUID().GetRawValue());
                    }
                    break;
                }
            }
            return;
        }

        // Non-social bots just stay put
        ALIVE_LOG_DEBUG("Fallback: Bot %s (non-social) staying put",
                       bot->GetName().c_str());
        return;
    }

    // =======================================================================
    // IDLE / DEFAULT SITUATIONS
    // =======================================================================

    // Curious bots explore
    if (personality.curiosity > 0.6f)
    {
        // Random patrol behavior
        ALIVE_LOG_DEBUG("Fallback: Bot %s (curious) patrolling",
                       bot->GetName().c_str());

        // Use playerbots patrol action
        ai->DoSpecificAction("follow master", Event(), true);
        return;
    }

    // Greedy bots look for loot/gathering
    if (personality.greed > 0.6f)
    {
        ALIVE_LOG_DEBUG("Fallback: Bot %s (greedy) looking for loot",
                       bot->GetName().c_str());

        // Try to find and loot nearby
        ai->DoSpecificAction("add all loot", Event(), true);
        return;
    }

    // Default: just stay and wait
    ALIVE_LOG_DEBUG("Fallback: Bot %s defaulting to idle",
                   bot->GetName().c_str());
    ai->DoSpecificAction("stay", Event(), true);

#else
    ALIVE_LOG_DEBUG("ExecuteFallbackBehavior: MOD_PLAYERBOTS not available");
#endif
}

// =============================================================================
// SOCKET COMMAND HANDLER
// =============================================================================
// Processes commands received from external systems (console, daemon, bridge).
// Commands are translated to BehaviorExecutor actions for execution.
//
// This provides remote control capabilities for bots, enabling:
// - Console-based testing and debugging
// - External AI daemon integration
// - Dashboard control panel
// =============================================================================

void AliveWorldManager::HandleSocketCommand(const SocketCommand& cmd)
{
    ALIVE_LOG_DEBUG("HandleSocketCommand: type=%s botGuid=%llu",
                   SocketCommandTypeToString(cmd.type), cmd.botGuid);

    if (!m_behaviorExecutor)
    {
        ALIVE_LOG_WARN("HandleSocketCommand: BehaviorExecutor not available");
        return;
    }

#ifdef MOD_PLAYERBOTS
    // Validate bot exists
    Player* bot = ObjectAccessor::FindPlayer(ObjectGuid(cmd.botGuid));
    if (!bot && cmd.botGuid != 0)
    {
        // Skip validation for system commands that don't target a bot
        if (cmd.type != SocketCommandType::REQUEST_STATE &&
            cmd.type != SocketCommandType::SET_DEBUG &&
            cmd.type != SocketCommandType::PING)
        {
            ALIVE_LOG_WARN("HandleSocketCommand: Bot %llu not found", cmd.botGuid);
            return;
        }
    }

    switch (cmd.type)
    {
        // ===================================================================
        // CHAT COMMANDS
        // ===================================================================
        case SocketCommandType::BOT_SAY:
        {
            if (cmd.message.empty())
            {
                ALIVE_LOG_WARN("HandleSocketCommand: BOT_SAY with empty message");
                return;
            }
            m_behaviorExecutor->Say(cmd.botGuid, cmd.message, BehaviorPriority::NORMAL);
            ALIVE_LOG_DEBUG("Bot %llu: Say '%s'", cmd.botGuid, cmd.message.c_str());
            break;
        }

        case SocketCommandType::BOT_YELL:
        {
            if (cmd.message.empty())
            {
                ALIVE_LOG_WARN("HandleSocketCommand: BOT_YELL with empty message");
                return;
            }
            // Yell uses higher priority
            BehaviorRequest req;
            req.botGuid = cmd.botGuid;
            req.type = BehaviorType::CHAT_YELL;
            req.message = cmd.message;
            req.priority = BehaviorPriority::NORMAL;
            m_behaviorExecutor->QueueBehavior(req);
            ALIVE_LOG_DEBUG("Bot %llu: Yell '%s'", cmd.botGuid, cmd.message.c_str());
            break;
        }

        case SocketCommandType::BOT_WHISPER:
        {
            if (cmd.message.empty() || cmd.targetGuid == 0)
            {
                ALIVE_LOG_WARN("HandleSocketCommand: BOT_WHISPER missing message or target");
                return;
            }
            m_behaviorExecutor->Whisper(cmd.botGuid, cmd.targetGuid, cmd.message);
            ALIVE_LOG_DEBUG("Bot %llu: Whisper to %llu '%s'",
                           cmd.botGuid, cmd.targetGuid, cmd.message.c_str());
            break;
        }

        case SocketCommandType::BOT_EMOTE:
        {
            // numParam is the emote ID, or message contains emote name
            if (cmd.numParam > 0)
            {
                BehaviorRequest req;
                req.botGuid = cmd.botGuid;
                req.type = BehaviorType::CHAT_EMOTE;
                req.spellId = cmd.numParam;  // Use spellId field for emote ID
                req.priority = BehaviorPriority::NORMAL;
                m_behaviorExecutor->QueueBehavior(req);
            }
            else if (!cmd.message.empty())
            {
                m_behaviorExecutor->Emote(cmd.botGuid, cmd.message, BehaviorPriority::NORMAL);
            }
            ALIVE_LOG_DEBUG("Bot %llu: Emote %u / '%s'",
                           cmd.botGuid, cmd.numParam, cmd.message.c_str());
            break;
        }

        // ===================================================================
        // CONSUMABLE COMMANDS
        // ===================================================================
        case SocketCommandType::BOT_DRINK:
        {
            if (bot)
            {
                PlayerbotAI* ai = sPlayerbotsMgr.GetPlayerbotAI(bot);
                if (ai)
                {
                    ai->DoSpecificAction("drink", Event(), true);
                    ALIVE_LOG_DEBUG("Bot %llu: Drink", cmd.botGuid);
                }
            }
            break;
        }

        case SocketCommandType::BOT_EAT:
        {
            if (bot)
            {
                PlayerbotAI* ai = sPlayerbotsMgr.GetPlayerbotAI(bot);
                if (ai)
                {
                    ai->DoSpecificAction("food", Event(), true);
                    ALIVE_LOG_DEBUG("Bot %llu: Eat", cmd.botGuid);
                }
            }
            break;
        }

        // ===================================================================
        // MOVEMENT COMMANDS
        // ===================================================================
        case SocketCommandType::BOT_FOLLOW:
        {
            if (cmd.targetGuid == 0)
            {
                ALIVE_LOG_WARN("HandleSocketCommand: BOT_FOLLOW missing target");
                return;
            }
            m_behaviorExecutor->Follow(cmd.botGuid, cmd.targetGuid);
            ALIVE_LOG_DEBUG("Bot %llu: Follow %llu", cmd.botGuid, cmd.targetGuid);
            break;
        }

        case SocketCommandType::BOT_STAY:
        {
            if (bot)
            {
                PlayerbotAI* ai = sPlayerbotsMgr.GetPlayerbotAI(bot);
                if (ai)
                {
                    ai->DoSpecificAction("stay", Event(), true);
                    ALIVE_LOG_DEBUG("Bot %llu: Stay", cmd.botGuid);
                }
            }
            break;
        }

        case SocketCommandType::BOT_MOVE_TO:
        {
            if (cmd.x == 0 && cmd.y == 0 && cmd.z == 0)
            {
                ALIVE_LOG_WARN("HandleSocketCommand: BOT_MOVE_TO with zero position");
                return;
            }
            m_behaviorExecutor->MoveToPosition(cmd.botGuid, cmd.x, cmd.y, cmd.z);
            ALIVE_LOG_DEBUG("Bot %llu: MoveTo (%.1f, %.1f, %.1f)",
                           cmd.botGuid, cmd.x, cmd.y, cmd.z);
            break;
        }

        // ===================================================================
        // COMBAT COMMANDS
        // ===================================================================
        case SocketCommandType::BOT_ATTACK:
        {
            if (cmd.targetGuid != 0)
            {
                m_behaviorExecutor->SetTarget(cmd.botGuid, cmd.targetGuid);
            }
            if (bot)
            {
                PlayerbotAI* ai = sPlayerbotsMgr.GetPlayerbotAI(bot);
                if (ai)
                {
                    ai->DoSpecificAction("attack", Event(), true);
                }
            }
            ALIVE_LOG_DEBUG("Bot %llu: Attack %llu", cmd.botGuid, cmd.targetGuid);
            break;
        }

        case SocketCommandType::BOT_FLEE:
        {
            m_behaviorExecutor->Flee(cmd.botGuid, cmd.targetGuid);
            ALIVE_LOG_DEBUG("Bot %llu: Flee from %llu", cmd.botGuid, cmd.targetGuid);
            break;
        }

        case SocketCommandType::BOT_ASSIST:
        {
            if (cmd.targetGuid == 0)
            {
                ALIVE_LOG_WARN("HandleSocketCommand: BOT_ASSIST missing target");
                return;
            }
            if (bot)
            {
                PlayerbotAI* ai = sPlayerbotsMgr.GetPlayerbotAI(bot);
                if (ai)
                {
                    // Set the assist target
                    Player* target = ObjectAccessor::FindPlayer(ObjectGuid(cmd.targetGuid));
                    if (target)
                    {
                        ai->DoSpecificAction("assist", Event(), true);
                    }
                }
            }
            ALIVE_LOG_DEBUG("Bot %llu: Assist %llu", cmd.botGuid, cmd.targetGuid);
            break;
        }

        case SocketCommandType::BOT_STOP_COMBAT:
        {
            if (bot)
            {
                PlayerbotAI* ai = sPlayerbotsMgr.GetPlayerbotAI(bot);
                if (ai)
                {
                    ai->DoSpecificAction("stop", Event(), true);
                }
            }
            ALIVE_LOG_DEBUG("Bot %llu: StopCombat", cmd.botGuid);
            break;
        }

        // ===================================================================
        // STRATEGY COMMANDS
        // ===================================================================
        case SocketCommandType::BOT_SET_ROLE:
        {
            if (cmd.message.empty())
            {
                ALIVE_LOG_WARN("HandleSocketCommand: BOT_SET_ROLE missing role");
                return;
            }
            if (bot)
            {
                PlayerbotAI* ai = sPlayerbotsMgr.GetPlayerbotAI(bot);
                if (ai)
                {
                    // Role is in message: "tank", "healer", "dps"
                    // Use DoSpecificAction instead of private HandleCommand
                    std::string roleAction = "role " + cmd.message;
                    ai->DoSpecificAction(roleAction, Event(), true);
                }
            }
            ALIVE_LOG_DEBUG("Bot %llu: SetRole '%s'", cmd.botGuid, cmd.message.c_str());
            break;
        }

        case SocketCommandType::BOT_SET_STRATEGY:
        {
            if (cmd.message.empty())
            {
                ALIVE_LOG_WARN("HandleSocketCommand: BOT_SET_STRATEGY missing strategy");
                return;
            }
            if (bot)
            {
                PlayerbotAI* ai = sPlayerbotsMgr.GetPlayerbotAI(bot);
                if (ai)
                {
                    ai->ChangeStrategy(cmd.message, BotState::BOT_STATE_COMBAT);
                }
            }
            ALIVE_LOG_DEBUG("Bot %llu: SetStrategy '%s'", cmd.botGuid, cmd.message.c_str());
            break;
        }

        // ===================================================================
        // SYSTEM COMMANDS
        // ===================================================================
        case SocketCommandType::REQUEST_STATE:
        {
            // Return bot state via bridge
            ALIVE_LOG_DEBUG("REQUEST_STATE for bot %llu", cmd.botGuid);
            // This would normally emit state back through the bridge
            // For now, just log that the request was received
            break;
        }

        case SocketCommandType::SET_DEBUG:
        {
            bool enable = (cmd.numParam != 0);
            SetDebugMode(enable);
            ALIVE_LOG_INFO("Debug mode %s via socket command", enable ? "ENABLED" : "DISABLED");
            break;
        }

        case SocketCommandType::PING:
        {
            // Just acknowledge - used for keep-alive
            ALIVE_LOG_TRACE("PING received");
            break;
        }

        // ===================================================================
        // GOAL COMMANDS
        // ===================================================================
        case SocketCommandType::SET_GOAL:
        {
            // Goal type in numParam, target info in message
            ALIVE_LOG_DEBUG("SET_GOAL for bot %llu: type=%u target='%s'",
                           cmd.botGuid, cmd.numParam, cmd.message.c_str());
            // Goal system integration would go here
            break;
        }

        case SocketCommandType::CANCEL_GOAL:
        {
            ALIVE_LOG_DEBUG("CANCEL_GOAL for bot %llu: goalId=%u",
                           cmd.botGuid, cmd.numParam);
            // Goal system integration would go here
            break;
        }

        case SocketCommandType::QUERY_GOALS:
        {
            ALIVE_LOG_DEBUG("QUERY_GOALS for bot %llu", cmd.botGuid);
            // Goal system integration would go here
            break;
        }

        default:
        {
            ALIVE_LOG_WARN("HandleSocketCommand: Unknown command type %u",
                          static_cast<uint32>(cmd.type));
            break;
        }
    }
#else
    ALIVE_LOG_DEBUG("HandleSocketCommand: MOD_PLAYERBOTS not available");
#endif
}

BotMemoryData* AliveWorldManager::GetBotMemoryRaw(uint32 botGuid) const
{
    if (!m_memoryManager)
        return nullptr;
    return m_memoryManager->GetBotMemory(static_cast<uint64>(botGuid));
}

} // namespace AliveWorld
