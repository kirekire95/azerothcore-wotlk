-- EC World Events: crash-safe Playerbot pre-event state
CREATE TABLE IF NOT EXISTS `ec_world_event_bot_return_context` (
    `bot_guid` INT UNSIGNED NOT NULL COMMENT 'Character GUID low part',
    `event_instance_id` INT UNSIGNED NOT NULL,
    `event_id` INT UNSIGNED NOT NULL,
    `map_id` INT UNSIGNED NOT NULL,
    `position_x` FLOAT NOT NULL,
    `position_y` FLOAT NOT NULL,
    `position_z` FLOAT NOT NULL,
    `orientation` FLOAT NOT NULL,
    `combat_strategies` TEXT NOT NULL,
    `non_combat_strategies` TEXT NOT NULL,
    `dead_strategies` TEXT NOT NULL,
    `captured_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    PRIMARY KEY (`bot_guid`),
    KEY `idx_event_instance` (`event_instance_id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci
  COMMENT='Crash-safe return state for playerbots recruited into EC world events';
