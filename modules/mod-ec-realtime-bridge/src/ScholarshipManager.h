/*
 * mod-ec-realtime-bridge - Scholarship Manager
 * Copyright (C) 2025 EC
 *
 * Manages player scholarship state from Korean Quest integration.
 * NPCs and game systems can reference player's real-life learning progress.
 *
 * ╔═══════════════════════════════════════════════════════════════════════════╗
 * ║                           ARCHITECTURE                                     ║
 * ╠═══════════════════════════════════════════════════════════════════════════╣
 * ║                                                                           ║
 * ║  Korean Quest App  ──HTTP──>  Node.js Backend  ──TCP──>  ScholarshipMgr  ║
 * ║       (polls)                 (translates)               (C++ singleton) ║
 * ║                                                               │           ║
 * ║                                                               ▼           ║
 * ║                                               NPCs, AI Prompts, Scripts   ║
 * ║                                                                           ║
 * ╚═══════════════════════════════════════════════════════════════════════════╝
 *
 * LORE TRANSLATION:
 *   Korean Quest "Diamond League" → WoW "Expert Scholar"
 *   Korean Quest "streak"         → WoW "dedication to studies"
 *   Korean Quest "arena win"      → WoW "scholarly combat victory"
 *
 * THREAD SAFETY:
 *   All public methods are thread-safe via std::mutex.
 *   Safe to call from world update, scripts, or any thread.
 *
 * EXAMPLE USAGE:
 *
 *   // Check if player has scholarship data
 *   if (sScholarshipMgr->HasScholarship())
 *   {
 *       // Get their rank
 *       std::string rank = sScholarshipMgr->GetScholarRank();
 *       // Returns: "Expert Scholar", "Master Scholar", etc.
 *
 *       // Get random dialogue for NPC
 *       std::string dialogue = sScholarshipMgr->GetRandomDialogueSnippet();
 *       creature->Whisper(dialogue, LANG_UNIVERSAL, player);
 *       // Whispers: "Your mastery of the Eastern Tongues is legendary, Grand Archivist."
 *
 *       // Check if player qualifies for a bonus
 *       if (sScholarshipMgr->QualifiesForBonus("GOLD", 7))
 *       {
 *           // Player is Gold League+ with 7+ day streak
 *           player->AddAura(SPELL_SCHOLAR_BUFF, player);
 *       }
 *
 *       // Get full context for AI prompts
 *       std::string context = sScholarshipMgr->GetAIPromptContext();
 *       // Returns multi-line string with all scholarship details
 *   }
 *
 * GM COMMANDS:
 *   .bridge scholarship  - Display scholarship status in-game
 *   .bridge status       - Show bridge connection status
 *
 * CONFIGURATION:
 *   Korean Quest polling is handled by Node.js backend.
 *   Set KOREAN_QUEST_USER_ID env var on Node.js side.
 */

#ifndef EC_SCHOLARSHIP_MANAGER_H
#define EC_SCHOLARSHIP_MANAGER_H

#include "RealtimeBridgeTypes.h"
#include <mutex>
#include <map>

namespace ECRealtimeBridge
{

/**
 * ScholarshipManager - Singleton managing player scholarship state
 *
 * Receives updates from Node.js backend via life events.
 * Provides thread-safe access to scholarship data for NPCs.
 */
class ScholarshipManager
{
public:
    static ScholarshipManager* Instance();

    // ========================================================================
    // STATE ACCESS
    // ========================================================================

    /**
     * Get current scholarship state
     * Thread-safe read access
     */
    ScholarshipState GetState() const;

    /**
     * Check if we have valid scholarship data
     */
    bool HasScholarship() const;

    /**
     * Get scholar rank string (e.g., "Expert Scholar")
     */
    std::string GetScholarRank() const;

    /**
     * Get current study streak in days
     */
    uint32 GetCurrentStreak() const;

    /**
     * Get primary disciplines (strengths)
     */
    std::vector<std::string> GetDisciplines() const;

    /**
     * Get intensity factor (0.0-1.0) based on percentile
     */
    float GetIntensity() const;

    /**
     * Check if player qualifies for a scholarship-based bonus
     */
    bool QualifiesForBonus(const std::string& minLeague, uint32 minStreak = 0) const;

    // ========================================================================
    // NPC DIALOGUE HELPERS
    // ========================================================================

    /**
     * Get a random NPC dialogue snippet based on scholarship
     * Returns empty string if no scholarship or no applicable snippets
     */
    std::string GetRandomDialogueSnippet() const;

    /**
     * Get all applicable dialogue snippets
     */
    std::vector<std::string> GetDialogueSnippets() const;

    /**
     * Get formatted context string for AI prompts
     */
    std::string GetAIPromptContext() const;

    // ========================================================================
    // EVENT HANDLING (called by RealtimeBridgeSocket)
    // ========================================================================

    /**
     * Handle incoming life event from Node.js
     */
    void HandleLifeEvent(const std::string& type, const std::string& jsonPayload);

    /**
     * Update state from scholarship event
     */
    void UpdateFromScholarshipEvent(
        const std::string& scholarRank,
        const std::string& discipline,
        float intensity,
        const std::string& league,
        uint8 tier,
        float percentile,
        uint32 streak
    );

    /**
     * Clear scholarship state (e.g., on logout)
     */
    void ClearState();

private:
    ScholarshipManager();
    ~ScholarshipManager() = default;

    // Thread safety
    mutable std::mutex _stateMutex;

    // Current state
    ScholarshipState _state;

    // League ordering for comparisons
    static const std::map<std::string, int> _leagueOrder;

    // Helper to compare leagues
    int GetLeagueRank(const std::string& league) const;

    // Parse JSON helpers
    void ParseScholarshipEvent(const std::string& jsonPayload);
    void ParseSnapshotEvent(const std::string& jsonPayload);
};

} // namespace ECRealtimeBridge

#define sScholarshipMgr ECRealtimeBridge::ScholarshipManager::Instance()

#endif // EC_SCHOLARSHIP_MANAGER_H
