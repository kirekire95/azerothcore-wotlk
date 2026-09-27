--
-- EC World Events - UNINSTALL (Characters Database)
-- Run this to completely remove the module tables
--

DROP TABLE IF EXISTS `ec_world_event_participants`;
DROP TABLE IF EXISTS `ec_world_event_logs`;
DROP TABLE IF EXISTS `ec_world_event_rewards`;
DROP TABLE IF EXISTS `ec_world_event_bot_return_context`;

-- Verify removal
SELECT 'EC World Events tables removed from characters database' AS status;
