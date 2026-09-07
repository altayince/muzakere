# Muzakere

Local, single-user KEP document workflow for a law office. C++20, Qt 6 Widgets,
SQLite. Original documents stay intact; uncertain matches require human review.

Implemented: import folders or ZIPs, preserve/hash documents, extract PDF text,
review accounting rows and generate an Excel workbook. No network services at runtime.

## Windows

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
PDF text and proposed envelope. Select rows and enter the service date if known;
dates in ZIP names or document headers are not assumed to be service dates.
**Seçilenleri kaydet** saves corrections without approval. **Seçilenleri onayla**
records your explicit review. **Onaylı Excel** exports approved rows; **İnceleme
Excel’i** exports all rows as a visibly marked draft. Conflicting envelope/recipient
rows cannot be approved; keep them in review. Existing output files are never overwritten.

The first sheet has the sample's 11 columns, frozen header and filter. **Kaynaklar**
holds stable record IDs, hashes, recipients and document links by ID; **İnceleme**
holds extraction snippets and manual edits. The reference workbook supplies the
layout only; none of its example debtor rows is bundled in the program.

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
Accounting return-sheet import and petition generation remain future increments.
Keep real client documents out of Git. See [ZIP-to-Excel details](docs/zip-to-excel.md).
