-- ============================================================================
-- EC Gossip Templates
-- Part of mod-alive-world for AzerothCore
--
-- This table stores gossip headline templates that bots use when spreading
-- news about world events. Templates support placeholders:
--   {primary}   - The primary subject (player name, creature name)
--   {secondary} - The secondary subject (victim, target, boss name)
--   {zone}      - The zone where the event occurred
--
-- Weight determines selection probability (higher = more likely)
-- Importance range filters templates based on event severity (1-10)
-- ============================================================================

DROP TABLE IF EXISTS `ec_gossip_templates`;

CREATE TABLE `ec_gossip_templates` (
    `id` INT UNSIGNED NOT NULL AUTO_INCREMENT,
    `event_type` VARCHAR(32) NOT NULL,
    `template_text` VARCHAR(255) NOT NULL,
    `weight` INT UNSIGNED NOT NULL DEFAULT 10,
    `min_importance` TINYINT UNSIGNED NOT NULL DEFAULT 1,
    `max_importance` TINYINT UNSIGNED NOT NULL DEFAULT 10,
    `is_exclamation` TINYINT(1) NOT NULL DEFAULT 0,
    `category` VARCHAR(32) NOT NULL DEFAULT 'general',
    `enabled` TINYINT(1) NOT NULL DEFAULT 1,
    `comment` VARCHAR(255) DEFAULT NULL,
    PRIMARY KEY (`id`),
    KEY `idx_event_type` (`event_type`),
    KEY `idx_enabled` (`enabled`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

-- ============================================================================
-- BOSS_KILLED templates
-- ============================================================================
INSERT INTO `ec_gossip_templates` (`event_type`, `template_text`, `weight`, `min_importance`, `max_importance`, `is_exclamation`, `category`, `comment`) VALUES
('BOSS_KILLED', '{secondary} has been defeated!', 10, 1, 10, 1, 'combat', 'Generic boss kill'),
('BOSS_KILLED', 'The mighty {secondary} has fallen!', 10, 5, 10, 1, 'combat', 'Epic boss kill'),
('BOSS_KILLED', 'Heroes have slain {secondary} in {zone}!', 8, 7, 10, 1, 'combat', 'Zone-specific boss kill'),
('BOSS_KILLED', 'Victory! {secondary} is no more!', 6, 5, 10, 1, 'combat', 'Celebratory'),
('BOSS_KILLED', 'Word spreads of {secondary}''s defeat.', 5, 1, 5, 0, 'combat', 'Low-key announcement'),
('BOSS_KILLED', 'Did you hear? {secondary} was slain in {zone}!', 8, 3, 8, 1, 'combat', 'Gossip style'),
('BOSS_KILLED', 'The beast {secondary} troubles {zone} no longer.', 5, 5, 10, 0, 'combat', 'Formal announcement'),
('BOSS_KILLED', 'Adventurers celebrate! {secondary} has been vanquished!', 6, 7, 10, 1, 'combat', 'Celebratory');

-- ============================================================================
-- PLAYER_KILLED_PLAYER templates (PvP)
-- ============================================================================
INSERT INTO `ec_gossip_templates` (`event_type`, `template_text`, `weight`, `min_importance`, `max_importance`, `is_exclamation`, `category`, `comment`) VALUES
('PLAYER_KILLED_PLAYER', '{primary} slew {secondary} in battle!', 10, 1, 10, 1, 'combat', 'Generic PvP'),
('PLAYER_KILLED_PLAYER', '{secondary} has fallen to {primary}!', 8, 3, 10, 1, 'combat', 'Victim focused'),
('PLAYER_KILLED_PLAYER', 'A fierce battle ended with {secondary}''s defeat.', 5, 1, 5, 0, 'combat', 'Neutral report'),
('PLAYER_KILLED_PLAYER', '{primary} claims another victim in {zone}.', 5, 5, 10, 0, 'combat', 'Serial killer vibe'),
('PLAYER_KILLED_PLAYER', 'Blood was spilled in {zone} today...', 3, 1, 5, 0, 'combat', 'Ominous'),
('PLAYER_KILLED_PLAYER', 'The rivalry continues! {primary} bested {secondary}!', 6, 5, 10, 1, 'combat', 'Rivalry'),
('PLAYER_KILLED_PLAYER', '{primary} struck down {secondary} without mercy!', 4, 7, 10, 1, 'combat', 'Brutal');

-- ============================================================================
-- RARE_SPAWN templates
-- ============================================================================
INSERT INTO `ec_gossip_templates` (`event_type`, `template_text`, `weight`, `min_importance`, `max_importance`, `is_exclamation`, `category`, `comment`) VALUES
('RARE_SPAWN', 'Rare creature spotted: {primary}!', 10, 1, 10, 1, 'world', 'Generic rare'),
('RARE_SPAWN', 'A rare {primary} has been seen in {zone}!', 10, 5, 10, 1, 'world', 'Zone specific'),
('RARE_SPAWN', 'Hunters, take note! {primary} lurks nearby.', 5, 1, 5, 0, 'world', 'Hunter advice'),
('RARE_SPAWN', 'The elusive {primary} has appeared in {zone}!', 8, 5, 10, 1, 'world', 'Elusive creature'),
('RARE_SPAWN', 'I saw tracks of {primary} near {zone}...', 3, 1, 5, 0, 'world', 'Subtle hint'),
('RARE_SPAWN', '{primary} walks again! Head to {zone}!', 6, 7, 10, 1, 'world', 'Call to action');

-- ============================================================================
-- WORLD_BOSS_SPAWN templates
-- ============================================================================
INSERT INTO `ec_gossip_templates` (`event_type`, `template_text`, `weight`, `min_importance`, `max_importance`, `is_exclamation`, `category`, `comment`) VALUES
('WORLD_BOSS_SPAWN', 'World boss {primary} has appeared!', 10, 8, 10, 1, 'world', 'Generic world boss'),
('WORLD_BOSS_SPAWN', 'The ground trembles... {primary} awakens!', 10, 8, 10, 1, 'world', 'Dramatic'),
('WORLD_BOSS_SPAWN', 'Gather your allies! {primary} threatens {zone}!', 8, 8, 10, 1, 'world', 'Call to arms'),
('WORLD_BOSS_SPAWN', 'The terror of {zone}, {primary}, has returned!', 7, 8, 10, 1, 'world', 'Return of boss'),
('WORLD_BOSS_SPAWN', 'Champions needed! {primary} rampages through {zone}!', 6, 9, 10, 1, 'world', 'Urgent');

-- ============================================================================
-- GUARD_CALLED templates (Crime/Wanted)
-- ============================================================================
INSERT INTO `ec_gossip_templates` (`event_type`, `template_text`, `weight`, `min_importance`, `max_importance`, `is_exclamation`, `category`, `comment`) VALUES
('GUARD_CALLED', 'Guards pursue {primary}!', 10, 1, 10, 1, 'crime', 'Generic pursuit'),
('GUARD_CALLED', 'Criminal {primary} spotted in {zone}!', 8, 5, 10, 1, 'crime', 'Criminal spotted'),
('GUARD_CALLED', 'Wanted: {primary} for crimes in {zone}.', 5, 3, 7, 0, 'crime', 'Wanted poster style'),
('GUARD_CALLED', 'The guards are after {primary}!', 10, 1, 5, 1, 'crime', 'Casual alert'),
('GUARD_CALLED', 'There''s trouble in {zone}! Someone attacked a civilian!', 5, 1, 3, 1, 'crime', 'Anonymous'),
('GUARD_CALLED', '{primary} has made enemies of the guard!', 6, 5, 10, 1, 'crime', 'Enemy of guard'),
('GUARD_CALLED', 'Stay clear of {primary}! The guards seek justice!', 4, 5, 10, 1, 'crime', 'Warning'),
('GUARD_CALLED', 'A dangerous criminal roams {zone}...', 3, 3, 7, 0, 'crime', 'Ominous'),
('GUARD_CALLED', 'Did you see? {primary} attacked someone in broad daylight!', 6, 1, 5, 1, 'crime', 'Witness style'),
('GUARD_CALLED', 'The wanted posters say {primary} is armed and dangerous!', 4, 7, 10, 1, 'crime', 'High bounty');

-- ============================================================================
-- ZONE_BECAME_ACTIVE templates
-- ============================================================================
INSERT INTO `ec_gossip_templates` (`event_type`, `template_text`, `weight`, `min_importance`, `max_importance`, `is_exclamation`, `category`, `comment`) VALUES
('ZONE_BECAME_ACTIVE', 'Activity reported in the area!', 10, 1, 10, 1, 'world', 'Generic'),
('ZONE_BECAME_ACTIVE', '{zone} is bustling with activity.', 5, 1, 5, 0, 'world', 'Casual'),
('ZONE_BECAME_ACTIVE', 'Many adventurers gather in {zone}.', 5, 3, 7, 0, 'world', 'Gathering'),
('ZONE_BECAME_ACTIVE', 'Something''s happening in {zone}...', 3, 1, 5, 0, 'world', 'Mysterious'),
('ZONE_BECAME_ACTIVE', '{zone} seems unusually busy today.', 4, 1, 5, 0, 'world', 'Observation');

-- ============================================================================
-- FACTION_CONFLICT templates
-- ============================================================================
INSERT INTO `ec_gossip_templates` (`event_type`, `template_text`, `weight`, `min_importance`, `max_importance`, `is_exclamation`, `category`, `comment`) VALUES
('FACTION_CONFLICT', 'Tensions rise between factions in {zone}!', 10, 5, 10, 1, 'world', 'Rising tensions'),
('FACTION_CONFLICT', 'Conflict brews in {zone}.', 5, 1, 5, 0, 'world', 'Brewing conflict'),
('FACTION_CONFLICT', 'War may be coming to {zone}...', 4, 7, 10, 0, 'world', 'War warning'),
('FACTION_CONFLICT', 'The factions clash in {zone}!', 8, 5, 10, 1, 'world', 'Active conflict');

-- ============================================================================
-- HOLIDAY_EVENT templates
-- ============================================================================
INSERT INTO `ec_gossip_templates` (`event_type`, `template_text`, `weight`, `min_importance`, `max_importance`, `is_exclamation`, `category`, `comment`) VALUES
('HOLIDAY_EVENT', 'The festivities have begun!', 10, 1, 10, 1, 'social', 'Generic holiday'),
('HOLIDAY_EVENT', 'Celebrations are underway in {zone}!', 8, 5, 10, 1, 'social', 'Zone celebration'),
('HOLIDAY_EVENT', 'Join the merriment in {zone}!', 6, 3, 8, 1, 'social', 'Invitation'),
('HOLIDAY_EVENT', 'The holiday spirit fills {zone}.', 4, 1, 5, 0, 'social', 'Ambient');

-- ============================================================================
-- BOT_ACHIEVEMENT templates
-- ============================================================================
INSERT INTO `ec_gossip_templates` (`event_type`, `template_text`, `weight`, `min_importance`, `max_importance`, `is_exclamation`, `category`, `comment`) VALUES
('BOT_ACHIEVEMENT', '{primary} has achieved something remarkable!', 10, 5, 10, 1, 'social', 'Generic achievement'),
('BOT_ACHIEVEMENT', 'Congratulations to {primary}!', 8, 1, 10, 1, 'social', 'Congratulations'),
('BOT_ACHIEVEMENT', '{primary} has earned great honor!', 6, 7, 10, 1, 'social', 'Honor earned'),
('BOT_ACHIEVEMENT', 'Word of {primary}''s deeds spreads...', 4, 3, 7, 0, 'social', 'Word spreads');

-- ============================================================================
-- GROUP_FORMED templates
-- ============================================================================
INSERT INTO `ec_gossip_templates` (`event_type`, `template_text`, `weight`, `min_importance`, `max_importance`, `is_exclamation`, `category`, `comment`) VALUES
('GROUP_FORMED', 'Adventurers are forming groups for {zone}!', 10, 3, 10, 1, 'social', 'Group forming'),
('GROUP_FORMED', 'A party gathers in {zone}.', 5, 1, 5, 0, 'social', 'Party gathering'),
('GROUP_FORMED', 'Looking for members to explore {zone}!', 6, 3, 8, 1, 'social', 'LFM style');

-- ============================================================================
-- GUILD_EVENT templates
-- ============================================================================
INSERT INTO `ec_gossip_templates` (`event_type`, `template_text`, `weight`, `min_importance`, `max_importance`, `is_exclamation`, `category`, `comment`) VALUES
('GUILD_EVENT', 'A guild has accomplished something great!', 10, 5, 10, 1, 'social', 'Guild accomplishment'),
('GUILD_EVENT', 'Guild activity reported in {zone}.', 5, 1, 5, 0, 'social', 'Guild activity'),
('GUILD_EVENT', 'The guilds are on the move!', 6, 3, 8, 1, 'social', 'Guilds moving');

-- ============================================================================
-- WEATHER_CHANGE templates
-- ============================================================================
INSERT INTO `ec_gossip_templates` (`event_type`, `template_text`, `weight`, `min_importance`, `max_importance`, `is_exclamation`, `category`, `comment`) VALUES
('WEATHER_CHANGE', 'The weather is changing in {zone}.', 10, 1, 5, 0, 'world', 'Generic weather'),
('WEATHER_CHANGE', 'Storm clouds gather over {zone}!', 5, 5, 10, 1, 'world', 'Storm coming'),
('WEATHER_CHANGE', 'Clear skies return to {zone}.', 3, 1, 3, 0, 'world', 'Clear skies'),
('WEATHER_CHANGE', 'Best stay indoors in {zone} today...', 4, 5, 10, 0, 'world', 'Bad weather warning');

-- ============================================================================
-- MERCHANT_EVENT templates
-- ============================================================================
INSERT INTO `ec_gossip_templates` (`event_type`, `template_text`, `weight`, `min_importance`, `max_importance`, `is_exclamation`, `category`, `comment`) VALUES
('MERCHANT_EVENT', 'A merchant has arrived with rare goods!', 10, 5, 10, 1, 'social', 'Rare goods'),
('MERCHANT_EVENT', 'Trading opportunities in {zone}.', 5, 1, 5, 0, 'social', 'Trading'),
('MERCHANT_EVENT', 'New wares available in {zone}!', 6, 3, 8, 1, 'social', 'New wares'),
('MERCHANT_EVENT', 'A wandering merchant visits {zone}.', 4, 1, 5, 0, 'social', 'Wandering merchant');

-- ============================================================================
-- GOSSIP templates (Generic chatter - extracted patterns from broadcast_text)
-- ============================================================================
INSERT INTO `ec_gossip_templates` (`event_type`, `template_text`, `weight`, `min_importance`, `max_importance`, `is_exclamation`, `category`, `comment`) VALUES
-- "Have you heard" patterns
('GOSSIP', 'Have you heard the latest news?', 10, 1, 5, 0, 'have_you_heard', 'Generic gossip opener'),
('GOSSIP', 'Have you heard about the situation in {zone}?', 8, 1, 5, 0, 'have_you_heard', 'Zone-specific'),
('GOSSIP', 'Have you heard what {primary} has been up to?', 7, 3, 7, 0, 'have_you_heard', 'Person-specific'),
('GOSSIP', 'Have you heard the rumors about {secondary}?', 6, 3, 7, 0, 'have_you_heard', 'Rumor style'),

-- "Did you hear" patterns
('GOSSIP', 'Did you hear what happened in {zone}?', 10, 1, 7, 0, 'did_you_hear', 'Breaking news style'),
('GOSSIP', 'Did you hear about {primary}?', 8, 3, 7, 0, 'did_you_hear', 'Person focused'),
('GOSSIP', 'Did you hear the news? Something is going on!', 7, 1, 5, 1, 'did_you_hear', 'Urgent style'),

-- "I heard" patterns
('GOSSIP', 'I heard there is trouble brewing in {zone}.', 10, 3, 7, 0, 'i_heard', 'Trouble report'),
('GOSSIP', 'I heard {primary} was seen around here.', 8, 1, 5, 0, 'i_heard', 'Sighting report'),
('GOSSIP', 'I heard strange things happening nearby.', 6, 1, 5, 0, 'i_heard', 'Mystery'),
('GOSSIP', 'I heard from a friend that {zone} is getting interesting.', 5, 1, 5, 0, 'i_heard', 'Second hand'),

-- "Rumor" patterns
('GOSSIP', 'Rumor has it that {primary} is up to something.', 10, 3, 7, 0, 'rumor', 'Mysterious rumor'),
('GOSSIP', 'Rumors say there is treasure hidden in {zone}.', 7, 1, 5, 0, 'rumor', 'Treasure rumor'),
('GOSSIP', 'The rumors about {zone} are true, I am told.', 6, 3, 7, 0, 'rumor', 'Confirmation'),
('GOSSIP', 'There are wild rumors going around about {secondary}.', 5, 1, 5, 0, 'rumor', 'Wild rumors'),

-- "They say" patterns
('GOSSIP', 'They say {primary} is quite formidable.', 10, 3, 7, 0, 'they_say', 'Person assessment'),
('GOSSIP', 'They say {zone} is dangerous these days.', 8, 1, 5, 0, 'they_say', 'Danger warning'),
('GOSSIP', 'They say strange creatures lurk in {zone}.', 7, 3, 7, 0, 'they_say', 'Creature warning'),
('GOSSIP', 'They say adventurers have been busy lately.', 5, 1, 5, 0, 'they_say', 'General observation'),

-- "Word is" patterns
('GOSSIP', 'Word is that {primary} has made a move.', 10, 3, 7, 0, 'word_is', 'Action report'),
('GOSSIP', 'Word is {zone} is bustling with activity.', 8, 1, 5, 0, 'word_is', 'Activity report'),
('GOSSIP', 'Word spreads of events unfolding in {zone}.', 7, 3, 7, 0, 'word_is', 'Spreading news'),
('GOSSIP', 'The word on the street is to stay alert.', 5, 1, 5, 0, 'word_is', 'General warning'),

-- Generic chatter
('GOSSIP', 'Interesting times we live in...', 8, 1, 3, 0, 'generic', 'Philosophical'),
('GOSSIP', 'Things are never dull around here.', 6, 1, 3, 0, 'generic', 'Commentary'),
('GOSSIP', 'So much going on these days.', 5, 1, 3, 0, 'generic', 'Observation'),
('GOSSIP', 'Have you noticed how busy things have been?', 5, 1, 3, 0, 'generic', 'Question');
