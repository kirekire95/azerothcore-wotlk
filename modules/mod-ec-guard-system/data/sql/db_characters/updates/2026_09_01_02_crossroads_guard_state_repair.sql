-- Repair only the exact corrupt live snapshot produced while the wrong guard
-- entries were counted as a fallen Crossroads garrison. Re-running this file
-- later must not erase legitimate event progress. owning_faction uses the
-- AzerothCore TeamId enum here: 0=Alliance, 1=Horde.

UPDATE `ec_guard_zone_state`
SET
    `threat_level` = 0,
    `heat_points` = 0,
    `guards_alive` = 14,
    `guards_killed_total` = 0,
    `civilians_killed_total` = 0,
    `alarm_state` = 0,
    `control_state` = 0,
    `current_phase` = 0,
    `owning_faction` = 1
WHERE `zone_id` = 17
  AND `guards_alive` = 0
  AND `guards_killed_total` = 44
  AND `heat_points` = 0
  AND `control_state` = 2;

INSERT IGNORE INTO `ec_guard_zone_state`
    (`zone_id`, `threat_level`, `heat_points`, `guards_alive`,
     `guards_killed_total`, `civilians_killed_total`, `alarm_state`,
     `control_state`, `current_phase`, `owning_faction`)
VALUES
    (17, 0, 0, 14, 0, 0, 0, 0, 0, 1);

-- The Crossroads is Horde-owned even when preserving a later legitimate state.
UPDATE `ec_guard_zone_state`
SET `owning_faction` = 1
WHERE `zone_id` = 17;
