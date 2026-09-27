CREATE TABLE IF NOT EXISTS `ec_world_settlement_node_effects` (
  `node_id` INT UNSIGNED NOT NULL,
  `effect_type` VARCHAR(32) NOT NULL,
  `effect_value` INT NOT NULL DEFAULT 0,
  PRIMARY KEY (`node_id`, `effect_type`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

DELETE FROM `ec_world_settlement_node_effects` WHERE `node_id` IN (1, 2, 3);
INSERT INTO `ec_world_settlement_node_effects`
(`node_id`, `effect_type`, `effect_value`)
VALUES
(1, 'treasury_income_pct', 100),
(2, 'regional_reinforcement', 100),
(3, 'regional_trade_pct', 100);
