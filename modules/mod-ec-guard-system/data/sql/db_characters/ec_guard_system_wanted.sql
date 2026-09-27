-- ==========================================================================
-- EC Guard System - Player Wanted Level Persistence
-- ==========================================================================
-- This table stores player wanted levels for persistence across sessions.
-- Wanted level is loaded on login and saved on logout.
-- ==========================================================================

DROP TABLE IF EXISTS `ec_guard_wanted_state`;

CREATE TABLE `ec_guard_wanted_state` (
    `guid` INT UNSIGNED NOT NULL COMMENT 'Player character GUID',
    `crime_points` INT UNSIGNED NOT NULL DEFAULT 0 COMMENT 'Accumulated crime points',
    `stars` TINYINT UNSIGNED NOT NULL DEFAULT 0 COMMENT 'Current wanted star level (0-5)',
    `civilian_kills` INT UNSIGNED NOT NULL DEFAULT 0 COMMENT 'Total civilians killed this session',
    `guard_kills` INT UNSIGNED NOT NULL DEFAULT 0 COMMENT 'Total guards killed this session',
    `crime_zone_id` INT UNSIGNED NOT NULL DEFAULT 0 COMMENT 'Zone ID where crime occurred',
    `crime_area_id` INT UNSIGNED NOT NULL DEFAULT 0 COMMENT 'Area ID where crime occurred',
    `crime_faction` TINYINT UNSIGNED NOT NULL DEFAULT 0 COMMENT 'AzerothCore TeamId of offended faction (0=Alliance, 1=Horde, 2=Neutral)',
    `last_crime_time` INT UNSIGNED NOT NULL DEFAULT 0 COMMENT 'Unix timestamp of last crime',
    PRIMARY KEY (`guid`),
    INDEX `idx_stars` (`stars`),
    INDEX `idx_zone` (`crime_zone_id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT='EC Guard System - Player wanted level persistence';

-- ==========================================================================
-- ROLLBACK: Remove wanted level table
-- ==========================================================================
/*
DROP TABLE IF EXISTS `ec_guard_wanted_state`;
*/
