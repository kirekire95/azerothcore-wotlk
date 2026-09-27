-- Guard System: Construction sites for peon/worker building system
-- Peons carry lumber from lumber piles to construction sites, building structures over time.
-- Tied to zone preparation phases (UNEASY/MOBILIZING).

CREATE TABLE IF NOT EXISTS `ec_guard_construction_sites` (
  `id` INT UNSIGNED NOT NULL AUTO_INCREMENT PRIMARY KEY,
  `zone_id` INT UNSIGNED NOT NULL,
  `site_name` VARCHAR(100) NOT NULL DEFAULT '',
  `structure_entry` INT UNSIGNED NOT NULL COMMENT 'Gameobject entry spawned when construction completes',
  `structure_type` TINYINT UNSIGNED NOT NULL DEFAULT 0 COMMENT 'PrepGameObjectType enum value',
  `build_x` FLOAT NOT NULL,
  `build_y` FLOAT NOT NULL,
  `build_z` FLOAT NOT NULL,
  `build_o` FLOAT NOT NULL DEFAULT 0,
  `lumber_x` FLOAT NOT NULL COMMENT 'Lumber pile X position',
  `lumber_y` FLOAT NOT NULL COMMENT 'Lumber pile Y position',
  `lumber_z` FLOAT NOT NULL COMMENT 'Lumber pile Z position',
  `trips_required` TINYINT UNSIGNED NOT NULL DEFAULT 3 COMMENT 'Lumber trips needed to complete',
  `min_phase` TINYINT UNSIGNED NOT NULL DEFAULT 1 COMMENT 'ZonePhase when construction starts (1=UNEASY, 2=MOBILIZING)',
  `workers_needed` TINYINT UNSIGNED NOT NULL DEFAULT 2 COMMENT 'Number of peons assigned',
  `faction` TINYINT UNSIGNED NOT NULL DEFAULT 3 COMMENT '0=neutral, 1=alliance, 2=horde, 3=any',
  `priority` TINYINT UNSIGNED NOT NULL DEFAULT 5 COMMENT 'Build order (lower = built first)',
  `enabled` TINYINT(1) NOT NULL DEFAULT 1,
  INDEX `idx_zone` (`zone_id`),
  INDEX `idx_phase` (`min_phase`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

-- =============================================================================
-- CROSSROADS (Zone 17 - The Barrens, Horde)
-- Lumber pile near the trees east of town
-- =============================================================================

INSERT IGNORE INTO `ec_guard_construction_sites`
  (`id`, `zone_id`, `site_name`, `structure_entry`, `structure_type`, `build_x`, `build_y`, `build_z`, `build_o`, `lumber_x`, `lumber_y`, `lumber_z`, `trips_required`, `min_phase`, `workers_needed`, `faction`, `priority`) VALUES
-- Watchtower at north road entrance
(1, 17, 'North Road Watchtower', 35251, 15, -437.5, -2621.0, 96.5, 4.71, -470.0, -2615.0, 96.0, 3, 1, 2, 2, 1),
-- Barricade blocking south road
(2, 17, 'South Road Barricade', 186273, 0, -449.0, -2695.0, 95.5, 0.0, -470.0, -2615.0, 96.0, 2, 1, 1, 2, 2),
-- Supply bunker near the inn
(3, 17, 'Inn Supply Bunker', 35252, 16, -462.0, -2663.0, 96.0, 1.57, -470.0, -2615.0, 96.0, 4, 2, 3, 2, 3),
-- Barricade at east road
(4, 17, 'East Road Barricade', 186273, 0, -419.0, -2652.0, 96.0, 1.57, -470.0, -2615.0, 96.0, 2, 2, 1, 2, 4);

-- =============================================================================
-- GOLDSHIRE (Zone 12 - Elwynn Forest, Alliance)
-- Lumber pile near the sawmill
-- =============================================================================

INSERT IGNORE INTO `ec_guard_construction_sites`
  (`id`, `zone_id`, `site_name`, `structure_entry`, `structure_type`, `build_x`, `build_y`, `build_z`, `build_o`, `lumber_x`, `lumber_y`, `lumber_z`, `trips_required`, `min_phase`, `workers_needed`, `faction`, `priority`) VALUES
-- Watchtower at road fork
(5, 12, 'Road Fork Watchtower', 35251, 15, -9456.0, 45.0, 57.0, 3.14, -9480.0, 55.0, 57.0, 3, 1, 2, 1, 1),
-- Barricade at bridge
(6, 12, 'Bridge Barricade', 186273, 0, -9465.0, 15.0, 56.5, 0.0, -9480.0, 55.0, 57.0, 2, 1, 1, 1, 2);
