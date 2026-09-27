-- ==========================================================================
-- EC Guard System - Zone State Persistence
-- ==========================================================================
-- This table stores zone threat/control states for persistence across
-- server restarts. Zone states are saved periodically and on shutdown.
-- ==========================================================================

DROP TABLE IF EXISTS `ec_guard_zone_state`;

CREATE TABLE `ec_guard_zone_state` (
    `zone_id` INT UNSIGNED NOT NULL COMMENT 'Zone ID',
    `threat_level` TINYINT UNSIGNED NOT NULL DEFAULT 0 COMMENT 'Zone threat level (0-5)',
    `heat_points` INT UNSIGNED NOT NULL DEFAULT 0 COMMENT 'Accumulated zone heat',
    `guards_alive` INT UNSIGNED NOT NULL DEFAULT 0 COMMENT 'Current living guards',
    `guards_killed_total` INT UNSIGNED NOT NULL DEFAULT 0 COMMENT 'Total guards killed in current incident',
    `civilians_killed_total` INT UNSIGNED NOT NULL DEFAULT 0 COMMENT 'Total civilians killed',
    `alarm_state` TINYINT UNSIGNED NOT NULL DEFAULT 0 COMMENT 'AlarmState enum (0=QUIET, 1=UNEASY, 2=MOBILIZING, 3=ATTACKED, 4=OCCUPIED, 5=RECOVERING)',
    `control_state` TINYINT UNSIGNED NOT NULL DEFAULT 0 COMMENT 'ControlState enum (0=STABLE, 1=CONTESTED, 2=FALLEN)',
    `current_phase` TINYINT UNSIGNED NOT NULL DEFAULT 0 COMMENT 'ZonePhase enum (0=QUIET, 1=UNEASY, 2=MOBILIZING, 3=IMPACT, 4=AFTERMATH)',
    `owning_faction` TINYINT UNSIGNED NOT NULL DEFAULT 2 COMMENT 'AzerothCore TeamId (0=ALLIANCE, 1=HORDE, 2=NEUTRAL)',
    `last_update` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
    PRIMARY KEY (`zone_id`),
    INDEX `idx_control_state` (`control_state`),
    INDEX `idx_heat` (`heat_points`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT='EC Guard System - Zone state persistence';

-- ==========================================================================
-- ROLLBACK: Remove zone state table
-- ==========================================================================
/*
DROP TABLE IF EXISTS `ec_guard_zone_state`;
*/
