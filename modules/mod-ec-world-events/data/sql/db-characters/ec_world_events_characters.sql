--
-- EC World Events - Database Schema (Characters Database)
-- Copyright (C) 2026 EC
--

-- Rewards table (references world.ec_world_events)
DROP TABLE IF EXISTS `ec_world_event_rewards`;
CREATE TABLE `ec_world_event_rewards` (
    `id` INT UNSIGNED NOT NULL AUTO_INCREMENT,
    `event_id` INT UNSIGNED NOT NULL,
    `reward_type` TINYINT UNSIGNED NOT NULL DEFAULT 0 COMMENT '0=Gold, 1=Honor, 2=Item, 3=Title',
    `value` INT UNSIGNED NOT NULL DEFAULT 0 COMMENT 'Gold (copper), Honor points, Item entry, or Title ID',
    `item_count` INT UNSIGNED NOT NULL DEFAULT 1 COMMENT 'Number of items (for item rewards)',
    `min_kills` INT UNSIGNED NOT NULL DEFAULT 0 COMMENT 'Minimum kills required',
    `participation_percent` FLOAT NOT NULL DEFAULT 0 COMMENT 'Minimum participation % (0-100)',
    `reward_condition` TINYINT UNSIGNED NOT NULL DEFAULT 0 COMMENT '0=any participant, 1=winning faction only, 2=losing faction only',
    PRIMARY KEY (`id`),
    KEY `idx_event` (`event_id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT='EC World Events - Reward Definitions';

-- Participants table (tracking player participation)
DROP TABLE IF EXISTS `ec_world_event_participants`;
CREATE TABLE `ec_world_event_participants` (
    `id` INT UNSIGNED NOT NULL AUTO_INCREMENT,
    `instance_id` INT UNSIGNED NOT NULL,
    `player_guid` INT UNSIGNED NOT NULL,
    `player_name` VARCHAR(50) NOT NULL,
    `kills` INT UNSIGNED NOT NULL DEFAULT 0,
    `deaths` INT UNSIGNED NOT NULL DEFAULT 0,
    `damage_dealt` BIGINT UNSIGNED NOT NULL DEFAULT 0,
    `healing_done` BIGINT UNSIGNED NOT NULL DEFAULT 0,
    `join_time` DATETIME NOT NULL,
    `rewarded` TINYINT(1) NOT NULL DEFAULT 0,
    PRIMARY KEY (`id`),
    UNIQUE KEY `idx_instance_player` (`instance_id`, `player_guid`),
    KEY `idx_player` (`player_guid`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT='EC World Events - Participant Tracking';

-- Event logs (history)
DROP TABLE IF EXISTS `ec_world_event_logs`;
CREATE TABLE `ec_world_event_logs` (
    `id` INT UNSIGNED NOT NULL AUTO_INCREMENT,
    `event_id` INT UNSIGNED NOT NULL,
    `instance_id` INT UNSIGNED NOT NULL,
    `state` TINYINT UNSIGNED NOT NULL DEFAULT 0 COMMENT '0=Scheduled, 1=Active, 2=Completed, 3=Cancelled, 4=Failed',
    `start_time` DATETIME NOT NULL,
    `end_time` DATETIME DEFAULT NULL,
    `total_kills` INT UNSIGNED NOT NULL DEFAULT 0,
    `escalation_level` INT UNSIGNED NOT NULL DEFAULT 0,
    `participant_count` INT UNSIGNED NOT NULL DEFAULT 0,
    `outcome` TINYINT UNSIGNED NOT NULL DEFAULT 0 COMMENT '0=Pending, 1=AttackerWin, 2=DefenderWin, 3=Draw',
    `created_at` TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    PRIMARY KEY (`id`),
    KEY `idx_event` (`event_id`),
    KEY `idx_instance` (`instance_id`),
    KEY `idx_time` (`start_time`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT='EC World Events - Event History';

-- Crash-safe return state for Playerbots recruited into events. This is kept
-- separate from character_entry_point because battlegrounds and LFG share it.
DROP TABLE IF EXISTS `ec_world_event_bot_return_context`;
CREATE TABLE `ec_world_event_bot_return_context` (
    `bot_guid` INT UNSIGNED NOT NULL COMMENT 'Character GUID low part',
    `event_instance_id` INT UNSIGNED NOT NULL,
    `event_id` INT UNSIGNED NOT NULL,
    `map_id` INT UNSIGNED NOT NULL,
    `position_x` FLOAT NOT NULL,
    `position_y` FLOAT NOT NULL,
    `position_z` FLOAT NOT NULL,
    `orientation` FLOAT NOT NULL,
    `combat_strategies` TEXT NOT NULL,
    `non_combat_strategies` TEXT NOT NULL,
    `dead_strategies` TEXT NOT NULL,
    `captured_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    PRIMARY KEY (`bot_guid`),
    KEY `idx_event_instance` (`event_instance_id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci
  COMMENT='Crash-safe return state for playerbots recruited into EC world events';

-- =============================================================================
-- DEFAULT REWARDS
-- =============================================================================

-- Rewards for Goldshire Raid (event_id = 1)
INSERT INTO `ec_world_event_rewards` (`event_id`, `reward_type`, `value`, `item_count`, `min_kills`, `participation_percent`)
VALUES
(1, 0, 100000, 1, 1, 10),   -- 10 gold for participating (1+ kills)
(1, 1, 100, 1, 5, 20),      -- 100 honor for 5+ kills
(1, 0, 500000, 1, 20, 40);  -- 50 gold bonus for 20+ kills

-- Rewards for Sentinel Hill Raid (event_id = 2)
INSERT INTO `ec_world_event_rewards` (`event_id`, `reward_type`, `value`, `item_count`, `min_kills`, `participation_percent`)
VALUES
(2, 0, 150000, 1, 1, 10),
(2, 1, 150, 1, 5, 20),
(2, 0, 750000, 1, 25, 40);

-- Rewards for Darkshire Raid (event_id = 3)
INSERT INTO `ec_world_event_rewards` (`event_id`, `reward_type`, `value`, `item_count`, `min_kills`, `participation_percent`)
VALUES
(3, 0, 200000, 1, 1, 10),
(3, 1, 200, 1, 5, 20),
(3, 0, 1000000, 1, 30, 40);

-- Rewards for Astranaar Raid (event_id = 4)
INSERT INTO `ec_world_event_rewards` (`event_id`, `reward_type`, `value`, `item_count`, `min_kills`, `participation_percent`)
VALUES
(4, 0, 250000, 1, 1, 10),
(4, 1, 250, 1, 5, 20),
(4, 0, 1250000, 1, 35, 40);

-- Rewards for Auberdine Raid (event_id = 5)
INSERT INTO `ec_world_event_rewards` (`event_id`, `reward_type`, `value`, `item_count`, `min_kills`, `participation_percent`)
VALUES
(5, 0, 200000, 1, 1, 10),
(5, 1, 200, 1, 5, 20),
(5, 0, 1000000, 1, 30, 40);

-- Rewards for Crossroads Raid (event_id = 6)
INSERT INTO `ec_world_event_rewards` (`event_id`, `reward_type`, `value`, `item_count`, `min_kills`, `participation_percent`)
VALUES
(6, 0, 200000, 1, 1, 10),
(6, 1, 200, 1, 5, 20),
(6, 0, 1000000, 1, 30, 40);

-- Rewards for Camp Taurajo Raid (event_id = 7)
INSERT INTO `ec_world_event_rewards` (`event_id`, `reward_type`, `value`, `item_count`, `min_kills`, `participation_percent`)
VALUES
(7, 0, 150000, 1, 1, 10),
(7, 1, 150, 1, 5, 20),
(7, 0, 750000, 1, 25, 40);

-- Rewards for Grom'gol Raid (event_id = 8)
INSERT INTO `ec_world_event_rewards` (`event_id`, `reward_type`, `value`, `item_count`, `min_kills`, `participation_percent`)
VALUES
(8, 0, 250000, 1, 1, 10),
(8, 1, 250, 1, 5, 20),
(8, 0, 1250000, 1, 35, 40);

-- Rewards for Tarren Mill Raid (event_id = 9)
INSERT INTO `ec_world_event_rewards` (`event_id`, `reward_type`, `value`, `item_count`, `min_kills`, `participation_percent`)
VALUES
(9, 0, 300000, 1, 1, 10),
(9, 1, 300, 1, 5, 20),
(9, 0, 1500000, 1, 40, 40);

-- Rewards for Splintertree Raid (event_id = 10)
INSERT INTO `ec_world_event_rewards` (`event_id`, `reward_type`, `value`, `item_count`, `min_kills`, `participation_percent`)
VALUES
(10, 0, 200000, 1, 1, 10),
(10, 1, 200, 1, 5, 20),
(10, 0, 1000000, 1, 30, 40);

-- =============================================================================
-- DEFENSE EVENT REWARDS (Elite Response Events)
-- =============================================================================

-- Rewards for Kor'kron Elite Response (event_id = 101) - Faction-wide Horde
INSERT INTO `ec_world_event_rewards` (`event_id`, `reward_type`, `value`, `item_count`, `min_kills`, `participation_percent`)
VALUES
(101, 0, 200000, 1, 1, 10),   -- 20 gold for participating
(101, 1, 200, 1, 5, 20),      -- 200 honor for 5+ kills
(101, 0, 1000000, 1, 25, 40); -- 100 gold bonus for 25+ kills

-- Rewards for Stormwind Royal Guard Response (event_id = 102) - Faction-wide Alliance
INSERT INTO `ec_world_event_rewards` (`event_id`, `reward_type`, `value`, `item_count`, `min_kills`, `participation_percent`)
VALUES
(102, 0, 200000, 1, 1, 10),
(102, 1, 200, 1, 5, 20),
(102, 0, 1000000, 1, 25, 40);

-- Rewards for Crossroads Elite Defense (event_id = 111)
INSERT INTO `ec_world_event_rewards` (`event_id`, `reward_type`, `value`, `item_count`, `min_kills`, `participation_percent`)
VALUES
(111, 0, 250000, 1, 1, 10),    -- 25 gold for participating
(111, 1, 250, 1, 5, 20),       -- 250 honor for 5+ kills
(111, 0, 1250000, 1, 30, 40),  -- 125 gold bonus for 30+ kills
(111, 2, 13446, 5, 20, 30);    -- 5x Major Healing Potion (item 13446) for 20+ kills

-- Rewards for Tarren Mill Elite Defense (event_id = 112)
INSERT INTO `ec_world_event_rewards` (`event_id`, `reward_type`, `value`, `item_count`, `min_kills`, `participation_percent`)
VALUES
(112, 0, 300000, 1, 1, 10),
(112, 1, 300, 1, 5, 20),
(112, 0, 1500000, 1, 35, 40),
(112, 2, 13446, 5, 25, 35);

-- Rewards for Goldshire Elite Defense (event_id = 121)
INSERT INTO `ec_world_event_rewards` (`event_id`, `reward_type`, `value`, `item_count`, `min_kills`, `participation_percent`)
VALUES
(121, 0, 150000, 1, 1, 10),    -- 15 gold for participating
(121, 1, 150, 1, 5, 20),       -- 150 honor for 5+ kills
(121, 0, 750000, 1, 20, 40),   -- 75 gold bonus for 20+ kills
(121, 2, 1710, 10, 15, 25);    -- 10x Greater Healing Potion (item 1710) for 15+ kills

-- Rewards for Astranaar Elite Defense (event_id = 122)
INSERT INTO `ec_world_event_rewards` (`event_id`, `reward_type`, `value`, `item_count`, `min_kills`, `participation_percent`)
VALUES
(122, 0, 250000, 1, 1, 10),
(122, 1, 250, 1, 5, 20),
(122, 0, 1250000, 1, 30, 40),
(122, 2, 13446, 5, 20, 30);

-- =============================================================================
-- VICTORY BONUS REWARDS (winning faction gets extra)
-- reward_condition: 0=any, 1=winning faction only, 2=losing faction only
-- =============================================================================

-- Victory bonuses for Goldshire Raid (event_id = 1)
INSERT INTO `ec_world_event_rewards` (`event_id`, `reward_type`, `value`, `item_count`, `min_kills`, `participation_percent`, `reward_condition`)
VALUES
(1, 0, 200000, 1, 0, 5, 1),  -- 20g victory bonus (winners, any participation)
(1, 1, 200, 1, 0, 5, 1),     -- 200 honor victory bonus
(1, 0, 50000, 1, 0, 5, 2);   -- 5g consolation (losers)

-- Victory bonuses for Sentinel Hill Raid (event_id = 2)
INSERT INTO `ec_world_event_rewards` (`event_id`, `reward_type`, `value`, `item_count`, `min_kills`, `participation_percent`, `reward_condition`)
VALUES
(2, 0, 300000, 1, 0, 5, 1),
(2, 1, 250, 1, 0, 5, 1),
(2, 0, 75000, 1, 0, 5, 2);

-- Victory bonuses for Darkshire Raid (event_id = 3)
INSERT INTO `ec_world_event_rewards` (`event_id`, `reward_type`, `value`, `item_count`, `min_kills`, `participation_percent`, `reward_condition`)
VALUES
(3, 0, 400000, 1, 0, 5, 1),
(3, 1, 300, 1, 0, 5, 1),
(3, 0, 100000, 1, 0, 5, 2);

-- Victory bonuses for Astranaar Raid (event_id = 4)
INSERT INTO `ec_world_event_rewards` (`event_id`, `reward_type`, `value`, `item_count`, `min_kills`, `participation_percent`, `reward_condition`)
VALUES
(4, 0, 500000, 1, 0, 5, 1),
(4, 1, 350, 1, 0, 5, 1),
(4, 0, 125000, 1, 0, 5, 2);

-- Victory bonuses for Auberdine Raid (event_id = 5)
INSERT INTO `ec_world_event_rewards` (`event_id`, `reward_type`, `value`, `item_count`, `min_kills`, `participation_percent`, `reward_condition`)
VALUES
(5, 0, 400000, 1, 0, 5, 1),
(5, 1, 300, 1, 0, 5, 1),
(5, 0, 100000, 1, 0, 5, 2);

-- Victory bonuses for Crossroads Raid (event_id = 6)
INSERT INTO `ec_world_event_rewards` (`event_id`, `reward_type`, `value`, `item_count`, `min_kills`, `participation_percent`, `reward_condition`)
VALUES
(6, 0, 400000, 1, 0, 5, 1),
(6, 1, 300, 1, 0, 5, 1),
(6, 0, 100000, 1, 0, 5, 2);

-- Victory bonuses for Camp Taurajo Raid (event_id = 7)
INSERT INTO `ec_world_event_rewards` (`event_id`, `reward_type`, `value`, `item_count`, `min_kills`, `participation_percent`, `reward_condition`)
VALUES
(7, 0, 300000, 1, 0, 5, 1),
(7, 1, 250, 1, 0, 5, 1),
(7, 0, 75000, 1, 0, 5, 2);

-- Victory bonuses for Grom'gol Raid (event_id = 8)
INSERT INTO `ec_world_event_rewards` (`event_id`, `reward_type`, `value`, `item_count`, `min_kills`, `participation_percent`, `reward_condition`)
VALUES
(8, 0, 500000, 1, 0, 5, 1),   -- 50g victory bonus
(8, 1, 350, 1, 0, 5, 1),      -- 350 honor victory bonus
(8, 0, 125000, 1, 0, 5, 2);   -- 12.5g consolation

-- Victory bonuses for Tarren Mill Raid (event_id = 9)
INSERT INTO `ec_world_event_rewards` (`event_id`, `reward_type`, `value`, `item_count`, `min_kills`, `participation_percent`, `reward_condition`)
VALUES
(9, 0, 600000, 1, 0, 5, 1),
(9, 1, 400, 1, 0, 5, 1),
(9, 0, 150000, 1, 0, 5, 2);

-- Victory bonuses for Splintertree Raid (event_id = 10)
INSERT INTO `ec_world_event_rewards` (`event_id`, `reward_type`, `value`, `item_count`, `min_kills`, `participation_percent`, `reward_condition`)
VALUES
(10, 0, 400000, 1, 0, 5, 1),
(10, 1, 300, 1, 0, 5, 1),
(10, 0, 100000, 1, 0, 5, 2);
