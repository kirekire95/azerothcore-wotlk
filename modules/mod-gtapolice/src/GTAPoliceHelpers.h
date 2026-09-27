/*
 * mod-gtapolice
 * Shared helper functions used across multiple files
 */

#ifndef GTA_POLICE_HELPERS_H
#define GTA_POLICE_HELPERS_H

#include "Player.h"
#include "Creature.h"
#include "GTAPoliceConfig.h"

#include <cmath>
#include <array>
#include "Random.h"

namespace GTAPolice
{

inline float Dist3(float ax, float ay, float az, float bx, float by, float bz)
{
    float dx = ax - bx;
    float dy = ay - by;
    float dz = az - bz;
    return std::sqrt(dx*dx + dy*dy + dz*dz);
}

inline void ApplyArrestCC(Player* player)
{
    if (!player) return;

    if (sGTAPoliceConfig->SpellArrestStun)
        player->CastSpell(player, sGTAPoliceConfig->SpellArrestStun, true);

    if (sGTAPoliceConfig->SpellArrestRoot)
        player->CastSpell(player, sGTAPoliceConfig->SpellArrestRoot, true);
}

inline void ClearArrestCC(Player* player)
{
    if (!player) return;

    if (sGTAPoliceConfig->SpellArrestStun)
        player->RemoveAurasDueToSpell(sGTAPoliceConfig->SpellArrestStun);

    if (sGTAPoliceConfig->SpellArrestRoot)
        player->RemoveAurasDueToSpell(sGTAPoliceConfig->SpellArrestRoot);
}

inline void ApplySiren(Creature* car, bool on)
{
    if (!car || !sGTAPoliceConfig->SpellSirenAura)
        return;

    if (on)
        car->CastSpell(car, sGTAPoliceConfig->SpellSirenAura, true);
    else
        car->RemoveAurasDueToSpell(sGTAPoliceConfig->SpellSirenAura);
}

// ============================================================================
// RADIO CHATTER - GTA-style police radio lines
// ============================================================================

// Chase phase radio lines
static constexpr std::array<const char*, 12> RADIO_CHASE = {{
    "Dispatch, suspect heading %s. In pursuit.",
    "10-80, high speed pursuit in progress.",
    "All units, suspect fleeing on foot. Requesting backup.",
    "Dispatch, suspect is evading. Increasing speed.",
    "Suspect is armed and dangerous. Use caution.",
    "10-31, crime in progress. All available units respond.",
    "We have visual on the suspect. Moving to intercept.",
    "Unit in pursuit. Suspect is not stopping.",
    "Dispatch, requesting air support for the chase.",
    "Suspect heading into the wilderness. Keep on them!",
    "This is Unit 7, engaging suspect. Over.",
    "Requesting roadblock at the next intersection.",
}};

// Approach phase (crew exits)
static constexpr std::array<const char*, 8> RADIO_APPROACH = {{
    "Moving in for apprehension. Weapons ready.",
    "Officers on foot. Surrounding the suspect.",
    "Approaching suspect location. Stay alert.",
    "Dispatch, officers are dismounting. Going in.",
    "10-29, suspect is cornered. Moving in.",
    "Units closing in. Prepare for takedown.",
    "Visual confirmed. Officers approaching on foot.",
    "Suspect has stopped. Initiating contact.",
}};

// Arrest phase
static constexpr std::array<const char*, 6> RADIO_ARREST = {{
    "Suspect apprehended. 10-15, prisoner in custody.",
    "10-95, suspect in custody. Transporting to jail.",
    "Arrest confirmed. Securing the suspect now.",
    "Got 'em! Suspect is in handcuffs.",
    "Dispatch, suspect neutralized and restrained.",
    "Subject arrested without incident. Transporting.",
}};

// Transport phase
static constexpr std::array<const char*, 6> RADIO_TRANSPORT = {{
    "10-95, transporting prisoner to the stockade.",
    "En route to jail with suspect in custody.",
    "Prisoner secure. Heading to the lockup.",
    "Dispatch, we're moving. ETA to jail, 2 minutes.",
    "Transport in progress. No issues.",
    "All clear. Bringing the prisoner in.",
}};

// Escape / lost suspect
static constexpr std::array<const char*, 6> RADIO_ESCAPE = {{
    "10-22, suspect has fled the area. Pursuit called off.",
    "We lost 'em. Suspect is gone.",
    "Dispatch, suspect escaped pursuit. Standing down.",
    "Lost visual. Returning to patrol.",
    "Suspect has evaded. Requesting APB.",
    "Cannot maintain pursuit. Suspect got away.",
}};

inline const char* GetRandomRadioLine(const char* const* lines, size_t count)
{
    return lines[urand(0, static_cast<uint32>(count - 1))];
}

inline void DoRadioChatter(Creature* speaker, uint8 phase)
{
    if (!speaker || !sGTAPoliceConfig->RadioChatterEnabled)
        return;

    const char* line = nullptr;

    switch (phase)
    {
        case 0: line = GetRandomRadioLine(RADIO_CHASE.data(), RADIO_CHASE.size()); break;
        case 1: line = GetRandomRadioLine(RADIO_APPROACH.data(), RADIO_APPROACH.size()); break;
        case 2: line = GetRandomRadioLine(RADIO_ARREST.data(), RADIO_ARREST.size()); break;
        case 3: line = GetRandomRadioLine(RADIO_TRANSPORT.data(), RADIO_TRANSPORT.size()); break;
        case 4: line = GetRandomRadioLine(RADIO_ESCAPE.data(), RADIO_ESCAPE.size()); break;
        default: return;
    }

    if (line)
        speaker->Say(line, LANG_UNIVERSAL);
}

} // namespace GTAPolice

#endif // GTA_POLICE_HELPERS_H
