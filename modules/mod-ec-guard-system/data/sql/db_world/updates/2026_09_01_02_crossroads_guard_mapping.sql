-- Correct the Crossroads' ordinary civilian response for WotLK-era data.
--
-- 3501 = Horde Guard, the native level-40 guard with 14 permanent spawns in
-- Crossroads area 380. 3296 is an Orgrimmar Grunt and 5953 is a Razor Hill
-- Grunt; neither belongs in an ordinary Crossroads civilian alarm.

ALTER TABLE `ec_guard_system_zone_config`
    ADD COLUMN IF NOT EXISTS `area_id` INT UNSIGNED NOT NULL DEFAULT 0
        AFTER `zone_id`;

INSERT INTO `ec_guard_system_zone_config`
    (`zone_id`, `area_id`, `faction`, `guard_entry_1`, `guard_entry_2`, `elite_entry_1`,
     `captain_entry`, `search_radius`)
VALUES
    (17, 380, 2, 3501, 0, 3501, 3501, 100.0)
ON DUPLICATE KEY UPDATE
    `area_id` = VALUES(`area_id`),
    `guard_entry_1` = VALUES(`guard_entry_1`),
    `guard_entry_2` = VALUES(`guard_entry_2`),
    `elite_entry_1` = VALUES(`elite_entry_1`),
    `captain_entry` = VALUES(`captain_entry`),
    `search_radius` = VALUES(`search_radius`);

-- The legacy strategic table is zone-keyed, but the C++ guard-state matcher
-- scopes this Horde row to area 380 so other Barrens Horde Guards cannot mutate
-- the Crossroads' 14-guard garrison.
INSERT INTO `ec_guard_zone_config`
    (`zone_id`, `zone_name`, `faction`, `initial_guard_count`, `guard_entry_list`,
     `elite_entry_list`, `captain_entry`, `bounty_hunter_entry`, `search_radius`,
     `heat_multiplier`, `enabled`)
VALUES
    (17, 'The Crossroads', 2, 14, '3501', '3501', 0, 0, 100.0, 1.0, 1)
ON DUPLICATE KEY UPDATE
    `zone_name` = VALUES(`zone_name`),
    `faction` = VALUES(`faction`),
    `initial_guard_count` = VALUES(`initial_guard_count`),
    `guard_entry_list` = VALUES(`guard_entry_list`),
    `elite_entry_list` = VALUES(`elite_entry_list`),
    `captain_entry` = VALUES(`captain_entry`),
    `search_radius` = VALUES(`search_radius`),
    `enabled` = VALUES(`enabled`);
