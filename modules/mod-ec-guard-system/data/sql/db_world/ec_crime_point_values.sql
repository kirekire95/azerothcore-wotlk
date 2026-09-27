-- ============================================================================
-- EC Guard System - Crime Point Values
-- Part of mod-ec-guard-system for AzerothCore
--
-- Defines how many crime points each type of crime awards.
-- These values determine how quickly players escalate through wanted levels.
--
-- NOTE: Escalation thresholds (points needed for each star level) are in
-- the config file as they're core system settings.
-- ============================================================================

DROP TABLE IF EXISTS `ec_crime_point_values`;

CREATE TABLE `ec_crime_point_values` (
    `crime_type` VARCHAR(50) NOT NULL COMMENT 'Crime identifier',
    `base_points` INT UNSIGNED NOT NULL COMMENT 'Base crime points awarded',
    `cooldown_ms` INT UNSIGNED NOT NULL DEFAULT 0 COMMENT 'Cooldown before same crime adds points again (0=no cooldown)',
    `stacks` TINYINT(1) NOT NULL DEFAULT 1 COMMENT 'Whether repeated crimes stack points',
    `severity` ENUM('MINOR', 'MODERATE', 'SERIOUS', 'SEVERE', 'EXTREME') NOT NULL DEFAULT 'MODERATE',
    `triggers_guard_response` TINYINT(1) NOT NULL DEFAULT 1 COMMENT 'Whether this crime alerts nearby guards',
    `witness_multiplier` FLOAT NOT NULL DEFAULT 1.0 COMMENT 'Multiplier when witnessed by civilians',
    `description` VARCHAR(255) DEFAULT NULL,
    PRIMARY KEY (`crime_type`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

-- ============================================================================
-- Crime Point Values
-- ============================================================================

INSERT INTO `ec_crime_point_values` (`crime_type`, `base_points`, `cooldown_ms`, `stacks`, `severity`, `triggers_guard_response`, `witness_multiplier`, `description`) VALUES
-- Civilian crimes
('CIVILIAN_ATTACK', 50, 5000, 1, 'MODERATE', 1, 1.5, 'Attacking a civilian NPC'),
('CIVILIAN_KILL', 100, 0, 1, 'SERIOUS', 1, 2.0, 'Killing a civilian NPC'),

-- Guard crimes (more severe)
('GUARD_ATTACK', 150, 3000, 1, 'SERIOUS', 1, 1.0, 'Attacking a guard'),
('GUARD_KILL', 300, 0, 1, 'SEVERE', 1, 1.0, 'Killing a guard'),

-- Elite/special guard crimes
('ELITE_GUARD_ATTACK', 250, 3000, 1, 'SEVERE', 1, 1.0, 'Attacking an elite guard'),
('ELITE_GUARD_KILL', 500, 0, 1, 'EXTREME', 1, 1.0, 'Killing an elite guard'),

-- Property crimes
('THEFT_MINOR', 25, 10000, 1, 'MINOR', 0, 1.2, 'Minor theft (under 1g value)'),
('THEFT_MAJOR', 75, 10000, 1, 'MODERATE', 1, 1.5, 'Major theft (over 1g value)'),
('VANDALISM', 30, 15000, 1, 'MINOR', 0, 1.0, 'Destroying property'),
('TRESPASSING', 20, 30000, 0, 'MINOR', 0, 1.0, 'Entering restricted area'),

-- Combat crimes
('ASSAULT_PLAYER', 40, 10000, 1, 'MODERATE', 0, 1.0, 'Attacking another player unprovoked'),
('MURDER_PLAYER', 80, 0, 1, 'SERIOUS', 0, 1.5, 'Killing another player'),

-- Faction-specific crimes
('FACTION_BETRAYAL', 200, 0, 0, 'SEVERE', 1, 1.0, 'Attacking own faction NPCs'),
('ENEMY_FACTION_AID', 50, 60000, 0, 'MODERATE', 1, 1.0, 'Aiding enemy faction'),

-- Special crimes
('FLEEING_ARREST', 100, 0, 1, 'SERIOUS', 1, 1.0, 'Running from guards during arrest'),
('RESISTING_ARREST', 75, 5000, 1, 'MODERATE', 1, 1.0, 'Fighting back during arrest'),
('JAILBREAK', 400, 0, 0, 'EXTREME', 1, 1.0, 'Escaping from jail'),
('BOUNTY_HUNTER_KILL', 350, 0, 1, 'EXTREME', 1, 1.0, 'Killing a bounty hunter');

-- ============================================================================
-- Zone-specific multipliers (optional - can be joined with zone config)
-- Some zones might have stricter or more lenient crime enforcement
-- ============================================================================

DROP TABLE IF EXISTS `ec_crime_zone_multipliers`;

CREATE TABLE `ec_crime_zone_multipliers` (
    `zone_id` INT UNSIGNED NOT NULL COMMENT 'Zone ID',
    `crime_type` VARCHAR(50) NOT NULL COMMENT 'Crime type or * for all',
    `point_multiplier` FLOAT NOT NULL DEFAULT 1.0 COMMENT 'Multiplier for crime points in this zone',
    `response_multiplier` FLOAT NOT NULL DEFAULT 1.0 COMMENT 'Multiplier for guard response speed',
    `description` VARCHAR(255) DEFAULT NULL,
    PRIMARY KEY (`zone_id`, `crime_type`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

-- Example zone multipliers
INSERT INTO `ec_crime_zone_multipliers` (`zone_id`, `crime_type`, `point_multiplier`, `response_multiplier`, `description`) VALUES
-- Stormwind (zone 1519) - strict enforcement
(1519, '*', 1.2, 1.5, 'Stormwind City - Heavy guard presence'),
-- Goldshire (zone 87) - moderate
(87, '*', 1.0, 1.0, 'Goldshire - Standard enforcement'),
-- Booty Bay (zone 35) - lenient (neutral)
(35, '*', 0.7, 0.5, 'Booty Bay - Bruisers are slow to respond'),
-- Orgrimmar (zone 1637) - strict
(1637, '*', 1.2, 1.5, 'Orgrimmar - Heavy grunt presence'),
-- Ratchet (zone 14) - very lenient
(14, '*', 0.5, 0.3, 'Ratchet - Goblin neutrality, minimal enforcement');
