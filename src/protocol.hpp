#pragma once

#include <dc/result.hpp>
#include <dc/string.hpp>
#include <dc/types.hpp>

#include <json.hpp>

namespace symbols {

/// Request methods supported by the protocol.
enum class Method : u8 {
    Query,
    Status,
    Rebuild,
    RebuildFile,
    Shutdown,
    Unknown,
};

/// A parsed request from the client.
struct Request {
    s64 id;
    Method method;
    dc::String pattern; // For Query method.
    s64 limit; // For Query method (default 200).
    dc::String file; // For RebuildFile method: absolute path to the file.
};

/// Parse a JSON line into a Request.
[[nodiscard]] auto parseRequest(dc::StringView jsonLine) -> dc::Result<Request, dc::String>;

/// Build a JSON response for a query result.
[[nodiscard]] auto buildQueryResponse(s64 id, const JsonValue& symbolsArray) -> dc::String;

/// Build a JSON response for status.
[[nodiscard]] auto buildStatusResponse(s64 id, dc::StringView status, s64 fileCount, s64 symbolCount) -> dc::String;

/// Build a simple acknowledgment response.
[[nodiscard]] auto buildAckResponse(s64 id, dc::StringView status) -> dc::String;

/// Build an error response.
[[nodiscard]] auto buildErrorResponse(s64 id, dc::StringView error) -> dc::String;

} // namespace symbols
