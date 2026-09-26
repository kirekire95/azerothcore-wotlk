CREATE TABLE IF NOT EXISTS `ec_world_settlements` (
  `id` INT UNSIGNED NOT NULL,
  `name` VARCHAR(100) NOT NULL,
  `map_id` SMALLINT UNSIGNED NOT NULL,
  `zone_id` SMALLINT UNSIGNED NOT NULL,
  `area_id` SMALLINT UNSIGNED NOT NULL DEFAULT 0,
  `center_x` FLOAT NOT NULL,
  `center_y` FLOAT NOT NULL,
  `center_z` FLOAT NOT NULL,
  `population_radius` FLOAT UNSIGNED NOT NULL DEFAULT 120,
  `faction` TINYINT UNSIGNED NOT NULL DEFAULT 0,
  `treasury` BIGINT UNSIGNED NOT NULL DEFAULT 0,
  `development_level` SMALLINT UNSIGNED NOT NULL DEFAULT 0,
  `damage_pct` TINYINT UNSIGNED NOT NULL DEFAULT 0,
  `last_economy_update` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
  PRIMARY KEY (`id`),
  KEY `idx_ec_settlement_zone` (`map_id`, `zone_id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

CREATE TABLE IF NOT EXISTS `ec_world_settlement_resources` (
  `id` INT UNSIGNED NOT NULL,
  `settlement_id` INT UNSIGNED NOT NULL,
  `resource_type` VARCHAR(32) NOT NULL,
  `name` VARCHAR(100) NOT NULL,
  `income_per_hour` INT UNSIGNED NOT NULL DEFAULT 0,
  `health_pct` TINYINT UNSIGNED NOT NULL DEFAULT 100,
  `enabled` TINYINT UNSIGNED NOT NULL DEFAULT 1,
  PRIMARY KEY (`id`),
  KEY `idx_ec_resource_settlement` (`settlement_id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

CREATE TABLE IF NOT EXISTS `ec_world_settlement_links` (
  `source_settlement_id` INT UNSIGNED NOT NULL,
  `target_settlement_id` INT UNSIGNED NOT NULL,
  `link_type` VARCHAR(32) NOT NULL,
  `transfer_pct` TINYINT UNSIGNED NOT NULL DEFAULT 0,
  `enabled` TINYINT UNSIGNED NOT NULL DEFAULT 1,
  PRIMARY KEY (`source_settlement_id`, `target_settlement_id`, `link_type`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

DELETE FROM `ec_world_settlement_links` WHERE `source_settlement_id` IN (1, 2, 3);
DELETE FROM `ec_world_settlement_resources` WHERE `settlement_id` IN (1, 2, 3);
DELETE FROM `ec_world_settlements` WHERE `id` IN (1, 2, 3);

INSERT INTO `ec_world_settlements`
(`id`, `name`, `map_id`, `zone_id`, `area_id`, `center_x`, `center_y`, `center_z`, `population_radius`, `faction`, `treasury`, `development_level`, `damage_pct`)
VALUES
(1, 'The Crossroads', 1, 17, 380, -467.14, -2638.91, 95.94, 150, 2, 0, 0, 0),
(2, 'Camp Taurajo', 1, 17, 378, -2365.36, -1912.73, 95.81, 130, 2, 0, 0, 0),
(3, 'Ratchet', 1, 17, 392, -956.664, -3754.71, 5.33239, 160, 3, 0, 0, 0);

INSERT INTO `ec_world_settlement_resources`
(`id`, `settlement_id`, `resource_type`, `name`, `income_per_hour`, `health_pct`, `enabled`)
VALUES
(1, 1, 'trade', 'Crossroads trade', 30, 100, 1),
(2, 2, 'supplies', 'Taurajo supplies', 20, 100, 1),
(3, 3, 'trade', 'Ratchet commerce', 45, 100, 1);

INSERT INTO `ec_world_settlement_links`
(`source_settlement_id`, `target_settlement_id`, `link_type`, `transfer_pct`, `enabled`)
VALUES
(2, 1, 'supplies', 25, 1),
(3, 1, 'trade', 15, 1);
