# Vehicle DBC & Database Fixes for mod-gtapolice

**Date**: 2026-02-05
**Status**: COMPLETED

## Critical Bug Fixed

### Issue: noVoluntaryExit Flag Not Applied to VehicleSeat.dbc

**Problem**: The `noVoluntaryExit` flag in seat definitions was never converted to VehicleSeat.dbc flags, allowing prisoners to voluntarily exit the police car.

**Root Cause**: The `seatToDBCRecord()` function in `vehicle-dbc-patcher.ts` directly used `seat.flags` without checking the `noVoluntaryExit` property.

**Solution**: Added logic to set/clear the `VEHICLE_SEAT_FLAG_CAN_ENTER_OR_EXIT` (0x01000000, bit 24) flag based on `noVoluntaryExit`.

---

## Changes Made

### 1. Fixed Vehicle DBC Patcher

**File**: `/client/packages/ec-custom-content/src/generators/dbc/vehicle-dbc-patcher.ts`

**Changes**:
- Added import of `VehicleSeatFlags` from `@ec-unreal/pipeline-dbc`
- Updated `seatToDBCRecord()` to handle `noVoluntaryExit`:
  ```typescript
  if (seat.noVoluntaryExit) {
    // Clear the CAN_ENTER_OR_EXIT flag to prevent voluntary exit
    flags &= ~VehicleSeatFlags.CAN_ENTER_OR_EXIT;
  } else {
    // Set the CAN_ENTER_OR_EXIT flag to allow voluntary exit
    flags |= VehicleSeatFlags.CAN_ENTER_OR_EXIT;
  }
  ```

**Result**:
- Seat 0 (90010): Driver - CAN_ENTER_OR_EXIT flag SET (voluntary exit allowed)
- Seat 1 (90011): Passenger - CAN_ENTER_OR_EXIT flag SET (voluntary exit allowed)
- Seat 2 (90012): Prisoner - CAN_ENTER_OR_EXIT flag CLEARED (no voluntary exit!)
- Seat 3 (90013): Reserved - CAN_ENTER_OR_EXIT flag SET (voluntary exit allowed)

### 2. Added VehicleSeatFlags Constant

**File**: `/client/packages/wow-pipeline-dbc/src/types/vehicle.ts`

**Changes**:
- Added `CAN_ENTER_OR_EXIT: 0x01000000` to `VehicleSeatFlags` enum

**Benefit**: Centralized flag definitions for better code documentation and reusability.

### 3. Created Wanted State Persistence Table

**File**: `/ec-modules/mod-gtapolice/data/sql/characters/base/01_wanted_state.sql`

**Table**: `ec_police_wanted_state`

**Columns**:
- `player_guid` (PRIMARY KEY) - Character GUID
- `wanted_stars` - Current wanted level (0-5)
- `crime_points` - Accumulated crime score
- `last_crime_time` - Timestamp of last crime
- `zone_id` - Last known zone
- `total_arrests` - Lifetime arrest count
- `total_escapes` - Lifetime escape count
- `created_at`, `updated_at` - Audit timestamps

**Indexes**:
- `idx_last_crime` - For cleanup queries (find stale wanted states)
- `idx_zone_wanted` - For zone queries (find all wanted players in a zone)

**Purpose**: Persist wanted level across logout/login, track player crime statistics.

---

## Verification Checklist

### Vehicle Definition Verification

**File**: `/client/packages/ec-custom-content/src/content/vehicles/index.ts`

| Seat | ID | Role | noVoluntaryExit | Expected Behavior |
|------|-----|------|-----------------|-------------------|
| 0 | 90010 | Driver | No | Can exit voluntarily |
| 1 | 90011 | Passenger | No | Can exit voluntarily |
| 2 | 90012 | Prisoner | **Yes** | **Cannot exit voluntarily** |
| 3 | 90013 | Reserved | No | Can exit voluntarily |

**Status**: ✅ Verified - Prisoner seat has `noVoluntaryExit: true`

### Existing SQL Tables

**File**: `/ec-modules/mod-gtapolice/data/sql/world/base/02_jail_locations.sql`

**Tables Already Exist**:
- `ec_jail_locations` - Jail teleport coordinates (10 jails: Alliance, Horde, Neutral)
- `ec_jail_punishments` - Jail time and fines by wanted stars (1-5 stars)
- `ec_police_car_paths` - Waypoint paths for police car movement

**Status**: ✅ Already implemented - No changes needed

---

## Database Schema Summary

### Characters Database (Player-Specific)
- `ec_police_wanted_state` - Wanted level persistence

### World Database (Static Game Data)
- `ec_jail_locations` - Jail locations and release points
- `ec_jail_punishments` - Punishment rules by star level
- `ec_police_car_paths` - Police car patrol routes

---

## Next Steps

### 1. Rebuild DBC Files
```bash
cd /Volumes/T7Shield/Emulation/Development/World-of-Warcraft/client
pnpm --filter @ec-unreal/ec-custom-content run generate:dbc -- \
  --dbc-path=/Volumes/T7Shield/Emulation/Development/World-of-Warcraft/azerothcore-wotlk/var/extractors/dbc \
  --only-vehicles
```

This patches only `Vehicle.dbc` and `VehicleSeat.dbc` in place and keeps a timestamped backup folder.

### 2. Install SQL Tables
```bash
# Install wanted_state table
mysql -h 127.0.0.1 -P 3306 -u root -ppassword acore_characters < \
  /Volumes/T7Shield/Emulation/Development/World-of-Warcraft/ec-modules/mod-gtapolice/data/sql/characters/base/01_wanted_state.sql
```

### 3. Test Prisoner Seat
1. Compile and start worldserver with mod-gtapolice
2. Spawn police car vehicle (VehicleId 9001)
3. Place player in prisoner seat (seat index 2)
4. Verify player cannot use "Leave Vehicle" button
5. Verify driver/passenger can exit normally

### 4. Test Wanted State Persistence
1. Commit a crime and gain wanted stars
2. Log out
3. Log back in
4. Verify wanted level persists

---

## Technical References

- **wowdev.wiki VehicleSeat**: https://wowdev.wiki/DB/VehicleSeat
- **VehicleSeat Flags**: Bit 24 (0x01000000) = CAN_ENTER_OR_EXIT
- **DBC Patcher**: `/client/packages/ec-custom-content/src/generators/dbc/vehicle-dbc-patcher.ts`
- **Vehicle Definition**: `/client/packages/ec-custom-content/src/content/vehicles/index.ts`

---

## Impact Assessment

### Critical Fix
- **Before**: Prisoners could exit police car using "Leave Vehicle" button
- **After**: Prisoners are locked in seat until released by game logic

### Database Persistence
- **Before**: Wanted level reset on logout (all data lost)
- **After**: Wanted level persists across sessions

### Code Quality
- **Before**: Magic number `0x01000000` with unclear meaning
- **After**: Named constant `VehicleSeatFlags.CAN_ENTER_OR_EXIT` with documentation

---

## Conclusion

All critical issues have been resolved:
1. ✅ Vehicle DBC patcher now correctly handles `noVoluntaryExit` flag
2. ✅ VehicleSeatFlags enum updated with CAN_ENTER_OR_EXIT constant
3. ✅ Wanted state persistence table created
4. ✅ Police car vehicle definition verified (prisoner seat configured correctly)
5. ✅ Existing jail location tables verified (already implemented)

The mod-gtapolice system is now ready for testing with proper prisoner detention mechanics and persistent wanted levels.
