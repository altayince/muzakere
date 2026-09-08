# Architecture and MVP boundaries

MUZ-3 adds native ZIP/PDF import, persisted field/matching review and accounting
Excel export on top of the initial slice below. See [ZIP-to-Excel](zip-to-excel.md)
for import behavior and [accounting returns](accounting-return.md) for schema v4,
the second workflow tab and response generation.

The first increment is the complete **folder -> preserved bytes -> SQLite ->
batch screen** slice. Imported does not mean legally approved. No case links are
inferred; `case_id` stays null until a later human review use case.

```text
src/ui                 Qt Widgets, tables, async import, visible review problems
src/application        ImportBatch + narrow repository/file/identity ports
src/domain             C++20 value types and validation; no Qt or database
src/infrastructure     Qt filesystem/hash, SQLite adapter, composition facade
src/main.cpp           Composition, single workspace, startup, smoke test
include/muz            Public headers following the same boundaries
migrations             Immutable versioned SQLite schema
tests/fixtures         Synthetic test data only
scripts                Repeatable local setup/build/test/run
```

Dependencies point inward. The UI receives a Workspace interface; it contains no
SQL. The SQLite connection belongs to its calling thread and uses prepared
statements, foreign keys, WAL and FULL synchronization. A transaction owns batch,
document, review and audit writes. The schema has `user_version` plus migration
history; newer versions fail closed. Audit events use internal identifiers and
UTC timestamps, with triggers refusing updates/deletes. This is application-level
append-only storage, not tamper-proof protection against a database administrator.

Initial entities: ProcessingBatch, IncomingDocument (one import occurrence),
StoredFile (unique SHA-256 bytes), ReviewIssue, CaseRecord (internal UUID distinct
from external identifiers), and audit_events. Envelope, pair, extracted-field,
accounting and petition tables come with their implementing use cases in later
migrations; no speculative ORM or empty subsystems.

Each file is streamed to a temporary file, hashed, verified against a second
source read, and published under `originals/<hash-prefix>/<sha256>` without
overwrite. Existing content is verified before reuse. Identical bytes do not
imply identical legal cases. Original names, normalized display names, source
paths, managed paths, sizes, MIME and import timestamps stay in the database.
Display names are never storage paths or identifiers. Source documents are never
modified. Content-based MIME classification is not document parsing/validation.

```text
workspace/
  workspace.lock
  database/muzakere.sqlite3
  originals/<prefix>/<sha256>
  staging/                 temporary copies, not legal records
  batches/
  generated/
  exports/
  templates/
  logs/
```

The app uses a per-user local data directory, not the checkout. A process lock
allows one application per workspace. Imports run on a worker, keeping the UI
responsive; a running import must finish before window close. Errors are displayed
in a batch table rather than per-file popups. Source symlinks are not followed;
the workspace and its parent cannot be imported. A missing/empty source fails
without creating a misleading successful batch.

Files and SQLite cannot share a transaction: a database failure or process crash
may leave unreferenced content in originals or stale staging files. These are
retained, never automatically deleted; retry can reuse verified originals. A
committed batch is written only after content publication. Storage is ordinary
local disk, not a hardware power-loss durability guarantee. A backup must cover
both SQLite and originals with the app closed. Encryption, signed audit records,
automatic orphan recovery, cancellation and large-history pagination are future
work. Do not treat this first increment as production acceptance of those areas.

## Use cases and next increments

1. Implemented: initialize workspace/schema, import folder, hash/preserve/dedup,
   save batch + issues + audit atomically, list batches/documents/problems.
2. Implemented in MUZ-3: text PDF extraction with source snippets/method/confidence; scanned PDFs
   marked OCR/manual review. Identifier normalization and validation.
3. Implemented in MUZ-3: matching proposals plus evidence and score;
   conflicting signals always require review. Manual pair reassignment remains future work.
4. Review extracted fields and create/update approved cases with stable UUIDs.
5. Implemented: XLSX export and return import keyed by stable row IDs, with
   duplicate/missing/new/changed identity validation. Rows can be reordered.
6. Implemented in MUZ-11: bundled VAR/YOK templates, profile fields, HTML preview
   and native Qt PDF generation for approved rows (or explicitly marked test data).
   Each output and rendered template has a recorded SHA-256 and publication state.

Backend, web, KEP transmission, e-signature, browser automation, cloud, LLMs and
OCR services remain outside MVP. No network calls exist in application code.
Expected I/O errors become per-file review issues; infrastructure failures abort
the use case. RAII controls transactions, file handles, connections and UI jobs.
