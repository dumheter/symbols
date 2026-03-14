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
