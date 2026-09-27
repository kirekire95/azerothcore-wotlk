-- One-time cleanup for RNDbot wanted/bounty rows recreated by the old logout
-- hook during the deployment restart. The matching C++ fix keeps bot crime
-- state session-local, so these rows do not return on later clean shutdowns.

CREATE TABLE IF NOT EXISTS `ec_guard_migration_markers` (
    `migration_id` VARCHAR(128) NOT NULL,
    `applied_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    PRIMARY KEY (`migration_id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

CREATE TABLE IF NOT EXISTS `ec_guard_wanted_restart_cleanup_20260901`
LIKE `ec_guard_wanted_state`;

CREATE TABLE IF NOT EXISTS `ec_bounties_restart_cleanup_20260901`
LIKE `ec_bounties`;

CREATE TABLE IF NOT EXISTS `character_aura_restart_cleanup_20260901`
LIKE `character_aura`;

START TRANSACTION;

INSERT IGNORE INTO `ec_guard_wanted_restart_cleanup_20260901`
SELECT wanted.*
FROM `ec_guard_wanted_state` AS wanted
JOIN `characters` AS character_row
    ON character_row.`guid` = wanted.`guid`
JOIN `acore_playerbots`.`playerbots_account_type` AS account_type
    ON account_type.`account_id` = character_row.`account`
WHERE account_type.`account_type` = 1
  AND NOT EXISTS (
      SELECT 1 FROM `ec_guard_migration_markers`
      WHERE `migration_id` = '2026_09_01_08_rndbot_restart_guard_state_cleanup'
  );

INSERT IGNORE INTO `ec_bounties_restart_cleanup_20260901`
SELECT bounty.*
FROM `ec_bounties` AS bounty
JOIN `characters` AS character_row
    ON character_row.`guid` = bounty.`target_guid`
JOIN `acore_playerbots`.`playerbots_account_type` AS account_type
    ON account_type.`account_id` = character_row.`account`
WHERE account_type.`account_type` = 1
  AND bounty.`status` = 0
  AND NOT EXISTS (
      SELECT 1 FROM `ec_guard_migration_markers`
      WHERE `migration_id` = '2026_09_01_08_rndbot_restart_guard_state_cleanup'
  );

INSERT IGNORE INTO `character_aura_restart_cleanup_20260901`
SELECT aura.*
FROM `character_aura` AS aura
JOIN `characters` AS character_row
    ON character_row.`guid` = aura.`guid`
JOIN `acore_playerbots`.`playerbots_account_type` AS account_type
    ON account_type.`account_id` = character_row.`account`
WHERE account_type.`account_type` = 1
  AND aura.`spell` IN (900001, 900002, 900003, 900004, 900005, 900010)
  AND NOT EXISTS (
      SELECT 1 FROM `ec_guard_migration_markers`
      WHERE `migration_id` = '2026_09_01_08_rndbot_restart_guard_state_cleanup'
  );

UPDATE `ec_bounties` AS bounty
JOIN `characters` AS character_row
    ON character_row.`guid` = bounty.`target_guid`
JOIN `acore_playerbots`.`playerbots_account_type` AS account_type
    ON account_type.`account_id` = character_row.`account`
SET bounty.`status` = 3,
    bounty.`expires_time` = COALESCE(bounty.`expires_time`, UTC_TIMESTAMP())
WHERE account_type.`account_type` = 1
  AND bounty.`status` = 0
  AND NOT EXISTS (
      SELECT 1 FROM `ec_guard_migration_markers`
      WHERE `migration_id` = '2026_09_01_08_rndbot_restart_guard_state_cleanup'
  );

DELETE wanted
FROM `ec_guard_wanted_state` AS wanted
JOIN `characters` AS character_row
    ON character_row.`guid` = wanted.`guid`
JOIN `acore_playerbots`.`playerbots_account_type` AS account_type
    ON account_type.`account_id` = character_row.`account`
WHERE account_type.`account_type` = 1
  AND NOT EXISTS (
      SELECT 1 FROM `ec_guard_migration_markers`
      WHERE `migration_id` = '2026_09_01_08_rndbot_restart_guard_state_cleanup'
  );

DELETE aura
FROM `character_aura` AS aura
JOIN `characters` AS character_row
    ON character_row.`guid` = aura.`guid`
JOIN `acore_playerbots`.`playerbots_account_type` AS account_type
    ON account_type.`account_id` = character_row.`account`
WHERE account_type.`account_type` = 1
  AND aura.`spell` IN (900001, 900002, 900003, 900004, 900005, 900010)
  AND NOT EXISTS (
      SELECT 1 FROM `ec_guard_migration_markers`
      WHERE `migration_id` = '2026_09_01_08_rndbot_restart_guard_state_cleanup'
  );

INSERT IGNORE INTO `ec_guard_migration_markers` (`migration_id`)
VALUES ('2026_09_01_08_rndbot_restart_guard_state_cleanup');

COMMIT;
