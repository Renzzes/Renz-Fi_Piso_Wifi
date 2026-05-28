-- Deduplication support for sync replays.
-- Only enforce uniqueness for non-null external_id values.
CREATE UNIQUE INDEX IF NOT EXISTS idx_sales_external_id_unique
  ON sales_transactions(external_id)
  WHERE external_id IS NOT NULL;

