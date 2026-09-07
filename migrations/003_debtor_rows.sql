CREATE TABLE accounting_rows_v3 (
    id TEXT PRIMARY KEY,
    document_id TEXT NOT NULL REFERENCES incoming_documents(id),
    batch_id TEXT NOT NULL REFERENCES processing_batches(id),
    payload TEXT NOT NULL,
    approved INTEGER NOT NULL DEFAULT 0 CHECK(approved IN (0,1))
);
-- statement
INSERT INTO accounting_rows_v3(id,document_id,batch_id,payload,approved)
SELECT id,id,batch_id,payload,approved FROM accounting_rows;
-- statement
DROP TABLE accounting_rows;
-- statement
ALTER TABLE accounting_rows_v3 RENAME TO accounting_rows;
-- statement
CREATE INDEX accounting_by_batch ON accounting_rows(batch_id);
-- statement
CREATE INDEX accounting_by_document ON accounting_rows(document_id);
-- statement
INSERT INTO schema_migrations(version,applied_at) VALUES(3,strftime('%Y-%m-%dT%H:%M:%fZ','now'));
-- statement
PRAGMA user_version=3;
