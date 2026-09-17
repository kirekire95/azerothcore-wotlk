-- ==========================================================================
-- Fix Flight Master Guard Issues
-- ==========================================================================
--
-- Issue 1: Enraged Wyvern (9297) and Enraged Hippogryph (9527) are NOT elite
--          but should be (like Enraged Gryphon 9526 which IS elite)
--
-- Issue 2: Spirit of Gnomeregan and Gryfe incorrectly summon BOTH mounts
--
-- ==========================================================================

-- ============================================================================
-- FIX 1: Set Enraged Wyvern and Hippogryph to ELITE rank
-- ============================================================================
-- Enraged Gryphon (9526) is already rank=1 (elite)
-- Enraged Wyvern (9297) is rank=0 (normal) - WRONG, should be elite
-- Enraged Hippogryph (9527) is rank=0 (normal) - WRONG, should be elite

UPDATE `creature_template` SET `rank` = 1 WHERE `entry` = 9297;  -- Enraged Wyvern
UPDATE `creature_template` SET `rank` = 1 WHERE `entry` = 9527;  -- Enraged Hippogryph

-- ============================================================================
-- FIX 2: Remove duplicate Wyvern summons from Alliance NPCs
-- ============================================================================
-- Bug: Some Alliance NPCs were copy-pasted with scripts that summon both
-- Enraged Gryphon (9526) AND Enraged Wyvern (9297) simultaneously.

-- Spirit of Gnomeregan (28037) - Alliance dungeon, should only summon Gryphon
DELETE FROM `smart_scripts` WHERE `entryorguid` = 28037 AND `source_type` = 0 AND `id` = 1 AND `action_param1` = 9297;

-- Gryfe (10583) - Wintergarde Keep Alliance base, should only summon Gryphon
DELETE FROM `smart_scripts` WHERE `entryorguid` = 10583 AND `source_type` = 0 AND `id` = 1 AND `action_param1` = 9297;


-- ============================================================================
-- ROLLBACK: Undo all fixes
-- ============================================================================
/*
-- Revert elite rank
UPDATE `creature_template` SET `rank` = 0 WHERE `entry` = 9297;  -- Enraged Wyvern
UPDATE `creature_template` SET `rank` = 0 WHERE `entry` = 9527;  -- Enraged Hippogryph

-- Re-insert deleted SmartAI scripts
INSERT INTO `smart_scripts` VALUES
(28037,0,1,0,61,0,100,0,0,0,0,0,0,0,12,9297,4,30000,0,0,0,1,0,0,0,0,0,0,0,0,'The Spirit of Gnomeregan - On Aggro - Summon Creature \'Enraged Wyvern\''),
(10583,0,1,0,61,0,100,0,0,0,0,0,0,0,12,9297,4,30000,0,0,0,1,0,0,0,0,0,0,0,0,'Gryfe - On Aggro - Summon Creature \'Enraged Wyvern\'');
*/
