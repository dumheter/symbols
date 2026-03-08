# AGENTS.md — symbols

C++23 project by Christoffer Gustafsson. Uses CMake + Ninja and the author's own
[`dc`](https://github.com/dumheter/dc) utility library fetched via `FetchContent`.

See `README.md` for a full project overview, protocol docs, and Emacs usage.

---

## Emacs Client — Deploy After Every Edit

`emacs/symbols-server.el` is the canonical source tracked in this repo.
**After every change to it you must run the deploy script** to copy it to the
local `.emacs.d` where Emacs actually loads it from:

```bash
python emacs/deploy.py
```

The destination is configured in `emacs/deploy.local` (machine-specific, not
tracked by git).  See `emacs/deploy.local.example` for the format.

---

## Build Commands

```bash
# Configure (debug, with compile_commands.json for tooling)
cmake -DCMAKE_EXPORT_COMPILE_COMMANDS=ON -DCMAKE_BUILD_TYPE=Debug -G "Ninja" -B build

# Build everything (main binary + test binary)
cmake --build build

# Build only the main binary
cmake --build build --target symbols

# Build only the test binary
cmake --build build --target test
```

Warnings are treated as errors (`-Werror` / `/WX`). Fix all warnings before committing.

---

## Test Commands

Test framework: **dtest** (part of the `dc` library, enabled via `DC_ENABLE_LIB_DTEST ON`).

```bash
# Run all tests
./build/test.exe          # Windows
./build/test              # Linux / macOS

# Run a single test by name (substring filter)
./build/test.exe --gtest_filter=*<testName>
./build/test --gtest_filter=*<testName>
```

### Writing Tests

Test files are named `<module>.test.cpp` and placed in `tests/`.

```cpp
#include <dc/dtest.hpp>
#include "your_module.hpp"

DTEST(descriptiveTestName)
{
    ASSERT_TRUE(someCondition);
    ASSERT_FALSE(otherCondition);
    ASSERT_EQ(expected, actual);
    ASSERT_NE(a, b);
}
```

Test names use **camelCase**.

---

## Formatting

Formatter: **clang-format** with project-local `.clang-format` (WebKit base style).

```bash
# Format a single file in-place
clang-format -style=file -i path/to/file.cpp

# Format all source files
find src tests -name "*.cpp" -o -name "*.hpp" | xargs clang-format -style=file -i
```

Key settings (from `.clang-format` and `.editorconfig`):
- `BasedOnStyle: WebKit`
- `IndentWidth: 4` (spaces, never tabs)
- `ColumnLimit: 120`
- `AllowShortFunctionsOnASingleLine: Empty`
- `AlwaysBreakTemplateDeclarations: Yes`
- Opening braces for functions/classes go on a **new line** (Allman style).

Always run clang-format before committing. Do not override formatting choices manually.

---

## Code Style

### Naming Conventions

| Construct         | Convention           | Example                       |
|-------------------|----------------------|-------------------------------|
| Classes / Structs | PascalCase           | `StringView`, `Stopwatch`     |
| Functions / Methods | camelCase          | `getSize()`, `isEmpty()`      |
| Local variables   | camelCase            | `localVar`, `itemCount`       |
| Member variables  | `m_` + camelCase     | `m_size`, `m_offset`          |
| Constants         | `k` + PascalCase     | `kLoremIpsum`, `kMaxRetries`  |
| Macros            | `ALL_CAPS` (+ prefix)| `DC_ASSERT`, `DC_INLINE`      |
| Namespaces        | lowercase            | `dc`, `dc::details`           |
| Test names        | camelCase            | `stringViewRunTime`, `makeOk` |

### Includes

- Use `#pragma once` — never `#ifndef` include guards.
- Always use angle brackets (`<>`) — no relative paths (`"../foo.hpp"`).
- C standard headers: prefer `<cstdint>`, `<cstring>` over `<stdint.h>`, `<string.h>`.
- Include order: dc/standard library headers first, then project headers.

```cpp
#pragma once

#include <dc/log.hpp>
#include <dc/result.hpp>
#include <cstdint>

#include <mymodule.hpp>
```

### Types

Use the dc type aliases from `<dc/types.hpp>`. **Never use raw `int`, `unsigned`, `long`, or
`size_t` directly.**

| Alias                     | Type                    |
|---------------------------|-------------------------|
| `s8`, `s16`, `s32`, `s64` | Signed integers         |
| `u8`, `u16`, `u32`, `u64` | Unsigned integers       |
| `usize`                   | `size_t`                |
| `f32`, `f64`              | `float`, `double`       |
| `char8`                   | `char` (not `char8_t`)  |
| `uintptr`, `intptr`       | `uintptr_t`, `intptr_t` |

**Type aliases are at global scope** — write `u32`, not `dc::u32`.

### `auto` Usage

Use `auto` **only** when the type is explicitly written on the right-hand side.

```cpp
// CORRECT — type is visible on RHS
const auto result = dc::Result<int, float>(Ok(42));
const auto* ptr = static_cast<MyClass*>(raw);

// WRONG — type is hidden
auto value = compute();   // avoid
```

### Const Correctness and Attributes

- Apply `const` everywhere applicable — parameters, local variables, member functions.
- Mark move constructors and move assignment operators `noexcept`.
- Apply `[[nodiscard]]` to all functions whose return value must not be ignored.
- Use `[[maybe_unused]]` on intentionally unused parameters instead of casting to void.

```cpp
template <typename T>
[[nodiscard]] auto makeResult(T value) noexcept -> dc::Result<T, dc::String>;
```

---

## Error Handling

**No exceptions.** The project does not use C++ exceptions.

Use dc's `Result<V, E>` and `Option<V>` types for fallible operations:

```cpp
#include <dc/result.hpp>

// Returning a result
[[nodiscard]] auto readFile(dc::StringView path) -> dc::Result<dc::String, dc::String>
{
    if (!fileExists)
        return dc::Err<dc::String>(dc::String("file not found"));
    return dc::Ok<dc::String>(contents);
}

// Consuming a result
auto result = readFile("config.txt");
if (result.isOk()) {
    auto contents = dc::move(result).unwrap();
    // use contents
}

// Exhaustive match
dc::move(result).match(
    [](dc::String&& contents) { /* ok path */ },
    [](dc::String&& err)      { LOG_ERROR("{}", err); }
);
```

### Result pitfalls

- **`dc::Result<void, E>` does not exist.** Use `dc::Result<bool, E>` and return
  `dc::Ok<bool>(true)` as a sentinel.
- **`Ok` / `Err` are not in global scope.** Always qualify them:
  `dc::Ok<T>(value)` and `dc::Err<E>(error)`.

Use `DC_ASSERT(condition, msg)` for recoverable invariant checks and
`DC_FATAL_ASSERT(condition, msg)` for unrecoverable failures.

---

## Logging

Use the dc logging macros from `<dc/log.hpp>`. Initialize at startup with `dc::log::init()`.

```cpp
LOG_INFO("loaded {} items", count);
LOG_WARNING("retrying after error: {}", err);   // NOTE: LOG_WARNING, not LOG_WARN
LOG_ERROR("fatal: {}", msg);
```

**The correct macro is `LOG_WARNING`**, not `LOG_WARN` — the latter does not exist in dc.

---

## dc Library API Reference

These are the parts of dc used in this project with their exact signatures.
The dc API differs from standard C++ in several non-obvious ways; read this
section before touching any dc code.

### `dc::List<T>`

```cpp
list.add(value);          // append — NOT pushBack()
list.getSize();           // NOT size() or getCount()
list.reserve(n);
list.resize(n);
list.clear();
list[i];                  // operator[] works
// range-for works via begin()/end()
```

**Incomplete-type gotcha:** The default template has a second parameter
`N = sizeof(T)`. If you declare `dc::List<Foo>` inside `Foo`'s own class body
(before `Foo` is complete), you must write `dc::List<Foo, 1>` to avoid a
`sizeof` on an incomplete type.

### `dc::StringView`

```cpp
sv.getSize();
sv[i];
sv.c_str();
// NO operator==  — compare like this:
dc::String(sv) == "literal"    // correct
```

### `dc::String`

```cpp
str.getSize();
str.c_str();
str += "more";
str.append("more");
str == "literal";          // operator==(const char8*) exists
str == otherString;        // operator==(const String&) exists
str.toView()[i];           // no operator[] on String; go via StringView
str.getDataAt(i);          // alternative character access
```

### `dc::Map<K,V>`

```cpp
// Insert: returns Value* pointing to the slot (NOT insert(key, value))
Value* slot = map.insert(key);
*slot = value;

// Lookup: returns Entry* or nullptr
Entry* e = map.tryGet(key);
if (e) { /* e->value */ }

map.getSize();
```

### `dc::File`

`open()` is an **instance method**, not a static factory:

```cpp
dc::File file;
file.open(path, dc::FileMode::Read);   // path is const dc::String&
auto content = file.read();            // returns Result<String, ...>
// write() is [[nodiscard]] — don't ignore its return value
```

### `dc::Stopwatch`

```cpp
dc::Stopwatch sw;
sw.start();
// ...
float s = sw.fs();      // elapsed seconds (start → stop), NOT fSeconds()
float s = sw.nowFs();   // elapsed seconds (start → now)
```

---

## CMake / FetchContent — tree-sitter

tree-sitter and tree-sitter-cpp both ship their own `CMakeLists.txt` files
that are incompatible with being used as subdirectories. The workaround is to
tell FetchContent to skip their CMake entirely and compile the grammar sources
directly:

```cmake
FetchContent_Declare(
    tree-sitter
    GIT_REPOSITORY https://github.com/tree-sitter/tree-sitter.git
    GIT_TAG        v0.26.6
    SOURCE_SUBDIR  _no_cmake_subdir_      # ← suppresses their CMakeLists
)

FetchContent_Declare(
    tree-sitter-cpp
    GIT_REPOSITORY https://github.com/tree-sitter/tree-sitter-cpp.git
    GIT_TAG        v0.23.4
    SOURCE_SUBDIR  _no_cmake_subdir_
)
```

Then add the sources manually as a static library and **compile them as C11**
(not C++):

```cmake
add_library(tree-sitter-cpp-grammar STATIC
    ${tree-sitter-cpp_SOURCE_DIR}/src/parser.c
    ${tree-sitter-cpp_SOURCE_DIR}/src/scanner.c
)
set_source_files_properties(
    ${tree-sitter-cpp_SOURCE_DIR}/src/parser.c
    ${tree-sitter-cpp_SOURCE_DIR}/src/scanner.c
    PROPERTIES LANGUAGE C
               COMPILE_OPTIONS "-std=c11"   # or /std:c11 on MSVC
)
```

Grammar files are pre-generated; there is no `grammar.js` build step needed.

---

## tree-sitter Query for C++ Symbols

The following query extracts all relevant C++ symbols.  Paste it verbatim —
minor deviations (e.g. missing the `field_declaration` arm) will silently
miss whole classes of symbols.

```scheme
(function_definition
  declarator: (function_declarator
    declarator: (_) @func))
(declaration
  declarator: (function_declarator
    declarator: (_) @func))
(field_declaration
  declarator: (function_declarator
    declarator: (_) @func))
(class_specifier
  name: (type_identifier) @class)
(struct_specifier
  name: (type_identifier) @struct)
(enum_specifier
  name: (type_identifier) @enum)
(alias_declaration
  name: (type_identifier) @alias)
(type_definition
  declarator: (type_identifier) @typedef)
```

Forward declarations (`class Foo;`, `struct Bar;`) are filtered **after** the
query by checking whether the parent node has a `"body"` child field.

---

## Emacs Client (`symbols-server.el`)

The Emacs client lives at `~/.emacs.d/symbols-server.el`.  Key design points
that future editors should be aware of:

### Process model
- One `symbols.exe` subprocess per project root, keyed by the projectile root
  string in the `symbols-server--servers` hash table.
- `symbols-server--get-or-start` is idempotent: it reuses a live process.
- The process filter accumulates a partial-line buffer because
  `process-filter` is not guaranteed to deliver complete lines.
- Pending requests are tracked as an `(id . callback)` alist; the filter
  dispatches on the `id` field of each incoming JSON object.
- The `id:0` startup notification sets the `:ready` flag; all other ids
  dispatch to registered callbacks.

### consult integration
- Uses `consult--read` with `:dynamic t` and a synchronous per-keystroke
  callback (waits up to 3 s with `accept-process-output`).
- Symbol metadata (file path, line number) is stored as the text property
  `symbols-server--sym` on each candidate string so it survives through
  consult's filtering without a separate lookup table.
- Preview and navigation logic matches `treesit-utils.el`: debounced file
  loading, `xref-push-marker-stack` before jumping, close preview-only
  buffers on exit.

### TnT project special case
- `symbols-server--search-dir` detects a project root whose last component
  is `"TnT"` and passes
  `Code/DICE/Extensions/BattlefieldOnline` as `--search-dir`, mirroring
  the search-directory logic in the legacy `treesit-utils.el`.

### init.el wiring
`M-s M-s` is bound to `symbols-server-find-symbols`.
`treesit-utils` is still loaded (functions available) but its `M-s M-s`
binding has been removed; it serves as a fallback if the binary is absent.

---

## General Guidelines

- Treat every compiler warning as an error — zero warnings policy.
- Prefer `dc::move()` over `std::move()` within this project.
- Do not add dependencies without discussion; new deps go through `FetchContent` in `CMakeLists.txt`.
- Keep functions small and focused; prefer free functions over methods where state is not needed.
- Templates go on their own line above the declaration (enforced by clang-format).
- Source files: `.cpp` extension; header files: `.hpp` extension.
