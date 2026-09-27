-- ==========================================================================
-- EC Guard System - Zone Guard Configuration
-- ==========================================================================
-- This table defines which guards spawn in each zone when players commit crimes.
-- ==========================================================================

DROP TABLE IF EXISTS `ec_guard_system_zone_config`;

CREATE TABLE `ec_guard_system_zone_config` (
    `zone_id` INT UNSIGNED NOT NULL COMMENT 'Zone ID from AreaTable.dbc',
    `area_id` INT UNSIGNED NOT NULL DEFAULT 0 COMMENT 'Exact settlement AreaTable.dbc ID; 0 means the whole zone',
    `faction` TINYINT UNSIGNED NOT NULL COMMENT '0=Neutral, 1=Alliance, 2=Horde',
    `guard_entry_1` INT UNSIGNED NOT NULL DEFAULT 0 COMMENT 'Primary guard creature entry',
    `guard_entry_2` INT UNSIGNED DEFAULT 0 COMMENT 'Secondary guard creature entry',
    `elite_entry_1` INT UNSIGNED DEFAULT 0 COMMENT 'Elite guard for 3+ stars',
    `captain_entry` INT UNSIGNED DEFAULT 0 COMMENT 'Captain for 5 stars',
    `search_radius` FLOAT NOT NULL DEFAULT 80.0 COMMENT 'Radius to search for idle guards',
    PRIMARY KEY (`zone_id`, `faction`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT='EC Guard System - Zone guard configurations';

-- ==========================================================================
-- ALLIANCE CITIES
-- ==========================================================================

-- Stormwind City (1519)
-- Guards: Stormwind City Guard (68), Stormwind Royal Guard (1756)
INSERT INTO `ec_guard_system_zone_config` VALUES (1519, 0, 1, 68, 0, 1756, 1756, 80.0);

-- Ironforge (1537)
-- Guards: Ironforge Guard (5595). Entry 5596 is Twain Test Prop.
INSERT INTO `ec_guard_system_zone_config` VALUES (1537, 0, 1, 5595, 0, 5595, 5595, 80.0);

-- Darnassus (1657)
-- Guards: Darnassus Sentinel (4262)
INSERT INTO `ec_guard_system_zone_config` VALUES (1657, 0, 1, 4262, 0, 4262, 4262, 80.0);

-- Exodar (3557)
-- Guards: Exodar Peacekeeper (16733)
INSERT INTO `ec_guard_system_zone_config` VALUES (3557, 0, 1, 16733, 0, 16733, 16733, 80.0);

-- ==========================================================================
-- HORDE CITIES
-- ==========================================================================

-- Orgrimmar (1637)
-- Guards: Orgrimmar Grunt (3296), Kor'kron Elite (14304).
-- Entry 19910 is Gargok, a Warsong Gulch battlemaster.
INSERT INTO `ec_guard_system_zone_config` VALUES (1637, 0, 2, 3296, 0, 14304, 14304, 80.0);

-- Undercity (1497)
-- Guards: Deathguard Elite (7980). Entry 12427 is Mountaineer Dolf.
INSERT INTO `ec_guard_system_zone_config` VALUES (1497, 0, 2, 7980, 0, 7980, 7980, 80.0);

-- Thunder Bluff (1638)
-- Guards: Bluffwatcher (3084)
INSERT INTO `ec_guard_system_zone_config` VALUES (1638, 0, 2, 3084, 0, 3084, 3084, 80.0);

-- Silvermoon City (3487)
-- Guards: Silvermoon City Guardian (16222)
INSERT INTO `ec_guard_system_zone_config` VALUES (3487, 0, 2, 16222, 0, 16222, 16222, 80.0);

-- ==========================================================================
-- NEUTRAL CITIES
-- ==========================================================================

-- Shattrath City (3703)
-- Guards: Shattrath City Peacekeeper (19687).
-- Entries 19679 and 18649 are "Slim" and Innkeeper Monica.
INSERT INTO `ec_guard_system_zone_config` VALUES (3703, 0, 0, 19687, 0, 19687, 19687, 80.0);

-- Dalaran (4395)
-- Guards: Violet Hold Guard (30659), with native spawns in Dalaran.
-- Entry 32674 is absent and 32675 is Babagahnoosh the Grumpy.
INSERT INTO `ec_guard_system_zone_config` VALUES (4395, 0, 0, 30659, 0, 30659, 30659, 80.0);

-- Booty Bay (area 35 inside Stranglethorn Vale zone 33)
-- Guards: Booty Bay Bruiser (4624)
INSERT INTO `ec_guard_system_zone_config` VALUES (33, 35, 0, 4624, 0, 4624, 4624, 80.0);

-- Gadgetzan (440)
-- Guards: Gadgetzan Bruiser (9460)
INSERT INTO `ec_guard_system_zone_config` VALUES (440, 976, 0, 9460, 0, 9460, 9460, 80.0);

-- Everlook (618)
-- Guards: Everlook Bruiser (11190)
INSERT INTO `ec_guard_system_zone_config` VALUES (618, 2255, 0, 11190, 0, 11190, 11190, 80.0);

-- Ratchet (17) - The Barrens - Neutral areas
-- Guards: Ratchet Bruiser (3502)
INSERT INTO `ec_guard_system_zone_config` VALUES (17, 392, 0, 3502, 0, 3502, 3502, 80.0);

-- ==========================================================================
-- OUTDOOR ZONES - HORDE TERRITORIES
-- ==========================================================================
-- Note: The Barrens (zone 17) is shared - Horde areas (Crossroads) use these guards
-- while neutral Ratchet uses the config above

-- The Barrens (17) - Horde settlements
-- Guards: Horde Guard (3501), the native Crossroads/Barrens guard template.
-- Do not use Orgrimmar Grunt (3296) or Razor Hill Grunt (5953) here.
INSERT INTO `ec_guard_system_zone_config` VALUES (17, 380, 2, 3501, 0, 3501, 3501, 100.0);

-- Durotar (14)
-- Guards: Razor Hill Grunt (5953), restricted to Razor Hill area 362.
INSERT INTO `ec_guard_system_zone_config` VALUES (14, 362, 2, 5953, 0, 5953, 5953, 80.0);

-- Bloodhoof Village / Mulgore is intentionally omitted. Bluffwatcher (3084)
-- is Thunder Bluff-local, and no generic summon-safe Bloodhoof guard exists.

-- Tirisfal Glades (85)
-- Guards: Deathguard (7980)
INSERT INTO `ec_guard_system_zone_config` VALUES (85, 159, 2, 7980, 0, 7980, 7980, 80.0);

-- Silverpine Forest (130)
-- Guards: Deathguard (7980)
INSERT INTO `ec_guard_system_zone_config` VALUES (130, 228, 2, 7980, 0, 7980, 7980, 100.0);

-- Eversong Woods (3430)
-- Guards: Silvermoon City Guardian (16222)
INSERT INTO `ec_guard_system_zone_config` VALUES (3430, 3462, 2, 16222, 0, 16222, 16222, 80.0);

-- Ghostlands (3433)
-- Guards: Silvermoon City Guardian (16222)
INSERT INTO `ec_guard_system_zone_config` VALUES (3433, 3488, 2, 16222, 0, 16222, 16222, 100.0);

-- ==========================================================================
-- OUTDOOR ZONES - ALLIANCE TERRITORIES
-- ==========================================================================

-- Elwynn Forest (12)
-- Guards: Stormwind Guard (1423), with native Goldshire/Elwynn spawns.
INSERT INTO `ec_guard_system_zone_config` VALUES (12, 87, 1, 1423, 0, 1423, 1423, 80.0);

-- Duskwood (10)
-- Guards: Night Watch Guard (10038), restricted to Darkshire area 42.
INSERT INTO `ec_guard_system_zone_config` VALUES (10, 42, 1, 10038, 0, 10038, 10038, 100.0);

-- Wetlands (11)
-- Guards: Menethil Sentry (1434), restricted to Menethil Harbor area 150.
INSERT INTO `ec_guard_system_zone_config` VALUES (11, 150, 1, 1434, 0, 1434, 1434, 100.0);

-- Westfall (40)
-- Guards: Protector of the People (8096), native to Sentinel Hill.
-- Entry 880 is Erlan Drudgemoor, a level-8 quest NPC.
INSERT INTO `ec_guard_system_zone_config` VALUES (40, 108, 1, 8096, 0, 8096, 8096, 100.0);

-- Redridge Mountains (44)
-- Guards: Lakeshire Guard (10037). Entry 382 is Marshal Marris.
INSERT INTO `ec_guard_system_zone_config` VALUES (44, 69, 1, 10037, 0, 10037, 10037, 100.0);

-- Dun Morogh (1)
-- Guards: Ironforge Mountaineer (727), native to Kharanos/Dun Morogh.
-- Entry 5596 is Twain Test Prop.
INSERT INTO `ec_guard_system_zone_config` VALUES (1, 131, 1, 727, 0, 727, 727, 80.0);

-- Loch Modan (38)
-- Guards: Thelsamar Mountaineer (8055). Entry 5596 is Twain Test Prop.
INSERT INTO `ec_guard_system_zone_config` VALUES (38, 144, 1, 8055, 0, 8055, 8055, 100.0);

-- Teldrassil (141)
-- Guards: Darnassus Sentinel (4262)
INSERT INTO `ec_guard_system_zone_config` VALUES (141, 186, 1, 4262, 0, 4262, 4262, 80.0);

-- Auberdine / Darkshore is intentionally omitted. Auberdine Sentinel (6086)
-- is a questgiver template, and Darnassus Sentinel (4262) is not local.

-- Azuremyst Isle (3524)
-- Guards: Exodar Peacekeeper (16733)
INSERT INTO `ec_guard_system_zone_config` VALUES (3524, 3576, 1, 16733, 0, 16733, 16733, 80.0);

-- Bloodmyst Isle (3525)
-- Guards: Exodar Peacekeeper (16733)
INSERT INTO `ec_guard_system_zone_config` VALUES (3525, 3584, 1, 16733, 0, 16733, 16733, 100.0);

-- ==========================================================================
-- CONTESTED ZONES (both factions have presence)
-- ==========================================================================

-- Ashenvale (331) - Alliance
-- Guards: Astranaar Sentinel (6087), restricted to Astranaar area 415.
-- Darnassus Sentinel (4262) belongs to Darnassus, not Ashenvale.
INSERT INTO `ec_guard_system_zone_config` VALUES (331, 415, 1, 6087, 0, 6087, 6087, 100.0);

-- Ashenvale (331) - Horde
-- Guards: Splintertree Guard (12903), restricted to Splintertree Post area 431.
INSERT INTO `ec_guard_system_zone_config` VALUES (331, 431, 2, 12903, 0, 12903, 12903, 100.0);

-- Hillsbrad Foothills (267) - Alliance (Southshore)
-- Guards: Southshore Guard (2386), restricted to Southshore area 271.
INSERT INTO `ec_guard_system_zone_config` VALUES (267, 271, 1, 2386, 0, 2386, 2386, 100.0);

-- Hillsbrad Foothills (267) - Horde (Tarren Mill)
-- Guards: Tarren Mill Deathguard (2405), restricted to Tarren Mill area 272.
INSERT INTO `ec_guard_system_zone_config` VALUES (267, 272, 2, 2405, 0, 2405, 2405, 100.0);

-- Stranglethorn Vale (33) - Horde
-- Grom'gol Grunt (1064), restricted to Grom'gol Base Camp area 117.
INSERT INTO `ec_guard_system_zone_config` VALUES (33, 117, 2, 1064, 0, 1064, 1064, 100.0);

-- ==========================================================================
-- ROLLBACK: Remove all zone configurations
-- ==========================================================================
/*
DROP TABLE IF EXISTS `ec_guard_system_zone_config`;
*/
