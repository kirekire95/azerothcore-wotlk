/*
 * mod-alive-world - Gossip Manager Implementation
 * Copyright (C) 2025 EC
 */

#include "GossipManager.h"
#include "AliveWorldBridgeAdapter.h"
#include "BotMemoryManager.h"
#include "BehaviorExecutor.h"
#include "AliveWorldManager.h"

#include "Player.h"
#include "ObjectAccessor.h"
#include "DatabaseEnv.h"
#include "Log.h"

#include "PlayerbotAI.h"
#include "PlayerbotMgr.h"

namespace AliveWorld
{

#define GOSSIP_LOG_DEBUG(fmt, ...) \
    LOG_DEBUG("module.alive-world", "[GossipManager] " fmt, ##__VA_ARGS__)

// ============================================================================
// CONSTRUCTOR / DESTRUCTOR
// ============================================================================

GossipManager::GossipManager()
{
}

GossipManager::~GossipManager()
{
    Shutdown();
}

// ============================================================================
// INITIALIZATION
// ============================================================================

void GossipManager::Initialize(BotMemoryManager* memoryMgr, BehaviorExecutor* behaviorExec)
{
    m_memoryManager = memoryMgr;
    m_behaviorExecutor = behaviorExec;

    LoadTemplates();

    LOG_INFO("module.alive-world", "[GossipManager] Initialized with %u templates",
        static_cast<uint32>(m_templates.size()));
}

void GossipManager::LoadTemplates()
{
    m_templates.clear();

    // Load from database if available
    QueryResult result = WorldDatabase.Query(
        "SELECT id, type, pattern, probability, faction_id FROM ec_gossip_templates WHERE enabled = 1");

    if (result)
    {
        do
        {
            Field* fields = result->Fetch();

            GossipTemplate tmpl;
            tmpl.id = fields[0].Get<uint32>();
            tmpl.type = static_cast<GossipType>(fields[1].Get<uint8>());
            tmpl.pattern = fields[2].Get<std::string>();
            tmpl.probability = fields[3].Get<uint32>();
            tmpl.factionId = fields[4].Get<uint32>();

            m_templates.push_back(tmpl);
        }
        while (result->NextRow());
    }

    // Add default templates if database is empty
    if (m_templates.empty())
    {
        // Crime templates
        m_templates.push_back({1, GossipType::CRIME, "Did you hear? {subject} attacked someone in {zone}!", 100, 0, {}});
        m_templates.push_back({2, GossipType::CRIME, "Watch out! {subject} is wanted by the guards!", 100, 0, {}});
        m_templates.push_back({3, GossipType::CRIME, "There's been trouble in {zone}. Some criminal named {subject}.", 80, 0, {}});

        // Boss kill templates
        m_templates.push_back({10, GossipType::BOSS_KILL, "{subject} has been defeated!", 100, 0, {}});
        m_templates.push_back({11, GossipType::BOSS_KILL, "Heroes have slain {subject}! What a victory!", 80, 0, {}});
        m_templates.push_back({12, GossipType::BOSS_KILL, "I heard {subject} fell in battle. About time!", 70, 0, {}});

        // Rare spawn templates
        m_templates.push_back({20, GossipType::RARE_SPAWN, "{subject} has appeared in {zone}!", 100, 0, {}});
        m_templates.push_back({21, GossipType::RARE_SPAWN, "A rare creature called {subject} was spotted nearby!", 80, 0, {}});

        // Achievement templates
        m_templates.push_back({30, GossipType::ACHIEVEMENT, "{subject} earned {action}! Impressive!", 100, 0, {}});
        m_templates.push_back({31, GossipType::ACHIEVEMENT, "Did you see? {subject} completed {action}!", 80, 0, {}});

        // Zone event templates
        m_templates.push_back({40, GossipType::ZONE_EVENT, "Something's happening in {zone}!", 100, 0, {}});
        m_templates.push_back({41, GossipType::ZONE_EVENT, "There's a battle brewing in {zone}. Heroes needed!", 90, 0, {}});
        m_templates.push_back({42, GossipType::ZONE_EVENT, "The call to arms has gone out for {zone}!", 80, 0, {}});

        // Warning templates
        m_templates.push_back({50, GossipType::WARNING, "Be careful in {zone}. It's not safe!", 100, 0, {}});
        m_templates.push_back({51, GossipType::WARNING, "I'd avoid {zone} if I were you.", 80, 0, {}});
        m_templates.push_back({52, GossipType::WARNING, "Danger! {subject} is on the prowl!", 90, 0, {}});

        // General templates
        m_templates.push_back({60, GossipType::GENERAL, "Have you heard the latest about {subject}?", 100, 0, {}});
        m_templates.push_back({61, GossipType::GENERAL, "Word is spreading about {subject}...", 80, 0, {}});
        m_templates.push_back({62, GossipType::GENERAL, "People are talking about {subject}.", 70, 0, {}});

        // Reaction templates
        m_templates.push_back({70, GossipType::REACTION, "What a sight to see!", 100, 0, {}});
        m_templates.push_back({71, GossipType::REACTION, "I can't believe what I just witnessed!", 80, 0, {}});
    }
}

void GossipManager::Shutdown()
{
    std::lock_guard<std::mutex> lock(m_gossipMutex);
    m_gossipPool.clear();
}

// ============================================================================
// CREATE GOSSIP
// ============================================================================

uint32 GossipManager::CreateGossip(GossipType type, uint32 zoneId, const std::string& text,
                                   const std::string& subject, uint32 importance,
                                   uint64 sourceGuid, const std::string& sourceName,
                                   uint32 ttlSeconds)
{
    GossipItem item;
    item.gossipId = m_nextGossipId++;
    item.originZoneId = zoneId;
    item.headline = subject.empty() ? text : subject;
    item.details = text;
    item.importance = static_cast<uint8>(std::min(importance, 10u));
    item.timestamp = NowMs();
    item.expiresAt = item.timestamp + (ttlSeconds * 1000);
    item.spreadRadius = 0;
    item.sourceEventId = 0;

    {
        std::lock_guard<std::mutex> lock(m_gossipMutex);

        // Enforce pool size limit
        if (m_gossipPool.size() >= m_maxGossipPoolSize)
        {
            CleanupExpired();

            // If still too many, remove oldest
            if (m_gossipPool.size() >= m_maxGossipPoolSize)
            {
                uint32 oldestId = 0;
                uint64 oldestTime = UINT64_MAX;
                for (const auto& pair : m_gossipPool)
                {
                    if (pair.second.timestamp < oldestTime)
                    {
                        oldestTime = pair.second.timestamp;
                        oldestId = pair.first;
                    }
                }
                if (oldestId > 0)
                    m_gossipPool.erase(oldestId);
            }
        }

        m_gossipPool[item.gossipId] = item;
    }

    // Emit new gossip event to bridge
    std::string gossipTypeStr;
    switch (type)
    {
        case GossipType::CRIME: gossipTypeStr = "crime"; break;
        case GossipType::BOSS_KILL: gossipTypeStr = "boss_kill"; break;
        case GossipType::RARE_SPAWN: gossipTypeStr = "rare_spawn"; break;
        case GossipType::ACHIEVEMENT: gossipTypeStr = "achievement"; break;
        case GossipType::ZONE_EVENT: gossipTypeStr = "zone_event"; break;
        case GossipType::WARNING: gossipTypeStr = "warning"; break;
        case GossipType::REACTION: gossipTypeStr = "reaction"; break;
        default: gossipTypeStr = "general"; break;
    }
    sAliveWorldBridge->EmitGossipSpread(gossipTypeStr, text, zoneId);

    LOG_DEBUG("module.alive-world", "[GossipManager] Created gossip %u: %s", item.gossipId, text.c_str());

    return item.gossipId;
}

uint32 GossipManager::GenerateFromTemplate(GossipType type, uint32 zoneId,
                                           const std::unordered_map<std::string, std::string>& variables,
                                           uint32 importance)
{
    const GossipTemplate* tmpl = SelectTemplate(type, variables);
    if (!tmpl)
    {
        // Fallback to generic text
        std::string text = "Something interesting happened";
        auto it = variables.find("subject");
        if (it != variables.end())
            text = "Something happened with " + it->second;

        return CreateGossip(type, zoneId, text,
            it != variables.end() ? it->second : "", importance);
    }

    std::string text = ApplyTemplate(*tmpl, variables);
    std::string subject = variables.count("subject") ? variables.at("subject") : "";

    return CreateGossip(type, zoneId, text, subject, importance);
}

// ============================================================================
// GET GOSSIP
// ============================================================================

const GossipItem* GossipManager::GetGossip(uint32 gossipId) const
{
    std::lock_guard<std::mutex> lock(m_gossipMutex);
    auto it = m_gossipPool.find(gossipId);
    if (it == m_gossipPool.end())
        return nullptr;
    return &it->second;
}

std::vector<GossipItem> GossipManager::GetGossipByZone(uint32 zoneId, uint32 limit) const
{
    std::vector<GossipItem> result;

    std::lock_guard<std::mutex> lock(m_gossipMutex);
    for (const auto& pair : m_gossipPool)
    {
        if (pair.second.originZoneId == zoneId && !IsGossipExpired(pair.second))
        {
            result.push_back(pair.second);
            if (result.size() >= limit)
                break;
        }
    }

    // Sort by importance desc, then by recency
    std::sort(result.begin(), result.end(), [](const GossipItem& a, const GossipItem& b) {
        if (a.importance != b.importance)
            return a.importance > b.importance;
        return a.timestamp > b.timestamp;
    });

    return result;
}

std::vector<GossipItem> GossipManager::GetGossipByType(GossipType type, uint32 limit) const
{
    // Note: GossipItem doesn't track type directly anymore
    // This returns all non-expired gossip as a fallback
    std::vector<GossipItem> result;

    std::lock_guard<std::mutex> lock(m_gossipMutex);
    for (const auto& pair : m_gossipPool)
    {
        if (!IsGossipExpired(pair.second))
        {
            result.push_back(pair.second);
            if (result.size() >= limit)
                break;
        }
    }

    return result;
}

std::vector<GossipItem> GossipManager::GetRecentGossip(uint32 limit) const
{
    std::vector<GossipItem> result;

    std::lock_guard<std::mutex> lock(m_gossipMutex);
    for (const auto& pair : m_gossipPool)
    {
        if (!IsGossipExpired(pair.second))
            result.push_back(pair.second);
    }

    // Sort by creation time desc
    std::sort(result.begin(), result.end(), [](const GossipItem& a, const GossipItem& b) {
        return a.timestamp > b.timestamp;
    });

    if (result.size() > limit)
        result.resize(limit);

    return result;
}

std::vector<GossipItem> GossipManager::GetHotGossip(uint32 limit) const
{
    std::vector<GossipItem> result;

    std::lock_guard<std::mutex> lock(m_gossipMutex);
    for (const auto& pair : m_gossipPool)
    {
        if (!IsGossipExpired(pair.second) && pair.second.importance >= 7)
            result.push_back(pair.second);
    }

    // Sort by spread chance (combines importance and recency)
    std::sort(result.begin(), result.end(), [](const GossipItem& a, const GossipItem& b) {
        return GetGossipSpreadChance(a) > GetGossipSpreadChance(b);
    });

    if (result.size() > limit)
        result.resize(limit);

    return result;
}

// ============================================================================
// BOT KNOWLEDGE
// ============================================================================

bool GossipManager::BotKnowsGossip(uint64 botGuid, uint32 gossipId) const
{
    std::lock_guard<std::mutex> lock(m_knowledgeMutex);
    auto it = m_botKnowledge.find(botGuid);
    if (it == m_botKnowledge.end())
        return false;
    return it->second.count(gossipId) > 0;
}

void GossipManager::BotLearnGossip(uint64 botGuid, uint32 gossipId)
{
    std::lock_guard<std::mutex> lock(m_knowledgeMutex);
    m_botKnowledge[botGuid].insert(gossipId);
}

std::vector<uint32> GossipManager::GetBotKnownGossip(uint64 botGuid) const
{
    std::vector<uint32> result;

    std::lock_guard<std::mutex> lock(m_knowledgeMutex);
    auto it = m_botKnowledge.find(botGuid);
    if (it != m_botKnowledge.end())
    {
        for (uint32 id : it->second)
            result.push_back(id);
    }

    return result;
}

// ============================================================================
// SPREADING
// ============================================================================

void GossipManager::AttemptSpread(uint64 botGuid)
{
    if (!m_enabled || !m_behaviorExecutor)
        return;

    Player* bot = ObjectAccessor::FindPlayer(ObjectGuid(botGuid));
    if (!bot)
        return;

    // Get gossip this bot knows
    auto knownGossip = GetBotKnownGossip(botGuid);
    if (knownGossip.empty())
        return;

    // Filter to valid, unexpired gossip
    std::vector<const GossipItem*> validGossip;
    {
        std::lock_guard<std::mutex> lock(m_gossipMutex);
        for (uint32 id : knownGossip)
        {
            auto it = m_gossipPool.find(id);
            if (it != m_gossipPool.end() && !IsGossipExpired(it->second))
                validGossip.push_back(&it->second);
        }
    }

    if (validGossip.empty())
        return;

    // Pick a random gossip to potentially spread
    const GossipItem* gossip = validGossip[urand(0, validGossip.size() - 1)];

    // Check spread chance
    if (urand(0, 100) > GetGossipSpreadChance(*gossip))
        return;

    // Find a target
    Player* target = FindSpreadTarget(bot, gossip->gossipId);
    if (!target)
        return;

    // Execute the spread
    m_behaviorExecutor->Say(botGuid, gossip->details, BehaviorPriority::LOW);

    // Mark target as knowing this gossip
    BotLearnGossip(target->GetGUID().GetRawValue(), gossip->gossipId);

    // Update spread radius
    {
        std::lock_guard<std::mutex> lock(m_gossipMutex);
        auto it = m_gossipPool.find(gossip->gossipId);
        if (it != m_gossipPool.end())
            it->second.spreadRadius++;
    }

    m_totalSpreads++;

    // Emit gossip spread event to bridge
    std::string gossipTypeStr;
    switch (static_cast<GossipType>(gossip->type))
    {
        case GossipType::CRIME: gossipTypeStr = "crime"; break;
        case GossipType::BOSS_KILL: gossipTypeStr = "boss_kill"; break;
        case GossipType::RARE_SPAWN: gossipTypeStr = "rare_spawn"; break;
        case GossipType::ACHIEVEMENT: gossipTypeStr = "achievement"; break;
        case GossipType::ZONE_EVENT: gossipTypeStr = "zone_event"; break;
        case GossipType::WARNING: gossipTypeStr = "warning"; break;
        case GossipType::REACTION: gossipTypeStr = "reaction"; break;
        default: gossipTypeStr = "general"; break;
    }
    sAliveWorldBridge->EmitGossipSpread(gossipTypeStr, gossip->details, gossip->originZoneId);

    GOSSIP_LOG_DEBUG("Bot %s spread gossip %u to %s",
        bot->GetName().c_str(), gossip->gossipId, target->GetName().c_str());
}

void GossipManager::ForceSpread(uint64 botGuid, uint32 gossipId)
{
    if (!m_behaviorExecutor)
        return;

    const GossipItem* gossip = GetGossip(gossipId);
    if (!gossip)
        return;

    m_behaviorExecutor->SpreadGossip(botGuid, gossip->details, gossipId);
}

Player* GossipManager::FindSpreadTarget(Player* source, uint32 gossipId)
{
    if (!source)
        return nullptr;

    std::vector<Player*> candidates;

    for (auto& pair : source->GetMap()->GetPlayers())
    {
        Player* other = pair.GetSource();
        if (other == source)
            continue;

        // Must be within range
        if (!source->IsWithinDist(other, m_spreadRange, false))
            continue;

        // Prefer bots (they're the gossip network)
        if (!sPlayerbotsMgr.GetPlayerbotAI(other))
            continue;

        // Must not already know this gossip
        if (BotKnowsGossip(other->GetGUID().GetRawValue(), gossipId))
            continue;

        candidates.push_back(other);
    }

    if (candidates.empty())
        return nullptr;

    return candidates[urand(0, candidates.size() - 1)];
}

// ============================================================================
// UPDATE
// ============================================================================

void GossipManager::Update(uint32 diff)
{
    if (!m_enabled)
        return;

    // Spread timer
    m_spreadTimer += diff;
    if (m_spreadTimer >= m_spreadIntervalMs)
    {
        m_spreadTimer = 0;

        // Pick some random bots to potentially spread gossip
        // This is called from AliveWorldManager::Update which has access to all bots
        // The actual spreading is triggered per-bot via AttemptSpread
    }

    // Cleanup timer
    m_cleanupTimer += diff;
    if (m_cleanupTimer >= 60000) // Every minute
    {
        m_cleanupTimer = 0;
        CleanupExpired();
    }
}

void GossipManager::CleanupExpired()
{
    std::lock_guard<std::mutex> lock(m_gossipMutex);

    std::vector<uint32> toRemove;
    for (const auto& pair : m_gossipPool)
    {
        if (IsGossipExpired(pair.second))
            toRemove.push_back(pair.first);
    }

    for (uint32 id : toRemove)
        m_gossipPool.erase(id);

    if (!toRemove.empty())
        LOG_DEBUG("module.alive-world", "[GossipManager] Cleaned up %u expired gossip items", static_cast<uint32>(toRemove.size()));
}

// ============================================================================
// TEMPLATE PROCESSING
// ============================================================================

std::string GossipManager::ApplyTemplate(const GossipTemplate& tmpl,
                                         const std::unordered_map<std::string, std::string>& vars)
{
    std::string result = tmpl.pattern;

    for (const auto& var : vars)
    {
        std::string placeholder = "{" + var.first + "}";
        size_t pos = 0;
        while ((pos = result.find(placeholder, pos)) != std::string::npos)
        {
            result.replace(pos, placeholder.length(), var.second);
            pos += var.second.length();
        }
    }

    return result;
}

const GossipTemplate* GossipManager::SelectTemplate(GossipType type,
                                                    const std::unordered_map<std::string, std::string>& /*vars*/)
{
    std::vector<const GossipTemplate*> matching;

    for (const auto& tmpl : m_templates)
    {
        if (tmpl.type == type)
            matching.push_back(&tmpl);
    }

    if (matching.empty())
        return nullptr;

    // Weighted random selection
    uint32 totalWeight = 0;
    for (const auto* tmpl : matching)
        totalWeight += tmpl->probability;

    uint32 roll = urand(0, totalWeight - 1);
    uint32 cumulative = 0;

    for (const auto* tmpl : matching)
    {
        cumulative += tmpl->probability;
        if (roll < cumulative)
            return tmpl;
    }

    return matching.back();
}

// ============================================================================
// STATS
// ============================================================================

uint32 GossipManager::GetTotalGossipCount() const
{
    std::lock_guard<std::mutex> lock(m_gossipMutex);
    return static_cast<uint32>(m_gossipPool.size());
}

uint32 GossipManager::GetActiveGossipCount() const
{
    std::lock_guard<std::mutex> lock(m_gossipMutex);
    uint32 count = 0;
    for (const auto& pair : m_gossipPool)
    {
        if (!IsGossipExpired(pair.second))
            count++;
    }
    return count;
}

} // namespace AliveWorld
