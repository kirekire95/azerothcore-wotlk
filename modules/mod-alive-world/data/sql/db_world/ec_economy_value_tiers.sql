-- ============================================================================
-- EC Alive World - Economy Value Tiers
-- Part of mod-alive-world for AzerothCore
--
-- Defines item value tiers for the economy system. Used by bots when:
-- - Generating gossip about valuable items
-- - Setting goals for farming/acquisition
-- - Evaluating trade opportunities
-- - Determining excitement level for drops
--
-- Values are in COPPER (100 copper = 1 silver, 10000 copper = 1 gold)
-- ============================================================================

DROP TABLE IF EXISTS `ec_economy_value_tiers`;

CREATE TABLE `ec_economy_value_tiers` (
    `tier_id` TINYINT UNSIGNED NOT NULL COMMENT 'Tier level (1-6)',
    `tier_name` VARCHAR(50) NOT NULL COMMENT 'Human-readable tier name',
    `min_value_copper` INT UNSIGNED NOT NULL COMMENT 'Minimum item value in copper',
    `max_value_copper` INT UNSIGNED NOT NULL COMMENT 'Maximum item value in copper (0=unlimited)',
    `gossip_priority` TINYINT UNSIGNED NOT NULL DEFAULT 5 COMMENT 'Priority for gossip generation (1-10)',
    `excitement_level` ENUM('NONE', 'MILD', 'MODERATE', 'HIGH', 'EXTREME') NOT NULL DEFAULT 'MILD',
    `goal_weight` FLOAT NOT NULL DEFAULT 1.0 COMMENT 'Weight for goal generation (higher=more likely)',
    `color_code` VARCHAR(10) DEFAULT NULL COMMENT 'Display color (WoW item quality color)',
    `description` VARCHAR(255) DEFAULT NULL,
    PRIMARY KEY (`tier_id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

INSERT INTO `ec_economy_value_tiers` (`tier_id`, `tier_name`, `min_value_copper`, `max_value_copper`, `gossip_priority`, `excitement_level`, `goal_weight`, `color_code`, `description`) VALUES
(1, 'Junk',       0,        9999,      1, 'NONE',     0.1, '|cff9d9d9d', 'Vendor trash, less than 1 gold'),
(2, 'Common',     10000,    99999,     3, 'MILD',     0.5, '|cffffffff', 'Common items, 1-10 gold'),
(3, 'Uncommon',   100000,   499999,    5, 'MODERATE', 1.0, '|cff1eff00', 'Uncommon items, 10-50 gold'),
(4, 'Valuable',   500000,   1999999,   7, 'HIGH',     1.5, '|cff0070dd', 'Valuable items, 50-200 gold'),
(5, 'Precious',   2000000,  9999999,   9, 'HIGH',     2.0, '|cffa335ee', 'Precious items, 200-1000 gold'),
(6, 'Legendary',  10000000, 0,        10, 'EXTREME',  3.0, '|cffff8000', 'Legendary value, 1000+ gold');

-- ============================================================================
-- Item Category Modifiers
-- Some item categories have different perceived value vs vendor price
-- ============================================================================

DROP TABLE IF EXISTS `ec_economy_category_modifiers`;

CREATE TABLE `ec_economy_category_modifiers` (
    `category` VARCHAR(50) NOT NULL COMMENT 'Item category',
    `value_multiplier` FLOAT NOT NULL DEFAULT 1.0 COMMENT 'Multiplier for perceived value',
    `demand_base` FLOAT NOT NULL DEFAULT 1.0 COMMENT 'Base demand level (affects gossip)',
    `bot_interest` FLOAT NOT NULL DEFAULT 1.0 COMMENT 'How interested bots are in this category',
    `description` VARCHAR(255) DEFAULT NULL,
    PRIMARY KEY (`category`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

INSERT INTO `ec_economy_category_modifiers` (`category`, `value_multiplier`, `demand_base`, `bot_interest`, `description`) VALUES
-- Consumables
('POTION', 1.2, 1.5, 1.3, 'Potions - always in demand'),
('ELIXIR', 1.3, 1.4, 1.2, 'Elixirs - raiders want these'),
('FLASK', 1.5, 1.8, 1.5, 'Flasks - high end raiders'),
('FOOD', 1.0, 1.2, 1.0, 'Food buffs'),
('BANDAGE', 0.8, 0.9, 0.7, 'Bandages - less exciting'),

-- Crafting
('ORE', 1.1, 1.3, 1.2, 'Mining ore'),
('HERB', 1.2, 1.4, 1.3, 'Herbalism herbs'),
('LEATHER', 1.0, 1.1, 1.0, 'Skinning leather'),
('CLOTH', 1.1, 1.2, 1.1, 'Tailoring cloth'),
('ENCHANTING_MAT', 1.4, 1.5, 1.4, 'Enchanting materials'),
('GEM', 1.5, 1.6, 1.5, 'Jewelcrafting gems'),

-- Equipment
('WEAPON', 1.3, 1.2, 1.4, 'Weapons - exciting drops'),
('ARMOR', 1.2, 1.1, 1.3, 'Armor pieces'),
('TRINKET', 1.5, 1.3, 1.6, 'Trinkets - unique effects'),
('RING', 1.3, 1.2, 1.3, 'Rings'),
('NECKLACE', 1.3, 1.2, 1.3, 'Necklaces'),

-- Special
('MOUNT', 3.0, 2.0, 2.5, 'Mounts - very exciting'),
('PET', 2.0, 1.8, 2.0, 'Companion pets'),
('RECIPE', 1.8, 1.5, 1.7, 'Crafting recipes'),
('PATTERN', 1.8, 1.5, 1.7, 'Crafting patterns'),
('SCHEMATIC', 1.8, 1.5, 1.7, 'Engineering schematics'),

-- Misc
('QUEST_ITEM', 0.5, 0.3, 0.2, 'Quest items - not tradeable'),
('JUNK', 0.3, 0.1, 0.1, 'Gray vendor trash'),
('REAGENT', 1.0, 1.0, 0.8, 'Spell reagents');

-- ============================================================================
-- Supply/Demand Events
-- Temporary modifiers for world events or seasonal changes
-- ============================================================================

DROP TABLE IF EXISTS `ec_economy_demand_events`;

CREATE TABLE `ec_economy_demand_events` (
    `event_id` INT UNSIGNED NOT NULL AUTO_INCREMENT,
    `event_name` VARCHAR(100) NOT NULL,
    `category` VARCHAR(50) NOT NULL COMMENT 'Item category affected',
    `demand_multiplier` FLOAT NOT NULL DEFAULT 1.0,
    `duration_hours` INT UNSIGNED NOT NULL DEFAULT 24,
    `trigger_type` ENUM('MANUAL', 'SCHEDULED', 'WORLD_EVENT', 'RANDOM') NOT NULL DEFAULT 'MANUAL',
    `trigger_data` VARCHAR(255) DEFAULT NULL COMMENT 'Data for trigger (event ID, schedule, etc)',
    `enabled` TINYINT(1) NOT NULL DEFAULT 1,
    `description` TEXT,
    PRIMARY KEY (`event_id`),
    KEY `idx_category` (`category`),
    KEY `idx_enabled` (`enabled`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

INSERT INTO `ec_economy_demand_events` (`event_name`, `category`, `demand_multiplier`, `duration_hours`, `trigger_type`, `trigger_data`, `enabled`, `description`) VALUES
('Raid Night', 'FLASK', 2.0, 6, 'SCHEDULED', 'WEEKLY:TUESDAY:18:00', 1, 'Tuesday raid night increases flask demand'),
('Raid Night', 'POTION', 1.5, 6, 'SCHEDULED', 'WEEKLY:TUESDAY:18:00', 1, 'Tuesday raid night increases potion demand'),
('Darkmoon Faire', 'PET', 1.8, 168, 'WORLD_EVENT', '479', 1, 'Darkmoon Faire increases pet interest'),
('Brewfest', 'FOOD', 1.5, 336, 'WORLD_EVENT', '372', 1, 'Brewfest increases food/drink interest'),
('Love is in the Air', 'GEM', 1.3, 336, 'WORLD_EVENT', '423', 1, 'Valentines increases jewelry demand'),
('War Effort', 'BANDAGE', 2.0, 48, 'MANUAL', NULL, 0, 'Manual trigger for RP war events');
