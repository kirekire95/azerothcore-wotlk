-- Ensure ec_bot_goals.goal_id is AUTO_INCREMENT to match GoalRepository inserts.
-- Database: acore_characters (GoalRepository uses CharacterDatabase).
-- Safe to run multiple times.

SET @tbl_exists := (
    SELECT COUNT(*)
    FROM information_schema.tables
    WHERE table_schema = DATABASE()
      AND table_name = 'ec_bot_goals'
);

SET @needs_ai := (
    SELECT COUNT(*)
    FROM information_schema.columns
    WHERE table_schema = DATABASE()
      AND table_name = 'ec_bot_goals'
      AND column_name = 'goal_id'
      AND extra NOT LIKE '%auto_increment%'
);

SET @sql := IF(
    @tbl_exists = 1 AND @needs_ai = 1,
    'ALTER TABLE `ec_bot_goals` MODIFY COLUMN `goal_id` INT UNSIGNED NOT NULL AUTO_INCREMENT',
    'SELECT ''ec_bot_goals.goal_id already AUTO_INCREMENT or table missing'' AS info'
);

PREPARE stmt FROM @sql;
EXECUTE stmt;
DEALLOCATE PREPARE stmt;
