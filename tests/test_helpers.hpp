#pragma once

#include <dc/file.hpp>
#include <dc/string.hpp>

#include <filesystem>

/// Write `content` to `path`, creating or overwriting the file.
[[maybe_unused]] static auto writeTempFile(const std::filesystem::path& path, const char* content) -> void
{
    dc::File file;
    auto openResult = file.open(dc::String(path.string().c_str()), dc::File::Mode::kWrite);
    if (openResult.isOk()) {
        [[maybe_unused]] auto wr = file.write(dc::String(content));
    }
}

/// Return the repository root directory, derived from the compile-time path of
/// this header (which lives at <root>/tests/test_helpers.hpp).
[[maybe_unused]] static auto repoRoot() -> std::filesystem::path
{
    // __FILE__ is the absolute (or build-relative) path to this header.
    // Its parent is tests/, whose parent is the repo root.
    const std::filesystem::path self(__FILE__);
    return self.parent_path().parent_path();
}
