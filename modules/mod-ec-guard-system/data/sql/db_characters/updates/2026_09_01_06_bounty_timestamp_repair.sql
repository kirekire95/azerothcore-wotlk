-- ECBountyHunterManager historically omitted expires_time and loaded neither
-- timestamp. On restart those rows became epoch-valued in memory, while the
-- REPLACE writer could not safely persist the resulting state. Preserve every
-- affected active row before normalizing the default one-hour lifetime.
--
-- If ECGuard.BountyDurationMs is overridden from its 3600000 ms default,
-- change the 3600 SECOND interval below before applying this migration.

CREATE TABLE IF NOT EXISTS `ec_guard_migration_markers` (
    `migration_id` VARCHAR(128) NOT NULL,
    `applied_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    PRIMARY KEY (`migration_id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

CREATE TABLE IF NOT EXISTS `ec_bounties_pre_timestamp_repair_20260901`
LIKE `ec_bounties`;

INSERT IGNORE INTO `ec_bounties_pre_timestamp_repair_20260901`
SELECT bounty.*
FROM `ec_bounties` AS bounty
WHERE bounty.`status` = 0
  AND (
      bounty.`posted_time` IS NULL
      OR UNIX_TIMESTAMP(bounty.`posted_time`) <= 1
      OR bounty.`expires_time` IS NULL
      OR UNIX_TIMESTAMP(bounty.`expires_time`) <= 1
      OR bounty.`expires_time` <= bounty.`posted_time`
      OR EXISTS (
          SELECT 1
          FROM `ec_bounties` AS duplicate
          WHERE duplicate.`status` = 0
            AND duplicate.`target_guid` = bounty.`target_guid`
            AND duplicate.`id` <> bounty.`id`
      )
  )
  AND NOT EXISTS (
      SELECT 1
      FROM `ec_guard_migration_markers`
      WHERE `migration_id` = '2026_09_01_06_bounty_timestamp_repair'
  );

-- Keep the newest active row for a target. Older duplicates remain in history
-- as expired rows; nothing is deleted.
UPDATE `ec_bounties` AS older
INNER JOIN `ec_bounties` AS newer
    ON newer.`target_guid` = older.`target_guid`
   AND newer.`status` = 0
   AND newer.`id` > older.`id`
SET older.`status` = 2,
    older.`expires_time` = COALESCE(
        older.`expires_time`, older.`posted_time`, CURRENT_TIMESTAMP)
WHERE older.`status` = 0
  AND NOT EXISTS (
      SELECT 1
      FROM `ec_guard_migration_markers`
      WHERE `migration_id` = '2026_09_01_06_bounty_timestamp_repair'
  );

-- A missing posting timestamp cannot be reconstructed. Retire that row rather
-- than inventing a fresh bounty lifetime; the backup retains the original.
UPDATE `ec_bounties`
SET `posted_time` = CURRENT_TIMESTAMP,
    `expires_time` = CURRENT_TIMESTAMP,
    `status` = 2
WHERE `status` = 0
  AND (`posted_time` IS NULL OR UNIX_TIMESTAMP(`posted_time`) <= 1)
  AND NOT EXISTS (
      SELECT 1
      FROM `ec_guard_migration_markers`
      WHERE `migration_id` = '2026_09_01_06_bounty_timestamp_repair'
  );

-- Reconstruct the intended deadline from the original posting time, not from
-- migration time. This prevents old phantom rows from receiving a fresh hour.
UPDATE `ec_bounties`
SET `expires_time` = DATE_ADD(`posted_time`, INTERVAL 3600 SECOND)
WHERE `status` = 0
  AND (
      `expires_time` IS NULL
      OR UNIX_TIMESTAMP(`expires_time`) <= 1
      OR `expires_time` <= `posted_time`
  )
  AND NOT EXISTS (
      SELECT 1
      FROM `ec_guard_migration_markers`
      WHERE `migration_id` = '2026_09_01_06_bounty_timestamp_repair'
  );

-- Materialize already elapsed deadlines immediately. Unexpired rows remain
-- active and retain the remainder of their original one-hour window.
UPDATE `ec_bounties`
SET `status` = 2
WHERE `status` = 0
  AND `expires_time` <= CURRENT_TIMESTAMP
  AND NOT EXISTS (
      SELECT 1
      FROM `ec_guard_migration_markers`
      WHERE `migration_id` = '2026_09_01_06_bounty_timestamp_repair'
  );

INSERT IGNORE INTO `ec_guard_migration_markers` (`migration_id`)
VALUES ('2026_09_01_06_bounty_timestamp_repair');
