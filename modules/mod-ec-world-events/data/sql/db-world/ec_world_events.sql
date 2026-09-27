--
-- EC World Events - Database Schema (World Database)
-- Copyright (C) 2026 EC
--

-- Event definitions table
DROP TABLE IF EXISTS `ec_world_event_creatures`;
DROP TABLE IF EXISTS `ec_world_event_objects`;
DROP TABLE IF EXISTS `ec_world_event_schedules`;
DROP TABLE IF EXISTS `ec_world_events`;
CREATE TABLE `ec_world_events` (
    `id` INT UNSIGNED NOT NULL AUTO_INCREMENT,
    `name` VARCHAR(100) NOT NULL,
    `description` TEXT,
    `type` TINYINT UNSIGNED NOT NULL DEFAULT 0 COMMENT '0=Raid, 1=Defense, 2=WorldBoss, 3=Gathering, 4=PvPZone',
    `attacker_faction` TINYINT UNSIGNED NOT NULL DEFAULT 0 COMMENT '0=Neutral, 1=Alliance, 2=Horde',
    `defender_faction` TINYINT UNSIGNED NOT NULL DEFAULT 0 COMMENT '0=Neutral, 1=Alliance, 2=Horde',
    `map_id` INT UNSIGNED NOT NULL DEFAULT 0,
    `pos_x` FLOAT NOT NULL DEFAULT 0,
    `pos_y` FLOAT NOT NULL DEFAULT 0,
    `pos_z` FLOAT NOT NULL DEFAULT 0,
    `orientation` FLOAT NOT NULL DEFAULT 0,
    `zone_id` INT UNSIGNED NOT NULL DEFAULT 0,
    `area_id` INT UNSIGNED NOT NULL DEFAULT 0,
    `location_name` VARCHAR(100) NOT NULL,
    `min_bots` INT UNSIGNED NOT NULL DEFAULT 10,
    `max_bots` INT UNSIGNED NOT NULL DEFAULT 50,
    `duration_minutes` INT UNSIGNED NOT NULL DEFAULT 30,
    `preparation_minutes` INT UNSIGNED NOT NULL DEFAULT 0 COMMENT 'Duration of PREPARING phase (0=skip)',
    `ending_minutes` INT UNSIGNED NOT NULL DEFAULT 0 COMMENT 'Duration of ENDING phase (0=skip)',
    `min_level` INT UNSIGNED NOT NULL DEFAULT 1,
    `max_level` INT UNSIGNED NOT NULL DEFAULT 80,
    `enabled` TINYINT(1) NOT NULL DEFAULT 1,
    `cooldown_minutes` INT UNSIGNED NOT NULL DEFAULT 0 COMMENT 'Per-event cooldown override in minutes (0=use global default)',
    `created_at` TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    `updated_at` TIMESTAMP DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
    PRIMARY KEY (`id`),
    KEY `idx_enabled` (`enabled`),
    KEY `idx_zone` (`zone_id`),
    KEY `idx_area` (`area_id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT='EC World Events - Event Definitions';

-- Schedule table for automatic events
CREATE TABLE `ec_world_event_schedules` (
    `id` INT UNSIGNED NOT NULL AUTO_INCREMENT,
    `event_id` INT UNSIGNED NOT NULL,
    `schedule_type` TINYINT UNSIGNED NOT NULL DEFAULT 0 COMMENT '0=Interval, 1=Cron, 2=OneTime',
    `interval_minutes` INT UNSIGNED DEFAULT NULL COMMENT 'For interval type',
    `cron_expression` VARCHAR(50) DEFAULT NULL COMMENT 'For cron type: min hour dom mon dow',
    `one_time_date` DATETIME DEFAULT NULL COMMENT 'For one-time events',
    `enabled` TINYINT(1) NOT NULL DEFAULT 1,
    `last_run` DATETIME DEFAULT NULL,
    `next_run` DATETIME DEFAULT NULL,
    PRIMARY KEY (`id`),
    KEY `idx_event` (`event_id`),
    KEY `idx_enabled` (`enabled`),
    KEY `idx_next_run` (`next_run`),
    CONSTRAINT `fk_schedule_event` FOREIGN KEY (`event_id`) REFERENCES `ec_world_events` (`id`) ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT='EC World Events - Scheduling';

-- NOTE: Schedule INSERT data is below, after event INSERTs (FK requires events to exist first)

-- Objects table (barricades, fortifications, banners)
CREATE TABLE `ec_world_event_objects` (
    `id` INT UNSIGNED NOT NULL AUTO_INCREMENT,
    `event_id` INT UNSIGNED NOT NULL,
    `gameobject_entry` INT UNSIGNED NOT NULL COMMENT 'Reference to gameobject_template.entry',
    `offset_x` FLOAT NOT NULL DEFAULT 0 COMMENT 'Offset from event center X',
    `offset_y` FLOAT NOT NULL DEFAULT 0 COMMENT 'Offset from event center Y',
    `offset_z` FLOAT NOT NULL DEFAULT 0 COMMENT 'Offset from event center Z',
    `orientation` FLOAT NOT NULL DEFAULT 0 COMMENT 'Object rotation',
    `spawn_phase` TINYINT UNSIGNED NOT NULL DEFAULT 1 COMMENT '1=PREPARING, 2=ACTIVE, 3=ENDING',
    `despawn_phase` TINYINT UNSIGNED NOT NULL DEFAULT 0 COMMENT '0=never, 1=PREPARING end, 2=ACTIVE end, 3=ENDING end',
    `name` VARCHAR(100) NOT NULL COMMENT 'Display name (e.g., Barricade, War Banner)',
    `is_capturable` TINYINT(1) NOT NULL DEFAULT 0 COMMENT 'Can be captured/burned like AV towers',
    `capture_time_seconds` INT UNSIGNED NOT NULL DEFAULT 240 COMMENT 'Time to capture/burn (default 4 min like AV)',
    `points_on_capture` INT NOT NULL DEFAULT 0 COMMENT 'Points awarded to capturing faction',
    `burning_entry` INT UNSIGNED DEFAULT NULL COMMENT 'Gameobject entry for burning state visual',
    `destroyed_entry` INT UNSIGNED DEFAULT NULL COMMENT 'Gameobject entry for destroyed state visual',
    `spawn_condition` TINYINT UNSIGNED NOT NULL DEFAULT 0 COMMENT '0=always, 1=attacker_win only, 2=defender_win only',
    `guard_respawn_seconds` INT UNSIGNED NOT NULL DEFAULT 0 COMMENT 'Barracks: interval to respawn guards (0=disabled)',
    `max_guards` INT UNSIGNED NOT NULL DEFAULT 0 COMMENT 'Barracks: max simultaneous alive guards from this building',
    PRIMARY KEY (`id`),
    KEY `idx_event` (`event_id`),
    KEY `idx_phase` (`spawn_phase`),
    KEY `idx_capturable` (`is_capturable`),
    CONSTRAINT `fk_object_event` FOREIGN KEY (`event_id`) REFERENCES `ec_world_events` (`id`) ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT='EC World Events - Spawned GameObjects';

-- Creatures table (named NPCs, elite guards, bosses)
CREATE TABLE `ec_world_event_creatures` (
    `id` INT UNSIGNED NOT NULL AUTO_INCREMENT,
    `event_id` INT UNSIGNED NOT NULL,
    `creature_entry` INT UNSIGNED NOT NULL COMMENT 'Reference to creature_template.entry',
    `offset_x` FLOAT NOT NULL DEFAULT 0 COMMENT 'Offset from event center X (ignored if assigned to building)',
    `offset_y` FLOAT NOT NULL DEFAULT 0 COMMENT 'Offset from event center Y (ignored if assigned to building)',
    `offset_z` FLOAT NOT NULL DEFAULT 0 COMMENT 'Offset from event center Z (ignored if assigned to building)',
    `orientation` FLOAT NOT NULL DEFAULT 0 COMMENT 'Creature facing direction',
    `spawn_phase` TINYINT UNSIGNED NOT NULL DEFAULT 2 COMMENT '1=PREPARING, 2=ACTIVE, 3=ENDING',
    `despawn_phase` TINYINT UNSIGNED NOT NULL DEFAULT 0 COMMENT '0=never, 1=PREPARING end, 2=ACTIVE end, 3=ENDING end',
    `is_boss` TINYINT(1) NOT NULL DEFAULT 0 COMMENT 'Is this a boss creature',
    `is_leader` TINYINT(1) NOT NULL DEFAULT 0 COMMENT 'Is this an event leader (Thrall, Jaina, etc.)',
    `name` VARCHAR(100) NOT NULL COMMENT 'Display name (e.g., Kor''kron Elite, Thrall)',
    `assigned_building_id` INT UNSIGNED DEFAULT NULL COMMENT 'Building this guard is assigned to (ec_world_event_objects.id)',
    `patrol_radius` FLOAT NOT NULL DEFAULT 10 COMMENT 'Patrol radius around assigned building or spawn point',
    -- Reinforcement/Arrival system (zeppelin, boat, portal arrivals)
    `spawn_delay_seconds` INT UNSIGNED NOT NULL DEFAULT 0 COMMENT 'Delay after phase starts before spawning (0=immediate)',
    `arrival_x` FLOAT DEFAULT NULL COMMENT 'Arrival point X (NULL=spawn at offset, set for zeppelin/boat/portal arrivals)',
    `arrival_y` FLOAT DEFAULT NULL COMMENT 'Arrival point Y',
    `arrival_z` FLOAT DEFAULT NULL COMMENT 'Arrival point Z',
    `arrival_method` TINYINT UNSIGNED NOT NULL DEFAULT 0 COMMENT '0=march/instant, 1=zeppelin, 2=boat, 3=portal',
    `reinforcement_wave` TINYINT UNSIGNED NOT NULL DEFAULT 0 COMMENT 'Wave number for grouping reinforcements (0=initial spawn)',
    PRIMARY KEY (`id`),
    KEY `idx_event` (`event_id`),
    KEY `idx_phase` (`spawn_phase`),
    KEY `idx_boss` (`is_boss`),
    KEY `idx_building` (`assigned_building_id`),
    KEY `idx_wave` (`reinforcement_wave`, `spawn_delay_seconds`),
    CONSTRAINT `fk_creature_event` FOREIGN KEY (`event_id`) REFERENCES `ec_world_events` (`id`) ON DELETE CASCADE,
    CONSTRAINT `fk_creature_building` FOREIGN KEY (`assigned_building_id`) REFERENCES `ec_world_event_objects` (`id`) ON DELETE SET NULL
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT='EC World Events - Spawned Creatures';

-- =============================================================================
-- DEFAULT EVENT DATA - Horde Raids on Alliance Towns
-- =============================================================================

-- Crossroads Raid (Horde attacking Alliance at Crossroads... wait, Crossroads is Horde)
-- Let's do Alliance towns:

-- 1. Goldshire Raid (Horde raids Goldshire)
INSERT INTO `ec_world_events` (`name`, `description`, `type`, `attacker_faction`, `defender_faction`,
    `map_id`, `pos_x`, `pos_y`, `pos_z`, `orientation`, `zone_id`, `area_id`, `location_name`,
    `min_bots`, `max_bots`, `duration_minutes`, `preparation_minutes`, `ending_minutes`, `min_level`, `max_level`, `enabled`)
VALUES
('Goldshire Raid', 'Horde forces are attacking Goldshire! Defend the town!', 0, 2, 1,
 0, -9459.52, 62.71, 56.33, 0, 12, 87, 'Goldshire',
 20, 100, 30, 5, 3, 10, 80, 1);

-- 2. Sentinel Hill Raid
INSERT INTO `ec_world_events` (`name`, `description`, `type`, `attacker_faction`, `defender_faction`,
    `map_id`, `pos_x`, `pos_y`, `pos_z`, `orientation`, `zone_id`, `area_id`, `location_name`,
    `min_bots`, `max_bots`, `duration_minutes`, `preparation_minutes`, `ending_minutes`, `min_level`, `max_level`, `enabled`)
VALUES
('Sentinel Hill Raid', 'Horde forces are attacking Sentinel Hill! Defend Westfall!', 0, 2, 1,
 0, -10628.5, 1036.64, 34.21, 0, 40, 108, 'Sentinel Hill',
 25, 120, 30, 5, 3, 15, 80, 1);

-- 3. Darkshire Raid
INSERT INTO `ec_world_events` (`name`, `description`, `type`, `attacker_faction`, `defender_faction`,
    `map_id`, `pos_x`, `pos_y`, `pos_z`, `orientation`, `zone_id`, `area_id`, `location_name`,
    `min_bots`, `max_bots`, `duration_minutes`, `preparation_minutes`, `ending_minutes`, `min_level`, `max_level`, `enabled`)
VALUES
('Darkshire Raid', 'Horde forces are attacking Darkshire! The town needs defenders!', 0, 2, 1,
 0, -10559.7, -1189.02, 28.07, 0, 10, 42, 'Darkshire',
 30, 150, 30, 5, 3, 20, 80, 1);

-- 4. Astranaar Raid (Night Elf town in Ashenvale)
INSERT INTO `ec_world_events` (`name`, `description`, `type`, `attacker_faction`, `defender_faction`,
    `map_id`, `pos_x`, `pos_y`, `pos_z`, `orientation`, `zone_id`, `area_id`, `location_name`,
    `min_bots`, `max_bots`, `duration_minutes`, `preparation_minutes`, `ending_minutes`, `min_level`, `max_level`, `enabled`)
VALUES
('Astranaar Raid', 'Horde forces are attacking Astranaar! Defend the forest!', 0, 2, 1,
 1, 2753.42, -348.53, 107.98, 0, 331, 415, 'Astranaar',
 40, 180, 30, 5, 3, 25, 80, 1);

-- 5. Auberdine Raid (Darkshore)
INSERT INTO `ec_world_events` (`name`, `description`, `type`, `attacker_faction`, `defender_faction`,
    `map_id`, `pos_x`, `pos_y`, `pos_z`, `orientation`, `zone_id`, `area_id`, `location_name`,
    `min_bots`, `max_bots`, `duration_minutes`, `preparation_minutes`, `ending_minutes`, `min_level`, `max_level`, `enabled`)
VALUES
('Auberdine Raid', 'Horde forces are attacking Auberdine! Protect the harbor!', 0, 2, 1,
 1, 6454.09, 527.78, 8.68, 0, 148, 442, 'Auberdine',
 35, 150, 30, 5, 3, 15, 80, 1);

-- =============================================================================
-- Alliance Raids on Horde Towns
-- =============================================================================

-- 6. Crossroads Raid (Alliance attacks the Crossroads)
INSERT INTO `ec_world_events` (`name`, `description`, `type`, `attacker_faction`, `defender_faction`,
    `map_id`, `pos_x`, `pos_y`, `pos_z`, `orientation`, `zone_id`, `area_id`, `location_name`,
    `min_bots`, `max_bots`, `duration_minutes`, `preparation_minutes`, `ending_minutes`, `min_level`, `max_level`, `enabled`)
VALUES
('Crossroads Raid', 'Alliance forces are attacking the Crossroads! For the Horde!', 0, 1, 2,
 1, -467.14, -2638.91, 95.94, 0, 17, 380, 'The Crossroads',
 30, 150, 30, 5, 3, 15, 80, 1);

-- 7. Camp Taurajo Raid
INSERT INTO `ec_world_events` (`name`, `description`, `type`, `attacker_faction`, `defender_faction`,
    `map_id`, `pos_x`, `pos_y`, `pos_z`, `orientation`, `zone_id`, `area_id`, `location_name`,
    `min_bots`, `max_bots`, `duration_minutes`, `preparation_minutes`, `ending_minutes`, `min_level`, `max_level`, `enabled`)
VALUES
('Camp Taurajo Raid', 'Alliance forces are attacking Camp Taurajo! Defend the camp!', 0, 1, 2,
 1, -2365.36, -1912.73, 95.81, 0, 17, 378, 'Camp Taurajo',
 25, 120, 30, 5, 3, 20, 80, 1);

-- 8. Grom'gol Base Camp Raid (Stranglethorn)
INSERT INTO `ec_world_events` (`name`, `description`, `type`, `attacker_faction`, `defender_faction`,
    `map_id`, `pos_x`, `pos_y`, `pos_z`, `orientation`, `zone_id`, `area_id`, `location_name`,
    `min_bots`, `max_bots`, `duration_minutes`, `preparation_minutes`, `ending_minutes`, `min_level`, `max_level`, `enabled`)
VALUES
('Grom''gol Raid', 'Alliance forces are attacking Grom''gol Base Camp! Defend the port!', 0, 1, 2,
 0, -12417.01, 146.48, 3.26, 0, 33, 117, 'Grom''gol Base Camp',
 40, 180, 30, 5, 3, 30, 80, 1);

-- 9. Tarren Mill Raid (Hillsbrad)
INSERT INTO `ec_world_events` (`name`, `description`, `type`, `attacker_faction`, `defender_faction`,
    `map_id`, `pos_x`, `pos_y`, `pos_z`, `orientation`, `zone_id`, `area_id`, `location_name`,
    `min_bots`, `max_bots`, `duration_minutes`, `preparation_minutes`, `ending_minutes`, `min_level`, `max_level`, `enabled`)
VALUES
('Tarren Mill Raid', 'Alliance forces are attacking Tarren Mill! Classic PvP returns!', 0, 1, 2,
 0, -21.75, -920.25, 54.86, 0, 267, 272, 'Tarren Mill',
 50, 200, 30, 5, 3, 25, 80, 1);

-- 10. Splintertree Post Raid (Ashenvale)
INSERT INTO `ec_world_events` (`name`, `description`, `type`, `attacker_faction`, `defender_faction`,
    `map_id`, `pos_x`, `pos_y`, `pos_z`, `orientation`, `zone_id`, `area_id`, `location_name`,
    `min_bots`, `max_bots`, `duration_minutes`, `preparation_minutes`, `ending_minutes`, `min_level`, `max_level`, `enabled`)
VALUES
('Splintertree Raid', 'Alliance forces are attacking Splintertree Post! Defend Ashenvale!', 0, 1, 2,
 1, 2256.97, -2521.63, 102.59, 0, 331, 2301, 'Splintertree Post',
 35, 160, 30, 5, 3, 20, 80, 1);

-- =============================================================================
-- DEFAULT EVENT SCHEDULES (all disabled by default - enable via SET enabled=1)
-- Schedule type 0 = Interval-based (repeats every N minutes)
-- Staggered intervals so events don't all fire simultaneously
-- next_run = NULL means "run on next check" when enabled
-- =============================================================================

-- Horde attacks Alliance towns (events 1-5)
INSERT INTO `ec_world_event_schedules` (`event_id`, `schedule_type`, `interval_minutes`, `enabled`, `next_run`)
VALUES
(1, 0, 90,  1, NULL),   -- Goldshire Raid: every 90 min
(2, 0, 105, 1, NULL),   -- Sentinel Hill Raid: every 105 min
(3, 0, 120, 1, NULL),   -- Darkshire Raid: every 120 min
(4, 0, 100, 1, NULL),   -- Astranaar Raid: every 100 min
(5, 0, 110, 1, NULL);   -- Auberdine Raid: every 110 min

-- Alliance attacks Horde towns (events 6-10)
INSERT INTO `ec_world_event_schedules` (`event_id`, `schedule_type`, `interval_minutes`, `enabled`, `next_run`)
VALUES
(6, 0, 95,  1, NULL),   -- Crossroads Raid: every 95 min
(7, 0, 115, 1, NULL),   -- Camp Taurajo Raid: every 115 min
(8, 0, 100, 1, NULL),   -- Grom'gol Raid: every 100 min
(9, 0, 85,  1, NULL),   -- Tarren Mill Raid: every 85 min
(10, 0, 105, 1, NULL);  -- Splintertree Raid: every 105 min

-- =============================================================================
-- WAR BANNER GAMEOBJECTS (required by ec_world_event_objects below)
-- =============================================================================
-- displayId 5771 = Alliance Banner model (used by entry 178365 etc.)
-- displayId 5773 = Horde Banner model (used by entry 178364 etc.)
-- displayId 9165 = Neutral Banner Aura model (used by entry 180102 etc.)
-- type 5 = GAMEOBJECT_TYPE_GENERIC (visual prop, no interaction)
-- Only delete EC-owned entries in the custom 9000xx/9001xx ranges.
DELETE FROM `gameobject_template` WHERE `entry` BETWEEN 900100 AND 900114
   OR `entry` BETWEEN 900080 AND 900082;
INSERT INTO `gameobject_template` (`entry`, `type`, `displayId`, `name`, `size`, `ScriptName`) VALUES
-- Banners (moved to 900080+ to avoid AB conflict)
(900080, 5, 5771, 'EC Alliance War Banner', 1.5, ''),
(900081, 5, 5773, 'EC Horde War Banner',    1.5, ''),
(900082, 5, 9165, 'EC Neutral War Banner',   1.5, ''),
-- Atmosphere
(900100, 5, 6411, 'EC War Bonfire',          1.0, ''),
(900101, 5,  192, 'EC Campfire',             1.0, ''),
-- Towers & Barricades (proper models instead of AV banner placeholders)
-- displayId 9423 = Guard Tower (from entry 19423)
-- displayId 9449 = Guard Tower 2 / Bunker-style (from entry 19449)
-- displayId 9482 = Troll Watch Tower (from entry 19482, for Horde towers)
-- displayId 9392 = PvP Wall / Wooden Barricade (from entry 19392)
-- displayId 9393 = PvP Wall 2 / Large Barricade (from entry 19393)
-- displayId 9395 = Spike Wall (from entry 19395)
(900102, 5, 9423, 'EC Guard Tower',          1.0, ''),
(900103, 5, 9449, 'EC Alliance Bunker',      1.0, ''),
(900104, 5,    3, 'EC Horde Tower',          1.0, ''),
(900105, 5, 9392, 'EC Wooden Barricade',     1.0, ''),
(900106, 5, 9393, 'EC Large Barricade',      1.0, ''),
(900107, 5, 9395, 'EC Spike Wall',           1.0, ''),
-- Burning/Destroyed variants for capturable buildings
-- displayId 410 = Tower Fire effect, displayId 45 = Tower Smoke
-- displayId 7699 = Wreckage A (rubble), displayId 5752 = Shield Wreckage
(900108, 5, 410,  'EC Guard Tower (Burning)',       1.0, ''),
(900109, 5,   45, 'EC Guard Tower (Destroyed)',     1.0, ''),
(900110, 5, 410,  'EC Alliance Bunker (Burning)',   1.0, ''),
(900111, 5,   45, 'EC Alliance Bunker (Destroyed)', 1.0, ''),
(900112, 5, 410,  'EC Horde Tower (Burning)',       1.0, ''),
(900113, 5,   45, 'EC Horde Tower (Destroyed)',     1.0, ''),
(900114, 5, 45,   'EC Barricade (Destroyed)',       1.0, '');

-- =============================================================================
-- EC CUSTOM GAMEOBJECT REFERENCE
-- =============================================================================
-- Banners:         900080=Alliance, 900081=Horde, 900082=Neutral
-- Atmosphere:      900100=Bonfire, 900101=Campfire
-- Towers (intact): 900102=Guard Tower, 900103=Alliance Bunker, 900104=Horde Tower
-- Towers (burning): 900108=Guard Tower, 900110=Alliance Bunker, 900112=Horde Tower
-- Towers (destroyed): 900109=Guard Tower, 900111=Alliance Bunker, 900113=Horde Tower
-- Barricades:      900105=Wooden, 900106=Large, 900107=Spike Wall
-- Barricade (destroyed): 900114=Rubble
--
-- CAPTURABLE OBJECT MECHANICS:
--   is_capturable = 1     : Object can be clicked to start burning
--   capture_time_seconds  : How long until it's destroyed (default 240 = 4 min like AV)
--   points_on_capture     : Score points when captured (affects event outcome)
--   burning_entry         : Gameobject to spawn during burning phase
--   destroyed_entry       : Gameobject to spawn when destroyed
--
-- Note: Verify entries exist in your gameobject_template table

-- =============================================================================
-- OBJECTS AND CREATURES - Goldshire Raid (event_id = 1, Horde attacking)
-- =============================================================================

-- PREPARING PHASE: War banners mark the rally point, creates atmosphere
INSERT INTO `ec_world_event_objects` (`event_id`, `gameobject_entry`, `offset_x`, `offset_y`, `offset_z`, `orientation`, `spawn_phase`, `despawn_phase`, `name`)
VALUES
(1, 900081, 0, 0, 0, 0, 1, 0, 'Horde War Banner (rally point)'),
(1, 900081, 15, 10, 0, 0.78, 1, 0, 'Horde War Banner (north-east)'),
(1, 900081, -15, 10, 0, 5.5, 1, 0, 'Horde War Banner (north-west)'),
(1, 900101, 0, 5, 0, 0, 1, 0, 'War Campfire');

-- ACTIVE PHASE: Barricades for tactical combat cover
INSERT INTO `ec_world_event_objects` (`event_id`, `gameobject_entry`, `offset_x`, `offset_y`, `offset_z`, `orientation`, `spawn_phase`, `despawn_phase`, `name`)
VALUES
(1, 900105, 20, 0, 0, 1.57, 2, 0, 'Barricade (east road)'),
(1, 900105, -20, 0, 0, 1.57, 2, 0, 'Barricade (west road)'),
(1, 900105, 0, 20, 0, 0, 2, 0, 'Barricade (north road)'),
(1, 900105, 0, -20, 0, 0, 2, 0, 'Barricade (south road)');

-- Horde Elite Guards (spawn during PREPARING to lead the assault)
INSERT INTO `ec_world_event_creatures` (`event_id`, `creature_entry`, `offset_x`, `offset_y`, `offset_z`, `orientation`, `spawn_phase`, `despawn_phase`, `is_boss`, `is_leader`, `name`)
VALUES
(1, 14304, 0, 3, 0, 0, 1, 0, 0, 1, 'Kor''kron Warlord'),
(1, 14304, 5, 0, 0, 5.5, 1, 0, 0, 0, 'Kor''kron Elite'),
(1, 14304, -5, 0, 0, 0.78, 1, 0, 0, 0, 'Kor''kron Elite');

-- PREPARING: Peons constructing fortifications (despawn when ACTIVE begins)
INSERT INTO `ec_world_event_creatures` (`event_id`, `creature_entry`, `offset_x`, `offset_y`, `offset_z`, `orientation`, `spawn_phase`, `despawn_phase`, `is_boss`, `is_leader`, `name`, `patrol_radius`)
VALUES
(1, 11656, 20, 2, 0, 1.57,  1, 2, 0, 0, 'Peon - East Barricade',  5),
(1, 11656, -20, 2, 0, 4.71, 1, 2, 0, 0, 'Peon - West Barricade',  5),
(1, 11656, 2, 20, 0, 0,     1, 2, 0, 0, 'Peon - North Barricade', 5),
(1, 11656, 2, -20, 0, 3.14, 1, 2, 0, 0, 'Peon - South Barricade', 5);

-- =============================================================================
-- OBJECTS AND CREATURES - Tarren Mill Raid (event_id = 9, Alliance attacking)
-- =============================================================================

-- PREPARING PHASE: Alliance rally point
INSERT INTO `ec_world_event_objects` (`event_id`, `gameobject_entry`, `offset_x`, `offset_y`, `offset_z`, `orientation`, `spawn_phase`, `despawn_phase`, `name`)
VALUES
(9, 900080, 0, 0, 0, 0, 1, 0, 'Alliance Battle Standard'),
(9, 900080, 12, 8, 0, 0.78, 1, 0, 'Alliance War Banner (flank)'),
(9, 900080, -12, 8, 0, 5.5, 1, 0, 'Alliance War Banner (flank)'),
(9, 900101, 0, 5, 0, 0, 1, 0, 'Command Campfire');

-- ACTIVE PHASE: Defensive fortifications
INSERT INTO `ec_world_event_objects` (`event_id`, `gameobject_entry`, `offset_x`, `offset_y`, `offset_z`, `orientation`, `spawn_phase`, `despawn_phase`, `name`)
VALUES
(9, 900105, 25, 0, 0, 1.57, 2, 0, 'Defensive Barricade (east)'),
(9, 900105, -25, 0, 0, 1.57, 2, 0, 'Defensive Barricade (west)'),
(9, 900106, 0, 25, 0, 0, 2, 0, 'Large Barricade (north)');

-- Alliance Elite Guards
INSERT INTO `ec_world_event_creatures` (`event_id`, `creature_entry`, `offset_x`, `offset_y`, `offset_z`, `orientation`, `spawn_phase`, `despawn_phase`, `is_boss`, `is_leader`, `name`)
VALUES
(9, 12480, 0, 3, 0, 0, 1, 0, 0, 1, '7th Legion Commander'),
(9, 12481, 5, 0, 0, 5.5, 1, 0, 0, 0, '7th Legion Elite'),
(9, 12481, -5, 0, 0, 0.78, 1, 0, 0, 0, '7th Legion Elite');

-- PREPARING: Construction workers building fortifications (despawn when ACTIVE begins)
INSERT INTO `ec_world_event_creatures` (`event_id`, `creature_entry`, `offset_x`, `offset_y`, `offset_z`, `orientation`, `spawn_phase`, `despawn_phase`, `is_boss`, `is_leader`, `name`, `patrol_radius`)
VALUES
(9, 28569, 25, 2, 0, 1.57,  1, 2, 0, 0, 'Worker - East Barricade',  5),
(9, 28569, -25, 2, 0, 4.71, 1, 2, 0, 0, 'Worker - West Barricade',  5),
(9, 28569, 2, 25, 0, 0,     1, 2, 0, 0, 'Worker - North Barricade', 5);

-- =============================================================================
-- OBJECTS AND CREATURES - Crossroads Raid (event_id = 6, Alliance attacking)
-- =============================================================================

-- PREPARING PHASE: Alliance prepares assault
INSERT INTO `ec_world_event_objects` (`event_id`, `gameobject_entry`, `offset_x`, `offset_y`, `offset_z`, `orientation`, `spawn_phase`, `despawn_phase`, `name`)
VALUES
(6, 900080, 0, 0, 0, 0, 1, 0, 'Alliance War Banner (center)'),
(6, 900080, 10, 5, 0, 0.78, 1, 0, 'Alliance War Banner (east)'),
(6, 900080, -10, 5, 0, 5.5, 1, 0, 'Alliance War Banner (west)');

-- ACTIVE PHASE: Barricades around the Crossroads
INSERT INTO `ec_world_event_objects` (`event_id`, `gameobject_entry`, `offset_x`, `offset_y`, `offset_z`, `orientation`, `spawn_phase`, `despawn_phase`, `name`)
VALUES
(6, 900105, 30, 0, 0, 1.57, 2, 0, 'Barricade (east entrance)'),
(6, 900105, -30, 0, 0, 1.57, 2, 0, 'Barricade (west entrance)'),
(6, 900105, 0, 30, 0, 0, 2, 0, 'Barricade (north entrance)'),
(6, 900105, 0, -30, 0, 0, 2, 0, 'Barricade (south entrance)');

-- Alliance Elite Guards
INSERT INTO `ec_world_event_creatures` (`event_id`, `creature_entry`, `offset_x`, `offset_y`, `offset_z`, `orientation`, `spawn_phase`, `despawn_phase`, `is_boss`, `is_leader`, `name`)
VALUES
(6, 12480, 0, 3, 0, 0, 1, 0, 0, 1, 'Alliance Field Marshal'),
(6, 12481, 5, 0, 0, 5.5, 1, 0, 0, 0, 'Alliance Elite Footman'),
(6, 12481, -5, 0, 0, 0.78, 1, 0, 0, 0, 'Alliance Elite Footman');

-- PREPARING: Construction workers building fortifications (despawn when ACTIVE begins)
INSERT INTO `ec_world_event_creatures` (`event_id`, `creature_entry`, `offset_x`, `offset_y`, `offset_z`, `orientation`, `spawn_phase`, `despawn_phase`, `is_boss`, `is_leader`, `name`, `patrol_radius`)
VALUES
(6, 28569, 30, 2, 0, 1.57,  1, 2, 0, 0, 'Worker - East Barricade',  5),
(6, 28569, -30, 2, 0, 4.71, 1, 2, 0, 0, 'Worker - West Barricade',  5),
(6, 28569, 2, 30, 0, 0,     1, 2, 0, 0, 'Worker - North Barricade', 5),
(6, 28569, 2, -30, 0, 3.14, 1, 2, 0, 0, 'Worker - South Barricade', 5);

-- =============================================================================
-- OBJECTS AND CREATURES - Sentinel Hill Raid (event_id = 2, Horde attacking)
-- =============================================================================

-- PREPARING PHASE: Horde rally with banners and campfires
INSERT INTO `ec_world_event_objects` (`event_id`, `gameobject_entry`, `offset_x`, `offset_y`, `offset_z`, `orientation`, `spawn_phase`, `despawn_phase`, `name`)
VALUES
(2, 900081, 0, 0, 0, 0, 1, 0, 'Horde War Banner (center)'),
(2, 900081, 12, 8, 0, 0.78, 1, 0, 'Horde War Banner (east)'),
(2, 900101, 5, 0, 0, 0, 1, 0, 'War Campfire');

-- ACTIVE PHASE: Barricades and tower at strategic points
INSERT INTO `ec_world_event_objects` (`event_id`, `gameobject_entry`, `offset_x`, `offset_y`, `offset_z`, `orientation`, `spawn_phase`, `despawn_phase`, `name`)
VALUES
(2, 900105, 25, 0, 0, 1.57, 2, 0, 'Barricade (east hill approach)'),
(2, 900105, -25, 0, 0, 1.57, 2, 0, 'Barricade (west hill approach)'),
(2, 900104, 0, 35, 0, 0, 2, 0, 'Horde Tower (north overlook)'),
(2, 900106, 15, 15, 0, 0.78, 2, 0, 'Large Barricade (northeast)');

-- Horde Elite Guards
INSERT INTO `ec_world_event_creatures` (`event_id`, `creature_entry`, `offset_x`, `offset_y`, `offset_z`, `orientation`, `spawn_phase`, `despawn_phase`, `is_boss`, `is_leader`, `name`)
VALUES
(2, 14304, 0, 3, 0, 0, 1, 0, 0, 1, 'Kor''kron Warlord'),
(2, 14304, 5, 0, 0, 5.5, 1, 0, 0, 0, 'Kor''kron Elite'),
(2, 14304, -5, 0, 0, 0.78, 1, 0, 0, 0, 'Kor''kron Elite'),
(2, 14304, 0, 30, 0, 3.14, 2, 0, 0, 0, 'Kor''kron Tower Guard');

-- PREPARING: Peons constructing fortifications (despawn when ACTIVE begins)
INSERT INTO `ec_world_event_creatures` (`event_id`, `creature_entry`, `offset_x`, `offset_y`, `offset_z`, `orientation`, `spawn_phase`, `despawn_phase`, `is_boss`, `is_leader`, `name`, `patrol_radius`)
VALUES
(2, 11656, 25, 2, 0, 1.57,  1, 2, 0, 0, 'Peon - East Barricade',   5),
(2, 11656, -25, 2, 0, 4.71, 1, 2, 0, 0, 'Peon - West Barricade',   5),
(2, 11656, 2, 35, 0, 0,     1, 2, 0, 0, 'Peon - Tower Construction', 5);

-- =============================================================================
-- OBJECTS AND CREATURES - Darkshire Raid (event_id = 3, Horde attacking)
-- =============================================================================

-- PREPARING PHASE: Horde rally in the dark woods
INSERT INTO `ec_world_event_objects` (`event_id`, `gameobject_entry`, `offset_x`, `offset_y`, `offset_z`, `orientation`, `spawn_phase`, `despawn_phase`, `name`)
VALUES
(3, 900081, 0, 0, 0, 0, 1, 0, 'Horde War Banner'),
(3, 900100, 5, 5, 0, 0, 1, 0, 'War Bonfire'),
(3, 900100, -5, 5, 0, 0, 1, 0, 'War Bonfire');

-- ACTIVE PHASE: Heavy fortification in the town
INSERT INTO `ec_world_event_objects` (`event_id`, `gameobject_entry`, `offset_x`, `offset_y`, `offset_z`, `orientation`, `spawn_phase`, `despawn_phase`, `name`)
VALUES
(3, 900105, 20, 0, 0, 1.57, 2, 0, 'Barricade (east road)'),
(3, 900105, -20, 0, 0, 1.57, 2, 0, 'Barricade (west road)'),
(3, 900104, 30, 20, 0, 5.5, 2, 0, 'Horde Tower (east hill)'),
(3, 900104, -30, 20, 0, 0.78, 2, 0, 'Horde Tower (west hill)'),
(3, 900106, 0, 25, 0, 0, 2, 0, 'Large Barricade (north gate)');

-- Horde Elite Guards (more due to higher level zone)
INSERT INTO `ec_world_event_creatures` (`event_id`, `creature_entry`, `offset_x`, `offset_y`, `offset_z`, `orientation`, `spawn_phase`, `despawn_phase`, `is_boss`, `is_leader`, `name`)
VALUES
(3, 14304, 0, 3, 0, 0, 1, 0, 0, 1, 'Kor''kron Battlemaster'),
(3, 14304, 5, 0, 0, 5.5, 1, 0, 0, 0, 'Kor''kron Elite'),
(3, 14304, -5, 0, 0, 0.78, 1, 0, 0, 0, 'Kor''kron Elite'),
(3, 14304, 30, 20, 0, 5.5, 2, 0, 0, 0, 'Tower Sharpshooter'),
(3, 14304, -30, 20, 0, 0.78, 2, 0, 0, 0, 'Tower Sharpshooter');

-- PREPARING: Peons constructing fortifications (despawn when ACTIVE begins)
INSERT INTO `ec_world_event_creatures` (`event_id`, `creature_entry`, `offset_x`, `offset_y`, `offset_z`, `orientation`, `spawn_phase`, `despawn_phase`, `is_boss`, `is_leader`, `name`, `patrol_radius`)
VALUES
(3, 11656, 20, 2, 0, 1.57,  1, 2, 0, 0, 'Peon - East Barricade',   5),
(3, 11656, -20, 2, 0, 4.71, 1, 2, 0, 0, 'Peon - West Barricade',   5),
(3, 11656, 30, 22, 0, 5.5,  1, 2, 0, 0, 'Peon - East Tower',       5),
(3, 11656, -30, 22, 0, 0.78, 1, 2, 0, 0, 'Peon - West Tower',      5);

-- =============================================================================
-- OBJECTS AND CREATURES - Astranaar Raid (event_id = 4, Horde attacking)
-- =============================================================================

-- PREPARING PHASE: Horde gathers in the forest
INSERT INTO `ec_world_event_objects` (`event_id`, `gameobject_entry`, `offset_x`, `offset_y`, `offset_z`, `orientation`, `spawn_phase`, `despawn_phase`, `name`)
VALUES
(4, 900081, 0, 0, 0, 0, 1, 0, 'Horde War Banner'),
(4, 900081, 15, 0, 0, 1.57, 1, 0, 'Horde War Banner (flank)'),
(4, 900101, 7, 0, 0, 0, 1, 0, 'War Campfire');

-- ACTIVE PHASE: Forest fortifications with towers overlooking roads
INSERT INTO `ec_world_event_objects` (`event_id`, `gameobject_entry`, `offset_x`, `offset_y`, `offset_z`, `orientation`, `spawn_phase`, `despawn_phase`, `name`)
VALUES
(4, 900105, 30, 0, 0, 1.57, 2, 0, 'Barricade (road east)'),
(4, 900105, -30, 0, 0, 1.57, 2, 0, 'Barricade (road west)'),
(4, 900104, 25, 25, 0, 5.5, 2, 0, 'Horde Tower (northeast)'),
(4, 900104, -25, 25, 0, 0.78, 2, 0, 'Horde Tower (northwest)'),
(4, 900106, 0, 35, 0, 0, 2, 0, 'Large Barricade (northern approach)');

-- Horde Elite Guards for Ashenvale
INSERT INTO `ec_world_event_creatures` (`event_id`, `creature_entry`, `offset_x`, `offset_y`, `offset_z`, `orientation`, `spawn_phase`, `despawn_phase`, `is_boss`, `is_leader`, `name`)
VALUES
(4, 14304, 0, 3, 0, 0, 1, 0, 0, 1, 'Kor''kron Warlord'),
(4, 14304, 6, 0, 0, 5.5, 1, 0, 0, 0, 'Kor''kron Elite'),
(4, 14304, -6, 0, 0, 0.78, 1, 0, 0, 0, 'Kor''kron Elite'),
(4, 14304, 25, 25, 0, 5.5, 2, 0, 0, 0, 'Tower Defender'),
(4, 14304, -25, 25, 0, 0.78, 2, 0, 0, 0, 'Tower Defender');

-- PREPARING: Peons constructing fortifications (despawn when ACTIVE begins)
INSERT INTO `ec_world_event_creatures` (`event_id`, `creature_entry`, `offset_x`, `offset_y`, `offset_z`, `orientation`, `spawn_phase`, `despawn_phase`, `is_boss`, `is_leader`, `name`, `patrol_radius`)
VALUES
(4, 11656, 30, 2, 0, 1.57,  1, 2, 0, 0, 'Peon - East Barricade',    5),
(4, 11656, -30, 2, 0, 4.71, 1, 2, 0, 0, 'Peon - West Barricade',    5),
(4, 11656, 25, 27, 0, 5.5,  1, 2, 0, 0, 'Peon - NE Tower',          5),
(4, 11656, -25, 27, 0, 0.78, 1, 2, 0, 0, 'Peon - NW Tower',         5);

-- =============================================================================
-- OBJECTS AND CREATURES - Auberdine Raid (event_id = 5, Horde attacking)
-- =============================================================================

-- PREPARING PHASE: Beach landing preparation
INSERT INTO `ec_world_event_objects` (`event_id`, `gameobject_entry`, `offset_x`, `offset_y`, `offset_z`, `orientation`, `spawn_phase`, `despawn_phase`, `name`)
VALUES
(5, 900081, 0, 0, 0, 0, 1, 0, 'Horde War Banner (beachhead)'),
(5, 900101, 8, 0, 0, 0, 1, 0, 'Landing Campfire'),
(5, 900101, -8, 0, 0, 0, 1, 0, 'Landing Campfire');

-- ACTIVE PHASE: Harbor fortification
INSERT INTO `ec_world_event_objects` (`event_id`, `gameobject_entry`, `offset_x`, `offset_y`, `offset_z`, `orientation`, `spawn_phase`, `despawn_phase`, `name`)
VALUES
(5, 900105, 20, 0, 0, 1.57, 2, 0, 'Barricade (dock entrance)'),
(5, 900105, -20, 0, 0, 1.57, 2, 0, 'Barricade (dock entrance)'),
(5, 900104, 30, 15, 0, 5.5, 2, 0, 'Horde Tower (harbor overlook)'),
(5, 900106, 0, 30, 0, 0, 2, 0, 'Large Barricade (town approach)');

-- Horde Elite Guards for harbor assault
INSERT INTO `ec_world_event_creatures` (`event_id`, `creature_entry`, `offset_x`, `offset_y`, `offset_z`, `orientation`, `spawn_phase`, `despawn_phase`, `is_boss`, `is_leader`, `name`)
VALUES
(5, 14304, 0, 3, 0, 0, 1, 0, 0, 1, 'Kor''kron Naval Commander'),
(5, 14304, 5, 0, 0, 5.5, 1, 0, 0, 0, 'Kor''kron Marine'),
(5, 14304, -5, 0, 0, 0.78, 1, 0, 0, 0, 'Kor''kron Marine');

-- PREPARING: Peons constructing fortifications (despawn when ACTIVE begins)
INSERT INTO `ec_world_event_creatures` (`event_id`, `creature_entry`, `offset_x`, `offset_y`, `offset_z`, `orientation`, `spawn_phase`, `despawn_phase`, `is_boss`, `is_leader`, `name`, `patrol_radius`)
VALUES
(5, 11656, 20, 2, 0, 1.57,  1, 2, 0, 0, 'Peon - Dock Barricade',    5),
(5, 11656, -20, 2, 0, 4.71, 1, 2, 0, 0, 'Peon - Dock Barricade',    5),
(5, 11656, 30, 17, 0, 5.5,  1, 2, 0, 0, 'Peon - Tower Construction', 5);

-- =============================================================================
-- OBJECTS AND CREATURES - Camp Taurajo Raid (event_id = 7, Alliance attacking)
-- =============================================================================

-- PREPARING PHASE: Horde defenders fortify Camp Taurajo
INSERT INTO `ec_world_event_objects` (`event_id`, `gameobject_entry`, `offset_x`, `offset_y`, `offset_z`, `orientation`, `spawn_phase`, `despawn_phase`, `name`)
VALUES
(7, 900081, 0, 0, 0, 0, 1, 0, 'Horde Defense Standard'),
(7, 900081, 10, 5, 0, 0.78, 1, 0, 'Horde War Banner'),
(7, 900101, 5, 0, 0, 0, 1, 0, 'Command Campfire');

-- ACTIVE PHASE: Bunker-style fortifications around the camp
INSERT INTO `ec_world_event_objects` (`event_id`, `gameobject_entry`, `offset_x`, `offset_y`, `offset_z`, `orientation`, `spawn_phase`, `despawn_phase`, `name`)
VALUES
(7, 900105, 25, 0, 0, 1.57, 2, 0, 'Barricade (eastern approach)'),
(7, 900105, -25, 0, 0, 1.57, 2, 0, 'Barricade (western approach)'),
(7, 900103, 30, 20, 0, 5.5, 2, 0, 'Alliance Bunker (northeast)'),
(7, 900106, 0, 30, 0, 0, 2, 0, 'Large Barricade (northern gate)');

-- Alliance Elite Guards (assault force appears when ACTIVE starts)
INSERT INTO `ec_world_event_creatures` (`event_id`, `creature_entry`, `offset_x`, `offset_y`, `offset_z`, `orientation`, `spawn_phase`, `despawn_phase`, `is_boss`, `is_leader`, `name`)
VALUES
(7, 12480, 0, 3, 0, 0, 2, 0, 0, 1, '7th Legion Commander'),
(7, 12481, 5, 0, 0, 5.5, 2, 0, 0, 0, '7th Legion Footman'),
(7, 12481, -5, 0, 0, 0.78, 2, 0, 0, 0, '7th Legion Footman'),
(7, 12481, 30, 20, 0, 5.5, 2, 0, 0, 0, 'Bunker Guard');

-- PREPARING: Horde peons building fortifications (despawn when ACTIVE begins)
INSERT INTO `ec_world_event_creatures` (`event_id`, `creature_entry`, `offset_x`, `offset_y`, `offset_z`, `orientation`, `spawn_phase`, `despawn_phase`, `is_boss`, `is_leader`, `name`, `patrol_radius`)
VALUES
(7, 11656, 25, 2, 0, 1.57,  1, 2, 0, 0, 'Peon - East Barricade',   5),
(7, 11656, -25, 2, 0, 4.71, 1, 2, 0, 0, 'Peon - West Barricade',   5),
(7, 11656, 30, 22, 0, 5.5,  1, 2, 0, 0, 'Peon - Bunker Construction', 5);

-- =============================================================================
-- OBJECTS AND CREATURES - Grom'gol Raid (event_id = 8, Alliance attacking)
-- =============================================================================

-- PREPARING PHASE: Alliance prepares naval assault
INSERT INTO `ec_world_event_objects` (`event_id`, `gameobject_entry`, `offset_x`, `offset_y`, `offset_z`, `orientation`, `spawn_phase`, `despawn_phase`, `name`)
VALUES
(8, 900080, 0, 0, 0, 0, 1, 0, 'Alliance Naval Standard'),
(8, 900080, 12, 0, 0, 1.57, 1, 0, 'Alliance War Banner'),
(8, 900101, 6, 0, 0, 0, 1, 0, 'Command Campfire');

-- ACTIVE PHASE: Heavy fortifications for the port assault
INSERT INTO `ec_world_event_objects` (`event_id`, `gameobject_entry`, `offset_x`, `offset_y`, `offset_z`, `orientation`, `spawn_phase`, `despawn_phase`, `name`)
VALUES
(8, 900105, 30, 0, 0, 1.57, 2, 0, 'Barricade (dock west)'),
(8, 900105, -30, 0, 0, 1.57, 2, 0, 'Barricade (dock east)'),
(8, 900103, 35, 25, 0, 5.5, 2, 0, 'Alliance Bunker (port overlook)'),
(8, 900103, -35, 25, 0, 0.78, 2, 0, 'Alliance Bunker (port overlook)'),
(8, 900106, 0, 35, 0, 0, 2, 0, 'Large Barricade (base approach)'),
(8, 900102, 0, 45, 0, 0, 2, 0, 'Guard Tower (command post)');

-- Alliance Assault Force - Staging point: road near ogres (-12395.1, 131.9, 3.45)
-- Offset from event center to staging point: x=21.9, y=-14.57, z=0.19

-- PREPARING: Scouts at staging point on the road (no arrival point, appear in place)
INSERT INTO `ec_world_event_creatures` (`event_id`, `creature_entry`, `offset_x`, `offset_y`, `offset_z`, `orientation`, `spawn_phase`, `despawn_phase`, `is_boss`, `is_leader`, `name`, `patrol_radius`)
VALUES
(8, 12480, 21.9, -14.57, 0.19, 4.71, 1, 0, 0, 1, '7th Legion Admiral', 5),
(8, 12481, 18.0, -16.0,  0.19, 4.71, 1, 0, 0, 0, '7th Legion Scout', 5),
(8, 12481, 25.0, -13.0,  0.19, 4.71, 1, 0, 0, 0, '7th Legion Scout', 5);

-- PREPARING: Construction workers building fortifications (despawn when ACTIVE begins)
INSERT INTO `ec_world_event_creatures` (`event_id`, `creature_entry`, `offset_x`, `offset_y`, `offset_z`, `orientation`, `spawn_phase`, `despawn_phase`, `is_boss`, `is_leader`, `name`, `patrol_radius`)
VALUES
(8, 28569, 30, 2, 0, 1.57,  1, 2, 0, 0, 'Worker - West Dock Barricade',  5),
(8, 28569, -30, 2, 0, 4.71, 1, 2, 0, 0, 'Worker - East Dock Barricade',  5),
(8, 28569, 35, 27, 0, 5.5,  1, 2, 0, 0, 'Worker - East Bunker',          5),
(8, 28569, -35, 27, 0, 0.78, 1, 2, 0, 0, 'Worker - West Bunker',         5),
(8, 28569, 2, 45, 0, 0,     1, 2, 0, 0, 'Worker - Command Tower',        5);

-- ACTIVE: Main assault vanguard - arrive at staging point, march into camp
-- Stormwind Royal Guard x4 (entry 1756, level 80, elite)
INSERT INTO `ec_world_event_creatures` (`event_id`, `creature_entry`, `offset_x`, `offset_y`, `offset_z`, `orientation`, `spawn_phase`, `despawn_phase`, `is_boss`, `is_leader`, `name`, `patrol_radius`, `spawn_delay_seconds`, `arrival_x`, `arrival_y`, `arrival_z`, `arrival_method`, `reinforcement_wave`)
VALUES
(8, 1756,  -5,   5, 0, 4.71, 2, 0, 0, 0, 'Stormwind Royal Guard', 10, 0, -12395.1, 131.9, 3.45, 0, 0),
(8, 1756,   5,   5, 0, 4.71, 2, 0, 0, 0, 'Stormwind Royal Guard', 10, 0, -12393.0, 133.0, 3.45, 0, 0),
(8, 1756, -10,  10, 0, 4.71, 2, 0, 0, 0, 'Stormwind Royal Guard', 10, 0, -12397.0, 130.0, 3.45, 0, 0),
(8, 1756,  10,  10, 0, 4.71, 2, 0, 0, 0, 'Stormwind Royal Guard', 10, 0, -12394.0, 132.5, 3.45, 0, 0);

-- Stormwind City Guard x4 (entry 68, level 75) - support troops
INSERT INTO `ec_world_event_creatures` (`event_id`, `creature_entry`, `offset_x`, `offset_y`, `offset_z`, `orientation`, `spawn_phase`, `despawn_phase`, `is_boss`, `is_leader`, `name`, `patrol_radius`, `spawn_delay_seconds`, `arrival_x`, `arrival_y`, `arrival_z`, `arrival_method`, `reinforcement_wave`)
VALUES
(8, 68, -15,  15, 0, 4.71, 2, 0, 0, 0, 'Stormwind City Guard', 12, 0, -12394.0, 129.0, 3.45, 0, 0),
(8, 68,  15,  15, 0, 4.71, 2, 0, 0, 0, 'Stormwind City Guard', 12, 0, -12396.0, 134.0, 3.45, 0, 0),
(8, 68,  -8,  20, 0, 4.71, 2, 0, 0, 0, 'Stormwind City Guard', 12, 0, -12394.5, 132.0, 3.45, 0, 0),
(8, 68,   8,  20, 0, 4.71, 2, 0, 0, 0, 'Stormwind City Guard', 12, 0, -12395.5, 131.0, 3.45, 0, 0);

-- Alliance Battleguard x2 (entry 2279, level 72) - flanking units
INSERT INTO `ec_world_event_creatures` (`event_id`, `creature_entry`, `offset_x`, `offset_y`, `offset_z`, `orientation`, `spawn_phase`, `despawn_phase`, `is_boss`, `is_leader`, `name`, `patrol_radius`, `spawn_delay_seconds`, `arrival_x`, `arrival_y`, `arrival_z`, `arrival_method`, `reinforcement_wave`)
VALUES
(8, 2279, -20,   8, 0, 4.71, 2, 0, 0, 0, 'Alliance Battleguard', 10, 0, -12393.0, 128.0, 3.45, 0, 0),
(8, 2279,  20,   8, 0, 4.71, 2, 0, 0, 0, 'Alliance Battleguard', 10, 0, -12397.0, 135.0, 3.45, 0, 0);

-- WAVE 1 (60s): Alliance Commander + Royal Guard escort march from staging point
INSERT INTO `ec_world_event_creatures` (`event_id`, `creature_entry`, `offset_x`, `offset_y`, `offset_z`, `orientation`, `spawn_phase`, `despawn_phase`, `is_boss`, `is_leader`, `name`, `patrol_radius`, `spawn_delay_seconds`, `arrival_x`, `arrival_y`, `arrival_z`, `arrival_method`, `reinforcement_wave`)
VALUES
(8, 33225,  0,   0, 0, 4.71, 2, 0, 0, 1, 'Marshal Jacob Alerius',  15, 60, -12395.1, 131.9, 3.45, 0, 1),
(8, 1756,   3,   2, 0, 4.71, 2, 0, 0, 0, 'Stormwind Royal Guard',  10, 60, -12394.0, 130.0, 3.45, 0, 1),
(8, 1756,  -3,   2, 0, 4.71, 2, 0, 0, 0, 'Stormwind Royal Guard',  10, 60, -12396.0, 133.0, 3.45, 0, 1),
(8, 1756,   3,  -2, 0, 4.71, 2, 0, 0, 0, 'Stormwind Royal Guard',  10, 60, -12394.5, 131.0, 3.45, 0, 1),
(8, 1756,  -3,  -2, 0, 4.71, 2, 0, 0, 0, 'Stormwind Royal Guard',  10, 60, -12396.5, 132.0, 3.45, 0, 1);

-- WAVE 2 (180s): Heavy reinforcements - Elite Guards + 7th Legion Infantry
INSERT INTO `ec_world_event_creatures` (`event_id`, `creature_entry`, `offset_x`, `offset_y`, `offset_z`, `orientation`, `spawn_phase`, `despawn_phase`, `is_boss`, `is_leader`, `name`, `patrol_radius`, `spawn_delay_seconds`, `arrival_x`, `arrival_y`, `arrival_z`, `arrival_method`, `reinforcement_wave`)
VALUES
(8, 16396,  -5,  12, 0, 4.71, 2, 0, 0, 0, 'Stormwind Elite Guard', 10, 180, -12395.1, 131.9, 3.45, 0, 2),
(8, 16396,   5,  12, 0, 4.71, 2, 0, 0, 0, 'Stormwind Elite Guard', 10, 180, -12394.0, 130.0, 3.45, 0, 2),
(8, 16396,  -5,  18, 0, 4.71, 2, 0, 0, 0, 'Stormwind Elite Guard', 10, 180, -12396.0, 133.0, 3.45, 0, 2),
(8, 16396,   5,  18, 0, 4.71, 2, 0, 0, 0, 'Stormwind Elite Guard', 10, 180, -12395.5, 131.5, 3.45, 0, 2),
(8, 34919, -12,  22, 0, 4.71, 2, 0, 0, 0, '7th Legion Infantry',   12, 180, -12393.0, 129.0, 3.45, 0, 2),
(8, 34919,  12,  22, 0, 4.71, 2, 0, 0, 0, '7th Legion Infantry',   12, 180, -12397.0, 134.0, 3.45, 0, 2);

-- =============================================================================
-- OBJECTS AND CREATURES - Splintertree Raid (event_id = 10, Alliance attacking)
-- =============================================================================

-- PREPARING PHASE: Night elf forces prepare in the forest
INSERT INTO `ec_world_event_objects` (`event_id`, `gameobject_entry`, `offset_x`, `offset_y`, `offset_z`, `orientation`, `spawn_phase`, `despawn_phase`, `name`)
VALUES
(10, 900080, 0, 0, 0, 0, 1, 0, 'Alliance Battle Standard'),
(10, 900080, 10, 8, 0, 0.78, 1, 0, 'Alliance War Banner'),
(10, 900101, 5, 0, 0, 0, 1, 0, 'Forest Campfire');

-- ACTIVE PHASE: Forest defensive positions with towers
INSERT INTO `ec_world_event_objects` (`event_id`, `gameobject_entry`, `offset_x`, `offset_y`, `offset_z`, `orientation`, `spawn_phase`, `despawn_phase`, `name`)
VALUES
(10, 900105, 25, 0, 0, 1.57, 2, 0, 'Barricade (east road)'),
(10, 900105, -25, 0, 0, 1.57, 2, 0, 'Barricade (west road)'),
(10, 900102, 30, 25, 0, 5.5, 2, 0, 'Sentinel Tower (east)'),
(10, 900102, -30, 25, 0, 0.78, 2, 0, 'Sentinel Tower (west)'),
(10, 900106, 0, 35, 0, 0, 2, 0, 'Large Barricade (post entrance)');

-- Alliance Elite Guards (Sentinel theme for Ashenvale)
INSERT INTO `ec_world_event_creatures` (`event_id`, `creature_entry`, `offset_x`, `offset_y`, `offset_z`, `orientation`, `spawn_phase`, `despawn_phase`, `is_boss`, `is_leader`, `name`)
VALUES
(10, 12480, 0, 3, 0, 0, 1, 0, 0, 1, 'Sentinel Commander'),
(10, 12481, 5, 0, 0, 5.5, 1, 0, 0, 0, 'Sentinel Warrior'),
(10, 12481, -5, 0, 0, 0.78, 1, 0, 0, 0, 'Sentinel Warrior'),
(10, 12481, 30, 25, 0, 5.5, 2, 0, 0, 0, 'Tower Archer'),
(10, 12481, -30, 25, 0, 0.78, 2, 0, 0, 0, 'Tower Archer');

-- PREPARING: Construction workers building fortifications (despawn when ACTIVE begins)
INSERT INTO `ec_world_event_creatures` (`event_id`, `creature_entry`, `offset_x`, `offset_y`, `offset_z`, `orientation`, `spawn_phase`, `despawn_phase`, `is_boss`, `is_leader`, `name`, `patrol_radius`)
VALUES
(10, 28569, 25, 2, 0, 1.57,  1, 2, 0, 0, 'Worker - East Barricade',   5),
(10, 28569, -25, 2, 0, 4.71, 1, 2, 0, 0, 'Worker - West Barricade',   5),
(10, 28569, 30, 27, 0, 5.5,  1, 2, 0, 0, 'Worker - East Tower',       5),
(10, 28569, -30, 27, 0, 0.78, 1, 2, 0, 0, 'Worker - West Tower',      5);

-- =============================================================================
-- MAKE TOWERS AND BUNKERS CAPTURABLE (AV-style burning)
-- =============================================================================

-- Guard Towers (900102) - 4 minute burn time, 50 points
UPDATE `ec_world_event_objects` SET
    `is_capturable` = 1,
    `capture_time_seconds` = 240,
    `points_on_capture` = 50,
    `burning_entry` = 900108,
    `destroyed_entry` = 900109
WHERE `gameobject_entry` = 900102;

-- Alliance Bunkers (900103) - 4 minute burn time, 75 points (larger structure)
UPDATE `ec_world_event_objects` SET
    `is_capturable` = 1,
    `capture_time_seconds` = 240,
    `points_on_capture` = 75,
    `burning_entry` = 900110,
    `destroyed_entry` = 900111
WHERE `gameobject_entry` = 900103;

-- Horde Towers (900104) - 4 minute burn time, 50 points
UPDATE `ec_world_event_objects` SET
    `is_capturable` = 1,
    `capture_time_seconds` = 240,
    `points_on_capture` = 50,
    `burning_entry` = 900112,
    `destroyed_entry` = 900113
WHERE `gameobject_entry` = 900104;

-- Large Barricades (900106) - 2 minute destroy time, 25 points (smaller fortification)
UPDATE `ec_world_event_objects` SET
    `is_capturable` = 1,
    `capture_time_seconds` = 120,
    `points_on_capture` = 25,
    `destroyed_entry` = 900114
WHERE `gameobject_entry` = 900106;

-- =============================================================================
-- FIX: Preparation banners should despawn when ACTIVE phase ends
-- so they get replaced by victory/defeat banners during ENDING phase.
-- despawn_phase=2 means "remove when leaving ACTIVE"
-- =============================================================================
UPDATE `ec_world_event_objects`
SET `despawn_phase` = 2
WHERE `spawn_phase` = 1
  AND `despawn_phase` = 0
  AND `is_capturable` = 0;

-- =============================================================================
-- ENDING PHASE: Victory/defeat banners (conditional on event outcome)
-- spawn_condition: 0=always, 1=attacker_win only, 2=defender_win only
-- =============================================================================

-- Events 1-5: Horde attacks Alliance
-- attacker=Horde(900081), defender=Alliance(900080)

-- Event 1: Goldshire Raid
INSERT INTO `ec_world_event_objects` (`event_id`, `gameobject_entry`, `offset_x`, `offset_y`, `offset_z`, `orientation`, `spawn_phase`, `despawn_phase`, `name`, `spawn_condition`)
VALUES
(1, 900081, 0, 0, 0, 0,    3, 0, 'Horde Victory Banner',    1),
(1, 900081, 10, 5, 0, 0.78, 3, 0, 'Horde Victory Banner',   1),
(1, 900081, -10, 5, 0, 5.5, 3, 0, 'Horde Victory Banner',   1),
(1, 900080, 0, 0, 0, 0,    3, 0, 'Alliance Stands Banner',  2),
(1, 900080, 10, 5, 0, 0.78, 3, 0, 'Alliance Stands Banner', 2);

-- Event 2: Sentinel Hill Raid
INSERT INTO `ec_world_event_objects` (`event_id`, `gameobject_entry`, `offset_x`, `offset_y`, `offset_z`, `orientation`, `spawn_phase`, `despawn_phase`, `name`, `spawn_condition`)
VALUES
(2, 900081, 0, 0, 0, 0,    3, 0, 'Horde Victory Banner',    1),
(2, 900081, 10, 5, 0, 0.78, 3, 0, 'Horde Victory Banner',   1),
(2, 900080, 0, 0, 0, 0,    3, 0, 'Alliance Stands Banner',  2),
(2, 900080, 10, 5, 0, 0.78, 3, 0, 'Alliance Stands Banner', 2);

-- Event 3: Darkshire Raid
INSERT INTO `ec_world_event_objects` (`event_id`, `gameobject_entry`, `offset_x`, `offset_y`, `offset_z`, `orientation`, `spawn_phase`, `despawn_phase`, `name`, `spawn_condition`)
VALUES
(3, 900081, 0, 0, 0, 0,    3, 0, 'Horde Victory Banner',    1),
(3, 900081, 12, 8, 0, 0.78, 3, 0, 'Horde Victory Banner',   1),
(3, 900080, 0, 0, 0, 0,    3, 0, 'Alliance Stands Banner',  2),
(3, 900080, 12, 8, 0, 0.78, 3, 0, 'Alliance Stands Banner', 2);

-- Event 4: Astranaar Raid
INSERT INTO `ec_world_event_objects` (`event_id`, `gameobject_entry`, `offset_x`, `offset_y`, `offset_z`, `orientation`, `spawn_phase`, `despawn_phase`, `name`, `spawn_condition`)
VALUES
(4, 900081, 0, 0, 0, 0,    3, 0, 'Horde Victory Banner',    1),
(4, 900081, 10, 5, 0, 0.78, 3, 0, 'Horde Victory Banner',   1),
(4, 900080, 0, 0, 0, 0,    3, 0, 'Alliance Stands Banner',  2),
(4, 900080, 10, 5, 0, 0.78, 3, 0, 'Alliance Stands Banner', 2);

-- Event 5: Auberdine Raid
INSERT INTO `ec_world_event_objects` (`event_id`, `gameobject_entry`, `offset_x`, `offset_y`, `offset_z`, `orientation`, `spawn_phase`, `despawn_phase`, `name`, `spawn_condition`)
VALUES
(5, 900081, 0, 0, 0, 0,    3, 0, 'Horde Victory Banner',    1),
(5, 900081, 10, 5, 0, 0.78, 3, 0, 'Horde Victory Banner',   1),
(5, 900080, 0, 0, 0, 0,    3, 0, 'Alliance Stands Banner',  2),
(5, 900080, 10, 5, 0, 0.78, 3, 0, 'Alliance Stands Banner', 2);

-- Events 6-10: Alliance attacks Horde
-- attacker=Alliance(900080), defender=Horde(900081)

-- Event 6: Crossroads Raid
INSERT INTO `ec_world_event_objects` (`event_id`, `gameobject_entry`, `offset_x`, `offset_y`, `offset_z`, `orientation`, `spawn_phase`, `despawn_phase`, `name`, `spawn_condition`)
VALUES
(6, 900080, 0, 0, 0, 0,    3, 0, 'Alliance Victory Banner', 1),
(6, 900080, 10, 5, 0, 0.78, 3, 0, 'Alliance Victory Banner', 1),
(6, 900081, 0, 0, 0, 0,    3, 0, 'Horde Stands Banner',     2),
(6, 900081, 10, 5, 0, 0.78, 3, 0, 'Horde Stands Banner',    2);

-- Event 7: Camp Taurajo Raid
INSERT INTO `ec_world_event_objects` (`event_id`, `gameobject_entry`, `offset_x`, `offset_y`, `offset_z`, `orientation`, `spawn_phase`, `despawn_phase`, `name`, `spawn_condition`)
VALUES
(7, 900080, 0, 0, 0, 0,    3, 0, 'Alliance Victory Banner', 1),
(7, 900080, 10, 5, 0, 0.78, 3, 0, 'Alliance Victory Banner', 1),
(7, 900081, 0, 0, 0, 0,    3, 0, 'Horde Stands Banner',     2),
(7, 900081, 10, 5, 0, 0.78, 3, 0, 'Horde Stands Banner',    2);

-- Event 8: Grom'gol Raid
INSERT INTO `ec_world_event_objects` (`event_id`, `gameobject_entry`, `offset_x`, `offset_y`, `offset_z`, `orientation`, `spawn_phase`, `despawn_phase`, `name`, `spawn_condition`)
VALUES
(8, 900080, 0, 0, 0, 0,    3, 0, 'Alliance Victory Banner', 1),
(8, 900080, 12, 0, 0, 1.57, 3, 0, 'Alliance Victory Banner', 1),
(8, 900080, -12, 0, 0, 4.71, 3, 0, 'Alliance Victory Banner', 1),
(8, 900081, 0, 0, 0, 0,    3, 0, 'Horde Stands Banner',     2),
(8, 900081, 12, 0, 0, 1.57, 3, 0, 'Horde Stands Banner',    2),
(8, 900081, -12, 0, 0, 4.71, 3, 0, 'Horde Stands Banner',   2);

-- Event 9: Tarren Mill Raid
INSERT INTO `ec_world_event_objects` (`event_id`, `gameobject_entry`, `offset_x`, `offset_y`, `offset_z`, `orientation`, `spawn_phase`, `despawn_phase`, `name`, `spawn_condition`)
VALUES
(9, 900080, 0, 0, 0, 0,    3, 0, 'Alliance Victory Banner', 1),
(9, 900080, 10, 5, 0, 0.78, 3, 0, 'Alliance Victory Banner', 1),
(9, 900081, 0, 0, 0, 0,    3, 0, 'Horde Stands Banner',     2),
(9, 900081, 10, 5, 0, 0.78, 3, 0, 'Horde Stands Banner',    2);

-- Event 10: Splintertree Raid
INSERT INTO `ec_world_event_objects` (`event_id`, `gameobject_entry`, `offset_x`, `offset_y`, `offset_z`, `orientation`, `spawn_phase`, `despawn_phase`, `name`, `spawn_condition`)
VALUES
(10, 900080, 0, 0, 0, 0,    3, 0, 'Alliance Victory Banner', 1),
(10, 900080, 10, 5, 0, 0.78, 3, 0, 'Alliance Victory Banner', 1),
(10, 900081, 0, 0, 0, 0,    3, 0, 'Horde Stands Banner',     2),
(10, 900081, 10, 5, 0, 0.78, 3, 0, 'Horde Stands Banner',    2);

-- =============================================================================
-- NOTES ON EXTENDING THIS DATA
-- =============================================================================
-- 1. Find valid gameobject_entry values:
--    SELECT entry, name FROM gameobject_template WHERE name LIKE '%barricade%';
--    SELECT entry, name FROM gameobject_template WHERE name LIKE '%banner%';
--    SELECT entry, name FROM gameobject_template WHERE name LIKE '%tower%';
--
-- 2. Find valid creature_entry values:
--    SELECT entry, name FROM creature_template WHERE name LIKE '%elite%' AND entry > 10000;
--    SELECT entry, name FROM creature_template WHERE name LIKE '%kor''kron%';
--    SELECT entry, name FROM creature_template WHERE name LIKE '%7th legion%';
--
-- 3. Phase values:
--    1 = PREPARING (rally phase, banners, leaders appear)
--    2 = ACTIVE (combat phase, barricades, reinforcements)
--    3 = ENDING (victory/defeat phase, outcome banners)
--
-- 4. despawn_phase = 0 means object stays until event ends completely
--
-- 5. spawn_condition (for ENDING phase victory/defeat objects):
--    0 = always spawn (default for all phases)
--    1 = spawn only if attacker won
--    2 = spawn only if defender won
--
-- 5. Offsets are relative to the event's location in ec_world_events:
--    offset_x = east/west from event center
--    offset_y = north/south from event center
--    offset_z = height adjustment (usually 0)
--
-- 6. Orientation in radians: 0=north, 1.57=east, 3.14=south, 4.71=west
--
-- 7. Building guards (AV-style):
--    Set assigned_building_id to a building's id from ec_world_event_objects
--    Set patrol_radius for the guard's patrol area around the building
--    Guards will spawn at the building location and attack anyone who tries to capture it

-- =============================================================================
-- CAPTURABLE TOWERS AND THEIR GUARDS - Goldshire Raid (event_id = 1)
-- =============================================================================

-- Insert capturable watch towers for Goldshire defense
INSERT INTO `ec_world_event_objects` (`event_id`, `gameobject_entry`, `offset_x`, `offset_y`, `offset_z`, `orientation`, `spawn_phase`, `despawn_phase`, `name`, `is_capturable`, `capture_time_seconds`, `points_on_capture`, `burning_entry`, `destroyed_entry`)
VALUES
(1, 900102, 40, 30, 0, 0, 1, 0, 'North Watch Tower', 1, 240, 50, 900108, 900109),
(1, 900102, 40, -30, 0, 0, 1, 0, 'South Watch Tower', 1, 240, 50, 900108, 900109),
(1, 900103, -50, 0, 0, 3.14, 1, 0, 'Goldshire Bunker', 1, 240, 75, 900110, 900111);

-- Insert tower guards with explicit building assignment using subqueries
-- These guards will spawn at the tower, patrol around it, and attack anyone who tries to capture it
INSERT INTO `ec_world_event_creatures` (`event_id`, `creature_entry`, `offset_x`, `offset_y`, `offset_z`, `orientation`, `spawn_phase`, `despawn_phase`, `is_boss`, `is_leader`, `name`, `assigned_building_id`, `patrol_radius`)
SELECT 1, 14304, 40, 30, 0, 0, 1, 0, 0, 0, 'North Tower Guard 1',
       (SELECT id FROM `ec_world_event_objects` WHERE event_id = 1 AND name = 'North Watch Tower' LIMIT 1), 8
UNION ALL
SELECT 1, 14304, 40, 30, 0, 1.57, 1, 0, 0, 0, 'North Tower Guard 2',
       (SELECT id FROM `ec_world_event_objects` WHERE event_id = 1 AND name = 'North Watch Tower' LIMIT 1), 8
UNION ALL
SELECT 1, 14304, 40, -30, 0, 0, 1, 0, 0, 0, 'South Tower Guard 1',
       (SELECT id FROM `ec_world_event_objects` WHERE event_id = 1 AND name = 'South Watch Tower' LIMIT 1), 8
UNION ALL
SELECT 1, 14304, 40, -30, 0, 4.71, 1, 0, 0, 0, 'South Tower Guard 2',
       (SELECT id FROM `ec_world_event_objects` WHERE event_id = 1 AND name = 'South Watch Tower' LIMIT 1), 8
UNION ALL
SELECT 1, 14304, -50, 0, 0, 0, 1, 0, 0, 0, 'Bunker Guard 1',
       (SELECT id FROM `ec_world_event_objects` WHERE event_id = 1 AND name = 'Goldshire Bunker' LIMIT 1), 12
UNION ALL
SELECT 1, 14304, -50, 0, 0, 1.57, 1, 0, 0, 0, 'Bunker Guard 2',
       (SELECT id FROM `ec_world_event_objects` WHERE event_id = 1 AND name = 'Goldshire Bunker' LIMIT 1), 12
UNION ALL
SELECT 1, 14304, -50, 0, 0, 3.14, 1, 0, 0, 0, 'Bunker Guard 3',
       (SELECT id FROM `ec_world_event_objects` WHERE event_id = 1 AND name = 'Goldshire Bunker' LIMIT 1), 12
UNION ALL
SELECT 1, 14304, -50, 0, 0, 4.71, 1, 0, 0, 0, 'Bunker Guard 4',
       (SELECT id FROM `ec_world_event_objects` WHERE event_id = 1 AND name = 'Goldshire Bunker' LIMIT 1), 12;

-- =============================================================================
-- CAPTURABLE TOWERS AND THEIR GUARDS - Tarren Mill Raid (event_id = 9)
-- =============================================================================

-- Insert capturable watch towers for Tarren Mill defense
INSERT INTO `ec_world_event_objects` (`event_id`, `gameobject_entry`, `offset_x`, `offset_y`, `offset_z`, `orientation`, `spawn_phase`, `despawn_phase`, `name`, `is_capturable`, `capture_time_seconds`, `points_on_capture`, `burning_entry`, `destroyed_entry`)
VALUES
(9, 900104, 35, 25, 0, 0, 1, 0, 'Horde Tower North', 1, 240, 50, 900112, 900113),
(9, 900104, 35, -25, 0, 0, 1, 0, 'Horde Tower South', 1, 240, 50, 900112, 900113);

-- Insert tower guards for Tarren Mill
INSERT INTO `ec_world_event_creatures` (`event_id`, `creature_entry`, `offset_x`, `offset_y`, `offset_z`, `orientation`, `spawn_phase`, `despawn_phase`, `is_boss`, `is_leader`, `name`, `assigned_building_id`, `patrol_radius`)
SELECT 9, 12481, 35, 25, 0, 0, 1, 0, 0, 0, 'Tower Guard North 1',
       (SELECT id FROM `ec_world_event_objects` WHERE event_id = 9 AND name = 'Horde Tower North' LIMIT 1), 8
UNION ALL
SELECT 9, 12481, 35, 25, 0, 3.14, 1, 0, 0, 0, 'Tower Guard North 2',
       (SELECT id FROM `ec_world_event_objects` WHERE event_id = 9 AND name = 'Horde Tower North' LIMIT 1), 8
UNION ALL
SELECT 9, 12481, 35, -25, 0, 0, 1, 0, 0, 0, 'Tower Guard South 1',
       (SELECT id FROM `ec_world_event_objects` WHERE event_id = 9 AND name = 'Horde Tower South' LIMIT 1), 8
UNION ALL
SELECT 9, 12481, 35, -25, 0, 3.14, 1, 0, 0, 0, 'Tower Guard South 2',
       (SELECT id FROM `ec_world_event_objects` WHERE event_id = 9 AND name = 'Horde Tower South' LIMIT 1), 8;

-- =============================================================================
-- CAPTURABLE BUILDING GUARDS - Sentinel Hill Raid (event_id = 2)
-- =============================================================================
INSERT INTO `ec_world_event_creatures` (`event_id`, `creature_entry`, `offset_x`, `offset_y`, `offset_z`, `orientation`, `spawn_phase`, `despawn_phase`, `is_boss`, `is_leader`, `name`, `assigned_building_id`, `patrol_radius`)
SELECT 2, 14304, 0, 35, 0, 0, 1, 0, 0, 0, 'Tower Guard 1',
       (SELECT id FROM `ec_world_event_objects` WHERE event_id = 2 AND name = 'Horde Tower (north overlook)' LIMIT 1), 8
UNION ALL
SELECT 2, 14304, 0, 35, 0, 3.14, 1, 0, 0, 0, 'Tower Guard 2',
       (SELECT id FROM `ec_world_event_objects` WHERE event_id = 2 AND name = 'Horde Tower (north overlook)' LIMIT 1), 8;

-- =============================================================================
-- CAPTURABLE BUILDING GUARDS - Darkshire Raid (event_id = 3)
-- =============================================================================
INSERT INTO `ec_world_event_creatures` (`event_id`, `creature_entry`, `offset_x`, `offset_y`, `offset_z`, `orientation`, `spawn_phase`, `despawn_phase`, `is_boss`, `is_leader`, `name`, `assigned_building_id`, `patrol_radius`)
SELECT 3, 14304, 30, 20, 0, 5.5, 1, 0, 0, 0, 'East Tower Guard 1',
       (SELECT id FROM `ec_world_event_objects` WHERE event_id = 3 AND name = 'Horde Tower (east hill)' LIMIT 1), 8
UNION ALL
SELECT 3, 14304, 30, 20, 0, 2.36, 1, 0, 0, 0, 'East Tower Guard 2',
       (SELECT id FROM `ec_world_event_objects` WHERE event_id = 3 AND name = 'Horde Tower (east hill)' LIMIT 1), 8
UNION ALL
SELECT 3, 14304, -30, 20, 0, 0.78, 1, 0, 0, 0, 'West Tower Guard 1',
       (SELECT id FROM `ec_world_event_objects` WHERE event_id = 3 AND name = 'Horde Tower (west hill)' LIMIT 1), 8
UNION ALL
SELECT 3, 14304, -30, 20, 0, 3.93, 1, 0, 0, 0, 'West Tower Guard 2',
       (SELECT id FROM `ec_world_event_objects` WHERE event_id = 3 AND name = 'Horde Tower (west hill)' LIMIT 1), 8;

-- =============================================================================
-- CAPTURABLE BUILDING GUARDS - Astranaar Raid (event_id = 4)
-- =============================================================================
INSERT INTO `ec_world_event_creatures` (`event_id`, `creature_entry`, `offset_x`, `offset_y`, `offset_z`, `orientation`, `spawn_phase`, `despawn_phase`, `is_boss`, `is_leader`, `name`, `assigned_building_id`, `patrol_radius`)
SELECT 4, 14304, 25, 25, 0, 5.5, 1, 0, 0, 0, 'NE Tower Guard 1',
       (SELECT id FROM `ec_world_event_objects` WHERE event_id = 4 AND name = 'Horde Tower (northeast)' LIMIT 1), 8
UNION ALL
SELECT 4, 14304, 25, 25, 0, 2.36, 1, 0, 0, 0, 'NE Tower Guard 2',
       (SELECT id FROM `ec_world_event_objects` WHERE event_id = 4 AND name = 'Horde Tower (northeast)' LIMIT 1), 8
UNION ALL
SELECT 4, 14304, -25, 25, 0, 0.78, 1, 0, 0, 0, 'NW Tower Guard 1',
       (SELECT id FROM `ec_world_event_objects` WHERE event_id = 4 AND name = 'Horde Tower (northwest)' LIMIT 1), 8
UNION ALL
SELECT 4, 14304, -25, 25, 0, 3.93, 1, 0, 0, 0, 'NW Tower Guard 2',
       (SELECT id FROM `ec_world_event_objects` WHERE event_id = 4 AND name = 'Horde Tower (northwest)' LIMIT 1), 8;

-- =============================================================================
-- CAPTURABLE BUILDING GUARDS - Auberdine Raid (event_id = 5)
-- =============================================================================
INSERT INTO `ec_world_event_creatures` (`event_id`, `creature_entry`, `offset_x`, `offset_y`, `offset_z`, `orientation`, `spawn_phase`, `despawn_phase`, `is_boss`, `is_leader`, `name`, `assigned_building_id`, `patrol_radius`)
SELECT 5, 14304, 30, 15, 0, 5.5, 1, 0, 0, 0, 'Harbor Tower Guard 1',
       (SELECT id FROM `ec_world_event_objects` WHERE event_id = 5 AND name = 'Horde Tower (harbor overlook)' LIMIT 1), 8
UNION ALL
SELECT 5, 14304, 30, 15, 0, 2.36, 1, 0, 0, 0, 'Harbor Tower Guard 2',
       (SELECT id FROM `ec_world_event_objects` WHERE event_id = 5 AND name = 'Horde Tower (harbor overlook)' LIMIT 1), 8;

-- =============================================================================
-- CAPTURABLE BUILDING GUARDS - Camp Taurajo Raid (event_id = 7)
-- =============================================================================
INSERT INTO `ec_world_event_creatures` (`event_id`, `creature_entry`, `offset_x`, `offset_y`, `offset_z`, `orientation`, `spawn_phase`, `despawn_phase`, `is_boss`, `is_leader`, `name`, `assigned_building_id`, `patrol_radius`)
SELECT 7, 12481, 30, 20, 0, 5.5, 2, 0, 0, 0, 'Bunker Guard 1',
       (SELECT id FROM `ec_world_event_objects` WHERE event_id = 7 AND name = 'Alliance Bunker (northeast)' LIMIT 1), 12
UNION ALL
SELECT 7, 12481, 30, 20, 0, 2.36, 2, 0, 0, 0, 'Bunker Guard 2',
       (SELECT id FROM `ec_world_event_objects` WHERE event_id = 7 AND name = 'Alliance Bunker (northeast)' LIMIT 1), 12
UNION ALL
SELECT 7, 12481, 30, 20, 0, 0.78, 2, 0, 0, 0, 'Bunker Guard 3',
       (SELECT id FROM `ec_world_event_objects` WHERE event_id = 7 AND name = 'Alliance Bunker (northeast)' LIMIT 1), 12;

-- =============================================================================
-- CAPTURABLE BUILDING GUARDS - Grom'gol Raid (event_id = 8)
-- =============================================================================
INSERT INTO `ec_world_event_creatures` (`event_id`, `creature_entry`, `offset_x`, `offset_y`, `offset_z`, `orientation`, `spawn_phase`, `despawn_phase`, `is_boss`, `is_leader`, `name`, `assigned_building_id`, `patrol_radius`)
SELECT 8, 12481, 35, 25, 0, 5.5, 2, 0, 0, 0, 'East Bunker Guard 1',
       (SELECT id FROM `ec_world_event_objects` WHERE event_id = 8 AND name = 'Alliance Bunker (port overlook)' LIMIT 1), 12
UNION ALL
SELECT 8, 12481, 35, 25, 0, 2.36, 2, 0, 0, 0, 'East Bunker Guard 2',
       (SELECT id FROM `ec_world_event_objects` WHERE event_id = 8 AND name = 'Alliance Bunker (port overlook)' LIMIT 1), 12
UNION ALL
SELECT 8, 12481, 0, 45, 0, 0, 2, 0, 0, 0, 'Command Tower Guard 1',
       (SELECT id FROM `ec_world_event_objects` WHERE event_id = 8 AND name = 'Guard Tower (command post)' LIMIT 1), 8
UNION ALL
SELECT 8, 12481, 0, 45, 0, 3.14, 2, 0, 0, 0, 'Command Tower Guard 2',
       (SELECT id FROM `ec_world_event_objects` WHERE event_id = 8 AND name = 'Guard Tower (command post)' LIMIT 1), 8;

-- =============================================================================
-- CAPTURABLE BUILDING GUARDS - Splintertree Raid (event_id = 10)
-- =============================================================================
INSERT INTO `ec_world_event_creatures` (`event_id`, `creature_entry`, `offset_x`, `offset_y`, `offset_z`, `orientation`, `spawn_phase`, `despawn_phase`, `is_boss`, `is_leader`, `name`, `assigned_building_id`, `patrol_radius`)
SELECT 10, 12481, 30, 25, 0, 5.5, 2, 0, 0, 0, 'East Tower Guard 1',
       (SELECT id FROM `ec_world_event_objects` WHERE event_id = 10 AND name = 'Sentinel Tower (east)' LIMIT 1), 8
UNION ALL
SELECT 10, 12481, 30, 25, 0, 2.36, 2, 0, 0, 0, 'East Tower Guard 2',
       (SELECT id FROM `ec_world_event_objects` WHERE event_id = 10 AND name = 'Sentinel Tower (east)' LIMIT 1), 8
UNION ALL
SELECT 10, 12481, -30, 25, 0, 0.78, 2, 0, 0, 0, 'West Tower Guard 1',
       (SELECT id FROM `ec_world_event_objects` WHERE event_id = 10 AND name = 'Sentinel Tower (west)' LIMIT 1), 8
UNION ALL
SELECT 10, 12481, -30, 25, 0, 3.93, 2, 0, 0, 0, 'West Tower Guard 2',
       (SELECT id FROM `ec_world_event_objects` WHERE event_id = 10 AND name = 'Sentinel Tower (west)' LIMIT 1), 8;

-- =============================================================================
-- ZEPPELIN REINFORCEMENTS - Grom'gol Raid (event_id = 8, Horde defending)
-- =============================================================================
-- Grom'gol zeppelin tower coordinates: approximately (-12369, 173, 36)
-- Grom'gol base camp center: (-12417, 146, 3)
-- Zeppelin arrives from Orgrimmar/Undercity bringing Thrall and his guards!

-- Wave 1: First batch of guards arrive by zeppelin (60 seconds after ACTIVE phase)
-- They spawn at the zeppelin tower and run down to defend the base
INSERT INTO `ec_world_event_creatures` (`event_id`, `creature_entry`, `offset_x`, `offset_y`, `offset_z`, `orientation`, `spawn_phase`, `despawn_phase`, `is_boss`, `is_leader`, `name`, `patrol_radius`, `spawn_delay_seconds`, `arrival_x`, `arrival_y`, `arrival_z`, `arrival_method`, `reinforcement_wave`)
VALUES
(8, 14304, 0, 0, 0, 0, 2, 0, 0, 0, 'Kor''kron Grunt (Zeppelin)', 15, 60, -12369, 173, 36, 1, 1),
(8, 14304, 0, 0, 0, 0, 2, 0, 0, 0, 'Kor''kron Grunt (Zeppelin)', 15, 60, -12369, 173, 36, 1, 1),
(8, 14304, 0, 0, 0, 0, 2, 0, 0, 0, 'Kor''kron Grunt (Zeppelin)', 15, 60, -12369, 173, 36, 1, 1),
(8, 14304, 0, 0, 0, 0, 2, 0, 0, 0, 'Kor''kron Grunt (Zeppelin)', 15, 60, -12369, 173, 36, 1, 1);

-- Wave 2: THRALL arrives with his elite guard! (180 seconds = 3 minutes)
-- The Warchief himself comes to defend the outpost!
INSERT INTO `ec_world_event_creatures` (`event_id`, `creature_entry`, `offset_x`, `offset_y`, `offset_z`, `orientation`, `spawn_phase`, `despawn_phase`, `is_boss`, `is_leader`, `name`, `patrol_radius`, `spawn_delay_seconds`, `arrival_x`, `arrival_y`, `arrival_z`, `arrival_method`, `reinforcement_wave`)
VALUES
-- Thrall (entry 4949 is generic Thrall, or use 14720 for Orgrimmar Thrall)
(8, 4949, 0, 5, 0, 0, 2, 0, 0, 1, 'Warchief Thrall', 20, 180, -12369, 173, 36, 1, 2),
-- Thrall's personal guard (Kor'kron Elite)
(8, 14304, 3, 2, 0, 0, 2, 0, 0, 0, 'Kor''kron Honor Guard', 10, 180, -12369, 173, 36, 1, 2),
(8, 14304, -3, 2, 0, 0, 2, 0, 0, 0, 'Kor''kron Honor Guard', 10, 180, -12369, 173, 36, 1, 2),
(8, 14304, 3, -2, 0, 0, 2, 0, 0, 0, 'Kor''kron Honor Guard', 10, 180, -12369, 173, 36, 1, 2),
(8, 14304, -3, -2, 0, 0, 2, 0, 0, 0, 'Kor''kron Honor Guard', 10, 180, -12369, 173, 36, 1, 2);

-- Wave 3: More reinforcements (300 seconds = 5 minutes) - if the battle continues
INSERT INTO `ec_world_event_creatures` (`event_id`, `creature_entry`, `offset_x`, `offset_y`, `offset_z`, `orientation`, `spawn_phase`, `despawn_phase`, `is_boss`, `is_leader`, `name`, `patrol_radius`, `spawn_delay_seconds`, `arrival_x`, `arrival_y`, `arrival_z`, `arrival_method`, `reinforcement_wave`)
VALUES
(8, 14304, 10, 10, 0, 0, 2, 0, 0, 0, 'Kor''kron Reinforcement', 15, 300, -12369, 173, 36, 1, 3),
(8, 14304, -10, 10, 0, 0, 2, 0, 0, 0, 'Kor''kron Reinforcement', 15, 300, -12369, 173, 36, 1, 3),
(8, 14304, 10, -10, 0, 0, 2, 0, 0, 0, 'Kor''kron Reinforcement', 15, 300, -12369, 173, 36, 1, 3),
(8, 14304, -10, -10, 0, 0, 2, 0, 0, 0, 'Kor''kron Reinforcement', 15, 300, -12369, 173, 36, 1, 3);

-- =============================================================================
-- LOCAL DEFENDERS - Grom'gol Raid (event_id = 8, Horde defending)
-- =============================================================================
-- These grunts spawn immediately at the innkeeper/camp area to defend
-- They don't arrive by zeppelin - they're already stationed at the base
-- Grom'gol innkeeper area: approximately (-12430, 176, 2.5)

INSERT INTO `ec_world_event_creatures` (`event_id`, `creature_entry`, `offset_x`, `offset_y`, `offset_z`, `orientation`, `spawn_phase`, `despawn_phase`, `is_boss`, `is_leader`, `name`, `patrol_radius`, `spawn_delay_seconds`, `arrival_x`, `arrival_y`, `arrival_z`, `arrival_method`, `reinforcement_wave`)
VALUES
-- Immediate defenders - stationed grunts around the inn/campfire
(8, 14304, -13, 30, -1, 0, 2, 0, 0, 0, 'Grom''gol Grunt', 15, 0, NULL, NULL, NULL, 0, 0),
(8, 14304, -20, 25, -1, 0.78, 2, 0, 0, 0, 'Grom''gol Grunt', 15, 0, NULL, NULL, NULL, 0, 0),
(8, 14304, -6, 35, -1, 5.5, 2, 0, 0, 0, 'Grom''gol Grunt', 15, 0, NULL, NULL, NULL, 0, 0),
(8, 14304, -25, 35, -1, 0, 2, 0, 0, 0, 'Grom''gol Grunt', 15, 0, NULL, NULL, NULL, 0, 0),
-- Dock guards
(8, 14304, 15, -10, 0, 1.57, 2, 0, 0, 0, 'Dock Guard', 10, 0, NULL, NULL, NULL, 0, 0),
(8, 14304, -15, -15, 0, 4.71, 2, 0, 0, 0, 'Dock Guard', 10, 0, NULL, NULL, NULL, 0, 0),
-- Tower base guards
(8, 14304, 48, 30, 0, 3.14, 2, 0, 0, 0, 'Tower Watchman', 8, 0, NULL, NULL, NULL, 0, 0),
(8, 14304, 52, 25, 0, 3.14, 2, 0, 0, 0, 'Tower Watchman', 8, 0, NULL, NULL, NULL, 0, 0);

-- =============================================================================
-- FLIGHT PATH REINFORCEMENTS - Crossroads Raid (event_id = 6, Horde defending)
-- =============================================================================
-- Horde reinforcements arrive by wyvern/wind rider from Orgrimmar!
-- Crossroads flight master: approximately (-462, -2659, 96)
-- Arrival method 4 = FLIGHT_PATH (wyvern for Horde, gryphon for Alliance)

-- Wave 1: First wyvern riders arrive (60 seconds)
INSERT INTO `ec_world_event_creatures` (`event_id`, `creature_entry`, `offset_x`, `offset_y`, `offset_z`, `orientation`, `spawn_phase`, `despawn_phase`, `is_boss`, `is_leader`, `name`, `patrol_radius`, `spawn_delay_seconds`, `arrival_x`, `arrival_y`, `arrival_z`, `arrival_method`, `reinforcement_wave`)
VALUES
(6, 14304, 5, -21, 0, 0, 2, 0, 0, 0, 'Orgrimmar Grunt (Wyvern)', 15, 60, -462, -2659, 96, 4, 1),
(6, 14304, -5, -21, 0, 0, 2, 0, 0, 0, 'Orgrimmar Grunt (Wyvern)', 15, 60, -462, -2659, 96, 4, 1),
(6, 14304, 10, -21, 0, 0, 2, 0, 0, 0, 'Orgrimmar Grunt (Wyvern)', 15, 65, -462, -2659, 96, 4, 1),
(6, 14304, -10, -21, 0, 0, 2, 0, 0, 0, 'Orgrimmar Grunt (Wyvern)', 15, 65, -462, -2659, 96, 4, 1);

-- Wave 2: Elite Kor'kron warriors arrive (120 seconds = 2 minutes)
INSERT INTO `ec_world_event_creatures` (`event_id`, `creature_entry`, `offset_x`, `offset_y`, `offset_z`, `orientation`, `spawn_phase`, `despawn_phase`, `is_boss`, `is_leader`, `name`, `patrol_radius`, `spawn_delay_seconds`, `arrival_x`, `arrival_y`, `arrival_z`, `arrival_method`, `reinforcement_wave`)
VALUES
(6, 14304, 0, -25, 0, 0, 2, 0, 0, 0, 'Kor''kron Elite (Wyvern)', 15, 120, -462, -2659, 96, 4, 2),
(6, 14304, 5, -25, 0, 0, 2, 0, 0, 0, 'Kor''kron Elite (Wyvern)', 15, 120, -462, -2659, 96, 4, 2),
(6, 14304, -5, -25, 0, 0, 2, 0, 0, 0, 'Kor''kron Elite (Wyvern)', 15, 125, -462, -2659, 96, 4, 2);

-- Wave 3: Barrens defenders rally (180 seconds = 3 minutes)
-- Local troops from nearby outposts march in
INSERT INTO `ec_world_event_creatures` (`event_id`, `creature_entry`, `offset_x`, `offset_y`, `offset_z`, `orientation`, `spawn_phase`, `despawn_phase`, `is_boss`, `is_leader`, `name`, `patrol_radius`, `spawn_delay_seconds`, `arrival_x`, `arrival_y`, `arrival_z`, `arrival_method`, `reinforcement_wave`)
VALUES
-- Marching from south road (Camp Taurajo direction)
(6, 14304, 0, -60, 0, 0, 2, 0, 0, 0, 'Barrens Defender', 15, 180, -467, -2550, 96, 0, 3),
(6, 14304, 5, -60, 0, 0, 2, 0, 0, 0, 'Barrens Defender', 15, 180, -467, -2550, 96, 0, 3),
(6, 14304, -5, -60, 0, 0, 2, 0, 0, 0, 'Barrens Defender', 15, 185, -467, -2550, 96, 0, 3),
(6, 14304, 10, -60, 0, 0, 2, 0, 0, 0, 'Barrens Defender', 15, 185, -467, -2550, 96, 0, 3);

-- Local Crossroads defenders (immediate spawn - already there)
INSERT INTO `ec_world_event_creatures` (`event_id`, `creature_entry`, `offset_x`, `offset_y`, `offset_z`, `orientation`, `spawn_phase`, `despawn_phase`, `is_boss`, `is_leader`, `name`, `patrol_radius`, `spawn_delay_seconds`, `arrival_x`, `arrival_y`, `arrival_z`, `arrival_method`, `reinforcement_wave`)
VALUES
(6, 14304, 10, 5, 0, 0, 2, 0, 0, 0, 'Crossroads Guard', 12, 0, NULL, NULL, NULL, 0, 0),
(6, 14304, -10, 5, 0, 0, 2, 0, 0, 0, 'Crossroads Guard', 12, 0, NULL, NULL, NULL, 0, 0),
(6, 14304, 15, -5, 0, 1.57, 2, 0, 0, 0, 'Crossroads Guard', 12, 0, NULL, NULL, NULL, 0, 0),
(6, 14304, -15, -5, 0, 4.71, 2, 0, 0, 0, 'Crossroads Guard', 12, 0, NULL, NULL, NULL, 0, 0),
(6, 14304, 5, 15, 0, 0, 2, 0, 0, 0, 'Crossroads Grunt', 10, 0, NULL, NULL, NULL, 0, 0),
(6, 14304, -5, 15, 0, 0, 2, 0, 0, 0, 'Crossroads Grunt', 10, 0, NULL, NULL, NULL, 0, 0);

-- =============================================================================
-- BARRACKS GUARD RESPAWN - Towers and Bunkers produce guards while INTACT
-- =============================================================================
-- SC2-style: INTACT buildings periodically spawn guards up to a cap.
-- BURNING or DESTROYED buildings stop producing.
-- guard_respawn_seconds: interval between guard spawns (0=disabled)
-- max_guards: max alive guards this building can have simultaneously

-- Guard Towers (900102): respawn a guard every 60s, up to 2
UPDATE `ec_world_event_objects` SET
    `guard_respawn_seconds` = 60,
    `max_guards` = 2
WHERE `gameobject_entry` = 900102 AND `is_capturable` = 1;

-- Alliance Bunkers (900103): respawn every 45s, up to 4 (larger structure)
UPDATE `ec_world_event_objects` SET
    `guard_respawn_seconds` = 45,
    `max_guards` = 4
WHERE `gameobject_entry` = 900103 AND `is_capturable` = 1;

-- Horde Towers (900104): respawn every 60s, up to 2
UPDATE `ec_world_event_objects` SET
    `guard_respawn_seconds` = 60,
    `max_guards` = 2
WHERE `gameobject_entry` = 900104 AND `is_capturable` = 1;

-- Note: Large Barricades (900106) are capturable but NOT barracks.
-- They don't produce guards - they're defensive fortifications only.

-- =============================================================================
-- BOAT REINFORCEMENTS - Example for coastal events
-- =============================================================================
-- Booty Bay has docks - Alliance could arrive by boat for certain events
-- (This is just an example - add boat arrivals as needed for coastal locations)

-- Preparation workers leave when PREPARING ends. Older rows incorrectly used
-- despawn_phase=2, which kept peons/workers around throughout the live battle.
UPDATE `ec_world_event_creatures`
SET `despawn_phase` = 1
WHERE `creature_entry` IN (11656, 28569)
  AND `spawn_phase` = 1
  AND `despawn_phase` = 2;
