-- =============================================================================
-- EC Guard System - Custom Bounty Hunter Assassin Creatures
--
-- Custom creature range: 900100-900199 (reserved for EC Guard System NPCs)
--
-- BLOODFANG ASSASSIN (900100):
--   Undead Rogue visual (Dark Ranger model). Dispatched to hunt Alliance
--   players with maximum wanted level. Dual-wields Perdition's Blade +
--   Core Hound Tooth.
--
-- SHADOWLEAF ASSASSIN (900101):
--   Night Elf Rogue visual (Maiev/Warden model). Dispatched to hunt Horde
--   players with maximum wanted level. Dual-wields Fang of Oblivion.
--
-- Both are Elite rank, level 80, fast speed for chasing. Stats are further
-- scaled by C++ ECBountyHunterManager::ApplyStatBuff() based on wanted stars.
-- =============================================================================

-- Clean up any existing custom creatures
DELETE FROM `creature_template` WHERE `entry` BETWEEN 900100 AND 900199;
DELETE FROM `creature_template_model` WHERE `CreatureID` BETWEEN 900100 AND 900199;
DELETE FROM `creature_equip_template` WHERE `CreatureID` BETWEEN 900100 AND 900199;

-- =============================================================================
-- BLOODFANG ASSASSIN (900100) - Undead Rogue, hunts Alliance players
-- Display: Dark Ranger Clea (30071) - hooded undead female, dark leather
-- Weapons: Perdition's Blade (18816) MH + Core Hound Tooth (18805) OH
-- =============================================================================
INSERT INTO `creature_template` (
    `entry`, `name`, `subname`,
    `minlevel`, `maxlevel`,
    `faction`,
    `speed_walk`, `speed_run`,
    `BaseAttackTime`, `RangeAttackTime`,
    `unit_class`, `unit_flags`, `unit_flags2`,
    `rank`,
    `DamageModifier`, `HealthModifier`, `ManaModifier`, `ArmorModifier`,
    `type`,
    `mechanic_immune_mask`,
    `flags_extra`,
    `ScriptName`
) VALUES (
    900100, 'Bloodfang Assassin', 'Bounty Hunter',
    80, 80,
    14,                             -- Monster (hostile to all, overridden by C++)
    1.0, 1.28,                      -- 12% faster than default for chasing
    1500, 2000,                     -- Fast melee attack speed (dagger tempo)
    1, 0, 2048,                     -- Warrior class, unit_flags2=CREATURE_FLAG2_ALLOW_FAKE_COMBAT
    1,                              -- Elite rank
    10.0, 5.0, 1.0, 1.0,           -- High damage and health, normal mana/armor
    7,                              -- Humanoid
    650853615,                      -- Immune to most CC (fear, stun, root, etc.)
    0,
    ''
);

INSERT INTO `creature_template_model` (`CreatureID`, `Idx`, `CreatureDisplayID`, `DisplayScale`, `Probability`, `VerifiedBuild`)
VALUES (900100, 0, 30071, 1.0, 1.0, 0);

INSERT INTO `creature_equip_template` (`CreatureID`, `ID`, `ItemID1`, `ItemID2`, `ItemID3`, `VerifiedBuild`)
VALUES (900100, 1, 18816, 18805, 0, 0);
-- ItemID1: Perdition's Blade (Classic rogue dagger)
-- ItemID2: Core Hound Tooth (Classic rogue off-hand)

-- =============================================================================
-- SHADOWLEAF ASSASSIN (900101) - Night Elf Rogue, hunts Horde players
-- Display: Maiev Shadowsong (21349) - armored night elf warden
-- Weapons: Fang of Oblivion (45607) dual-wielded
-- =============================================================================
INSERT INTO `creature_template` (
    `entry`, `name`, `subname`,
    `minlevel`, `maxlevel`,
    `faction`,
    `speed_walk`, `speed_run`,
    `BaseAttackTime`, `RangeAttackTime`,
    `unit_class`, `unit_flags`, `unit_flags2`,
    `rank`,
    `DamageModifier`, `HealthModifier`, `ManaModifier`, `ArmorModifier`,
    `type`,
    `mechanic_immune_mask`,
    `flags_extra`,
    `ScriptName`
) VALUES (
    900101, 'Shadowleaf Assassin', 'Bounty Hunter',
    80, 80,
    14,                             -- Monster (hostile to all, overridden by C++)
    1.0, 1.28,                      -- 12% faster than default for chasing
    1500, 2000,                     -- Fast melee attack speed (dagger tempo)
    1, 0, 2048,                     -- Warrior class, unit_flags2=CREATURE_FLAG2_ALLOW_FAKE_COMBAT
    1,                              -- Elite rank
    10.0, 5.0, 1.0, 1.0,           -- High damage and health, normal mana/armor
    7,                              -- Humanoid
    650853615,                      -- Immune to most CC (fear, stun, root, etc.)
    0,
    ''
);

INSERT INTO `creature_template_model` (`CreatureID`, `Idx`, `CreatureDisplayID`, `DisplayScale`, `Probability`, `VerifiedBuild`)
VALUES (900101, 0, 21349, 1.0, 1.0, 0);

INSERT INTO `creature_equip_template` (`CreatureID`, `ID`, `ItemID1`, `ItemID2`, `ItemID3`, `VerifiedBuild`)
VALUES (900101, 1, 45607, 45607, 0, 0);
-- ItemID1: Fang of Oblivion (Ulduar rogue dagger)
-- ItemID2: Fang of Oblivion (dual-wielded)
