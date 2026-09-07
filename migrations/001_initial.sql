CREATE TABLE schema_migrations (
    version INTEGER PRIMARY KEY,
    applied_at TEXT NOT NULL
);
-- statement
CREATE TABLE processing_batches (
    id TEXT PRIMARY KEY,
    created_at TEXT NOT NULL,
    status TEXT NOT NULL CHECK (status IN ('imported', 'needs_review')),
    imported_count INTEGER NOT NULL CHECK (imported_count >= 0),
    duplicate_count INTEGER NOT NULL CHECK (duplicate_count >= 0),
    issue_count INTEGER NOT NULL CHECK (issue_count >= 0)
);
-- statement
CREATE TABLE stored_files (
    sha256 TEXT PRIMARY KEY CHECK (length(sha256) = 64 AND sha256 NOT GLOB '*[^a-f0-9]*'),
    managed_path TEXT NOT NULL UNIQUE,
    size_bytes INTEGER NOT NULL CHECK (size_bytes >= 0),
    mime_type TEXT NOT NULL
);
-- statement
CREATE TABLE case_records (
    id TEXT PRIMARY KEY,
    external_case_number TEXT,
    court_name TEXT,
    review_status TEXT NOT NULL DEFAULT 'needs_review' CHECK (review_status IN ('needs_review', 'approved'))
);
-- statement
CREATE TABLE incoming_documents (
    id TEXT PRIMARY KEY,
    batch_id TEXT NOT NULL REFERENCES processing_batches(id),
    file_sha256 TEXT NOT NULL REFERENCES stored_files(sha256),
    case_id TEXT REFERENCES case_records(id),
    original_filename TEXT NOT NULL,
    normalized_filename TEXT NOT NULL,
    source_path TEXT NOT NULL,
    imported_at TEXT NOT NULL,
    is_duplicate INTEGER NOT NULL CHECK (is_duplicate IN (0, 1))
);
-- statement
CREATE INDEX documents_by_batch ON incoming_documents(batch_id);
-- statement
CREATE INDEX documents_by_case ON incoming_documents(case_id);
-- statement
CREATE INDEX documents_by_hash ON incoming_documents(file_sha256);
-- statement
CREATE INDEX batches_by_created_at ON processing_batches(created_at DESC, id);
-- statement
CREATE TABLE review_issues (
    id TEXT PRIMARY KEY,
    batch_id TEXT NOT NULL REFERENCES processing_batches(id),
    source_path TEXT NOT NULL,
    code TEXT NOT NULL,
    status TEXT NOT NULL DEFAULT 'open' CHECK (status IN ('open', 'resolved'))
);
-- statement
CREATE INDEX issues_by_batch ON review_issues(batch_id);
-- statement
CREATE TABLE audit_events (
    sequence INTEGER PRIMARY KEY AUTOINCREMENT,
    occurred_at TEXT NOT NULL,
    event_type TEXT NOT NULL,
    entity_id TEXT NOT NULL,
    batch_id TEXT NOT NULL REFERENCES processing_batches(id)
);
-- statement
CREATE INDEX audit_by_entity ON audit_events(entity_id, sequence);
-- statement
CREATE TRIGGER audit_no_update BEFORE UPDATE ON audit_events
BEGIN SELECT RAISE(ABORT, 'audit events are append-only'); END;
-- statement
CREATE TRIGGER audit_no_delete BEFORE DELETE ON audit_events
BEGIN SELECT RAISE(ABORT, 'audit events are append-only'); END;
-- statement
INSERT INTO schema_migrations(version, applied_at) VALUES(1, strftime('%Y-%m-%dT%H:%M:%fZ', 'now'));
-- statement
PRAGMA user_version = 1;
