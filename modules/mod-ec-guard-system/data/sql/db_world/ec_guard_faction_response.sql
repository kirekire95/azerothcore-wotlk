-- ==========================================================================
-- EC Guard System - Faction Elite Response Groups
-- ==========================================================================
-- This table defines which elite forces respond when a zone of that faction
-- falls or reaches critical threat levels. Think Thrall's Kor'kron Elite
-- or Stormwind's Royal Guard responding to a massacre.
-- ==========================================================================

DROP TABLE IF EXISTS `ec_guard_faction_response`;

CREATE TABLE `ec_guard_faction_response` (
    `id` INT UNSIGNED NOT NULL AUTO_INCREMENT,
    `faction` TINYINT UNSIGNED NOT NULL COMMENT '0=Neutral, 1=Alliance, 2=Horde, 3=Both',
    `response_name` VARCHAR(100) NOT NULL COMMENT 'Display name (e.g., "Kor''kron Elite")',
    `response_tier` TINYINT UNSIGNED NOT NULL DEFAULT 1 COMMENT '1=Regular elite, 2=Named champions, 3=Faction leader guard',
    `creature_entry_list` VARCHAR(255) NOT NULL COMMENT 'Comma-separated creature template entries',
    `spawn_count_min` INT UNSIGNED NOT NULL DEFAULT 3 COMMENT 'Minimum spawns',
    `spawn_count_max` INT UNSIGNED NOT NULL DEFAULT 6 COMMENT 'Maximum spawns',
    `spawn_delay_ms` INT UNSIGNED NOT NULL DEFAULT 30000 COMMENT 'Delay before spawning (dramatic effect)',
    `announcement_text` VARCHAR(255) DEFAULT NULL COMMENT 'Zone announcement when they arrive',
    `zone_whitelist` VARCHAR(255) DEFAULT NULL COMMENT 'Only respond to these zone IDs (comma-separated), NULL = all',
    `zone_blacklist` VARCHAR(255) DEFAULT NULL COMMENT 'Never respond to these zone IDs (comma-separated)',
    `min_threat_level` TINYINT UNSIGNED NOT NULL DEFAULT 5 COMMENT 'Minimum zone threat to trigger (1-5)',
    `requires_zone_fallen` TINYINT UNSIGNED NOT NULL DEFAULT 0 COMMENT '1 = Only respond if zone is FALLEN',
    `priority` INT UNSIGNED NOT NULL DEFAULT 100 COMMENT 'Lower = higher priority when multiple groups qualify',
    `enabled` TINYINT UNSIGNED NOT NULL DEFAULT 1,
    PRIMARY KEY (`id`),
    INDEX `idx_faction` (`faction`),
    INDEX `idx_tier` (`response_tier`),
    INDEX `idx_enabled` (`enabled`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT='EC Guard System - Elite response force definitions';

-- The response loader supports zone whitelists, not settlement/area
-- whitelists. Therefore only true city-zone IDs are enabled here. Outdoor
-- towns (Goldshire, Crossroads, Razor Hill, Astranaar, Booty Bay, etc.) use the
-- area-scoped ordinary guard response instead; enabling an elite response for
-- those zone IDs could summon one town's guards beside a player in another
-- settlement. There are no faction-wide fallbacks, named leaders, quest NPCs,
-- vendors, triggers, or opposite-faction templates. Announcement text is NULL
-- so these rows cannot emit invented English world barks.
INSERT INTO `ec_guard_faction_response`
(`faction`, `response_name`, `response_tier`, `creature_entry_list`,
 `spawn_count_min`, `spawn_count_max`, `spawn_delay_ms`, `announcement_text`,
 `zone_whitelist`, `zone_blacklist`, `min_threat_level`,
 `requires_zone_fallen`, `priority`, `enabled`)
VALUES
-- Alliance city jurisdictions
(1, 'Stormwind Royal Guard',       2, '1756',  2, 4, 35000, NULL, '1519', NULL, 4, 0, 100, 1),
(1, 'Ironforge Guard',             2, '5595',  2, 4, 35000, NULL, '1537', NULL, 4, 0, 100, 1),
(1, 'Darnassus Sentinel',          2, '4262',  2, 4, 35000, NULL, '1657', NULL, 4, 0, 100, 1),
(1, 'Exodar Peacekeeper',          2, '16733', 2, 4, 35000, NULL, '3557', NULL, 4, 0, 100, 1),

-- Horde city jurisdictions
(2, 'Kor''kron Elite',             2, '14304', 2, 4, 35000, NULL, '1637', NULL, 4, 0, 100, 1),
(2, 'Deathguard Elite',            2, '7980',  2, 4, 35000, NULL, '1497', NULL, 4, 0, 100, 1),
(2, 'Bluffwatcher',                2, '3084',  2, 4, 35000, NULL, '1638', NULL, 4, 0, 100, 1),
(2, 'Silvermoon City Guardian',    2, '16222', 2, 4, 35000, NULL, '3487', NULL, 4, 0, 100, 1),

-- Neutral city jurisdictions
(0, 'Shattrath Peacekeeper',       2, '19687', 2, 4, 35000, NULL, '3703', NULL, 4, 0, 100, 1),
(0, 'Dalaran Violet Hold Guard',   2, '30659', 2, 4, 35000, NULL, '4395', NULL, 4, 0, 100, 1);
