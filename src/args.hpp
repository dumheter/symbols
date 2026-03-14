#pragma once

#include <dc/result.hpp>
#include <dc/string.hpp>

#include <server.hpp>

namespace symbols {

/// Result of parsing command-line arguments.
struct ParsedArgs {
    ServerConfig config;
    bool helpRequested = false;
};

/// Parse command-line arguments into a ParsedArgs.
/// Returns Err if an unknown option is encountered, a required value is missing,
/// or --root is not supplied (and help was not requested).
[[nodiscard]] auto parseArgs(int argc, const char* const* argv) -> dc::Result<ParsedArgs, dc::String>;

} // namespace symbols
