-- Synthetic preparation chatter is custom content, not native WotLK.
-- Disable the reported Astranaar rows; native civilian distress and guard
-- response code is separate and remains enabled.
UPDATE `ec_guard_zone_dialogues` SET `enabled` = 0 WHERE `zone_id` = 331;
UPDATE `ec_guard_zone_dialogue_pairs` SET `enabled` = 0 WHERE `zone_id` = 331;
UPDATE `ec_guard_town_criers` SET `enabled` = 0 WHERE `zone_id` = 331;
