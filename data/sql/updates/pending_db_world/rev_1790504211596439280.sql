ALTER TABLE `ec_world_settlement_nodes`
  ADD COLUMN `rebuild_progress` INT UNSIGNED NOT NULL DEFAULT 0 AFTER `health_pct`,
  ADD COLUMN `rebuild_cost` INT UNSIGNED NOT NULL DEFAULT 500 AFTER `rebuild_progress`;

UPDATE `ec_world_settlement_nodes`
SET `rebuild_cost` = CASE `node_type`
  WHEN 'supplies' THEN 400
  WHEN 'trade' THEN 500
  ELSE 500
END
WHERE `id` IN (1, 2, 3);
