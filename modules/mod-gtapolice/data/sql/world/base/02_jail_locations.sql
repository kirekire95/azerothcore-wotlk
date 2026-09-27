-- =====================================================
-- mod-gtapolice: Jail Locations Table
-- =====================================================

-- Create jail locations table if it doesn't exist
DROP TABLE IF EXISTS `ec_jail_locations`;
CREATE TABLE `ec_jail_locations` (
    `id` INT UNSIGNED NOT NULL AUTO_INCREMENT,
    `name` VARCHAR(64) NOT NULL DEFAULT '',
    `map_id` SMALLINT UNSIGNED NOT NULL DEFAULT 0,
    `cell_x` FLOAT NOT NULL DEFAULT 0,
    `cell_y` FLOAT NOT NULL DEFAULT 0,
    `cell_z` FLOAT NOT NULL DEFAULT 0,
    `cell_o` FLOAT NOT NULL DEFAULT 0,
    `release_x` FLOAT NOT NULL DEFAULT 0,
    `release_y` FLOAT NOT NULL DEFAULT 0,
    `release_z` FLOAT NOT NULL DEFAULT 0,
    `release_o` FLOAT NOT NULL DEFAULT 0,
    `faction` TINYINT UNSIGNED NOT NULL DEFAULT 2 COMMENT '0=Alliance, 1=Horde, 2=Neutral',
    `default_sentence_ms` INT UNSIGNED NOT NULL DEFAULT 60000,
    PRIMARY KEY (`id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

-- Default jail locations
INSERT INTO `ec_jail_locations` (`id`, `name`, `map_id`, `cell_x`, `cell_y`, `cell_z`, `cell_o`, `release_x`, `release_y`, `release_z`, `release_o`, `faction`, `default_sentence_ms`) VALUES
-- Alliance jails (cell coords = inside buildings, release coords = outside entrance)
(1, 'Stormwind Stockade', 0, -8759.0, 844.0, 88.0, 3.14, -8753.0, 847.0, 96.0, 0.0, 0, 60000),
(2, 'Ironforge Holding', 0, -4820.0, -1155.0, 502.0, 5.5, -4918.0, -940.0, 501.0, 0.0, 0, 60000),
(3, 'Darnassus Barrow', 1, 10082.0, 2412.0, 1323.0, 2.0, 9951.0, 2495.0, 1317.0, 0.0, 0, 60000),

-- Horde jails
(4, 'Orgrimmar Hold', 1, 1577.0, -4417.0, 7.5, 1.2, 1629.0, -4373.0, 31.0, 0.0, 1, 60000),
(5, 'Undercity Dungeon', 0, 1620.0, 260.0, -62.0, 4.7, 1586.0, 239.0, -43.0, 0.0, 1, 60000),
(6, 'Thunder Bluff Cell', 1, -1263.0, 124.0, 131.0, 3.5, -1290.0, 159.0, 131.0, 0.0, 1, 60000),

-- Neutral jails (smaller sentences)
(7, 'Booty Bay Brig', 0, -14460.0, 509.0, 3.0, 1.8, -14445.0, 464.0, 36.0, 0.0, 2, 30000),
(8, 'Gadgetzan Jail', 1, -7161.0, -3810.0, 9.0, 0.6, -7120.0, -3780.0, 9.0, 0.0, 2, 30000),
(9, 'Shattrath Holding', 530, -1838.0, 5390.0, -12.0, 2.4, -1850.0, 5430.0, -12.0, 0.0, 2, 45000),
(10, 'Dalaran Prison', 571, 5768.0, 650.0, 641.0, 5.0, 5815.0, 640.0, 648.0, 0.0, 2, 45000);

-- =====================================================
-- Jail Punishments by Wanted Stars
-- =====================================================

DROP TABLE IF EXISTS `ec_jail_punishments`;
CREATE TABLE `ec_jail_punishments` (
    `stars` TINYINT UNSIGNED NOT NULL,
    `jail_time_ms` INT UNSIGNED NOT NULL DEFAULT 60000,
    `gold_fine` INT UNSIGNED NOT NULL DEFAULT 0,
    `debuff_spell_id` INT UNSIGNED NOT NULL DEFAULT 0,
    `debuff_duration_ms` INT UNSIGNED NOT NULL DEFAULT 0,
    PRIMARY KEY (`stars`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

INSERT INTO `ec_jail_punishments` (`stars`, `jail_time_ms`, `gold_fine`, `debuff_spell_id`, `debuff_duration_ms`) VALUES
(1, 30000,   100,    0, 0),       -- 30s, 1g fine
(2, 60000,   500,    0, 0),       -- 1min, 5g fine
(3, 120000,  2000,   0, 0),       -- 2min, 20g fine
(4, 300000,  5000,   0, 0),       -- 5min, 50g fine
(5, 600000,  10000,  0, 0);       -- 10min, 100g fine

-- =====================================================
-- Police Car Waypoint Paths (optional)
-- For more advanced pathing instead of direct movement
-- =====================================================

DROP TABLE IF EXISTS `ec_police_car_paths`;
CREATE TABLE `ec_police_car_paths` (
    `id` INT UNSIGNED NOT NULL,
    `point_id` INT UNSIGNED NOT NULL,
    `map_id` SMALLINT UNSIGNED NOT NULL DEFAULT 0,
    `x` FLOAT NOT NULL DEFAULT 0,
    `y` FLOAT NOT NULL DEFAULT 0,
    `z` FLOAT NOT NULL DEFAULT 0,
    `delay_ms` INT UNSIGNED NOT NULL DEFAULT 0,
    PRIMARY KEY (`id`, `point_id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

-- Example path from Stormwind entrance to Stockade
INSERT INTO `ec_police_car_paths` (`id`, `point_id`, `map_id`, `x`, `y`, `z`, `delay_ms`) VALUES
-- Path 1: Stormwind entrance to Stockade
(1, 1, 0, -8833.0, 630.0, 95.0, 0),
(1, 2, 0, -8820.0, 640.0, 94.0, 0),
(1, 3, 0, -8800.0, 660.0, 97.0, 0),
(1, 4, 0, -8780.0, 700.0, 97.0, 0),
(1, 5, 0, -8765.0, 750.0, 96.0, 0),
(1, 6, 0, -8765.0, 846.0, 87.0, 0);  -- Stockade entrance
