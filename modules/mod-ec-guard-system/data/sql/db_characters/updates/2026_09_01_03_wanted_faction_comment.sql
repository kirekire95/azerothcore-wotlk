-- ec_guard_wanted_state persists raw AzerothCore TeamId values.
-- Correct the old inverted schema comment; no row conversion is required.
ALTER TABLE `ec_guard_wanted_state`
    MODIFY COLUMN `crime_faction` TINYINT UNSIGNED NOT NULL DEFAULT 0
    COMMENT 'AzerothCore TeamId of offended faction (0=Alliance, 1=Horde, 2=Neutral)';
