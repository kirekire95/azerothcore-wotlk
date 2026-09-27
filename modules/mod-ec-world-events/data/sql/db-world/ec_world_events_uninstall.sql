--
-- EC World Events - UNINSTALL (World Database)
-- Run this to completely remove the module tables
--

DROP TABLE IF EXISTS `ec_world_event_schedules`;
DROP TABLE IF EXISTS `ec_world_events`;

-- Verify removal
SELECT 'EC World Events tables removed from world database' AS status;
