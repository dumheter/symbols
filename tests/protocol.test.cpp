#include <dc/dtest.hpp>
#include <protocol.hpp>

using namespace symbols;

// ---------------------------------------------------------------------------
// parseRequest
// ---------------------------------------------------------------------------

DTEST(parseRequestQueryMethod)
{
    auto result = parseRequest(dc::StringView(R"({"id":1,"method":"query","params":{"pattern":"Foo","limit":50}})"));
    ASSERT_TRUE(result.isOk());
    auto req = dc::move(result).unwrap();
    ASSERT_EQ(req.id, static_cast<s64>(1));
    ASSERT_EQ(req.method, Method::Query);
    ASSERT_TRUE(req.pattern == "Foo");
    ASSERT_EQ(req.limit, static_cast<s64>(50));
}

DTEST(parseRequestQueryDefaultLimit)
{
    // When params has no "limit" field (or limit <= 0), default of 200 applies.
    auto result = parseRequest(dc::StringView(R"({"id":2,"method":"query","params":{"pattern":"Bar"}})"));
    ASSERT_TRUE(result.isOk());
    auto req = dc::move(result).unwrap();
    ASSERT_EQ(req.method, Method::Query);
    ASSERT_EQ(req.limit, static_cast<s64>(200));
}

DTEST(parseRequestQueryNoParams)
{
    // Query without params block — pattern is empty, limit defaults to 200.
    auto result = parseRequest(dc::StringView(R"({"id":3,"method":"query"})"));
    ASSERT_TRUE(result.isOk());
    auto req = dc::move(result).unwrap();
    ASSERT_EQ(req.method, Method::Query);
    ASSERT_EQ(req.limit, static_cast<s64>(200));
}

DTEST(parseRequestStatusMethod)
{
    auto result = parseRequest(dc::StringView(R"({"id":10,"method":"status"})"));
    ASSERT_TRUE(result.isOk());
    auto req = dc::move(result).unwrap();
    ASSERT_EQ(req.id, static_cast<s64>(10));
    ASSERT_EQ(req.method, Method::Status);
}

DTEST(parseRequestRebuildMethod)
{
    auto result = parseRequest(dc::StringView(R"({"id":11,"method":"rebuild"})"));
    ASSERT_TRUE(result.isOk());
    auto req = dc::move(result).unwrap();
    ASSERT_EQ(req.id, static_cast<s64>(11));
    ASSERT_EQ(req.method, Method::Rebuild);
}

DTEST(parseRequestShutdownMethod)
{
    auto result = parseRequest(dc::StringView(R"({"id":99,"method":"shutdown"})"));
    ASSERT_TRUE(result.isOk());
    auto req = dc::move(result).unwrap();
    ASSERT_EQ(req.id, static_cast<s64>(99));
    ASSERT_EQ(req.method, Method::Shutdown);
}

DTEST(parseRequestUnknownMethod)
{
    auto result = parseRequest(dc::StringView(R"({"id":5,"method":"frobulate"})"));
    ASSERT_TRUE(result.isOk());
    auto req = dc::move(result).unwrap();
    ASSERT_EQ(req.method, Method::Unknown);
}

DTEST(parseRequestInvalidJson)
{
    auto result = parseRequest(dc::StringView("not json at all {{{"));
    ASSERT_FALSE(result.isOk());
}

DTEST(parseRequestEmptyJson)
{
    auto result = parseRequest(dc::StringView(""));
    ASSERT_FALSE(result.isOk());
}

DTEST(parseRequestZeroLimitUsesDefault)
{
    // limit:0 is <= 0, so the default 200 should be used.
    auto result = parseRequest(dc::StringView(R"({"id":7,"method":"query","params":{"pattern":"X","limit":0}})"));
    ASSERT_TRUE(result.isOk());
    auto req = dc::move(result).unwrap();
    ASSERT_EQ(req.limit, static_cast<s64>(200));
}

// ---------------------------------------------------------------------------
// buildQueryResponse
// ---------------------------------------------------------------------------

DTEST(buildQueryResponseContainsIdAndSymbols)
{
    auto arr = JsonValue::makeArray();
    arr.pushBack(JsonValue::makeString(dc::String("MyClass")));

    const dc::String response = buildQueryResponse(42, arr);
    auto parseResult = JsonValue::parse(dc::StringView(response));
    ASSERT_TRUE(parseResult.isOk());
    auto val = dc::move(parseResult).unwrap();

    ASSERT_EQ(val.getNumber("id"), static_cast<s64>(42));
    const JsonValue* syms = val.get("symbols");
    ASSERT_TRUE(syms != nullptr);
    ASSERT_EQ(syms->arraySize(), static_cast<usize>(1));
}

DTEST(buildQueryResponseEmptyArray)
{
    const auto arr = JsonValue::makeArray();
    const dc::String response = buildQueryResponse(0, arr);
    auto parseResult = JsonValue::parse(dc::StringView(response));
    ASSERT_TRUE(parseResult.isOk());
    auto val = dc::move(parseResult).unwrap();
    const JsonValue* syms = val.get("symbols");
    ASSERT_TRUE(syms != nullptr);
    ASSERT_EQ(syms->arraySize(), static_cast<usize>(0));
}

// ---------------------------------------------------------------------------
// buildStatusResponse
// ---------------------------------------------------------------------------

DTEST(buildStatusResponseFields)
{
    const dc::String response = buildStatusResponse(5, dc::StringView("ready"), 12, 345);
    auto parseResult = JsonValue::parse(dc::StringView(response));
    ASSERT_TRUE(parseResult.isOk());
    auto val = dc::move(parseResult).unwrap();

    ASSERT_EQ(val.getNumber("id"), static_cast<s64>(5));
    ASSERT_TRUE(dc::String(val.getString("status")) == "ready");
    ASSERT_EQ(val.getNumber("files"), static_cast<s64>(12));
    ASSERT_EQ(val.getNumber("symbols"), static_cast<s64>(345));
}

// ---------------------------------------------------------------------------
// buildAckResponse
// ---------------------------------------------------------------------------

DTEST(buildAckResponseFields)
{
    const dc::String response = buildAckResponse(7, dc::StringView("ok"));
    auto parseResult = JsonValue::parse(dc::StringView(response));
    ASSERT_TRUE(parseResult.isOk());
    auto val = dc::move(parseResult).unwrap();

    ASSERT_EQ(val.getNumber("id"), static_cast<s64>(7));
    ASSERT_TRUE(dc::String(val.getString("status")) == "ok");
    // Ack response must NOT have an "error" field.
    ASSERT_TRUE(val.get("error") == nullptr);
}

// ---------------------------------------------------------------------------
// buildErrorResponse
// ---------------------------------------------------------------------------

DTEST(buildErrorResponseFields)
{
    const dc::String response = buildErrorResponse(3, dc::StringView("unknown method"));
    auto parseResult = JsonValue::parse(dc::StringView(response));
    ASSERT_TRUE(parseResult.isOk());
    auto val = dc::move(parseResult).unwrap();

    ASSERT_EQ(val.getNumber("id"), static_cast<s64>(3));
    ASSERT_TRUE(dc::String(val.getString("error")) == "unknown method");
    // Error response must NOT have a "status" field.
    ASSERT_TRUE(val.get("status") == nullptr);
}

// ---------------------------------------------------------------------------
// Edge cases (tasks 8a–8b)
// ---------------------------------------------------------------------------

DTEST(parseRequestNegativeLimitFallsBackToDefault)
{
    // limit:-5 is <= 0, so the default of 200 must be used.
    auto result = parseRequest(dc::StringView(R"({"id":1,"method":"query","params":{"pattern":"X","limit":-5}})"));
    ASSERT_TRUE(result.isOk());
    auto req = dc::move(result).unwrap();
    ASSERT_EQ(req.limit, static_cast<s64>(200));
}

DTEST(parseRequestNegativeIdIsPreserved)
{
    // Negative id values are valid and must be preserved as-is.
    auto result = parseRequest(dc::StringView(R"({"id":-42,"method":"status"})"));
    ASSERT_TRUE(result.isOk());
    auto req = dc::move(result).unwrap();
    ASSERT_EQ(req.id, static_cast<s64>(-42));
    ASSERT_EQ(req.method, Method::Status);
}
