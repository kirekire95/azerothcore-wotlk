-- ============================================================================
-- EC Bot Goals Table
-- ============================================================================
-- Database: acore_characters (all C++ access uses CharacterDatabase).
-- This file lives in mod-playerbots/data/sql/characters so AzerothCore's
-- module updater installs it into the same database used at runtime.
--
-- Stores bot goals - what they're pursuing (items, reputation, gold, etc.)
-- Goals are personality-driven and persist across server restarts.
--
-- Used by:
--   C++ GoalRepository/GoalMgr (mod-playerbots) - CharacterDatabase CRUD
--   TS GoalSystem (wow-control-backend) - reads/writes via SQL queries
-- ============================================================================

CREATE TABLE IF NOT EXISTS `ec_bot_goals` (
    `goal_id` INT UNSIGNED NOT NULL AUTO_INCREMENT,
    `bot_guid` BIGINT UNSIGNED NOT NULL COMMENT 'Bot character GUID',
    `goal_type` TINYINT UNSIGNED NOT NULL COMMENT '1=Item, 2=Rep, 3=Achievement, 4=Explore, 5=Profession, 6=Gold, 7=Level, 8=PvP, 9=Social',
    `priority` TINYINT UNSIGNED NOT NULL DEFAULT 3 COMMENT '1=Critical, 2=High, 3=Medium, 4=Low, 5=Passive',
    `status` TINYINT UNSIGNED NOT NULL DEFAULT 1 COMMENT '1=Active, 2=Paused, 3=Completed, 4=Abandoned, 5=Blocked',
    `target_id` INT UNSIGNED NOT NULL DEFAULT 0 COMMENT 'ItemID, FactionID, ZoneID, etc.',
    `target_name` VARCHAR(128) NOT NULL DEFAULT '' COMMENT 'Human-readable target name',
    `progress` FLOAT NOT NULL DEFAULT 0 COMMENT '0.0 - 1.0 completion percentage',
    `progress_current` INT UNSIGNED NOT NULL DEFAULT 0 COMMENT 'Current value (e.g., rep gained)',
    `progress_target` INT UNSIGNED NOT NULL DEFAULT 1 COMMENT 'Target value needed',
    `zone_id` INT UNSIGNED DEFAULT NULL COMMENT 'Zone where goal can be completed (optional)',
    `personality_weight` FLOAT NOT NULL DEFAULT 0.5 COMMENT 'How well this matches bot personality (0.0 to 1.0)',
    `created_at` BIGINT UNSIGNED NOT NULL DEFAULT 0 COMMENT 'Unix timestamp ms',
    `updated_at` BIGINT UNSIGNED NOT NULL DEFAULT 0 COMMENT 'Last progress update',
    `completed_at` BIGINT UNSIGNED NOT NULL DEFAULT 0 COMMENT 'Completion timestamp (0 if not done)',
    `reason` VARCHAR(255) NOT NULL DEFAULT '' COMMENT 'Why bot chose this goal',
    `notes` TEXT DEFAULT NULL COMMENT 'Additional context',
    PRIMARY KEY (`goal_id`),
    KEY `idx_bot_guid` (`bot_guid`),
    KEY `idx_status` (`status`),
    KEY `idx_goal_type` (`goal_type`),
    KEY `idx_bot_status` (`bot_guid`, `status`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

-- ============================================================================
-- Goal Type Reference:
--   1 = ITEM        - Acquire specific item (gear, crafting mat)
--   2 = REPUTATION  - Gain reputation with a faction
--   3 = ACHIEVEMENT - Complete an achievement
--   4 = EXPLORE     - Visit a specific zone
--   5 = PROFESSION  - Learn recipe or level profession
--   6 = GOLD        - Accumulate gold
--   7 = LEVEL       - Gain experience/levels
--   8 = PVP         - PvP goals (honor, kills)
--   9 = SOCIAL      - Social goals (friends, guild)
--
-- Priority Reference:
--   1 = CRITICAL - Must do immediately
--   2 = HIGH     - Very important
--   3 = MEDIUM   - Normal priority
--   4 = LOW      - Nice to have
--   5 = PASSIVE  - Background goal
--
-- Status Reference:
--   1 = ACTIVE    - Currently pursuing
--   2 = PAUSED    - Temporarily paused
--   3 = COMPLETED - Successfully finished
--   4 = ABANDONED - Given up
--   5 = BLOCKED   - Cannot progress
-- ============================================================================
