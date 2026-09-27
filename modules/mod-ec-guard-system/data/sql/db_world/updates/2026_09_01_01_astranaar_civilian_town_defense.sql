-- Repair Astranaar's faction-correct civilian alarm and guard mapping.
-- 3495 is Gagsprocket, not an Astranaar guard; 6087 is Astranaar Sentinel.

ALTER TABLE `ec_guard_system_zone_config`
    ADD COLUMN IF NOT EXISTS `area_id` INT UNSIGNED NOT NULL DEFAULT 0
        AFTER `zone_id`;

INSERT INTO `ec_guard_system_zone_config`
    (`zone_id`, `area_id`, `faction`, `guard_entry_1`, `guard_entry_2`, `elite_entry_1`, `captain_entry`, `search_radius`)
VALUES
    (331, 415, 1, 6087, 0, 6087, 6087, 100.0)
ON DUPLICATE KEY UPDATE
    `area_id` = VALUES(`area_id`),
    `guard_entry_1` = VALUES(`guard_entry_1`),
    `guard_entry_2` = VALUES(`guard_entry_2`),
    `elite_entry_1` = VALUES(`elite_entry_1`),
    `captain_entry` = VALUES(`captain_entry`),
    `search_radius` = VALUES(`search_radius`);

INSERT INTO `ec_guard_zone_config`
    (`zone_id`, `zone_name`, `faction`, `initial_guard_count`, `guard_entry_list`,
     `elite_entry_list`, `captain_entry`, `bounty_hunter_entry`, `search_radius`,
     `heat_multiplier`, `enabled`)
VALUES
    (331, 'Ashenvale', 1, 11, '6087', '6087', 6087, 0, 100.0, 1.0, 1)
ON DUPLICATE KEY UPDATE
    `zone_name` = VALUES(`zone_name`),
    `faction` = VALUES(`faction`),
    `initial_guard_count` = VALUES(`initial_guard_count`),
    `guard_entry_list` = VALUES(`guard_entry_list`),
    `elite_entry_list` = VALUES(`elite_entry_list`),
    `captain_entry` = VALUES(`captain_entry`),
    `search_radius` = VALUES(`search_radius`),
    `enabled` = VALUES(`enabled`);

-- Make the permanent local sentinels authoritative guards even before zone
-- configuration is consulted.
UPDATE `creature_template`
SET `flags_extra` = `flags_extra` | 32768
WHERE `entry` = 6087;
