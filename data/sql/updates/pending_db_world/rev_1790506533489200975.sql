INSERT INTO `ec_world_settlement_node_effects`
(`node_id`, `effect_type`, `effect_value`)
VALUES
(1, 'guard_capacity_pct', 25),
(1, 'guard_response_pct', 20),
(2, 'reinforcement_response_pct', 25)
ON DUPLICATE KEY UPDATE `effect_value` = VALUES(`effect_value`);
