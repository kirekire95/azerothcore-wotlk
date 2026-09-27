/*
 * mod-alive-world - Bot Memory Manager
 * Copyright (C) 2025 EC
 *
 * Manages per-bot memory including combat history, social relationships,
 * personality traits, and known gossip. Each bot has lightweight memory
 * that persists across sessions.
 */

#ifndef ALIVE_WORLD_BOT_MEMORY_H
#define ALIVE_WORLD_BOT_MEMORY_H

#include "AliveWorldTypes.h"

#include <mutex>
#include <shared_mutex>
#include <unordered_map>

class Player;

namespace AliveWorld
{
    class WorldStateManager;

    class BotMemoryManager
    {
    public:
        BotMemoryManager();
        ~BotMemoryManager();

        // Initialization
        void Initialize(WorldStateManager* worldStateManager);
        void LoadConfig();

        // Bot memory access
        BotMemoryData* GetBotMemory(uint64 botGuid);
        const BotMemoryData* GetBotMemory(uint64 botGuid) const;
        BotMemoryData& GetOrCreateBotMemory(uint64 botGuid, const std::string& botName);
        bool HasBotMemory(uint64 botGuid) const;
        void RemoveBotMemory(uint64 botGuid);

        // Combat memory
        void RecordCombat(uint64 botGuid, const CombatMemory& combat);
        std::vector<CombatMemory> GetRecentCombat(uint64 botGuid, uint32 count = 5) const;
        float GetCombatWinRate(uint64 botGuid) const;

        // Social memory
        void RecordSocialInteraction(uint64 botGuid, uint64 playerGuid,
                                     const std::string& playerName,
                                     const std::string& interactionType,
                                     float sentimentDelta = 0.0f);
        SocialMemory* GetSocialMemory(uint64 botGuid, uint64 playerGuid);
        float GetSentiment(uint64 botGuid, uint64 playerGuid) const;
        void UpdateSentiment(uint64 botGuid, uint64 playerGuid, float delta);
        std::vector<SocialMemory> GetFriends(uint64 botGuid, float minSentiment = 0.5f) const;
        std::vector<SocialMemory> GetEnemies(uint64 botGuid, float maxSentiment = -0.3f) const;

        // Location memory
        void RecordZoneVisit(uint64 botGuid, uint32 zoneId, uint32 durationMinutes);
        uint32 GetFavoriteZone(uint64 botGuid) const;
        uint32 GetZoneTimeSpent(uint64 botGuid, uint32 zoneId) const;
        void SetCurrentZone(uint64 botGuid, uint32 zoneId);

        // Personality
        BotPersonality& GetPersonality(uint64 botGuid);
        void EvolvePersonality(uint64 botGuid, const std::string& eventType, float intensity);
        void SetMood(uint64 botGuid, const std::string& mood);
        std::string GetMood(uint64 botGuid) const;
        bool HasPersonalityOverride(uint64 botGuid) const;
        bool GetPersonalityOverride(uint64 botGuid, BotPersonalityOverride& outOverride) const;
        std::vector<BotPersonalityOverride> GetRelentlessHunterProfiles() const;
        void ReloadPersonalityOverrides();

        // Gossip knowledge
        void LearnGossip(uint64 botGuid, uint32 gossipId);
        bool KnowsGossip(uint64 botGuid, uint32 gossipId) const;
        std::vector<uint32> GetKnownGossip(uint64 botGuid) const;

        // World event participation (mod-ec-world-events integration)
        void RecordEventJoin(uint64 botGuid, uint32 eventId, uint32 instanceId,
                            const std::string& eventName, uint8 eventType, uint32 zoneId,
                            bool voluntary, const std::string& role = "participant");
        void RecordEventLeave(uint64 botGuid, uint32 instanceId, bool successful);
        void UpdateEventStats(uint64 botGuid, uint32 instanceId,
                             uint32 kills, uint32 deaths, uint64 damageDealt, uint64 healingDone);
        bool IsInEvent(uint64 botGuid) const;
        uint32 GetCurrentEventInstance(uint64 botGuid) const;
        std::vector<EventParticipation> GetEventHistory(uint64 botGuid, uint32 count = 10) const;
        uint32 GetEventParticipationCount(uint64 botGuid) const;
        float GetEventSuccessRate(uint64 botGuid) const;

        // Intelligence tier
        void SetBotTier(uint64 botGuid, BotTier tier);
        BotTier GetBotTier(uint64 botGuid) const;
        std::vector<uint64> GetBotsByTier(BotTier tier) const;
        uint32 GetTierCount(BotTier tier) const;

        // AI state
        void SetAIProcessing(uint64 botGuid, bool processing);
        bool IsAIProcessing(uint64 botGuid) const;
        void SetLastAIRequest(uint64 botGuid, uint64 timestamp);
        uint64 GetLastAIRequest(uint64 botGuid) const;
        void RecordAIDecision(uint64 botGuid, const std::string& decision);
        std::string GetLastAIDecision(uint64 botGuid) const;

        // Persistence
        void LoadFromDatabase();
        void SaveToDatabase();
        void SaveBotMemory(uint64 botGuid);
        void LoadBotMemory(uint64 botGuid);

        // Update (called periodically)
        void Update(uint32 diff);

        // Stats
        uint32 GetTotalBots() const;
        uint32 GetLoadedBots() const;

        // Settings
        bool IsEnabled() const { return m_enabled; }
        void SetEnabled(bool enabled) { m_enabled = enabled; }

    private:
        // Initialize random personality
        void InitializeRandomPersonality(BotPersonality& personality);

        // Initialize personality based on class and race (the "Data Goldmine" approach)
        void InitializePersonalityFromClassRace(BotPersonality& personality, uint8 playerClass, uint8 playerRace);

        // Personality overrides
        void LoadPersonalityOverrides();
        bool ApplyPersonalityOverride(uint64 botGuid, BotMemoryData& memory);
        void EnforcePersonalityOverrides();

        // Serialize/deserialize for database
        std::string SerializeMemory(const BotMemoryData& memory) const;
        bool DeserializeMemory(const std::string& json, BotMemoryData& memory);

        // Configuration
        bool m_enabled;
        uint32 m_maxCombatLog;
        uint32 m_maxSocialConnections;
        uint32 m_persistIntervalSeconds;
        bool m_personalityEvolution;

        // Bot memory storage
        std::unordered_map<uint64, BotMemoryData> m_botMemory;
        std::unordered_map<uint64, BotPersonalityOverride> m_personalityOverrides;
        mutable std::shared_mutex m_memoryMutex;

        // Tier tracking for quick lookup
        std::set<uint64> m_tier1Bots;
        std::set<uint64> m_tier2Bots;
        mutable std::mutex m_tierMutex;

        // Dirty tracking for persistence
        std::set<uint64> m_dirtyBots;
        std::mutex m_dirtyMutex;

        // Reference to world state manager
        WorldStateManager* m_worldStateManager;

        // Timing
        uint32 m_persistTimer;
        uint64 m_lastPersistTime;
    };

} // namespace AliveWorld

#endif // ALIVE_WORLD_BOT_MEMORY_H
