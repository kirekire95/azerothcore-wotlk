-- Add manual_only column to personality templates table (idempotent)
SET @col_exists = (SELECT COUNT(*) FROM information_schema.columns
  WHERE table_schema = DATABASE()
  AND table_name = 'mod_ollama_chat_personality_templates'
  AND column_name = 'manual_only');

SET @sql = IF(@col_exists = 0,
  'ALTER TABLE `mod_ollama_chat_personality_templates` ADD COLUMN `manual_only` TINYINT(1) NOT NULL DEFAULT 0 AFTER `prompt`',
  'SELECT 1');

PREPARE stmt FROM @sql;
EXECUTE stmt;
DEALLOCATE PREPARE stmt;
