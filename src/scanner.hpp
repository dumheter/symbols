#pragma once

#include <dc/list.hpp>
#include <dc/string.hpp>
#include <dc/types.hpp>

#include <ignore.hpp>

#include <filesystem>

namespace symbols {

/// Recursively scan a directory for C/C++ source files.
/// Returns a list of absolute file paths.
/// Files whose path (relative to root) is matched by ignoreList are excluded.
[[nodiscard]] auto scanDirectory(const std::filesystem::path& root, const dc::List<dc::String>& extensions,
    const IgnoreList& ignoreList = IgnoreList {}) -> dc::List<std::filesystem::path>;

/// Default C/C++ file extensions to scan for.
[[nodiscard]] auto defaultCppExtensions() -> dc::List<dc::String>;

} // namespace symbols
