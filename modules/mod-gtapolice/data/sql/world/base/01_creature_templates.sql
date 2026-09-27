-- =====================================================
-- mod-gtapolice: Creature Templates
-- AzerothCore uses separate creature_template_model table
-- =====================================================

-- Clean up existing entries
DELETE FROM `creature_template` WHERE `entry` IN (90010, 90011, 90012);
DELETE FROM `creature_template_model` WHERE `CreatureID` IN (90010, 90011, 90012);

-- Police Officer NPC template (scanner - detects wanted players, dispatches cars)
INSERT INTO `creature_template`
(entry, name, subname, minlevel, maxlevel, faction, npcflag, unit_class, unit_flags, type, type_flags, speed_walk, speed_run, BaseAttackTime, RangeAttackTime, mingold, maxgold, AIName, ScriptName)
VALUES
(90010, 'Police Officer', 'Law Enforcement', 80, 80, 35, 0, 1, 0, 7, 0, 1.0, 1.14286, 2000, 2000, 0, 0, '', 'npc_gtapolice_officer');

-- Model for Police Officer: 3167 (Stormwind Guard - plate armor, blue tabard)
INSERT INTO `creature_template_model` (CreatureID, Idx, CreatureDisplayID, DisplayScale, Probability)
VALUES (90010, 0, 3167, 1.0, 1.0);

-- Police Car (creature used by police pursuit)
-- SAFE DEFAULT: VehicleId=0 prevents client freeze if custom Vehicle.dbc entry 9001 is missing.
-- To enable full vehicle transport, install custom Vehicle.dbc (entry 9001) and then:
--   UPDATE creature_template SET VehicleId = 9001 WHERE entry = 90011;
INSERT INTO `creature_template`
(entry, name, subname, minlevel, maxlevel, faction, npcflag, unit_class, unit_flags, type, type_flags, speed_walk, speed_run, BaseAttackTime, RangeAttackTime, mingold, maxgold, AIName, ScriptName, VehicleId)
VALUES
(90011, 'Police Car', 'LSPD', 80, 80, 16, 0, 1, 0, 7, 0, 1.0, 2.0, 2000, 2000, 0, 0, '', 'npc_gtapolice_car', 0);

-- Model for Police Car: 25871 (Mekgineer's Chopper - mechanical vehicle appearance)
INSERT INTO `creature_template_model` (CreatureID, Idx, CreatureDisplayID, DisplayScale, Probability)
VALUES (90011, 0, 25871, 1.0, 1.0);

-- Crew Officer NPC template (rides in car, exits, approaches on foot, arrests)
INSERT INTO `creature_template`
(entry, name, subname, minlevel, maxlevel, faction, npcflag, unit_class, unit_flags, type, type_flags, speed_walk, speed_run, BaseAttackTime, RangeAttackTime, mingold, maxgold, AIName, ScriptName)
VALUES
(90012, 'Police Officer', 'LSPD', 80, 80, 16, 0, 1, 0, 7, 0, 1.0, 1.71429, 2000, 2000, 0, 0, '', 'npc_gtapolice_crew');

-- Model for Crew Officer: 3167 (Stormwind Guard - same as scanner officer)
INSERT INTO `creature_template_model` (CreatureID, Idx, CreatureDisplayID, DisplayScale, Probability)
VALUES (90012, 0, 3167, 1.0, 1.0);

-- =====================================================
-- Spawn Police Officers across all major cities
-- Officer entry 90010 = scanner AI that detects wanted players
-- Guid range: 9001001 - 9001030
-- =====================================================

DELETE FROM `creature` WHERE `guid` BETWEEN 9001001 AND 9001030;

-- ---------------------------------------------------------
-- ALLIANCE CITIES
-- ---------------------------------------------------------

-- Stormwind (map 0)
INSERT INTO `creature`
(guid, id1, map, zoneId, areaId, spawnMask, phaseMask, equipment_id,
 position_x, position_y, position_z, orientation, spawntimesecs, wander_distance, MovementType)
VALUES
-- SW: Gates / Valley of Heroes
(9001001, 90010, 0, 0, 0, 1, 1, 0,
 -8833.0, 630.0, 95.0, 0.0, 300, 5, 1),
-- SW: Trade District (center)
(9001002, 90010, 0, 0, 0, 1, 1, 0,
 -8670.0, 634.0, 97.0, 3.14, 300, 5, 1),
-- SW: Cathedral Square
(9001003, 90010, 0, 0, 0, 1, 1, 0,
 -8525.0, 840.0, 109.0, 1.5, 300, 5, 1),
-- SW: Old Town
(9001004, 90010, 0, 0, 0, 1, 1, 0,
 -8690.0, 368.0, 101.0, 4.7, 300, 5, 1),

-- Ironforge (map 0)
-- IF: The Commons (AH area)
(9001005, 90010, 0, 0, 0, 1, 1, 0,
 -4918.0, -940.0, 501.5, 2.0, 300, 5, 1),
-- IF: Military Ward
(9001006, 90010, 0, 0, 0, 1, 1, 0,
 -5038.0, -815.0, 495.0, 0.5, 300, 5, 1),
-- IF: Mystic Ward
(9001007, 90010, 0, 0, 0, 1, 1, 0,
 -4628.0, -1049.0, 502.0, 5.0, 300, 5, 1),

-- Darnassus (map 1)
-- Darn: Temple of the Moon
(9001008, 90010, 1, 0, 0, 1, 1, 0,
 9951.0, 2490.0, 1316.0, 4.0, 300, 5, 1),
-- Darn: Tradesman's Terrace
(9001009, 90010, 1, 0, 0, 1, 1, 0,
 10118.0, 2313.0, 1329.0, 1.2, 300, 5, 1),

-- Exodar (map 530)
-- Exodar: Seat of the Naaru
(9001010, 90010, 530, 0, 0, 1, 1, 0,
 -4013.0, -11895.0, -1.3, 2.0, 300, 5, 1),

-- ---------------------------------------------------------
-- HORDE CITIES
-- ---------------------------------------------------------

-- Orgrimmar (map 1)
-- Org: Valley of Strength (entrance)
(9001011, 90010, 1, 0, 0, 1, 1, 0,
 1629.0, -4373.0, 31.0, 3.5, 300, 5, 1),
-- Org: The Drag
(9001012, 90010, 1, 0, 0, 1, 1, 0,
 1774.0, -4320.0, 25.0, 1.0, 300, 5, 1),
-- Org: Valley of Honor
(9001013, 90010, 1, 0, 0, 1, 1, 0,
 1898.0, -4485.0, 27.0, 5.0, 300, 5, 1),

-- Undercity (map 0)
-- UC: Trade Quarter
(9001014, 90010, 0, 0, 0, 1, 1, 0,
 1586.0, 239.0, -52.0, 3.14, 300, 5, 1),
-- UC: War Quarter
(9001015, 90010, 0, 0, 0, 1, 1, 0,
 1556.0, 346.0, -52.0, 0.5, 300, 5, 1),

-- Thunder Bluff (map 1)
-- TB: High Rise
(9001016, 90010, 1, 0, 0, 1, 1, 0,
 -1290.0, 159.0, 129.0, 2.0, 300, 5, 1),
-- TB: Spirit Rise
(9001017, 90010, 1, 0, 0, 1, 1, 0,
 -1045.0, 26.0, 69.0, 4.5, 300, 5, 1),

-- Silvermoon City (map 530)
-- SMC: Bazaar
(9001018, 90010, 530, 0, 0, 1, 1, 0,
 9478.0, -7294.0, 14.3, 3.0, 300, 5, 1),

-- ---------------------------------------------------------
-- NEUTRAL CITIES
-- ---------------------------------------------------------

-- Dalaran (map 571)
-- Dal: Magus Commerce Exchange
(9001019, 90010, 571, 0, 0, 1, 1, 0,
 5807.0, 629.0, 647.0, 1.0, 300, 5, 1),
-- Dal: Runeweaver Square
(9001020, 90010, 571, 0, 0, 1, 1, 0,
 5772.0, 558.0, 647.0, 3.5, 300, 5, 1),

-- Shattrath (map 530)
-- Shatt: Lower City
(9001021, 90010, 530, 0, 0, 1, 1, 0,
 -1860.0, 5420.0, -12.0, 1.5, 300, 5, 1),
-- Shatt: Terrace of Light
(9001022, 90010, 530, 0, 0, 1, 1, 0,
 -1838.0, 5482.0, -12.0, 4.0, 300, 5, 1),

-- Booty Bay (map 0)
-- BB: Docks
(9001023, 90010, 0, 0, 0, 1, 1, 0,
 -14438.0, 476.0, 15.0, 2.0, 300, 5, 1),

-- Gadgetzan (map 1)
-- Gadget: Center
(9001024, 90010, 1, 0, 0, 1, 1, 0,
 -7134.0, -3785.0, 8.0, 0.5, 300, 5, 1);

-- =====================================================
-- PAY-N-SPRAY NPC (entry 90015)
-- Shady vendor who clears wanted level for gold
-- =====================================================

DELETE FROM `creature_template` WHERE `entry` = 90015;
DELETE FROM `creature_template_model` WHERE `CreatureID` = 90015;

-- Pay-n-Spray NPC template (gossip vendor)
INSERT INTO `creature_template`
(entry, name, subname, minlevel, maxlevel, faction, npcflag, unit_class, unit_flags, type, type_flags, speed_walk, speed_run, BaseAttackTime, RangeAttackTime, mingold, maxgold, AIName, ScriptName)
VALUES
(90015, 'Shifty Pete', 'Fixer', 60, 60, 35, 1, 1, 0, 7, 0, 1.0, 1.14286, 2000, 2000, 0, 0, '', 'npc_gtapolice_paynspray');

-- Model for Pay-n-Spray: 11141 (Defias Thug - shady human male)
INSERT INTO `creature_template_model` (CreatureID, Idx, CreatureDisplayID, DisplayScale, Probability)
VALUES (90015, 0, 11141, 1.0, 1.0);

-- =====================================================
-- SPAWN PAY-N-SPRAY NPCs (hidden locations in major cities)
-- Guid range: 9001031 - 9001040
-- =====================================================

DELETE FROM `creature` WHERE `guid` BETWEEN 9001031 AND 9001040;

INSERT INTO `creature`
(guid, id1, map, zoneId, areaId, spawnMask, phaseMask, equipment_id,
 position_x, position_y, position_z, orientation, spawntimesecs, wander_distance, MovementType)
VALUES
-- Stormwind: Cut-Throat Alley (hidden alley near docks)
(9001031, 90015, 0, 0, 0, 1, 1, 0,
 -8644.0, 444.0, 99.0, 0.5, 300, 0, 0),
-- Orgrimmar: Cleft of Shadow (dark corner)
(9001032, 90015, 1, 0, 0, 1, 1, 0,
 1651.0, -4229.0, 36.0, 4.5, 300, 0, 0),
-- Undercity: Rogues' Quarter
(9001033, 90015, 0, 0, 0, 1, 1, 0,
 1506.0, 55.0, -62.0, 2.5, 300, 0, 0),
-- Ironforge: Forlorn Cavern (dark area)
(9001034, 90015, 0, 0, 0, 1, 1, 0,
 -4861.0, -1028.0, 502.0, 1.0, 300, 0, 0),
-- Booty Bay: Back alley behind inn
(9001035, 90015, 0, 0, 0, 1, 1, 0,
 -14456.0, 509.0, 26.0, 3.14, 300, 0, 0),
-- Dalaran: Sewers (Underbelly)
(9001036, 90015, 571, 0, 0, 1, 1, 0,
 5803.0, 700.0, 657.0, 0.0, 300, 0, 0),
-- Shattrath: Lower City (dark corner)
(9001037, 90015, 530, 0, 0, 1, 1, 0,
 -1795.0, 5388.0, -12.0, 5.5, 300, 0, 0),
-- Gadgetzan: Behind the inn
(9001038, 90015, 1, 0, 0, 1, 1, 0,
 -7177.0, -3805.0, 8.5, 2.5, 300, 0, 0);
