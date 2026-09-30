# Muzakere

Local, single-user KEP document workflow for a law office. C++20, Qt 6 Widgets,
SQLite. Original documents stay intact; uncertain matches require human review.

Implemented: import folders or ZIPs, preserve/hash documents, extract PDF text,
review accounting rows, export/import accounting workbooks and generate one VAR/YOK
response PDF per debtor row. No network services at runtime.

## Windows

For another user's PC, distribute `out/installer/setup.exe`; no developer tools or
Office installation are needed. See the [installation and test guide](docs/testing-installer.md).

Python 3.11+, Git and internet access are needed for initial tool setup. Tools are
installed only under ignored `.tools/`; no machine-wide compiler install.

```powershell
powershell -ExecutionPolicy Bypass -File scripts/dev.ps1 Setup
powershell -ExecutionPolicy Bypass -File scripts/dev.ps1 Build
powershell -ExecutionPolicy Bypass -File scripts/dev.ps1 Test
powershell -ExecutionPolicy Bypass -File scripts/dev.ps1 Run
```

Choose **Klasörden belge al** (`Ctrl+I`) to import a folder recursively. Select a
batch to inspect files and review issues. Identical bytes share one preserved
copy; each import occurrence retains its own UUID. Imports are not case approvals.
The default workspace is `%LOCALAPPDATA%/Muzakere/Muzakere/workspace`; pass
`--workspace <directory>` to the executable for a different location.

For the ZIP-to-Excel workflow, choose **ZIP arşivi al ve Excel satırlarını hazırla**.
The **Excel hazırlama / inceleme** tab shows editable fields, warnings, the source
PDF text and proposed envelope. New rows temporarily use the computer's local
date when prepared as the service date. You can edit it; saved rows keep their
date when reopened or exported. TODO (MUZ-5): obtain this date from the KEP record
during KEP integration, replacing the temporary default.
Older batches with missing, never-edited dates receive this default when opened
and require approval again. Existing dates and deliberate manual edits are preserved.
**Seçilenleri kaydet** saves corrections without approval. **Seçilenleri onayla**
records your explicit review. **Muhasebesiz hamdata oluştur** exports all saved
document rows to HAMDATA. **Muhasebeli ham data oluştur** also creates MUHASEBE,
deduplicated by TCKN/VKN with empty reply cells. Conflicting envelope/recipient
rows remain blocked for response generation. Existing files are never overwritten.

HAMDATA matches the supplied real workbook's 11 columns: service date, reply
deadline, delivery channel, subject, recipient, office, case, debt amount, debtor,
TCKN/VKN and creditor. Unknown deadline/channel values remain blank. Each debtor
has a separate document row with their own
TCKN/VKN, shared document/case details and the document's full amount (not divided;
amounts on sibling rows must not be added as independent debts). Multiple debtors
alone no longer generate a warning. Hidden **_MUZ** metadata retains source IDs
and blockers without adding visible columns. Real external HAMDATA/MUHASEBE
workbooks can also be imported without this metadata. Client rows are never bundled.

Rows are green when no issues are detected, yellow when review is needed, and red
when an approval blocker exists. The desktop updates colors as fields are edited;
The response tab also distinguishes warnings from blockers. Approval remains a separate action.
Existing combined rows split automatically only when source names and IDs match
exactly and were not manually edited; split rows require fresh approval.

## Linux / existing Qt toolchain

Install CMake 3.25+, Ninja, a C++20 compiler, Qt 6.4+ Core/Widgets/Concurrent/SQL,
Qt base private development headers (required by QXlsx), and the SQLite driver.
Set `CMAKE_PREFIX_PATH` if Qt is outside standard paths. Prepare native PDFium once:

```sh
python3 -m venv .tools/python
.tools/python/bin/pip install pypdfium2==5.13.0
.tools/python/bin/python scripts/prepare-pdfium.py
```

Python is only a build-time download/packaging tool; the runtime is native C++.

```sh
cmake --preset dev
cmake --build --preset dev --parallel 4
ctest --preset dev
./build/dev/muzakere
```

See [architecture](docs/architecture.md), [dependency decisions](docs/dependencies.md),
[schema v1](migrations/001_initial.sql), and [workflow](WORKFLOW.md).
The second main tab imports HAMDATA/MUHASEBE directly. A debtor's reply applies
to every associated case row; identical names with different IDs stay separate.
Numbers (including zero and signed amounts) select VAR, blank amounts select YOK.
Missing/duplicate identity matches and invalid cells block the affected rows.
The former random test export button has been removed. Preview the response,
check lawyer/address, then generate selected valid rows. The original ZIP is not
needed on the second PC. Legacy MUZ-11 workbooks remain readable.
See [accounting return details](docs/accounting-return.md).
Keep real client documents out of Git. See [ZIP-to-Excel details](docs/zip-to-excel.md).

For debugging, **TEST — Veritabanını sıfırla** clears every database record after
confirmation, including batches, approvals and audit history. The database schema
is recreated atomically. Source files and generated Excel files remain on disk;
you can import the same ZIP again. The button is disabled during processing.

## Web migration

The browser workflow now covers the current local end-to-end flow while keeping
the desktop app available. Start the headless server after a normal build:

```sh
./build/dev/muzakere_server --workspace /tmp/muz-web --host 127.0.0.1 --port 8080
```

Open `http://127.0.0.1:8080/` to run the two-stage web workflow:

1. upload a ZIP, open **Düzenle** for all review fields, save corrections, and download HAMDATA or HAMDATA + MUHASEBE directly;
2. upload the returned workbook, inspect VAR/YOK and row details, enter lawyer/address, select eligible rows, preview responses, generate PDFs and use each **PDF indir** action.

Both lists use compact tables with expandable details and scrolling contained
inside the table on smaller screens. Preview and PDF generation use the current
lawyer/address form values; saving the profile remains a separate action.
Excel/PDF download actions offer file name/location selection where the browser
supports Save As, otherwise they use the browser's download preferences. After
PDF generation, use the individual **PDF indir** buttons to save the files.

The web server remains a thin adapter over the C++ workspace/domain logic. The
browser does not implement accounting matching, approval, VAR/YOK, response text
or PDF business rules. Not implemented for web deployment yet: authentication,
Cloudflare/R2/D1 storage, PTT KEP, e-signature and sending integrations. See
[web migration](docs/web-migration.md).

Build and verify the installer after the Windows toolchain setup:

```powershell
powershell -ExecutionPolicy Bypass -File scripts/setup-installer-tools.ps1
powershell -ExecutionPolicy Bypass -File scripts/package.ps1
powershell -ExecutionPolicy Bypass -File scripts/smoke-package.ps1
```

`package.ps1 -ProfileFile <local.json>` optionally includes default `lawyer` and
`address` strings; keep this file outside Git. CI builds a generic installer as
the `muzakere-windows-setup` artifact after Windows tests and a deployed runtime test.
