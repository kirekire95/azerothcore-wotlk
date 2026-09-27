-- ============================================================================
-- EC Alive World - Goal System Targets
-- Part of mod-alive-world for AzerothCore
--
-- Defines target values and configurations for bot goals.
-- Goals drive bot behavior and give them purpose in the world.
--
-- NOTE: Goal system timers and performance settings belong in .conf files.
-- This table contains BALANCE and CONTENT settings that affect gameplay.
-- ============================================================================

DROP TABLE IF EXISTS `ec_goal_targets`;

CREATE TABLE `ec_goal_targets` (
    `goal_type` VARCHAR(50) NOT NULL COMMENT 'Goal type identifier',
    `subtype` VARCHAR(50) NOT NULL DEFAULT 'DEFAULT' COMMENT 'Subtype for variants',
    `target_value` INT NOT NULL COMMENT 'Target value to complete goal',
    `target_unit` VARCHAR(50) NOT NULL DEFAULT 'COUNT' COMMENT 'Unit of measurement',
    `min_level` TINYINT UNSIGNED NOT NULL DEFAULT 1 COMMENT 'Minimum bot level',
    `max_level` TINYINT UNSIGNED NOT NULL DEFAULT 80 COMMENT 'Maximum bot level',
    `priority_weight` FLOAT NOT NULL DEFAULT 1.0 COMMENT 'Weight for goal selection',
    `time_limit_minutes` INT UNSIGNED NOT NULL DEFAULT 0 COMMENT 'Time limit (0=no limit)',
    `reward_gold_copper` INT UNSIGNED NOT NULL DEFAULT 0 COMMENT 'Gold reward in copper',
    `reward_xp` INT UNSIGNED NOT NULL DEFAULT 0 COMMENT 'XP reward',
    `cooldown_minutes` INT UNSIGNED NOT NULL DEFAULT 0 COMMENT 'Cooldown before same goal',
    `enabled` TINYINT(1) NOT NULL DEFAULT 1,
    `description` TEXT,
    PRIMARY KEY (`goal_type`, `subtype`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

-- ============================================================================
-- Exploration Goals
-- ============================================================================
INSERT INTO `ec_goal_targets` (`goal_type`, `subtype`, `target_value`, `target_unit`, `min_level`, `max_level`, `priority_weight`, `time_limit_minutes`, `description`) VALUES
('EXPLORATION', 'ZONE_DISCOVER', 5, 'SUBZONES', 1, 80, 1.0, 120, 'Discover 5 subzones in current zone'),
('EXPLORATION', 'ZONE_COMPLETE', 100, 'PERCENT', 10, 80, 0.8, 300, 'Fully explore a zone (100%)'),
('EXPLORATION', 'CONTINENT', 20, 'ZONES', 20, 80, 0.5, 0, 'Explore 20 zones on continent'),
('EXPLORATION', 'WORLD', 50, 'ZONES', 40, 80, 0.3, 0, 'Explore 50 zones total');

-- ============================================================================
-- Zone Time Goals (spend time in specific zones)
-- ============================================================================
INSERT INTO `ec_goal_targets` (`goal_type`, `subtype`, `target_value`, `target_unit`, `min_level`, `max_level`, `priority_weight`, `time_limit_minutes`, `description`) VALUES
('ZONE_TIME', 'SHORT_VISIT', 15, 'MINUTES', 1, 80, 1.5, 60, 'Spend 15 minutes in a zone'),
('ZONE_TIME', 'EXTENDED_STAY', 30, 'MINUTES', 1, 80, 1.0, 120, 'Spend 30 minutes in a zone'),
('ZONE_TIME', 'RESIDENT', 60, 'MINUTES', 1, 80, 0.7, 180, 'Spend 1 hour in a zone');

-- ============================================================================
-- Combat Goals
-- ============================================================================
INSERT INTO `ec_goal_targets` (`goal_type`, `subtype`, `target_value`, `target_unit`, `min_level`, `max_level`, `priority_weight`, `time_limit_minutes`, `description`) VALUES
('COMBAT', 'KILLS_EASY', 10, 'KILLS', 1, 80, 1.2, 60, 'Kill 10 creatures'),
('COMBAT', 'KILLS_MEDIUM', 25, 'KILLS', 10, 80, 1.0, 120, 'Kill 25 creatures'),
('COMBAT', 'KILLS_HARD', 50, 'KILLS', 20, 80, 0.8, 180, 'Kill 50 creatures'),
('COMBAT', 'ELITE_KILL', 1, 'KILLS', 30, 80, 0.6, 60, 'Kill an elite creature'),
('COMBAT', 'RARE_KILL', 1, 'KILLS', 20, 80, 0.4, 0, 'Kill a rare spawn'),
('COMBAT', 'BOSS_KILL', 1, 'KILLS', 60, 80, 0.3, 0, 'Kill a dungeon/raid boss');

-- ============================================================================
-- PvP Goals
-- ============================================================================
INSERT INTO `ec_goal_targets` (`goal_type`, `subtype`, `target_value`, `target_unit`, `min_level`, `max_level`, `priority_weight`, `time_limit_minutes`, `description`) VALUES
('PVP', 'HONOR_SMALL', 100, 'HONOR', 10, 80, 0.8, 120, 'Earn 100 honor'),
('PVP', 'HONOR_MEDIUM', 500, 'HONOR', 20, 80, 0.6, 240, 'Earn 500 honor'),
('PVP', 'HONOR_LARGE', 1000, 'HONOR', 40, 80, 0.4, 0, 'Earn 1000 honor'),
('PVP', 'KILLS_SMALL', 5, 'KILLS', 10, 80, 0.7, 120, 'Get 5 honorable kills'),
('PVP', 'KILLS_MEDIUM', 15, 'KILLS', 20, 80, 0.5, 240, 'Get 15 honorable kills'),
('PVP', 'KILLS_LARGE', 50, 'KILLS', 40, 80, 0.3, 0, 'Get 50 honorable kills'),
('PVP', 'BATTLEGROUND', 1, 'COMPLETION', 20, 80, 0.6, 60, 'Complete a battleground');

-- ============================================================================
-- Profession Goals
-- ============================================================================
INSERT INTO `ec_goal_targets` (`goal_type`, `subtype`, `target_value`, `target_unit`, `min_level`, `max_level`, `priority_weight`, `time_limit_minutes`, `description`) VALUES
('PROFESSION', 'SKILL_SMALL', 25, 'SKILL_POINTS', 5, 80, 1.0, 120, 'Gain 25 profession skill'),
('PROFESSION', 'SKILL_MEDIUM', 50, 'SKILL_POINTS', 10, 80, 0.8, 240, 'Gain 50 profession skill'),
('PROFESSION', 'SKILL_LARGE', 75, 'SKILL_POINTS', 20, 80, 0.6, 0, 'Gain 75 profession skill'),
('PROFESSION', 'CRAFT_ITEMS', 10, 'ITEMS', 10, 80, 0.9, 60, 'Craft 10 items'),
('PROFESSION', 'GATHER_NODES', 20, 'NODES', 5, 80, 1.1, 90, 'Gather from 20 nodes'),
('PROFESSION', 'MAX_SKILL', 450, 'SKILL_LEVEL', 55, 80, 0.3, 0, 'Max out a profession (450)');

-- ============================================================================
-- Social Goals
-- ============================================================================
INSERT INTO `ec_goal_targets` (`goal_type`, `subtype`, `target_value`, `target_unit`, `min_level`, `max_level`, `priority_weight`, `time_limit_minutes`, `description`) VALUES
('SOCIAL', 'MAKE_FRIEND', 1, 'FRIENDS', 1, 80, 0.8, 180, 'Make a new friend'),
('SOCIAL', 'MAKE_FRIENDS', 5, 'FRIENDS', 1, 80, 0.5, 0, 'Make 5 new friends'),
('SOCIAL', 'HELP_PLAYER', 3, 'ASSISTS', 10, 80, 0.9, 120, 'Help 3 players'),
('SOCIAL', 'GROUP_DUNGEON', 1, 'COMPLETION', 15, 80, 0.7, 120, 'Complete a dungeon with group'),
('SOCIAL', 'GUILD_ACTIVITY', 5, 'ACTIVITIES', 10, 80, 0.6, 180, 'Participate in 5 guild activities'),
('SOCIAL', 'TRADE_SUCCESS', 3, 'TRADES', 10, 80, 0.7, 120, 'Complete 3 successful trades');

-- ============================================================================
-- Reputation Goals
-- ============================================================================
INSERT INTO `ec_goal_targets` (`goal_type`, `subtype`, `target_value`, `target_unit`, `min_level`, `max_level`, `priority_weight`, `time_limit_minutes`, `description`) VALUES
('REPUTATION', 'FRIENDLY', 3000, 'REP_POINTS', 10, 80, 0.7, 0, 'Reach Friendly with a faction'),
('REPUTATION', 'HONORED', 9000, 'REP_POINTS', 20, 80, 0.5, 0, 'Reach Honored with a faction'),
('REPUTATION', 'REVERED', 21000, 'REP_POINTS', 40, 80, 0.3, 0, 'Reach Revered with a faction'),
('REPUTATION', 'EXALTED', 42000, 'REP_POINTS', 60, 80, 0.2, 0, 'Reach Exalted with a faction'),
('REPUTATION', 'DAILY_QUESTS', 5, 'QUESTS', 70, 80, 0.8, 180, 'Complete 5 daily quests');

-- ============================================================================
-- Economy Goals
-- ============================================================================
INSERT INTO `ec_goal_targets` (`goal_type`, `subtype`, `target_value`, `target_unit`, `min_level`, `max_level`, `priority_weight`, `time_limit_minutes`, `reward_gold_copper`, `description`) VALUES
('ECONOMY', 'EARN_SMALL', 10000, 'COPPER', 5, 80, 1.0, 120, 0, 'Earn 1 gold'),
('ECONOMY', 'EARN_MEDIUM', 50000, 'COPPER', 20, 80, 0.8, 240, 0, 'Earn 5 gold'),
('ECONOMY', 'EARN_LARGE', 100000, 'COPPER', 40, 80, 0.6, 0, 0, 'Earn 10 gold'),
('ECONOMY', 'SAVE_GOAL', 500000, 'COPPER', 30, 80, 0.4, 0, 0, 'Save up 50 gold'),
('ECONOMY', 'FARM_ITEMS', 10, 'ITEMS', 10, 80, 0.9, 120, 0, 'Farm 10 valuable items'),
('ECONOMY', 'AUCTION_SELL', 5, 'AUCTIONS', 20, 80, 0.7, 180, 0, 'Sell 5 items on auction house');

-- ============================================================================
-- Quest Goals
-- ============================================================================
INSERT INTO `ec_goal_targets` (`goal_type`, `subtype`, `target_value`, `target_unit`, `min_level`, `max_level`, `priority_weight`, `time_limit_minutes`, `description`) VALUES
('QUEST', 'COMPLETE_SMALL', 5, 'QUESTS', 1, 80, 1.2, 120, 'Complete 5 quests'),
('QUEST', 'COMPLETE_MEDIUM', 15, 'QUESTS', 10, 80, 1.0, 240, 'Complete 15 quests'),
('QUEST', 'COMPLETE_LARGE', 30, 'QUESTS', 20, 80, 0.7, 0, 'Complete 30 quests'),
('QUEST', 'ZONE_LOREMASTER', 50, 'QUESTS', 20, 80, 0.4, 0, 'Complete 50 quests in zone (Loremaster)'),
('QUEST', 'GROUP_QUEST', 1, 'QUESTS', 15, 80, 0.6, 60, 'Complete a group quest'),
('QUEST', 'DUNGEON_QUEST', 1, 'QUESTS', 15, 80, 0.5, 90, 'Complete a dungeon quest');

-- ============================================================================
-- Achievement Goals (meta-goals)
-- ============================================================================
INSERT INTO `ec_goal_targets` (`goal_type`, `subtype`, `target_value`, `target_unit`, `min_level`, `max_level`, `priority_weight`, `time_limit_minutes`, `description`) VALUES
('ACHIEVEMENT', 'EXPLORER', 1, 'ACHIEVEMENT', 40, 80, 0.3, 0, 'Earn Explorer title'),
('ACHIEVEMENT', 'LOREMASTER', 1, 'ACHIEVEMENT', 60, 80, 0.2, 0, 'Earn Loremaster title'),
('ACHIEVEMENT', 'PVP_RANK', 1, 'ACHIEVEMENT', 20, 80, 0.4, 0, 'Reach a PvP rank'),
('ACHIEVEMENT', 'PROFESSION_MASTER', 1, 'ACHIEVEMENT', 55, 80, 0.3, 0, 'Master a profession');

-- ============================================================================
-- Goal Chains (sequential goals)
-- ============================================================================

DROP TABLE IF EXISTS `ec_goal_chains`;

CREATE TABLE `ec_goal_chains` (
    `chain_id` INT UNSIGNED NOT NULL AUTO_INCREMENT,
    `chain_name` VARCHAR(100) NOT NULL,
    `step_order` TINYINT UNSIGNED NOT NULL,
    `goal_type` VARCHAR(50) NOT NULL,
    `goal_subtype` VARCHAR(50) NOT NULL DEFAULT 'DEFAULT',
    `required_previous` TINYINT(1) NOT NULL DEFAULT 1 COMMENT 'Must complete previous step first',
    `bonus_multiplier` FLOAT NOT NULL DEFAULT 1.0 COMMENT 'Reward multiplier for chain completion',
    `description` TEXT,
    PRIMARY KEY (`chain_id`, `step_order`),
    KEY `idx_chain_name` (`chain_name`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

-- Example goal chains
INSERT INTO `ec_goal_chains` (`chain_id`, `chain_name`, `step_order`, `goal_type`, `goal_subtype`, `bonus_multiplier`, `description`) VALUES
-- Adventurer chain
(1, 'Aspiring Adventurer', 1, 'EXPLORATION', 'ZONE_DISCOVER', 1.0, 'Discover new areas'),
(1, 'Aspiring Adventurer', 2, 'COMBAT', 'KILLS_EASY', 1.1, 'Prove your combat skills'),
(1, 'Aspiring Adventurer', 3, 'QUEST', 'COMPLETE_SMALL', 1.2, 'Complete quests'),
(1, 'Aspiring Adventurer', 4, 'SOCIAL', 'MAKE_FRIEND', 1.5, 'Make a friend along the way'),

-- Merchant chain
(2, 'Aspiring Merchant', 1, 'PROFESSION', 'GATHER_NODES', 1.0, 'Gather materials'),
(2, 'Aspiring Merchant', 2, 'ECONOMY', 'FARM_ITEMS', 1.1, 'Collect valuable items'),
(2, 'Aspiring Merchant', 3, 'ECONOMY', 'AUCTION_SELL', 1.2, 'Sell on auction house'),
(2, 'Aspiring Merchant', 4, 'ECONOMY', 'EARN_MEDIUM', 1.5, 'Profit!'),

-- Warrior chain
(3, 'Warrior Training', 1, 'COMBAT', 'KILLS_EASY', 1.0, 'Basic combat training'),
(3, 'Warrior Training', 2, 'COMBAT', 'KILLS_MEDIUM', 1.1, 'Advanced combat'),
(3, 'Warrior Training', 3, 'COMBAT', 'ELITE_KILL', 1.3, 'Face an elite'),
(3, 'Warrior Training', 4, 'PVP', 'KILLS_SMALL', 1.5, 'Test your skills against players');
