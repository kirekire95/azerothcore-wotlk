/*
 * mod-alive-world - Gossip Template Manager
 * Copyright (C) 2025 EC
 */

#include "GossipTemplateManager.h"
#include "AIDecisionEngine.h"
#include "AliveWorldManager.h"  // For ALIVE_LOG_* macros

#include "DatabaseEnv.h"
#include "DBCStores.h"  // For AreaTableEntry
#include "QueryResult.h"  // For Field, ResultSet

#include <algorithm>
#include <numeric>

namespace AliveWorld
{

GossipTemplateManager::GossipTemplateManager()
    : m_loaded(false)
    , m_totalTemplates(0)
    , m_aiTweakEnabled(false)
    , m_aiTweakChance(30)  // 30% chance to tweak by default
    , m_tweakedCount(0)
    , m_aiEngine(nullptr)
{
    // Seed the random number generator
    std::random_device rd;
    m_rng.seed(rd());
}

void GossipTemplateManager::Initialize()
{
    LoadTemplatesFromDB();

    // Also load broadcast_text extracted patterns if available
    LoadBroadcastPatterns();

    // If no templates loaded from DB, use defaults
    if (m_totalTemplates == 0)
    {
        ALIVE_LOG_INFO("No gossip templates found in database, loading defaults");
        LoadDefaultTemplates();
    }

    ALIVE_LOG_INFO("GossipTemplateManager initialized with %u templates", m_totalTemplates);
    m_loaded = true;
}

void GossipTemplateManager::LoadTemplatesFromDB()
{
    ALIVE_LOG_DEBUG("Loading gossip templates from database...");

    // Query the ec_gossip_templates table
    QueryResult result = WorldDatabase.Query(
        "SELECT id, event_type, template_text, weight, min_importance, max_importance, "
        "is_exclamation, category FROM ec_gossip_templates WHERE enabled = 1"
    );

    if (!result)
    {
        ALIVE_LOG_DEBUG("No templates found in ec_gossip_templates table");
        return;
    }

    std::unique_lock<std::shared_mutex> lock(m_mutex);
    m_templates.clear();
    m_totalTemplates = 0;

    do
    {
        Field* fields = result->Fetch();

        GossipTemplate tmpl;
        tmpl.id = fields[0].Get<uint32>();
        tmpl.eventType = StringToEventType(fields[1].Get<std::string>());
        tmpl.templateText = fields[2].Get<std::string>();
        tmpl.weight = fields[3].Get<uint32>();
        tmpl.minImportance = fields[4].Get<uint8>();
        tmpl.maxImportance = fields[5].Get<uint8>();
        tmpl.isExclamation = fields[6].Get<bool>();
        tmpl.category = fields[7].Get<std::string>();

        // Validate
        if (tmpl.templateText.empty())
            continue;

        if (tmpl.weight == 0)
            tmpl.weight = 1;

        m_templates[tmpl.eventType].push_back(tmpl);
        m_totalTemplates++;

    } while (result->NextRow());

    ALIVE_LOG_INFO("Loaded %u gossip templates from database", m_totalTemplates);
}

void GossipTemplateManager::LoadBroadcastPatterns()
{
    ALIVE_LOG_DEBUG("Loading broadcast_text gossip patterns...");

    // Query the ec_broadcast_gossip_patterns table (created by extraction script)
    QueryResult result = WorldDatabase.Query(
        "SELECT id, template_text, weight, pattern_type, category "
        "FROM ec_broadcast_gossip_patterns WHERE enabled = 1 "
        "AND LENGTH(template_text) > 10"
    );

    if (!result)
    {
        ALIVE_LOG_DEBUG("No broadcast gossip patterns found (table may not exist yet)");
        return;
    }

    std::unique_lock<std::shared_mutex> lock(m_mutex);
    uint32 patternCount = 0;

    do
    {
        Field* fields = result->Fetch();

        GossipTemplate tmpl;
        tmpl.id = 10000 + fields[0].Get<uint32>();  // Offset to avoid ID conflicts
        tmpl.eventType = WorldEventType::GOSSIP;
        tmpl.templateText = fields[1].Get<std::string>();
        tmpl.weight = fields[2].Get<uint32>();
        tmpl.minImportance = 1;
        tmpl.maxImportance = 7;  // Gossip is casual, not urgent
        tmpl.isExclamation = (tmpl.templateText.find('!') != std::string::npos);
        tmpl.category = fields[4].Get<std::string>();

        // Skip if text is too long or empty
        if (tmpl.templateText.empty() || tmpl.templateText.length() > 255)
            continue;

        // Clean up any remaining WoW placeholders we might have missed
        std::string& text = tmpl.templateText;

        // Replace remaining placeholders
        size_t pos;
        while ((pos = text.find("$n")) != std::string::npos)
            text.replace(pos, 2, "{primary}");
        while ((pos = text.find("$N")) != std::string::npos)
            text.replace(pos, 2, "{primary}");
        while ((pos = text.find("$r")) != std::string::npos)
            text.replace(pos, 2, "{secondary}");
        while ((pos = text.find("$R")) != std::string::npos)
            text.replace(pos, 2, "{secondary}");
        while ((pos = text.find("$c")) != std::string::npos)
            text.replace(pos, 2, "{secondary}");
        while ((pos = text.find("$C")) != std::string::npos)
            text.replace(pos, 2, "{secondary}");
        while ((pos = text.find("$g")) != std::string::npos)
        {
            // Gender placeholder $g male : female; - just use male version
            size_t colonPos = text.find(':', pos);
            size_t semiPos = text.find(';', pos);
            if (colonPos != std::string::npos && semiPos != std::string::npos && colonPos < semiPos)
            {
                std::string maleText = text.substr(pos + 3, colonPos - pos - 3);
                text.replace(pos, semiPos - pos + 1, maleText);
            }
            else
            {
                text.erase(pos, 2);  // Just remove $g if format is wrong
            }
        }

        m_templates[WorldEventType::GOSSIP].push_back(tmpl);
        patternCount++;
        m_totalTemplates++;

    } while (result->NextRow());

    ALIVE_LOG_INFO("Loaded %u broadcast_text gossip patterns", patternCount);
}

void GossipTemplateManager::LoadDefaultTemplates()
{
    std::unique_lock<std::shared_mutex> lock(m_mutex);

    // BOSS_KILLED templates
    m_templates[WorldEventType::BOSS_KILLED] = {
        { 1, WorldEventType::BOSS_KILLED, "{secondary} has been defeated!", 10, 1, 10, true, "combat" },
        { 2, WorldEventType::BOSS_KILLED, "The mighty {secondary} has fallen!", 10, 5, 10, true, "combat" },
        { 3, WorldEventType::BOSS_KILLED, "Heroes have slain {secondary} in {zone}!", 8, 7, 10, true, "combat" },
        { 4, WorldEventType::BOSS_KILLED, "Victory! {secondary} is no more!", 6, 5, 10, true, "combat" },
        { 5, WorldEventType::BOSS_KILLED, "Word spreads of {secondary}'s defeat.", 5, 1, 5, false, "combat" },
    };

    // PLAYER_KILLED_PLAYER templates
    m_templates[WorldEventType::PLAYER_KILLED_PLAYER] = {
        { 10, WorldEventType::PLAYER_KILLED_PLAYER, "{primary} slew {secondary} in battle!", 10, 1, 10, true, "combat" },
        { 11, WorldEventType::PLAYER_KILLED_PLAYER, "{secondary} has fallen to {primary}!", 8, 3, 10, true, "combat" },
        { 12, WorldEventType::PLAYER_KILLED_PLAYER, "A fierce battle ended with {secondary}'s defeat.", 5, 1, 5, false, "combat" },
        { 13, WorldEventType::PLAYER_KILLED_PLAYER, "{primary} claims another victim in {zone}.", 5, 5, 10, false, "combat" },
    };

    // RARE_SPAWN templates
    m_templates[WorldEventType::RARE_SPAWN] = {
        { 20, WorldEventType::RARE_SPAWN, "Rare creature spotted: {primary}!", 10, 1, 10, true, "world" },
        { 21, WorldEventType::RARE_SPAWN, "A rare {primary} has been seen in {zone}!", 10, 5, 10, true, "world" },
        { 22, WorldEventType::RARE_SPAWN, "Hunters, take note! {primary} lurks nearby.", 5, 1, 5, false, "world" },
    };

    // WORLD_BOSS_SPAWN templates
    m_templates[WorldEventType::WORLD_BOSS_SPAWN] = {
        { 30, WorldEventType::WORLD_BOSS_SPAWN, "World boss {primary} has appeared!", 10, 8, 10, true, "world" },
        { 31, WorldEventType::WORLD_BOSS_SPAWN, "The ground trembles... {primary} awakens!", 10, 8, 10, true, "world" },
        { 32, WorldEventType::WORLD_BOSS_SPAWN, "Gather your allies! {primary} threatens {zone}!", 8, 8, 10, true, "world" },
    };

    // GUARD_CALLED templates
    m_templates[WorldEventType::GUARD_CALLED] = {
        { 40, WorldEventType::GUARD_CALLED, "Guards pursue {primary}!", 10, 1, 10, true, "crime" },
        { 41, WorldEventType::GUARD_CALLED, "Criminal {primary} spotted in {zone}!", 8, 5, 10, true, "crime" },
        { 42, WorldEventType::GUARD_CALLED, "Wanted: {primary} for crimes in {zone}.", 5, 3, 7, false, "crime" },
        { 43, WorldEventType::GUARD_CALLED, "The guards are after {primary}!", 10, 1, 5, true, "crime" },
        { 44, WorldEventType::GUARD_CALLED, "There's trouble in {zone}! Someone attacked a civilian!", 5, 1, 3, true, "crime" },
    };

    // ZONE_BECAME_ACTIVE templates
    m_templates[WorldEventType::ZONE_BECAME_ACTIVE] = {
        { 50, WorldEventType::ZONE_BECAME_ACTIVE, "Activity reported in the area!", 10, 1, 10, true, "world" },
        { 51, WorldEventType::ZONE_BECAME_ACTIVE, "{zone} is bustling with activity.", 5, 1, 5, false, "world" },
        { 52, WorldEventType::ZONE_BECAME_ACTIVE, "Many adventurers gather in {zone}.", 5, 3, 7, false, "world" },
    };

    // FACTION_CONFLICT templates
    m_templates[WorldEventType::FACTION_CONFLICT] = {
        { 60, WorldEventType::FACTION_CONFLICT, "Tensions rise between factions in {zone}!", 10, 5, 10, true, "world" },
        { 61, WorldEventType::FACTION_CONFLICT, "Conflict brews in {zone}.", 5, 1, 5, false, "world" },
    };

    // MERCHANT_EVENT templates
    m_templates[WorldEventType::MERCHANT_EVENT] = {
        { 70, WorldEventType::MERCHANT_EVENT, "A merchant has arrived with rare goods!", 10, 5, 10, true, "social" },
        { 71, WorldEventType::MERCHANT_EVENT, "Trading opportunities in {zone}.", 5, 1, 5, false, "social" },
    };

    // WEATHER_CHANGE templates
    m_templates[WorldEventType::WEATHER_CHANGE] = {
        { 80, WorldEventType::WEATHER_CHANGE, "The weather is changing in {zone}.", 10, 1, 5, false, "world" },
        { 81, WorldEventType::WEATHER_CHANGE, "Storm clouds gather over {zone}!", 5, 5, 10, true, "world" },
    };

    // GOSSIP templates - Generic chatter patterns (until broadcast_text extraction runs)
    m_templates[WorldEventType::GOSSIP] = {
        { 90, WorldEventType::GOSSIP, "Have you heard the latest?", 10, 1, 5, false, "gossip" },
        { 91, WorldEventType::GOSSIP, "I heard something interesting...", 10, 1, 5, false, "gossip" },
        { 92, WorldEventType::GOSSIP, "Did you hear about {primary}?", 8, 3, 7, false, "gossip" },
        { 93, WorldEventType::GOSSIP, "They say strange things are happening in {zone}.", 7, 1, 5, false, "gossip" },
        { 94, WorldEventType::GOSSIP, "Word is that {primary} has been busy.", 6, 3, 7, false, "gossip" },
        { 95, WorldEventType::GOSSIP, "Rumors say {secondary} was spotted near {zone}.", 5, 3, 7, false, "gossip" },
        { 96, WorldEventType::GOSSIP, "I overheard someone talking about {zone}...", 5, 1, 5, false, "gossip" },
        { 97, WorldEventType::GOSSIP, "Have you heard about the goings-on in {zone}?", 7, 1, 5, false, "gossip" },
    };

    // Count total
    m_totalTemplates = 0;
    for (const auto& pair : m_templates)
    {
        m_totalTemplates += static_cast<uint32>(pair.second.size());
    }

    ALIVE_LOG_INFO("Loaded %u default gossip templates", m_totalTemplates);
}

void GossipTemplateManager::AddTemplate(const GossipTemplate& tmpl)
{
    std::unique_lock<std::shared_mutex> lock(m_mutex);
    m_templates[tmpl.eventType].push_back(tmpl);
    m_totalTemplates++;
}

uint32 GossipTemplateManager::GetTemplateCount() const
{
    return m_totalTemplates;
}

uint32 GossipTemplateManager::GetTemplateCountForType(WorldEventType type) const
{
    std::shared_lock<std::shared_mutex> lock(m_mutex);
    auto it = m_templates.find(type);
    if (it == m_templates.end())
        return 0;
    return static_cast<uint32>(it->second.size());
}

std::string GossipTemplateManager::GenerateHeadline(const WorldEvent& event) const
{
    // Get zone name if we have a zoneId
    std::string zoneName = "the area";
    if (event.zoneId > 0)
    {
        // Try to get zone name from AreaTableEntry
        if (const AreaTableEntry* area = sAreaTableStore.LookupEntry(event.zoneId))
        {
            zoneName = area->area_name[0];  // English name
        }
    }

    return GenerateHeadline(event.type, event.primaryName, event.secondaryName,
                            zoneName, event.importance);
}

std::string GossipTemplateManager::GenerateHeadline(WorldEventType type,
                                                     const std::string& primaryName,
                                                     const std::string& secondaryName,
                                                     const std::string& zoneName,
                                                     uint8 importance) const
{
    const GossipTemplate* tmpl = SelectTemplate(type, importance);

    if (!tmpl)
    {
        // Fallback: generate a basic headline
        return primaryName.empty() ? "Something happened!" : primaryName + " did something!";
    }

    return SubstitutePlaceholders(tmpl->templateText, primaryName, secondaryName, zoneName);
}

const GossipTemplate* GossipTemplateManager::SelectTemplate(WorldEventType type, uint8 importance) const
{
    std::shared_lock<std::shared_mutex> lock(m_mutex);

    auto it = m_templates.find(type);
    if (it == m_templates.end() || it->second.empty())
        return nullptr;

    const auto& templates = it->second;

    // Filter by importance range and calculate total weight
    std::vector<const GossipTemplate*> eligible;
    std::vector<uint32> weights;
    uint32 totalWeight = 0;

    for (const auto& tmpl : templates)
    {
        if (importance >= tmpl.minImportance && importance <= tmpl.maxImportance)
        {
            eligible.push_back(&tmpl);
            weights.push_back(tmpl.weight);
            totalWeight += tmpl.weight;
        }
    }

    if (eligible.empty())
    {
        // No templates match importance, fall back to any template
        for (const auto& tmpl : templates)
        {
            eligible.push_back(&tmpl);
            weights.push_back(tmpl.weight);
            totalWeight += tmpl.weight;
        }
    }

    if (eligible.empty() || totalWeight == 0)
        return nullptr;

    // Weighted random selection
    std::uniform_int_distribution<uint32> dist(0, totalWeight - 1);
    uint32 roll = dist(m_rng);

    uint32 cumulative = 0;
    for (size_t i = 0; i < eligible.size(); ++i)
    {
        cumulative += weights[i];
        if (roll < cumulative)
            return eligible[i];
    }

    return eligible.back();
}

std::string GossipTemplateManager::SubstitutePlaceholders(const std::string& tmpl,
                                                          const std::string& primary,
                                                          const std::string& secondary,
                                                          const std::string& zone) const
{
    std::string result = tmpl;

    // Replace {primary} with primary name
    size_t pos = result.find("{primary}");
    while (pos != std::string::npos)
    {
        result.replace(pos, 9, primary.empty() ? "someone" : primary);
        pos = result.find("{primary}", pos);
    }

    // Replace {secondary} with secondary name
    pos = result.find("{secondary}");
    while (pos != std::string::npos)
    {
        result.replace(pos, 11, secondary.empty() ? "something" : secondary);
        pos = result.find("{secondary}", pos);
    }

    // Replace {zone} with zone name
    pos = result.find("{zone}");
    while (pos != std::string::npos)
    {
        result.replace(pos, 6, zone.empty() ? "the area" : zone);
        pos = result.find("{zone}", pos);
    }

    return result;
}

std::string GossipTemplateManager::EventTypeToString(WorldEventType type)
{
    switch (type)
    {
        case WorldEventType::BOSS_KILLED:           return "BOSS_KILLED";
        case WorldEventType::PLAYER_KILLED_PLAYER:  return "PLAYER_KILLED_PLAYER";
        case WorldEventType::RARE_SPAWN:            return "RARE_SPAWN";
        case WorldEventType::WORLD_BOSS_SPAWN:      return "WORLD_BOSS_SPAWN";
        case WorldEventType::GUARD_CALLED:          return "GUARD_CALLED";
        case WorldEventType::ZONE_BECAME_ACTIVE:    return "ZONE_BECAME_ACTIVE";
        case WorldEventType::GROUP_FORMED:          return "GROUP_FORMED";
        case WorldEventType::FESTIVAL_START:        return "FESTIVAL_START";
        case WorldEventType::FESTIVAL_END:          return "FESTIVAL_END";
        case WorldEventType::FACTION_CONFLICT:      return "FACTION_CONFLICT";
        case WorldEventType::MERCHANT_EVENT:        return "MERCHANT_EVENT";
        case WorldEventType::WEATHER_CHANGE:        return "WEATHER_CHANGE";
        case WorldEventType::HOLIDAY_EVENT:         return "HOLIDAY_EVENT";
        case WorldEventType::BOT_ACHIEVEMENT:       return "BOT_ACHIEVEMENT";
        case WorldEventType::GUILD_EVENT:           return "GUILD_EVENT";
        case WorldEventType::GOSSIP:                return "GOSSIP";
        default:                                     return "UNKNOWN";
    }
}

WorldEventType GossipTemplateManager::StringToEventType(const std::string& str)
{
    if (str == "BOSS_KILLED")           return WorldEventType::BOSS_KILLED;
    if (str == "PLAYER_KILLED_PLAYER")  return WorldEventType::PLAYER_KILLED_PLAYER;
    if (str == "RARE_SPAWN")            return WorldEventType::RARE_SPAWN;
    if (str == "WORLD_BOSS_SPAWN")      return WorldEventType::WORLD_BOSS_SPAWN;
    if (str == "GUARD_CALLED")          return WorldEventType::GUARD_CALLED;
    if (str == "ZONE_BECAME_ACTIVE")    return WorldEventType::ZONE_BECAME_ACTIVE;
    if (str == "GROUP_FORMED")          return WorldEventType::GROUP_FORMED;
    if (str == "FESTIVAL_START")        return WorldEventType::FESTIVAL_START;
    if (str == "FESTIVAL_END")          return WorldEventType::FESTIVAL_END;
    if (str == "FACTION_CONFLICT")      return WorldEventType::FACTION_CONFLICT;
    if (str == "MERCHANT_EVENT")        return WorldEventType::MERCHANT_EVENT;
    if (str == "WEATHER_CHANGE")        return WorldEventType::WEATHER_CHANGE;
    if (str == "HOLIDAY_EVENT")         return WorldEventType::HOLIDAY_EVENT;
    if (str == "BOT_ACHIEVEMENT")       return WorldEventType::BOT_ACHIEVEMENT;
    if (str == "GUILD_EVENT")           return WorldEventType::GUILD_EVENT;
    if (str == "GOSSIP")                return WorldEventType::GOSSIP;

    // Default
    return WorldEventType::ZONE_BECAME_ACTIVE;
}

void GossipTemplateManager::GenerateHeadlineWithTweak(const WorldEvent& event,
                                                      const BotPersonality& personality,
                                                      std::function<void(const std::string&, bool)> callback)
{
    // First generate the base headline
    std::string baseHeadline = GenerateHeadline(event);

    // Check if AI tweaking is enabled and we have the engine
    if (!m_aiTweakEnabled || !m_aiEngine)
    {
        ALIVE_LOG_DEBUG("AI tweak disabled or no engine, using base headline");
        callback(baseHeadline, false);
        return;
    }

    // Roll for tweak chance
    std::uniform_int_distribution<uint32> dist(0, 99);
    if (dist(m_rng) >= m_aiTweakChance)
    {
        ALIVE_LOG_DEBUG("AI tweak roll failed (chance: %u%%), using base headline", m_aiTweakChance);
        callback(baseHeadline, false);
        return;
    }

    // Get zone name
    std::string zoneName = "the area";
    if (event.zoneId > 0)
    {
        if (const AreaTableEntry* area = sAreaTableStore.LookupEntry(event.zoneId))
        {
            zoneName = area->area_name[0];
        }
    }

    ALIVE_LOG_DEBUG("Attempting AI tweak for headline: %s", baseHeadline.c_str());

    // Queue the tweak request
    // The callback captures the base headline as fallback
    m_aiEngine->QueueGossipTweak(
        baseHeadline,
        personality,
        zoneName,
        [this, baseHeadline, callback](const std::string& tweakedText, bool success)
        {
            if (success && !tweakedText.empty())
            {
                m_tweakedCount++;
                ALIVE_LOG_DEBUG("AI tweaked headline: %s", tweakedText.c_str());
                callback(tweakedText, true);
            }
            else
            {
                ALIVE_LOG_DEBUG("AI tweak failed, using base headline");
                callback(baseHeadline, false);
            }
        }
    );
}

} // namespace AliveWorld
