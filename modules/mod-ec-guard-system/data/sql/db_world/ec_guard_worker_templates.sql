-- Guard System: Worker NPC templates (Peons/Peasants) for construction system
-- Workers carry lumber, build structures, and say Warcraft III-style voice lines.

CREATE TABLE IF NOT EXISTS `ec_guard_worker_templates` (
  `id` INT UNSIGNED NOT NULL AUTO_INCREMENT PRIMARY KEY,
  `faction` TINYINT UNSIGNED NOT NULL COMMENT '1=alliance, 2=horde',
  `creature_entry` INT UNSIGNED NOT NULL COMMENT 'Creature template entry (Peon/Peasant)',
  `lumber_display_id` INT UNSIGNED NOT NULL DEFAULT 0 COMMENT 'Item display ID for carrying lumber visual',
  `tool_display_id` INT UNSIGNED NOT NULL DEFAULT 0 COMMENT 'Item display ID for hammer/axe building visual',
  `walk_speed` FLOAT NOT NULL DEFAULT 1.0,
  `say_start` TEXT COMMENT 'Pipe-separated lines said on spawn',
  `say_working` TEXT COMMENT 'Pipe-separated lines said while working',
  `say_done` TEXT COMMENT 'Pipe-separated lines said when construction completes',
  `say_lumber` TEXT COMMENT 'Pipe-separated lines said when picking up lumber',
  `say_flee` TEXT COMMENT 'Pipe-separated lines said when fleeing (IMPACT phase)',
  `enabled` TINYINT(1) NOT NULL DEFAULT 1,
  INDEX `idx_faction` (`faction`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

-- =============================================================================
-- HORDE PEONS (Warcraft III Peon voice lines)
-- creature_entry 10556 = Lazy Peon (has orc peon model)
-- =============================================================================

INSERT IGNORE INTO `ec_guard_worker_templates`
  (`id`, `faction`, `creature_entry`, `lumber_display_id`, `tool_display_id`, `walk_speed`,
   `say_start`, `say_working`, `say_done`, `say_lumber`, `say_flee`) VALUES
(1, 2, 10556, 36713, 2196, 1.0,
 'Ready to work!|Zug zug!|Something need doing?|Dabu!',
 'More work?|Work work...|Me busy, leave me alone!|Okie dokie',
 'Job''s done!|Work complete!|I can do that!',
 'I got the lumber!|Got the wood!|Okie dokie|Lumber ready!',
 'Me not that kind of orc!|Run away!|Help!'),
(2, 2, 10556, 36713, 2196, 0.9,
 'Zug zug!|Something need doing?|Ready to work!',
 'Work work...|More work?|Swobu!',
 'Job''s done!|Work complete!',
 'Lumber ready!|Got the wood!',
 'Run away!|Me not that kind of orc!');

-- =============================================================================
-- ALLIANCE PEASANTS (Warcraft III Peasant voice lines)
-- creature_entry 1976 = Stormwind Peasant (has human worker model)
-- =============================================================================

INSERT IGNORE INTO `ec_guard_worker_templates`
  (`id`, `faction`, `creature_entry`, `lumber_display_id`, `tool_display_id`, `walk_speed`,
   `say_start`, `say_working`, `say_done`, `say_lumber`, `say_flee`) VALUES
(3, 1, 1976, 36713, 2196, 1.0,
 'Yes, milord?|What is it?|Right-o!|More work?',
 'More work?|What else?|Hmm?|Yes, milord',
 'Job''s done!|All done!|Right then!',
 'Lumber ready!|Got the timber!|Yes, milord',
 'Help! Help!|We''re under attack!|Run for your lives!'),
(4, 1, 1976, 36713, 2196, 0.9,
 'Right-o!|What is it?|Yes?',
 'More work?|What else?',
 'Job''s done!|All done!',
 'Got the timber!|Lumber ready!',
 'Help!|We''re under attack!');
