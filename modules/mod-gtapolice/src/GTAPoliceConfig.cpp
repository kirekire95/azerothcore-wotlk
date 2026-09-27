/*
 * mod-gtapolice
 * Configuration loader implementation
 */

#include "GTAPoliceConfig.h"
#include "Config.h"
#include "Log.h"

void GTAPoliceConfig::Load()
{
    // General
    Enabled = sConfigMgr->GetOption<bool>("GTAPolice.Enable", true);
    DebugLog = sConfigMgr->GetOption<bool>("GTAPolice.DebugLog", false);
    AffectPlayerbots = sConfigMgr->GetOption<bool>("GTAPolice.AffectPlayerbots", false);
    AllowDirectBookingWithoutTransport =
        sConfigMgr->GetOption<bool>("GTAPolice.AllowDirectBookingWithoutTransport", false);

    // Creature entries
    OfficerEntry = sConfigMgr->GetOption<uint32>("GTAPolice.OfficerEntry", 90010);
    CarEntry = sConfigMgr->GetOption<uint32>("GTAPolice.CarEntry", 90011);
    CrewOfficerEntry = sConfigMgr->GetOption<uint32>("GTAPolice.CrewOfficerEntry", 90012);
    UseCustomCarModel = sConfigMgr->GetOption<bool>("GTAPolice.UseCustomCarModel", false);
    CustomCarDisplayId = sConfigMgr->GetOption<uint32>("GTAPolice.CustomCarDisplayId", 900400);
    FallbackCarDisplayId = sConfigMgr->GetOption<uint32>("GTAPolice.FallbackCarDisplayId", 24240);

    // Vehicle seats
    SeatDriver = sConfigMgr->GetOption<int8>("GTAPolice.SeatDriver", 0);
    SeatCrewPassenger = sConfigMgr->GetOption<int8>("GTAPolice.SeatCrewPassenger", 1);
    SeatPrisoner = sConfigMgr->GetOption<int8>("GTAPolice.SeatPrisoner", 2);

    // Crew system
    CrewSize = sConfigMgr->GetOption<uint32>("GTAPolice.CrewSize", 2);
    CrewExitRange = sConfigMgr->GetOption<float>("GTAPolice.CrewExitRange", 12.0f);
    CrewArrestRange = sConfigMgr->GetOption<float>("GTAPolice.CrewArrestRange", 3.0f);
    CrewEscapeRange = sConfigMgr->GetOption<float>("GTAPolice.CrewEscapeRange", 40.0f);
    CrewExitDelay = sConfigMgr->GetOption<uint32>("GTAPolice.CrewExitDelay", 500);
    CrewLoadingTimeout = sConfigMgr->GetOption<uint32>("GTAPolice.CrewLoadingTimeout", 5000);
    CrewRunSpeed = sConfigMgr->GetOption<float>("GTAPolice.CrewRunSpeed", 1.5f);
    CrewDespawnNonDriver = sConfigMgr->GetOption<bool>("GTAPolice.CrewDespawnNonDriver", true);

    // Spells
    SpellArrestStun = sConfigMgr->GetOption<uint32>("GTAPolice.SpellArrestStun", 853);
    SpellArrestRoot = sConfigMgr->GetOption<uint32>("GTAPolice.SpellArrestRoot", 122);
    SpellSirenAura = sConfigMgr->GetOption<uint32>("GTAPolice.SpellSirenAura", 0);

    // Officer behavior
    OfficerScanRange = sConfigMgr->GetOption<float>("GTAPolice.OfficerScanRange", 60.0f);
    OfficerScanInterval = sConfigMgr->GetOption<uint32>("GTAPolice.OfficerScanInterval", 1500);

    // Pursuit
    CarSpawnDistance = sConfigMgr->GetOption<float>("GTAPolice.CarSpawnDistance", 18.0f);
    CarChaseRepathDistance = sConfigMgr->GetOption<float>("GTAPolice.CarChaseRepathDistance", 2.0f);
    CarArrestRange = sConfigMgr->GetOption<float>("GTAPolice.CarArrestRange", 6.0f);
    CarChaseTimeout = sConfigMgr->GetOption<uint32>("GTAPolice.CarChaseTimeout", 45000);
    CarEscapeDistance = sConfigMgr->GetOption<float>("GTAPolice.CarEscapeDistance", 100.0f);

    // Transport
    TransportFailsafe = sConfigMgr->GetOption<uint32>("GTAPolice.TransportFailsafe", 20000);
    ArrivalDistance = sConfigMgr->GetOption<float>("GTAPolice.ArrivalDistance", 8.0f);

    // Jail
    JailMap = sConfigMgr->GetOption<uint32>("GTAPolice.JailMap", 0);
    JailX = sConfigMgr->GetOption<float>("GTAPolice.JailX", -8823.0f);
    JailY = sConfigMgr->GetOption<float>("GTAPolice.JailY", 622.0f);
    JailZ = sConfigMgr->GetOption<float>("GTAPolice.JailZ", 95.0f);
    JailO = sConfigMgr->GetOption<float>("GTAPolice.JailO", 0.0f);

    // Guard System Integration
    UseGuardSystemWanted = sConfigMgr->GetOption<bool>("GTAPolice.UseGuardSystemWanted", true);
    MinWantedStarsForPursuit = sConfigMgr->GetOption<uint8>("GTAPolice.MinWantedStarsForPursuit", 2);
    MinWantedStarsForArrest = sConfigMgr->GetOption<uint8>("GTAPolice.MinWantedStarsForArrest", 3);
    ClearWantedOnJail = sConfigMgr->GetOption<bool>("GTAPolice.ClearWantedOnJail", true);

    // Bridge
    EmitBridgeEvents = sConfigMgr->GetOption<bool>("GTAPolice.EmitBridgeEvents", true);

    // Star decay
    StarDecayEnabled = sConfigMgr->GetOption<bool>("GTAPolice.StarDecayEnabled", true);
    StarDecayIntervalMs = sConfigMgr->GetOption<uint32>("GTAPolice.StarDecayIntervalMs", 60000);
    StarDecayGracePeriodMs = sConfigMgr->GetOption<uint32>("GTAPolice.StarDecayGracePeriodMs", 30000);
    StarDecayRequiredEvasionMs = sConfigMgr->GetOption<uint32>("GTAPolice.StarDecayRequiredEvasionMs", 30000);

    // Multi-car pursuit settings
    MaxCarsPerPursuit = sConfigMgr->GetOption<uint32>("GTAPolice.MaxCarsPerPursuit", 4);
    CarReinforceIntervalMs = sConfigMgr->GetOption<uint32>("GTAPolice.CarReinforceIntervalMs", 30000);
    CarSpawnMinDistance = sConfigMgr->GetOption<float>("GTAPolice.CarSpawnMinDistance", 12.0f);
    CarSpawnMaxDistance = sConfigMgr->GetOption<float>("GTAPolice.CarSpawnMaxDistance", 25.0f);

    // Load cars per star level (index 0 is unused)
    CarsPerStar[0] = 0;
    for (int i = 1; i <= 5; ++i)
    {
        std::string key = "GTAPolice.CarsAtStar" + std::to_string(i);
        uint32 defaultCars = (i <= 2) ? 1 : (i - 1);  // 1,1,2,3,4 by default
        CarsPerStar[i] = sConfigMgr->GetOption<uint32>(key, defaultCars);
    }

    // Radio chatter
    RadioChatterEnabled = sConfigMgr->GetOption<bool>("GTAPolice.RadioChatterEnabled", true);
    RadioChatterIntervalMs = sConfigMgr->GetOption<uint32>("GTAPolice.RadioChatterIntervalMs", 8000);

    // Bribe system
    BribeEnabled = sConfigMgr->GetOption<bool>("GTAPolice.BribeEnabled", true);
    BribeCostPerStar = sConfigMgr->GetOption<uint32>("GTAPolice.BribeCostPerStar", 50000);  // 5 gold per star
    BribeCooldownMs = sConfigMgr->GetOption<uint32>("GTAPolice.BribeCooldownMs", 300000);    // 5 minutes
    BribeMaxStars = sConfigMgr->GetOption<uint8>("GTAPolice.BribeMaxStars", 3);

    // Pay-n-Spray
    PaynSprayEnabled = sConfigMgr->GetOption<bool>("GTAPolice.PaynSprayEnabled", true);
    PaynSprayCostPerStar = sConfigMgr->GetOption<uint32>("GTAPolice.PaynSprayCostPerStar", 100000);  // 10 gold per star
    PaynSprayCooldownMs = sConfigMgr->GetOption<uint32>("GTAPolice.PaynSprayCooldownMs", 600000);     // 10 minutes
    PaynSprayNPCEntry = sConfigMgr->GetOption<uint32>("GTAPolice.PaynSprayNPCEntry", 90015);

    // Zone heat system
    ZoneHeatEnabled = sConfigMgr->GetOption<bool>("GTAPolice.ZoneHeatEnabled", true);
    ZoneHeatDecayIntervalMs = sConfigMgr->GetOption<uint32>("GTAPolice.ZoneHeatDecayIntervalMs", 60000);
    ZoneHeatDecayAmount = sConfigMgr->GetOption<uint32>("GTAPolice.ZoneHeatDecayAmount", 5);
    ZoneHeatPerCrime = sConfigMgr->GetOption<uint32>("GTAPolice.ZoneHeatPerCrime", 10);
    ZoneHeatThresholdLow = sConfigMgr->GetOption<uint32>("GTAPolice.ZoneHeatThresholdLow", 30);
    ZoneHeatThresholdHigh = sConfigMgr->GetOption<uint32>("GTAPolice.ZoneHeatThresholdHigh", 80);

    // Witness system integration
    WitnessAlertsEnabled = sConfigMgr->GetOption<bool>("GTAPolice.WitnessAlertsEnabled", true);
    WitnessDetectedHeat = sConfigMgr->GetOption<uint32>("GTAPolice.WitnessDetectedHeat", 5);
    WitnessConfirmedHeatBonus = sConfigMgr->GetOption<uint32>("GTAPolice.WitnessConfirmedHeatBonus", 10);
    WitnessSuppressedHeatReduction = sConfigMgr->GetOption<uint32>("GTAPolice.WitnessSuppressedHeatReduction", 3);

    if (Enabled)
    {
        LOG_INFO("module", "[mod-gtapolice] v2.1 | Configuration loaded:");
        LOG_INFO("module", "  Officer Entry: {}, Car Entry: {}, Crew Entry: {}", OfficerEntry, CarEntry, CrewOfficerEntry);
        LOG_INFO("module", "  Playerbots: {}, Direct booking without transport: {}",
            AffectPlayerbots ? "included" : "ignored",
            AllowDirectBookingWithoutTransport ? "enabled" : "disabled");
        LOG_INFO("module", "  Car Model: {} (custom={}, fallback={})",
            UseCustomCarModel ? "custom" : "template", CustomCarDisplayId, FallbackCarDisplayId);
        LOG_INFO("module", "  Crew Size: {}, Exit Range: {:.1f}, Arrest Range: {:.1f}", CrewSize, CrewExitRange, CrewArrestRange);
        LOG_INFO("module", "  Seats: Driver={}, Passenger={}, Prisoner={}", SeatDriver, SeatCrewPassenger, SeatPrisoner);
        LOG_INFO("module", "  Guard System Integration: {}", UseGuardSystemWanted ? "enabled" : "disabled");
        LOG_INFO("module", "  Bridge Events: {}", EmitBridgeEvents ? "enabled" : "disabled");
        LOG_INFO("module", "  Star Decay: {} (interval={}ms, grace={}ms, evasion={}ms)",
            StarDecayEnabled ? "enabled" : "disabled",
            StarDecayIntervalMs, StarDecayGracePeriodMs, StarDecayRequiredEvasionMs);
        LOG_INFO("module", "  Multi-Car: Max={}, Reinforce={}ms, Spawn={:.1f}-{:.1f} yards",
            MaxCarsPerPursuit, CarReinforceIntervalMs, CarSpawnMinDistance, CarSpawnMaxDistance);
        LOG_INFO("module", "  Cars per star: 1={}, 2={}, 3={}, 4={}, 5={}",
            CarsPerStar[1], CarsPerStar[2], CarsPerStar[3], CarsPerStar[4], CarsPerStar[5]);
        LOG_INFO("module", "  Radio Chatter: {} (interval={}ms)",
            RadioChatterEnabled ? "enabled" : "disabled", RadioChatterIntervalMs);
        LOG_INFO("module", "  Bribe: {} (cost={}/star, cd={}ms, maxStars={})",
            BribeEnabled ? "enabled" : "disabled", BribeCostPerStar, BribeCooldownMs, BribeMaxStars);
        LOG_INFO("module", "  Pay-n-Spray: {} (cost={}/star, cd={}ms, NPC={})",
            PaynSprayEnabled ? "enabled" : "disabled", PaynSprayCostPerStar, PaynSprayCooldownMs, PaynSprayNPCEntry);
        LOG_INFO("module", "  Zone Heat: {} (decay={}ms, per-crime={}, low={}, high={})",
            ZoneHeatEnabled ? "enabled" : "disabled", ZoneHeatDecayIntervalMs,
            ZoneHeatPerCrime, ZoneHeatThresholdLow, ZoneHeatThresholdHigh);
        LOG_INFO("module", "  Witness Integration: {} (detected={}, confirmed={}, suppressed=-{})",
            WitnessAlertsEnabled ? "enabled" : "disabled",
            WitnessDetectedHeat, WitnessConfirmedHeatBonus, WitnessSuppressedHeatReduction);
    }
}

void GTAPoliceConfig::Reload()
{
    LOG_INFO("module", "[mod-gtapolice] Reloading configuration...");
    Load();
}
