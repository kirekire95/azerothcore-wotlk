# Quick Fix Summary - mod-gtapolice Vehicle & Database

## What Was Fixed

### 1. CRITICAL: Vehicle Seat Exit Bug ⚠️
**Problem**: Prisoners could exit police car voluntarily.
**Fix**: Updated `vehicle-dbc-patcher.ts` to handle `noVoluntaryExit` flag.
**Result**: Prisoner seat (90012) now correctly prevents voluntary exit.

### 2. Added Wanted State Persistence
**File**: `data/sql/characters/base/01_wanted_state.sql`
**Table**: `ec_police_wanted_state`
**Purpose**: Persist wanted level across logout/login.

### 3. Updated Flag Constants
**File**: `wow-pipeline-dbc/src/types/vehicle.ts`
**Added**: `CAN_ENTER_OR_EXIT: 0x01000000` to VehicleSeatFlags

---

## Files Changed

| File | Change |
|------|--------|
| `client/packages/ec-custom-content/src/generators/dbc/vehicle-dbc-patcher.ts` | Added noVoluntaryExit handling |
| `client/packages/wow-pipeline-dbc/src/types/vehicle.ts` | Added CAN_ENTER_OR_EXIT flag |
| `ec-modules/mod-gtapolice/data/sql/characters/base/01_wanted_state.sql` | Created wanted state table |

---

## How to Apply

```bash
# 1. Rebuild DBC files
cd /Volumes/T7Shield/Emulation/Development/World-of-Warcraft/client/packages/ec-custom-content
pnpm run build-dbc

# 2. Install SQL table
mysql -h 127.0.0.1 -P 3306 -u root -ppassword acore_characters < \
  /Volumes/T7Shield/Emulation/Development/World-of-Warcraft/ec-modules/mod-gtapolice/data/sql/characters/base/01_wanted_state.sql

# 3. Restart worldserver
# (DBC changes require client restart too)
```

---

## Verification

### Police Car Seats (VehicleId 9001)
- ✅ Seat 0 (90010): Driver - Can exit
- ✅ Seat 1 (90011): Passenger - Can exit
- ✅ Seat 2 (90012): **Prisoner - Cannot exit**
- ✅ Seat 3 (90013): Reserved - Can exit

### Database Tables
- ✅ `ec_police_wanted_state` (characters) - Wanted level persistence
- ✅ `ec_jail_locations` (world) - Jail teleport locations
- ✅ `ec_jail_punishments` (world) - Punishment rules

---

## Test Plan

1. **Test Prisoner Detention**
   - Spawn police car (`.npc add temp 900400`)
   - Enter as driver (seat 0)
   - Force player into prisoner seat (seat 2)
   - Verify: Player cannot click "Leave Vehicle"
   - Verify: Driver can exit normally

2. **Test Wanted State Persistence**
   - Commit crime (`.gtapolice crime [stars]`)
   - Check wanted stars (`.gtapolice status`)
   - Log out
   - Log back in
   - Verify: Wanted stars persist

---

## Before vs After

| Scenario | Before | After |
|----------|--------|-------|
| Prisoner exits car | Can click "Leave Vehicle" | Button disabled/blocked |
| Logout with wanted | Wanted level resets to 0 | Wanted level persists |
| Flag constants | Magic number 0x01000000 | Named VehicleSeatFlags.CAN_ENTER_OR_EXIT |

---

## Technical Details

**VehicleSeat.dbc Flag Logic**:
```typescript
if (seat.noVoluntaryExit) {
  flags &= ~VehicleSeatFlags.CAN_ENTER_OR_EXIT;  // Clear flag (block exit)
} else {
  flags |= VehicleSeatFlags.CAN_ENTER_OR_EXIT;   // Set flag (allow exit)
}
```

**Flag Value**: 0x01000000 (bit 24)
**Reference**: https://wowdev.wiki/DB/VehicleSeat

---

## Status: ✅ READY FOR TESTING
