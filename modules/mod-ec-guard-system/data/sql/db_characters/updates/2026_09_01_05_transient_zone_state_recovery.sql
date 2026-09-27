-- Runtime actors and incident context are not persisted across a worldserver
-- restart. Retaining only heat/alarm/phase created phantom attacks with nobody
-- present, so preserve the rows and lifetime totals while clearing transients.
CREATE TABLE IF NOT EXISTS `ec_guard_migration_markers` (
    `migration_id` VARCHAR(128) NOT NULL,
    `applied_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    PRIMARY KEY (`migration_id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

CREATE TABLE IF NOT EXISTS `ec_guard_zone_state_pre_phantom_recovery_20260901`
LIKE `ec_guard_zone_state`;

INSERT IGNORE INTO `ec_guard_zone_state_pre_phantom_recovery_20260901`
SELECT state.*
FROM `ec_guard_zone_state` AS state
WHERE state.`zone_id` = 331
  AND NOT EXISTS (
    SELECT 1
    FROM `ec_guard_migration_markers`
    WHERE `migration_id` = '2026_09_01_05_transient_zone_state_recovery'
);

UPDATE `ec_guard_zone_state`
SET `threat_level` = 0,
    `heat_points` = 0,
    `alarm_state` = 0,
    `control_state` = 0,
    `current_phase` = 0
WHERE `zone_id` = 331
  AND NOT EXISTS (
    SELECT 1
    FROM `ec_guard_migration_markers`
    WHERE `migration_id` = '2026_09_01_05_transient_zone_state_recovery'
);

INSERT IGNORE INTO `ec_guard_migration_markers` (`migration_id`)
VALUES ('2026_09_01_05_transient_zone_state_recovery');
