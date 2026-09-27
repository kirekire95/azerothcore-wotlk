-- Temporary pursuit guards were historically added to guards_alive but were
-- not removed when their timers expired, inflating Ashenvale to 352 guards and
-- leaving 48 transient deaths in the incident counters. Reset that corrupted
-- snapshot to Astranaar's 11 permanent Sentinels at a stable event boundary.
-- owning_faction stores the AzerothCore TeamId enum: 0=Alliance, 1=Horde.

INSERT INTO `ec_guard_zone_state`
    (`zone_id`, `threat_level`, `heat_points`, `guards_alive`, `guards_killed_total`,
     `civilians_killed_total`, `alarm_state`, `control_state`, `current_phase`,
     `owning_faction`)
VALUES
    (331, 0, 0, 11, 0, 0, 0, 0, 0, 0)
ON DUPLICATE KEY UPDATE
    -- Only the known 352/48 snapshot is reset. Re-running this migration after
    -- later legitimate combat must not erase heat, kills, or phase progress.
    `alarm_state` = IF(`guards_alive` > 11 AND `guards_killed_total` = 48
        AND `heat_points` = 0 AND `control_state` = 0, 0, `alarm_state`),
    `guards_killed_total` = IF(`guards_alive` > 11 AND `guards_killed_total` = 48
        AND `heat_points` = 0 AND `control_state` = 0, 0, `guards_killed_total`),
    `guards_alive` = IF(`guards_alive` > 11, 11, `guards_alive`),
    `owning_faction` = 0;
