CREATE TABLE IF NOT EXISTS `ec_world_settlement_guard_tiers` (
  `settlement_id` INT UNSIGNED NOT NULL,
  `development_level` SMALLINT UNSIGNED NOT NULL,
  `creature_entry` INT UNSIGNED NOT NULL,
  `name` VARCHAR(100) NOT NULL,
  PRIMARY KEY (`settlement_id`, `development_level`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

CREATE TABLE IF NOT EXISTS `ec_world_settlement_nodes` (
  `id` INT UNSIGNED NOT NULL,
  `settlement_id` INT UNSIGNED NOT NULL,
  `resource_id` INT UNSIGNED DEFAULT NULL,
  `node_type` VARCHAR(32) NOT NULL,
  `name` VARCHAR(100) NOT NULL,
  `map_id` SMALLINT UNSIGNED NOT NULL,
  `position_x` FLOAT NOT NULL,
  `position_y` FLOAT NOT NULL,
  `position_z` FLOAT NOT NULL,
  `radius` FLOAT UNSIGNED NOT NULL DEFAULT 30,
  `owner_faction` TINYINT UNSIGNED NOT NULL DEFAULT 0,
  `state` TINYINT UNSIGNED NOT NULL DEFAULT 0 COMMENT '0 operational, 1 assaulted, 2 destroyed, 3 rebuilding',
  `health_pct` TINYINT UNSIGNED NOT NULL DEFAULT 100,
  PRIMARY KEY (`id`),
  KEY `idx_ec_node_settlement` (`settlement_id`),
  KEY `idx_ec_node_resource` (`resource_id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

DELETE FROM `ec_world_settlement_guard_tiers` WHERE `settlement_id` IN (1, 2);
INSERT INTO `ec_world_settlement_guard_tiers`
(`settlement_id`, `development_level`, `creature_entry`, `name`)
VALUES
(1, 0, 3296, 'Orgrimmar Grunt'),
(1, 2, 18950, 'Orgrimmar Grunt'),
(1, 4, 29949, 'Orgrimmar Defender'),
(1, 6, 14304, 'Kor\'kron Elite'),
(2, 0, 3296, 'Orgrimmar Grunt'),
(2, 2, 18950, 'Orgrimmar Grunt'),
(2, 4, 29949, 'Orgrimmar Defender'),
(2, 6, 14304, 'Kor\'kron Elite');

DELETE FROM `ec_world_settlement_nodes` WHERE `id` IN (1, 2, 3);
INSERT INTO `ec_world_settlement_nodes`
(`id`, `settlement_id`, `resource_id`, `node_type`, `name`, `map_id`,
 `position_x`, `position_y`, `position_z`, `radius`, `owner_faction`, `state`, `health_pct`)
SELECT 1, 1, 1, 'trade', 'Crossroads trade', 1, -467.14, -2638.91, 95.94, 35, 2, 0, 100
UNION ALL
SELECT 2, 2, 2, 'supplies', 'Taurajo supplies', 1, -2365.36, -1912.73, 95.81, 35, 2, 0, 100
UNION ALL
SELECT 3, 3, 3, 'trade', 'Ratchet commerce', 1, -956.664, -3754.71, 5.33239, 40, 3, 0, 100;
