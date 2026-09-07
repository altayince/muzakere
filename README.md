# Muzakere

Local, single-user KEP document workflow for a law office. C++20, Qt 6 Widgets,
SQLite. Original documents stay intact; uncertain matches require human review.

First milestone: import a local folder, preserve and hash documents, save them in
SQLite, and display batches. No network services at runtime.

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

## Linux / existing Qt toolchain

Install CMake 3.25+, Ninja, a C++20 compiler, Qt 6.4+ Core/Widgets/Concurrent/SQL
and the SQLite driver. Set `CMAKE_PREFIX_PATH` if Qt is outside standard paths.

```sh
cmake --preset dev
cmake --build --preset dev --parallel 4
ctest --preset dev
./build/dev/muzakere
```

See [architecture](docs/architecture.md), [dependency decisions](docs/dependencies.md),
[schema v1](migrations/001_initial.sql), and [workflow](WORKFLOW.md).
This initial milestone does not yet extract PDF fields, match envelopes, exchange
Excel sheets, or generate petitions. Keep real client documents out of Git.
