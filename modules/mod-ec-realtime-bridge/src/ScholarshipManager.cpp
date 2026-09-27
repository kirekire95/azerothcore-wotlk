/*
 * mod-ec-realtime-bridge - Scholarship Manager Implementation
 * Copyright (C) 2025 EC
 */

#include "ScholarshipManager.h"
#include "Log.h"
#include <chrono>
#include <random>

namespace ECRealtimeBridge
{

// League ordering for comparisons
const std::map<std::string, int> ScholarshipManager::_leagueOrder = {
    {"UNRANKED", 0},
    {"BRONZE", 1},
    {"SILVER", 2},
    {"GOLD", 3},
    {"PLATINUM", 4},
    {"DIAMOND", 5},
    {"MASTER", 6},
    {"GRANDMASTER", 7}
};

ScholarshipManager* ScholarshipManager::Instance()
{
    static ScholarshipManager instance;
    return &instance;
}

ScholarshipManager::ScholarshipManager()
{
    // State initialized by default constructor
}

// ============================================================================
// STATE ACCESS
// ============================================================================

ScholarshipState ScholarshipManager::GetState() const
{
    std::lock_guard<std::mutex> lock(_stateMutex);
    return _state;
}

bool ScholarshipManager::HasScholarship() const
{
    std::lock_guard<std::mutex> lock(_stateMutex);
    return _state.IsValid();
}

std::string ScholarshipManager::GetScholarRank() const
{
    std::lock_guard<std::mutex> lock(_stateMutex);
    return _state.scholarRank;
}

uint32 ScholarshipManager::GetCurrentStreak() const
{
    std::lock_guard<std::mutex> lock(_stateMutex);
    return _state.currentStreak;
}

std::vector<std::string> ScholarshipManager::GetDisciplines() const
{
    std::lock_guard<std::mutex> lock(_stateMutex);
    return _state.disciplines;
}

float ScholarshipManager::GetIntensity() const
{
    std::lock_guard<std::mutex> lock(_stateMutex);
    return _state.GetIntensity();
}

int ScholarshipManager::GetLeagueRank(const std::string& league) const
{
    auto it = _leagueOrder.find(league);
    if (it != _leagueOrder.end())
        return it->second;
    return 0;
}

bool ScholarshipManager::QualifiesForBonus(const std::string& minLeague, uint32 minStreak) const
{
    std::lock_guard<std::mutex> lock(_stateMutex);

    if (!_state.IsValid())
        return false;

    // Check league requirement
    if (!minLeague.empty())
    {
        int currentRank = GetLeagueRank(_state.league);
        int requiredRank = GetLeagueRank(minLeague);
        if (currentRank < requiredRank)
            return false;
    }

    // Check streak requirement
    if (minStreak > 0 && _state.currentStreak < minStreak)
        return false;

    return true;
}

// ============================================================================
// NPC DIALOGUE HELPERS
// ============================================================================

std::vector<std::string> ScholarshipManager::GetDialogueSnippets() const
{
    std::lock_guard<std::mutex> lock(_stateMutex);
    std::vector<std::string> snippets;

    if (!_state.IsValid())
        return snippets;

    // Rank-based snippets
    if (_state.league == "GRANDMASTER")
    {
        snippets.push_back("Your mastery of the Eastern Tongues is legendary, Grand Archivist.");
    }
    else if (_state.league == "MASTER")
    {
        snippets.push_back("Few have achieved your level of scholarly excellence, Master Scholar.");
    }
    else if (_state.league == "DIAMOND")
    {
        snippets.push_back("You've proven yourself a true Expert Scholar.");
    }
    else if (_state.league == "PLATINUM")
    {
        snippets.push_back("Your dedication to the scholarly arts is evident, Adept Scholar.");
    }
    else if (_state.league == "GOLD")
    {
        snippets.push_back("You progress well in your studies, Journeyman Scholar.");
    }

    // Streak-based snippets
    if (_state.currentStreak >= 30)
    {
        char buf[256];
        snprintf(buf, sizeof(buf), "Your dedication is remarkable - %u days of consistent study!", _state.currentStreak);
        snippets.push_back(buf);
    }
    else if (_state.currentStreak >= 14)
    {
        char buf[256];
        snprintf(buf, sizeof(buf), "Two weeks of daily practice! Your discipline rivals the monks.");
        snippets.push_back(buf);
    }
    else if (_state.currentStreak >= 7)
    {
        char buf[256];
        snprintf(buf, sizeof(buf), "I see you've maintained your studies for %u days. Commendable discipline.", _state.currentStreak);
        snippets.push_back(buf);
    }

    // Discipline-based snippets
    for (const auto& discipline : _state.disciplines)
    {
        if (discipline == "Kinship Lore")
            snippets.push_back("Your understanding of kinship bonds runs deep.");
        else if (discipline == "Diplomatic Protocols")
            snippets.push_back("You speak with the grace of a seasoned diplomat.");
        else if (discipline == "Cultural Customs")
            snippets.push_back("Your knowledge of customs and traditions is impressive.");
        else if (discipline == "Arcane Numerology")
            snippets.push_back("Numbers bend to your will, scholar.");
    }

    // Arena-based snippets
    if (_state.arenaWins >= 50)
    {
        snippets.push_back("Your victories in scholarly combat are legendary across the realm.");
    }
    else if (_state.arenaWins >= 10)
    {
        snippets.push_back("Your victories in scholarly combat are well known.");
    }

    // Vocabulary mastery
    if (_state.vocabTotal > 0)
    {
        float masteryPercent = (_state.vocabMastered * 100.0f) / _state.vocabTotal;
        if (masteryPercent >= 80.0f)
        {
            snippets.push_back("Your vocabulary mastery is exceptional.");
        }
        else if (masteryPercent >= 50.0f)
        {
            snippets.push_back("Your word knowledge grows stronger each day.");
        }
    }

    return snippets;
}

std::string ScholarshipManager::GetRandomDialogueSnippet() const
{
    auto snippets = GetDialogueSnippets();
    if (snippets.empty())
        return "";

    // Random selection
    static std::random_device rd;
    static std::mt19937 gen(rd());
    std::uniform_int_distribution<size_t> dist(0, snippets.size() - 1);

    return snippets[dist(gen)];
}

std::string ScholarshipManager::GetAIPromptContext() const
{
    std::lock_guard<std::mutex> lock(_stateMutex);

    if (!_state.IsValid())
        return "The player's scholarship status is unknown.";

    std::string context;
    context += "Scholar Rank: " + _state.scholarRank + "\n";

    char buf[256];
    snprintf(buf, sizeof(buf), "Study Streak: %u days\n", _state.currentStreak);
    context += buf;

    if (!_state.disciplines.empty())
    {
        context += "Strong in: ";
        for (size_t i = 0; i < _state.disciplines.size() && i < 3; ++i)
        {
            if (i > 0) context += ", ";
            context += _state.disciplines[i];
        }
        context += "\n";
    }

    if (!_state.weakDisciplines.empty())
    {
        context += "Needs practice: ";
        for (size_t i = 0; i < _state.weakDisciplines.size() && i < 2; ++i)
        {
            if (i > 0) context += ", ";
            context += _state.weakDisciplines[i];
        }
        context += "\n";
    }

    if (_state.vocabTotal > 0)
    {
        float masteryPercent = (_state.vocabMastered * 100.0f) / _state.vocabTotal;
        snprintf(buf, sizeof(buf), "Vocabulary: %.0f%% mastered (%.0f%% weekly accuracy)\n",
            masteryPercent, _state.weeklyAccuracy);
        context += buf;
    }

    if (_state.arenaWins > 0 || _state.arenaLosses > 0)
    {
        snprintf(buf, sizeof(buf), "Arena: %uW/%uL (%.1f%% win rate)\n",
            _state.arenaWins, _state.arenaLosses, _state.winRate);
        context += buf;
    }

    return context;
}

// ============================================================================
// EVENT HANDLING
// ============================================================================

void ScholarshipManager::HandleLifeEvent(const std::string& type, const std::string& jsonPayload)
{
    LOG_DEBUG("module", "[Scholarship] Received life event: {} payload_size={}", type, jsonPayload.size());

    if (type == "scholarship_rank_changed" ||
        type == "scholarship_streak_milestone" ||
        type == "scholarship_streak_broken" ||
        type == "scholarship_lesson_complete" ||
        type == "scholarship_chapter_complete" ||
        type == "scholarship_arena_result" ||
        type == "scholarship_achievement" ||
        type == "scholarship_milestone")
    {
        ParseScholarshipEvent(jsonPayload);
    }
    else if (type == "scholarship_snapshot")
    {
        ParseSnapshotEvent(jsonPayload);
    }
    else
    {
        LOG_DEBUG("module", "[Scholarship] Unknown life event type: {}", type);
    }
}

void ScholarshipManager::ParseScholarshipEvent(const std::string& jsonPayload)
{
    // Extract fields from JSON
    std::string scholarRank = ExtractJsonString(jsonPayload, "scholarRank");
    std::string discipline = ExtractJsonString(jsonPayload, "discipline");
    float intensity = static_cast<float>(ExtractJsonDouble(jsonPayload, "intensity"));
    std::string league = ExtractJsonString(jsonPayload, "league");
    int64_t tier = ExtractJsonInt(jsonPayload, "tier");
    float percentile = static_cast<float>(ExtractJsonDouble(jsonPayload, "percentile"));
    int64_t streak = ExtractJsonInt(jsonPayload, "streak");

    UpdateFromScholarshipEvent(
        scholarRank,
        discipline,
        intensity,
        league,
        static_cast<uint8>(tier),
        percentile,
        static_cast<uint32>(streak)
    );
}

void ScholarshipManager::ParseSnapshotEvent(const std::string& jsonPayload)
{
    std::lock_guard<std::mutex> lock(_stateMutex);

    _state.scholarRank = ExtractJsonString(jsonPayload, "scholarRank");
    _state.league = ExtractJsonString(jsonPayload, "league");
    _state.tier = static_cast<uint8>(ExtractJsonInt(jsonPayload, "tier"));
    _state.percentile = static_cast<float>(ExtractJsonDouble(jsonPayload, "percentile"));
    _state.level = static_cast<uint32>(ExtractJsonInt(jsonPayload, "level"));
    _state.totalXp = static_cast<uint32>(ExtractJsonInt(jsonPayload, "totalXp"));
    _state.currentStreak = static_cast<uint32>(ExtractJsonInt(jsonPayload, "currentStreak"));
    _state.longestStreak = static_cast<uint32>(ExtractJsonInt(jsonPayload, "longestStreak"));
    _state.arenaWins = static_cast<uint32>(ExtractJsonInt(jsonPayload, "matchesWon"));
    _state.winRate = static_cast<float>(ExtractJsonDouble(jsonPayload, "winRate"));

    _state.lastUpdated = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count();

    LOG_INFO("module", "[Scholarship] State updated: {} ({} T{}) streak={} days",
        _state.scholarRank, _state.league, _state.tier, _state.currentStreak);
}

void ScholarshipManager::UpdateFromScholarshipEvent(
    const std::string& scholarRank,
    const std::string& discipline,
    float intensity,
    const std::string& league,
    uint8 tier,
    float percentile,
    uint32 streak)
{
    std::lock_guard<std::mutex> lock(_stateMutex);

    _state.scholarRank = scholarRank;
    _state.league = league;
    _state.tier = tier;
    _state.percentile = percentile;
    _state.currentStreak = streak;

    // Add discipline to list if not already present
    if (!discipline.empty())
    {
        bool found = false;
        for (const auto& d : _state.disciplines)
        {
            if (d == discipline)
            {
                found = true;
                break;
            }
        }
        if (!found && _state.disciplines.size() < 5)
        {
            _state.disciplines.push_back(discipline);
        }
    }

    _state.lastUpdated = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count();

    LOG_INFO("module", "[Scholarship] Event update: {} ({} T{}) discipline={} streak={}",
        scholarRank, league, tier, discipline, streak);
}

void ScholarshipManager::ClearState()
{
    std::lock_guard<std::mutex> lock(_stateMutex);
    _state = ScholarshipState();
    LOG_DEBUG("module", "[Scholarship] State cleared");
}

// ============================================================================
// ScholarshipState implementation
// ============================================================================

bool ScholarshipState::QualifiesForBonus(const std::string& minLeague, uint32 minStreak) const
{
    if (!IsValid())
        return false;

    // Use the manager's league comparison
    return sScholarshipMgr->QualifiesForBonus(minLeague, minStreak);
}

} // namespace ECRealtimeBridge
