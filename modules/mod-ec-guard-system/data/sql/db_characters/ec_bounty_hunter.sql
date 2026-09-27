--
-- EC Guard System - Bounty Hunter Tables
-- Copyright (C) 2026 EC
--

-- Bounties table (tracks active and historical bounties)
DROP TABLE IF EXISTS `ec_bounties`;
CREATE TABLE `ec_bounties` (
    `id` INT UNSIGNED NOT NULL AUTO_INCREMENT,
    `target_guid` INT UNSIGNED NOT NULL COMMENT 'Player GUID of the wanted player',
    `target_name` VARCHAR(50) NOT NULL,
    `wanted_level` TINYINT UNSIGNED NOT NULL DEFAULT 0 COMMENT '0=None, 1-5=Stars',
    `reward_gold` INT UNSIGNED NOT NULL DEFAULT 0 COMMENT 'Reward in copper',
    `posted_time` TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    `expires_time` TIMESTAMP NULL,
    `last_known_zone` INT UNSIGNED NOT NULL DEFAULT 0,
    `claimed_by_guid` INT UNSIGNED DEFAULT NULL COMMENT 'Player who claimed the bounty',
    `claimed_time` TIMESTAMP NULL,
    `status` TINYINT UNSIGNED NOT NULL DEFAULT 0 COMMENT '0=Active, 1=Claimed, 2=Expired, 3=Cancelled',
    PRIMARY KEY (`id`),
    KEY `idx_target` (`target_guid`),
    KEY `idx_status` (`status`),
    KEY `idx_posted` (`posted_time`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT='EC Guard System - Bounty Tracking';

-- Bounty hunters table (tracks spawned hunter NPCs)
DROP TABLE IF EXISTS `ec_bounty_hunters`;
CREATE TABLE `ec_bounty_hunters` (
    `id` INT UNSIGNED NOT NULL AUTO_INCREMENT,
    `hunter_guid` INT UNSIGNED NOT NULL COMMENT 'Creature GUID',
    `target_guid` INT UNSIGNED NOT NULL COMMENT 'Player being hunted',
    `bounty_id` INT UNSIGNED DEFAULT NULL COMMENT 'Associated bounty ID',
    `spawn_time` TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    `despawn_time` TIMESTAMP NULL,
    `status` TINYINT UNSIGNED NOT NULL DEFAULT 0 COMMENT '0=Hunting, 1=KilledTarget, 2=WasKilled, 3=Despawned',
    PRIMARY KEY (`id`),
    KEY `idx_hunter` (`hunter_guid`),
    KEY `idx_target` (`target_guid`),
    KEY `idx_bounty` (`bounty_id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT='EC Guard System - Bounty Hunter NPC Tracking';

-- Bounty claims history (for statistics and leaderboards)
DROP TABLE IF EXISTS `ec_bounty_claims`;
CREATE TABLE `ec_bounty_claims` (
    `id` INT UNSIGNED NOT NULL AUTO_INCREMENT,
    `bounty_id` INT UNSIGNED NOT NULL,
    `target_guid` INT UNSIGNED NOT NULL,
    `target_name` VARCHAR(50) NOT NULL,
    `claimer_guid` INT UNSIGNED NOT NULL,
    `claimer_name` VARCHAR(50) NOT NULL,
    `reward_gold` INT UNSIGNED NOT NULL,
    `wanted_level` TINYINT UNSIGNED NOT NULL,
    `claim_time` TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    PRIMARY KEY (`id`),
    KEY `idx_claimer` (`claimer_guid`),
    KEY `idx_target` (`target_guid`),
    KEY `idx_time` (`claim_time`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT='EC Guard System - Bounty Claim History';
