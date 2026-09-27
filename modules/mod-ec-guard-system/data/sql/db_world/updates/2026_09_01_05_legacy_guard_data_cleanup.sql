-- Final cleanup for the legacy guard-state, preparation, and faction-response
-- datasets. This migration is intentionally data-only and idempotent.
--
-- The *_bak_20260901_05 tables preserve the first pre-cleanup version of each
-- affected row. INSERT IGNORE keeps that snapshot intact on repeat execution.

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

CREATE TABLE IF NOT EXISTS `ec_guard_system_zone_config_bak_20260901_05`
    LIKE `ec_guard_system_zone_config`;
INSERT IGNORE INTO `ec_guard_system_zone_config_bak_20260901_05`
    SELECT * FROM `ec_guard_system_zone_config`;

CREATE TABLE IF NOT EXISTS `ec_guard_zone_config_bak_20260901_05`
    LIKE `ec_guard_zone_config`;
INSERT IGNORE INTO `ec_guard_zone_config_bak_20260901_05`
    SELECT * FROM `ec_guard_zone_config`;

CREATE TABLE IF NOT EXISTS `ec_guard_zone_dialogues_bak_20260901_05`
    LIKE `ec_guard_zone_dialogues`;
INSERT IGNORE INTO `ec_guard_zone_dialogues_bak_20260901_05`
    SELECT * FROM `ec_guard_zone_dialogues`;

CREATE TABLE IF NOT EXISTS `ec_guard_zone_dialogue_pairs_bak_20260901_05`
    LIKE `ec_guard_zone_dialogue_pairs`;
INSERT IGNORE INTO `ec_guard_zone_dialogue_pairs_bak_20260901_05`
    SELECT * FROM `ec_guard_zone_dialogue_pairs`;

CREATE TABLE IF NOT EXISTS `ec_guard_militia_spawns_bak_20260901_05`
    LIKE `ec_guard_militia_spawns`;
INSERT IGNORE INTO `ec_guard_militia_spawns_bak_20260901_05`
    SELECT * FROM `ec_guard_militia_spawns`;

CREATE TABLE IF NOT EXISTS `ec_guard_town_criers_bak_20260901_05`
    LIKE `ec_guard_town_criers`;
INSERT IGNORE INTO `ec_guard_town_criers_bak_20260901_05`
    SELECT * FROM `ec_guard_town_criers`;

CREATE TABLE IF NOT EXISTS `ec_guard_faction_response_bak_20260901_05`
    LIKE `ec_guard_faction_response`;
INSERT IGNORE INTO `ec_guard_faction_response_bak_20260901_05`
    SELECT * FROM `ec_guard_faction_response`;

-- --------------------------------------------------------------------------
-- Area-scoped ordinary guard rows
-- --------------------------------------------------------------------------
-- Goldshire uses the Elwynn-native Stormwind Guard (1423), and Menethil uses
-- Menethil Sentry (1434). Duskwood's legacy row was never represented in the
-- ordinary-response table, so assert it here as well.
INSERT INTO `ec_guard_system_zone_config`
    (`zone_id`, `area_id`, `faction`, `guard_entry_1`, `guard_entry_2`,
     `elite_entry_1`, `captain_entry`, `search_radius`)
VALUES
    (12, 87,  1,  1423, 0,  1423,  1423,  80.0),
    (10, 42,  1, 10038, 0, 10038, 10038, 100.0),
    (11, 150, 1,  1434, 0,  1434,  1434, 100.0)
ON DUPLICATE KEY UPDATE
    `area_id` = VALUES(`area_id`),
    `guard_entry_1` = VALUES(`guard_entry_1`),
    `guard_entry_2` = VALUES(`guard_entry_2`),
    `elite_entry_1` = VALUES(`elite_entry_1`),
    `captain_entry` = VALUES(`captain_entry`),
    `search_radius` = VALUES(`search_radius`);

-- There is no summon-safe, settlement-local generic guard for Bloodhoof:
-- Bluffwatcher 3084 belongs to Thunder Bluff and exposes capital-city gossip.
-- Auberdine Sentinel 6086 is local but retains a questgiver service flag;
-- Darnassus Sentinel 4262 is not local. Keep both areas disabled rather than
-- leaking another settlement or cloning a service template.
DELETE FROM `ec_guard_system_zone_config`
WHERE (`zone_id` = 215 AND `faction` = 2)
   OR (`zone_id` = 148 AND `faction` = 1)
   OR (`zone_id` = 35  AND `faction` = 0);

-- --------------------------------------------------------------------------
-- Legacy strategic guard rows
-- --------------------------------------------------------------------------
-- This table remains zone-keyed. Mixed-faction Northrend zones and settlements
-- without a safe local generic template are deliberately absent.
INSERT INTO `ec_guard_zone_config`
    (`zone_id`, `zone_name`, `faction`, `initial_guard_count`,
     `guard_entry_list`, `elite_entry_list`, `captain_entry`,
     `bounty_hunter_entry`, `search_radius`, `heat_multiplier`, `enabled`)
VALUES
    (10,   'Duskwood',              1,  8, '10038',  '10038',  10038, 0,  80.0, 1.0, 1),
    (11,   'Wetlands',              1, 10, '1434',   '1434',    1434, 0,  80.0, 1.0, 1),
    (12,   'Elwynn Forest',         1, 15, '1423',   '1423',    1423, 0,  80.0, 1.0, 1),
    (14,   'Durotar',               2, 15, '5953',   '5953',    5953, 0,  80.0, 1.0, 1),
    (17,   'The Crossroads',        2, 14, '3501',   '3501',    3501, 0, 100.0, 1.0, 1),
    (33,   'Booty Bay',             0, 15, '4624',   '4624',    4624, 0,  80.0, 1.0, 1),
    (38,   'Loch Modan',            1,  8, '8055',   '8055',    8055, 0,  80.0, 1.0, 1),
    (40,   'Westfall',              1, 10, '8096',   '8096',    8096, 0,  80.0, 1.0, 1),
    (44,   'Redridge Mountains',    1, 12, '10037',  '10037',  10037, 0,  80.0, 1.0, 1),
    (85,   'Tirisfal Glades',       2, 10, '7980',   '7980',    7980, 0,  80.0, 1.0, 1),
    (130,  'Silverpine Forest',     2,  8, '7980',   '7980',    7980, 0,  80.0, 1.0, 1),
    (141,  'Teldrassil',            1, 12, '4262',   '4262',    4262, 0,  80.0, 1.0, 1),
    (267,  'Hillsbrad Foothills',   2, 10, '2405',   '2405',    2405, 0,  80.0, 1.0, 1),
    (331,  'Ashenvale',             1, 11, '6087',   '6087',    6087, 0, 100.0, 1.0, 1),
    (440,  'Tanaris',               0, 12, '9460',   '9460',    9460, 0,  80.0, 1.0, 1),
    (618,  'Winterspring',          0, 10, '11190',  '11190',  11190, 0,  80.0, 1.0, 1),
    (1497, 'Undercity',             2, 40, '7980',   '7980',    7980, 0,  80.0, 1.0, 1),
    (1519, 'Stormwind City',        1, 50, '68,1976','1756',    1756, 0,  80.0, 1.0, 1),
    (1537, 'Ironforge',             1, 40, '5595',   '5595',    5595, 0,  80.0, 1.0, 1),
    (1637, 'Orgrimmar',             2, 50, '3296',   '14304',  14304, 0,  80.0, 1.0, 1),
    (1638, 'Thunder Bluff',         2, 30, '3084',   '3084',    3084, 0,  80.0, 1.0, 1),
    (1657, 'Darnassus',             1, 35, '4262',   '4262',    4262, 0,  80.0, 1.0, 1),
    (3487, 'Silvermoon City',       2, 40, '16222',  '16222',  16222, 0,  80.0, 1.0, 1),
    (3557, 'Exodar',                1, 35, '16733',  '16733',  16733, 0,  80.0, 1.0, 1),
    (3703, 'Shattrath City',        0, 40, '19687',  '19687',  19687, 0,  80.0, 1.0, 1),
    (4395, 'Dalaran',               0, 50, '30659',  '30659',  30659, 0,  80.0, 1.0, 1)
ON DUPLICATE KEY UPDATE
    `zone_name` = VALUES(`zone_name`),
    `faction` = VALUES(`faction`),
    `initial_guard_count` = VALUES(`initial_guard_count`),
    `guard_entry_list` = VALUES(`guard_entry_list`),
    `elite_entry_list` = VALUES(`elite_entry_list`),
    `captain_entry` = VALUES(`captain_entry`),
    `bounty_hunter_entry` = VALUES(`bounty_hunter_entry`),
    `search_radius` = VALUES(`search_radius`),
    `heat_multiplier` = VALUES(`heat_multiplier`),
    `enabled` = VALUES(`enabled`);

DELETE FROM `ec_guard_zone_config`
WHERE `zone_id` IN (148, 215, 495, 3537);

-- --------------------------------------------------------------------------
-- Preparation actors and custom dialogue
-- --------------------------------------------------------------------------
-- Correct the three installed Crossroads preparation rows. A local guard may
-- patrol, but it must not be presented as a crate-carrying civilian worker.
UPDATE `ec_guard_militia_spawns`
SET `creature_entry` = 3501, `faction` = 2, `enabled` = 0
WHERE `zone_id` = 17 AND `activity` IN (2, 3)
  AND `spawn_x` BETWEEN -453.0 AND -444.5
  AND `spawn_y` BETWEEN -2662.0 AND -2657.5;

UPDATE `ec_guard_militia_spawns`
SET `creature_entry` = 3501, `faction` = 2, `enabled` = 1
WHERE `zone_id` = 17 AND `activity` = 6
  AND `spawn_x` BETWEEN -449.5 AND -448.0
  AND `spawn_y` BETWEEN -2666.0 AND -2665.0;

-- Normalize every installed seed crier to its own town before disabling the
-- synthetic announcement feature. The coordinate predicates make these
-- updates narrow and safe on repeat runs.
UPDATE `ec_guard_town_criers` SET `crier_entry`=3501, `faction`=2, `location_name`='Crossroads'
WHERE `zone_id`=17 AND `pos_x` BETWEEN -451 AND -449 AND `pos_y` BETWEEN -2661 AND -2659;
UPDATE `ec_guard_town_criers` SET `crier_entry`=468, `faction`=1, `location_name`='Goldshire'
WHERE `zone_id`=12 AND `pos_x` BETWEEN -9461 AND -9459 AND `pos_y` BETWEEN 56 AND 58;
UPDATE `ec_guard_town_criers` SET `crier_entry`=468, `faction`=1, `location_name`='Stormwind'
WHERE `zone_id`=1519 AND `pos_x` BETWEEN -8831 AND -8829 AND `pos_y` BETWEEN 625 AND 627;
UPDATE `ec_guard_town_criers` SET `crier_entry`=1064, `faction`=2, `location_name`='Grom''gol'
WHERE `zone_id`=33 AND `pos_x` BETWEEN -12367 AND -12365 AND `pos_y` BETWEEN 136 AND 138;
UPDATE `ec_guard_town_criers` SET `zone_id`=33, `crier_entry`=4624, `faction`=0, `location_name`='Booty Bay'
WHERE `zone_id` IN (33,35) AND `pos_x` BETWEEN -14452 AND -14450 AND `pos_y` BETWEEN 494 AND 496;
UPDATE `ec_guard_town_criers` SET `crier_entry`=3502, `faction`=0, `location_name`='Ratchet'
WHERE `zone_id`=17 AND `pos_x` BETWEEN -957 AND -955 AND `pos_y` BETWEEN -3755 AND -3753;
UPDATE `ec_guard_town_criers` SET `crier_entry`=2405, `faction`=2, `location_name`='Tarren Mill'
WHERE `zone_id`=267 AND `pos_x` BETWEEN -34 AND -32 AND `pos_y` BETWEEN -924 AND -922;
UPDATE `ec_guard_town_criers` SET `crier_entry`=2386, `faction`=1, `location_name`='Southshore'
WHERE `zone_id`=267 AND `pos_x` BETWEEN -830 AND -828 AND `pos_y` BETWEEN -559 AND -557;
UPDATE `ec_guard_town_criers` SET `crier_entry`=10038, `faction`=1, `location_name`='Darkshire'
WHERE `zone_id`=10 AND `pos_x` BETWEEN -10565 AND -10563 AND `pos_y` BETWEEN -1173 AND -1171;
UPDATE `ec_guard_town_criers` SET `crier_entry`=10037, `faction`=1, `location_name`='Lakeshire'
WHERE `zone_id`=44 AND `pos_x` BETWEEN -9224 AND -9222 AND `pos_y` BETWEEN -2189 AND -2187;
UPDATE `ec_guard_town_criers` SET `crier_entry`=6087, `faction`=1, `location_name`='Astranaar'
WHERE `zone_id`=331 AND `pos_x` BETWEEN 2737 AND 2739 AND `pos_y` BETWEEN -391 AND -389;
UPDATE `ec_guard_town_criers` SET `crier_entry`=5953, `faction`=2, `location_name`='Razor Hill'
WHERE `zone_id`=14 AND `pos_x` BETWEEN 345 AND 347 AND `pos_y` BETWEEN -4709 AND -4707;
UPDATE `ec_guard_town_criers` SET `crier_entry`=7980, `faction`=2, `location_name`='Brill'
WHERE `zone_id`=85 AND `pos_x` BETWEEN 2265 AND 2267 AND `pos_y` BETWEEN 288 AND 290;
UPDATE `ec_guard_town_criers` SET `crier_entry`=8096, `faction`=1, `location_name`='Sentinel Hill'
WHERE `zone_id`=40 AND `pos_x` BETWEEN -10629 AND -10627 AND `pos_y` BETWEEN 1096 AND 1098;

-- Booty Bay is area 35 inside top-level Stranglethorn Vale zone 33.
UPDATE `ec_guard_zone_dialogues`
SET `zone_id`=33, `location_name`='Booty Bay', `faction`=0
WHERE `zone_id`=35;
UPDATE `ec_guard_zone_dialogue_pairs`
SET `zone_id`=33, `location_name`='Booty Bay', `faction`=0
WHERE `zone_id`=35;

-- These preparation barks and conversations are authored module text, not
-- native WotLK creature_text. Keep them available in the backup only; disable
-- every installed row so fish, dead NPCs, and wrong-faction actors cannot emit
-- the synthetic alert spam.
UPDATE `ec_guard_zone_dialogues` SET `enabled`=0;
UPDATE `ec_guard_zone_dialogue_pairs` SET `enabled`=0;
UPDATE `ec_guard_town_criers` SET `enabled`=0;

-- --------------------------------------------------------------------------
-- Faction elite response groups
-- --------------------------------------------------------------------------
-- This loader has zone whitelists but no area whitelist. Limit it to true city
-- zones; outdoor towns continue to receive the area-scoped ordinary response.
-- Replacing this module-owned table removes every stale named NPC, vendor,
-- trigger, wrong-faction entry, and settlement-leaking outdoor response.
DELETE FROM `ec_guard_faction_response`;

INSERT INTO `ec_guard_faction_response`
    (`id`, `faction`, `response_name`, `response_tier`, `creature_entry_list`,
     `spawn_count_min`, `spawn_count_max`, `spawn_delay_ms`,
     `announcement_text`, `zone_whitelist`, `zone_blacklist`,
     `min_threat_level`, `requires_zone_fallen`, `priority`, `enabled`)
VALUES
    (1,  1, 'Stormwind Royal Guard',      2, '1756',  2, 4, 35000, NULL, '1519', NULL, 4, 0, 100, 1),
    (2,  1, 'Ironforge Guard',            2, '5595',  2, 4, 35000, NULL, '1537', NULL, 4, 0, 100, 1),
    (3,  1, 'Darnassus Sentinel',         2, '4262',  2, 4, 35000, NULL, '1657', NULL, 4, 0, 100, 1),
    (4,  1, 'Exodar Peacekeeper',         2, '16733', 2, 4, 35000, NULL, '3557', NULL, 4, 0, 100, 1),
    (5,  2, 'Kor''kron Elite',            2, '14304', 2, 4, 35000, NULL, '1637', NULL, 4, 0, 100, 1),
    (6,  2, 'Deathguard Elite',           2, '7980',  2, 4, 35000, NULL, '1497', NULL, 4, 0, 100, 1),
    (7,  2, 'Bluffwatcher',               2, '3084',  2, 4, 35000, NULL, '1638', NULL, 4, 0, 100, 1),
    (8,  2, 'Silvermoon City Guardian',   2, '16222', 2, 4, 35000, NULL, '3487', NULL, 4, 0, 100, 1),
    (9,  0, 'Shattrath Peacekeeper',      2, '19687', 2, 4, 35000, NULL, '3703', NULL, 4, 0, 100, 1),
    (10, 0, 'Dalaran Violet Hold Guard',  2, '30659', 2, 4, 35000, NULL, '4395', NULL, 4, 0, 100, 1);

ALTER TABLE `ec_guard_faction_response` AUTO_INCREMENT = 11;

-- --------------------------------------------------------------------------
-- Manual rollback (do not run unless deliberately reverting this cleanup):
-- --------------------------------------------------------------------------
-- DELETE FROM ec_guard_system_zone_config;
-- INSERT INTO ec_guard_system_zone_config SELECT * FROM ec_guard_system_zone_config_bak_20260901_05;
-- DELETE FROM ec_guard_zone_config;
-- INSERT INTO ec_guard_zone_config SELECT * FROM ec_guard_zone_config_bak_20260901_05;
-- DELETE FROM ec_guard_zone_dialogues;
-- INSERT INTO ec_guard_zone_dialogues SELECT * FROM ec_guard_zone_dialogues_bak_20260901_05;
-- DELETE FROM ec_guard_zone_dialogue_pairs;
-- INSERT INTO ec_guard_zone_dialogue_pairs SELECT * FROM ec_guard_zone_dialogue_pairs_bak_20260901_05;
-- DELETE FROM ec_guard_militia_spawns;
-- INSERT INTO ec_guard_militia_spawns SELECT * FROM ec_guard_militia_spawns_bak_20260901_05;
-- DELETE FROM ec_guard_town_criers;
-- INSERT INTO ec_guard_town_criers SELECT * FROM ec_guard_town_criers_bak_20260901_05;
-- DELETE FROM ec_guard_faction_response;
-- INSERT INTO ec_guard_faction_response SELECT * FROM ec_guard_faction_response_bak_20260901_05;
