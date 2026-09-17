-- Fix: Horde flightmasters should summon Enraged Wyvern (9297) not Enraged Gryphon (9526)
--
-- Bug: All flightmasters were using the Alliance "Enraged Gryphon" when attacked,
-- even Horde flightmasters. The Horde equivalent "Enraged Wyvern" (9297) exists
-- but wasn't being used.
--
-- This fix updates SmartAI scripts for Horde flightmasters (faction 8193 or
-- with "Wind Rider Master" subname) to summon the correct creature.
--
-- ============================================================================
-- TO UNDO THIS FIX, run the ROLLBACK section at the bottom of this file
-- ============================================================================

-- Thysta (1387) - Wind Rider Master, Grom'gol
UPDATE `smart_scripts` SET `action_param1` = 9297 WHERE `entryorguid` = 1387 AND `action_type` = 12 AND `action_param1` = 9526;

-- Krexcil (18938) - Flight Master, Horde
UPDATE `smart_scripts` SET `action_param1` = 9297 WHERE `entryorguid` = 18938 AND `action_type` = 12 AND `action_param1` = 9526;

-- Innalia (18942) - Wind Rider Master, Shattrath
UPDATE `smart_scripts` SET `action_param1` = 9297 WHERE `entryorguid` = 18942 AND `action_type` = 12 AND `action_param1` = 9526;

-- Maddix (19581) - Flight Master, Horde
UPDATE `smart_scripts` SET `action_param1` = 9297 WHERE `entryorguid` = 19581 AND `action_type` = 12 AND `action_param1` = 9526;

-- Grennik (19583) - Flight Master, Horde
UPDATE `smart_scripts` SET `action_param1` = 9297 WHERE `entryorguid` = 19583 AND `action_type` = 12 AND `action_param1` = 9526;

-- Harpax (20515) - Flight Master, Horde
UPDATE `smart_scripts` SET `action_param1` = 9297 WHERE `entryorguid` = 20515 AND `action_type` = 12 AND `action_param1` = 9526;

-- Alieshor (21766) - Flight Master, Horde
UPDATE `smart_scripts` SET `action_param1` = 9297 WHERE `entryorguid` = 21766 AND `action_type` = 12 AND `action_param1` = 9526;

-- Fhyn Leafshadow (22216) - Flight Master, Horde
UPDATE `smart_scripts` SET `action_param1` = 9297 WHERE `entryorguid` = 22216 AND `action_type` = 12 AND `action_param1` = 9526;

-- Sky-Master Maxxor (22455) - Flight Master, Horde
UPDATE `smart_scripts` SET `action_param1` = 9297 WHERE `entryorguid` = 22455 AND `action_type` = 12 AND `action_param1` = 9526;

-- Gorrim (22931) - Emerald Circle Flight Master (Neutral but Horde-aligned in Shadowmoon)
UPDATE `smart_scripts` SET `action_param1` = 9297 WHERE `entryorguid` = 22931 AND `action_type` = 12 AND `action_param1` = 9526;

-- Dyslix Silvergrub (23612) - Flight Master, Horde
UPDATE `smart_scripts` SET `action_param1` = 9297 WHERE `entryorguid` = 23612 AND `action_type` = 12 AND `action_param1` = 9526;

-- Kara Thricestar (26602) - Air Traffic Controller, Horde
UPDATE `smart_scripts` SET `action_param1` = 9297 WHERE `entryorguid` = 26602 AND `action_type` = 12 AND `action_param1` = 9526;

-- Baneflight (28615) - Flight Master, Horde (Icecrown)
UPDATE `smart_scripts` SET `action_param1` = 9297 WHERE `entryorguid` = 28615 AND `action_type` = 12 AND `action_param1` = 9526;

-- Danica Saint (28618) - Flight Master, Horde
UPDATE `smart_scripts` SET `action_param1` = 9297 WHERE `entryorguid` = 28618 AND `action_type` = 12 AND `action_param1` = 9526;

-- Gurric (28623) - Flight Master, Horde
UPDATE `smart_scripts` SET `action_param1` = 9297 WHERE `entryorguid` = 28623 AND `action_type` = 12 AND `action_param1` = 9526;

-- Maaka (28624) - Flight Master, Horde
UPDATE `smart_scripts` SET `action_param1` = 9297 WHERE `entryorguid` = 28624 AND `action_type` = 12 AND `action_param1` = 9526;

-- Grimwing (29480) - Flight Master, Horde (Ebon Hold - both factions use this but model is Horde)
UPDATE `smart_scripts` SET `action_param1` = 9297 WHERE `entryorguid` = 29480 AND `action_type` = 12 AND `action_param1` = 9526;

-- Dreadwind (31078) - Flight Master, Horde
UPDATE `smart_scripts` SET `action_param1` = 9297 WHERE `entryorguid` = 31078 AND `action_type` = 12 AND `action_param1` = 9526;

-- Also update the comment text in smart_scripts to reflect the correct creature name
UPDATE `smart_scripts` SET `comment` = REPLACE(`comment`, 'Enraged Gryphon', 'Enraged Wyvern')
WHERE `entryorguid` IN (1387, 18938, 18942, 19581, 19583, 20515, 21766, 22216, 22455, 22931, 23612, 26602, 28615, 28618, 28623, 28624, 29480, 31078)
AND `action_type` = 12 AND `action_param1` = 9297;


-- ============================================================================
-- ROLLBACK: Undo the fix (revert to Enraged Gryphon 9526)
-- ============================================================================
-- To undo, uncomment and run the following:
/*

-- Revert creature ID back to Enraged Gryphon (9526)
UPDATE `smart_scripts` SET `action_param1` = 9526 WHERE `entryorguid` = 1387 AND `action_type` = 12 AND `action_param1` = 9297;
UPDATE `smart_scripts` SET `action_param1` = 9526 WHERE `entryorguid` = 18938 AND `action_type` = 12 AND `action_param1` = 9297;
UPDATE `smart_scripts` SET `action_param1` = 9526 WHERE `entryorguid` = 18942 AND `action_type` = 12 AND `action_param1` = 9297;
UPDATE `smart_scripts` SET `action_param1` = 9526 WHERE `entryorguid` = 19581 AND `action_type` = 12 AND `action_param1` = 9297;
UPDATE `smart_scripts` SET `action_param1` = 9526 WHERE `entryorguid` = 19583 AND `action_type` = 12 AND `action_param1` = 9297;
UPDATE `smart_scripts` SET `action_param1` = 9526 WHERE `entryorguid` = 20515 AND `action_type` = 12 AND `action_param1` = 9297;
UPDATE `smart_scripts` SET `action_param1` = 9526 WHERE `entryorguid` = 21766 AND `action_type` = 12 AND `action_param1` = 9297;
UPDATE `smart_scripts` SET `action_param1` = 9526 WHERE `entryorguid` = 22216 AND `action_type` = 12 AND `action_param1` = 9297;
UPDATE `smart_scripts` SET `action_param1` = 9526 WHERE `entryorguid` = 22455 AND `action_type` = 12 AND `action_param1` = 9297;
UPDATE `smart_scripts` SET `action_param1` = 9526 WHERE `entryorguid` = 22931 AND `action_type` = 12 AND `action_param1` = 9297;
UPDATE `smart_scripts` SET `action_param1` = 9526 WHERE `entryorguid` = 23612 AND `action_type` = 12 AND `action_param1` = 9297;
UPDATE `smart_scripts` SET `action_param1` = 9526 WHERE `entryorguid` = 26602 AND `action_type` = 12 AND `action_param1` = 9297;
UPDATE `smart_scripts` SET `action_param1` = 9526 WHERE `entryorguid` = 28615 AND `action_type` = 12 AND `action_param1` = 9297;
UPDATE `smart_scripts` SET `action_param1` = 9526 WHERE `entryorguid` = 28618 AND `action_type` = 12 AND `action_param1` = 9297;
UPDATE `smart_scripts` SET `action_param1` = 9526 WHERE `entryorguid` = 28623 AND `action_type` = 12 AND `action_param1` = 9297;
UPDATE `smart_scripts` SET `action_param1` = 9526 WHERE `entryorguid` = 28624 AND `action_type` = 12 AND `action_param1` = 9297;
UPDATE `smart_scripts` SET `action_param1` = 9526 WHERE `entryorguid` = 29480 AND `action_type` = 12 AND `action_param1` = 9297;
UPDATE `smart_scripts` SET `action_param1` = 9526 WHERE `entryorguid` = 31078 AND `action_type` = 12 AND `action_param1` = 9297;

-- Revert comment text back to Enraged Gryphon
UPDATE `smart_scripts` SET `comment` = REPLACE(`comment`, 'Enraged Wyvern', 'Enraged Gryphon')
WHERE `entryorguid` IN (1387, 18938, 18942, 19581, 19583, 20515, 21766, 22216, 22455, 22931, 23612, 26602, 28615, 28618, 28623, 28624, 29480, 31078)
AND `action_type` = 12 AND `action_param1` = 9526;

*/
