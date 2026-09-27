/*
 * mod-alive-world - Main Manager
 * Copyright (C) 2025 EC
 *
 * Central coordinator for the Alive World system. Manages all subsystems
 * and provides the main entry point for the module.
 */

#ifndef ALIVE_WORLD_MANAGER_H
#define ALIVE_WORLD_MANAGER_H

#include "AliveWorldTypes.h"

#include <memory>
#include <unordered_map>

class Player;
class Creature;
class Unit;

namespace AliveWorld
{
    class EventCollector;
    class WorldStateManager;
    class GossipTemplateManager;
    class BotMemoryManager;
    class IntelligenceTierManager;
    class AIDecisionEngine;
    class BehaviorExecutor;
    class StrategyParser;
    // AliveWorldSocket and AliveWorldHttp removed - using mod-ec-realtime-bridge instead
    class EmoteReactionSystem;

    // Debug logging helper
    class Logger
    {
    public:
        static void Log(AWLogLevel level, const char* func, const char* fmt, ...);
        static void LogToGame(const std::string& message);
        static void SetDebugEnabled(bool enabled);
        static void SetLogLevel(uint8 level);
        static bool IsDebugEnabled();
        static uint8 GetLogLevel();

    private:
        static bool s_debugEnabled;
        static uint8 s_logLevel;
    };

    // Logging macros
    #define ALIVE_LOG_ERROR(fmt, ...) AliveWorld::Logger::Log(AliveWorld::AWLogLevel::AW_ERROR, __FUNCTION__, fmt, ##__VA_ARGS__)
    #define ALIVE_LOG_WARN(fmt, ...)  AliveWorld::Logger::Log(AliveWorld::AWLogLevel::AW_WARNING, __FUNCTION__, fmt, ##__VA_ARGS__)
    #define ALIVE_LOG_INFO(fmt, ...)  AliveWorld::Logger::Log(AliveWorld::AWLogLevel::AW_INFO, __FUNCTION__, fmt, ##__VA_ARGS__)
    #define ALIVE_LOG_DEBUG(fmt, ...) AliveWorld::Logger::Log(AliveWorld::AWLogLevel::AW_DEBUG, __FUNCTION__, fmt, ##__VA_ARGS__)
    #define ALIVE_LOG_TRACE(fmt, ...) AliveWorld::Logger::Log(AliveWorld::AWLogLevel::AW_TRACE, __FUNCTION__, fmt, ##__VA_ARGS__)

    class AliveWorldManager
    {
    private:
        AliveWorldManager();
        ~AliveWorldManager();

    public:
        static AliveWorldManager* Instance();

        // Initialization
        void Initialize();
        void LoadConfig();
        void Shutdown();

        // Main update loop (called from WorldScript::OnUpdate)
        void Update(uint32 diff);

        // Event hooks (called from scripts)
        void OnPlayerLogin(Player* player);
        void OnPlayerLogout(Player* player);
        void OnPlayerChat(Player* player, uint32 type, const std::string& message);
        void OnPlayerEmote(Player* player, uint32 emoteId);
        void OnPlayerKillCreature(Player* player, Creature* creature);
        void OnPlayerKillPlayer(Player* killer, Player* victim);
        void OnPlayerDeath(Player* player, Unit* killer);
        void OnCreatureDeath(Creature* creature, Unit* killer);
        void OnPlayerChangeZone(Player* player, uint32 newZoneId, uint32 newAreaId);
        void OnBotSpawn(Player* bot);
        void OnBotDespawn(Player* bot);
        void OnGroupFormed(Player* leader);
        void OnGroupDisbanded(Player* leader);
        void OnTradeComplete(Player* player1, Player* player2);
        void OnDuelStart(Player* player1, Player* player2);
        void OnDuelEnd(Player* winner, Player* loser);
        void OnBossEngaged(Creature* boss, Player* engager);
        void OnBossKilled(Creature* boss, Player* killer);
        void OnRareSpawn(Creature* creature);
        void OnWorldEventStart(uint32 eventId, const std::string& eventName);
        void OnWorldEventEnd(uint32 eventId, const std::string& eventName);
        void OnCrimeCommitted(Unit* criminal, uint32 zoneId, uint8 wantedLevel);

        // Weather and time events
        void OnWeatherChange(uint32 zoneId, uint8 weatherType, uint32 weatherState, float grade);
        void CheckTimeTransition();

        // Weather/Time gossip generation
        std::string GetWeatherGossip(uint32 zoneId) const;
        std::string GetWeatherGossip(uint32 zoneId, const BotPersonality& personality) const;
        std::string GetTimeGossip() const;
        std::string GetTimeGossip(const BotPersonality& personality) const;
        std::string GetEnvironmentGossip(uint32 zoneId) const;  // Combined weather + time
        std::string GetEnvironmentGossip(uint32 zoneId, const BotPersonality& personality) const;

        // Bot AI integration (called from playerbots)
        bool OnBotNeedsDecision(Player* bot, const std::string& situation);
        bool OnBotReceivedChat(Player* bot, Player* speaker, const std::string& message);
        void OnBotStrategyChange(Player* bot, const std::string& newStrategy);

        // Subsystem access
        EventCollector* GetEventCollector() { return m_eventCollector.get(); }
        WorldStateManager* GetWorldStateManager() { return m_worldStateManager.get(); }
        GossipTemplateManager* GetGossipTemplateManager() { return m_gossipTemplateManager.get(); }
        BotMemoryManager* GetBotMemoryManager() { return m_memoryManager.get(); }
        IntelligenceTierManager* GetTierManager() { return m_tierManager.get(); }
        AIDecisionEngine* GetAIEngine() { return m_aiEngine.get(); }
        BehaviorExecutor* GetBehaviorExecutor() { return m_behaviorExecutor.get(); }
        StrategyParser* GetStrategyParser() { return m_strategyParser.get(); }
        EmoteReactionSystem* GetEmoteSystem() { return m_emoteSystem.get(); }

        // Public accessors for socket JSON building
        BotMemoryData* GetBotMemoryRaw(uint32 botGuid) const;
        BotTier GetBotTier(uint32 botGuid) const;

        // Daemon connectivity
        bool IsDaemonConnected() const { return m_daemonConnected; }
        bool IsFallbackMode() const { return !m_daemonConnected; }

        // Status
        bool IsEnabled() const { return m_enabled; }
        bool IsInitialized() const { return m_initialized; }
        uint64 GetUptimeSeconds() const;

        // Debug
        void SetDebugMode(bool enabled);
        bool IsDebugMode() const { return m_debugEnabled; }
        void SetDebugLogLevel(uint8 level);
        uint8 GetDebugLogLevel() const { return m_debugLogLevel; }
        void DumpState();

        // Statistics
        APIStats GetStats() const;

        // Check if unit is a playerbot (public for script access)
        bool IsBot(Unit* unit) const;

        // Command handler for socket commands (public for bridge adapter access)
        void HandleSocketCommand(const struct SocketCommand& cmd);

    private:

        // Fallback behavior when daemon is disconnected
        void ExecuteFallbackBehavior(Player* bot, const std::string& situation);

        // Apply AI decision to bot behavior (attack, defend, flee, call for help)
        void ApplyAIDecision(uint64 botGuid, const std::string& decision);

        // Periodic check for bot proximity to players and health/mana status
        void UpdateBotProximityAndHealth();

        // Periodic enforcement for bot hunter profiles
        void UpdateRelentlessHunters(uint32 diff);

        // Configuration
        bool m_enabled;
        uint32 m_updateIntervalMs;
        bool m_relentlessHunterEnabled;
        uint32 m_relentlessHunterUpdateIntervalMs;
        bool m_relentlessHunterCrossZoneEnabled;
        bool m_relentlessHunterCrossMapTeleportEnabled;
        uint32 m_relentlessHunterTeleportCooldownMs;
        bool m_debugEnabled;
        uint8 m_debugLogLevel;

        // State
        bool m_initialized;
        uint64 m_startTime;
        bool m_daemonConnected;

        // Subsystems
        std::unique_ptr<EventCollector> m_eventCollector;
        std::unique_ptr<WorldStateManager> m_worldStateManager;
        std::unique_ptr<GossipTemplateManager> m_gossipTemplateManager;
        std::unique_ptr<BotMemoryManager> m_memoryManager;
        std::unique_ptr<IntelligenceTierManager> m_tierManager;
        std::unique_ptr<AIDecisionEngine> m_aiEngine;
        std::unique_ptr<BehaviorExecutor> m_behaviorExecutor;
        std::unique_ptr<StrategyParser> m_strategyParser;
        std::unique_ptr<EmoteReactionSystem> m_emoteSystem;

        // Timing
        uint32 m_updateTimer;
        uint32 m_relentlessHunterTimer;

        // Relentless hunter action cooldowns by bot GUID
        std::unordered_map<uint64, uint64> m_hunterNextActionMs;
        std::unordered_map<uint64, uint64> m_hunterNextTeleportMs;
    };

    #define sAliveWorldMgr AliveWorld::AliveWorldManager::Instance()

} // namespace AliveWorld

#endif // ALIVE_WORLD_MANAGER_H
