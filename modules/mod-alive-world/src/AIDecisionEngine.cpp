/*
 * mod-alive-world - AI Decision Engine Implementation
 * Copyright (C) 2025 EC
 */

#include "AIDecisionEngine.h"
#include "AliveWorldBridgeAdapter.h"
#include "AliveWorldManager.h"
#include "BotMemoryManager.h"
#include "WorldStateManager.h"
#include "IntelligenceTierManager.h"
#include "Config.h"
#include "Player.h"

#include <functional>
#include <sstream>

// HTTP client for Ollama API
#ifdef MOD_OLLAMA_CHAT
#include "mod-ollama-chat_httpclient.h"
#endif

namespace AliveWorld
{

static std::string EscapeJsonString(const std::string& input)
{
    std::string output;
    output.reserve(input.size() + 16);

    for (char c : input)
    {
        switch (c)
        {
            case '"':  output += "\\\""; break;
            case '\\': output += "\\\\"; break;
            case '\b': output += "\\b";  break;
            case '\f': output += "\\f";  break;
            case '\n': output += "\\n";  break;
            case '\r': output += "\\r";  break;
            case '\t': output += "\\t";  break;
            default:
                if (static_cast<unsigned char>(c) < 0x20)
                {
                    char buf[8];
                    snprintf(buf, sizeof(buf), "\\u%04x", static_cast<unsigned char>(c));
                    output += buf;
                }
                else
                {
                    output += c;
                }
                break;
        }
    }

    return output;
}

static std::string UnescapeJsonString(const std::string& input)
{
    std::string output;
    output.reserve(input.size());

    for (size_t i = 0; i < input.size(); ++i)
    {
        char c = input[i];
        if (c != '\\' || i + 1 >= input.size())
        {
            output += c;
            continue;
        }

        char next = input[++i];
        switch (next)
        {
            case '"': output += '"'; break;
            case '\\': output += '\\'; break;
            case '/': output += '/'; break;
            case 'b': output += '\b'; break;
            case 'f': output += '\f'; break;
            case 'n': output += '\n'; break;
            case 'r': output += '\r'; break;
            case 't': output += '\t'; break;
            case 'u':
                if (i + 4 < input.size())
                {
                    unsigned int code = 0;
                    std::istringstream hex(input.substr(i + 1, 4));
                    hex >> std::hex >> code;
                    output += (code <= 0x7F) ? static_cast<char>(code) : '?';
                    i += 4;
                }
                break;
            default:
                output += next;
                break;
        }
    }

    return output;
}

static std::string ExtractJsonString(const std::string& json, const std::string& key)
{
    std::string searchKey = "\"" + key + "\"";
    size_t keyPos = json.find(searchKey);
    if (keyPos == std::string::npos)
        return "";

    size_t colonPos = json.find(':', keyPos + searchKey.length());
    if (colonPos == std::string::npos)
        return "";

    size_t valueStart = json.find_first_not_of(" \t\n\r", colonPos + 1);
    if (valueStart == std::string::npos || json[valueStart] != '"')
        return "";

    std::string raw;
    raw.reserve(128);

    for (size_t i = valueStart + 1; i < json.size(); ++i)
    {
        char c = json[i];
        if (c == '"' && json[i - 1] != '\\')
        {
            return UnescapeJsonString(raw);
        }
        raw += c;
    }

    return "";
}

AIDecisionEngine::AIDecisionEngine()
    : m_enabled(true)
    , m_maxConcurrentRequests(5)
    , m_requestTimeoutSeconds(30)
    , m_cacheEnabled(true)
    , m_maxCacheSize(200)
    , m_cacheTTLSeconds(1800)
    , m_nextRequestId(1)
    , m_running(false)
    , m_activeWorkers(0)
    , m_totalRequests(0)
    , m_successfulRequests(0)
    , m_failedRequests(0)
    , m_cacheHits(0)
    , m_cacheMisses(0)
    , m_totalResponseTimeMs(0)
    , m_memoryManager(nullptr)
    , m_worldStateManager(nullptr)
    , m_tierManager(nullptr)
    , m_ollamaHost("127.0.0.1")
    , m_ollamaPort(11434)
    , m_ollamaModel("llama3.2")
{
}

AIDecisionEngine::~AIDecisionEngine()
{
    Shutdown();
}

void AIDecisionEngine::Initialize(BotMemoryManager* memoryManager,
                                  WorldStateManager* worldStateManager,
                                  IntelligenceTierManager* tierManager)
{
    ALIVE_LOG_INFO("AI Decision Engine initializing...");

    m_memoryManager = memoryManager;
    m_worldStateManager = worldStateManager;
    m_tierManager = tierManager;

    LoadConfig();

    if (m_enabled)
    {
        ALIVE_LOG_INFO("AI Decision Engine enabled");
        ALIVE_LOG_INFO("  Max Concurrent Requests: %u", m_maxConcurrentRequests);
        ALIVE_LOG_INFO("  Request Timeout: %u seconds", m_requestTimeoutSeconds);
        ALIVE_LOG_INFO("  Response Cache: %s (max %u, TTL %us)",
            m_cacheEnabled ? "ON" : "OFF", m_maxCacheSize, m_cacheTTLSeconds);
        ALIVE_LOG_INFO("  Ollama: %s:%u, model: %s",
            m_ollamaHost.c_str(), m_ollamaPort, m_ollamaModel.c_str());

        // Start worker threads
        m_running = true;
        for (uint32 i = 0; i < m_maxConcurrentRequests; ++i)
        {
            m_workers.emplace_back(&AIDecisionEngine::WorkerThread, this);
        }

        ALIVE_LOG_INFO("Started %u worker threads", m_maxConcurrentRequests);
    }
    else
    {
        ALIVE_LOG_WARN("AI Decision Engine disabled in config");
    }
}

void AIDecisionEngine::LoadConfig()
{
    m_enabled = sConfigMgr->GetOption<bool>("AliveWorld.AI.Enable", true);
    m_maxConcurrentRequests = sConfigMgr->GetOption<uint32>("AliveWorld.AI.MaxConcurrentRequests", 5);
    m_requestTimeoutSeconds = sConfigMgr->GetOption<uint32>("AliveWorld.AI.RequestTimeoutSeconds", 30);
    m_cacheEnabled = sConfigMgr->GetOption<bool>("AliveWorld.AI.ResponseCache.Enable", true);
    m_maxCacheSize = sConfigMgr->GetOption<uint32>("AliveWorld.AI.ResponseCache.MaxSize", 200);
    m_cacheTTLSeconds = sConfigMgr->GetOption<uint32>("AliveWorld.AI.ResponseCache.TTLSeconds", 1800);

    // Ollama config (reuse from mod-ollama-chat if available)
    m_ollamaHost = sConfigMgr->GetOption<std::string>("OllamaChat.Host", "127.0.0.1");
    m_ollamaPort = sConfigMgr->GetOption<uint32>("OllamaChat.Port", 11434);
    m_ollamaModel = sConfigMgr->GetOption<std::string>("OllamaChat.Model", "llama3.2");
}

void AIDecisionEngine::Shutdown()
{
    if (!m_running)
        return;

    ALIVE_LOG_INFO("AI Decision Engine shutting down...");

    m_running = false;
    m_queueCondition.notify_all();

    // Wait for workers to finish
    for (auto& worker : m_workers)
    {
        if (worker.joinable())
            worker.join();
    }
    m_workers.clear();

    // Clear queues
    {
        std::lock_guard<std::mutex> lock(m_queueMutex);
        while (!m_requestQueue.empty())
            m_requestQueue.pop();
    }

    {
        std::lock_guard<std::mutex> lock(m_activeMutex);
        m_activeRequests.clear();
    }

    ALIVE_LOG_INFO("AI Decision Engine shutdown complete");
}

uint32 AIDecisionEngine::QueueRequest(const AIRequest& request)
{
    if (!m_enabled || !m_running)
        return 0;

    AIRequest req = request;
    req.requestId = m_nextRequestId++;
    req.createdAt = NowMs();
    req.timeoutAt = req.createdAt + (m_requestTimeoutSeconds * 1000);

    {
        std::lock_guard<std::mutex> lock(m_queueMutex);
        m_requestQueue.push(req);
    }

    m_queueCondition.notify_one();
    m_totalRequests++;

    // Emit to bridge
    sAliveWorldBridge->EmitAIRequestQueued(
        req.botGuid, req.requestId,
        static_cast<uint8>(req.type),
        static_cast<uint8>(req.priority));

    ALIVE_LOG_DEBUG("AI request queued [%u]: type=%u, priority=%u, bot=%llu",
        req.requestId, static_cast<uint8>(req.type),
        static_cast<uint8>(req.priority), req.botGuid);

    return req.requestId;
}

uint32 AIDecisionEngine::QueueChatResponse(uint64 botGuid, const std::string& playerMessage,
                                           std::function<void(const std::string&, bool)> callback)
{
    // Check cache first
    std::string context = BuildChatContext(botGuid, playerMessage);
    size_t hash = HashContext(context);

    std::string cachedResponse;
    if (CheckCache(hash, cachedResponse))
    {
        // Return cached response asynchronously
        if (callback)
            callback(cachedResponse, true);
        return 0;  // No request queued
    }

    // Determine priority based on tier
    AIRequestPriority priority = AIRequestPriority::MEDIUM;
    if (m_tierManager)
    {
        BotTier tier = m_tierManager->GetBotTier(botGuid);
        priority = (tier == BotTier::TIER_1_ACTIVE) ? AIRequestPriority::HIGH : AIRequestPriority::MEDIUM;
    }

    AIRequest request;
    request.botGuid = botGuid;
    request.type = AIRequestType::CHAT_RESPONSE;
    request.priority = priority;
    request.prompt = playerMessage;
    request.context = context;
    request.callback = callback;

    return QueueRequest(request);
}

uint32 AIDecisionEngine::QueueCombatDecision(uint64 botGuid, const std::string& situation,
                                             std::function<void(const std::string&, bool)> callback)
{
    std::string context = BuildCombatContext(botGuid, situation);
    size_t hash = HashContext(context);

    std::string cachedResponse;
    if (CheckCache(hash, cachedResponse))
    {
        if (callback)
            callback(cachedResponse, true);
        return 0;
    }

    AIRequest request;
    request.botGuid = botGuid;
    request.type = AIRequestType::COMBAT_DECISION;
    request.priority = AIRequestPriority::URGENT;
    request.prompt = situation;
    request.context = context;
    request.callback = callback;

    return QueueRequest(request);
}

uint32 AIDecisionEngine::QueueStrategyAdapt(uint64 botGuid,
                                            std::function<void(const std::string&, bool)> callback)
{
    std::string context = BuildContext(botGuid, AIRequestType::STRATEGY_ADAPT);

    AIRequest request;
    request.botGuid = botGuid;
    request.type = AIRequestType::STRATEGY_ADAPT;
    request.priority = AIRequestPriority::LOW;
    request.prompt = "Based on recent events and personality, how should I adapt my behavior?";
    request.context = context;
    request.callback = callback;

    return QueueRequest(request);
}

uint32 AIDecisionEngine::QueueGossipGenerate(const WorldEvent& event,
                                             std::function<void(const std::string&, bool)> callback)
{
    std::ostringstream prompt;
    prompt << "Generate a short gossip message (1-2 sentences) about this event: "
           << event.details;

    AIRequest request;
    request.botGuid = 0;  // No specific bot
    request.type = AIRequestType::GOSSIP_GENERATE;
    request.priority = AIRequestPriority::LOW;
    request.prompt = prompt.str();
    request.callback = callback;

    return QueueRequest(request);
}

uint32 AIDecisionEngine::QueueGossipTweak(const std::string& templateText,
                                          const BotPersonality& personality,
                                          const std::string& zoneName,
                                          std::function<void(const std::string&, bool)> callback)
{
    // Build a prompt that asks Ollama to vary the template based on personality
    std::ostringstream prompt;
    prompt << "You are a medieval fantasy NPC. Rephrase this gossip in your own words, keeping the same meaning but adding personality:\n\n"
           << "Original: \"" << templateText << "\"\n\n"
           << "Your personality traits:\n"
           << "- Aggression: " << static_cast<int>(personality.aggression * 100) << "%\n"
           << "- Caution: " << static_cast<int>(personality.caution * 100) << "%\n"
           << "- Sociability: " << static_cast<int>(personality.sociability * 100) << "%\n"
           << "- Curiosity: " << static_cast<int>(personality.curiosity * 100) << "%\n"
           << "- Current mood: " << personality.currentMood << "\n\n"
           << "Zone context: " << zoneName << "\n\n"
           << "Respond with ONLY the rephrased gossip (1-2 sentences, no quotes, no explanation):";

    AIRequest request;
    request.botGuid = 0;
    request.type = AIRequestType::GOSSIP_TWEAK;
    request.priority = AIRequestPriority::LOW;
    request.prompt = prompt.str();

    // Wrap callback to add [TWEAKED 🤖] prefix
    request.callback = [originalCallback = std::move(callback)](const std::string& response, bool success)
    {
        if (success && !response.empty())
        {
            // Add tweaked prefix to indicate AI-enhanced content
            std::string tweakedResponse = "[TWEAKED \xF0\x9F\xA4\x96] " + response;  // 🤖 emoji in UTF-8
            originalCallback(tweakedResponse, true);
        }
        else
        {
            // Fall back to original (callback will use template)
            originalCallback("", false);
        }
    };

    return QueueRequest(request);
}

bool AIDecisionEngine::CancelRequest(uint32 requestId)
{
    // Note: We can't easily remove from priority queue, so we mark it as cancelled
    std::lock_guard<std::mutex> lock(m_activeMutex);

    auto it = m_activeRequests.find(requestId);
    if (it != m_activeRequests.end())
    {
        m_activeRequests.erase(it);
        return true;
    }

    return false;
}

bool AIDecisionEngine::IsRequestPending(uint32 requestId) const
{
    std::lock_guard<std::mutex> lock(m_activeMutex);
    return m_activeRequests.find(requestId) != m_activeRequests.end();
}

uint32 AIDecisionEngine::GetPendingRequestCount() const
{
    std::lock_guard<std::mutex> lock(m_queueMutex);
    return static_cast<uint32>(m_requestQueue.size());
}

uint32 AIDecisionEngine::GetProcessingCount() const
{
    return m_activeWorkers.load();
}

bool AIDecisionEngine::CheckCache(size_t contextHash, std::string& cachedResponse)
{
    if (!m_cacheEnabled)
        return false;

    std::lock_guard<std::mutex> lock(m_cacheMutex);

    auto it = m_responseCache.find(contextHash);
    if (it != m_responseCache.end())
    {
        // Check if expired
        if (NowMs() < it->second.expiresAt)
        {
            cachedResponse = it->second.response;
            const_cast<CachedResponse&>(it->second).hitCount++;
            m_cacheHits++;

            // Emit cache hit
            sAliveWorldBridge->EmitAICacheHit(0, std::to_string(contextHash));

            ALIVE_LOG_TRACE("Cache hit (hash: %zu, hits: %u)", contextHash, it->second.hitCount);
            return true;
        }
        else
        {
            // Expired, remove it
            m_responseCache.erase(it);
        }
    }

    m_cacheMisses++;
    return false;
}

void AIDecisionEngine::AddToCache(size_t contextHash, const std::string& response)
{
    if (!m_cacheEnabled)
        return;

    std::lock_guard<std::mutex> lock(m_cacheMutex);

    // Evict oldest if at capacity
    if (m_responseCache.size() >= m_maxCacheSize)
    {
        // Simple eviction: remove first item (not ideal but simple)
        m_responseCache.erase(m_responseCache.begin());
    }

    CachedResponse cached;
    cached.contextHash = contextHash;
    cached.response = response;
    cached.createdAt = NowMs();
    cached.expiresAt = cached.createdAt + (m_cacheTTLSeconds * 1000);
    cached.hitCount = 0;

    m_responseCache[contextHash] = cached;
    ALIVE_LOG_TRACE("Added to cache (hash: %zu)", contextHash);
}

void AIDecisionEngine::ClearCache()
{
    std::lock_guard<std::mutex> lock(m_cacheMutex);
    m_responseCache.clear();
    ALIVE_LOG_INFO("Response cache cleared");
}

uint32 AIDecisionEngine::GetCacheSize() const
{
    std::lock_guard<std::mutex> lock(m_cacheMutex);
    return static_cast<uint32>(m_responseCache.size());
}

float AIDecisionEngine::GetCacheHitRate() const
{
    uint32 total = m_cacheHits + m_cacheMisses;
    return total > 0 ? (static_cast<float>(m_cacheHits) / total) * 100.0f : 0.0f;
}

std::string AIDecisionEngine::BuildContext(uint64 botGuid, AIRequestType type)
{
    std::ostringstream context;

    if (m_memoryManager && botGuid > 0)
    {
        const BotMemoryData* memory = m_memoryManager->GetBotMemory(botGuid);
        if (memory)
        {
            context << "Bot: " << memory->botName << "\n";
            context << "Personality: Aggression=" << memory->personality.aggression
                    << ", Helpfulness=" << memory->personality.helpfulness
                    << ", Mood=" << memory->personality.currentMood << "\n";

            // Add recent combat summary
            if (!memory->combatLog.empty())
            {
                context << "Recent combat: ";
                const auto& lastCombat = memory->combatLog.back();
                context << (lastCombat.wasVictorious ? "Won" : "Lost")
                        << " against " << lastCombat.opponentName << "\n";
            }
        }
    }

    // Add world state context
    if (m_worldStateManager && m_memoryManager && botGuid > 0)
    {
        const BotMemoryData* memory = m_memoryManager->GetBotMemory(botGuid);
        if (memory && memory->currentZoneId > 0)
        {
            ZoneState state = m_worldStateManager->GetZoneState(memory->currentZoneId);
            context << "Zone: " << state.GetTotalPlayers() << " players, "
                    << "Activity=" << static_cast<int>(state.activityLevel)
                    << ", Danger=" << static_cast<int>(state.dangerLevel) << "\n";
        }
    }

    return context.str();
}

std::string AIDecisionEngine::BuildChatContext(uint64 botGuid, const std::string& playerMessage)
{
    std::ostringstream context;
    context << BuildContext(botGuid, AIRequestType::CHAT_RESPONSE);
    context << "Player message: \"" << playerMessage << "\"\n";
    context << "Respond in character as a WoW player, 1-2 sentences.\n";
    return context.str();
}

std::string AIDecisionEngine::BuildCombatContext(uint64 botGuid, const std::string& situation)
{
    std::ostringstream context;
    context << BuildContext(botGuid, AIRequestType::COMBAT_DECISION);
    context << "Combat situation: " << situation << "\n";
    context << "Decide: attack, defend, flee, or call for help. One word response.\n";
    return context.str();
}

size_t AIDecisionEngine::HashContext(const std::string& context)
{
    return std::hash<std::string>{}(context);
}

void AIDecisionEngine::WorkerThread()
{
    ALIVE_LOG_DEBUG("AI worker thread started");

    while (m_running)
    {
        AIRequest request;

        {
            std::unique_lock<std::mutex> lock(m_queueMutex);

            m_queueCondition.wait(lock, [this]() {
                return !m_running || !m_requestQueue.empty();
            });

            if (!m_running && m_requestQueue.empty())
                break;

            if (m_requestQueue.empty())
                continue;

            request = m_requestQueue.top();
            m_requestQueue.pop();
        }

        // Check if request has timed out
        if (NowMs() > request.timeoutAt)
        {
            ALIVE_LOG_DEBUG("Request %u timed out before processing", request.requestId);
            m_failedRequests++;
            if (request.callback)
                request.callback("", false);
            continue;
        }

        // Mark as active
        {
            std::lock_guard<std::mutex> lock(m_activeMutex);
            m_activeRequests[request.requestId] = request;
        }

        m_activeWorkers++;

        // Process the request
        ALIVE_LOG_DEBUG("Processing request %u", request.requestId);
        AIResponse response = ProcessRequest(request);

        m_activeWorkers--;

        // Remove from active
        {
            std::lock_guard<std::mutex> lock(m_activeMutex);
            m_activeRequests.erase(request.requestId);
        }

        // Cache successful responses
        if (response.success && m_cacheEnabled && !request.context.empty())
        {
            size_t hash = HashContext(request.context);
            AddToCache(hash, response.response);
        }

        // Update stats
        if (response.success)
            m_successfulRequests++;
        else
            m_failedRequests++;

        m_totalResponseTimeMs += response.processingTimeMs;

        // Emit completion to bridge
        sAliveWorldBridge->EmitAIRequestCompleted(
            request.botGuid, request.requestId,
            response.success, static_cast<uint32>(response.processingTimeMs));

        // Call callback
        if (request.callback)
            request.callback(response.response, response.success);
    }

    ALIVE_LOG_DEBUG("AI worker thread exiting");
}

AIResponse AIDecisionEngine::ProcessRequest(const AIRequest& request)
{
    AIResponse response;
    response.requestId = request.requestId;
    response.success = false;

    uint64 startTime = NowMs();

    // Build full prompt
    std::string fullPrompt = request.context + "\n" + request.prompt;

    // Call LLM
    std::string llmResponse = CallOllama(fullPrompt);

    response.processingTimeMs = NowMs() - startTime;

    if (!llmResponse.empty())
    {
        response.success = true;
        response.response = llmResponse;
        ALIVE_LOG_DEBUG("Request %u completed in %llu ms",
            request.requestId, response.processingTimeMs);
    }
    else
    {
        response.errorMessage = "Empty response from Ollama";
        ALIVE_LOG_WARN("Request %u failed: %s", request.requestId, response.errorMessage.c_str());
    }

    // Update bot memory with AI request timestamp
    if (m_memoryManager && request.botGuid > 0)
    {
        m_memoryManager->SetAIProcessing(request.botGuid, false);
        m_memoryManager->SetLastAIRequest(request.botGuid, NowMs());
    }

    return response;
}

std::string AIDecisionEngine::CallOllama(const std::string& prompt)
{
#ifdef MOD_OLLAMA_CHAT
    // Use the existing HTTP client from mod-ollama-chat
    OllamaHttpClient client;
    client.SetTimeout(m_requestTimeoutSeconds);

    if (!client.IsAvailable())
    {
        ALIVE_LOG_WARN("Ollama HTTP client not available");
        return "";
    }

    // Build JSON request
    std::ostringstream json;
    json << "{\"model\":\"" << EscapeJsonString(m_ollamaModel) << "\","
         << "\"prompt\":\"" << EscapeJsonString(prompt) << "\","
         << "\"stream\":false,"
         << "\"options\":{\"temperature\":0.7,\"num_predict\":100}}";

    std::string url = "http://" + m_ollamaHost + ":" + std::to_string(m_ollamaPort) + "/api/generate";
    std::string response = client.Post(url, json.str());

    return ExtractJsonString(response, "response");
#else
    ALIVE_LOG_WARN("mod-ollama-chat not available - AI features disabled");
    return "";
#endif
}

float AIDecisionEngine::GetAverageResponseTime() const
{
    if (m_successfulRequests == 0)
        return 0.0f;

    return static_cast<float>(m_totalResponseTimeMs.load()) / m_successfulRequests.load();
}

void AIDecisionEngine::Update(uint32 diff)
{
    // Periodic cache cleanup could go here
    // For now, expired entries are cleaned on access
}

} // namespace AliveWorld
