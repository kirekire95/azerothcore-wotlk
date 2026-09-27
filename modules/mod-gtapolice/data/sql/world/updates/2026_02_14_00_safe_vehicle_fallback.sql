-- Prevent client freezes when custom Vehicle.dbc entry 9001 is not installed.
-- Keeps mod-gtapolice functional in safe no-vehicle arrest mode.
UPDATE `creature_template`
SET `VehicleId` = 0
WHERE `entry` = 90011
  AND `VehicleId` = 9001;
