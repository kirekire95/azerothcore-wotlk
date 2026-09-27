-- ==========================================================================
-- EC Guard System - Zone Configuration Table
-- ==========================================================================
-- This table defines guard system configuration per zone, including
-- initial guard counts and zone threat tracking settings.
-- ==========================================================================

DROP TABLE IF EXISTS `ec_guard_zone_config`;

CREATE TABLE `ec_guard_zone_config` (
    `zone_id` INT UNSIGNED NOT NULL COMMENT 'Zone ID from AreaTable.dbc',
    `zone_name` VARCHAR(100) NOT NULL COMMENT 'Human-readable zone name',
    `faction` TINYINT UNSIGNED NOT NULL DEFAULT 0 COMMENT 'Owning faction: 0=Neutral, 1=Alliance, 2=Horde',
    `initial_guard_count` INT UNSIGNED NOT NULL DEFAULT 10 COMMENT 'Number of guards at full strength',
    `guard_entry_list` VARCHAR(255) DEFAULT NULL COMMENT 'Comma-separated creature entries for regular guards',
    `elite_entry_list` VARCHAR(255) DEFAULT NULL COMMENT 'Comma-separated creature entries for elite guards',
    `captain_entry` INT UNSIGNED DEFAULT 0 COMMENT 'Captain creature entry for max threat',
    `bounty_hunter_entry` INT UNSIGNED DEFAULT 0 COMMENT 'Bounty hunter creature entry',
    `search_radius` FLOAT NOT NULL DEFAULT 80.0 COMMENT 'Radius to search for idle guards',
    `heat_multiplier` FLOAT NOT NULL DEFAULT 1.0 COMMENT 'Zone-specific heat multiplier',
    `enabled` TINYINT UNSIGNED NOT NULL DEFAULT 1 COMMENT 'Whether guard system is active in this zone',
    PRIMARY KEY (`zone_id`),
    INDEX `idx_faction` (`faction`),
    INDEX `idx_enabled` (`enabled`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT='EC Guard System - Per-zone configuration';

-- ==========================================================================
-- ALLIANCE ZONES
-- ==========================================================================

-- Elwynn Forest (ID: 12)
INSERT INTO `ec_guard_zone_config` (`zone_id`, `zone_name`, `faction`, `initial_guard_count`, `guard_entry_list`, `elite_entry_list`, `captain_entry`)
VALUES (12, 'Elwynn Forest', 1, 15, '1423', '1423', 1423);

-- Stormwind City (ID: 1519)
INSERT INTO `ec_guard_zone_config` (`zone_id`, `zone_name`, `faction`, `initial_guard_count`, `guard_entry_list`, `elite_entry_list`, `captain_entry`)
VALUES (1519, 'Stormwind City', 1, 50, '68,1976', '1756', 1756);

-- Westfall (ID: 40)
INSERT INTO `ec_guard_zone_config` (`zone_id`, `zone_name`, `faction`, `initial_guard_count`, `guard_entry_list`, `elite_entry_list`, `captain_entry`)
VALUES (40, 'Westfall', 1, 10, '8096', '8096', 8096);

-- Redridge Mountains (ID: 44)
INSERT INTO `ec_guard_zone_config` (`zone_id`, `zone_name`, `faction`, `initial_guard_count`, `guard_entry_list`, `elite_entry_list`, `captain_entry`)
VALUES (44, 'Redridge Mountains', 1, 12, '10037', '10037', 10037);

-- Duskwood (ID: 10)
INSERT INTO `ec_guard_zone_config` (`zone_id`, `zone_name`, `faction`, `initial_guard_count`, `guard_entry_list`, `elite_entry_list`, `captain_entry`)
VALUES (10, 'Duskwood', 1, 8, '10038', '10038', 10038);

-- Ironforge (ID: 1537)
INSERT INTO `ec_guard_zone_config` (`zone_id`, `zone_name`, `faction`, `initial_guard_count`, `guard_entry_list`, `elite_entry_list`, `captain_entry`)
VALUES (1537, 'Ironforge', 1, 40, '5595', '5595', 5595);

-- Darnassus (ID: 1657)
INSERT INTO `ec_guard_zone_config` (`zone_id`, `zone_name`, `faction`, `initial_guard_count`, `guard_entry_list`, `elite_entry_list`, `captain_entry`)
VALUES (1657, 'Darnassus', 1, 35, '4262', '4262', 4262);

-- Exodar (ID: 3557)
INSERT INTO `ec_guard_zone_config` (`zone_id`, `zone_name`, `faction`, `initial_guard_count`, `guard_entry_list`, `elite_entry_list`, `captain_entry`)
VALUES (3557, 'Exodar', 1, 35, '16733', '16733', 16733);

-- Teldrassil (ID: 141)
INSERT INTO `ec_guard_zone_config` (`zone_id`, `zone_name`, `faction`, `initial_guard_count`, `guard_entry_list`, `elite_entry_list`, `captain_entry`)
VALUES (141, 'Teldrassil', 1, 12, '4262', '4262', 4262);

-- Darkshore is intentionally omitted. Auberdine Sentinel (6086) is a
-- questgiver template, and Darnassus Sentinel (4262) is not Auberdine-local.
-- This zone-only legacy table cannot safely represent a summon template here.

-- Loch Modan (ID: 38)
INSERT INTO `ec_guard_zone_config` (`zone_id`, `zone_name`, `faction`, `initial_guard_count`, `guard_entry_list`, `elite_entry_list`, `captain_entry`)
VALUES (38, 'Loch Modan', 1, 8, '8055', '8055', 8055);

-- Wetlands (ID: 11)
INSERT INTO `ec_guard_zone_config` (`zone_id`, `zone_name`, `faction`, `initial_guard_count`, `guard_entry_list`, `elite_entry_list`, `captain_entry`)
VALUES (11, 'Wetlands', 1, 10, '1434', '1434', 1434);

-- ==========================================================================
-- HORDE ZONES
-- ==========================================================================

-- Durotar (ID: 14)
INSERT INTO `ec_guard_zone_config` (`zone_id`, `zone_name`, `faction`, `initial_guard_count`, `guard_entry_list`, `elite_entry_list`, `captain_entry`)
VALUES (14, 'Durotar', 2, 15, '5953', '5953', 5953);

-- Orgrimmar (ID: 1637)
INSERT INTO `ec_guard_zone_config` (`zone_id`, `zone_name`, `faction`, `initial_guard_count`, `guard_entry_list`, `elite_entry_list`, `captain_entry`)
VALUES (1637, 'Orgrimmar', 2, 50, '3296', '14304', 14304);

-- The Crossroads (area 380 in The Barrens, zone ID: 17)
INSERT INTO `ec_guard_zone_config` (`zone_id`, `zone_name`, `faction`, `initial_guard_count`, `guard_entry_list`, `elite_entry_list`, `captain_entry`)
VALUES (17, 'The Crossroads', 2, 14, '3501', '3501', 3501);

-- Mulgore is intentionally omitted. Bluffwatcher (3084) is a Thunder Bluff
-- guard, and Bloodhoof Village has no generic, summon-safe local guard entry.

-- Thunder Bluff (ID: 1638)
INSERT INTO `ec_guard_zone_config` (`zone_id`, `zone_name`, `faction`, `initial_guard_count`, `guard_entry_list`, `elite_entry_list`, `captain_entry`)
VALUES (1638, 'Thunder Bluff', 2, 30, '3084', '3084', 3084);

-- Silvermoon City (ID: 3487)
INSERT INTO `ec_guard_zone_config` (`zone_id`, `zone_name`, `faction`, `initial_guard_count`, `guard_entry_list`, `elite_entry_list`, `captain_entry`)
VALUES (3487, 'Silvermoon City', 2, 40, '16222', '16222', 16222);

-- Tirisfal Glades (ID: 85)
INSERT INTO `ec_guard_zone_config` (`zone_id`, `zone_name`, `faction`, `initial_guard_count`, `guard_entry_list`, `elite_entry_list`, `captain_entry`)
VALUES (85, 'Tirisfal Glades', 2, 10, '7980', '7980', 7980);

-- Undercity (ID: 1497)
INSERT INTO `ec_guard_zone_config` (`zone_id`, `zone_name`, `faction`, `initial_guard_count`, `guard_entry_list`, `elite_entry_list`, `captain_entry`)
VALUES (1497, 'Undercity', 2, 40, '7980', '7980', 7980);

-- Silverpine Forest (ID: 130)
INSERT INTO `ec_guard_zone_config` (`zone_id`, `zone_name`, `faction`, `initial_guard_count`, `guard_entry_list`, `elite_entry_list`, `captain_entry`)
VALUES (130, 'Silverpine Forest', 2, 8, '7980', '7980', 7980);

-- Hillsbrad Foothills (ID: 267)
INSERT INTO `ec_guard_zone_config` (`zone_id`, `zone_name`, `faction`, `initial_guard_count`, `guard_entry_list`, `elite_entry_list`, `captain_entry`)
VALUES (267, 'Hillsbrad Foothills', 2, 10, '2405', '2405', 2405);

-- ==========================================================================
-- CONTESTED/NEUTRAL ZONES
-- ==========================================================================

-- Ashenvale (ID: 331) - Astranaar's Alliance garrison
INSERT INTO `ec_guard_zone_config` (`zone_id`, `zone_name`, `faction`, `initial_guard_count`, `guard_entry_list`, `elite_entry_list`, `captain_entry`)
VALUES (331, 'Ashenvale', 1, 11, '6087', '6087', 6087);

-- Stranglethorn Vale (ID: 33) - Neutral Booty Bay state, area-gated by the
-- matching ec_guard_system_zone_config row (area 35).
INSERT INTO `ec_guard_zone_config` (`zone_id`, `zone_name`, `faction`, `initial_guard_count`, `guard_entry_list`, `elite_entry_list`, `captain_entry`)
VALUES (33, 'Booty Bay', 0, 15, '4624', '4624', 4624);

-- Tanaris (ID: 440) - Neutral (Gadgetzan)
INSERT INTO `ec_guard_zone_config` (`zone_id`, `zone_name`, `faction`, `initial_guard_count`, `guard_entry_list`, `elite_entry_list`, `captain_entry`)
VALUES (440, 'Tanaris', 0, 12, '9460', '9460', 9460);

-- Winterspring (ID: 618) - Neutral (Everlook)
INSERT INTO `ec_guard_zone_config` (`zone_id`, `zone_name`, `faction`, `initial_guard_count`, `guard_entry_list`, `elite_entry_list`, `captain_entry`)
VALUES (618, 'Winterspring', 0, 10, '11190', '11190', 11190);

-- Ratchet / The Barrens neutral area
-- (Uses zone 17 already defined as Horde, neutral Ratchet is area within)

-- ==========================================================================
-- OUTLAND ZONES (TBC)
-- ==========================================================================

-- Shattrath City (ID: 3703)
INSERT INTO `ec_guard_zone_config` (`zone_id`, `zone_name`, `faction`, `initial_guard_count`, `guard_entry_list`, `elite_entry_list`, `captain_entry`)
VALUES (3703, 'Shattrath City', 0, 40, '19687', '19687', 19687);

-- ==========================================================================
-- NORTHREND ZONES (WOTLK)
-- ==========================================================================

-- Dalaran (ID: 4395)
INSERT INTO `ec_guard_zone_config` (`zone_id`, `zone_name`, `faction`, `initial_guard_count`, `guard_entry_list`, `elite_entry_list`, `captain_entry`)
VALUES (4395, 'Dalaran', 0, 50, '30659', '30659', 30659);

-- Borean Tundra and Howling Fjord are intentionally omitted. Each contains
-- several Alliance, Horde, and neutral settlements, while this legacy table is
-- keyed only by zone. A single row would leak one town's faction and guards
-- across the entire zone.

-- ==========================================================================
-- CROSSROADS SPECIAL (Key story location)
-- ==========================================================================
-- The Crossroads is in The Barrens but deserves special attention as
-- a frequently attacked PvP hotspot

-- Note: The Crossroads is area 380 within zone 17 (The Barrens)
-- We could add area-specific tracking if needed

-- ==========================================================================
-- ROLLBACK: Remove zone config table
-- ==========================================================================
/*
DROP TABLE IF EXISTS `ec_guard_zone_config`;
*/
