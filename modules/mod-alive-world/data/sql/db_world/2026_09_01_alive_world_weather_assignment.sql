-- Wire the already-registered AliveWorldWeatherScript to weather-enabled zones.
-- Preserve any pre-existing third-party ScriptName assignment.

CREATE TABLE IF NOT EXISTS `ec_alive_world_game_weather_script_backup` LIKE `game_weather`;

INSERT IGNORE INTO `ec_alive_world_game_weather_script_backup`
SELECT *
FROM `game_weather`
WHERE `ScriptName` IS NULL OR `ScriptName` = '';

UPDATE `game_weather`
SET `ScriptName` = 'AliveWorldWeatherScript'
WHERE `ScriptName` IS NULL OR `ScriptName` = '';
