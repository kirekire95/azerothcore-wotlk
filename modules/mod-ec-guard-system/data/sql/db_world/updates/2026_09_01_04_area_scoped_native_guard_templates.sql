-- Harden civilian guard responses against invalid creature templates and
-- zone-wide settlement leakage.
--
-- area_id = 0 is reserved for true city/zone rows. Outdoor settlements use
-- their exact WotLK AreaTable.dbc ID. The C++ loader rejects missing or
-- obviously non-guard templates and the runtime checks area_id again before
-- classifying, alerting, or spawning a guard.

SET @ec_guard_add_area_sql = IF(
    EXISTS (
        SELECT 1
        FROM `information_schema`.`COLUMNS`
        WHERE `TABLE_SCHEMA` = DATABASE()
          AND `TABLE_NAME` = 'ec_guard_system_zone_config'
          AND `COLUMN_NAME` = 'area_id'
    ),
    'SELECT 1',
    'ALTER TABLE `ec_guard_system_zone_config` ADD COLUMN `area_id` INT UNSIGNED NOT NULL DEFAULT 0 AFTER `zone_id`'
);
PREPARE ec_guard_add_area_stmt FROM @ec_guard_add_area_sql;
EXECUTE ec_guard_add_area_stmt;
DEALLOCATE PREPARE ec_guard_add_area_stmt;

INSERT INTO `ec_guard_system_zone_config`
    (`zone_id`, `area_id`, `faction`, `guard_entry_1`, `guard_entry_2`,
     `elite_entry_1`, `captain_entry`, `search_radius`)
VALUES
    -- True city/zone rows.
    (1519,    0, 1,    68,     0,  1756,  1756,  80.0), -- Stormwind
    (1537,    0, 1,  5595,     0,  5595,  5595,  80.0), -- Ironforge; 5596 is a test prop
    (1657,    0, 1,  4262,     0,  4262,  4262,  80.0), -- Darnassus
    (3557,    0, 1, 16733,     0, 16733, 16733,  80.0), -- Exodar
    (1637,    0, 2,  3296,     0, 14304, 14304,  80.0), -- Orgrimmar; 19910 is battlemaster Gargok
    (1497,    0, 2,  7980,     0,  7980,  7980,  80.0), -- Undercity; 12427 is Mountaineer Dolf
    (1638,    0, 2,  3084,     0,  3084,  3084,  80.0), -- Thunder Bluff
    (3487,    0, 2, 16222,     0, 16222, 16222,  80.0), -- Silvermoon
    (3703,    0, 0, 19687,     0, 19687, 19687,  80.0), -- Shattrath City Peacekeeper
    (4395,    0, 0, 30659,     0, 30659, 30659,  80.0), -- Dalaran/Violet Hold Guard
    (  33,   35, 0,  4624,     0,  4624,  4624,  80.0), -- Booty Bay

    -- Exact outdoor settlements.
    ( 440,  976, 0,  9460,     0,  9460,  9460,  80.0), -- Gadgetzan
    ( 618, 2255, 0, 11190,     0, 11190, 11190,  80.0), -- Everlook
    (  17,  392, 0,  3502,     0,  3502,  3502,  80.0), -- Ratchet
    (  17,  380, 2,  3501,     0,  3501,  3501, 100.0), -- Crossroads
    (  14,  362, 2,  5953,     0,  5953,  5953,  80.0), -- Razor Hill
    ( 215,  222, 2,  3084,     0,  3084,  3084,  80.0), -- Bloodhoof Village
    (  85,  159, 2,  7980,     0,  7980,  7980,  80.0), -- Brill
    ( 130,  228, 2,  7980,     0,  7980,  7980, 100.0), -- The Sepulcher
    (3430, 3462, 2, 16222,     0, 16222, 16222,  80.0), -- Fairbreeze Village
    (3433, 3488, 2, 16222,     0, 16222, 16222, 100.0), -- Tranquillien
    (  12,   87, 1,    68,     0,    68,  1756,  80.0), -- Goldshire
    (  40,  108, 1,  8096,     0,  8096,  8096, 100.0), -- Sentinel Hill
    (  44,   69, 1, 10037,     0, 10037, 10037, 100.0), -- Lakeshire
    (   1,  131, 1,   727,     0,   727,   727,  80.0), -- Kharanos
    (  38,  144, 1,  8055,     0,  8055,  8055, 100.0), -- Thelsamar
    ( 141,  186, 1,  4262,     0,  4262,  4262,  80.0), -- Dolanaar
    ( 148,  442, 1,  4262,     0,  4262,  4262, 100.0), -- Auberdine
    (3524, 3576, 1, 16733,     0, 16733, 16733,  80.0), -- Azure Watch
    (3525, 3584, 1, 16733,     0, 16733, 16733, 100.0), -- Blood Watch
    ( 331,  415, 1,  6087,     0,  6087,  6087, 100.0), -- Astranaar
    ( 331,  431, 2, 12903,     0, 12903, 12903, 100.0), -- Splintertree Post
    ( 267,  271, 1,  2386,     0,  2386,  2386, 100.0), -- Southshore
    ( 267,  272, 2,  2405,     0,  2405,  2405, 100.0), -- Tarren Mill
    (  33,  117, 2,  1064,     0,  1064,  1064, 100.0)  -- Grom'gol
ON DUPLICATE KEY UPDATE
    `area_id` = VALUES(`area_id`),
    `guard_entry_1` = VALUES(`guard_entry_1`),
    `guard_entry_2` = VALUES(`guard_entry_2`),
    `elite_entry_1` = VALUES(`elite_entry_1`),
    `captain_entry` = VALUES(`captain_entry`),
    `search_radius` = VALUES(`search_radius`);

-- The old row treated subzone 35 as a top-level zone, so GetZoneId() could
-- never select it while a player was in Booty Bay (area 35, parent zone 33).
DELETE FROM `ec_guard_system_zone_config`
WHERE `zone_id` = 35 AND `faction` = 0;

-- Astranaar's permanent local guards should be authoritative even before a
-- zone mapping lookup. This is idempotent.
UPDATE `creature_template`
SET `flags_extra` = `flags_extra` | 32768
WHERE `entry` = 6087;

-- The legacy strategic state remains zone-keyed, but its Ashenvale row is the
-- Astranaar garrison. Remove Darnassus Sentinel 4262 from every strategic
-- response field so it cannot be selected anywhere in Ashenvale.
UPDATE `ec_guard_zone_config`
SET `guard_entry_list` = '6087',
    `elite_entry_list` = '6087',
    `captain_entry` = 6087
WHERE `zone_id` = 331 AND `faction` = 1;

-- Rollback (retain the corrected creature IDs but disable area enforcement):
-- UPDATE `ec_guard_system_zone_config` SET `area_id` = 0;
