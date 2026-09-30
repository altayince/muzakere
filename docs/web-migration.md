# Web migration

The browser migration keeps the desktop application available and does not
duplicate business rules in JavaScript.

The current local browser workflow is:

```text
ZIP upload
-> review/edit/save
-> HAMDATA or HAMDATA + MUHASEBE export
-> accounting return upload
-> VAR/YOK review
-> response preview
-> selected response PDF generation/download
```

The HTTP layer is intentionally thin. Upload validation, ZIP safety checks, PDF
text extraction, debtor parsing, review warnings, approval blockers, accounting
return matching, response preview HTML and response PDF generation still run
through the existing C++ workspace/domain/infrastructure path. The frontend only
presents state, gathers user choices and calls those endpoints.

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
POST /api/accounting-returns
GET  /api/accounting-returns/{returnId}
GET  /api/response-profile
POST /api/response-profile
POST /api/accounting-returns/{returnId}/responses/{rowId}/preview
POST /api/accounting-returns/{returnId}/response-exports
GET  /api/response-exports/{exportId}
GET  /api/response-exports/{exportId}/files/{fileId}
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
- web export flow that calls the existing core exporter without adding approval or validation gates
- integration tests for XLSX downloads, headers, invalid IDs, blockers and MUHASEBE deduplication

Implemented in MUZ-23:

- browser upload for returned HAMDATA/MUHASEBE `.xlsx` workbooks
- accounting-return endpoints backed by `LocalWorkspace::import_accounting_return` and `response_rows`
- persisted return review reload by return id
- browser table for VAR/YOK, warnings and blockers from existing C++ matching logic
- integration tests for numeric, zero, signed and blank returns, fanout, duplicate/missing matches, invalid cells and malformed uploads

Implemented in MUZ-25:

- browser response profile fields for lawyer and address, persisted through `LocalWorkspace::save_response_profile`
- response preview endpoint backed by `LocalWorkspace::preview_response`
- response PDF export endpoint backed by `LocalWorkspace::generate_responses`, storing generated PDFs under the workspace-managed `generated/responses` tree
- selectable valid accounting-return rows and one generated VAR/YOK PDF per selected row
- opaque PDF export/file download IDs so local filesystem paths are never exposed to the browser
- integration tests for profile persistence, preview, VAR/YOK PDF creation, PDF download headers/content and invalid/blocked row failures

Implemented in MUZ-27:

- three-stage browser layout for ZIP review, accounting return and response PDF generation
- row-level and selected-row response preview UI backed by the existing preview endpoint
- blocked response rows remain unpreviewable in the browser
- stale response preview/download state is cleared when a new ZIP or accounting return flow starts
- static web asset tests for preview wiring, valid-row selection and stale-state cleanup hooks

Not implemented yet:
- authentication and authorization
- cloud object/database storage
- Cloudflare deployment
- PTT KEP, e-signature or sending integrations
