CREATE TABLE batch_archives (
    batch_id TEXT PRIMARY KEY REFERENCES processing_batches(id),
    original_name TEXT NOT NULL,
    sha256 TEXT NOT NULL,
    managed_path TEXT NOT NULL,
    size_bytes INTEGER NOT NULL
);
-- statement
CREATE TABLE accounting_rows (
    id TEXT PRIMARY KEY REFERENCES incoming_documents(id),
    batch_id TEXT NOT NULL REFERENCES processing_batches(id),
    payload TEXT NOT NULL,
    approved INTEGER NOT NULL DEFAULT 0 CHECK(approved IN (0,1))
);
-- statement
CREATE INDEX accounting_by_batch ON accounting_rows(batch_id);
-- statement
CREATE TABLE accounting_exports (
    id TEXT PRIMARY KEY,
    batch_id TEXT NOT NULL REFERENCES processing_batches(id),
    created_at TEXT NOT NULL,
    output_path TEXT NOT NULL,
    sha256 TEXT NOT NULL,
    row_count INTEGER NOT NULL,
    is_draft INTEGER NOT NULL CHECK(is_draft IN (0,1)),
    status TEXT NOT NULL DEFAULT 'prepared' CHECK(status IN ('prepared','published'))
);
-- statement
INSERT INTO schema_migrations(version, applied_at) VALUES(2, strftime('%Y-%m-%dT%H:%M:%fZ','now'));
-- statement
PRAGMA user_version=2;
