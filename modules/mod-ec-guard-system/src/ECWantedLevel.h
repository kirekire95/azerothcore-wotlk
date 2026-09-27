/*
 * EC Guard System - Wanted Level Data Structures
 * GTA-style wanted level system for AzerothCore
 */

#ifndef EC_GUARD_SYSTEM_WANTED_LEVEL_H
#define EC_GUARD_SYSTEM_WANTED_LEVEL_H

#include "ObjectGuid.h"
#include "Player.h"
#include "SharedDefines.h"
#include <chrono>

namespace ECGuardSystem
{
    // Crime severity levels
    enum class CrimeType : uint8
    {
        CIVILIAN_ATTACK = 0,    // Attacking a civilian NPC
        CIVILIAN_KILL   = 1,    // Killing a civilian NPC
        GUARD_ATTACK    = 2,    // Attacking a guard
        GUARD_KILL      = 3,    // Killing a guard
    };

    // Wanted star levels (GTA-style)
    enum class WantedStars : uint8
    {
        NONE        = 0,    // No wanted level
        ONE_STAR    = 1,    // Minor offense - 1 guard response
        TWO_STAR    = 2,    // Moderate offense - 2 guards
        THREE_STAR  = 3,    // Serious offense - 3 stronger guards
        FOUR_STAR   = 4,    // Severe offense - 4 elite guards
        FIVE_STAR   = 5     // Maximum - 5 elite guards + captain
    };

    // Per-player wanted state
    struct WantedState
    {
        ObjectGuid playerGuid;
        WantedStars stars = WantedStars::NONE;
        uint32 crimePoints = 0;                 // Accumulated crime points
        uint32 civilianKills = 0;               // Civilians killed
        uint32 guardKills = 0;                  // Guards killed this session

        // Timers (using steady_clock for monotonic time)
        std::chrono::steady_clock::time_point lastCrimeTime;
        std::chrono::steady_clock::time_point lastGuardSpawnTime;

        // Cooldowns and tracking
        uint32 activeGuardCount = 0;            // Currently pursuing guards

        // Zone tracking
        uint32 crimeZoneId = 0;                 // Zone where crime occurred
        uint32 crimeAreaId = 0;                 // Area where crime occurred
        TeamId crimeFaction = TEAM_NEUTRAL;     // Faction offended

        // Helper methods
        bool IsWanted() const { return stars > WantedStars::NONE; }

        bool CanSpawnGuard(uint32 cooldownMs) const
        {
            auto now = std::chrono::steady_clock::now();
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastGuardSpawnTime).count();
            return elapsed >= cooldownMs;
        }

        void UpdateStars(uint32 p1, uint32 p2, uint32 p3, uint32 p4, uint32 p5)
        {
            if (crimePoints >= p5)
                stars = WantedStars::FIVE_STAR;
            else if (crimePoints >= p4)
                stars = WantedStars::FOUR_STAR;
            else if (crimePoints >= p3)
                stars = WantedStars::THREE_STAR;
            else if (crimePoints >= p2)
                stars = WantedStars::TWO_STAR;
            else if (crimePoints >= p1)
                stars = WantedStars::ONE_STAR;
            else
                stars = WantedStars::NONE;
        }

        void Reset()
        {
            stars = WantedStars::NONE;
            crimePoints = 0;
            civilianKills = 0;
            guardKills = 0;
            activeGuardCount = 0;
            crimeZoneId = 0;
            crimeAreaId = 0;
            crimeFaction = TEAM_NEUTRAL;
        }
    };

    // Escalation configuration (loaded from config)
    struct EscalationConfig
    {
        uint32 pointsFor1Star = 100;    // ~1 civilian kill
        uint32 pointsFor2Star = 300;    // ~1 guard attack or 3 civilians
        uint32 pointsFor3Star = 600;    // ~1 guard kill or sustained violence
        uint32 pointsFor4Star = 1000;   // Multiple guard kills
        uint32 pointsFor5Star = 1500;   // Massacre

        // Crime point values
        uint32 civilianAttackPoints = 50;
        uint32 civilianKillPoints = 100;
        uint32 guardAttackPoints = 150;
        uint32 guardKillPoints = 300;
    };

    // Per-zone guard configuration
    struct ZoneGuardConfig
    {
        uint32 zoneId;
        // Zero means the complete zone. Outdoor settlement rows should use an
        // exact AreaTable.dbc ID so their guards cannot leak across the zone.
        uint32 areaId = 0;
        TeamId faction;                     // TEAM_ALLIANCE, TEAM_HORDE, TEAM_NEUTRAL
        std::vector<uint32> guardEntries;   // Regular guard creature entries
        std::vector<uint32> eliteEntries;   // Elite guard entries (3+ stars)
        uint32 captainEntry = 0;            // Captain entry (5 stars)
        float searchRadius = 80.0f;         // Radius to search for existing guards
    };

    // ==========================================================================
    // GUARD SPELL CONSTANTS (WoW 3.3.5 verified)
    // ==========================================================================
    namespace GuardSpells
    {
        constexpr uint32 HAMSTRING           = 9080;   // Snare, all tiers
        constexpr uint32 SHIELD_BASH         = 11972;  // Interrupt, tier 2+
        constexpr uint32 CHARGE              = 22911;  // Gap close, tier 2+
        constexpr uint32 NET                 = 12024;  // Root, tier 3+
        constexpr uint32 INTIMIDATING_SHOUT  = 19134;  // Fear, tier 3 only (5 star)

        // Star-specific wanted mark auras (visible debuff on wanted players)
        // Custom spells 900001-900005: SPELL_AURA_DUMMY with permanent duration
        // Defined in: data/sql/db_world/ec_guard_wanted_mark_spell.sql
        // Each star level has a unique icon and tooltip
        constexpr uint32 WANTED_MARK_1STAR   = 900001; // "Wanted" - skull icon
        constexpr uint32 WANTED_MARK_2STAR   = 900002; // "Wanted - Dangerous" - crossed swords
        constexpr uint32 WANTED_MARK_3STAR   = 900003; // "Wanted - Armed & Dangerous" - rampage
        constexpr uint32 WANTED_MARK_4STAR   = 900004; // "Wanted - Extremely Dangerous" - dragon skull
        constexpr uint32 WANTED_MARK_5STAR   = 900005; // "MOST WANTED" - death pact

        // DEPRECATED: Use GetWantedMarkForStars() or RemoveAllWantedMarks() instead.
        // This alias only references 1-star mark - using it directly will miss 2-5 star auras.
        constexpr uint32 WANTED_MARK         = 900001;

        // Bounty hunter tracking debuff (applied when hunter is dispatched)
        // Defined in: data/sql/db_world/ec_guard_wanted_mark_spell.sql
        constexpr uint32 HUNTED_MARK         = 900010; // "Hunted" - crosshairs icon

        // Get the wanted mark spell ID for a given star level
        inline uint32 GetWantedMarkForStars(WantedStars stars)
        {
            switch (stars)
            {
                case WantedStars::FIVE_STAR:   return WANTED_MARK_5STAR;
                case WantedStars::FOUR_STAR:   return WANTED_MARK_4STAR;
                case WantedStars::THREE_STAR:  return WANTED_MARK_3STAR;
                case WantedStars::TWO_STAR:    return WANTED_MARK_2STAR;
                case WantedStars::ONE_STAR:    return WANTED_MARK_1STAR;
                default:                       return 0;
            }
        }

        // Remove all wanted mark auras from a player
        inline void RemoveAllWantedMarks(Player* player)
        {
            if (!player) return;
            player->RemoveAura(WANTED_MARK_1STAR);
            player->RemoveAura(WANTED_MARK_2STAR);
            player->RemoveAura(WANTED_MARK_3STAR);
            player->RemoveAura(WANTED_MARK_4STAR);
            player->RemoveAura(WANTED_MARK_5STAR);
        }
    }

    // ==========================================================================
    // CUSTOM CREATURE ENTRIES (Bounty Hunter Assassins)
    // Defined in: data/sql/db_world/ec_guard_assassin_creatures.sql
    // ==========================================================================
    namespace AssassinEntries
    {
        // Bloodfang Assassin: Undead Rogue (Dark Ranger model)
        // Dispatched to hunt Alliance players at max wanted level
        constexpr uint32 BLOODFANG_ASSASSIN  = 900100;

        // Shadowleaf Assassin: Night Elf Rogue (Maiev/Warden model)
        // Dispatched to hunt Horde players at max wanted level
        constexpr uint32 SHADOWLEAF_ASSASSIN = 900101;
    }

    // Guard tier based on wanted star level
    enum class GuardTier : uint8
    {
        REGULAR = 0,    // 1-2 stars: melee only
        VETERAN = 1,    // 3 stars: hamstring + charge
        ELITE   = 2,    // 4 stars: + shield bash + net
        CHAMPION = 3,   // 5 stars: + intimidating shout
    };

    // Determine guard tier from wanted stars
    inline GuardTier GetGuardTierForStars(WantedStars stars)
    {
        switch (stars)
        {
            case WantedStars::FIVE_STAR:   return GuardTier::CHAMPION;
            case WantedStars::FOUR_STAR:   return GuardTier::ELITE;
            case WantedStars::THREE_STAR:  return GuardTier::VETERAN;
            default:                       return GuardTier::REGULAR;
        }
    }

    // Per-guard spell timer tracking (stored externally, keyed by guard GUID)
    struct GuardSpellTimers
    {
        uint32 hamstringTimer    = 2000;   // First hamstring after 2s
        uint32 shieldBashTimer   = 5000;   // First bash after 5s
        uint32 chargeTimer       = 0;      // Charge immediately on pull
        uint32 netTimer          = 8000;   // First net after 8s
        uint32 shoutTimer        = 12000;  // Shout after 12s
        bool chargeUsed          = false;  // Charge is one-shot per combat
        GuardTier tier            = GuardTier::REGULAR;
    };

    // Civilian/Guard flavor text
    struct FlavorText
    {
        static const std::vector<std::string>& GetCivilianYells()
        {
            static std::vector<std::string> yells = {
                "Guards! Help!",
                "Someone stop them!",
                "Murder! Help!",
                "The city is under attack!",
                "Protect us!",
                "Call the guards!"
            };
            return yells;
        }

        static const std::vector<std::string>& GetGuardYells()
        {
            static std::vector<std::string> yells = {
                "You dare attack the guard?!",
                "Reinforcements, now!",
                "You'll hang for this!",
                "To arms! We're under attack!",
                "Criminal scum!",
                "Stop right there!"
            };
            return yells;
        }

        static const std::vector<std::string>& GetGuardChaseYells()
        {
            static std::vector<std::string> yells = {
                "Halt, criminal!",
                "Stop right there, criminal scum!",
                "You won't escape justice!",
                "In the name of the law, stop!"
            };
            return yells;
        }
    };
}

#endif // EC_GUARD_SYSTEM_WANTED_LEVEL_H
