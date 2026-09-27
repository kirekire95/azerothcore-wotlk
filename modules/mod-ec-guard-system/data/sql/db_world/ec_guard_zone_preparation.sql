-- ==========================================================================
-- EC Guard System - Zone Preparation Phase Database Tables
-- "Shatter Before the Storm" - Foreshadowing & Buildup System
-- ==========================================================================

-- ==========================================================================
-- ZONE DIALOGUES - NPC chatter during phases
-- ==========================================================================
DROP TABLE IF EXISTS `ec_guard_zone_dialogues`;

CREATE TABLE `ec_guard_zone_dialogues` (
    `id` INT UNSIGNED NOT NULL AUTO_INCREMENT,
    `zone_id` INT UNSIGNED NOT NULL DEFAULT 0 COMMENT '0 = any zone',
    `min_phase` TINYINT UNSIGNED NOT NULL DEFAULT 1 COMMENT '0=QUIET, 1=UNEASY, 2=MOBILIZING, 3=IMPACT, 4=AFTERMATH',
    `max_phase` TINYINT UNSIGNED NOT NULL DEFAULT 2,
    `speaker_type` VARCHAR(32) NOT NULL COMMENT 'guard, merchant, civilian, militia, crier, innkeeper',
    `dialogue` VARCHAR(255) NOT NULL,
    `response_dialogue` VARCHAR(255) DEFAULT NULL COMMENT 'Optional response from nearby NPC',
    `response_chance` FLOAT NOT NULL DEFAULT 0.4,
    `cooldown_ms` INT UNSIGNED NOT NULL DEFAULT 60000,
    `trigger_radius` FLOAT NOT NULL DEFAULT 30.0 COMMENT 'Distance from players to trigger',
    `is_yell` TINYINT UNSIGNED NOT NULL DEFAULT 0,
    `requires_audience` TINYINT UNSIGNED NOT NULL DEFAULT 1,
    `faction` TINYINT UNSIGNED NOT NULL DEFAULT 3 COMMENT '0=Neutral, 1=Alliance, 2=Horde, 3=Any',
    `min_hour` TINYINT UNSIGNED NOT NULL DEFAULT 0 COMMENT 'In-game hour (0-24), 0=any',
    `max_hour` TINYINT UNSIGNED NOT NULL DEFAULT 0,
    `location_name` VARCHAR(64) DEFAULT NULL COMMENT 'Location filter within zone (e.g., Crossroads, Ratchet)',
    `enabled` TINYINT UNSIGNED NOT NULL DEFAULT 1,
    PRIMARY KEY (`id`),
    INDEX `idx_zone_phase` (`zone_id`, `min_phase`, `max_phase`),
    INDEX `idx_speaker` (`speaker_type`),
    INDEX `idx_location` (`zone_id`, `location_name`),
    INDEX `idx_enabled` (`enabled`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT='EC Guard System - NPC dialogues for zone phases';

-- ==========================================================================
-- ZONE DIALOGUE PAIRS - Two NPCs having a conversation
-- ==========================================================================
DROP TABLE IF EXISTS `ec_guard_zone_dialogue_pairs`;

CREATE TABLE `ec_guard_zone_dialogue_pairs` (
    `id` INT UNSIGNED NOT NULL AUTO_INCREMENT,
    `zone_id` INT UNSIGNED NOT NULL DEFAULT 0 COMMENT '0 = any zone',
    `min_phase` TINYINT UNSIGNED NOT NULL DEFAULT 1,
    `max_phase` TINYINT UNSIGNED NOT NULL DEFAULT 2,
    `speaker_a_type` VARCHAR(32) NOT NULL,
    `speaker_b_type` VARCHAR(32) NOT NULL,
    `exchanges` TEXT NOT NULL COMMENT 'Pipe-separated lines: A|B|A|B|...',
    `delay_between_lines_ms` INT UNSIGNED NOT NULL DEFAULT 3000,
    `cooldown_ms` INT UNSIGNED NOT NULL DEFAULT 120000,
    `max_distance` FLOAT NOT NULL DEFAULT 10.0 COMMENT 'Max distance between speakers',
    `requires_audience` TINYINT UNSIGNED NOT NULL DEFAULT 1,
    `faction` TINYINT UNSIGNED NOT NULL DEFAULT 3,
    `location_name` VARCHAR(64) DEFAULT NULL COMMENT 'Location filter within zone (e.g., Crossroads, Ratchet)',
    `enabled` TINYINT UNSIGNED NOT NULL DEFAULT 1,
    PRIMARY KEY (`id`),
    INDEX `idx_zone_phase` (`zone_id`, `min_phase`, `max_phase`),
    INDEX `idx_location` (`zone_id`, `location_name`),
    INDEX `idx_enabled` (`enabled`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT='EC Guard System - NPC conversation pairs';

-- ==========================================================================
-- MILITIA SPAWN POINTS - NPCs that spawn during mobilization
-- ==========================================================================
DROP TABLE IF EXISTS `ec_guard_militia_spawns`;

CREATE TABLE `ec_guard_militia_spawns` (
    `id` INT UNSIGNED NOT NULL AUTO_INCREMENT,
    `zone_id` INT UNSIGNED NOT NULL,
    `creature_entry` INT UNSIGNED NOT NULL COMMENT 'Creature template entry',
    `spawn_x` FLOAT NOT NULL,
    `spawn_y` FLOAT NOT NULL,
    `spawn_z` FLOAT NOT NULL,
    `spawn_o` FLOAT NOT NULL DEFAULT 0,
    `waypoint1_x` FLOAT NOT NULL DEFAULT 0 COMMENT 'First waypoint (e.g., supply depot)',
    `waypoint1_y` FLOAT NOT NULL DEFAULT 0,
    `waypoint1_z` FLOAT NOT NULL DEFAULT 0,
    `waypoint2_x` FLOAT NOT NULL DEFAULT 0 COMMENT 'Second waypoint (e.g., barricade)',
    `waypoint2_y` FLOAT NOT NULL DEFAULT 0,
    `waypoint2_z` FLOAT NOT NULL DEFAULT 0,
    `activity` TINYINT UNSIGNED NOT NULL DEFAULT 2 COMMENT '0=IDLE,1=GOSSIPING,2=CARRYING_CRATE,3=RUNNING,4=BOARDING,5=GATHERING,6=PATROLLING,7=WATCHING,8=PRAYING,9=HIDING_GOODS',
    `min_phase` TINYINT UNSIGNED NOT NULL DEFAULT 2 COMMENT 'Phase to start spawning (2=MOBILIZING)',
    `spawn_delay_ms` INT UNSIGNED NOT NULL DEFAULT 0 COMMENT 'Delay after phase starts',
    `despawn_on_phase` TINYINT UNSIGNED NOT NULL DEFAULT 0 COMMENT 'Phase to despawn (0=never)',
    `faction` TINYINT UNSIGNED NOT NULL DEFAULT 3,
    `enabled` TINYINT UNSIGNED NOT NULL DEFAULT 1,
    PRIMARY KEY (`id`),
    INDEX `idx_zone_phase` (`zone_id`, `min_phase`),
    INDEX `idx_creature` (`creature_entry`),
    INDEX `idx_enabled` (`enabled`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT='EC Guard System - Militia NPC spawn points';

-- ==========================================================================
-- TOWN CRIERS - NPCs that make announcements
-- ==========================================================================
DROP TABLE IF EXISTS `ec_guard_town_criers`;

CREATE TABLE `ec_guard_town_criers` (
    `id` INT UNSIGNED NOT NULL AUTO_INCREMENT,
    `zone_id` INT UNSIGNED NOT NULL,
    `crier_entry` INT UNSIGNED NOT NULL COMMENT 'Creature template entry',
    `pos_x` FLOAT NOT NULL,
    `pos_y` FLOAT NOT NULL,
    `pos_z` FLOAT NOT NULL,
    `pos_o` FLOAT NOT NULL DEFAULT 0,
    `calm_announcements` TEXT DEFAULT NULL COMMENT 'Pipe-separated calm phase announcements',
    `alert_announcements` TEXT DEFAULT NULL COMMENT 'Pipe-separated alert phase announcements',
    `urgent_announcements` TEXT DEFAULT NULL COMMENT 'Pipe-separated urgent phase announcements',
    `panic_announcements` TEXT DEFAULT NULL COMMENT 'Pipe-separated panic phase announcements',
    `announce_interval_ms` INT UNSIGNED NOT NULL DEFAULT 45000,
    `yell_radius` FLOAT NOT NULL DEFAULT 100.0,
    `faction` TINYINT UNSIGNED NOT NULL DEFAULT 3,
    `location_name` VARCHAR(64) DEFAULT NULL COMMENT 'Location filter within zone (e.g., Crossroads, Ratchet)',
    `enabled` TINYINT UNSIGNED NOT NULL DEFAULT 1,
    PRIMARY KEY (`id`),
    INDEX `idx_zone` (`zone_id`),
    INDEX `idx_location` (`zone_id`, `location_name`),
    INDEX `idx_enabled` (`enabled`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT='EC Guard System - Town crier configurations';

-- ==========================================================================
-- ZONE GAMEOBJECTS - Barricades, debris, etc.
-- ==========================================================================
DROP TABLE IF EXISTS `ec_guard_zone_gameobjects`;

CREATE TABLE `ec_guard_zone_gameobjects` (
    `id` INT UNSIGNED NOT NULL AUTO_INCREMENT,
    `zone_id` INT UNSIGNED NOT NULL,
    `gameobject_entry` INT UNSIGNED NOT NULL,
    `type` TINYINT UNSIGNED NOT NULL DEFAULT 0 COMMENT '0=BARRICADE,1=SUPPLY_CRATE,2=WEAPON_RACK,3=TORCH,4=WARNING_SIGN,5=FACTION_BANNER,6=CAMPFIRE,7=TENT,8=DEBRIS,9=FIRE',
    `spawn_x` FLOAT NOT NULL,
    `spawn_y` FLOAT NOT NULL,
    `spawn_z` FLOAT NOT NULL,
    `spawn_o` FLOAT NOT NULL DEFAULT 0,
    `spawn_on_phase` TINYINT UNSIGNED NOT NULL DEFAULT 2 COMMENT 'Phase to spawn (2=MOBILIZING)',
    `despawn_on_phase` TINYINT UNSIGNED NOT NULL DEFAULT 0 COMMENT 'Phase to despawn (0=QUIET)',
    `spawn_delay_ms` INT UNSIGNED NOT NULL DEFAULT 0,
    `despawn_delay_ms` INT UNSIGNED NOT NULL DEFAULT 0,
    `respawn_time_ms` INT UNSIGNED NOT NULL DEFAULT 0 COMMENT '0 = no respawn',
    `faction` TINYINT UNSIGNED NOT NULL DEFAULT 3,
    `faction_override` TINYINT UNSIGNED NOT NULL DEFAULT 0 COMMENT 'If set, only spawn when zone controlled by faction',
    `enabled` TINYINT UNSIGNED NOT NULL DEFAULT 1,
    PRIMARY KEY (`id`),
    INDEX `idx_zone_phase` (`zone_id`, `spawn_on_phase`),
    INDEX `idx_type` (`type`),
    INDEX `idx_enabled` (`enabled`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT='EC Guard System - Phase gameobjects (barricades, debris)';

-- ==========================================================================
-- FACTION BANNERS - Banners that change based on zone control
-- ==========================================================================
DROP TABLE IF EXISTS `ec_guard_faction_banners`;

CREATE TABLE `ec_guard_faction_banners` (
    `id` INT UNSIGNED NOT NULL AUTO_INCREMENT,
    `zone_id` INT UNSIGNED NOT NULL,
    `pos_x` FLOAT NOT NULL,
    `pos_y` FLOAT NOT NULL,
    `pos_z` FLOAT NOT NULL,
    `pos_o` FLOAT NOT NULL DEFAULT 0,
    `alliance_banner_entry` INT UNSIGNED NOT NULL DEFAULT 0 COMMENT 'GO entry for Alliance control',
    `horde_banner_entry` INT UNSIGNED NOT NULL DEFAULT 0 COMMENT 'GO entry for Horde control',
    `neutral_banner_entry` INT UNSIGNED NOT NULL DEFAULT 0 COMMENT 'GO entry for neutral/contested',
    `fallen_banner_entry` INT UNSIGNED NOT NULL DEFAULT 0 COMMENT 'GO entry for fallen zone (torn/burning)',
    `enabled` TINYINT UNSIGNED NOT NULL DEFAULT 1,
    PRIMARY KEY (`id`),
    INDEX `idx_zone` (`zone_id`),
    INDEX `idx_enabled` (`enabled`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT='EC Guard System - Faction banners that change with control';

-- ==========================================================================
-- SAMPLE DATA - Crossroads (Zone ID: 17 - The Barrens)
-- ==========================================================================

-- Dialogues for The Barrens (zone 17)
INSERT INTO `ec_guard_zone_dialogues` (`zone_id`, `min_phase`, `max_phase`, `speaker_type`, `dialogue`, `cooldown_ms`) VALUES
-- UNEASY phase dialogues
(17, 1, 1, 'guard', 'Keep your eyes open. The scouts reported movement to the east.', 60000),
(17, 1, 1, 'guard', 'I don''t like the feel of things today. Stay alert.', 60000),
(17, 1, 1, 'guard', 'Alliance dogs have been spotted near the road. Watch yourself.', 60000),
(17, 1, 1, 'merchant', 'Business has been slow. Everyone seems on edge.', 60000),
(17, 1, 1, 'merchant', 'I should secure my valuables. Just in case.', 60000),
(17, 1, 1, 'civilian', 'Did you hear? There was fighting near Ratchet.', 60000),
(17, 1, 1, 'civilian', 'The children should stay indoors today.', 60000),
-- MOBILIZING phase dialogues
(17, 2, 2, 'guard', 'All citizens to shelter! This is not a drill!', 45000),
(17, 2, 2, 'guard', 'Lock down the Crossroads! Secure the perimeter!', 45000),
(17, 2, 2, 'guard', 'Get those barricades up! NOW!', 45000),
(17, 2, 2, 'merchant', 'I''m closing shop! Everyone out!', 45000),
(17, 2, 2, 'civilian', 'To the inn! Everyone to the inn!', 45000),
(17, 2, 2, 'civilian', 'For the Horde! We will not fall!', 45000);

-- Dialogue pairs for The Barrens
INSERT INTO `ec_guard_zone_dialogue_pairs` (`zone_id`, `min_phase`, `max_phase`, `speaker_a_type`, `speaker_b_type`, `exchanges`, `cooldown_ms`) VALUES
(17, 1, 2, 'guard', 'guard', 'Did you see the scout''s report?|Aye. Alliance movement near the Gold Road.|The captain''s worried. I can tell.|We''ve handled worse. Lok''tar ogar.', 120000),
(17, 1, 2, 'civilian', 'guard', 'Guard! What''s happening? Why the extra patrols?|Just a precaution, citizen. Stay calm.|But my family needs to know if we''re safe!|Get to the inn. It''s the safest place right now.', 120000),
(17, 1, 2, 'merchant', 'merchant', 'Have you heard anything?|Caravans from Ratchet are being delayed.|This is bad for business.|I''m hiding my best goods. You should do the same.', 120000);

-- Sample militia spawns for Crossroads
INSERT INTO `ec_guard_militia_spawns` (`zone_id`, `creature_entry`, `spawn_x`, `spawn_y`, `spawn_z`, `waypoint1_x`, `waypoint1_y`, `waypoint1_z`, `activity`, `min_phase`, `faction`) VALUES
-- Horde Guard 3501 is the native Crossroads template; never borrow an
-- Orgrimmar Grunt for these local preparation actors.
(17, 3501, -452.5, -2661.3, 95.8, -460.0, -2670.0, 95.8, 2, 2, 2),  -- Carrying crate
(17, 3501, -445.2, -2658.1, 95.8, -440.0, -2650.0, 95.8, 3, 2, 2),  -- Running errands
(17, 3501, -448.9, -2665.5, 95.8, -450.0, -2680.0, 95.8, 6, 2, 2);  -- Fast patrol

-- Town crier for Crossroads
INSERT INTO `ec_guard_town_criers` (`zone_id`, `crier_entry`, `pos_x`, `pos_y`, `pos_z`, `pos_o`,
    `calm_announcements`, `alert_announcements`, `urgent_announcements`, `panic_announcements`) VALUES
(17, 3501, -450.0, -2660.0, 95.8, 0.5,
    'All is well in the Crossroads! Trade routes are open!|The Horde stands strong! Fear nothing!',
    'Citizens! Increased patrols are for your safety!|Report any suspicious activity to the nearest guard!|Alliance movement reported nearby. Stay vigilant!',
    'URGENT! All citizens return home immediately!|WARNING! Threat detected! Seek shelter now!|EMERGENCY! The guard is mobilizing! Clear the streets!',
    'WE ARE UNDER ATTACK! SEEK SHELTER!|FOR THE HORDE! TO ARMS!|THE CROSSROADS WILL NOT FALL! FIGHT!');

-- Barricades for Crossroads (use appropriate GO entries for your server)
INSERT INTO `ec_guard_zone_gameobjects` (`zone_id`, `gameobject_entry`, `type`, `spawn_x`, `spawn_y`, `spawn_z`, `spawn_o`, `spawn_on_phase`, `despawn_on_phase`) VALUES
-- Main road barricades (use wooden barricade entry, example: 186273)
(17, 186273, 0, -468.5, -2660.0, 95.8, 1.57, 2, 0),
(17, 186273, 0, -432.5, -2660.0, 95.8, 1.57, 2, 0),
-- Supply crates near inn
(17, 2844, 1, -453.0, -2673.0, 95.8, 0.0, 2, 0),
(17, 2844, 1, -455.0, -2673.5, 95.8, 0.3, 2, 0);

-- Faction banners for Crossroads
INSERT INTO `ec_guard_faction_banners` (`zone_id`, `pos_x`, `pos_y`, `pos_z`, `pos_o`,
    `alliance_banner_entry`, `horde_banner_entry`, `neutral_banner_entry`, `fallen_banner_entry`) VALUES
-- Main entrance banner (use appropriate banner GOs for your server)
(17, -450.0, -2655.0, 96.5, 0.0, 180354, 180355, 180100, 180356);

-- ==========================================================================
-- SAMPLE DATA - Goldshire (Zone ID: 12 - Elwynn Forest)
-- ==========================================================================

INSERT INTO `ec_guard_zone_dialogues` (`zone_id`, `min_phase`, `max_phase`, `speaker_type`, `dialogue`, `cooldown_ms`) VALUES
-- UNEASY phase
(12, 1, 1, 'guard', 'Keep your eyes peeled. We''ve had reports of Horde scouts.', 60000),
(12, 1, 1, 'guard', 'The marshal wants extra vigilance today.', 60000),
(12, 1, 1, 'merchant', 'I hope the trouble doesn''t reach us here.', 60000),
(12, 1, 1, 'civilian', 'Light protect us. I heard there was fighting near Westfall.', 60000),
(12, 1, 1, 'innkeeper', 'The inn will be open for anyone seeking shelter.', 60000),
-- MOBILIZING phase
(12, 2, 2, 'guard', 'Citizens of Goldshire! Seek shelter immediately!', 45000),
(12, 2, 2, 'guard', 'For the Alliance! We will defend our home!', 45000),
(12, 2, 2, 'merchant', 'Light save us! Lock the doors!', 45000);

INSERT INTO `ec_guard_zone_dialogue_pairs` (`zone_id`, `min_phase`, `max_phase`, `speaker_a_type`, `speaker_b_type`, `exchanges`, `cooldown_ms`) VALUES
(12, 1, 2, 'guard', 'guard', 'Any word from the scouts?|Nothing good. Horde activity on the roads.|Stormwind needs to know.|Messenger''s already been sent. Stay sharp.', 120000),
(12, 1, 2, 'civilian', 'innkeeper', 'Is it true? Are the Horde coming?|Just rumors for now. But the inn is open if you need shelter.|Thank the Light. My children need somewhere safe.|We''ll keep everyone safe here.', 120000);

-- Town crier for Goldshire
INSERT INTO `ec_guard_town_criers` (`zone_id`, `crier_entry`, `pos_x`, `pos_y`, `pos_z`, `pos_o`,
    `calm_announcements`, `alert_announcements`, `urgent_announcements`, `panic_announcements`) VALUES
(12, 468, -9460.0, 57.0, 56.3, 2.0,
    'All is well in Goldshire! The roads are safe!|Remember to visit the Lion''s Pride Inn!',
    'Citizens! Be watchful! Report any suspicious activity!|Increased patrols are for your protection!',
    'WARNING! All citizens seek shelter immediately!|URGENT! Hostile forces approaching! Find safety!',
    'WE ARE UNDER ATTACK! FOR THE ALLIANCE!|DEFEND GOLDSHIRE! TO ARMS!');

-- ==========================================================================
-- SAMPLE DATA - Orgrimmar (Zone ID: 1637)
-- ==========================================================================

INSERT INTO `ec_guard_zone_dialogues` (`zone_id`, `min_phase`, `max_phase`, `speaker_type`, `dialogue`, `cooldown_ms`) VALUES
(1637, 1, 1, 'guard', 'The city is secure. But stay vigilant.', 60000),
(1637, 1, 1, 'guard', 'Any fool who attacks Orgrimmar will learn their mistake.', 60000),
(1637, 2, 2, 'guard', 'FOR THE HORDE! DEFEND THE CITY!', 45000),
(1637, 2, 2, 'guard', 'The Warchief''s guard will crush these invaders!', 45000);

-- ==========================================================================
-- SAMPLE DATA - Stormwind City (Zone ID: 1519)
-- ==========================================================================

INSERT INTO `ec_guard_zone_dialogues` (`zone_id`, `min_phase`, `max_phase`, `speaker_type`, `dialogue`, `cooldown_ms`) VALUES
(1519, 1, 1, 'guard', 'Light be with you, citizen. Stay safe.', 60000),
(1519, 1, 1, 'guard', 'The city guard stands ready. Always.', 60000),
(1519, 2, 2, 'guard', 'FOR THE KING! FOR THE ALLIANCE!', 45000),
(1519, 2, 2, 'guard', 'Stormwind will never fall! To arms!', 45000);

-- Town crier for Stormwind Trade District
INSERT INTO `ec_guard_town_criers` (`zone_id`, `crier_entry`, `pos_x`, `pos_y`, `pos_z`, `pos_o`,
    `calm_announcements`, `alert_announcements`, `urgent_announcements`, `panic_announcements`) VALUES
(1519, 468, -8830.0, 626.0, 94.0, 3.0,
    'Hear ye! Hear ye! Stormwind prospers under King Wrynn!|The Alliance stands strong!',
    'Citizens! Be aware of increased security measures!|Report any suspicious activity to the city guard!',
    'EMERGENCY! All citizens seek shelter immediately!|WARNING! Hostile forces detected near the city!',
    'STORMWIND IS UNDER ATTACK! TO ARMS! TO ARMS!|THE KING CALLS ALL ABLE WARRIORS TO DEFEND THE CITY!');

-- ==========================================================================
-- STRANGLETHORN VALE (Zone ID: 33) - Grom'gol Base Camp
-- ==========================================================================

INSERT INTO `ec_guard_zone_dialogues` (`zone_id`, `min_phase`, `max_phase`, `speaker_type`, `dialogue`, `faction`, `cooldown_ms`) VALUES
-- UNEASY phase (Horde perspective at Grom'gol)
(33, 1, 1, 'guard', 'The jungle holds many dangers. Stay close to camp.', 2, 60000),
(33, 1, 1, 'guard', 'Alliance scum have been spotted near the river. Keep your axes ready.', 2, 60000),
(33, 1, 1, 'guard', 'The raptors are restless tonight. Something disturbs them.', 2, 60000),
(33, 1, 1, 'guard', 'Nesingwary''s hunters grow bold. They venture too close to our borders.', 2, 60000),
(33, 1, 1, 'merchant', 'The zeppelin from Orgrimmar is late. I hope nothing happened.', 2, 60000),
(33, 1, 1, 'merchant', 'Trade has been slow. The jungle paths are becoming dangerous.', 2, 60000),
(33, 1, 1, 'civilian', 'I heard screams in the jungle last night. The trolls are hunting again.', 2, 60000),
(33, 1, 1, 'civilian', 'The Bloodsail pirates raid ships daily now. Times are hard.', 2, 60000),
-- MOBILIZING phase
(33, 2, 2, 'guard', 'All warriors to your posts! For the Horde!', 2, 45000),
(33, 2, 2, 'guard', 'Grom''gol will not fall! Defend the base camp!', 2, 45000),
(33, 2, 2, 'guard', 'Secure the zeppelin tower! No one passes!', 2, 45000),
(33, 2, 2, 'merchant', 'Board up the supplies! Don''t let them take our goods!', 2, 45000),
(33, 2, 2, 'civilian', 'To the tower! It''s the safest place!', 2, 45000);

-- Alliance perspective at Stranglethorn (Rebel Camp area)
INSERT INTO `ec_guard_zone_dialogues` (`zone_id`, `min_phase`, `max_phase`, `speaker_type`, `dialogue`, `faction`, `cooldown_ms`) VALUES
(33, 1, 1, 'guard', 'The Horde patrol the main roads. We must stay hidden.', 1, 60000),
(33, 1, 1, 'guard', 'Colonel Kurzen''s men grow more aggressive. Watch the jungle.', 1, 60000),
(33, 1, 1, 'civilian', 'We''re cut off from Booty Bay. The roads are too dangerous.', 1, 60000),
(33, 2, 2, 'guard', 'Defend the camp! For the Alliance!', 1, 45000),
(33, 2, 2, 'guard', 'Fall back to defensive positions! Hold the line!', 1, 45000);

-- Neutral dialogues (Nesingwary Camp)
INSERT INTO `ec_guard_zone_dialogues` (`zone_id`, `min_phase`, `max_phase`, `speaker_type`, `dialogue`, `faction`, `cooldown_ms`) VALUES
(33, 1, 1, 'civilian', 'The hunting party hasn''t returned. I''m worried.', 0, 60000),
(33, 1, 1, 'civilian', 'Did you hear that roar? Something big is out there.', 0, 60000),
(33, 1, 1, 'merchant', 'I''ve got the finest leather this side of Booty Bay. But who''s buying?', 0, 60000);

-- Dialogue pairs for Stranglethorn
INSERT INTO `ec_guard_zone_dialogue_pairs` (`zone_id`, `min_phase`, `max_phase`, `speaker_a_type`, `speaker_b_type`, `exchanges`, `faction`, `cooldown_ms`) VALUES
(33, 1, 2, 'guard', 'guard', 'Did you see the tracks near the river?|Aye. Alliance boots. Fresh.|Commander wants double patrols tonight.|Then we hunt at dawn. Lok''tar.', 2, 120000),
(33, 1, 2, 'guard', 'merchant', 'Any news from the zeppelin crew?|They spotted ships. Bloodsail flags.|Pirates again? As if we didn''t have enough problems.|Keep your blade close. It''s going to be a long night.', 2, 120000),
(33, 1, 2, 'civilian', 'civilian', 'I miss Orgrimmar. The jungle is too dangerous.|We knew the risks when we came here.|But the trolls... the tigers... the Alliance...|The Horde endures. We will survive this.', 2, 120000);

-- Town crier for Grom'gol
INSERT INTO `ec_guard_town_criers` (`zone_id`, `crier_entry`, `pos_x`, `pos_y`, `pos_z`, `pos_o`,
    `calm_announcements`, `alert_announcements`, `urgent_announcements`, `panic_announcements`, `faction`) VALUES
(33, 1064, -12366.0, 137.0, 2.9, 1.0,
    'Grom''gol stands strong! The Horde''s foothold in Stranglethorn endures!|Trade routes to Booty Bay are open!',
    'Warriors! Extra patrols ordered! Stay vigilant!|Report any Alliance activity to the commander!',
    'WARNING! Hostile forces detected! All civilians to the tower!|URGENT! Prepare for battle! Secure all supplies!',
    'WE ARE UNDER ATTACK! FOR THE HORDE!|DEFEND GROM''GOL! TO ARMS! TO ARMS!', 2);

-- ==========================================================================
-- BOOTY BAY (Zone ID: 35)
-- ==========================================================================

INSERT INTO `ec_guard_zone_dialogues` (`zone_id`, `min_phase`, `max_phase`, `speaker_type`, `dialogue`, `faction`, `cooldown_ms`) VALUES
-- Neutral zone - both factions welcome (but tense)
(35, 1, 1, 'guard', 'No fighting in Booty Bay! Baron''s orders!', 0, 60000),
(35, 1, 1, 'guard', 'The Bloodsail are getting bolder. Keep your wits about you.', 0, 60000),
(35, 1, 1, 'guard', 'I don''t care if you''re Horde or Alliance. Start trouble, and I''ll gut you myself.', 0, 60000),
(35, 1, 1, 'merchant', 'Finest goods in all of Stranglethorn! ...if you can afford ''em.', 0, 60000),
(35, 1, 1, 'merchant', 'The pirates hit another supply ship. Prices are going up.', 0, 60000),
(35, 1, 1, 'civilian', 'Did you see that brawl in the tavern? Horde and Alliance going at it.', 0, 60000),
(35, 1, 1, 'civilian', 'The Baron''s bruisers broke it up fast. No one messes with the Cartel.', 0, 60000),
(35, 1, 1, 'innkeeper', 'Welcome to the Salty Sailor! No weapons drawn inside!', 0, 60000),
-- MOBILIZING phase
(35, 2, 2, 'guard', 'BLOODSAIL ATTACK! All bruisers to the docks!', 0, 45000),
(35, 2, 2, 'guard', 'Protect the ships! Don''t let them take the cargo!', 0, 45000),
(35, 2, 2, 'merchant', 'My goods! Someone help me secure my goods!', 0, 45000),
(35, 2, 2, 'civilian', 'The pirates! They''re coming from the sea!', 0, 45000);

-- Dialogue pairs for Booty Bay
INSERT INTO `ec_guard_zone_dialogue_pairs` (`zone_id`, `min_phase`, `max_phase`, `speaker_a_type`, `speaker_b_type`, `exchanges`, `cooldown_ms`) VALUES
(35, 1, 2, 'merchant', 'merchant', 'Another ship lost to the Bloodsail...|Third one this month. Insurance won''t cover it.|The Baron needs to do something about those pirates.|He''s got his hands full keeping Horde and Alliance from killing each other.', 120000),
(35, 1, 2, 'guard', 'civilian', 'You there! No loitering near the docks!|I''m just watching the ships!|Watch them from the tavern. The docks are restricted.|Fine, fine. But something big is coming in. I can feel it.', 120000),
(35, 1, 2, 'innkeeper', 'civilian', 'Room for the night?|How much?|Ten gold. Twelve if you want breakfast.|Ten gold?! Highway robbery!|Take it or sleep with the murlocs. Your choice.', 120000);

-- Town crier for Booty Bay
INSERT INTO `ec_guard_town_criers` (`zone_id`, `crier_entry`, `pos_x`, `pos_y`, `pos_z`, `pos_o`,
    `calm_announcements`, `alert_announcements`, `urgent_announcements`, `panic_announcements`) VALUES
(35, 4624, -14451.0, 495.0, 15.0, 0.0,
    'Welcome to Booty Bay! Neutral ground! No faction fighting!|The Steamwheedle Cartel wishes you profitable trading!',
    'Attention! Bloodsail activity reported! Stay near the docks!|All merchants: secure your valuables!',
    'WARNING! Pirate ships sighted! All civilians to shelter!|BRUISERS TO STATIONS! PREPARE FOR ATTACK!',
    'BLOODSAIL ATTACK IN PROGRESS! DEFEND BOOTY BAY!|THE CARTEL REWARDS ALL WHO FIGHT! TO ARMS!');

-- ==========================================================================
-- RATCHET (Zone ID: 17 - The Barrens, Neutral)
-- ==========================================================================

INSERT INTO `ec_guard_zone_dialogues` (`zone_id`, `min_phase`, `max_phase`, `speaker_type`, `dialogue`, `faction`, `cooldown_ms`) VALUES
-- Ratchet-specific dialogues (neutral goblin town)
(17, 1, 1, 'guard', 'No fighting in Ratchet! Gazlowe doesn''t tolerate troublemakers!', 0, 60000),
(17, 1, 1, 'guard', 'Pirates on the horizon again. The third time this week.', 0, 60000),
(17, 1, 1, 'merchant', 'Business is booming! Both factions need supplies!', 0, 60000),
(17, 1, 1, 'merchant', 'The ship from Booty Bay is late. Hope they didn''t run into Blackwater Raiders.', 0, 60000),
(17, 1, 1, 'civilian', 'Horde and Alliance drinking in the same tavern. Only in Ratchet.', 0, 60000),
(17, 2, 2, 'guard', 'Bruisers! Secure the port! Defend our profits!', 0, 45000),
(17, 2, 2, 'merchant', 'My warehouse! Don''t let them near my warehouse!', 0, 45000);

-- Town crier for Ratchet
INSERT INTO `ec_guard_town_criers` (`zone_id`, `crier_entry`, `pos_x`, `pos_y`, `pos_z`, `pos_o`,
    `calm_announcements`, `alert_announcements`, `urgent_announcements`, `panic_announcements`) VALUES
(17, 3502, -956.0, -3754.0, 5.5, 4.0,
    'Welcome to Ratchet! Steamwheedle Cartel territory! Neutral ground!|Ships departing for Booty Bay every hour!',
    'Attention traders! Increased pirate activity reported!|All vessels advised to travel in convoy!',
    'WARNING! Hostile forces approaching! All civilians to the inn!|BRUISERS MOBILIZING! PREPARE FOR TROUBLE!',
    'RATCHET IS UNDER ATTACK! DEFEND THE PORT!|GAZLOWE REWARDS ALL WHO FIGHT! EARN YOUR GOLD!');

-- ==========================================================================
-- HILLSBRAD FOOTHILLS (Zone ID: 267) - Tarren Mill (Horde) & Southshore (Alliance)
-- ==========================================================================

-- Tarren Mill (Horde)
INSERT INTO `ec_guard_zone_dialogues` (`zone_id`, `min_phase`, `max_phase`, `speaker_type`, `dialogue`, `faction`, `cooldown_ms`) VALUES
(267, 1, 1, 'guard', 'The Alliance fortify Southshore daily. War is coming.', 2, 60000),
(267, 1, 1, 'guard', 'Stay sharp. Alliance patrols grow bolder every week.', 2, 60000),
(267, 1, 1, 'guard', 'The Deathguard will defend Tarren Mill. Let them come.', 2, 60000),
(267, 1, 1, 'merchant', 'The constant fighting is bad for business.', 2, 60000),
(267, 1, 1, 'civilian', 'I can see Southshore from here. They''re watching us too.', 2, 60000),
(267, 1, 1, 'civilian', 'The Forsaken brought us into the Horde. Now we fight their wars.', 2, 60000),
(267, 2, 2, 'guard', 'FOR THE DARK LADY! DEFEND TARREN MILL!', 2, 45000),
(267, 2, 2, 'guard', 'Alliance attack! All Deathguard to the gates!', 2, 45000),
(267, 2, 2, 'civilian', 'The living attack! Find shelter, quickly!', 2, 45000);

-- Southshore (Alliance)
INSERT INTO `ec_guard_zone_dialogues` (`zone_id`, `min_phase`, `max_phase`, `speaker_type`, `dialogue`, `faction`, `cooldown_ms`) VALUES
(267, 1, 1, 'guard', 'Tarren Mill watches us. The Forsaken never sleep.', 1, 60000),
(267, 1, 1, 'guard', 'Marshal Redpath wants extra patrols tonight.', 1, 60000),
(267, 1, 1, 'guard', 'We hold the line here. Lordaeron will be reclaimed.', 1, 60000),
(267, 1, 1, 'merchant', 'Weapons are selling fast. Everyone expects a fight.', 1, 60000),
(267, 1, 1, 'civilian', 'My family fled Tarren Mill when the Scourge came. Now the Forsaken hold it.', 1, 60000),
(267, 1, 1, 'civilian', 'We should strike first. Waiting only makes them stronger.', 1, 60000),
(267, 2, 2, 'guard', 'FOR THE ALLIANCE! DEFEND SOUTHSHORE!', 1, 45000),
(267, 2, 2, 'guard', 'Horde assault! Man the walls!', 1, 45000),
(267, 2, 2, 'civilian', 'Light protect us! To the inn!', 1, 45000);

-- Dialogue pairs for Hillsbrad
INSERT INTO `ec_guard_zone_dialogue_pairs` (`zone_id`, `min_phase`, `max_phase`, `speaker_a_type`, `speaker_b_type`, `exchanges`, `faction`, `cooldown_ms`) VALUES
(267, 1, 2, 'guard', 'guard', 'Did you see the Alliance riders?|Three of them. Scouting our positions.|They''re getting ready for something big.|Let them come. We''ll send them to their graves.', 2, 120000),
(267, 1, 2, 'civilian', 'merchant', 'Are you leaving?|Too dangerous to stay. The fighting will reach us soon.|Where will you go?|Anywhere but here. This land is cursed.', 2, 120000),
(267, 1, 2, 'guard', 'guard', 'Movement near Tarren Mill. Forsaken scouts.|How many?|At least a dozen. More behind them, probably.|Alert the Marshal. This is it.', 1, 120000),
(267, 1, 2, 'civilian', 'guard', 'Guard! Is it true? Are the Forsaken coming?|Stay calm. The garrison is prepared.|But my children! What do I do?|Take them to the inn. It''s the safest place.', 1, 120000);

-- Town criers for Hillsbrad
INSERT INTO `ec_guard_town_criers` (`zone_id`, `crier_entry`, `pos_x`, `pos_y`, `pos_z`, `pos_o`,
    `calm_announcements`, `alert_announcements`, `urgent_announcements`, `panic_announcements`, `faction`) VALUES
-- Tarren Mill crier (Horde)
(267, 2405, -33.0, -923.0, 54.0, 0.5,
    'Tarren Mill stands! The Forsaken hold this land!|The Dark Lady protects her people!',
    'Warning! Alliance movement detected!|All Deathguard, increase patrol frequency!',
    'URGENT! Alliance forces massing! Prepare for battle!|All civilians to shelter! Deathguard to positions!',
    'TARREN MILL IS UNDER ATTACK! FOR THE DARK LADY!|DEFEND YOUR UNDEATH! SLAY THE LIVING!', 2),
-- Southshore crier (Alliance)
(267, 2386, -829.0, -558.0, 16.0, 1.5,
    'Southshore stands strong! The Alliance holds!|Trade routes remain open! Light guide you!',
    'Citizens! Horde activity reported! Stay vigilant!|All militia report for duty!',
    'WARNING! Forsaken forces approaching! Seek shelter!|ALL GUARDS TO STATIONS! PREPARE FOR ATTACK!',
    'SOUTHSHORE IS UNDER SIEGE! FOR THE ALLIANCE!|LIGHT GIVE US STRENGTH! DEFEND YOUR HOMES!', 1);

-- ==========================================================================
-- DUSKWOOD (Zone ID: 10) - Darkshire
-- ==========================================================================

INSERT INTO `ec_guard_zone_dialogues` (`zone_id`, `min_phase`, `max_phase`, `speaker_type`, `dialogue`, `faction`, `cooldown_ms`) VALUES
(10, 1, 1, 'guard', 'Stay close to the lanterns. The darkness hides many horrors.', 1, 60000),
(10, 1, 1, 'guard', 'The Night Watch never sleeps. Neither do the undead.', 1, 60000),
(10, 1, 1, 'guard', 'I heard howling near Raven Hill. The worgen grow bolder.', 1, 60000),
(10, 1, 1, 'guard', 'Mor''Ladim walks again. Stay away from the cemetery.', 1, 60000),
(10, 1, 1, 'merchant', 'Oil for your lanterns! Don''t let the darkness take you!', 1, 60000),
(10, 1, 1, 'merchant', 'Prices are high, but so is the cost of dying out here.', 1, 60000),
(10, 1, 1, 'civilian', 'The screams at night... I can''t sleep anymore.', 1, 60000),
(10, 1, 1, 'civilian', 'Why doesn''t Stormwind send more soldiers? We''re dying here!', 1, 60000),
(10, 1, 1, 'innkeeper', 'Room for the night? Trust me, you don''t want to be outside after dark.', 1, 60000),
(10, 2, 2, 'guard', 'TO ARMS! The undead rise! Defend Darkshire!', 1, 45000),
(10, 2, 2, 'guard', 'Night Watch! Form ranks! We fight or we die!', 1, 45000),
(10, 2, 2, 'civilian', 'Light save us! They''re everywhere!', 1, 45000),
(10, 2, 2, 'civilian', 'Where are the soldiers from Stormwind?! We''re alone!', 1, 45000);

-- Dialogue pairs for Duskwood
INSERT INTO `ec_guard_zone_dialogue_pairs` (`zone_id`, `min_phase`, `max_phase`, `speaker_a_type`, `speaker_b_type`, `exchanges`, `cooldown_ms`) VALUES
(10, 1, 2, 'guard', 'guard', 'Another patrol didn''t return last night.|Which route?|The one past Manor Mistmantle.|That place is cursed. We should burn it to the ground.', 120000),
(10, 1, 2, 'civilian', 'guard', 'My husband went to Raven Hill three days ago. He hasn''t come back.|I''m sorry. No one comes back from Raven Hill.|You have to help me find him!|Ma''am... if he went to Raven Hill, he''s not your husband anymore.', 120000),
(10, 1, 2, 'merchant', 'innkeeper', 'Any travelers tonight?|Two. They''re terrified.|Everyone is. This cursed darkness...|Stormwind has abandoned us. We''re on our own.', 120000);

-- Town crier for Darkshire
INSERT INTO `ec_guard_town_criers` (`zone_id`, `crier_entry`, `pos_x`, `pos_y`, `pos_z`, `pos_o`,
    `calm_announcements`, `alert_announcements`, `urgent_announcements`, `panic_announcements`) VALUES
(10, 10038, -10564.0, -1172.0, 28.0, 4.0,
    'Keep your lanterns lit! The Night Watch protects you!|Stay within the town limits after dark!',
    'Warning! Increased undead activity reported!|All citizens must remain indoors after sunset!',
    'URGENT! The dead are rising! Seek shelter immediately!|ALL NIGHT WATCH TO STATIONS! PREPARE FOR BATTLE!',
    'DARKSHIRE IS UNDER ATTACK! THE DARKNESS COMES!|LIGHT PRESERVE US! FIGHT FOR YOUR LIVES!');

-- ==========================================================================
-- REDRIDGE MOUNTAINS (Zone ID: 44) - Lakeshire
-- ==========================================================================

INSERT INTO `ec_guard_zone_dialogues` (`zone_id`, `min_phase`, `max_phase`, `speaker_type`, `dialogue`, `faction`, `cooldown_ms`) VALUES
(44, 1, 1, 'guard', 'The Blackrock orcs probe our defenses daily.', 1, 60000),
(44, 1, 1, 'guard', 'We''ve asked Stormwind for reinforcements. None have come.', 1, 60000),
(44, 1, 1, 'guard', 'The bridge is our lifeline. We must hold it at all costs.', 1, 60000),
(44, 1, 1, 'merchant', 'Supplies are running low. The roads are too dangerous.', 1, 60000),
(44, 1, 1, 'merchant', 'Weapons sell well. Fear does that.', 1, 60000),
(44, 1, 1, 'civilian', 'My son joined the militia. I pray daily for his safety.', 1, 60000),
(44, 1, 1, 'civilian', 'Magistrate Solomon does what he can. But we need soldiers.', 1, 60000),
(44, 2, 2, 'guard', 'BLACKROCK ATTACK! Defend the bridge!', 1, 45000),
(44, 2, 2, 'guard', 'For Lakeshire! For the Alliance! Hold the line!', 1, 45000),
(44, 2, 2, 'civilian', 'The orcs! They''re coming across the lake!', 1, 45000);

-- Dialogue pairs for Redridge
INSERT INTO `ec_guard_zone_dialogue_pairs` (`zone_id`, `min_phase`, `max_phase`, `speaker_a_type`, `speaker_b_type`, `exchanges`, `cooldown_ms`) VALUES
(44, 1, 2, 'guard', 'guard', 'Orc scouts were spotted near the dam.|How many?|At least five. Could be more in the hills.|Tell the Magistrate. This could be the real attack.', 120000),
(44, 1, 2, 'civilian', 'merchant', 'Do you have any weapons left?|Just this old sword. Take it.|How much?|Free. You''ll need it more than gold where we''re going.', 120000);

-- Town crier for Lakeshire
INSERT INTO `ec_guard_town_criers` (`zone_id`, `crier_entry`, `pos_x`, `pos_y`, `pos_z`, `pos_o`,
    `calm_announcements`, `alert_announcements`, `urgent_announcements`, `panic_announcements`) VALUES
(44, 10037, -9223.0, -2188.0, 66.0, 0.5,
    'Lakeshire stands! The bridge holds!|Magistrate Solomon requests all able-bodied citizens for militia duty!',
    'Warning! Orc activity detected near Render''s Rock!|All militia report to the town hall!',
    'URGENT! Blackrock forces massing! All citizens to shelter!|MILITIA TO THE BRIDGE! PREPARE FOR BATTLE!',
    'LAKESHIRE IS UNDER ATTACK! DEFEND THE BRIDGE!|FOR THE ALLIANCE! FOR OUR HOMES! FIGHT!');

-- ==========================================================================
-- ASHENVALE (Zone ID: 331) - Astranaar (Alliance)
-- ==========================================================================

INSERT INTO `ec_guard_zone_dialogues` (`zone_id`, `min_phase`, `max_phase`, `speaker_type`, `dialogue`, `faction`, `cooldown_ms`) VALUES
(331, 1, 1, 'guard', 'The Horde''s lumber operations push deeper into our forests.', 1, 60000),
(331, 1, 1, 'guard', 'Warsong outriders were seen near the river. Stay alert.', 1, 60000),
(331, 1, 1, 'guard', 'Cenarius gave his life for these forests. We will not let them fall.', 1, 60000),
(331, 1, 1, 'guard', 'The corruption spreads from Felwood. Our battles are on many fronts.', 1, 60000),
(331, 1, 1, 'merchant', 'Trade has slowed. The roads are too dangerous.', 1, 60000),
(331, 1, 1, 'civilian', 'I can hear the trees weeping. The Horde''s axes never stop.', 1, 60000),
(331, 1, 1, 'civilian', 'Elune guide us. These are dark times for Ashenvale.', 1, 60000),
(331, 2, 2, 'guard', 'WARSONG ATTACK! Sentinels to arms!', 1, 45000),
(331, 2, 2, 'guard', 'Defend Astranaar! For Elune! For the forests!', 1, 45000),
(331, 2, 2, 'civilian', 'They defile our sacred groves! Fight them!', 1, 45000);

-- Dialogue pairs for Ashenvale (Alliance)
INSERT INTO `ec_guard_zone_dialogue_pairs` (`zone_id`, `min_phase`, `max_phase`, `speaker_a_type`, `speaker_b_type`, `exchanges`, `faction`, `cooldown_ms`) VALUES
(331, 1, 2, 'guard', 'guard', 'The Warsong Gulch sees heavy fighting.|I know. My sister fell there.|Elune grant her peace.|She will be avenged. I swear it.', 1, 120000),
(331, 1, 2, 'civilian', 'guard', 'How long can we hold?|As long as we must.|But the Horde has so many...|One Sentinel is worth ten orcs. Have faith.', 1, 120000);

-- Warsong Lumber Camp (Horde) dialogues
INSERT INTO `ec_guard_zone_dialogues` (`zone_id`, `min_phase`, `max_phase`, `speaker_type`, `dialogue`, `faction`, `cooldown_ms`) VALUES
(331, 1, 1, 'guard', 'The night elves watch from the shadows. Keep your axe close.', 2, 60000),
(331, 1, 1, 'guard', 'Grom Hellscream died in these forests. We honor his memory with every tree.', 2, 60000),
(331, 1, 1, 'civilian', 'The lumber feeds our war machine. Keep cutting.', 2, 60000),
(331, 2, 2, 'guard', 'SENTINELS! Warsong Clan, to arms!', 2, 45000),
(331, 2, 2, 'guard', 'For Hellscream! For the Horde!', 2, 45000);

-- Town crier for Astranaar
INSERT INTO `ec_guard_town_criers` (`zone_id`, `crier_entry`, `pos_x`, `pos_y`, `pos_z`, `pos_o`,
    `calm_announcements`, `alert_announcements`, `urgent_announcements`, `panic_announcements`, `faction`) VALUES
(331, 6087, 2738.0, -390.0, 107.0, 3.0,
    'Astranaar stands guard! The forests of Ashenvale will not fall!|Elune watch over you, traveler!',
    'Warning! Warsong activity reported! All Sentinels on alert!|Travelers should avoid the eastern roads!',
    'URGENT! Horde forces approaching! All civilians to the moonwell!|SENTINELS TO ARMS! DEFEND OUR HOME!',
    'ASTRANAAR IS UNDER ATTACK! FOR ELUNE! FOR KALIMDOR!|THE FORESTS WILL NOT FALL! FIGHT!', 1);

-- ==========================================================================
-- CAMP TAURAJO (Zone ID: 17 - The Barrens, Horde)
-- ==========================================================================

INSERT INTO `ec_guard_zone_dialogues` (`zone_id`, `min_phase`, `max_phase`, `speaker_type`, `dialogue`, `faction`, `cooldown_ms`) VALUES
-- Camp Taurajo specific
(17, 1, 1, 'guard', 'The kodo rest here before the long journey. We must protect them.', 2, 60000),
(17, 1, 1, 'guard', 'Centaurs raid our herds. The Barrens tests all who live here.', 2, 60000),
(17, 1, 1, 'civilian', 'The Earthmother provides. Even in this harsh land.', 2, 60000),
(17, 1, 1, 'merchant', 'Leather from the finest kodo. The Grimtotem can''t match our quality.', 2, 60000),
(17, 2, 2, 'guard', 'Defend the herd! Protect Camp Taurajo!', 2, 45000);

-- ==========================================================================
-- DUROTAR (Zone ID: 14) - Razor Hill (Horde)
-- ==========================================================================

INSERT INTO `ec_guard_zone_dialogues` (`zone_id`, `min_phase`, `max_phase`, `speaker_type`, `dialogue`, `faction`, `cooldown_ms`) VALUES
(14, 1, 1, 'guard', 'Razor Hill is the first line of defense for Orgrimmar.', 2, 60000),
(14, 1, 1, 'guard', 'The quilboar test our borders. They will learn their mistake.', 2, 60000),
(14, 1, 1, 'guard', 'Young warriors prove themselves here. Many become grunts.', 2, 60000),
(14, 1, 1, 'merchant', 'Fresh meat! Straight from the hunt!', 2, 60000),
(14, 1, 1, 'civilian', 'The Warchief''s banner flies proud. The Horde is strong.', 2, 60000),
(14, 2, 2, 'guard', 'Raiders! Defend Razor Hill! Lok''tar ogar!', 2, 45000),
(14, 2, 2, 'guard', 'For the Horde! Protect the village!', 2, 45000);

-- Dialogue pairs for Durotar
INSERT INTO `ec_guard_zone_dialogue_pairs` (`zone_id`, `min_phase`, `max_phase`, `speaker_a_type`, `speaker_b_type`, `exchanges`, `faction`, `cooldown_ms`) VALUES
(14, 1, 2, 'guard', 'guard', 'Did you hear? Centaur were spotted near Thunder Ridge.|How many?|A war party. Dozens at least.|Alert the sergeant. This could be serious.', 2, 120000);

-- Town crier for Razor Hill
INSERT INTO `ec_guard_town_criers` (`zone_id`, `crier_entry`, `pos_x`, `pos_y`, `pos_z`, `pos_o`,
    `calm_announcements`, `alert_announcements`, `urgent_announcements`, `panic_announcements`, `faction`) VALUES
(14, 5953, 346.0, -4708.0, 16.0, 5.0,
    'Razor Hill stands strong! Young warriors, prove your worth!|The Warchief sends his regards!',
    'Warning! Quilboar activity detected! Stay alert!|All grunts report for patrol duty!',
    'URGENT! Raiders approaching! All warriors to defensive positions!|CIVILIANS TO THE BARRACKS! PREPARE FOR BATTLE!',
    'RAZOR HILL IS UNDER ATTACK! FOR THE HORDE!|DEFEND THE VILLAGE! LOK''TAR OGAR!', 2);

-- ==========================================================================
-- TIRISFAL GLADES (Zone ID: 85) - Brill (Horde/Undead)
-- ==========================================================================

INSERT INTO `ec_guard_zone_dialogues` (`zone_id`, `min_phase`, `max_phase`, `speaker_type`, `dialogue`, `faction`, `cooldown_ms`) VALUES
(85, 1, 1, 'guard', 'The Scarlet Crusade grows more aggressive. They burn first, ask questions never.', 2, 60000),
(85, 1, 1, 'guard', 'Undercity prepares for war. The living will learn to fear us.', 2, 60000),
(85, 1, 1, 'guard', 'Patrol the roads. The Crusade plants spies everywhere.', 2, 60000),
(85, 1, 1, 'merchant', 'Fresh... parts. Don''t ask where they came from.', 2, 60000),
(85, 1, 1, 'civilian', 'I don''t remember my life before. Perhaps that''s a blessing.', 2, 60000),
(85, 1, 1, 'civilian', 'The Dark Lady saved us from the Scourge. We serve her willingly.', 2, 60000),
(85, 2, 2, 'guard', 'CRUSADE ATTACK! Deathguard, defend Brill!', 2, 45000),
(85, 2, 2, 'guard', 'For the Dark Lady! Slay the living zealots!', 2, 45000),
(85, 2, 2, 'civilian', 'They come to burn us! Fight or be destroyed!', 2, 45000);

-- Dialogue pairs for Tirisfal
INSERT INTO `ec_guard_zone_dialogue_pairs` (`zone_id`, `min_phase`, `max_phase`, `speaker_a_type`, `speaker_b_type`, `exchanges`, `faction`, `cooldown_ms`) VALUES
(85, 1, 2, 'guard', 'guard', 'Crusade patrols near the Scarlet Monastery.|They''re always near the Monastery. It''s their fortress.|Something''s different. More of them than usual.|Inform High Executor Hadrec. He should know.', 2, 120000),
(85, 1, 2, 'civilian', 'civilian', 'Do you remember... before?|Flashes. A family. A farm. All gone now.|The plague took everything.|And gave us undeath. Is that better or worse?', 2, 120000);

-- Town crier for Brill
INSERT INTO `ec_guard_town_criers` (`zone_id`, `crier_entry`, `pos_x`, `pos_y`, `pos_z`, `pos_o`,
    `calm_announcements`, `alert_announcements`, `urgent_announcements`, `panic_announcements`, `faction`) VALUES
(85, 7980, 2266.0, 289.0, 35.0, 5.5,
    'Brill stands! The Forsaken endure!|The Dark Lady watches over her children!',
    'Warning! Scarlet Crusade activity reported!|All Deathguard on alert!',
    'URGENT! Crusade forces approaching! Prepare for battle!|ALL FORSAKEN TO ARMS! DEFEND BRILL!',
    'BRILL IS UNDER ATTACK! FOR THE DARK LADY!|SLAY THE LIVING! VICTORY OR FINAL DEATH!', 2);

-- ==========================================================================
-- WESTFALL (Zone ID: 40) - Sentinel Hill (Alliance)
-- ==========================================================================

INSERT INTO `ec_guard_zone_dialogues` (`zone_id`, `min_phase`, `max_phase`, `speaker_type`, `dialogue`, `faction`, `cooldown_ms`) VALUES
(40, 1, 1, 'guard', 'The Defias grow stronger every day. We''re barely holding on.', 1, 60000),
(40, 1, 1, 'guard', 'Stormwind ignores our pleas. We fight alone.', 1, 60000),
(40, 1, 1, 'guard', 'Vancleef''s followers are everywhere. Trust no one.', 1, 60000),
(40, 1, 1, 'merchant', 'What little I have, I''ll sell. But there''s not much left.', 1, 60000),
(40, 1, 1, 'civilian', 'The Defias burned my farm. I have nothing left.', 1, 60000),
(40, 1, 1, 'civilian', 'The nobles in Stormwind feast while we starve.', 1, 60000),
(40, 1, 1, 'civilian', 'Gryan Stoutmantle does what he can. But it''s never enough.', 1, 60000),
(40, 2, 2, 'guard', 'DEFIAS ATTACK! People''s Militia, to arms!', 1, 45000),
(40, 2, 2, 'guard', 'Defend Sentinel Hill! This is our last stand!', 1, 45000),
(40, 2, 2, 'civilian', 'They come for what little we have! Fight them!', 1, 45000);

-- Dialogue pairs for Westfall
INSERT INTO `ec_guard_zone_dialogue_pairs` (`zone_id`, `min_phase`, `max_phase`, `speaker_a_type`, `speaker_b_type`, `exchanges`, `cooldown_ms`) VALUES
(40, 1, 2, 'guard', 'guard', 'Another farm burned last night.|Which one?|The Furlbrows. Nothing left.|Light rest their souls. The Defias show no mercy.', 120000),
(40, 1, 2, 'civilian', 'guard', 'When will Stormwind send help?|I''ve sent three messengers. No reply.|They''ve abandoned us!|The People''s Militia fights on. We won''t abandon you.', 120000),
(40, 1, 2, 'civilian', 'civilian', 'I''ve heard rumors of a Defias hideout. Somewhere underground.|The Deadmines. My brother disappeared near there.|I''m sorry. No one returns from the Deadmines.|Vancleef will pay for what he''s done.', 120000);

-- Town crier for Sentinel Hill
INSERT INTO `ec_guard_town_criers` (`zone_id`, `crier_entry`, `pos_x`, `pos_y`, `pos_z`, `pos_o`,
    `calm_announcements`, `alert_announcements`, `urgent_announcements`, `panic_announcements`) VALUES
(40, 8096, -10628.0, 1097.0, 35.0, 0.0,
    'Sentinel Hill holds! The People''s Militia protects you!|Gryan Stoutmantle calls for volunteers!',
    'Warning! Defias activity increasing! Stay close to town!|All militia report for duty!',
    'URGENT! Defias forces massing! All citizens to shelter!|MILITIA TO DEFENSIVE POSITIONS! PREPARE FOR ATTACK!',
    'SENTINEL HILL IS UNDER ATTACK! DEFEND YOUR HOMES!|THE MILITIA FIGHTS! JOIN US OR DIE!');

-- ==========================================================================
-- LOCATION_NAME UPDATES (Zone 17 disambiguation for Crossroads/Ratchet/Camp Taurajo)
-- ==========================================================================

-- Zone 17 dialogues: Crossroads (Horde) - faction 2 or 3, excludes neutral (faction 0)
-- Dialogues mentioning "Crossroads" explicitly or Horde-specific without neutral flag
UPDATE `ec_guard_zone_dialogues` SET `location_name` = 'Crossroads'
WHERE `zone_id` = 17 AND (`faction` = 2 OR `faction` = 3)
  AND `dialogue` NOT LIKE '%Ratchet%' AND `dialogue` NOT LIKE '%Gazlowe%'
  AND `dialogue` NOT LIKE '%pirate%' AND `dialogue` NOT LIKE '%Bruiser%'
  AND `dialogue` NOT LIKE '%kodo%' AND `dialogue` NOT LIKE '%Taurajo%';

-- Zone 17 dialogues: Ratchet (Neutral) - faction 0
UPDATE `ec_guard_zone_dialogues` SET `location_name` = 'Ratchet'
WHERE `zone_id` = 17 AND `faction` = 0;

-- Zone 17 dialogues: Camp Taurajo (Horde) - kodo/Taurajo/Centaur references
UPDATE `ec_guard_zone_dialogues` SET `location_name` = 'Camp Taurajo'
WHERE `zone_id` = 17 AND (`dialogue` LIKE '%kodo%' OR `dialogue` LIKE '%Taurajo%' OR `dialogue` LIKE '%Centaur%');

-- Zone 17 dialogue pairs: Crossroads
UPDATE `ec_guard_zone_dialogue_pairs` SET `location_name` = 'Crossroads'
WHERE `zone_id` = 17 AND (`faction` = 2 OR `faction` = 3)
  AND `exchanges` NOT LIKE '%Ratchet%';

-- Zone 17 criers: Position-based identification
-- Crossroads crier: around (-450, -2660)
UPDATE `ec_guard_town_criers` SET `location_name` = 'Crossroads'
WHERE `zone_id` = 17 AND `pos_x` > -500 AND `pos_x` < -400
  AND `pos_y` > -2700 AND `pos_y` < -2600;

-- Ratchet crier: around (-956, -3754)
UPDATE `ec_guard_town_criers` SET `location_name` = 'Ratchet'
WHERE `zone_id` = 17 AND `pos_x` > -1000 AND `pos_x` < -900
  AND `pos_y` > -3800 AND `pos_y` < -3700;

-- Booty Bay is area 35 inside Stranglethorn Vale zone 33. Preserve its exact
-- town label before replacing the unreachable legacy zone_id 35.
UPDATE `ec_guard_zone_dialogues`
SET `zone_id` = 33, `location_name` = 'Booty Bay', `faction` = 0
WHERE `zone_id` = 35;
UPDATE `ec_guard_zone_dialogue_pairs`
SET `zone_id` = 33, `location_name` = 'Booty Bay', `faction` = 0
WHERE `zone_id` = 35;
UPDATE `ec_guard_town_criers`
SET `zone_id` = 33, `location_name` = 'Booty Bay', `faction` = 0
WHERE `zone_id` = 35;

-- Normalize the actor faction/locality for every seeded crier. These remain
-- disabled below because their announcement scripts are custom, not native
-- WotLK text; the mapping is safe if an administrator later opts in.
UPDATE `ec_guard_town_criers` SET `faction` = 2, `location_name` = 'Crossroads'
WHERE `zone_id` = 17 AND `pos_x` BETWEEN -500 AND -400 AND `pos_y` BETWEEN -2700 AND -2600;
UPDATE `ec_guard_town_criers` SET `faction` = 0, `location_name` = 'Ratchet'
WHERE `zone_id` = 17 AND `pos_x` BETWEEN -1000 AND -900 AND `pos_y` BETWEEN -3800 AND -3700;
UPDATE `ec_guard_town_criers` SET `faction` = 2, `location_name` = 'Grom''gol'
WHERE `zone_id` = 33 AND `pos_x` BETWEEN -12450 AND -12250 AND `pos_y` BETWEEN 50 AND 250;
UPDATE `ec_guard_town_criers` SET `faction` = 2 WHERE `zone_id` IN (14, 85);
UPDATE `ec_guard_town_criers` SET `faction` = 1 WHERE `zone_id` IN (10, 12, 40, 44, 331, 1519);

-- No native Crossroads worker/militia template was found. Do not make the
-- local Horde Guard perform crate/errand civilian animations; retain only the
-- local guard patrol row as an optional, correctly classified preparation NPC.
UPDATE `ec_guard_militia_spawns` SET `enabled` = 0
WHERE `zone_id` = 17 AND `activity` IN (2, 3);

-- Synthetic preparation chatter is custom content, not native WotLK. Keep the
-- seed data available for explicit opt-in, but never enable it by installation.
UPDATE `ec_guard_zone_dialogues` SET `enabled` = 0;
UPDATE `ec_guard_zone_dialogue_pairs` SET `enabled` = 0;
UPDATE `ec_guard_town_criers` SET `enabled` = 0;
