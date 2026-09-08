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
records your explicit review. **Onaylı Excel** exports approved rows; **İnceleme
Excel’i** exports all rows as a visibly marked draft. Conflicting envelope/recipient
rows cannot be approved; keep them in review. Existing output files are never overwritten.

The first sheet keeps the sample's 11 columns and adds **Muhatap**, **Uyarılar**
and an empty rightmost **Muhasebe** column,
with a frozen header and filter. Each debtor has a separate row with their own
TCKN/VKN, shared document/case details and the document's full amount (not divided;
amounts on sibling rows must not be added as independent debts). Multiple debtors
alone no longer generate a warning. **Kaynaklar**
holds stable record IDs, hashes, recipients and document links by ID; **İnceleme**
holds extraction snippets and manual edits. The reference workbook supplies the
layout only; none of its example debtor rows is bundled in the program.

Rows are green when no issues are detected, yellow when review is needed, and red
when an approval blocker exists. The desktop updates colors as fields are edited;
Excel uses the same saved-data classification. Approval remains a separate action.
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
The second main tab imports the returned workbook: a number (including zero)
means VAR; a blank cell means YOK. Invalid cells and changed debtor identities
block PDF generation for that row. Preview the response, check lawyer/address,
then generate selected valid rows. The original ZIP is not needed on the second PC.
The separate **Test: muhasebe dönüşü oluştur** export simulates accounting using
random amounts and blanks; its PDFs visibly identify test data.
See [accounting return details](docs/accounting-return.md).
Keep real client documents out of Git. See [ZIP-to-Excel details](docs/zip-to-excel.md).

For debugging, **TEST — Veritabanını sıfırla** clears every database record after
confirmation, including batches, approvals and audit history. The database schema
is recreated atomically. Source files and generated Excel files remain on disk;
you can import the same ZIP again. The button is disabled during processing.

Build and verify the installer after the Windows toolchain setup:

```powershell
powershell -ExecutionPolicy Bypass -File scripts/setup-installer-tools.ps1
powershell -ExecutionPolicy Bypass -File scripts/package.ps1
powershell -ExecutionPolicy Bypass -File scripts/smoke-package.ps1
```

`package.ps1 -ProfileFile <local.json>` optionally includes default `lawyer` and
`address` strings; keep this file outside Git. CI builds a generic installer as
the `muzakere-windows-setup` artifact after Windows tests and a deployed runtime test.
