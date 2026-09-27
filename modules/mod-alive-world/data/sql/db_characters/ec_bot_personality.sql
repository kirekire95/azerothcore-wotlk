-- EC Bot Personality System
-- Database: acore_characters (GoalMgr uses CharacterDatabase).
-- Persists per-bot personality traits used by the Goal System
-- Traits drive hostile territory behavior, goal weighting, and future AI decisions

CREATE TABLE IF NOT EXISTS `ec_bot_personality` (
    `bot_guid` BIGINT UNSIGNED NOT NULL COMMENT 'Bot character GUID',
    `aggression` FLOAT NOT NULL DEFAULT 0.5 COMMENT '0.0-1.0: Combat engagement tendency',
    `helpfulness` FLOAT NOT NULL DEFAULT 0.5 COMMENT '0.0-1.0: Willingness to assist others',
    `chattiness` FLOAT NOT NULL DEFAULT 0.5 COMMENT '0.0-1.0: Communication frequency',
    `curiosity` FLOAT NOT NULL DEFAULT 0.5 COMMENT '0.0-1.0: Exploration tendency',
    `greed` FLOAT NOT NULL DEFAULT 0.5 COMMENT '0.0-1.0: Gold/item acquisition focus',
    `created_at` BIGINT UNSIGNED NOT NULL DEFAULT 0,
    PRIMARY KEY (`bot_guid`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;
