#pragma once

#include <dc/job_system.hpp>
#include <dc/list.hpp>
#include <dc/string.hpp>
#include <dc/types.hpp>

#include <indexer.hpp>
#include <protocol.hpp>

#include <filesystem>
#include <istream>
#include <ostream>

namespace symbols {

/// Configuration for the symbol server.
struct ServerConfig {
    std::filesystem::path projectRoot;
    dc::List<dc::String> searchDirs;
    bool useCache = true;
    dc::JobSystem* jobSystem = nullptr; ///< Required: must be non-null when used.
};

/// Initialize the indexer: load from cache or perform a fresh build.
/// Exposed for testing.
auto initializeIndex(Indexer& indexer, const ServerConfig& config) -> void;

/// Handle a single parsed request and return a JSON response string.
/// Exposed for testing.
[[nodiscard]] auto handleRequest(const Request& req, Indexer& indexer, const ServerConfig& config) -> dc::String;

/// Send a JSON response to the given output stream (one line + flush).
/// Exposed for testing.
auto sendResponse(std::ostream& out, const dc::String& json) -> void;

/// Inner server loop: read newline-delimited JSON requests from `in`,
/// write JSON responses to `out`, until shutdown or EOF.
/// The indexer must already be initialised before calling this.
/// Exposed for testing.
[[nodiscard]] auto runServerLoop(std::istream& in, std::ostream& out, Indexer& indexer, const ServerConfig& config)
    -> s32;

/// Run the symbol server.
/// Reads JSON requests from stdin, writes JSON responses to stdout.
/// Blocks until a shutdown request is received or stdin is closed.
[[nodiscard]] auto runServer(const ServerConfig& config) -> s32;

} // namespace symbols
