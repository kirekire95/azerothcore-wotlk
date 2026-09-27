/*
 * EC Guard System - Witness System Implementation
 * Crimes only register if a friendly NPC witnesses them.
 * Witnesses get 5 seconds to flee to a guard. Kill the witness = crime suppressed.
 */

#include "ECWitnessSystem.h"
#include "ECGuardSystemManager.h"
#include "ECGuardSystemBridge.h"
#include "Config.h"
#include "Creature.h"
#include "Player.h"
#include "Map.h"
#include "MapMgr.h"
#include "CellImpl.h"
#include "GridNotifiers.h"
#include "GridNotifiersImpl.h"
#include "ObjectAccessor.h"
#include "Chat.h"
#include <algorithm>

namespace ECGuardSystem
{
    // =========================================================================
    // Custom grid checker: matches ANY creature within range (no entry filter).
    // AzerothCore's AllCreaturesOfEntryInRange with entry=0 does NOT wildcard;
    // it literally checks GetEntry()==0, which matches nothing.
    // =========================================================================
    class AnyCreatureInRange
    {
    public:
        AnyCreatureInRange(WorldObject const* obj, float range)
            : m_obj(obj), m_range(range) {}

        bool operator()(Unit* u) const
        {
            return u && m_obj->IsWithinDist(u, m_range, false);
        }

    private:
        WorldObject const* m_obj;
        float m_range;
    };
    ECWitnessSystem* ECWitnessSystem::instance()
    {
        static ECWitnessSystem instance;
        return &instance;
    }

    void ECWitnessSystem::RegisterCallback(WitnessEventCallback callback)
    {
        m_callbacks.push_back(std::move(callback));
        ECG_LOG_INFO("ECWitnessSystem: External callback registered (total: %zu)", m_callbacks.size());
    }

    void ECWitnessSystem::CancelAlertsForCriminal(ObjectGuid criminalGuid)
    {
        std::size_t cancelled = 0;
        {
            std::lock_guard<std::mutex> lock(m_alertMutex);
            std::size_t const previousSize = m_activeAlerts.size();
            m_activeAlerts.erase(
                std::remove_if(m_activeAlerts.begin(), m_activeAlerts.end(),
                    [criminalGuid](WitnessAlert const& alert)
                    {
                        return alert.criminalGuid == criminalGuid;
                    }),
                m_activeAlerts.end());
            cancelled = previousSize - m_activeAlerts.size();
        }

        if (cancelled > 0)
            ECG_LOG_DEBUG("CancelAlertsForCriminal: Cancelled %zu pending witness report(s) for %s",
                cancelled, criminalGuid.ToString().c_str());
    }

    void ECWitnessSystem::NotifyCallbacks(Player* criminal, uint32 zoneId, WitnessEventType eventType, uint32 crimePoints)
    {
        for (const auto& cb : m_callbacks)
        {
            try { cb(criminal, zoneId, eventType, crimePoints); }
            catch (...) { ECG_LOG_WARN("ECWitnessSystem: Callback threw exception"); }
        }
    }

    void ECWitnessSystem::LoadFromConfig()
    {
        m_enabled = sConfigMgr->GetOption<bool>("ECGuardSystem.Witness.Enabled", true);
        m_witnessRange = sConfigMgr->GetOption<float>("ECGuardSystem.Witness.Range", 40.0f);
        m_alertDurationMs = sConfigMgr->GetOption<uint32>("ECGuardSystem.Witness.AlertDurationMs", 5000);
        m_requireLineOfSight = sConfigMgr->GetOption<bool>("ECGuardSystem.Witness.RequireLineOfSight", true);

        ECG_LOG_INFO("Witness system loaded - Enabled: %s, Range: %.0f, AlertMs: %u, LoS: %s",
            m_enabled ? "yes" : "no", m_witnessRange, m_alertDurationMs,
            m_requireLineOfSight ? "yes" : "no");
    }

    bool ECWitnessSystem::CheckForWitnesses(Player* criminal, Creature* victim, CrimeType crimeType)
    {
        if (!m_enabled)
        {
            ECG_LOG_TRACE("CheckForWitnesses: Witness system disabled, skipping");
            return false;
        }

        if (!criminal || !victim)
        {
            ECG_LOG_WARN("CheckForWitnesses: null criminal=%p or victim=%p",
                (void*)criminal, (void*)victim);
            return false;
        }

        bool const isCivilianCrime =
            crimeType == CrimeType::CIVILIAN_ATTACK ||
            crimeType == CrimeType::CIVILIAN_KILL;
        bool const isGuardCrime =
            crimeType == CrimeType::GUARD_ATTACK ||
            crimeType == CrimeType::GUARD_KILL;
        bool const isKill =
            crimeType == CrimeType::CIVILIAN_KILL ||
            crimeType == CrimeType::GUARD_KILL;

        // Keep kill routing valid while the victim corpse is still in the
        // world, but reject every other stale or misclassified target before
        // the grid search can turn it into a witness event.
        if (!criminal->IsInWorld() || !criminal->IsAlive() ||
            !victim->IsInWorld() || victim->IsDuringRemoveFromWorld() ||
            (!isKill && !victim->IsAlive()) ||
            (isCivilianCrime && !sECGuardSystemMgr->IsCreatureCivilian(victim)) ||
            (isGuardCrime && !sECGuardSystemMgr->IsCreatureGuard(victim)) ||
            (!isCivilianCrime && !isGuardCrime) ||
            criminal->GetMap() != victim->GetMap())
        {
            ECG_LOG_WARN("WITNESS TARGET REJECTED: criminal=%s victim=%s entry=%u type=%u alive=%s inWorld=%s civilian=%s guard=%s sameMap=%s",
                criminal->GetName().c_str(), victim->GetName().c_str(), victim->GetEntry(),
                static_cast<uint8>(crimeType), victim->IsAlive() ? "yes" : "no",
                victim->IsInWorld() ? "yes" : "no",
                sECGuardSystemMgr->IsCreatureCivilian(victim) ? "yes" : "no",
                sECGuardSystemMgr->IsCreatureGuard(victim) ? "yes" : "no",
                criminal->GetMap() == victim->GetMap() ? "yes" : "no");
            return false;
        }

        ECG_LOG_DEBUG("CheckForWitnesses: %s committed crime type %u against %s (entry %u) at [%.1f, %.1f, %.1f]",
            criminal->GetName().c_str(), static_cast<uint8>(crimeType),
            victim->GetName().c_str(), victim->GetEntry(),
            victim->GetPositionX(), victim->GetPositionY(), victim->GetPositionZ());

        // Find a witness NPC near the crime
        Creature* witness = FindWitness(criminal, victim);

        if (!witness)
        {
            ECG_LOG_INFO("No witness found for crime by %s against %s in zone %u - crime goes UNNOTICED",
                criminal->GetName().c_str(), victim->GetName().c_str(), victim->GetZoneId());
            return false;
        }

        // Defense in depth: this must remain unconditional even if a future
        // grid visitor or candidate classifier changes its filtering rules.
        if (witness == victim || witness->GetGUID() == victim->GetGUID())
        {
            ECG_LOG_ERROR("WITNESS SELF REJECTED: victim %s entry=%u GUID=%s was selected as its own witness",
                victim->GetName().c_str(), victim->GetEntry(),
                victim->GetGUID().ToString().c_str());
            return false;
        }

        // Determine crime points based on type
        const auto& config = sECGuardSystemMgr->GetEscalationConfig();
        uint32 crimePoints = 0;
        switch (crimeType)
        {
            case CrimeType::CIVILIAN_ATTACK: crimePoints = config.civilianAttackPoints; break;
            case CrimeType::CIVILIAN_KILL:   crimePoints = config.civilianKillPoints; break;
            case CrimeType::GUARD_ATTACK:    crimePoints = config.guardAttackPoints; break;
            case CrimeType::GUARD_KILL:      crimePoints = config.guardKillPoints; break;
        }

        ECG_LOG_DEBUG("CheckForWitnesses: Crime points for type %u = %u", static_cast<uint8>(crimeType), crimePoints);

        // Create the alert
        WitnessAlert alert;
        alert.witnessGuid = witness->GetGUID();
        alert.criminalGuid = criminal->GetGUID();
        alert.victimGuid = victim->GetGUID();
        alert.crimeType = crimeType;
        alert.crimePoints = crimePoints;
        alert.crimeZoneId = victim->GetZoneId();
        alert.crimeAreaId = victim->GetAreaId();
        alert.crimeMapId = victim->GetMapId();
        alert.crimeInstanceId = victim->GetInstanceId();
        alert.crimeFaction = sECGuardSystemMgr->GetCrimeFaction(criminal, victim);
        alert.alertTimerMs = m_alertDurationMs;
        alert.elapsedMs = 0;
        alert.confirmed = false;
        alert.suppressed = false;

        uint32 activeCount = 0;
        {
            std::lock_guard<std::mutex> lock(m_alertMutex);
            m_activeAlerts.push_back(alert);
            activeCount = static_cast<uint32>(m_activeAlerts.size());
        }

        // Make the witness react
        MakeWitnessFlee(witness, criminal);

        // Emit to bridge
        sGuardSystemBridge->EmitWitnessDetected(criminal, witness, victim, crimeType, m_alertDurationMs);

        // Notify external modules (e.g., mod-gtapolice zone heat)
        NotifyCallbacks(criminal, alert.crimeZoneId, WitnessEventType::DETECTED, crimePoints);

        ECG_LOG_INFO("WITNESS ALERT: %s (entry %u, GUID %s) saw crime by %s against %s - %u ms to report (active alerts: %u)",
            witness->GetName().c_str(), witness->GetEntry(),
            witness->GetGUID().ToString().c_str(),
            criminal->GetName().c_str(), victim->GetName().c_str(),
            m_alertDurationMs, activeCount);

        // Notify the criminal with a readable timer. The update loop below sends
        // second-by-second reminders until the witness reports the crime.
        uint32 const alertSeconds = std::max<uint32>(1,
            (m_alertDurationMs / 1000) + (m_alertDurationMs % 1000 == 0 ? 0 : 1));
        ChatHandler(criminal->GetSession()).PSendSysMessage(
            "|cffff8800[WITNESS]|r {} saw your crime! Stop the witness within {} {} or the guards will be alerted.",
            witness->GetName().c_str(), alertSeconds, alertSeconds == 1 ? "second" : "seconds");

        return true;
    }

    Creature* ECWitnessSystem::FindWitness(Player* criminal, Creature* victim)
    {
        if (!criminal || !victim || !criminal->IsInWorld() || !criminal->IsAlive() ||
            !victim->IsInWorld() || victim->IsDuringRemoveFromWorld() ||
            criminal->GetMap() != victim->GetMap())
            return nullptr;

        Map* map = criminal->GetMap();
        if (!map)
            return nullptr;

        // Search for friendly NPCs within range that could witness the crime
        Creature* bestWitness = nullptr;
        float bestDist = m_witnessRange + 1.0f;

        // Use grid visitor to find nearby creatures
        // NOTE: We use our custom AnyCreatureInRange checker because AzerothCore's
        // AllCreaturesOfEntryInRange with entry=0 does NOT wildcard - it literally
        // checks GetEntry()==0, which matches nothing.
        std::list<Creature*> nearbyCreatures;
        AnyCreatureInRange checker(victim, m_witnessRange);
        Acore::CreatureListSearcher<AnyCreatureInRange> searcher(victim, nearbyCreatures, checker);
        Cell::VisitObjects(victim, searcher, m_witnessRange);

        ECG_LOG_DEBUG("FindWitness: Grid search found %zu creatures within %.0f yards of %s",
            nearbyCreatures.size(), m_witnessRange, victim->GetName().c_str());

        uint32 skippedSelf = 0, skippedDead = 0, skippedHostile = 0;
        uint32 skippedCombat = 0, skippedType = 0, skippedLoS = 0;
        uint32 candidates = 0;

        for (Creature* creature : nearbyCreatures)
        {
            if (!creature)
            { skippedType++; continue; }

            // Skip the victim itself. Check both object identity and GUID so
            // no self-witness can survive unusual object lifecycle behaviour.
            if (creature == victim || creature->GetGUID() == victim->GetGUID())
            { skippedSelf++; continue; }

            // Skip dead NPCs
            if (!creature->IsInWorld() || creature->IsDuringRemoveFromWorld() ||
                !creature->IsAlive())
            { skippedDead++; continue; }

            // Summons, pets, totems, triggers, critters, and owned helpers are
            // combat/world objects, not independent witnesses. Permanent town
            // guards remain eligible through the guard classification below.
            if (creature->IsSummon() || creature->IsPet() || creature->IsTotem() ||
                creature->IsCritter() || creature->IsTrigger() ||
                !creature->GetOwnerGUID().IsEmpty())
            { skippedType++; continue; }

            // A witness must belong to the victim's side. Global
            // IsHostileToPlayers() is not sufficient in a contested town and
            // allowed nearby Warsong NPCs to report crimes against Alliance NPCs.
            if (!creature->IsFriendlyTo(victim))
            { skippedHostile++; continue; }

            // Skip creatures already in combat
            if (creature->IsInCombat())
            { skippedCombat++; continue; }

            // Must be a civilian or guard (not a beast, critter, etc.)
            bool isCivilian = sECGuardSystemMgr->IsCreatureCivilian(creature);
            bool isGuard = sECGuardSystemMgr->IsCreatureGuard(creature);
            if (!isCivilian && !isGuard)
            {
                ECG_LOG_TRACE("FindWitness: Skipping %s (entry %u) - not civilian or guard",
                    creature->GetName().c_str(), creature->GetEntry());
                skippedType++;
                continue;
            }

            // Check line of sight if required
            if (m_requireLineOfSight && !creature->IsWithinLOSInMap(victim))
            {
                ECG_LOG_TRACE("FindWitness: Skipping %s (entry %u) - no LoS to victim",
                    creature->GetName().c_str(), creature->GetEntry());
                skippedLoS++;
                continue;
            }

            candidates++;

            // Prefer the closest witness
            float dist = creature->GetDistance(victim);
            ECG_LOG_TRACE("FindWitness: Candidate %s (entry %u, %s) at %.1f yards",
                creature->GetName().c_str(), creature->GetEntry(),
                isGuard ? "guard" : "civilian", dist);

            if (dist < bestDist)
            {
                bestDist = dist;
                bestWitness = creature;
            }
        }

        ECG_LOG_DEBUG("FindWitness: Filter results - candidates=%u, skipped: self=%u dead=%u hostile=%u combat=%u type=%u los=%u",
            candidates, skippedSelf, skippedDead, skippedHostile, skippedCombat, skippedType, skippedLoS);

        if (bestWitness)
        {
            ECG_LOG_DEBUG("FindWitness: Best witness = %s (entry %u) at %.1f yards",
                bestWitness->GetName().c_str(), bestWitness->GetEntry(), bestDist);
        }

        return bestWitness;
    }

    void ECWitnessSystem::MakeWitnessFlee(Creature* witness, Player* criminal)
    {
        if (!witness || !witness->IsInWorld() || witness->IsDuringRemoveFromWorld() ||
            !witness->IsAlive() ||
            !criminal || !criminal->IsInWorld() || !criminal->IsAlive())
        {
            ECG_LOG_DEBUG("MakeWitnessFlee suppressed: witness or criminal is dead/out of world");
            return;
        }

        ECG_LOG_DEBUG("MakeWitnessFlee: Witness %s (entry %u) reacting to criminal %s",
            witness->GetName().c_str(), witness->GetEntry(), criminal->GetName().c_str());

        // Yell about the crime
        static const std::vector<std::string> witnessYells = {
            "Murder! Guards! GUARDS!",
            "Help! Someone call the guards!",
            "I saw it! That criminal killed someone!",
            "Guards! Over here! A murderer!",
            "Somebody stop them! MURDER!",
        };

        std::string chosenYell = witnessYells[urand(0, witnessYells.size() - 1)];
        witness->Yell(chosenYell, LANG_UNIVERSAL);

        // Try to find a nearby guard to flee towards
        Creature* nearestGuard = nullptr;
        float nearestGuardDist = 100.0f;

        std::list<Creature*> nearbyCreatures;
        AnyCreatureInRange checker(witness, 80.0f);
        Acore::CreatureListSearcher<AnyCreatureInRange> searcher(witness, nearbyCreatures, checker);
        Cell::VisitObjects(witness, searcher, 80.0f);

        ECG_LOG_DEBUG("MakeWitnessFlee: Grid search found %zu creatures within 80 yards of witness %s",
            nearbyCreatures.size(), witness->GetName().c_str());

        for (Creature* creature : nearbyCreatures)
        {
            if (creature->GetGUID() == witness->GetGUID())
                continue;

            if (!creature->IsInWorld() || creature->IsDuringRemoveFromWorld() ||
                !creature->IsAlive())
                continue;

            if (sECGuardSystemMgr->IsCreatureGuard(creature))
            {
                float dist = witness->GetDistance(creature);
                if (dist < nearestGuardDist)
                {
                    nearestGuardDist = dist;
                    nearestGuard = creature;
                }
            }
        }

        if (nearestGuard)
        {
            // Flee toward the nearest guard
            witness->GetMotionMaster()->MovePoint(0,
                nearestGuard->GetPositionX(),
                nearestGuard->GetPositionY(),
                nearestGuard->GetPositionZ());

            ECG_LOG_DEBUG("Witness %s fleeing toward guard %s (%.0f yards away)",
                witness->GetName().c_str(), nearestGuard->GetName().c_str(), nearestGuardDist);
        }
        else
        {
            // No guard nearby - flee in panic away from the criminal
            witness->GetMotionMaster()->MoveFleeing(criminal, m_alertDurationMs);

            ECG_LOG_DEBUG("Witness %s fleeing in panic (no guard nearby)",
                witness->GetName().c_str());
        }

        // Emit to bridge
        sGuardSystemBridge->EmitWitnessFleeing(witness, criminal, nearestGuard != nullptr, chosenYell);
    }

    void ECWitnessSystem::Update(uint32 diff)
    {
        if (!m_enabled)
            return;

        std::vector<WitnessAlert> toConfirm;
        std::vector<WitnessAlert> toSuppress;
        struct CountdownNotification
        {
            ObjectGuid criminalGuid;
            ObjectGuid witnessGuid;
            std::string witnessName;
            uint32 crimeMapId;
            uint32 crimeInstanceId;
            uint32 remainingSeconds;
        };
        std::vector<CountdownNotification> countdownNotifications;

        {
            std::lock_guard<std::mutex> lock(m_alertMutex);

            if (!m_activeAlerts.empty())
            {
                ECG_LOG_TRACE("Update: Processing %zu active witness alerts (diff=%u ms)",
                    m_activeAlerts.size(), diff);
            }

            auto it = m_activeAlerts.begin();
            while (it != m_activeAlerts.end())
            {
                WitnessAlert& alert = *it;

                if (alert.witnessGuid == alert.victimGuid)
                {
                    alert.suppressed = true;
                    toSuppress.push_back(alert);
                    ECG_LOG_ERROR("Update: rejected self-witness alert GUID=%s type=%u",
                        alert.witnessGuid.ToString().c_str(),
                        static_cast<uint8>(alert.crimeType));
                    it = m_activeAlerts.erase(it);
                    continue;
                }

                // Creature-death hooks mark matching alerts while holding the
                // same mutex. Honor that state before any timer or countdown
                // work so a killed witness can never report on the expiry tick.
                if (alert.suppressed)
                {
                    toSuppress.push_back(alert);
                    it = m_activeAlerts.erase(it);
                    continue;
                }

                // Resolve the witness on the map/instance where the crime
                // happened. Looking it up relative to a criminal who has since
                // teleported falsely treats a surviving witness as despawned.
                Player* criminal = ObjectAccessor::FindPlayer(alert.criminalGuid);
                Map* originMap = sMapMgr->FindMap(alert.crimeMapId, alert.crimeInstanceId);
                Creature* witness = originMap ? originMap->GetCreature(alert.witnessGuid) : nullptr;

                if (!witness || !witness->IsInWorld() || witness->IsDuringRemoveFromWorld() ||
                    !witness->IsAlive())
                {
                    // Witness killed or despawned - crime suppressed!
                    alert.suppressed = true;
                    toSuppress.push_back(alert);

                    ECG_LOG_DEBUG("Update: Witness %s for criminal %s - %s (elapsed %u/%u ms)",
                        alert.witnessGuid.ToString().c_str(),
                        criminal ? criminal->GetName().c_str() : "offline",
                        !witness ? "DESPAWNED" : "KILLED",
                        alert.elapsedMs, alert.alertTimerMs);

                    it = m_activeAlerts.erase(it);
                    continue;
                }

                // Update timer without overflowing, and retain the previous value
                // so countdown messages fire exactly once per crossed second.
                uint32 const previousElapsedMs = alert.elapsedMs;
                uint32 const remainingBeforeUpdate = alert.alertTimerMs - alert.elapsedMs;
                alert.elapsedMs = diff >= remainingBeforeUpdate
                    ? alert.alertTimerMs
                    : alert.elapsedMs + diff;

                if (alert.elapsedMs >= alert.alertTimerMs)
                {
                    // Time's up - crime confirmed!
                    alert.confirmed = true;
                    toConfirm.push_back(alert);

                    ECG_LOG_DEBUG("Update: Alert EXPIRED for criminal %s - witness %s survived %u ms, crime CONFIRMED",
                        criminal ? criminal->GetName().c_str() : "offline",
                        witness->GetName().c_str(), alert.alertTimerMs);

                    it = m_activeAlerts.erase(it);
                    continue;
                }

                uint32 const previousRemainingSeconds =
                    ((alert.alertTimerMs - previousElapsedMs) / 1000) +
                    ((alert.alertTimerMs - previousElapsedMs) % 1000 == 0 ? 0 : 1);
                uint32 const remainingSeconds =
                    ((alert.alertTimerMs - alert.elapsedMs) / 1000) +
                    ((alert.alertTimerMs - alert.elapsedMs) % 1000 == 0 ? 0 : 1);

                if (remainingSeconds > 0 && remainingSeconds < previousRemainingSeconds)
                {
                    ECG_LOG_TRACE("Update: Alert countdown - %s has %u seconds left to stop %s",
                        criminal ? criminal->GetName().c_str() : "offline",
                        remainingSeconds,
                        witness->GetName().c_str());

                    if (criminal && criminal->IsInWorld())
                        countdownNotifications.push_back(
                            { alert.criminalGuid, alert.witnessGuid, witness->GetName(),
                              alert.crimeMapId, alert.crimeInstanceId, remainingSeconds });
                }

                ++it;
            }
        }

        // Send chat outside the alert mutex; only immutable notification data was
        // copied while the alert was locked.
        for (CountdownNotification const& notification : countdownNotifications)
        {
            Map* originMap = sMapMgr->FindMap(
                notification.crimeMapId, notification.crimeInstanceId);
            Creature* witness = originMap ? originMap->GetCreature(notification.witnessGuid) : nullptr;
            if (!witness || !witness->IsInWorld() || witness->IsDuringRemoveFromWorld() ||
                !witness->IsAlive())
                continue;

            if (Player* criminal = ObjectAccessor::FindPlayer(notification.criminalGuid);
                criminal && criminal->IsInWorld())
            {
                ChatHandler(criminal->GetSession()).PSendSysMessage(
                    "|cffff8800[WITNESS]|r {} reports your crime in {} {}!",
                    notification.witnessName, notification.remainingSeconds,
                    notification.remainingSeconds == 1 ? "second" : "seconds");
            }
        }

        // Process confirmed crimes (outside lock)
        for (const auto& alert : toConfirm)
        {
            ECG_LOG_INFO("Update: Processing CONFIRMED crime (type %u, %u points)",
                static_cast<uint8>(alert.crimeType), alert.crimePoints);
            ConfirmCrime(alert);
        }

        // Process suppressed crimes (outside lock)
        for (const auto& alert : toSuppress)
        {
            Player* criminal = ObjectAccessor::FindPlayer(alert.criminalGuid);
            ECG_LOG_INFO("Update: Processing SUPPRESSED crime for %s",
                criminal ? criminal->GetName().c_str() : "offline");
            SuppressCrime(alert, criminal);
        }
    }

    void ECWitnessSystem::OnCreatureDeath(Creature* creature, Unit* killer)
    {
        if (!m_enabled || !creature)
            return;

        ECG_LOG_TRACE("OnCreatureDeath: %s (entry %u, GUID %s) killed by %s",
            creature->GetName().c_str(), creature->GetEntry(),
            creature->GetGUID().ToString().c_str(),
            killer ? killer->GetName().c_str() : "unknown");

        std::lock_guard<std::mutex> lock(m_alertMutex);

        // Check if the dead creature was a witness
        // NOTE: A witness can have MULTIPLE active alerts if they saw multiple crimes
        // We must suppress ALL of them, not just the first one
        uint32 suppressedCount = 0;
        for (auto it = m_activeAlerts.begin(); it != m_activeAlerts.end(); ++it)
        {
            if (it->witnessGuid == creature->GetGUID())
            {
                it->suppressed = true;
                suppressedCount++;

                ECG_LOG_INFO("OnCreatureDeath: WITNESS %s killed by %s - marking alert #%u SUPPRESSED (elapsed %u/%u ms)",
                    creature->GetName().c_str(),
                    killer ? killer->GetName().c_str() : "unknown",
                    suppressedCount, it->elapsedMs, it->alertTimerMs);

                // Don't break - continue to suppress ALL alerts from this witness
            }
        }

        if (suppressedCount == 0)
        {
            ECG_LOG_TRACE("OnCreatureDeath: %s was NOT a witness (no matching alerts among %zu active)",
                creature->GetName().c_str(), m_activeAlerts.size());
        }
        else if (suppressedCount > 1)
        {
            ECG_LOG_INFO("OnCreatureDeath: Suppressed %u alerts from witness %s",
                suppressedCount, creature->GetName().c_str());
        }
    }

    void ECWitnessSystem::ConfirmCrime(const WitnessAlert& alert)
    {
        if (alert.witnessGuid == alert.victimGuid)
        {
            ECG_LOG_ERROR("ConfirmCrime: discarded self-witness alert GUID=%s type=%u",
                alert.witnessGuid.ToString().c_str(),
                static_cast<uint8>(alert.crimeType));
            return;
        }

        Player* criminal = ObjectAccessor::FindPlayer(alert.criminalGuid);
        if (!criminal || !criminal->IsInWorld())
        {
            ECG_LOG_WARN("ConfirmCrime: Criminal %s not found or not in world",
                alert.criminalGuid.ToString().c_str());
            return;
        }

        // Death clears wanted state. A report queued before death must not recreate it.
        if (!criminal->IsAlive())
        {
            ECG_LOG_DEBUG("ConfirmCrime: Criminal %s is dead; discarding delayed witness report",
                criminal->GetName().c_str());
            return;
        }

        Map* originMap = sMapMgr->FindMap(alert.crimeMapId, alert.crimeInstanceId);
        Creature* witness = originMap ? originMap->GetCreature(alert.witnessGuid) : nullptr;
        if (!witness || !witness->IsInWorld() || witness->IsDuringRemoveFromWorld() ||
            !witness->IsAlive())
        {
            ECG_LOG_DEBUG("ConfirmCrime: witness %s died or despawned at report boundary; suppressing crime",
                alert.witnessGuid.ToString().c_str());
            SuppressCrime(alert, criminal);
            return;
        }

        Creature* victim = originMap ? originMap->GetCreature(alert.victimGuid) : nullptr;
        bool const inOriginJurisdiction =
            criminal->GetMapId() == alert.crimeMapId &&
            criminal->GetInstanceId() == alert.crimeInstanceId &&
            criminal->GetZoneId() == alert.crimeZoneId &&
            criminal->GetAreaId() == alert.crimeAreaId;

        ECG_LOG_INFO("ConfirmCrime: Crime CONFIRMED - %s committed %s against %s (%u points)",
            criminal->GetName().c_str(),
            alert.crimeType == CrimeType::CIVILIAN_KILL ? "CIVILIAN_KILL" :
            alert.crimeType == CrimeType::CIVILIAN_ATTACK ? "CIVILIAN_ATTACK" :
            alert.crimeType == CrimeType::GUARD_KILL ? "GUARD_KILL" : "GUARD_ATTACK",
            victim ? victim->GetName().c_str() : "despawned",
            alert.crimePoints);

        // Emit to bridge
        sGuardSystemBridge->EmitWitnessCrimeConfirmed(criminal, alert.crimeType, alert.crimePoints);

        // Notify external modules (e.g., mod-gtapolice zone heat boost)
        NotifyCallbacks(criminal, alert.crimeZoneId, WitnessEventType::CONFIRMED, alert.crimePoints);

        // Notify the criminal. A report still records the crime if they fled,
        // but it must not claim or create a local response in a new jurisdiction.
        if (inOriginJurisdiction)
        {
            ChatHandler(criminal->GetSession()).PSendSysMessage(
                "|cffff0000[WANTED]|r The witness reported your crime! Guards are on their way!");
        }
        else
        {
            ChatHandler(criminal->GetSession()).SendSysMessage(
                "|cffff0000[WANTED]|r The witness reported your crime in the original jurisdiction.");
        }

        // Now actually register the crime through the normal system
        if (victim && inOriginJurisdiction)
        {
            ECG_LOG_DEBUG("ConfirmCrime: Victim still alive, routing through AddCrime()");
            sECGuardSystemMgr->AddCrime(criminal, alert.crimeType, victim);
        }
        else
        {
            // Victim is gone (despawned/dead). Retain the witnessed crime's original
            // jurisdiction; the criminal may have moved or been teleported while the
            // report timer was running.
            ECG_LOG_DEBUG("ConfirmCrime: Victim despawned, creating synthetic crime (%u points)", alert.crimePoints);
            WantedState* state = sECGuardSystemMgr->GetOrCreateWantedState(criminal->GetGUID());
            if (state)
            {
                WantedStars oldStars = state->stars;
                ECG_LOG_DEBUG("ConfirmCrime: Before - stars=%u, points=%u", static_cast<uint8>(oldStars), state->crimePoints);
                state->crimePoints += alert.crimePoints;
                state->lastCrimeTime = std::chrono::steady_clock::now();
                state->crimeZoneId = alert.crimeZoneId;
                state->crimeAreaId = alert.crimeAreaId;
                state->crimeFaction = alert.crimeFaction;

                if (alert.crimeType == CrimeType::CIVILIAN_KILL)
                    state->civilianKills++;
                else if (alert.crimeType == CrimeType::GUARD_KILL)
                    state->guardKills++;

                const auto& config = sECGuardSystemMgr->GetEscalationConfig();
                state->UpdateStars(
                    config.pointsFor1Star, config.pointsFor2Star,
                    config.pointsFor3Star, config.pointsFor4Star,
                    config.pointsFor5Star);

                ECG_LOG_DEBUG("ConfirmCrime: After - stars=%u, points=%u",
                    static_cast<uint8>(state->stars), state->crimePoints);

                if (state->stars > oldStars)
                {
                    ECG_LOG_INFO("ConfirmCrime: Stars ESCALATED %u -> %u for %s",
                        static_cast<uint8>(oldStars), static_cast<uint8>(state->stars),
                        criminal->GetName().c_str());

                    uint8 const starCount = static_cast<uint8>(state->stars);
                    ChatHandler(criminal->GetSession()).PSendSysMessage(
                        "|cffff0000[WANTED]|r Your wanted level has increased to {} {}!",
                        starCount, starCount == 1 ? "star" : "stars");

                    // Swap star-specific wanted mark aura (each star has its own icon/tooltip)
                    uint32 oldSpell = GuardSpells::GetWantedMarkForStars(oldStars);
                    if (oldSpell > 0)
                        criminal->RemoveAura(oldSpell);

                    uint32 newSpell = GuardSpells::GetWantedMarkForStars(state->stars);
                    if (newSpell > 0)
                    {
                        ECG_LOG_DEBUG("ConfirmCrime: Swapping wanted aura on %s: spell %u -> %u (%u stars)",
                            criminal->GetName().c_str(), oldSpell, newSpell, static_cast<uint8>(state->stars));
                        criminal->AddAura(newSpell, criminal);
                    }
                }

                if (state->IsWanted() && inOriginJurisdiction)
                {
                    ECG_LOG_DEBUG("ConfirmCrime: Player is wanted, dispatching guards");
                    sECGuardSystemMgr->DispatchGuards(criminal, nullptr);
                }
                else if (state->IsWanted())
                {
                    ECG_LOG_INFO("ConfirmCrime: Recorded origin crime without dispatch after jurisdiction change: player=%s origin=%u/%u/%u/%u current=%u/%u/%u/%u faction=%u",
                        criminal->GetName().c_str(), alert.crimeMapId, alert.crimeInstanceId,
                        alert.crimeZoneId, alert.crimeAreaId, criminal->GetMapId(),
                        criminal->GetInstanceId(), criminal->GetZoneId(), criminal->GetAreaId(),
                        static_cast<uint8>(alert.crimeFaction));
                }
            }
        }
    }

    void ECWitnessSystem::SuppressCrime(const WitnessAlert& alert, Player* criminal)
    {
        if (!criminal || !criminal->IsInWorld())
        {
            ECG_LOG_TRACE("SuppressCrime: Criminal offline or not in world");
            return;
        }

        ECG_LOG_INFO("SuppressCrime: %s silenced the witness (crime type %u, %u points NOT applied)",
            criminal->GetName().c_str(), static_cast<uint8>(alert.crimeType), alert.crimePoints);

        // Emit to bridge
        sGuardSystemBridge->EmitWitnessCrimeSuppressed(criminal, alert.crimeType, alert.crimePoints);

        // Notify external modules (e.g., mod-gtapolice reduced zone heat)
        NotifyCallbacks(criminal, alert.crimeZoneId, WitnessEventType::SUPPRESSED, alert.crimePoints);

        // Notify the criminal they got away with it
        ChatHandler(criminal->GetSession()).PSendSysMessage(
            "|cff00ff00[WITNESS]|r The witness was stopped before reporting. The crime went unreported.");
    }

} // namespace ECGuardSystem
