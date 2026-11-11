# Agent guidance

## Project overview

HeaderScope is a local C/C++ include-dependency analyzer with a Qt desktop app
and a command-line inspection tool. Read `README.md` for usage and
`docs/architecture.md` for module boundaries and the analysis contract.
For window changes, also read `app/window/README.md`.

## Repository map

- `include/headerscope/analysis.hpp`: shared `ScanResult`, edges, diagnostics,
  and status types. Keep this contract independent of Qt and Clang.
- `backend/scanner/`: Clang LibTooling implementation of `scanIncludes`.
- `backend/graph/`: standard-library graph algorithms, reports, and exports.
  Keep the graph library independent of Qt and Clang.
- `app/window/`: Qt Widgets UI, background analysis, and `GraphPlaceholder`.
- `app/graph_view/`: integration target for the future graph widget. Preserve
  the `HeaderScope::graph_view` target when implementing it.
- `demo/fixtures/`: shared in-memory analysis fixtures.
- `demo/include-chain/`: small CMake project for scanner and manual checks.
- `demo/inspect/`: `headerscope_inspect`, snapshots, and CLI tests.
- `demo/project_generator/`: generated test projects described by `samples.json`.
- `.github/workflows/`: scanner and Qt build/test configurations.

## Build and test

Run commands from the repository root. Use out-of-source `build*` directories.
The baseline requirements are CMake 3.21+ and a C++20 compiler. The app needs
Qt 6.5+ (Widgets and Concurrent, plus Test when testing). The scanner needs
LLVM/Clang 18+ development libraries; its CLI tests also need Python 3.9+.
`BUILD_TESTING` defaults to ON through CTest.

Core without Qt or Clang:

```sh
cmake -S . -B build-core -DHEADERSCOPE_BUILD_APP=OFF -DHEADERSCOPE_BUILD_SCANNER=OFF
cmake --build build-core
ctest --test-dir build-core --output-on-failure
```

Scanner, graph reports, and CLI without Qt:

```sh
cmake -S . -B build-inspect -DHEADERSCOPE_BUILD_APP=OFF -DHEADERSCOPE_BUILD_SCANNER=ON
cmake --build build-inspect
ctest --test-dir build-inspect --output-on-failure
```

Desktop app (scanner is OFF by default):

```sh
cmake -S . -B build-window -DHEADERSCOPE_BUILD_APP=ON -DHEADERSCOPE_BUILD_SCANNER=OFF
cmake --build build-window
ctest --test-dir build-window --output-on-failure
```

Set `CMAKE_PREFIX_PATH` for a Qt or LLVM installation when needed; LLVM can
also be located with `LLVM_DIR` and `Clang_DIR`. Do not hard-code local SDK
paths into tracked files. For real-analysis UI changes, also build and test
with `HEADERSCOPE_BUILD_SCANNER=ON`, as the Qt CI matrix does.

CTest names include `graph_export`, `scanner`, `reports`, `inspect_e2e`,
`inspect_e2e.graphs`, `export_cli_arguments`, and `window`, depending on the
enabled modules. The window test already uses Qt's offscreen platform.
Run checks appropriate to the changed modules and report any missing
dependencies or checks that could not run. Documentation-only edits do not
require a compilation.

## Implementation conventions

- Follow the surrounding C++ style: four-space indentation, existing brace
  placement, and the `headerscope` namespace. Use the existing CMake targets
  and keep dependencies within their module boundaries.
- File identity is a normalized absolute `std::filesystem::path`. Preserve
  UTF-8 paths at UI and export boundaries and support macOS and Windows.
- An include edge points from the including file to the included header;
  line numbers are one-based. Include all edge endpoints and analyzed
  sources without includes in `ScanResult::files`.
- Preserve directives on different source lines in scanner output. Graph
  traversal deduplicates file pairs; affected-file queries traverse reverse
  edges. Keep externally visible graph and export ordering deterministic.
- Preserve the distinction between complete, partial, and failed analysis.
  Keep usable partial results and diagnostics; do not invent edges for
  unresolved headers. Snapshot saving and comparison require complete data.
- Keep analysis off the UI thread and deliver widget updates on the main
  thread. Respect the scanner's lack of cancellation and the existing guard
  against concurrent runs.
- Add meaningful regression coverage for behavior changes using the existing
  tests and fixtures. Generator samples belong in
  `demo/project_generator/samples.json`; generated projects and build outputs
  do not belong in source control.
- Keep user-facing documentation and UI text consistent with the existing
  Russian wording. Update relevant documentation when behavior, commands,
  or shared contracts change.
