-- ============================================================================
-- mod-alive-world Database Schema
-- ============================================================================
-- Persistence tables for events, gossip, and bot memory.
-- Run this against the `acore_characters` database.
--
-- Author: EC
-- Date: 2025-01-25
-- ============================================================================

-- ============================================================================
-- WORLD EVENTS TABLE
-- ============================================================================
-- Stores all world events for historical analysis and replay.
-- Events are archived after 24 hours to alive_world_events_archive.

DROP TABLE IF EXISTS `alive_world_events`;
CREATE TABLE `alive_world_events` (
    `event_id` BIGINT UNSIGNED NOT NULL,
    `event_type` TINYINT UNSIGNED NOT NULL COMMENT 'Event type enum value from WorldEventType',
    `timestamp` BIGINT UNSIGNED NOT NULL COMMENT 'Unix timestamp in milliseconds',
    `zone_id` INT UNSIGNED NOT NULL DEFAULT 0,
    `area_id` INT UNSIGNED NOT NULL DEFAULT 0,
    `map_id` INT UNSIGNED NOT NULL DEFAULT 0,
    `primary_guid` BIGINT UNSIGNED NOT NULL DEFAULT 0 COMMENT 'Primary actor (player/bot/creature)',
    `primary_name` VARCHAR(64) DEFAULT NULL,
    `secondary_guid` BIGINT UNSIGNED DEFAULT NULL COMMENT 'Secondary actor (target/victim)',
    `secondary_name` VARCHAR(64) DEFAULT NULL,
    `importance` TINYINT UNSIGNED NOT NULL DEFAULT 5 COMMENT '1-10 scale',
    `details_json` TEXT COMMENT 'Event details/description',
    `processed` TINYINT UNSIGNED NOT NULL DEFAULT 0 COMMENT 'Whether event was processed by AI',
    `created_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    PRIMARY KEY (`event_id`),
    KEY `idx_timestamp` (`timestamp`),
    KEY `idx_event_type` (`event_type`),
    KEY `idx_zone` (`zone_id`),
    KEY `idx_map` (`map_id`),
    KEY `idx_primary_guid` (`primary_guid`),
    KEY `idx_importance` (`importance`),
    KEY `idx_created` (`created_at`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci
COMMENT='World events for AliveWorld system';

-- ============================================================================
-- WORLD EVENTS ARCHIVE TABLE
-- ============================================================================
-- Archive table for events older than 24 hours.
-- Same structure as alive_world_events for easy migration.

DROP TABLE IF EXISTS `alive_world_events_archive`;
CREATE TABLE `alive_world_events_archive` LIKE `alive_world_events`;
ALTER TABLE `alive_world_events_archive` COMMENT='Archived world events (older than 24 hours)';

-- ============================================================================
-- GOSSIP POOL TABLE
-- ============================================================================
-- Stores active and historical gossip items.

DROP TABLE IF EXISTS `alive_world_gossip`;
CREATE TABLE `alive_world_gossip` (
    `gossip_id` BIGINT UNSIGNED NOT NULL COMMENT 'C++-generated gossip ID',
    `gossip_type` VARCHAR(32) NOT NULL COMMENT 'Type (boss_killed, crime, rare_spawn, player_achievement)',
    `headline` VARCHAR(255) NOT NULL COMMENT 'Short headline text',
    `details` TEXT COMMENT 'Full gossip details',
    `source_event_id` BIGINT UNSIGNED DEFAULT NULL COMMENT 'Originating event ID',
    `origin_zone_id` INT UNSIGNED NOT NULL,
    `origin_bot_guid` BIGINT UNSIGNED DEFAULT NULL COMMENT 'Bot who first heard/created gossip',
    `subject_name` VARCHAR(64) DEFAULT NULL COMMENT 'Who/what the gossip is about',
    `importance` TINYINT UNSIGNED NOT NULL DEFAULT 5 COMMENT '1-10 scale',
    `spread_count` INT UNSIGNED NOT NULL DEFAULT 0 COMMENT 'How many bots know this',
    `spread_radius` TINYINT UNSIGNED NOT NULL DEFAULT 1 COMMENT 'How far it can spread (zones)',
    `is_active` TINYINT UNSIGNED NOT NULL DEFAULT 1 COMMENT 'Still spreading or expired',
    `created_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    `expires_at` TIMESTAMP NOT NULL COMMENT 'When gossip expires',
    PRIMARY KEY (`gossip_id`),
    KEY `idx_type` (`gossip_type`),
    KEY `idx_zone` (`origin_zone_id`),
    KEY `idx_importance` (`importance`),
    KEY `idx_active` (`is_active`),
    KEY `idx_expires` (`expires_at`),
    KEY `idx_source_event` (`source_event_id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci
COMMENT='Gossip pool for AliveWorld system';

-- ============================================================================
-- GOSSIP KNOWLEDGE TABLE
-- ============================================================================
-- Tracks which bots know which gossip items.

DROP TABLE IF EXISTS `alive_world_gossip_knowledge`;
CREATE TABLE `alive_world_gossip_knowledge` (
    `id` BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
    `gossip_id` BIGINT UNSIGNED NOT NULL,
    `bot_guid` BIGINT UNSIGNED NOT NULL,
    `learned_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    `learned_from_guid` BIGINT UNSIGNED DEFAULT NULL COMMENT 'Who told them (NULL if original)',
    `zone_id` INT UNSIGNED NOT NULL COMMENT 'Where they learned it',
    `has_spread` TINYINT UNSIGNED NOT NULL DEFAULT 0 COMMENT 'Whether they told others',
    PRIMARY KEY (`id`),
    UNIQUE KEY `uk_gossip_bot` (`gossip_id`, `bot_guid`),
    KEY `idx_bot` (`bot_guid`),
    KEY `idx_gossip` (`gossip_id`),
    KEY `idx_learned` (`learned_at`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci
COMMENT='Tracks which bots know which gossip';

-- ============================================================================
-- BOT MEMORY TABLE
-- ============================================================================
-- Persistent bot memory including personality, combat history, and preferences.

DROP TABLE IF EXISTS `alive_world_bot_memory`;
CREATE TABLE `alive_world_bot_memory` (
    `bot_guid` BIGINT UNSIGNED NOT NULL,
    `bot_name` VARCHAR(64) NOT NULL,
    `bot_class` TINYINT UNSIGNED NOT NULL DEFAULT 0,
    `bot_race` TINYINT UNSIGNED NOT NULL DEFAULT 0,
    `bot_level` TINYINT UNSIGNED NOT NULL DEFAULT 1,

    -- Personality traits (0.0 to 1.0, stored as 0-100)
    `personality_aggression` TINYINT UNSIGNED NOT NULL DEFAULT 50,
    `personality_caution` TINYINT UNSIGNED NOT NULL DEFAULT 50,
    `personality_loyalty` TINYINT UNSIGNED NOT NULL DEFAULT 50,
    `personality_vengefulness` TINYINT UNSIGNED NOT NULL DEFAULT 30,
    `personality_helpfulness` TINYINT UNSIGNED NOT NULL DEFAULT 50,
    `personality_sociability` TINYINT UNSIGNED NOT NULL DEFAULT 50,
    `personality_curiosity` TINYINT UNSIGNED NOT NULL DEFAULT 50,
    `personality_greed` TINYINT UNSIGNED NOT NULL DEFAULT 50,
    `current_mood` VARCHAR(32) DEFAULT 'neutral',
    `mood_timestamp` BIGINT UNSIGNED DEFAULT NULL,

    -- Stats
    `total_kills` INT UNSIGNED NOT NULL DEFAULT 0,
    `total_deaths` INT UNSIGNED NOT NULL DEFAULT 0,
    `total_assists` INT UNSIGNED NOT NULL DEFAULT 0,
    `gold_earned` BIGINT UNSIGNED NOT NULL DEFAULT 0,
    `gold_spent` BIGINT UNSIGNED NOT NULL DEFAULT 0,

    -- Preferences
    `favorite_zone_id` INT UNSIGNED DEFAULT NULL,
    `current_zone_id` INT UNSIGNED DEFAULT NULL,
    `home_zone_id` INT UNSIGNED DEFAULT NULL,

    -- Current tier (1=Active, 2=Aware, 3=Passive)
    `current_tier` TINYINT UNSIGNED NOT NULL DEFAULT 3,

    -- Combat history as JSON array (last 10 encounters)
    `combat_log_json` TEXT COMMENT 'JSON array of recent combat encounters',

    -- Social connections as JSON object (guid -> relationship)
    `social_connections_json` TEXT COMMENT 'JSON object of social relationships',

    -- Zone time spent as JSON object (zone_id -> minutes)
    `zone_time_json` TEXT COMMENT 'JSON object of time spent per zone',

    -- Known gossip IDs as JSON array
    `known_gossip_json` TEXT COMMENT 'JSON array of known gossip IDs',

    -- Timestamps
    `first_seen` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    `last_seen` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
    `last_ai_request` TIMESTAMP NULL DEFAULT NULL,

    PRIMARY KEY (`bot_guid`),
    KEY `idx_name` (`bot_name`),
    KEY `idx_tier` (`current_tier`),
    KEY `idx_zone` (`current_zone_id`),
    KEY `idx_last_seen` (`last_seen`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci
COMMENT='Persistent bot memory for AliveWorld system';

-- ============================================================================
-- BOT PERSONALITY OVERRIDES TABLE
-- ============================================================================
-- Optional per-bot locks for fixed personality traits and relentless hunter
-- behavior. If no row exists for a bot, normal personality evolution applies.

DROP TABLE IF EXISTS `alive_world_bot_personality_overrides`;
CREATE TABLE `alive_world_bot_personality_overrides` (
    `bot_guid` BIGINT UNSIGNED NOT NULL,
    `is_enabled` TINYINT UNSIGNED NOT NULL DEFAULT 1,

    `lock_aggression` TINYINT UNSIGNED NOT NULL DEFAULT 0,
    `aggression` TINYINT UNSIGNED NOT NULL DEFAULT 50,

    `lock_caution` TINYINT UNSIGNED NOT NULL DEFAULT 0,
    `caution` TINYINT UNSIGNED NOT NULL DEFAULT 50,

    `lock_sociability` TINYINT UNSIGNED NOT NULL DEFAULT 0,
    `sociability` TINYINT UNSIGNED NOT NULL DEFAULT 50,

    `lock_loyalty` TINYINT UNSIGNED NOT NULL DEFAULT 0,
    `loyalty` TINYINT UNSIGNED NOT NULL DEFAULT 50,

    `lock_curiosity` TINYINT UNSIGNED NOT NULL DEFAULT 0,
    `curiosity` TINYINT UNSIGNED NOT NULL DEFAULT 50,

    `lock_vengefulness` TINYINT UNSIGNED NOT NULL DEFAULT 0,
    `vengefulness` TINYINT UNSIGNED NOT NULL DEFAULT 30,

    `lock_helpfulness` TINYINT UNSIGNED NOT NULL DEFAULT 0,
    `helpfulness` TINYINT UNSIGNED NOT NULL DEFAULT 50,

    `lock_greed` TINYINT UNSIGNED NOT NULL DEFAULT 0,
    `greed` TINYINT UNSIGNED NOT NULL DEFAULT 50,

    `lock_mood` TINYINT UNSIGNED NOT NULL DEFAULT 0,
    `fixed_mood` VARCHAR(32) DEFAULT NULL,

    -- Optional persistent hunt/camp profile
    `relentless_hunter` TINYINT UNSIGNED NOT NULL DEFAULT 0,
    `relentless_target_guid` BIGINT UNSIGNED NOT NULL DEFAULT 0,
    `hunter_pursuit_radius` FLOAT NOT NULL DEFAULT 120.0,
    `hunter_engage_radius` FLOAT NOT NULL DEFAULT 35.0,
    `hunter_reengage_cooldown_ms` INT UNSIGNED NOT NULL DEFAULT 2000,

    `updated_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,

    PRIMARY KEY (`bot_guid`),
    KEY `idx_hunter_enabled` (`relentless_hunter`, `is_enabled`),
    KEY `idx_hunter_target` (`relentless_target_guid`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci
COMMENT='Per-bot personality locks and relentless hunter profiles';

-- ============================================================================
-- BOT SOCIAL CONNECTIONS TABLE
-- ============================================================================
-- Normalized table for bot-to-player/bot relationships.

DROP TABLE IF EXISTS `alive_world_bot_social`;
CREATE TABLE `alive_world_bot_social` (
    `id` BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
    `bot_guid` BIGINT UNSIGNED NOT NULL,
    `target_guid` BIGINT UNSIGNED NOT NULL,
    `target_name` VARCHAR(64) NOT NULL,
    `target_is_bot` TINYINT UNSIGNED NOT NULL DEFAULT 0,
    `sentiment_score` SMALLINT NOT NULL DEFAULT 0 COMMENT '-100 to +100',
    `trust_level` SMALLINT NOT NULL DEFAULT 0 COMMENT '-100 to +100',
    `interaction_count` INT UNSIGNED NOT NULL DEFAULT 0,
    `is_friend` TINYINT UNSIGNED NOT NULL DEFAULT 0,
    `is_nemesis` TINYINT UNSIGNED NOT NULL DEFAULT 0,
    `last_interaction_type` VARCHAR(32) DEFAULT NULL COMMENT 'group, help, chat, combat, trade',
    `last_interaction_at` TIMESTAMP NULL DEFAULT NULL,
    `first_met_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    `first_met_zone` INT UNSIGNED DEFAULT NULL,
    PRIMARY KEY (`id`),
    UNIQUE KEY `uk_bot_target` (`bot_guid`, `target_guid`),
    KEY `idx_bot` (`bot_guid`),
    KEY `idx_target` (`target_guid`),
    KEY `idx_sentiment` (`sentiment_score`),
    KEY `idx_friend` (`is_friend`),
    KEY `idx_nemesis` (`is_nemesis`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci
COMMENT='Bot social relationships for AliveWorld system';

-- ============================================================================
-- BOT COMBAT HISTORY TABLE
-- ============================================================================
-- Normalized table for combat encounters.

DROP TABLE IF EXISTS `alive_world_bot_combat`;
CREATE TABLE `alive_world_bot_combat` (
    `id` BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
    `bot_guid` BIGINT UNSIGNED NOT NULL,
    `opponent_guid` BIGINT UNSIGNED NOT NULL,
    `opponent_name` VARCHAR(64) NOT NULL,
    `opponent_type` VARCHAR(16) NOT NULL COMMENT 'player, creature, boss, elite',
    `opponent_level` TINYINT UNSIGNED DEFAULT NULL,
    `opponent_entry` INT UNSIGNED DEFAULT NULL COMMENT 'Creature template entry',
    `was_victorious` TINYINT UNSIGNED NOT NULL,
    `damage_dealt` INT UNSIGNED NOT NULL DEFAULT 0,
    `damage_taken` INT UNSIGNED NOT NULL DEFAULT 0,
    `healing_done` INT UNSIGNED NOT NULL DEFAULT 0,
    `duration_seconds` INT UNSIGNED NOT NULL DEFAULT 0,
    `zone_id` INT UNSIGNED NOT NULL,
    `combat_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    PRIMARY KEY (`id`),
    KEY `idx_bot` (`bot_guid`),
    KEY `idx_opponent` (`opponent_guid`),
    KEY `idx_combat_at` (`combat_at`),
    KEY `idx_victorious` (`was_victorious`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci
COMMENT='Bot combat history for AliveWorld system';

-- ============================================================================
-- ZONE NAMES LOOKUP TABLE
-- ============================================================================
-- Maps zone IDs to human-readable names (extracted from AreaTable.dbc).

DROP TABLE IF EXISTS `alive_world_zone_names`;
CREATE TABLE `alive_world_zone_names` (
    `zone_id` INT UNSIGNED NOT NULL,
    `zone_name` VARCHAR(64) NOT NULL,
    `parent_zone_id` INT UNSIGNED DEFAULT NULL,
    `map_id` INT UNSIGNED NOT NULL DEFAULT 0,
    `faction` TINYINT UNSIGNED NOT NULL DEFAULT 0 COMMENT '0=neutral, 1=alliance, 2=horde',
    `level_min` TINYINT UNSIGNED NOT NULL DEFAULT 0,
    `level_max` TINYINT UNSIGNED NOT NULL DEFAULT 0,
    PRIMARY KEY (`zone_id`),
    KEY `idx_name` (`zone_name`),
    KEY `idx_map` (`map_id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci
COMMENT='Zone name lookup for AliveWorld system';

-- Populate common zones (subset - full list from AreaTable.dbc)
INSERT INTO `alive_world_zone_names` (`zone_id`, `zone_name`, `map_id`, `faction`, `level_min`, `level_max`) VALUES
(1, 'Dun Morogh', 0, 1, 1, 12),
(12, 'Elwynn Forest', 0, 1, 1, 10),
(14, 'Durotar', 1, 2, 1, 10),
(15, 'Dustwallow Marsh', 1, 0, 35, 45),
(17, 'The Barrens', 1, 2, 10, 25),
(33, 'Stranglethorn Vale', 0, 0, 30, 45),
(38, 'Loch Modan', 0, 1, 10, 20),
(40, 'Westfall', 0, 1, 10, 20),
(44, 'Redridge Mountains', 0, 1, 15, 25),
(45, 'Arathi Highlands', 0, 0, 30, 40),
(47, 'The Hinterlands', 0, 0, 40, 50),
(51, 'Searing Gorge', 0, 0, 43, 50),
(85, 'Tirisfal Glades', 0, 2, 1, 10),
(130, 'Silverpine Forest', 0, 2, 10, 20),
(139, 'Eastern Plaguelands', 0, 0, 53, 60),
(141, 'Teldrassil', 1, 1, 1, 10),
(148, 'Darkshore', 1, 1, 10, 20),
(215, 'Mulgore', 1, 2, 1, 10),
(267, 'Hillsbrad Foothills', 0, 0, 20, 30),
(331, 'Ashenvale', 1, 0, 18, 30),
(357, 'Feralas', 1, 0, 40, 50),
(361, 'Felwood', 1, 0, 48, 55),
(394, 'Grizzly Hills', 571, 0, 73, 75),
(400, 'Thousand Needles', 1, 0, 25, 35),
(405, 'Desolace', 1, 0, 30, 40),
(406, 'Stonetalon Mountains', 1, 0, 15, 27),
(440, 'Tanaris', 1, 0, 40, 50),
(490, 'Un''Goro Crater', 1, 0, 48, 55),
(493, 'Moonglade', 1, 0, 1, 80),
(618, 'Winterspring', 1, 0, 53, 60),
(1377, 'Silithus', 1, 0, 55, 60),
(1497, 'Undercity', 0, 2, 1, 80),
(1519, 'Stormwind City', 0, 1, 1, 80),
(1537, 'Ironforge', 0, 1, 1, 80),
(1637, 'Orgrimmar', 1, 2, 1, 80),
(1638, 'Thunder Bluff', 1, 2, 1, 80),
(1657, 'Darnassus', 1, 1, 1, 80),
(3430, 'Eversong Woods', 530, 2, 1, 10),
(3433, 'Ghostlands', 530, 2, 10, 20),
(3483, 'Hellfire Peninsula', 530, 0, 58, 63),
(3518, 'Nagrand', 530, 0, 64, 67),
(3519, 'Terokkar Forest', 530, 0, 62, 65),
(3521, 'Zangarmarsh', 530, 0, 60, 64),
(3522, 'Blade''s Edge Mountains', 530, 0, 65, 68),
(3523, 'Netherstorm', 530, 0, 67, 70),
(3524, 'Azuremyst Isle', 530, 1, 1, 10),
(3525, 'Bloodmyst Isle', 530, 1, 10, 20),
(3537, 'Borean Tundra', 571, 0, 68, 72),
(3711, 'Sholazar Basin', 571, 0, 76, 78),
(4080, 'Isle of Quel''Danas', 530, 0, 70, 70),
(4197, 'Wintergrasp', 571, 0, 77, 80),
(4395, 'Dalaran', 571, 0, 1, 80),
(4742, 'Hrothgar''s Landing', 571, 0, 77, 80);

-- ============================================================================
-- EMOTE REACTIONS LOG TABLE
-- ============================================================================
-- Logs emote reactions for debugging and analysis.

DROP TABLE IF EXISTS `alive_world_emote_log`;
CREATE TABLE `alive_world_emote_log` (
    `id` BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
    `bot_guid` BIGINT UNSIGNED NOT NULL,
    `bot_name` VARCHAR(64) NOT NULL,
    `emote_id` INT UNSIGNED NOT NULL,
    `emote_name` VARCHAR(32) DEFAULT NULL,
    `trigger_event_type` VARCHAR(32) NOT NULL COMMENT 'What triggered the emote',
    `trigger_subject` VARCHAR(64) DEFAULT NULL COMMENT 'Who/what was the subject',
    `zone_id` INT UNSIGNED NOT NULL,
    `personality_trait` VARCHAR(32) DEFAULT NULL COMMENT 'Which trait drove selection',
    `emoted_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    PRIMARY KEY (`id`),
    KEY `idx_bot` (`bot_guid`),
    KEY `idx_emote` (`emote_id`),
    KEY `idx_trigger` (`trigger_event_type`),
    KEY `idx_emoted_at` (`emoted_at`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci
COMMENT='Emote reaction log for AliveWorld system';

-- ============================================================================
-- BOT GOALS TABLE
-- ============================================================================
-- Stores bot goals - what they want to achieve based on personality and game data.

DROP TABLE IF EXISTS `alive_bot_goals`;
CREATE TABLE `alive_bot_goals` (
    `bot_guid` BIGINT UNSIGNED NOT NULL,
    `goals_json` MEDIUMTEXT NOT NULL COMMENT 'JSON array of BotGoal objects',
    `active_count` TINYINT UNSIGNED NOT NULL DEFAULT 0 COMMENT 'Number of active goals',
    `completed_count` INT UNSIGNED NOT NULL DEFAULT 0 COMMENT 'Total completed goals',
    `last_refresh` TIMESTAMP NULL DEFAULT NULL COMMENT 'Last time goals were refreshed',
    `updated_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
    PRIMARY KEY (`bot_guid`),
    KEY `idx_active_count` (`active_count`),
    KEY `idx_updated` (`updated_at`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci
COMMENT='Bot goals for AliveWorld Goal System';

-- ============================================================================
-- BOT GOALS HISTORY TABLE
-- ============================================================================
-- Tracks completed/abandoned goals for analytics.

DROP TABLE IF EXISTS `alive_bot_goals_history`;
CREATE TABLE `alive_bot_goals_history` (
    `id` BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
    `bot_guid` BIGINT UNSIGNED NOT NULL,
    `goal_id` INT UNSIGNED NOT NULL,
    `goal_type` TINYINT UNSIGNED NOT NULL COMMENT 'GoalType enum value',
    `goal_priority` TINYINT UNSIGNED NOT NULL COMMENT 'GoalPriority enum value',
    `goal_status` TINYINT UNSIGNED NOT NULL COMMENT 'GoalStatus enum value (completed/abandoned)',
    `target_id` INT UNSIGNED NOT NULL DEFAULT 0,
    `target_name` VARCHAR(128) DEFAULT NULL,
    `progress_final` FLOAT NOT NULL DEFAULT 0 COMMENT 'Final progress 0.0-1.0',
    `reason` VARCHAR(255) DEFAULT NULL COMMENT 'Why bot chose this goal',
    `created_at` BIGINT UNSIGNED NOT NULL COMMENT 'Unix timestamp ms',
    `completed_at` BIGINT UNSIGNED NOT NULL COMMENT 'Unix timestamp ms',
    `duration_ms` BIGINT UNSIGNED NOT NULL DEFAULT 0 COMMENT 'How long goal took',
    `logged_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    PRIMARY KEY (`id`),
    KEY `idx_bot` (`bot_guid`),
    KEY `idx_type` (`goal_type`),
    KEY `idx_status` (`goal_status`),
    KEY `idx_completed` (`completed_at`),
    KEY `idx_logged` (`logged_at`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci
COMMENT='Bot goal history for analytics';

-- ============================================================================
-- BOT ECONOMIC STATE TABLE
-- ============================================================================
-- Stores bot economic awareness and preferences.

DROP TABLE IF EXISTS `alive_bot_economic_state`;
CREATE TABLE `alive_bot_economic_state` (
    `bot_guid` BIGINT UNSIGNED NOT NULL,
    `current_gold` INT UNSIGNED NOT NULL DEFAULT 0 COMMENT 'Current gold in copper',
    `target_gold` INT UNSIGNED NOT NULL DEFAULT 0 COMMENT 'Savings goal in copper',
    `preferred_farm_category` TINYINT UNSIGNED NOT NULL DEFAULT 0 COMMENT 'ItemCategory enum',
    `last_farm_zone_id` INT UNSIGNED DEFAULT NULL,
    `last_sell_time` BIGINT UNSIGNED DEFAULT NULL COMMENT 'Unix timestamp ms',
    `last_buy_time` BIGINT UNSIGNED DEFAULT NULL COMMENT 'Unix timestamp ms',
    `greed_factor` FLOAT NOT NULL DEFAULT 1.0 COMMENT 'From personality, affects goals',
    `watched_items_json` TEXT COMMENT 'JSON array of item IDs to track',
    `known_vendors_json` TEXT COMMENT 'JSON array of vendor entries',
    `updated_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
    PRIMARY KEY (`bot_guid`),
    KEY `idx_target_gold` (`target_gold`),
    KEY `idx_farm_zone` (`last_farm_zone_id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci
COMMENT='Bot economic state for AliveWorld Economy System';

-- ============================================================================
-- ECONOMIC GOSSIP TABLE
-- ============================================================================
-- Stores market-related gossip (price changes, demand rumors).

DROP TABLE IF EXISTS `alive_economic_gossip`;
CREATE TABLE `alive_economic_gossip` (
    `gossip_id` INT UNSIGNED NOT NULL AUTO_INCREMENT,
    `item_id` INT UNSIGNED NOT NULL DEFAULT 0 COMMENT '0 for general gossip',
    `item_name` VARCHAR(128) DEFAULT NULL,
    `message` VARCHAR(512) NOT NULL,
    `price_change_percent` FLOAT NOT NULL DEFAULT 0 COMMENT 'Positive = rising',
    `reliability` TINYINT UNSIGNED NOT NULL DEFAULT 5 COMMENT '1-10 accuracy',
    `created_at` BIGINT UNSIGNED NOT NULL COMMENT 'Unix timestamp ms',
    `expires_at` BIGINT UNSIGNED NOT NULL COMMENT 'Unix timestamp ms',
    `is_active` TINYINT UNSIGNED NOT NULL DEFAULT 1,
    PRIMARY KEY (`gossip_id`),
    KEY `idx_item` (`item_id`),
    KEY `idx_active` (`is_active`),
    KEY `idx_expires` (`expires_at`),
    KEY `idx_created` (`created_at`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci
COMMENT='Economic gossip for AliveWorld Economy System';

-- ============================================================================
-- MARKET PRICE CACHE TABLE
-- ============================================================================
-- Caches simulated market prices for valuable items.

DROP TABLE IF EXISTS `alive_market_prices`;
CREATE TABLE `alive_market_prices` (
    `item_id` INT UNSIGNED NOT NULL,
    `item_name` VARCHAR(128) NOT NULL,
    `value_tier` TINYINT UNSIGNED NOT NULL DEFAULT 0 COMMENT 'ItemValueTier enum',
    `category` TINYINT UNSIGNED NOT NULL DEFAULT 0 COMMENT 'ItemCategory enum',
    `base_sell_price` INT UNSIGNED NOT NULL DEFAULT 0 COMMENT 'Vendor sell in copper',
    `estimated_market_price` INT UNSIGNED NOT NULL DEFAULT 0 COMMENT 'AH estimate in copper',
    `demand_multiplier` FLOAT NOT NULL DEFAULT 1.0 COMMENT '0.5 - 2.0 range',
    `quality` TINYINT UNSIGNED NOT NULL DEFAULT 0,
    `is_crafting_mat` TINYINT UNSIGNED NOT NULL DEFAULT 0,
    `is_bop` TINYINT UNSIGNED NOT NULL DEFAULT 0,
    `last_updated` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
    PRIMARY KEY (`item_id`),
    KEY `idx_tier` (`value_tier`),
    KEY `idx_category` (`category`),
    KEY `idx_market_price` (`estimated_market_price`),
    KEY `idx_demand` (`demand_multiplier`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci
COMMENT='Market price cache for AliveWorld Economy System';

-- ============================================================================
-- FARMING LOCATIONS TABLE
-- ============================================================================
-- Caches good farming spots derived from creature_loot_template.

DROP TABLE IF EXISTS `alive_farming_locations`;
CREATE TABLE `alive_farming_locations` (
    `id` INT UNSIGNED NOT NULL AUTO_INCREMENT,
    `zone_id` INT UNSIGNED NOT NULL,
    `zone_name` VARCHAR(64) NOT NULL,
    `area_id` INT UNSIGNED DEFAULT NULL,
    `creature_entry` INT UNSIGNED NOT NULL,
    `creature_name` VARCHAR(128) NOT NULL,
    `creature_level` TINYINT UNSIGNED NOT NULL,
    `recommended_level` TINYINT UNSIGNED NOT NULL,
    `valuable_drops_json` TEXT COMMENT 'JSON array of item IDs',
    `drop_rate_average` FLOAT NOT NULL DEFAULT 0,
    `gold_per_hour` FLOAT NOT NULL DEFAULT 0,
    `danger_level` TINYINT UNSIGNED NOT NULL DEFAULT 1 COMMENT '1-10',
    `requires_group` TINYINT UNSIGNED NOT NULL DEFAULT 0,
    `last_updated` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
    PRIMARY KEY (`id`),
    KEY `idx_zone` (`zone_id`),
    KEY `idx_creature` (`creature_entry`),
    KEY `idx_level` (`recommended_level`),
    KEY `idx_gold_per_hour` (`gold_per_hour`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci
COMMENT='Farming location cache for AliveWorld Economy System';

-- ============================================================================
-- VENDOR LOCATIONS TABLE
-- ============================================================================
-- Caches vendor information for quick lookup.

DROP TABLE IF EXISTS `alive_vendor_locations`;
CREATE TABLE `alive_vendor_locations` (
    `creature_entry` INT UNSIGNED NOT NULL,
    `vendor_name` VARCHAR(128) NOT NULL,
    `zone_id` INT UNSIGNED NOT NULL,
    `area_id` INT UNSIGNED DEFAULT NULL,
    `map_id` INT UNSIGNED NOT NULL,
    `pos_x` FLOAT NOT NULL,
    `pos_y` FLOAT NOT NULL,
    `pos_z` FLOAT NOT NULL,
    `sells_repair` TINYINT UNSIGNED NOT NULL DEFAULT 0,
    `sells_food_drink` TINYINT UNSIGNED NOT NULL DEFAULT 0,
    `sells_reagents` TINYINT UNSIGNED NOT NULL DEFAULT 0,
    `required_reputation_faction` INT UNSIGNED DEFAULT NULL,
    `required_reputation_rank` INT UNSIGNED DEFAULT NULL,
    `items_json` TEXT COMMENT 'JSON array of item IDs sold',
    `last_updated` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
    PRIMARY KEY (`creature_entry`),
    KEY `idx_zone` (`zone_id`),
    KEY `idx_map` (`map_id`),
    KEY `idx_repair` (`sells_repair`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci
COMMENT='Vendor location cache for AliveWorld Economy System';

-- ============================================================================
-- BOT TRADE HISTORY TABLE
-- ============================================================================
-- Logs cosmetic trades between bots for immersion tracking.

DROP TABLE IF EXISTS `alive_bot_trade_history`;
CREATE TABLE `alive_bot_trade_history` (
    `id` BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
    `buyer_guid` BIGINT UNSIGNED NOT NULL,
    `seller_guid` BIGINT UNSIGNED NOT NULL,
    `item_id` INT UNSIGNED NOT NULL,
    `item_name` VARCHAR(128) DEFAULT NULL,
    `price_copper` INT UNSIGNED NOT NULL DEFAULT 0,
    `zone_id` INT UNSIGNED NOT NULL,
    `traded_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    PRIMARY KEY (`id`),
    KEY `idx_buyer` (`buyer_guid`),
    KEY `idx_seller` (`seller_guid`),
    KEY `idx_item` (`item_id`),
    KEY `idx_traded` (`traded_at`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci
COMMENT='Bot trade history for AliveWorld Economy System';

-- ============================================================================
-- CLEANUP PROCEDURES
-- ============================================================================

-- Archive old events (run daily)
DROP PROCEDURE IF EXISTS `alive_world_archive_events`;
DELIMITER //
CREATE PROCEDURE `alive_world_archive_events`()
BEGIN
    DECLARE cutoff BIGINT;
    SET cutoff = UNIX_TIMESTAMP(NOW() - INTERVAL 24 HOUR) * 1000;

    -- Move old events to archive
    INSERT INTO `alive_world_events_archive`
    SELECT * FROM `alive_world_events` WHERE `timestamp` < cutoff;

    -- Delete archived events from main table
    DELETE FROM `alive_world_events` WHERE `timestamp` < cutoff;

    -- Clean up expired gossip
    UPDATE `alive_world_gossip` SET `is_active` = 0 WHERE `expires_at` < NOW() AND `is_active` = 1;

    -- Clean up old gossip knowledge for inactive gossip
    DELETE gk FROM `alive_world_gossip_knowledge` gk
    INNER JOIN `alive_world_gossip` g ON gk.gossip_id = g.gossip_id
    WHERE g.is_active = 0 AND g.expires_at < NOW() - INTERVAL 7 DAY;

    -- Delete old inactive gossip
    DELETE FROM `alive_world_gossip` WHERE `is_active` = 0 AND `expires_at` < NOW() - INTERVAL 7 DAY;

    -- Clean up old emote logs (keep 7 days)
    DELETE FROM `alive_world_emote_log` WHERE `emoted_at` < NOW() - INTERVAL 7 DAY;

    -- Clean up old combat history (keep 30 days)
    DELETE FROM `alive_world_bot_combat` WHERE `combat_at` < NOW() - INTERVAL 30 DAY;

    -- Clean up expired economic gossip
    DELETE FROM `alive_economic_gossip` WHERE `is_active` = 0 AND FROM_UNIXTIME(`expires_at` / 1000) < NOW() - INTERVAL 7 DAY;
    UPDATE `alive_economic_gossip` SET `is_active` = 0 WHERE `is_active` = 1 AND FROM_UNIXTIME(`expires_at` / 1000) < NOW();

    -- Clean up old trade history (keep 30 days)
    DELETE FROM `alive_bot_trade_history` WHERE `traded_at` < NOW() - INTERVAL 30 DAY;
END //
DELIMITER ;

-- ============================================================================
-- SCHEDULED EVENT FOR CLEANUP
-- ============================================================================
-- Run cleanup daily at 4 AM server time

CREATE EVENT IF NOT EXISTS `alive_world_daily_cleanup`
ON SCHEDULE EVERY 1 DAY
STARTS CURRENT_DATE + INTERVAL 1 DAY + INTERVAL 4 HOUR
DO CALL `alive_world_archive_events`();

-- Enable event scheduler if not already enabled
-- SET GLOBAL event_scheduler = ON;

-- ============================================================================
-- VIEWS FOR EASY QUERYING
-- ============================================================================

-- Active gossip with zone names
CREATE OR REPLACE VIEW `v_alive_world_active_gossip` AS
SELECT
    g.*,
    z.zone_name as origin_zone_name
FROM `alive_world_gossip` g
LEFT JOIN `alive_world_zone_names` z ON g.origin_zone_id = z.zone_id
WHERE g.is_active = 1
ORDER BY g.importance DESC, g.created_at DESC;

-- Legacy alias: alive_bot_memory -> alive_world_bot_memory
-- The C++ code may reference this as a fallback for older installations
CREATE OR REPLACE VIEW `alive_bot_memory` AS
SELECT
    bot_guid,
    bot_name,
    CONCAT('{}') as personality_json,  -- Legacy field, no longer used
    favorite_zone_id as preferred_zone_id
FROM `alive_world_bot_memory`;

-- Bot memory with zone names
CREATE OR REPLACE VIEW `v_alive_world_bot_memory` AS
SELECT
    bm.*,
    cz.zone_name as current_zone_name,
    fz.zone_name as favorite_zone_name,
    hz.zone_name as home_zone_name
FROM `alive_world_bot_memory` bm
LEFT JOIN `alive_world_zone_names` cz ON bm.current_zone_id = cz.zone_id
LEFT JOIN `alive_world_zone_names` fz ON bm.favorite_zone_id = fz.zone_id
LEFT JOIN `alive_world_zone_names` hz ON bm.home_zone_id = hz.zone_id;

-- Personality visibility view with override/hunter profile data
CREATE OR REPLACE VIEW `v_alive_world_bot_personality` AS
SELECT
    bm.bot_guid,
    bm.bot_name,
    bm.personality_aggression,
    bm.personality_caution,
    bm.personality_sociability,
    bm.personality_loyalty,
    bm.personality_curiosity,
    bm.personality_vengefulness,
    bm.personality_helpfulness,
    bm.personality_greed,
    bm.current_mood,
    bm.current_tier,
    IFNULL(po.is_enabled, 0) AS override_enabled,
    IFNULL(po.lock_aggression, 0) AS lock_aggression,
    IFNULL(po.lock_caution, 0) AS lock_caution,
    IFNULL(po.lock_sociability, 0) AS lock_sociability,
    IFNULL(po.lock_loyalty, 0) AS lock_loyalty,
    IFNULL(po.lock_curiosity, 0) AS lock_curiosity,
    IFNULL(po.lock_vengefulness, 0) AS lock_vengefulness,
    IFNULL(po.lock_helpfulness, 0) AS lock_helpfulness,
    IFNULL(po.lock_greed, 0) AS lock_greed,
    IFNULL(po.lock_mood, 0) AS lock_mood,
    IFNULL(po.fixed_mood, '') AS fixed_mood,
    IFNULL(po.relentless_hunter, 0) AS relentless_hunter,
    IFNULL(po.relentless_target_guid, 0) AS relentless_target_guid,
    IFNULL(po.hunter_pursuit_radius, 0) AS hunter_pursuit_radius,
    IFNULL(po.hunter_engage_radius, 0) AS hunter_engage_radius,
    IFNULL(po.hunter_reengage_cooldown_ms, 0) AS hunter_reengage_cooldown_ms
FROM `alive_world_bot_memory` bm
LEFT JOIN `alive_world_bot_personality_overrides` po ON po.bot_guid = bm.bot_guid;

-- Recent events with zone names
CREATE OR REPLACE VIEW `v_alive_world_recent_events` AS
SELECT
    e.*,
    z.zone_name
FROM `alive_world_events` e
LEFT JOIN `alive_world_zone_names` z ON e.zone_id = z.zone_id
ORDER BY e.timestamp DESC
LIMIT 1000;

-- ============================================================================
-- GRANT PERMISSIONS (adjust user as needed)
-- ============================================================================
-- GRANT SELECT, INSERT, UPDATE, DELETE ON `acore_characters`.`alive_world_*` TO 'acore'@'localhost';

SELECT 'AliveWorld persistence schema installed successfully!' AS Status;
