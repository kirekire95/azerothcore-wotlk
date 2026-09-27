-- ==========================================================================
-- EC World Events - Defense Event Definitions
-- ==========================================================================
-- These events are triggered by the Guard System when a zone falls.
-- Elite faction forces respond to reclaim fallen territories.
--
-- Event Types: 0=Raid, 1=Defense, 2=WorldBoss, 3=Gathering, 4=PvPZone
-- Faction: 0=Neutral, 1=Alliance, 2=Horde
-- ==========================================================================

-- ==========================================================================
-- FACTION-WIDE DEFAULTS (zone_id = 0 means "any zone")
-- Used as fallback when no zone-specific defense event exists
-- ==========================================================================

INSERT INTO `ec_world_events`
    (`id`, `name`, `description`, `type`, `attacker_faction`, `defender_faction`,
     `map_id`, `pos_x`, `pos_y`, `pos_z`, `orientation`, `zone_id`, `area_id`, `location_name`,
     `min_bots`, `max_bots`, `duration_minutes`, `min_level`, `max_level`, `enabled`)
VALUES
    -- Horde faction-wide response (Kor'kron Elite)
    (101, 'Kor''kron Elite Response',
     'Thrall''s elite Kor''kron guard has been dispatched to reclaim the fallen territory. The Horde will not tolerate this affront!',
     1, 0, 2,  -- type=Defense, attacker=Neutral (defenders attack criminals), defender=Horde
     1, 0, 0, 0, 0, 0, 0, 'Horde Territory',  -- Generic location, coords unused for faction-wide
     20, 60, 30, 1, 80, 1),

    -- Alliance faction-wide response (Stormwind Royal Guard)
    (102, 'Stormwind Royal Guard Response',
     'The King has dispatched his Royal Guard to restore order. The Alliance will defend its people!',
     1, 0, 1,  -- type=Defense, attacker=Neutral, defender=Alliance
     0, 0, 0, 0, 0, 0, 0, 'Alliance Territory',
     20, 60, 30, 1, 80, 1);

-- ==========================================================================
-- ZONE-SPECIFIC DEFENSE EVENTS
-- Higher priority than faction-wide defaults
-- ==========================================================================

-- ----- HORDE ZONES -----

INSERT INTO `ec_world_events`
    (`id`, `name`, `description`, `type`, `attacker_faction`, `defender_faction`,
     `map_id`, `pos_x`, `pos_y`, `pos_z`, `orientation`, `zone_id`, `area_id`, `location_name`,
     `min_bots`, `max_bots`, `duration_minutes`, `min_level`, `max_level`, `enabled`)
VALUES
    -- Crossroads (Barrens) - Iconic Horde hub
    (111, 'Crossroads Elite Defense',
     'The Crossroads has fallen! Thrall''s Kor''kron Elite march from Orgrimmar. Blood and thunder!',
     1, 0, 2,
     1, -467, -2638, 95.77, 0,  -- Crossroads coords
     17, 380, 'The Crossroads',  -- zone_id=17 (Barrens), area_id=380
     25, 80, 45, 10, 80, 1),

    -- Tarren Mill (Hillsbrad) - Classic PvP hotspot
    (112, 'Tarren Mill Elite Defense',
     'Tarren Mill burns! The Forsaken have called upon their Horde allies. The Dark Lady demands vengeance!',
     1, 0, 2,
     0, -21, -920, 54.65, 0,  -- Tarren Mill coords
     267, 272, 'Tarren Mill',  -- zone_id=267 (Hillsbrad Foothills), area_id=272
     30, 100, 45, 20, 80, 1);

-- ----- ALLIANCE ZONES -----

INSERT INTO `ec_world_events`
    (`id`, `name`, `description`, `type`, `attacker_faction`, `defender_faction`,
     `map_id`, `pos_x`, `pos_y`, `pos_z`, `orientation`, `zone_id`, `area_id`, `location_name`,
     `min_bots`, `max_bots`, `duration_minutes`, `min_level`, `max_level`, `enabled`)
VALUES
    -- Goldshire (Elwynn Forest) - Starter zone hub
    (121, 'Goldshire Elite Defense',
     'Goldshire is under siege! Stormwind''s finest have been dispatched. For the Alliance!',
     1, 0, 1,
     0, -9459, 62, 56.32, 0,  -- Goldshire coords
     12, 87, 'Goldshire',  -- zone_id=12 (Elwynn Forest), area_id=87
     20, 60, 30, 5, 80, 1),

    -- Astranaar (Ashenvale) - Night Elf territory
    (122, 'Astranaar Elite Defense',
     'Astranaar has fallen to savages! The Sentinels rally with reinforcements from Darnassus!',
     1, 0, 1,
     1, 2753, -348, 107.17, 0,  -- Astranaar coords
     331, 415, 'Astranaar',  -- zone_id=331 (Ashenvale), area_id=415
     30, 90, 45, 20, 80, 1);

-- ==========================================================================
-- DEFENSE EVENT OBJECTS AND CREATURES
-- ==========================================================================
-- Faction-wide events (101, 102) have no fixed coords, so they rely on
-- the Guard System to provide spawn location at runtime. No static
-- objects/creatures needed — bots are spawned dynamically by Playerbots.

-- ==========================================================================
-- CROSSROADS ELITE DEFENSE (event_id = 111, Horde defending)
-- ==========================================================================

-- Defensive fortifications around the Crossroads
INSERT INTO `ec_world_event_objects` (`event_id`, `gameobject_entry`, `offset_x`, `offset_y`, `offset_z`, `orientation`, `spawn_phase`, `despawn_phase`, `name`)
VALUES
(111, 900081, 0, 0, 0, 0, 1, 0, 'Horde War Banner (defense rally)'),
(111, 900081, 10, 5, 0, 0.78, 1, 0, 'Horde War Banner (east)'),
(111, 900100, 5, 0, 0, 0, 1, 0, 'War Bonfire');

INSERT INTO `ec_world_event_objects` (`event_id`, `gameobject_entry`, `offset_x`, `offset_y`, `offset_z`, `orientation`, `spawn_phase`, `despawn_phase`, `name`)
VALUES
(111, 900105, 30, 0, 0, 1.57, 2, 0, 'Barricade (east road)'),
(111, 900105, -30, 0, 0, 1.57, 2, 0, 'Barricade (west road)'),
(111, 900105, 0, 30, 0, 0, 2, 0, 'Barricade (north road)'),
(111, 900105, 0, -30, 0, 0, 2, 0, 'Barricade (south road)'),
(111, 900104, 35, 25, 0, 5.5, 2, 0, 'Horde Tower (northeast)'),
(111, 900104, -35, 25, 0, 0.78, 2, 0, 'Horde Tower (northwest)');

-- Kor'kron defenders
INSERT INTO `ec_world_event_creatures` (`event_id`, `creature_entry`, `offset_x`, `offset_y`, `offset_z`, `orientation`, `spawn_phase`, `despawn_phase`, `is_boss`, `is_leader`, `name`)
VALUES
(111, 14304, 0, 3, 0, 0, 1, 0, 0, 1, 'Kor''kron Warlord'),
(111, 14304, 5, 0, 0, 5.5, 1, 0, 0, 0, 'Kor''kron Elite'),
(111, 14304, -5, 0, 0, 0.78, 1, 0, 0, 0, 'Kor''kron Elite');

-- Peons reinforcing defenses
INSERT INTO `ec_world_event_creatures` (`event_id`, `creature_entry`, `offset_x`, `offset_y`, `offset_z`, `orientation`, `spawn_phase`, `despawn_phase`, `is_boss`, `is_leader`, `name`, `patrol_radius`)
VALUES
(111, 11656, 30, 2, 0, 1.57,  1, 2, 0, 0, 'Peon - East Barricade',  5),
(111, 11656, -30, 2, 0, 4.71, 1, 2, 0, 0, 'Peon - West Barricade',  5),
(111, 11656, 2, 30, 0, 0,     1, 2, 0, 0, 'Peon - North Barricade', 5);

-- ==========================================================================
-- TARREN MILL ELITE DEFENSE (event_id = 112, Horde defending)
-- ==========================================================================

INSERT INTO `ec_world_event_objects` (`event_id`, `gameobject_entry`, `offset_x`, `offset_y`, `offset_z`, `orientation`, `spawn_phase`, `despawn_phase`, `name`)
VALUES
(112, 900081, 0, 0, 0, 0, 1, 0, 'Horde War Banner (defense rally)'),
(112, 900100, 5, 0, 0, 0, 1, 0, 'War Bonfire');

INSERT INTO `ec_world_event_objects` (`event_id`, `gameobject_entry`, `offset_x`, `offset_y`, `offset_z`, `orientation`, `spawn_phase`, `despawn_phase`, `name`)
VALUES
(112, 900105, 25, 0, 0, 1.57, 2, 0, 'Barricade (east)'),
(112, 900105, -25, 0, 0, 1.57, 2, 0, 'Barricade (west)'),
(112, 900104, 30, 20, 0, 5.5, 2, 0, 'Horde Tower (northeast)'),
(112, 900106, 0, 25, 0, 0, 2, 0, 'Large Barricade (north)');

INSERT INTO `ec_world_event_creatures` (`event_id`, `creature_entry`, `offset_x`, `offset_y`, `offset_z`, `orientation`, `spawn_phase`, `despawn_phase`, `is_boss`, `is_leader`, `name`)
VALUES
(112, 14304, 0, 3, 0, 0, 1, 0, 0, 1, 'Kor''kron Battlemaster'),
(112, 14304, 5, 0, 0, 5.5, 1, 0, 0, 0, 'Kor''kron Elite'),
(112, 14304, -5, 0, 0, 0.78, 1, 0, 0, 0, 'Kor''kron Elite');

INSERT INTO `ec_world_event_creatures` (`event_id`, `creature_entry`, `offset_x`, `offset_y`, `offset_z`, `orientation`, `spawn_phase`, `despawn_phase`, `is_boss`, `is_leader`, `name`, `patrol_radius`)
VALUES
(112, 11656, 25, 2, 0, 1.57,  1, 2, 0, 0, 'Peon - East Barricade',  5),
(112, 11656, -25, 2, 0, 4.71, 1, 2, 0, 0, 'Peon - West Barricade',  5);

-- ==========================================================================
-- GOLDSHIRE ELITE DEFENSE (event_id = 121, Alliance defending)
-- ==========================================================================

INSERT INTO `ec_world_event_objects` (`event_id`, `gameobject_entry`, `offset_x`, `offset_y`, `offset_z`, `orientation`, `spawn_phase`, `despawn_phase`, `name`)
VALUES
(121, 900080, 0, 0, 0, 0, 1, 0, 'Alliance War Banner (defense rally)'),
(121, 900080, 10, 5, 0, 0.78, 1, 0, 'Alliance War Banner (east)'),
(121, 900101, 5, 0, 0, 0, 1, 0, 'Command Campfire');

INSERT INTO `ec_world_event_objects` (`event_id`, `gameobject_entry`, `offset_x`, `offset_y`, `offset_z`, `orientation`, `spawn_phase`, `despawn_phase`, `name`)
VALUES
(121, 900105, 25, 0, 0, 1.57, 2, 0, 'Barricade (east road)'),
(121, 900105, -25, 0, 0, 1.57, 2, 0, 'Barricade (west road)'),
(121, 900105, 0, 25, 0, 0, 2, 0, 'Barricade (north road)'),
(121, 900102, 30, 20, 0, 5.5, 2, 0, 'Guard Tower (northeast)'),
(121, 900103, -40, 0, 0, 3.14, 2, 0, 'Alliance Bunker (west)');

-- Stormwind defenders
INSERT INTO `ec_world_event_creatures` (`event_id`, `creature_entry`, `offset_x`, `offset_y`, `offset_z`, `orientation`, `spawn_phase`, `despawn_phase`, `is_boss`, `is_leader`, `name`)
VALUES
(121, 12480, 0, 3, 0, 0, 1, 0, 0, 1, 'Stormwind Field Marshal'),
(121, 12481, 5, 0, 0, 5.5, 1, 0, 0, 0, 'Stormwind Elite Guard'),
(121, 12481, -5, 0, 0, 0.78, 1, 0, 0, 0, 'Stormwind Elite Guard');

INSERT INTO `ec_world_event_creatures` (`event_id`, `creature_entry`, `offset_x`, `offset_y`, `offset_z`, `orientation`, `spawn_phase`, `despawn_phase`, `is_boss`, `is_leader`, `name`, `patrol_radius`)
VALUES
(121, 28569, 25, 2, 0, 1.57,  1, 2, 0, 0, 'Worker - East Barricade',  5),
(121, 28569, -25, 2, 0, 4.71, 1, 2, 0, 0, 'Worker - West Barricade',  5),
(121, 28569, 2, 25, 0, 0,     1, 2, 0, 0, 'Worker - North Barricade', 5);

-- ==========================================================================
-- ASTRANAAR ELITE DEFENSE (event_id = 122, Alliance defending)
-- ==========================================================================

INSERT INTO `ec_world_event_objects` (`event_id`, `gameobject_entry`, `offset_x`, `offset_y`, `offset_z`, `orientation`, `spawn_phase`, `despawn_phase`, `name`)
VALUES
(122, 900080, 0, 0, 0, 0, 1, 0, 'Alliance Battle Standard (defense)'),
(122, 900101, 5, 0, 0, 0, 1, 0, 'Command Campfire');

INSERT INTO `ec_world_event_objects` (`event_id`, `gameobject_entry`, `offset_x`, `offset_y`, `offset_z`, `orientation`, `spawn_phase`, `despawn_phase`, `name`)
VALUES
(122, 900105, 30, 0, 0, 1.57, 2, 0, 'Barricade (east road)'),
(122, 900105, -30, 0, 0, 1.57, 2, 0, 'Barricade (west road)'),
(122, 900102, 25, 25, 0, 5.5, 2, 0, 'Sentinel Tower (northeast)'),
(122, 900102, -25, 25, 0, 0.78, 2, 0, 'Sentinel Tower (northwest)'),
(122, 900106, 0, 35, 0, 0, 2, 0, 'Large Barricade (north approach)');

-- Sentinel defenders
INSERT INTO `ec_world_event_creatures` (`event_id`, `creature_entry`, `offset_x`, `offset_y`, `offset_z`, `orientation`, `spawn_phase`, `despawn_phase`, `is_boss`, `is_leader`, `name`)
VALUES
(122, 12480, 0, 3, 0, 0, 1, 0, 0, 1, 'Sentinel Commander'),
(122, 12481, 5, 0, 0, 5.5, 1, 0, 0, 0, 'Sentinel Warrior'),
(122, 12481, -5, 0, 0, 0.78, 1, 0, 0, 0, 'Sentinel Warrior');

INSERT INTO `ec_world_event_creatures` (`event_id`, `creature_entry`, `offset_x`, `offset_y`, `offset_z`, `orientation`, `spawn_phase`, `despawn_phase`, `is_boss`, `is_leader`, `name`, `patrol_radius`)
VALUES
(122, 28569, 30, 2, 0, 1.57,  1, 2, 0, 0, 'Worker - East Barricade',  5),
(122, 28569, -30, 2, 0, 4.71, 1, 2, 0, 0, 'Worker - West Barricade',  5),
(122, 28569, 25, 27, 0, 5.5,  1, 2, 0, 0, 'Worker - NE Tower',        5),
(122, 28569, -25, 27, 0, 0.78, 1, 2, 0, 0, 'Worker - NW Tower',       5);

-- ==========================================================================
-- ROLLBACK: Remove defense events
-- ==========================================================================
/*
DELETE FROM `ec_world_events` WHERE `id` IN (101, 102, 111, 112, 121, 122);
*/

-- Defense preparation workers also leave before ACTIVE combat begins.
UPDATE `ec_world_event_creatures`
SET `despawn_phase` = 1
WHERE `creature_entry` IN (11656, 28569)
  AND `spawn_phase` = 1
  AND `despawn_phase` = 2;
