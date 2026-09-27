/*
 * mod-gtapolice
 * Police Officer AI implementation - scans for wanted players, dispatches cars
 */

#include "GTAPoliceOfficerAI.h"
#include "GTAPoliceCarAI.h"
#include "GTAPoliceConfig.h"
#include "GTAPoliceBridge.h"
#include "GTAPoliceManager.h"

#include "Player.h"
#include "WorldSession.h"
#include "Creature.h"
#include "ObjectAccessor.h"
#include "Map.h"
#include "Log.h"
#include "Cell.h"
#include "CellImpl.h"
#include "GridNotifiers.h"
#include "GridNotifiersImpl.h"

// ============================================================================
// SPATIAL SEARCH - Grid-based wanted player finder (O(k) vs O(n))
// Uses AzerothCore's cell visitor to only check nearby grid cells
// ============================================================================

namespace {

// Custom check functor for finding wanted players
class WantedPlayerInRangeCheck
{
public:
    WantedPlayerInRangeCheck(WorldObject const* searcher, float range)
        : _searcher(searcher), _range(range), _best(nullptr), _bestDist(range + 1.0f) {}

    bool operator()(Player* player)
    {
        if (!player || !player->IsAlive() || player->IsGameMaster())
            return false;

        if (!sGTAPoliceConfig->AffectPlayerbots && player->GetSession() && player->GetSession()->IsBot())
            return false;

        // In vehicle? Skip
        if (player->GetVehicle())
            return false;

        // Not wanted? Skip
        if (!sGTAPoliceMgr->IsPlayerWanted(player))
            return false;

        // Already in pursuit or on cooldown? Skip
        if (!sGTAPoliceMgr->CanStartPursuit(player->GetGUID()))
            return false;

        // Check distance
        float dist = _searcher->GetDistance(player);
        if (dist > _range)
            return false;

        // Track closest
        if (dist < _bestDist)
        {
            _bestDist = dist;
            _best = player;
        }

        return false; // Keep searching for closer
    }

    Player* GetBest() const { return _best; }
    float GetBestDist() const { return _bestDist; }

private:
    WorldObject const* _searcher;
    float _range;
    Player* _best;
    float _bestDist;
};

} // anonymous namespace

// ============================================================================
// CONSTRUCTOR
// ============================================================================

npc_gtapolice_officer::npc_gtapolice_officer() : CreatureScript("npc_gtapolice_officer") {}

npc_gtapolice_officer::OfficerAI::OfficerAI(Creature* creature) : ScriptedAI(creature) {}

CreatureAI* npc_gtapolice_officer::GetAI(Creature* creature) const
{
    return new OfficerAI(creature);
}

// ============================================================================
// AI OVERRIDES
// ============================================================================

void npc_gtapolice_officer::OfficerAI::Reset()
{
    _events.Reset();
    _activeCarGuid.Clear();
    _events.ScheduleEvent(EVENT_SCAN, Milliseconds(sGTAPoliceConfig->OfficerScanInterval));
}

void npc_gtapolice_officer::OfficerAI::UpdateAI(uint32 diff)
{
    if (!sGTAPoliceConfig->Enabled)
        return;

    _events.Update(diff);

    while (uint32 eventId = _events.ExecuteEvent())
    {
        if (eventId == EVENT_SCAN)
        {
            Player* wanted = FindNearestWantedPlayer();
            if (wanted)
                SpawnPursuitCar(wanted);

            _events.ScheduleEvent(EVENT_SCAN, Milliseconds(sGTAPoliceConfig->OfficerScanInterval));
        }
    }
}

// ============================================================================
// PRIVATE METHODS
// ============================================================================

Creature* npc_gtapolice_officer::OfficerAI::GetActiveCar()
{
    return _activeCarGuid.IsEmpty() ? nullptr : ObjectAccessor::GetCreature(*me, _activeCarGuid);
}

Player* npc_gtapolice_officer::OfficerAI::FindNearestWantedPlayer()
{
    // Use grid-based spatial search (O(k) where k = nearby players)
    // instead of iterating all players on map (O(n))
    float range = sGTAPoliceConfig->OfficerScanRange;

    Player* result = nullptr;
    WantedPlayerInRangeCheck checker(me, range);
    Acore::PlayerSearcher<WantedPlayerInRangeCheck> searcher(me, result, checker);
    Cell::VisitObjects(me, searcher, range);

    // Use checker's best result (it tracks closest wanted player)
    return checker.GetBest();
}

void npc_gtapolice_officer::OfficerAI::SpawnPursuitCar(Player* target)
{
    if (!target) return;

    // Don't spawn if we already have an active car
    if (Creature* active = GetActiveCar())
    {
        if (active->IsAlive())
            return;
        _activeCarGuid.Clear();
    }

    // Emit detection event
    uint32 stars = sGTAPoliceMgr->GetPlayerWantedStars(target);
    sGTAPoliceBridge->EmitOfficerDetectedWanted(me, target, stars);

    // Start pursuit via manager
    if (!sGTAPoliceMgr->StartPursuit(target, me))
    {
        if (sGTAPoliceConfig->DebugLog)
            LOG_INFO("module", "[mod-gtapolice] Officer failed to start pursuit");
        return;
    }

    // Find the cars that were spawned and start their chase
    auto data = sGTAPoliceMgr->GetPursuitData(target->GetGUID());
    if (data.has_value() && !data->carGuids.empty())
    {
        _activeCarGuid = data->carGuids[0];

        for (const auto& carGuid : data->carGuids)
        {
            Creature* car = ObjectAccessor::GetCreature(*me, carGuid);
            if (car)
            {
                if (carGuid == data->carGuids[0])
                    sGTAPoliceBridge->EmitOfficerDispatchedCar(me, car, target);

                if (auto* carAI = dynamic_cast<npc_gtapolice_car::CarAI*>(car->AI()))
                {
                    carAI->SetTarget(target->GetGUID());
                    carAI->StartChase();
                }
            }
        }
    }

    if (sGTAPoliceConfig->DebugLog)
        LOG_INFO("module", "[mod-gtapolice] Officer dispatched car for {}", target->GetName());
}
