# symbols

A long-running C++ symbol server for Emacs.  
It parses C/C++ source files with tree-sitter, maintains a fuzzy-searchable
index, and serves interactive queries from an Emacs client over a
JSON-over-stdio protocol.

---

## What it does

- Walks a project directory (or a configurable sub-tree) and extracts every
  function, class, struct, enum, `using` alias, and typedef.
- Caches the index to `<project_root>/.cache/symbols-index.json` so cold
  restarts are fast.
- Stays alive as a persistent subprocess; Emacs sends a query line and gets
  back a ranked result list in milliseconds.
- The Emacs client (`symbols-server.el`) wires this into `consult` so the
  user sees live-filtered results in the minibuffer as they type, with
  file-preview and jump-to-definition.

---

## Project status

| Component | Status |
|---|---|
| `CMakeLists.txt` — tree-sitter + dc via FetchContent | **done** |
| `src/scanner` — recursive C/C++ file walker | **done** |
| `src/parser` — tree-sitter symbol extractor | **done** |
| `src/json` — hand-rolled JSON parser/serializer | **done** |
| `src/indexer` — index build, cache, fuzzy search | **done** |
| `src/protocol` — request/response codec | **done** |
| `src/server` — main stdio loop | **done** |
| `tests/` — 19 passing tests (json, parser, indexer) | **done** |
| `symbols-server.el` — Emacs client | **done** |
| Incremental rebuild (mtime-based) | not started |
| Parallel parsing (thread pool) | not started |

The binary is built at `build/symbols.exe` (Windows) / `build/symbols`
(Linux/macOS) and is functional end-to-end.  Indexing the symbols project
itself produces ~3 000 symbols from ~170 files in roughly 10 s on first run;
subsequent startups load from cache in under a second.

---

## Repository layout

```
symbols/
├── src/
│   ├── main.cpp          Entry point, CLI argument parsing
│   ├── scanner.hpp/.cpp  Recursive directory walk, C/C++ file discovery
│   ├── parser.hpp/.cpp   tree-sitter symbol extraction
│   ├── indexer.hpp/.cpp  Index build, disk cache, fuzzy search with scoring
│   ├── json.hpp/.cpp     Hand-rolled JSON parser and serializer
│   ├── protocol.hpp/.cpp JSON request/response codec
│   └── server.hpp/.cpp   Main server loop (stdin → process → stdout)
├── tests/
│   ├── main_test.cpp     dtest entry point
│   ├── json.test.cpp     8 JSON tests
│   ├── parser.test.cpp   9 parser tests
│   └── indexer.test.cpp  2 indexer tests
├── CMakeLists.txt
├── AGENTS.md             Agent/contributor guide (build, style, dc API gotchas)
└── README.md             This file
```

Emacs client lives in the repo and is deployed to `.emacs.d`:

```
emacs/
├── symbols-server.el      Emacs client — process management + consult UI
├── deploy.py              Copies symbols-server.el to the local .emacs.d
├── deploy.local.example   Template for the deploy target path
└── deploy.local           Your machine-specific target (not tracked by git)
```

---

## Building

Requires: CMake ≥ 3.20, Ninja, a C++23 compiler (MSVC 2022, GCC 13+, or
Clang 17+), and internet access on first configure (FetchContent downloads
`dc` and the tree-sitter libraries).

```bash
# Configure
cmake -DCMAKE_EXPORT_COMPILE_COMMANDS=ON -DCMAKE_BUILD_TYPE=Debug -G "Ninja" -B build

# Build everything (server + tests)
cmake --build build

# Build only the server binary
cmake --build build --target symbols

# Build only the test binary
cmake --build build --target test
```

---

## Running the server manually

```bash
# Index a project, use cache (default)
./build/symbols.exe --root C:/dev/myproject

# Restrict scanning to a sub-tree
./build/symbols.exe --root C:/dev/TnT --search-dir Code/DICE/Extensions/BattlefieldOnline

# Force a fresh index (ignore cache)
./build/symbols.exe --root C:/dev/myproject --no-cache
```

On startup the server writes one line to stdout once the index is ready:

```json
{"id":0,"status":"ready","files":171,"symbols":3015}
```

Then it reads requests and writes responses, one JSON object per line.

---

## JSON protocol

All communication is line-delimited JSON on stdin/stdout.

### Requests (Emacs → server)

```json
{"id":1,"method":"query","params":{"pattern":"StatCateg","limit":200}}
{"id":2,"method":"status"}
{"id":3,"method":"rebuild"}
{"id":4,"method":"shutdown"}
```

### Responses (server → Emacs)

```json
{"id":0,"status":"ready","files":171,"symbols":3015}
{"id":1,"symbols":[{"name":"StatCategory","kind":"class","file":"src/stats.h","line":42,"score":500},...]}
{"id":2,"status":"ready","files":171,"symbols":3015}
{"id":3,"status":"rebuilt"}
{"id":4,"status":"shutdown"}
```

`score` is the fuzzy-match score: exact name match = 1000, prefix = 500+,
subsequence with word-boundary bonuses = lower values.

---

## Emacs client — deploy workflow

`emacs/symbols-server.el` is the canonical source.  After every edit to it,
run the deploy script to copy it into your local `.emacs.d`:

```bash
python emacs/deploy.py
```

The destination directory is read from `emacs/deploy.local` (not tracked by
git, so it stays machine-specific).  On first use:

```bash
cp emacs/deploy.local.example emacs/deploy.local
# edit deploy.local and set your .emacs.d path, e.g.:
#   C:/Users/you/AppData/Roaming/.emacs.d
```

Then reload the file in Emacs (`M-x load-file RET ... symbols-server.el RET`)
or restart Emacs.

---

## Emacs usage

`M-s M-s` opens the consult minibuffer.  Type any substring of a symbol
name; the server does the fuzzy filtering and returns the top 200 matches.
Hovering over a candidate previews the file; `RET` jumps there (pushing an
xref marker so `M-,` returns you).

`C-u M-s M-s` forces an index rebuild before searching.

Additional commands:

| Command | Description |
|---|---|
| `symbols-server-shutdown` | Gracefully stop the server for the current project |
| `symbols-server-restart` | Kill and restart the server |

The server is started lazily on the first `M-s M-s` in a project and stays
alive until Emacs exits or you call `symbols-server-shutdown`.

---

## Symbols extracted

| tree-sitter node | Kind reported | Notes |
|---|---|---|
| `function_definition` | `function` | All function definitions |
| `declaration` (with `function_declarator`) | `function` | Forward-declared functions |
| `field_declaration` (with `function_declarator`) | `function` | Method declarations in class bodies |
| `class_specifier` | `class` | Skips forward declarations (no body) |
| `struct_specifier` | `struct` | Skips forward declarations (no body) |
| `enum_specifier` | `enum` | Plain and `enum class`/`enum struct` |
| `alias_declaration` | `alias` | `using Foo = ...;` |
| `type_definition` | `typedef` | `typedef ... Foo;` |

---

## Dependencies

| Dependency | Version | How fetched |
|---|---|---|
| [`dc`](https://github.com/dumheter/dc) | HEAD | `FetchContent` |
| [tree-sitter](https://github.com/tree-sitter/tree-sitter) | 0.26.6 | `FetchContent` (CMake bypassed with `SOURCE_SUBDIR _no_cmake_subdir_`) |
| [tree-sitter-cpp](https://github.com/tree-sitter/tree-sitter-cpp) | 0.23.4 | `FetchContent` (grammar files compiled directly as C11) |
