-- Preserve complete strategy and AI-context values. VARCHAR(255) could
-- truncate larger exact strategy snapshots before event-return verification.
ALTER TABLE `playerbots_db_store`
    MODIFY COLUMN `value` TEXT DEFAULT NULL;
