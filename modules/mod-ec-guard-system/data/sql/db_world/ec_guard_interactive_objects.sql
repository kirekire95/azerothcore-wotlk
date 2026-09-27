-- Guard System: Interactive gameobject definitions
-- Capture points, traps, area damage, guardposts spawned during zone phases.
-- These are the "active defense" objects that interact with players.

CREATE TABLE IF NOT EXISTS `ec_guard_interactive_objects` (
  `id` INT UNSIGNED NOT NULL AUTO_INCREMENT PRIMARY KEY,
  `zone_id` INT UNSIGNED NOT NULL,
  `gameobject_entry` INT UNSIGNED NOT NULL COMMENT 'Entry from gameobject_template',
  `gameobject_type` TINYINT UNSIGNED NOT NULL COMMENT '6=TRAP, 12=AREADAMAGE, 21=GUARDPOST, 29=CAPTURE_POINT, 33=DESTRUCTIBLE',
  `spawn_x` FLOAT NOT NULL,
  `spawn_y` FLOAT NOT NULL,
  `spawn_z` FLOAT NOT NULL,
  `spawn_o` FLOAT NOT NULL DEFAULT 0,
  `spawn_on_phase` TINYINT UNSIGNED NOT NULL DEFAULT 0 COMMENT '0=always, 1=UNEASY, 2=MOBILIZING, 3=IMPACT, 4=AFTERMATH',
  `despawn_on_phase` TINYINT UNSIGNED NOT NULL DEFAULT 0 COMMENT '0=never, phase number to despawn',
  `linked_construction_id` INT UNSIGNED DEFAULT NULL COMMENT 'If built by peons, FK to ec_guard_construction_sites.id',
  `faction` TINYINT UNSIGNED NOT NULL DEFAULT 3 COMMENT '0=neutral, 1=alliance, 2=horde, 3=any',
  `description` VARCHAR(255) DEFAULT '',
  `enabled` TINYINT(1) NOT NULL DEFAULT 1,
  INDEX `idx_zone` (`zone_id`),
  INDEX `idx_phase` (`spawn_on_phase`),
  INDEX `idx_type` (`gameobject_type`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

-- =============================================================================
-- CROSSROADS (Zone 17 - The Barrens, Horde)
-- =============================================================================

INSERT IGNORE INTO `ec_guard_interactive_objects`
  (`id`, `zone_id`, `gameobject_entry`, `gameobject_type`, `spawn_x`, `spawn_y`, `spawn_z`, `spawn_o`,
   `spawn_on_phase`, `despawn_on_phase`, `linked_construction_id`, `faction`, `description`) VALUES
-- Horde town flag (capture point) - always present, shows zone control
(1, 17, 180428, 29, -450.0, -2655.0, 96.5, 0.0, 0, 0, NULL, 2, 'Crossroads Horde Flag'),
-- Explosive trap at south road approach (MOBILIZING phase)
(2, 17, 191307, 6, -448.0, -2700.0, 95.5, 0.0, 2, 4, NULL, 2, 'South road explosive trap'),
-- Fire trap at east road approach
(3, 17, 183104, 12, -415.0, -2650.0, 96.0, 0.0, 3, 4, NULL, 2, 'East road fire hazard'),
-- Guard post at town center (spawns replacement guards)
(4, 17, 37071, 21, -445.0, -2660.0, 96.0, 0.0, 1, 0, NULL, 2, 'Town center guard post');

-- =============================================================================
-- GOLDSHIRE (Zone 12 - Elwynn Forest, Alliance)
-- =============================================================================

INSERT IGNORE INTO `ec_guard_interactive_objects`
  (`id`, `zone_id`, `gameobject_entry`, `gameobject_type`, `spawn_x`, `spawn_y`, `spawn_z`, `spawn_o`,
   `spawn_on_phase`, `despawn_on_phase`, `linked_construction_id`, `faction`, `description`) VALUES
-- Alliance town flag
(5, 12, 183356, 29, -9459.0, 42.0, 57.0, 0.0, 0, 0, NULL, 1, 'Goldshire Alliance Flag'),
-- Trap at bridge approach
(6, 12, 191307, 6, -9468.0, 10.0, 56.5, 0.0, 2, 4, NULL, 1, 'Bridge approach trap'),
-- Guard post near the inn
(7, 12, 37071, 21, -9455.0, 35.0, 57.0, 0.0, 1, 0, NULL, 1, 'Inn guard post');

-- =============================================================================
-- GROM'GOL BASE CAMP (Zone 33 - Stranglethorn Vale, Horde)
-- =============================================================================

INSERT IGNORE INTO `ec_guard_interactive_objects`
  (`id`, `zone_id`, `gameobject_entry`, `gameobject_type`, `spawn_x`, `spawn_y`, `spawn_z`, `spawn_o`,
   `spawn_on_phase`, `despawn_on_phase`, `linked_construction_id`, `faction`, `description`) VALUES
-- Horde flag at camp center
(8, 33, 180428, 29, -12366.0, 195.0, 3.0, 0.0, 0, 0, NULL, 2, 'Grom''gol Horde Flag'),
-- Trap at dock approach
(9, 33, 191307, 6, -12350.0, 180.0, 2.5, 0.0, 2, 4, NULL, 2, 'Dock approach trap'),
-- Fire pit at jungle entrance
(10, 33, 183104, 12, -12380.0, 220.0, 3.5, 0.0, 3, 4, NULL, 2, 'Jungle entrance fire pit');
