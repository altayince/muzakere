# Dependency decisions

Only Qt and Catch2 are introduced in this increment. Build setup downloads tools;
the compiled application performs no network requests. Domain and application
libraries need only the C++ standard library.

| Component | Decision and maintenance | License / alternative |
| --- | --- | --- |
| Qt Core, Widgets, Concurrent, SQL | Mature, actively maintained Qt 6 APIs; one package supplies UI, worker pool, SHA-256, MIME and SQLite adapter. Native Windows toolchain pins Qt 6.8.3 / MinGW 13.1; minimum Qt 6.4 also tested on Linux. Pin is a reproducibility baseline, not a claim of latest security patch. | LGPLv3 or commercial; use shared Qt. Alternative: wxWidgets plus separate hashing/SQL dependencies. |
| SQLite via Qt QSQLITE | Local transactional store without another wrapper or ORM. SQLite ships with the selected Qt driver. | SQLite public domain; Qt SQL LGPLv3/commercial. Alternative: direct sqlite3 C API, if independent SQLite patch cadence becomes necessary. |
| Catch2 3.8.1 | Test-only pinned dependency; maintained project. Downloaded on initial CMake configure, cached for later builds. | BSL-1.0; alternative GoogleTest BSD-3-Clause. |
| Logging | Error codes/internal UUIDs and SQLite audit now; no document-body logging. | No extra dependency. Consider spdlog (MIT) only when rotation/sinks become needed. |
| PDF, deferred | Candidate Qt PDF/PDFium for text extraction, behind TextExtractor. Qt PDF is maintained and fits the UI stack, but validate native build availability before adding it, particularly MinGW. | Qt PDF LGPLv3/commercial plus bundled third-party notices. Alternative standalone PDFium adapter (BSD-style, greater packaging cost). No PDF parser is shipped yet. |
| XLSX, deferred | Candidate QXlsx, a maintained Qt reader/writer; add only with accounting round-trip tests. | MIT. Alternative OpenXLSX (BSD-3-Clause). No spreadsheet library is shipped yet. |

Sources checked during setup: [Qt licensing](https://doc.qt.io/qt-6/licensing.html),
[Qt SQL and bundled SQLite](https://doc.qt.io/qt-6/qtsql-index.html),
[Catch2](https://github.com/catchorg/Catch2),
[Qt PDF](https://doc.qt.io/qt-6/qtpdf-index.html),
[QXlsx](https://github.com/QtExcel/QXlsx).
Distribution packaging must include the actual Qt and third-party notices and
meet the chosen license terms; there is no redistributable installer in this slice.
