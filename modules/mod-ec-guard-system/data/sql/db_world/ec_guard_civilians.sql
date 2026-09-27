-- ============================================================================
-- EC Guard System - Civilian Entries
-- Part of mod-ec-guard-system for AzerothCore
--
-- Defines which creature entries are considered "civilians" that flee when
-- combat occurs nearby and trigger crime events when attacked by players.
-- ============================================================================

DROP TABLE IF EXISTS `ec_guard_civilians`;

CREATE TABLE `ec_guard_civilians` (
    `entry` INT UNSIGNED NOT NULL COMMENT 'creature_template entry',
    `name` VARCHAR(100) DEFAULT NULL COMMENT 'Description for reference',
    `enabled` TINYINT(1) NOT NULL DEFAULT 1,
    PRIMARY KEY (`entry`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

-- Common civilian NPCs (vendors, quest givers, innkeepers, etc.)
-- These are NPCs that should trigger crime when attacked
INSERT INTO `ec_guard_civilians` (`entry`, `name`, `enabled`) VALUES
-- Generic Vendors
(54, 'Barkeep', 1),
(55, 'Blacksmith', 1),
(66, 'Stormwind City Guard', 0),  -- Guard, not civilian
(68, 'Stormwind Royal Guard', 0), -- Guard, not civilian

-- Innkeepers (common flag: UNIT_NPC_FLAG_INNKEEPER)
(295, 'Innkeeper Allison', 1),
(6740, 'Innkeeper Farley', 1),
(6741, 'Innkeeper Skindle', 1),
(6746, 'Innkeeper Trelayne', 1),
(6747, 'Innkeeper Boorand Plainswind', 1),
(6790, 'Innkeeper Anderson', 1),

-- Flight Masters (common flag: UNIT_NPC_FLAG_FLIGHTMASTER)
(352, 'Dungar Longdrink', 1),
(523, 'Thor', 1),
(931, 'Gryth Thurden', 1),

-- Stable Masters
(927, 'Shelby Stoneflint', 1),
(928, 'Kelm Hargunth', 1),

-- Auctioneers
(8719, 'Auctioneer Jaxon', 1),
(8720, 'Auctioneer Stampi', 1),

-- Bankers
(2456, 'Banker', 1),
(2457, 'Banker', 1),

-- Common Goldshire NPCs
(241, 'Remy "Two Times"', 1),
(240, 'Innkeeper Farley', 1),
(244, 'Marshal Dughan', 0),  -- Guard-like

-- Trainers (should not be attacked)
(198, 'Khelden Bremen', 1),
(375, 'Priestess Anetta', 1),
(459, 'Alamar Grimm', 1),
(837, 'Eagan Peltskinner', 1);

-- Add more entries as needed - the table is meant to be extended
-- based on server customization needs
