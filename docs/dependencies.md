# Dependency decisions

Qt, Catch2, PDFium, QXlsx and miniz are used. Build setup downloads tools;
the compiled application performs no network requests. Domain and application
libraries need only the C++ standard library.

| Component | Decision and maintenance | License / alternative |
| --- | --- | --- |
| Qt Core, Gui, Widgets, Concurrent, SQL, Network | Mature, actively maintained Qt 6 APIs; one package supplies UI, worker pool, SHA-256, MIME, SQLite adapter and the first thin HTTP server. Native Windows toolchain pins Qt 6.8.3 / MinGW 13.1; minimum Qt 6.4 also tested on Linux. Pin is a reproducibility baseline, not a claim of latest security patch. | LGPLv3 or commercial; use shared Qt. Alternative: wxWidgets or a separate HTTP stack plus separate hashing/SQL dependencies. |
| SQLite via Qt QSQLITE | Local transactional store without another wrapper or ORM. SQLite ships with the selected Qt driver. | SQLite public domain; Qt SQL LGPLv3/commercial. Alternative: direct sqlite3 C API, if independent SQLite patch cadence becomes necessary. |
| Catch2 3.8.1 | Test-only pinned dependency; maintained project. Downloaded on initial CMake configure, cached for later builds. | BSL-1.0; alternative GoogleTest BSD-3-Clause. |
| Logging | Error codes/internal UUIDs and SQLite audit now; no document-body logging. | No extra dependency. Consider spdlog (MIT) only when rotation/sinks become needed. |
| PDFium | Native PDFium 153.0.7999.0 distributed by pinned pypdfium2 5.13.0, actively maintained upstream. A single-threaded C++ helper loads the stable C API; documents never pass through Python at runtime. Source PDF limits and a worker timeout protect the UI from routine parser failures. | BSD-style plus bundled third-party notices, copied next to build outputs. Alternative Qt PDF; standalone PDFium avoids Qt PDF's toolchain/packaging restrictions. |
| QXlsx 1.5.1.1 | Maintained Qt XLSX reader/writer, integrated with CMake. Used for typed cells, formatting, worksheets and round-trip tests. Private Qt headers needed at build time. | MIT. Alternative OpenXLSX (BSD-3-Clause), which adds a separate XML/ZIP stack. |
| miniz 3.1.2 | Maintained native ZIP implementation; archive stats allow checking entry sizes/paths before decompression, and extraction validates CRC. Also adds frozen headers/filter metadata to the QXlsx output using Qt's XML stream API. | MIT. Alternative libarchive, broader format/dependency surface; QXlsx's private ZIP reader does not expose the required import limits. |

Sources checked during setup: [Qt licensing](https://doc.qt.io/qt-6/licensing.html),
[Qt SQL and bundled SQLite](https://doc.qt.io/qt-6/qtsql-index.html),
[Catch2](https://github.com/catchorg/Catch2),
[Qt PDF](https://doc.qt.io/qt-6/qtpdf-index.html),
[QXlsx](https://github.com/QtExcel/QXlsx).
[PDFium distribution and licensing](https://github.com/pypdfium2-team/pypdfium2),
[miniz](https://github.com/richgel999/miniz).
Distribution packaging must include the actual Qt and third-party notices and
meet the chosen license terms. MUZ-11 adds Inno Setup 6.4.3 as a build-only tool;
its pinned official installer and Qt Base source archive are SHA-256 verified.
`package.ps1` bundles shared Qt/runtime libraries, PDFium, all collected notices
and the matching Qt Base source archive. Application runtime does not use Python.
See [Inno Setup 6.4.3 license](https://github.com/jrsoftware/issrc/blob/is-6_4_3/license.txt).
