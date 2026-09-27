-- Preserve the settlement area that owns a wanted incident. Zone IDs alone
-- cannot distinguish Astranaar from Horde outposts in Ashenvale, or the
-- Crossroads from other settlements in the Barrens.
ALTER TABLE `ec_guard_wanted_state`
    ADD COLUMN IF NOT EXISTS `crime_area_id` INT UNSIGNED NOT NULL DEFAULT 0
    COMMENT 'Area ID where crime occurred' AFTER `crime_zone_id`;
