-- ============================================================================
-- Migration: Fixed personality locks + relentless hunter profiles
-- ============================================================================
-- 1) Extends alive_world_bot_memory with missing core traits:
--    - personality_caution
--    - personality_loyalty
--    - personality_vengefulness
-- 2) Adds alive_world_bot_personality_overrides for per-bot fixed personalities
--    and optional relentless hunter behavior.
-- ============================================================================

DROP PROCEDURE IF EXISTS `_alive_world_add_column_if_missing`;
DELIMITER //
CREATE PROCEDURE `_alive_world_add_column_if_missing`(
    IN p_table VARCHAR(64),
    IN p_column VARCHAR(64),
    IN p_definition VARCHAR(255)
)
BEGIN
    SET @tbl_exists = (
        SELECT COUNT(*) FROM information_schema.tables
        WHERE table_schema = DATABASE()
          AND table_name = p_table
    );

    IF @tbl_exists > 0 THEN
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
    END IF;
END //
DELIMITER ;

-- Add missing personality traits to bot memory table (safe for re-run).
CALL `_alive_world_add_column_if_missing`(
    'alive_world_bot_memory',
    'personality_caution',
    "TINYINT UNSIGNED NOT NULL DEFAULT 50 COMMENT 'Caution 0-100' AFTER `personality_aggression`"
);
CALL `_alive_world_add_column_if_missing`(
    'alive_world_bot_memory',
    'personality_loyalty',
    "TINYINT UNSIGNED NOT NULL DEFAULT 50 COMMENT 'Loyalty 0-100' AFTER `personality_sociability`"
);
CALL `_alive_world_add_column_if_missing`(
    'alive_world_bot_memory',
    'personality_vengefulness',
    "TINYINT UNSIGNED NOT NULL DEFAULT 30 COMMENT 'Vengefulness 0-100' AFTER `personality_curiosity`"
);

-- New per-bot override table (safe for re-run).
CREATE TABLE IF NOT EXISTS `alive_world_bot_personality_overrides` (
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

-- Visibility view for quick inspection from SQL clients.
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

DROP PROCEDURE IF EXISTS `_alive_world_add_column_if_missing`;

SELECT 'AliveWorld personality override migration complete!' AS Status;
