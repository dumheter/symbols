#pragma once

#include <dc/result.hpp>
#include <dc/string.hpp>

#include <filesystem>

namespace symbols {

/// Return the default directory for persistent symbols-server logs.
[[nodiscard]] auto defaultLogDirectory() -> std::filesystem::path;

/// Return a unique log file path inside `logDirectory`.
[[nodiscard]] auto makeLogFilePath(const std::filesystem::path& logDirectory) -> std::filesystem::path;

/// Attach a persistent file sink to the global logger.
[[nodiscard]] auto attachFileLogger(const std::filesystem::path& logDirectory)
    -> dc::Result<std::filesystem::path, dc::String>;

} // namespace symbols
