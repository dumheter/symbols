#pragma once

#include <dc/list.hpp>
#include <dc/string.hpp>
#include <dc/types.hpp>

#include <filesystem>

namespace symbols {

/// A single rule parsed from a .symbols-ignore file.
///
/// Two forms are supported:
///   - Exact path:  "build/foo/parser.c"   — matches that specific relative path only.
///   - Dir glob:    "build/*"              — matches any path whose first component(s)
///                                          equal the prefix before the trailing "/*".
///
/// Paths are always compared with forward slashes and are relative to the project root.
struct IgnoreRule {
    dc::String pattern; ///< The raw pattern string (forward-slash normalised, no trailing newline).
    bool isGlob; ///< true  → pattern ends with "/*" (directory wildcard)
                 ///< false → exact relative path match
};

/// The parsed contents of a .symbols-ignore file.
class IgnoreList {
public:
    IgnoreList() = default;

    /// Parse ignore rules from the given text (the full content of a .symbols-ignore file).
    /// Lines starting with '#' and blank lines are ignored.
    [[nodiscard]] static auto parse(dc::StringView text) -> IgnoreList;

    /// Load and parse a .symbols-ignore file from disk.
    /// Returns an empty IgnoreList (no rules) if the file does not exist or cannot be read.
    [[nodiscard]] static auto loadFromDirectory(const std::filesystem::path& projectRoot) -> IgnoreList;

    /// Return true if the given relative path (forward-slash separated) should be excluded.
    [[nodiscard]] auto isIgnored(dc::StringView relativePath) const -> bool;

    [[nodiscard]] auto ruleCount() const -> usize
    {
        return m_rules.getSize();
    }

private:
    dc::List<IgnoreRule> m_rules;
};

} // namespace symbols
