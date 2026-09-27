-- Guard System: Debris templates for zone environmental effects
-- These game objects spawn after combat events (guard fights, crime scenes)

CREATE TABLE IF NOT EXISTS `ec_guard_debris_templates` (
  `gameobject_entry` INT UNSIGNED NOT NULL PRIMARY KEY,
  `spawn_chance` FLOAT NOT NULL DEFAULT 0.5,
  `despawn_time_ms` FLOAT NOT NULL DEFAULT 300000,
  `description` VARCHAR(255) DEFAULT '',
  `enabled` TINYINT(1) NOT NULL DEFAULT 1
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

INSERT IGNORE INTO `ec_guard_debris_templates` (`gameobject_entry`, `spawn_chance`, `despawn_time_ms`, `description`) VALUES
-- Standard debris (random scatter after combat)
(190751, 0.4, 300000, 'Broken weapon rack'),
(190752, 0.3, 300000, 'Overturned barrel'),
(190753, 0.2, 300000, 'Scattered crates'),
(190754, 0.15, 300000, 'Broken cart wheel'),
(190755, 0.1, 300000, 'Fallen lantern'),
-- Destructible structures (built by peons, spawn_chance=0 means not random debris)
(35251, 0.0, 0, 'Watchtower (built by peons)'),
(35252, 0.0, 0, 'Supply bunker (built by peons)'),
(35253, 0.0, 0, 'Palisade wall section (built by peons)');
