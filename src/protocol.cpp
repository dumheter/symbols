#include <protocol.hpp>

namespace symbols {

auto parseRequest(dc::StringView jsonLine) -> dc::Result<Request, dc::String>
{
    auto parseResult = JsonValue::parse(jsonLine);
    if (!parseResult.isOk()) {
        dc::String err("Failed to parse request JSON: ");
        err += dc::move(parseResult).unwrapErr();
        return dc::Err<dc::String>(dc::move(err));
    }

    const JsonValue root = dc::move(parseResult).unwrap();

    Request req;
    req.id = root.getNumber("id");
    req.limit = 200;

    const dc::StringView methodStr = root.getString("method");
    if (dc::String(methodStr) == "query") {
        req.method = Method::Query;

        const JsonValue* params = root.get("params");
        if (params) {
            req.pattern = dc::String(params->getString("pattern"));
            const s64 limit = params->getNumber("limit");
            if (limit > 0)
                req.limit = limit;
        }
    } else if (dc::String(methodStr) == "status") {
        req.method = Method::Status;
    } else if (dc::String(methodStr) == "rebuild") {
        req.method = Method::Rebuild;
    } else if (dc::String(methodStr) == "forceRebuild") {
        req.method = Method::ForceRebuild;
    } else if (dc::String(methodStr) == "rebuildFile") {
        req.method = Method::RebuildFile;

        const JsonValue* params = root.get("params");
        if (params)
            req.file = dc::String(params->getString("file"));
    } else if (dc::String(methodStr) == "shutdown") {
        req.method = Method::Shutdown;
    } else {
        req.method = Method::Unknown;
    }

    return dc::Ok<Request>(dc::move(req));
}

auto buildQueryResponse(s64 id, const JsonValue& symbolsArray) -> dc::String
{
    auto root = JsonValue::makeObject();
    root.set(dc::String("id"), JsonValue::makeNumber(id));
    root.set(dc::String("symbols"), JsonValue(symbolsArray));
    return root.serialize();
}

auto buildStatusResponse(s64 id, dc::StringView status, s64 fileCount, s64 symbolCount) -> dc::String
{
    auto root = JsonValue::makeObject();
    root.set(dc::String("id"), JsonValue::makeNumber(id));
    root.set(dc::String("status"), JsonValue::makeString(dc::String(status)));
    root.set(dc::String("files"), JsonValue::makeNumber(fileCount));
    root.set(dc::String("symbols"), JsonValue::makeNumber(symbolCount));
    return root.serialize();
}

auto buildAckResponse(s64 id, dc::StringView status) -> dc::String
{
    auto root = JsonValue::makeObject();
    root.set(dc::String("id"), JsonValue::makeNumber(id));
    root.set(dc::String("status"), JsonValue::makeString(dc::String(status)));
    return root.serialize();
}

auto buildErrorResponse(s64 id, dc::StringView error) -> dc::String
{
    auto root = JsonValue::makeObject();
    root.set(dc::String("id"), JsonValue::makeNumber(id));
    root.set(dc::String("error"), JsonValue::makeString(dc::String(error)));
    return root.serialize();
}

} // namespace symbols
