-- ============================================================================
-- EC Guard System - Zone Heat Thresholds
-- Part of mod-ec-guard-system for AzerothCore
--
-- Defines the heat thresholds that trigger different zone phases.
-- Heat accumulates from crimes and decays over time.
--
-- Zone Phases:
-- - CALM: Normal state, regular patrols
-- - UNEASY: Guards more alert, increased patrols
-- - MOBILIZING: Guards actively searching, checkpoints appear
-- - IMPACT: Full lockdown, elite guards deployed
-- - MARTIAL_LAW: Maximum response, bounty hunters active
--
-- NOTE: Heat decay RATES and INTERVALS belong in .conf (performance tuning).
-- These THRESHOLDS are game balance and belong in SQL.
-- ============================================================================

DROP TABLE IF EXISTS `ec_zone_heat_thresholds`;

CREATE TABLE `ec_zone_heat_thresholds` (
    `phase_id` TINYINT UNSIGNED NOT NULL COMMENT 'Phase order (0-4)',
    `phase_name` VARCHAR(50) NOT NULL COMMENT 'Phase identifier',
    `heat_threshold` INT UNSIGNED NOT NULL COMMENT 'Heat points to enter this phase',
    `guard_multiplier` FLOAT NOT NULL DEFAULT 1.0 COMMENT 'Guard spawn multiplier',
    `patrol_frequency` FLOAT NOT NULL DEFAULT 1.0 COMMENT 'Patrol frequency multiplier',
    `detection_range` FLOAT NOT NULL DEFAULT 1.0 COMMENT 'Detection range multiplier',
    `response_speed` FLOAT NOT NULL DEFAULT 1.0 COMMENT 'Response speed multiplier',
    `elite_chance` FLOAT NOT NULL DEFAULT 0.0 COMMENT 'Chance to spawn elite guards (0-1)',
    `bounty_hunter_enabled` TINYINT(1) NOT NULL DEFAULT 0 COMMENT 'Can spawn bounty hunters',
    `ambient_dialogue` TINYINT(1) NOT NULL DEFAULT 1 COMMENT 'NPCs comment on zone state',
    `civilian_behavior` ENUM('NORMAL', 'CAUTIOUS', 'FEARFUL', 'HIDING', 'EVACUATED') NOT NULL DEFAULT 'NORMAL',
    `music_intensity` TINYINT UNSIGNED NOT NULL DEFAULT 0 COMMENT 'Music tension level (0-4)',
    `description` TEXT,
    PRIMARY KEY (`phase_id`),
    UNIQUE KEY `uk_phase_name` (`phase_name`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

INSERT INTO `ec_zone_heat_thresholds` (`phase_id`, `phase_name`, `heat_threshold`, `guard_multiplier`, `patrol_frequency`, `detection_range`, `response_speed`, `elite_chance`, `bounty_hunter_enabled`, `ambient_dialogue`, `civilian_behavior`, `music_intensity`, `description`) VALUES
(0, 'CALM',        0,    1.0, 1.0, 1.0, 1.0, 0.00, 0, 1, 'NORMAL',    0, 'Normal zone state - regular patrols'),
(1, 'UNEASY',      300,  1.2, 1.3, 1.1, 1.2, 0.05, 0, 1, 'CAUTIOUS',  1, 'Guards are alert - something is wrong'),
(2, 'MOBILIZING',  750,  1.5, 1.6, 1.3, 1.5, 0.15, 0, 1, 'FEARFUL',   2, 'Guards actively mobilizing - checkpoints'),
(3, 'IMPACT',      1500, 2.0, 2.0, 1.5, 2.0, 0.30, 1, 1, 'HIDING',    3, 'Full guard response - elite units deployed'),
(4, 'MARTIAL_LAW', 3000, 3.0, 2.5, 2.0, 2.5, 0.50, 1, 0, 'EVACUATED', 4, 'Martial law - maximum security response');

-- ============================================================================
-- Zone-Specific Heat Modifiers
-- Some zones have different heat accumulation/decay rates
-- ============================================================================

DROP TABLE IF EXISTS `ec_zone_heat_modifiers`;

CREATE TABLE `ec_zone_heat_modifiers` (
    `zone_id` INT UNSIGNED NOT NULL COMMENT 'Zone ID',
    `zone_name` VARCHAR(100) DEFAULT NULL COMMENT 'Zone name for reference',
    `heat_gain_multiplier` FLOAT NOT NULL DEFAULT 1.0 COMMENT 'Multiplier for heat gain',
    `heat_decay_multiplier` FLOAT NOT NULL DEFAULT 1.0 COMMENT 'Multiplier for heat decay',
    `max_heat` INT UNSIGNED NOT NULL DEFAULT 5000 COMMENT 'Maximum heat cap for zone',
    `base_guard_level` TINYINT UNSIGNED NOT NULL DEFAULT 1 COMMENT 'Starting guard presence (1-5)',
    `response_delay_multiplier` FLOAT NOT NULL DEFAULT 1.0 COMMENT 'Guard response delay multiplier',
    `special_rules` SET('NO_BOUNTY_HUNTERS', 'NO_ELITES', 'INSTANT_RESPONSE', 'SLOW_DECAY', 'FAST_DECAY', 'SANCTUARY') DEFAULT NULL,
    `description` TEXT,
    PRIMARY KEY (`zone_id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

-- Alliance capitals and towns
INSERT INTO `ec_zone_heat_modifiers` (`zone_id`, `zone_name`, `heat_gain_multiplier`, `heat_decay_multiplier`, `max_heat`, `base_guard_level`, `response_delay_multiplier`, `special_rules`, `description`) VALUES
(1519, 'Stormwind City', 1.3, 0.8, 6000, 5, 0.5, 'INSTANT_RESPONSE', 'Heavy guard presence, fast response'),
(1537, 'Ironforge', 1.2, 0.9, 5500, 4, 0.6, NULL, 'Mountain fortress with strong defenses'),
(1657, 'Darnassus', 1.0, 1.2, 4000, 3, 0.8, NULL, 'Night elf capital, more peaceful'),
(1, 'Dun Morogh', 0.9, 1.1, 3000, 2, 1.0, NULL, 'Dwarven starting zone'),
(12, 'Elwynn Forest', 1.0, 1.0, 3500, 2, 1.0, NULL, 'Human starting zone'),
(87, 'Goldshire', 1.1, 1.0, 4000, 3, 0.9, NULL, 'Busy town, moderate presence');

-- Horde capitals and towns
INSERT INTO `ec_zone_heat_modifiers` (`zone_id`, `zone_name`, `heat_gain_multiplier`, `heat_decay_multiplier`, `max_heat`, `base_guard_level`, `response_delay_multiplier`, `special_rules`, `description`) VALUES
(1637, 'Orgrimmar', 1.3, 0.8, 6000, 5, 0.5, 'INSTANT_RESPONSE', 'Horde capital, grunts everywhere'),
(1638, 'Thunder Bluff', 1.0, 1.2, 4000, 3, 0.9, NULL, 'Tauren city, more peaceful'),
(1497, 'Undercity', 1.2, 0.9, 5000, 4, 0.7, NULL, 'Forsaken capital, abominations patrol'),
(14, 'Durotar', 1.0, 1.0, 3500, 2, 1.0, NULL, 'Orc starting zone'),
(215, 'Mulgore', 0.8, 1.3, 3000, 2, 1.2, 'SLOW_DECAY', 'Peaceful Tauren lands');

-- Neutral zones
INSERT INTO `ec_zone_heat_modifiers` (`zone_id`, `zone_name`, `heat_gain_multiplier`, `heat_decay_multiplier`, `max_heat`, `base_guard_level`, `response_delay_multiplier`, `special_rules`, `description`) VALUES
(35, 'Booty Bay', 0.7, 1.5, 2500, 2, 1.5, 'NO_ELITES', 'Goblin neutrality, bruisers are slow'),
(2257, 'Gadgetzan', 0.6, 1.5, 2000, 2, 1.5, 'NO_ELITES', 'Goblin town, profit over justice'),
(440, 'Ratchet', 0.5, 2.0, 1500, 1, 2.0, 'NO_BOUNTY_HUNTERS,NO_ELITES,FAST_DECAY', 'Minimal enforcement'),
(3703, 'Shattrath City', 0.3, 2.0, 1000, 5, 0.3, 'SANCTUARY', 'Sanctuary city - minimal crime possible'),
(4395, 'Dalaran', 0.3, 2.0, 1000, 5, 0.3, 'SANCTUARY', 'Sanctuary city - minimal crime possible');

-- Contested/dangerous zones
INSERT INTO `ec_zone_heat_modifiers` (`zone_id`, `zone_name`, `heat_gain_multiplier`, `heat_decay_multiplier`, `max_heat`, `base_guard_level`, `response_delay_multiplier`, `special_rules`, `description`) VALUES
(33, 'Stranglethorn Vale', 0.6, 1.8, 2000, 1, 2.0, 'SLOW_DECAY', 'Jungle lawlessness'),
(17, 'Barrens', 0.7, 1.5, 2500, 1, 1.8, NULL, 'Wide open, sparse patrols'),
(36, 'Alterac Mountains', 0.5, 2.0, 1500, 1, 2.5, 'NO_BOUNTY_HUNTERS', 'Lawless territory'),
(47, 'Hinterlands', 0.6, 1.8, 2000, 1, 2.0, NULL, 'Remote wilderness');

-- ============================================================================
-- Guard Response Escalation
-- Defines what happens at each star level
-- ============================================================================

DROP TABLE IF EXISTS `ec_guard_response_escalation`;

CREATE TABLE `ec_guard_response_escalation` (
    `star_level` TINYINT UNSIGNED NOT NULL COMMENT 'Wanted star level (1-5)',
    `guard_count_min` TINYINT UNSIGNED NOT NULL COMMENT 'Minimum guards dispatched',
    `guard_count_max` TINYINT UNSIGNED NOT NULL COMMENT 'Maximum guards dispatched',
    `elite_count` TINYINT UNSIGNED NOT NULL DEFAULT 0 COMMENT 'Elite guards in response',
    `response_delay_ms` INT UNSIGNED NOT NULL COMMENT 'Delay before guards arrive',
    `search_duration_ms` INT UNSIGNED NOT NULL COMMENT 'How long guards search',
    `pursuit_range` FLOAT NOT NULL COMMENT 'How far guards will chase',
    `can_call_backup` TINYINT(1) NOT NULL DEFAULT 0 COMMENT 'Can request more guards',
    `bounty_hunter_chance` FLOAT NOT NULL DEFAULT 0.0 COMMENT 'Chance to spawn bounty hunter',
    `death_penalty_multiplier` FLOAT NOT NULL DEFAULT 1.0 COMMENT 'Death penalty multiplier',
    `description` TEXT,
    PRIMARY KEY (`star_level`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

INSERT INTO `ec_guard_response_escalation` (`star_level`, `guard_count_min`, `guard_count_max`, `elite_count`, `response_delay_ms`, `search_duration_ms`, `pursuit_range`, `can_call_backup`, `bounty_hunter_chance`, `death_penalty_multiplier`, `description`) VALUES
(1, 1, 2, 0, 15000, 60000,   50.0, 0, 0.00, 1.0, 'Minor offense - 1-2 guards, short search'),
(2, 2, 3, 0, 10000, 120000,  75.0, 0, 0.00, 1.2, 'Moderate offense - 2-3 guards, longer search'),
(3, 3, 5, 1, 8000,  180000,  100.0, 1, 0.10, 1.5, 'Serious offense - 3-5 guards + elite, extended search'),
(4, 4, 6, 2, 5000,  300000,  150.0, 1, 0.25, 2.0, 'Severe offense - 4-6 guards + 2 elites, persistent'),
(5, 6, 10, 3, 3000,  600000,  200.0, 1, 0.50, 3.0, 'Maximum response - full force, bounty hunters');
