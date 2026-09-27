# Korean Quest Scholarship Integration

## Overview

This integration connects the Korean Quest language learning application to World of Warcraft,
allowing NPCs and game systems to reference the player's real-life learning achievements.
Learning progress is translated into lore-appropriate "scholarship" concepts.

## Architecture

```
┌─────────────────────────────────────────────────────────────────────────────────┐
│                              KOREAN QUEST APP                                    │
│  (Language learning - leagues, streaks, vocabulary, arena battles)              │
└─────────────────────────────────────────────────────────────────────────────────┘
                                      │
                                      │ GraphQL API (HTTP polling every 60s)
                                      ▼
┌─────────────────────────────────────────────────────────────────────────────────┐
│                           NODE.JS BACKEND                                        │
│  ┌─────────────────────┐    ┌──────────────────────┐    ┌───────────────────┐   │
│  │  KoreanQuestService │───▶│ ScholarshipIntegr.   │───▶│ RealtimeBridge    │   │
│  │  (polls API)        │    │ (translates to lore) │    │ (TCP socket)      │   │
│  └─────────────────────┘    └──────────────────────┘    └───────────────────┘   │
└─────────────────────────────────────────────────────────────────────────────────┘
                                      │
                                      │ TCP JSON (namespace: "life")
                                      ▼
┌─────────────────────────────────────────────────────────────────────────────────┐
│                          AZEROTHCORE C++ SERVER                                  │
│  ┌─────────────────────┐    ┌──────────────────────┐                            │
│  │  RealtimeBridge     │───▶│ ScholarshipManager   │◀─── NPCs, AI, Scripts      │
│  │  (receives events)  │    │ (stores state)       │                            │
│  └─────────────────────┘    └──────────────────────┘                            │
└─────────────────────────────────────────────────────────────────────────────────┘
```

## Data Flow

### 1. Korean Quest → Node.js

`KoreanQuestService` polls the Korean Quest GraphQL API every 60 seconds:

```typescript
// Queries executed:
progressSnapshot(userId: $userId) { league, tier, currentStreak, ... }
lifeEvents(userId: $userId, since: $since) { type, payload, ... }
```

### 2. Node.js → C++ Bridge

`ScholarshipIntegration` translates Korean Quest data to lore-appropriate events:

| Korean Quest Term | WoW Lore Translation |
|-------------------|---------------------|
| Bronze League     | Initiate            |
| Silver League     | Apprentice Scholar  |
| Gold League       | Journeyman Scholar  |
| Platinum League   | Adept Scholar       |
| Diamond League    | Expert Scholar      |
| Master League     | Master Scholar      |
| Grandmaster       | Grand Archivist     |

| Topic              | Discipline           |
|--------------------|---------------------|
| Family Terms       | Kinship Lore        |
| Greetings          | Diplomatic Protocols|
| Numbers            | Arcane Numerology   |
| Food & Drink       | Culinary Arts       |
| Honorifics         | Court Etiquette     |

Events sent to C++ via TCP socket (namespace: `life`):

```json
{
  "ns": "life",
  "type": "scholarship_snapshot",
  "data": {
    "scholarRank": "Expert Scholar",
    "league": "DIAMOND",
    "tier": 2,
    "percentile": 85.5,
    "currentStreak": 14,
    "longestStreak": 30,
    "vocabTotal": 500,
    "vocabMastered": 250,
    "matchesWon": 15,
    "winRate": 65.0
  }
}
```

### 3. C++ ScholarshipManager

Thread-safe singleton that stores and provides access to scholarship state:

```cpp
// Get current state
ScholarshipState state = sScholarshipMgr->GetState();

// Check if player has scholarship data
if (sScholarshipMgr->HasScholarship())
{
    // Get rank for display
    std::string rank = sScholarshipMgr->GetScholarRank();  // "Expert Scholar"

    // Get study streak
    uint32 streak = sScholarshipMgr->GetCurrentStreak();   // 14

    // Check if player qualifies for a bonus
    if (sScholarshipMgr->QualifiesForBonus("GOLD", 7))
    {
        // Player is Gold+ with 7+ day streak
        ApplyScholarBonus(player);
    }

    // Get random dialogue for NPC
    std::string dialogue = sScholarshipMgr->GetRandomDialogueSnippet();
    creature->Whisper(dialogue, LANG_UNIVERSAL, player);

    // Get context for AI prompts
    std::string context = sScholarshipMgr->GetAIPromptContext();
    // Returns: "Scholar Rank: Expert Scholar\nStudy Streak: 14 days\n..."
}
```

## Event Types

### Incoming Events (from Node.js)

| Event Type                    | Description                              |
|------------------------------|------------------------------------------|
| `scholarship_rank_changed`    | Player promoted/demoted in league        |
| `scholarship_streak_milestone`| Study streak milestone (7, 14, 30 days)  |
| `scholarship_streak_broken`   | Player broke their study streak          |
| `scholarship_lesson_complete` | Completed a lesson                       |
| `scholarship_chapter_complete`| Completed a chapter/unit                 |
| `scholarship_arena_result`    | Won/lost arena match                     |
| `scholarship_achievement`     | Unlocked achievement                     |
| `scholarship_milestone`       | XP or level milestone                    |
| `scholarship_snapshot`        | Full state synchronization               |

## NPC Dialogue Examples

Based on scholarship state, NPCs can say things like:

**Rank-based:**
- Grandmaster: "Your mastery of the Eastern Tongues is legendary, Grand Archivist."
- Master: "Few have achieved your level of scholarly excellence, Master Scholar."
- Diamond: "You've proven yourself a true Expert Scholar."

**Streak-based:**
- 30+ days: "Your dedication is remarkable - 30 days of consistent study!"
- 14+ days: "Two weeks of daily practice! Your discipline rivals the monks."
- 7+ days: "I see you've maintained your studies for 7 days. Commendable discipline."

**Discipline-based:**
- Kinship Lore: "Your understanding of kinship bonds runs deep."
- Diplomatic Protocols: "You speak with the grace of a seasoned diplomat."
- Arcane Numerology: "Numbers bend to your will, scholar."

**Arena-based:**
- 50+ wins: "Your victories in scholarly combat are legendary across the realm."
- 10+ wins: "Your victories in scholarly combat are well known."

## In-Game Commands

| Command | Description |
|---------|-------------|
| `.bridge scholarship` | Display current scholarship status |
| `.bridge status` | Show realtime bridge connection status |

Example output of `.bridge scholarship`:
```
=== Scholarship Status ===
Rank: Expert Scholar (DIAMOND T2)
Percentile: 85.5%
Level: 12 | XP: 15000
Study Streak: 14 days (Best: 30)
Strong in: Kinship Lore, Diplomatic Protocols
Needs work: Arcane Numerology
Vocabulary: 250/500 (50.0% mastered)
Weekly Accuracy: 78.5%
Arena: 15 W / 8 L (65.2% win rate)

"I see you've maintained your studies for 14 days. Commendable discipline."
```

## Configuration

### Node.js Environment Variables

```bash
# Korean Quest API endpoint
KOREAN_QUEST_API_URL=http://localhost:3000/graphql

# User ID to poll (required for service to start)
KOREAN_QUEST_USER_ID=your-user-id

# Poll interval in milliseconds (default: 60000)
KOREAN_QUEST_POLL_INTERVAL=60000

# Enable/disable the service (default: enabled)
KOREAN_QUEST_ENABLED=1
```

### C++ Configuration (worldserver.conf)

```conf
# Enable the realtime bridge
ECRealtimeBridge.Enable = 1

# Node.js backend address
ECRealtimeBridge.Host = "127.0.0.1"
ECRealtimeBridge.Port = 8092

# Debug logging
ECRealtimeBridge.Debug = 0
```

## Files

### Node.js (wow-control-backend)

| File | Purpose |
|------|---------|
| `services/korean-quest/KoreanQuestService.ts` | Polls Korean Quest API |
| `services/korean-quest/types.ts` | Type definitions and lore mappings |
| `services/aliveworld/ScholarshipIntegration.ts` | Translates and emits to C++ |
| `services/aliveworld/types.ts` | ScholarshipState interface |
| `services/realtime-bridge/RealtimeBridgeService.ts` | TCP socket to C++ |
| `services/realtime-bridge/types.ts` | Bridge message types |

### C++ (mod-ec-realtime-bridge)

| File | Purpose |
|------|---------|
| `src/ScholarshipManager.h` | Singleton header, API declarations |
| `src/ScholarshipManager.cpp` | State storage, dialogue generation |
| `src/RealtimeBridgeTypes.h` | LIFE_EVENTS namespace, ScholarshipState struct |
| `src/RealtimeBridgeScripts.cpp` | Event handler registration, commands |

## Usage in Custom Scripts

### Example: Scholarship-aware NPC

```cpp
#include "ScholarshipManager.h"

class npc_scholar_trainer : public CreatureScript
{
public:
    npc_scholar_trainer() : CreatureScript("npc_scholar_trainer") {}

    bool OnGossipHello(Player* player, Creature* creature) override
    {
        if (sScholarshipMgr->HasScholarship())
        {
            // Personalized greeting based on rank
            std::string snippet = sScholarshipMgr->GetRandomDialogueSnippet();
            if (!snippet.empty())
            {
                creature->Whisper(snippet, LANG_UNIVERSAL, player);
            }

            // Check for special dialogue based on progress
            if (sScholarshipMgr->QualifiesForBonus("DIAMOND", 14))
            {
                AddGossipItemFor(player, GOSSIP_ICON_CHAT,
                    "Teach me the advanced techniques, Master.",
                    GOSSIP_SENDER_MAIN, 1);
            }
        }

        AddGossipItemFor(player, GOSSIP_ICON_CHAT,
            "Tell me about your studies.",
            GOSSIP_SENDER_MAIN, 0);

        SendGossipMenuFor(player, DEFAULT_GOSSIP_MESSAGE, creature->GetGUID());
        return true;
    }
};
```

### Example: Scholarship-based Buff

```cpp
void ApplyScholarshipBuff(Player* player)
{
    if (!sScholarshipMgr->HasScholarship())
        return;

    uint32 streak = sScholarshipMgr->GetCurrentStreak();

    // Apply buff based on streak
    if (streak >= 30)
        player->AddAura(SPELL_SCHOLAR_MASTERY, player);      // +10% XP
    else if (streak >= 14)
        player->AddAura(SPELL_SCHOLAR_DEDICATION, player);   // +5% XP
    else if (streak >= 7)
        player->AddAura(SPELL_SCHOLAR_FOCUS, player);        // +2% XP
}
```

### Example: AI Prompt Enhancement

```cpp
std::string BuildAIPrompt(Player* player, const std::string& userMessage)
{
    std::string prompt = "You are an NPC in World of Warcraft.\n\n";

    // Add scholarship context if available
    std::string scholarContext = sScholarshipMgr->GetAIPromptContext();
    if (!scholarContext.empty() && scholarContext != "The player's scholarship status is unknown.")
    {
        prompt += "The player is a scholar with the following progress:\n";
        prompt += scholarContext;
        prompt += "\nReference their scholarly achievements naturally in conversation.\n\n";
    }

    prompt += "Player says: " + userMessage;
    return prompt;
}
```

## Troubleshooting

### No scholarship data appearing

1. Check Korean Quest service is running:
   - Verify `KOREAN_QUEST_USER_ID` is set
   - Check logs for `[KoreanQuest] Started`

2. Check realtime bridge connection:
   - In-game: `.bridge status`
   - Look for `Connected: YES | Authenticated: YES`

3. Check if life events are being received:
   - Enable debug: `.bridge debug on`
   - Look for `[Scholarship] Event:` or `[Scholarship] Sent snapshot` in logs

### Events not reaching C++

1. Verify the bridge is connected:
   ```
   [RealtimeBridge] Client connected from 127.0.0.1:xxxxx
   [RealtimeBridge] Authentication successful
   ```

2. Check for handler registration:
   ```
   ECRealtimeBridge: Registered handler for namespace 'life'
   ```

3. Enable debug mode and check for incoming messages:
   ```
   ECRealtimeBridge: Received: {"ns":"life","type":"scholarship_snapshot"...
   ```

## Thread Safety

`ScholarshipManager` is fully thread-safe:

- All public methods use `std::lock_guard<std::mutex>`
- State is copied out (not returned by reference)
- Safe to call from any thread (main, world update, scripts)

```cpp
// Safe to call from anywhere
ScholarshipState state = sScholarshipMgr->GetState();  // Returns copy
std::string rank = sScholarshipMgr->GetScholarRank();  // Thread-safe
```
