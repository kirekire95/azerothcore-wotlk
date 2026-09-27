/*
 * mod-alive-world - AI Decision Engine
 * Copyright (C) 2025 EC
 *
 * Manages AI/LLM requests for bot decision making. Implements rate limiting,
 * priority queuing, and response caching to handle hundreds of bots efficiently.
 */

#ifndef ALIVE_WORLD_AI_DECISION_ENGINE_H
#define ALIVE_WORLD_AI_DECISION_ENGINE_H

#include "AliveWorldTypes.h"

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <queue>
#include <thread>
#include <unordered_map>

namespace AliveWorld
{
    class BotMemoryManager;
    class WorldStateManager;
    class IntelligenceTierManager;

    class AIDecisionEngine
    {
    public:
        AIDecisionEngine();
        ~AIDecisionEngine();

        // Initialization
        void Initialize(BotMemoryManager* memoryManager,
                        WorldStateManager* worldStateManager,
                        IntelligenceTierManager* tierManager);
        void LoadConfig();
        void Shutdown();

        // Request submission
        uint32 QueueRequest(const AIRequest& request);
        uint32 QueueChatResponse(uint64 botGuid, const std::string& playerMessage,
                                 std::function<void(const std::string&, bool)> callback);
        uint32 QueueCombatDecision(uint64 botGuid, const std::string& situation,
                                   std::function<void(const std::string&, bool)> callback);
        uint32 QueueStrategyAdapt(uint64 botGuid,
                                  std::function<void(const std::string&, bool)> callback);
        uint32 QueueGossipGenerate(const WorldEvent& event,
                                   std::function<void(const std::string&, bool)> callback);

        // Tweak a gossip template using Ollama for variety
        // Returns tweaked text with [TWEAKED 🤖] prefix when successful
        uint32 QueueGossipTweak(const std::string& templateText,
                                const BotPersonality& personality,
                                const std::string& zoneName,
                                std::function<void(const std::string&, bool)> callback);

        // Request management
        bool CancelRequest(uint32 requestId);
        bool IsRequestPending(uint32 requestId) const;
        uint32 GetPendingRequestCount() const;
        uint32 GetProcessingCount() const;

        // Cache management
        bool CheckCache(size_t contextHash, std::string& cachedResponse);
        void AddToCache(size_t contextHash, const std::string& response);
        void ClearCache();
        uint32 GetCacheSize() const;
        float GetCacheHitRate() const;

        // Context building
        std::string BuildContext(uint64 botGuid, AIRequestType type);
        std::string BuildChatContext(uint64 botGuid, const std::string& playerMessage);
        std::string BuildCombatContext(uint64 botGuid, const std::string& situation);
        size_t HashContext(const std::string& context);

        // Update (processes queue)
        void Update(uint32 diff);

        // Stats
        uint32 GetTotalRequests() const { return m_totalRequests; }
        uint32 GetSuccessfulRequests() const { return m_successfulRequests; }
        uint32 GetFailedRequests() const { return m_failedRequests; }
        uint32 GetCacheHits() const { return m_cacheHits; }
        uint32 GetCacheMisses() const { return m_cacheMisses; }
        float GetAverageResponseTime() const;

        // Settings
        bool IsEnabled() const { return m_enabled; }
        void SetEnabled(bool enabled) { m_enabled = enabled; }
        uint32 GetMaxConcurrent() const { return m_maxConcurrentRequests; }

    private:
        // Worker thread for processing requests
        void WorkerThread();

        // Process a single request (calls LLM)
        AIResponse ProcessRequest(const AIRequest& request);

        // Call Ollama API
        std::string CallOllama(const std::string& prompt);

        // Priority queue comparison
        struct RequestCompare
        {
            bool operator()(const AIRequest& a, const AIRequest& b) const
            {
                // Higher priority first (lower number = higher priority)
                if (a.priority != b.priority)
                    return a.priority > b.priority;
                // Earlier requests first
                return a.createdAt > b.createdAt;
            }
        };

        // Configuration
        bool m_enabled;
        uint32 m_maxConcurrentRequests;
        uint32 m_requestTimeoutSeconds;
        bool m_cacheEnabled;
        uint32 m_maxCacheSize;
        uint32 m_cacheTTLSeconds;

        // Request queue
        std::priority_queue<AIRequest, std::vector<AIRequest>, RequestCompare> m_requestQueue;
        mutable std::mutex m_queueMutex;
        std::condition_variable m_queueCondition;

        // Active requests being processed
        std::unordered_map<uint32, AIRequest> m_activeRequests;
        mutable std::mutex m_activeMutex;

        // Request ID generator
        std::atomic<uint32> m_nextRequestId;

        // Response cache
        std::unordered_map<size_t, CachedResponse> m_responseCache;
        mutable std::mutex m_cacheMutex;

        // Worker threads
        std::vector<std::thread> m_workers;
        std::atomic<bool> m_running;
        std::atomic<uint32> m_activeWorkers;

        // Statistics
        std::atomic<uint32> m_totalRequests;
        std::atomic<uint32> m_successfulRequests;
        std::atomic<uint32> m_failedRequests;
        std::atomic<uint32> m_cacheHits;
        std::atomic<uint32> m_cacheMisses;
        std::atomic<uint64> m_totalResponseTimeMs;

        // References
        BotMemoryManager* m_memoryManager;
        WorldStateManager* m_worldStateManager;
        IntelligenceTierManager* m_tierManager;

        // Ollama configuration (from mod-ollama-chat)
        std::string m_ollamaHost;
        uint32 m_ollamaPort;
        std::string m_ollamaModel;
    };

} // namespace AliveWorld

#endif // ALIVE_WORLD_AI_DECISION_ENGINE_H
