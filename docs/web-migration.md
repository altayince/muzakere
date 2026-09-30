# Web migration

MUZ-15 starts the browser-first migration without replacing the existing desktop
application or duplicating business rules in JavaScript.

The first vertical slice is:

```text
browser
-> POST /api/imports with a ZIP file
-> muzakere_server
-> LocalWorkspace::import_archive
-> LocalWorkspace::prepare_accounting
-> review rows as JSON
-> browser table with green/yellow/red status
```

The HTTP layer is intentionally thin. Upload validation, ZIP safety checks, PDF
text extraction, debtor parsing, review warnings and approval blockers still run
through the existing C++ workspace/domain/infrastructure path. The frontend only
uploads a file and renders the returned review rows.

## Local development

Build the normal developer preset, then start the server:

```sh
cmake --preset dev
cmake --build --preset dev --parallel 4
./build/dev/muzakere_server --workspace /tmp/muz-web --host 127.0.0.1 --port 8080
```

Open `http://127.0.0.1:8080/` and upload a ZIP. The server also exposes:

```text
GET  /health
POST /api/imports
GET  /api/batches/{batchId}/review
POST /api/batches/{batchId}/review/save
POST /api/batches/{batchId}/review/approve
POST /api/batches/{batchId}/exports/hamdata
POST /api/batches/{batchId}/exports/hamdata-with-accounting
GET  /api/exports/{exportId}/download
```

`POST /api/imports` accepts either raw `application/zip` bytes or a
`multipart/form-data` upload with a file field named `file`. The response
contains the batch summary, import issues and review rows. Row status values are
`ready`, `review` and `blocked`, matching the desktop green/yellow/red semantics.

## Container

The repository includes a Dockerfile for Linux container readiness:

```sh
docker build -t muzakere-web .
docker run --rm -p 8080:8080 -v muzakere-data:/data muzakere-web
```

The Docker build compiles `muzakere_server`, runs the web integration tests and
executes the server health smoke test. Runtime data is kept under
`MUZ_WORKSPACE`, which defaults to `/data/workspace`; production deployment can
replace that storage implementation later without changing domain logic.

## Migration status

Implemented in MUZ-15:

- headless `muzakere_server`
- health endpoint
- ZIP upload endpoint
- embedded responsive browser UI for import/review display
- JSON serialization of existing C++ review rows
- server and upload integration tests
- Docker build and smoke test

Implemented in MUZ-19:

- persisted review-state fetch endpoint
- browser editing for the desktop review fields
- save endpoint that keeps approval separate
- explicit approval endpoint for eligible rows
- UI indicators for unsaved, saved, approved, warnings and blockers
- regression tests for persistence, approval and invalid API payloads

Implemented in MUZ-21:

- browser buttons for HAMDATA and HAMDATA + MUHASEBE workbook generation
- batch-scoped export endpoints that call `LocalWorkspace::export_hamdata`
- opaque export download IDs so local filesystem paths are never exposed to the browser
- approval-gated web export flow while preserving existing desktop exporter behavior
- integration tests for XLSX downloads, headers, invalid IDs, blockers and MUHASEBE deduplication

Not implemented yet:

- accounting return import
- VAR/YOK PDF generation and download
- authentication and authorization
- cloud object/database storage
- Cloudflare deployment
- PTT KEP, e-signature or sending integrations
