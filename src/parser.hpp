#pragma once

#include <dc/list.hpp>
#include <dc/result.hpp>
#include <dc/string.hpp>
#include <dc/types.hpp>

#include <filesystem>

namespace symbols {

/// The kind of symbol extracted from source code.
enum class SymbolKind : u8 {
    Function,
    Class,
    Struct,
    Enum,
    Alias,
    Typedef,
};

/// Convert a SymbolKind to its string representation.
[[nodiscard]] auto symbolKindToString(SymbolKind kind) -> dc::StringView;

/// Parse a string back to SymbolKind, returns Function on failure.
[[nodiscard]] auto stringToSymbolKind(dc::StringView str) -> SymbolKind;

/// A symbol extracted from a source file.
struct Symbol {
    dc::String name;
    SymbolKind kind;
    dc::String file; // Relative path from project root.
    u32 line;
};

/// Parser that uses tree-sitter to extract symbols from C/C++ files.
/// Not thread-safe -- use one instance per thread.
class Parser {
public:
    Parser();
    ~Parser();

    Parser(const Parser&) = delete;
    auto operator=(const Parser&) -> Parser& = delete;
    Parser(Parser&&) noexcept;
    auto operator=(Parser&&) noexcept -> Parser&;

    /// Parse a single file and extract all symbols.
    /// @param filePath     Absolute path to the file.
    /// @param relativePath Pre-computed relative path from the project root (used as Symbol::file).
    [[nodiscard]] auto parseFile(const std::filesystem::path& filePath, dc::StringView relativePath)
        -> dc::Result<dc::List<Symbol>, dc::String>;

private:
    struct Impl;
    Impl* m_impl;
};

} // namespace symbols
