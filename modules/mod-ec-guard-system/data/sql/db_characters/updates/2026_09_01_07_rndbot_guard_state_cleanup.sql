-- The previous broad witness classifier allowed random playerbots to acquire
-- phantom wanted levels and bounties from critters, combat helpers, and other
-- invalid witnesses. Preserve every affected row, cancel only active RNDbot
-- bounties, and remove only RNDbot wanted state after the fixed worldserver is
-- ready for cutover. Real-player and AddClass/altbot records are untouched.

CREATE TABLE IF NOT EXISTS `ec_guard_migration_markers` (
    `migration_id` VARCHAR(128) NOT NULL,
    `applied_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    PRIMARY KEY (`migration_id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

CREATE TABLE IF NOT EXISTS `ec_guard_wanted_rndbot_cleanup_20260901`
LIKE `ec_guard_wanted_state`;

CREATE TABLE IF NOT EXISTS `ec_bounties_rndbot_cleanup_20260901`
LIKE `ec_bounties`;

START TRANSACTION;

INSERT IGNORE INTO `ec_guard_wanted_rndbot_cleanup_20260901`
SELECT wanted.*
FROM `ec_guard_wanted_state` AS wanted
INNER JOIN `characters` AS character_row
    ON character_row.`guid` = wanted.`guid`
INNER JOIN `acore_playerbots`.`playerbots_account_type` AS account_type
    ON account_type.`account_id` = character_row.`account`
   AND account_type.`account_type` = 1
WHERE NOT EXISTS (
    SELECT 1
    FROM `ec_guard_migration_markers`
    WHERE `migration_id` = '2026_09_01_07_rndbot_guard_state_cleanup'
);

INSERT IGNORE INTO `ec_bounties_rndbot_cleanup_20260901`
SELECT bounty.*
FROM `ec_bounties` AS bounty
INNER JOIN `characters` AS character_row
    ON character_row.`guid` = bounty.`target_guid`
INNER JOIN `acore_playerbots`.`playerbots_account_type` AS account_type
    ON account_type.`account_id` = character_row.`account`
   AND account_type.`account_type` = 1
WHERE bounty.`status` = 0
  AND NOT EXISTS (
      SELECT 1
      FROM `ec_guard_migration_markers`
      WHERE `migration_id` = '2026_09_01_07_rndbot_guard_state_cleanup'
  );

UPDATE `ec_bounties` AS bounty
INNER JOIN `characters` AS character_row
    ON character_row.`guid` = bounty.`target_guid`
INNER JOIN `acore_playerbots`.`playerbots_account_type` AS account_type
    ON account_type.`account_id` = character_row.`account`
   AND account_type.`account_type` = 1
SET bounty.`status` = 3,
    bounty.`expires_time` = COALESCE(bounty.`expires_time`, UTC_TIMESTAMP())
WHERE bounty.`status` = 0
  AND NOT EXISTS (
      SELECT 1
      FROM `ec_guard_migration_markers`
      WHERE `migration_id` = '2026_09_01_07_rndbot_guard_state_cleanup'
  );

DELETE wanted
FROM `ec_guard_wanted_state` AS wanted
INNER JOIN `characters` AS character_row
    ON character_row.`guid` = wanted.`guid`
INNER JOIN `acore_playerbots`.`playerbots_account_type` AS account_type
    ON account_type.`account_id` = character_row.`account`
   AND account_type.`account_type` = 1
WHERE NOT EXISTS (
    SELECT 1
    FROM `ec_guard_migration_markers`
    WHERE `migration_id` = '2026_09_01_07_rndbot_guard_state_cleanup'
);

INSERT IGNORE INTO `ec_guard_migration_markers` (`migration_id`)
VALUES ('2026_09_01_07_rndbot_guard_state_cleanup');

COMMIT;
