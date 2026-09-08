CREATE TABLE accounting_returns (
    id TEXT PRIMARY KEY, source_name TEXT NOT NULL, created_at TEXT NOT NULL,
    sha256 TEXT NOT NULL, managed_path TEXT NOT NULL
);
-- statement
CREATE TABLE response_rows (
    id TEXT PRIMARY KEY, return_id TEXT NOT NULL REFERENCES accounting_returns(id),
    position INTEGER NOT NULL, payload TEXT NOT NULL
);
-- statement
CREATE INDEX responses_by_return ON response_rows(return_id,position);
-- statement
CREATE TABLE response_exports (
    id TEXT PRIMARY KEY, row_id TEXT NOT NULL REFERENCES response_rows(id),
    created_at TEXT NOT NULL, output_path TEXT NOT NULL, sha256 TEXT NOT NULL,
    template_sha256 TEXT NOT NULL, status TEXT NOT NULL CHECK(status IN ('prepared','published'))
);
-- statement
CREATE TABLE response_profile (id INTEGER PRIMARY KEY CHECK(id=1), lawyer TEXT NOT NULL, address TEXT NOT NULL);
-- statement
CREATE TABLE response_events (
    sequence INTEGER PRIMARY KEY AUTOINCREMENT, return_id TEXT NOT NULL REFERENCES accounting_returns(id),
    occurred_at TEXT NOT NULL, event_type TEXT NOT NULL, entity_id TEXT NOT NULL
);
-- statement
CREATE TRIGGER response_no_update BEFORE UPDATE ON response_events
BEGIN SELECT RAISE(ABORT,'response events are append-only'); END;
-- statement
CREATE TRIGGER response_no_delete BEFORE DELETE ON response_events
BEGIN SELECT RAISE(ABORT,'response events are append-only'); END;
-- statement
INSERT INTO schema_migrations(version,applied_at) VALUES(4,strftime('%Y-%m-%dT%H:%M:%fZ','now'));
-- statement
PRAGMA user_version=4;
