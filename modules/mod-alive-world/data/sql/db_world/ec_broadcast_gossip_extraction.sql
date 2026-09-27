-- ============================================================================
-- EC Broadcast Text Gossip Pattern Extraction
-- Part of mod-alive-world for AzerothCore
--
-- This script extracts gossip-like dialogue patterns from WoW's broadcast_text
-- table (9.3 MB of NPC dialogue) and converts them to gossip templates.
--
-- Patterns extracted:
--   - "Have you heard..." - News/rumors
--   - "Did you hear..." - Breaking news
--   - "I heard..." - Personal reports
--   - "Rumor has it..." - Rumors
--   - "They say..." - Hearsay
--   - "Word is..." - News updates
--   - "Word spreads..." - Spreading news
--
-- WoW placeholders are converted:
--   $n → {primary}    (player name)
--   $r → {secondary}  (race)
--   $c → {secondary}  (class)
--   $B$B → (removed)  (line breaks)
-- ============================================================================

-- Create table for extracted broadcast_text patterns
DROP TABLE IF EXISTS `ec_broadcast_gossip_patterns`;

CREATE TABLE `ec_broadcast_gossip_patterns` (
    `id` INT UNSIGNED NOT NULL AUTO_INCREMENT,
    `broadcast_text_id` INT UNSIGNED NOT NULL COMMENT 'Source broadcast_text.ID',
    `pattern_type` VARCHAR(32) NOT NULL COMMENT 'have_you_heard, i_heard, they_say, etc.',
    `original_text` TEXT NOT NULL COMMENT 'Original text from broadcast_text',
    `template_text` VARCHAR(255) NOT NULL COMMENT 'Converted template with placeholders',
    `event_type` VARCHAR(32) NOT NULL DEFAULT 'GOSSIP' COMMENT 'Target event type',
    `weight` INT UNSIGNED NOT NULL DEFAULT 5,
    `category` VARCHAR(32) NOT NULL DEFAULT 'broadcast' COMMENT 'Source category',
    `enabled` TINYINT(1) NOT NULL DEFAULT 1,
    `created_at` TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    PRIMARY KEY (`id`),
    KEY `idx_pattern_type` (`pattern_type`),
    KEY `idx_event_type` (`event_type`),
    KEY `idx_enabled` (`enabled`),
    UNIQUE KEY `uk_broadcast_id` (`broadcast_text_id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

-- ============================================================================
-- Extract "Have you heard" patterns
-- ============================================================================
INSERT IGNORE INTO `ec_broadcast_gossip_patterns`
    (`broadcast_text_id`, `pattern_type`, `original_text`, `template_text`, `event_type`, `weight`, `category`)
SELECT
    ID,
    'have_you_heard',
    COALESCE(NULLIF(MaleText, ''), FemaleText),
    -- Convert WoW placeholders to our format and truncate to 255 chars
    LEFT(
        REPLACE(
            REPLACE(
                REPLACE(
                    REPLACE(
                        REPLACE(COALESCE(NULLIF(MaleText, ''), FemaleText), '$n', '{primary}'),
                        '$N', '{primary}'
                    ),
                    '$B$B', ' '
                ),
                '$b$b', ' '
            ),
            '\n', ' '
        ),
        255
    ),
    'GOSSIP',
    8,
    'broadcast_heard'
FROM broadcast_text
WHERE (MaleText LIKE '%Have you heard%' OR FemaleText LIKE '%Have you heard%')
  AND LanguageID IN (0, 7)  -- Common or Universal
  AND LENGTH(COALESCE(NULLIF(MaleText, ''), FemaleText)) > 20
  AND LENGTH(COALESCE(NULLIF(MaleText, ''), FemaleText)) < 300
LIMIT 100;

-- ============================================================================
-- Extract "Did you hear" patterns
-- ============================================================================
INSERT IGNORE INTO `ec_broadcast_gossip_patterns`
    (`broadcast_text_id`, `pattern_type`, `original_text`, `template_text`, `event_type`, `weight`, `category`)
SELECT
    ID,
    'did_you_hear',
    COALESCE(NULLIF(MaleText, ''), FemaleText),
    LEFT(
        REPLACE(
            REPLACE(
                REPLACE(
                    REPLACE(
                        REPLACE(COALESCE(NULLIF(MaleText, ''), FemaleText), '$n', '{primary}'),
                        '$N', '{primary}'
                    ),
                    '$B$B', ' '
                ),
                '$b$b', ' '
            ),
            '\n', ' '
        ),
        255
    ),
    'GOSSIP',
    9,
    'broadcast_heard'
FROM broadcast_text
WHERE (MaleText LIKE '%Did you hear%' OR FemaleText LIKE '%Did you hear%')
  AND LanguageID IN (0, 7)
  AND LENGTH(COALESCE(NULLIF(MaleText, ''), FemaleText)) > 15
  AND LENGTH(COALESCE(NULLIF(MaleText, ''), FemaleText)) < 300
LIMIT 100;

-- ============================================================================
-- Extract "I heard" patterns
-- ============================================================================
INSERT IGNORE INTO `ec_broadcast_gossip_patterns`
    (`broadcast_text_id`, `pattern_type`, `original_text`, `template_text`, `event_type`, `weight`, `category`)
SELECT
    ID,
    'i_heard',
    COALESCE(NULLIF(MaleText, ''), FemaleText),
    LEFT(
        REPLACE(
            REPLACE(
                REPLACE(
                    REPLACE(
                        REPLACE(COALESCE(NULLIF(MaleText, ''), FemaleText), '$n', '{primary}'),
                        '$N', '{primary}'
                    ),
                    '$B$B', ' '
                ),
                '$b$b', ' '
            ),
            '\n', ' '
        ),
        255
    ),
    'GOSSIP',
    7,
    'broadcast_heard'
FROM broadcast_text
WHERE (MaleText LIKE 'I heard%' OR MaleText LIKE '% I heard%'
       OR FemaleText LIKE 'I heard%' OR FemaleText LIKE '% I heard%')
  AND LanguageID IN (0, 7)
  AND LENGTH(COALESCE(NULLIF(MaleText, ''), FemaleText)) > 15
  AND LENGTH(COALESCE(NULLIF(MaleText, ''), FemaleText)) < 300
LIMIT 100;

-- ============================================================================
-- Extract "Rumor" patterns
-- ============================================================================
INSERT IGNORE INTO `ec_broadcast_gossip_patterns`
    (`broadcast_text_id`, `pattern_type`, `original_text`, `template_text`, `event_type`, `weight`, `category`)
SELECT
    ID,
    'rumor',
    COALESCE(NULLIF(MaleText, ''), FemaleText),
    LEFT(
        REPLACE(
            REPLACE(
                REPLACE(
                    REPLACE(
                        REPLACE(COALESCE(NULLIF(MaleText, ''), FemaleText), '$n', '{primary}'),
                        '$N', '{primary}'
                    ),
                    '$B$B', ' '
                ),
                '$b$b', ' '
            ),
            '\n', ' '
        ),
        255
    ),
    'GOSSIP',
    8,
    'broadcast_rumor'
FROM broadcast_text
WHERE (MaleText LIKE '%rumor%' OR FemaleText LIKE '%rumor%')
  AND LanguageID IN (0, 7)
  AND LENGTH(COALESCE(NULLIF(MaleText, ''), FemaleText)) > 15
  AND LENGTH(COALESCE(NULLIF(MaleText, ''), FemaleText)) < 300
LIMIT 100;

-- ============================================================================
-- Extract "They say" patterns
-- ============================================================================
INSERT IGNORE INTO `ec_broadcast_gossip_patterns`
    (`broadcast_text_id`, `pattern_type`, `original_text`, `template_text`, `event_type`, `weight`, `category`)
SELECT
    ID,
    'they_say',
    COALESCE(NULLIF(MaleText, ''), FemaleText),
    LEFT(
        REPLACE(
            REPLACE(
                REPLACE(
                    REPLACE(
                        REPLACE(COALESCE(NULLIF(MaleText, ''), FemaleText), '$n', '{primary}'),
                        '$N', '{primary}'
                    ),
                    '$B$B', ' '
                ),
                '$b$b', ' '
            ),
            '\n', ' '
        ),
        255
    ),
    'GOSSIP',
    7,
    'broadcast_hearsay'
FROM broadcast_text
WHERE (MaleText LIKE '%They say%' OR MaleText LIKE '%they say%'
       OR FemaleText LIKE '%They say%' OR FemaleText LIKE '%they say%')
  AND LanguageID IN (0, 7)
  AND LENGTH(COALESCE(NULLIF(MaleText, ''), FemaleText)) > 15
  AND LENGTH(COALESCE(NULLIF(MaleText, ''), FemaleText)) < 300
LIMIT 100;

-- ============================================================================
-- Extract "Word is" / "Word has" patterns
-- ============================================================================
INSERT IGNORE INTO `ec_broadcast_gossip_patterns`
    (`broadcast_text_id`, `pattern_type`, `original_text`, `template_text`, `event_type`, `weight`, `category`)
SELECT
    ID,
    'word_is',
    COALESCE(NULLIF(MaleText, ''), FemaleText),
    LEFT(
        REPLACE(
            REPLACE(
                REPLACE(
                    REPLACE(
                        REPLACE(COALESCE(NULLIF(MaleText, ''), FemaleText), '$n', '{primary}'),
                        '$N', '{primary}'
                    ),
                    '$B$B', ' '
                ),
                '$b$b', ' '
            ),
            '\n', ' '
        ),
        255
    ),
    'GOSSIP',
    8,
    'broadcast_news'
FROM broadcast_text
WHERE (MaleText LIKE 'Word is%' OR MaleText LIKE '% word is%'
       OR MaleText LIKE 'Word has%' OR MaleText LIKE '% word has%'
       OR FemaleText LIKE 'Word is%' OR FemaleText LIKE '% word is%')
  AND LanguageID IN (0, 7)
  AND LENGTH(COALESCE(NULLIF(MaleText, ''), FemaleText)) > 15
  AND LENGTH(COALESCE(NULLIF(MaleText, ''), FemaleText)) < 300
LIMIT 50;

-- ============================================================================
-- Extract generic gossip patterns (news, reports)
-- ============================================================================
INSERT IGNORE INTO `ec_broadcast_gossip_patterns`
    (`broadcast_text_id`, `pattern_type`, `original_text`, `template_text`, `event_type`, `weight`, `category`)
SELECT
    ID,
    'news',
    COALESCE(NULLIF(MaleText, ''), FemaleText),
    LEFT(
        REPLACE(
            REPLACE(
                REPLACE(
                    REPLACE(
                        REPLACE(COALESCE(NULLIF(MaleText, ''), FemaleText), '$n', '{primary}'),
                        '$N', '{primary}'
                    ),
                    '$B$B', ' '
                ),
                '$b$b', ' '
            ),
            '\n', ' '
        ),
        255
    ),
    'GOSSIP',
    5,
    'broadcast_news'
FROM broadcast_text
WHERE (MaleText LIKE '%reported%' OR MaleText LIKE '%activity%'
       OR MaleText LIKE '%spotted%' OR MaleText LIKE '%seen in%'
       OR FemaleText LIKE '%reported%' OR FemaleText LIKE '%activity%')
  AND LanguageID IN (0, 7)
  AND LENGTH(COALESCE(NULLIF(MaleText, ''), FemaleText)) > 20
  AND LENGTH(COALESCE(NULLIF(MaleText, ''), FemaleText)) < 200
LIMIT 100;

-- ============================================================================
-- Copy extracted patterns to ec_gossip_templates for use by the system
-- ============================================================================

-- Insert broadcast patterns into main gossip templates table
-- Note: Let MySQL auto-generate IDs, skip duplicates based on template_text
INSERT IGNORE INTO `ec_gossip_templates`
    (`event_type`, `template_text`, `weight`, `min_importance`, `max_importance`,
     `is_exclamation`, `category`, `enabled`, `comment`)
SELECT
    'GOSSIP',  -- New event type for generic gossip
    p.template_text,
    p.weight,
    1,  -- min_importance
    7,  -- max_importance (gossip is casual)
    IF(p.template_text LIKE '%!%', 1, 0),  -- is_exclamation
    p.category,
    1,  -- enabled
    CONCAT('Extracted from broadcast_text ID ', p.broadcast_text_id)
FROM ec_broadcast_gossip_patterns p
WHERE p.enabled = 1
  AND LENGTH(p.template_text) > 10;

-- ============================================================================
-- Summary: Show extraction results
-- ============================================================================
SELECT
    pattern_type,
    COUNT(*) as count,
    category
FROM ec_broadcast_gossip_patterns
WHERE enabled = 1
GROUP BY pattern_type, category
ORDER BY count DESC;

SELECT CONCAT('Total broadcast patterns extracted: ', COUNT(*)) as summary
FROM ec_broadcast_gossip_patterns WHERE enabled = 1;

SELECT CONCAT('Total gossip templates now: ', COUNT(*)) as summary
FROM ec_gossip_templates WHERE enabled = 1;
