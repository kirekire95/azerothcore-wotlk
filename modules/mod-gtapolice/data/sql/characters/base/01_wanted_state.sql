-- =====================================================
-- mod-gtapolice: Wanted State Persistence
-- Stores wanted level across logout/login
-- =====================================================

DROP TABLE IF EXISTS `ec_police_wanted_state`;
CREATE TABLE `ec_police_wanted_state` (
    `player_guid` INT UNSIGNED NOT NULL,
    `wanted_stars` TINYINT UNSIGNED NOT NULL DEFAULT 0,
    `crime_points` INT UNSIGNED NOT NULL DEFAULT 0,
    `last_crime_time` INT UNSIGNED NOT NULL DEFAULT 0,
    `zone_id` INT UNSIGNED NOT NULL DEFAULT 0,
    `total_arrests` INT UNSIGNED NOT NULL DEFAULT 0,
    `total_escapes` INT UNSIGNED NOT NULL DEFAULT 0,
    `created_at` TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    `updated_at` TIMESTAMP DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
    PRIMARY KEY (`player_guid`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT='Stores player wanted level and crime statistics';

-- Index for cleanup queries (find stale wanted states)
CREATE INDEX `idx_last_crime` ON `ec_police_wanted_state` (`last_crime_time`);

-- Index for zone queries (find all wanted players in a zone)
CREATE INDEX `idx_zone_wanted` ON `ec_police_wanted_state` (`zone_id`, `wanted_stars`);
