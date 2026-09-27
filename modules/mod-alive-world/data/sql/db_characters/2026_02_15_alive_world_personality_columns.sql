-- ============================================================================
-- Migration: Add personality columns to alive_world_bot_memory
-- ============================================================================
-- The alive_world_bot_memory table may exist from an older schema that stored
-- personality as JSON. This migration adds the individual personality trait
-- columns needed by BotMemoryManager.cpp (LoadFromDatabase / PersistToDatabase).
--
-- Columns added:
--   personality_aggression   TINYINT UNSIGNED (0-100, maps to 0.0-1.0)
--   personality_helpfulness  TINYINT UNSIGNED
--   personality_sociability  TINYINT UNSIGNED
--   personality_curiosity    TINYINT UNSIGNED
--   personality_greed        TINYINT UNSIGNED
--   current_mood             VARCHAR(32)
--   mood_timestamp           BIGINT UNSIGNED
--   bot_class                TINYINT UNSIGNED
--   bot_race                 TINYINT UNSIGNED
--   bot_level                TINYINT UNSIGNED
--   total_kills              INT UNSIGNED
--   total_deaths             INT UNSIGNED
--   total_assists            INT UNSIGNED
--   gold_earned              BIGINT UNSIGNED
--   gold_spent               BIGINT UNSIGNED
--   home_zone_id             INT UNSIGNED
--   first_seen               TIMESTAMP
--   last_ai_request          TIMESTAMP
--
-- Safe to re-run: uses IF NOT EXISTS checks via prepared statements.
-- ============================================================================

-- Helper procedure to add a column only if it doesn't exist
DROP PROCEDURE IF EXISTS `_alive_world_add_column`;
DELIMITER //
CREATE PROCEDURE `_alive_world_add_column`(
    IN p_table VARCHAR(64),
    IN p_column VARCHAR(64),
    IN p_definition VARCHAR(255)
)
BEGIN
    SET @col_exists = (
        SELECT COUNT(*) FROM information_schema.columns
        WHERE table_schema = DATABASE()
        AND table_name = p_table
        AND column_name = p_column
    );

    IF @col_exists = 0 THEN
        SET @sql = CONCAT('ALTER TABLE `', p_table, '` ADD COLUMN `', p_column, '` ', p_definition);
        PREPARE stmt FROM @sql;
        EXECUTE stmt;
        DEALLOCATE PREPARE stmt;
    END IF;
END //
DELIMITER ;

-- Only proceed if the table exists (skip if fresh install will use full schema)
SET @tbl_exists = (
    SELECT COUNT(*) FROM information_schema.tables
    WHERE table_schema = DATABASE()
    AND table_name = 'alive_world_bot_memory'
);

-- Add personality columns
SET @dummy = IF(@tbl_exists > 0, (SELECT 1 FROM (
    SELECT 1
) AS t), 0);

-- Personality trait columns
CALL `_alive_world_add_column`('alive_world_bot_memory', 'personality_aggression',
    "TINYINT UNSIGNED NOT NULL DEFAULT 50 COMMENT 'Aggression 0-100 (maps to 0.0-1.0)' AFTER `bot_name`");
CALL `_alive_world_add_column`('alive_world_bot_memory', 'personality_helpfulness',
    "TINYINT UNSIGNED NOT NULL DEFAULT 50 COMMENT 'Helpfulness 0-100' AFTER `personality_aggression`");
CALL `_alive_world_add_column`('alive_world_bot_memory', 'personality_sociability',
    "TINYINT UNSIGNED NOT NULL DEFAULT 50 COMMENT 'Sociability 0-100' AFTER `personality_helpfulness`");
CALL `_alive_world_add_column`('alive_world_bot_memory', 'personality_curiosity',
    "TINYINT UNSIGNED NOT NULL DEFAULT 50 COMMENT 'Curiosity 0-100' AFTER `personality_sociability`");
CALL `_alive_world_add_column`('alive_world_bot_memory', 'personality_greed',
    "TINYINT UNSIGNED NOT NULL DEFAULT 50 COMMENT 'Greed 0-100' AFTER `personality_curiosity`");
CALL `_alive_world_add_column`('alive_world_bot_memory', 'current_mood',
    "VARCHAR(32) DEFAULT 'neutral' AFTER `personality_greed`");
CALL `_alive_world_add_column`('alive_world_bot_memory', 'mood_timestamp',
    "BIGINT UNSIGNED DEFAULT NULL AFTER `current_mood`");

-- Class/race/level columns
CALL `_alive_world_add_column`('alive_world_bot_memory', 'bot_class',
    "TINYINT UNSIGNED NOT NULL DEFAULT 0 AFTER `bot_name`");
CALL `_alive_world_add_column`('alive_world_bot_memory', 'bot_race',
    "TINYINT UNSIGNED NOT NULL DEFAULT 0 AFTER `bot_class`");
CALL `_alive_world_add_column`('alive_world_bot_memory', 'bot_level',
    "TINYINT UNSIGNED NOT NULL DEFAULT 1 AFTER `bot_race`");

-- Stats columns
CALL `_alive_world_add_column`('alive_world_bot_memory', 'total_kills',
    "INT UNSIGNED NOT NULL DEFAULT 0");
CALL `_alive_world_add_column`('alive_world_bot_memory', 'total_deaths',
    "INT UNSIGNED NOT NULL DEFAULT 0");
CALL `_alive_world_add_column`('alive_world_bot_memory', 'total_assists',
    "INT UNSIGNED NOT NULL DEFAULT 0");
CALL `_alive_world_add_column`('alive_world_bot_memory', 'gold_earned',
    "BIGINT UNSIGNED NOT NULL DEFAULT 0");
CALL `_alive_world_add_column`('alive_world_bot_memory', 'gold_spent',
    "BIGINT UNSIGNED NOT NULL DEFAULT 0");

-- Zone columns
CALL `_alive_world_add_column`('alive_world_bot_memory', 'home_zone_id',
    "INT UNSIGNED DEFAULT NULL");

-- Timestamp columns
CALL `_alive_world_add_column`('alive_world_bot_memory', 'first_seen',
    "TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP");
CALL `_alive_world_add_column`('alive_world_bot_memory', 'last_ai_request',
    "TIMESTAMP NULL DEFAULT NULL");

-- Cleanup helper procedure
DROP PROCEDURE IF EXISTS `_alive_world_add_column`;

SELECT 'alive_world_bot_memory personality migration complete!' AS Status;
