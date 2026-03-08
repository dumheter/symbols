#pragma once

#include <dc/list.hpp>
#include <dc/string.hpp>
#include <dc/types.hpp>

#include <filesystem>

namespace symbols {

/// Configuration for the symbol server.
struct ServerConfig {
    std::filesystem::path projectRoot;
    dc::List<dc::String> searchDirs;
    bool useCache = true;
};

/// Run the symbol server.
/// Reads JSON requests from stdin, writes JSON responses to stdout.
/// Blocks until a shutdown request is received or stdin is closed.
[[nodiscard]] auto runServer(const ServerConfig& config) -> s32;

} // namespace symbols
