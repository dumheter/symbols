#include <dc/dtest.hpp>
#include <dc/file.hpp>
#include <indexer.hpp>
#include <json.hpp>
#include <protocol.hpp>
#include <server.hpp>

#include <filesystem>
#include <sstream>

using namespace symbols;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static auto writeTempFile(const std::filesystem::path& path, const char* content) -> void
{
    dc::File file;
    auto openResult = file.open(dc::String(path.string().c_str()), dc::File::Mode::kWrite);
    if (openResult.isOk()) {
        [[maybe_unused]] auto wr = file.write(dc::String(content));
    }
}

static auto makeRequest(Method method, s64 id = 1) -> Request
{
    Request req;
    req.id = id;
    req.method = method;
    req.limit = 200;
    return req;
}

static auto makeQueryRequest(dc::String pattern, s64 id = 1, s64 limit = 10) -> Request
{
    Request req;
    req.id = id;
    req.method = Method::Query;
    req.pattern = dc::move(pattern);
    req.limit = limit;
    return req;
}

// ---------------------------------------------------------------------------
// handleRequest — Method::Query
// ---------------------------------------------------------------------------

DTEST(handleRequestQueryReturnsSymbolsArray)
{
    const auto tempDir = std::filesystem::temp_directory_path() / "symbols_server_query";
    std::filesystem::create_directories(tempDir);
    writeTempFile(tempDir / "a.cpp", "void myFunc() {}");

    Indexer indexer;
    indexer.build(tempDir);
    ASSERT_TRUE(indexer.isReady());

    ServerConfig config;
    config.projectRoot = tempDir;
    config.useCache = false;

    const Request req = makeQueryRequest(dc::String("myFunc"));
    const dc::String response = handleRequest(req, indexer, config);

    auto parseResult = JsonValue::parse(dc::StringView(response));
    ASSERT_TRUE(parseResult.isOk());
    const auto val = dc::move(parseResult).unwrap();

    ASSERT_EQ(val.getNumber("id"), static_cast<s64>(1));
    const JsonValue* syms = val.get("symbols");
    ASSERT_TRUE(syms != nullptr);
    ASSERT_TRUE(syms->arraySize() >= static_cast<usize>(1));

    // First symbol must be myFunc.
    const JsonValue& first = syms->at(0);
    ASSERT_TRUE(dc::String(first.getString("name")) == "myFunc");

    std::filesystem::remove_all(tempDir);
}

DTEST(handleRequestQueryEmptyResultsWhenNoMatch)
{
    const auto tempDir = std::filesystem::temp_directory_path() / "symbols_server_query_nomatch";
    std::filesystem::create_directories(tempDir);
    writeTempFile(tempDir / "a.cpp", "void alpha() {}");

    Indexer indexer;
    indexer.build(tempDir);

    ServerConfig config;
    config.projectRoot = tempDir;
    config.useCache = false;

    const Request req = makeQueryRequest(dc::String("zzznomatch"));
    const dc::String response = handleRequest(req, indexer, config);

    auto parseResult = JsonValue::parse(dc::StringView(response));
    ASSERT_TRUE(parseResult.isOk());
    const auto val = dc::move(parseResult).unwrap();
    const JsonValue* syms = val.get("symbols");
    ASSERT_TRUE(syms != nullptr);
    ASSERT_EQ(syms->arraySize(), static_cast<usize>(0));

    std::filesystem::remove_all(tempDir);
}

DTEST(handleRequestQuerySymbolHasExpectedFields)
{
    const auto tempDir = std::filesystem::temp_directory_path() / "symbols_server_query_fields";
    std::filesystem::create_directories(tempDir);
    writeTempFile(tempDir / "a.cpp", "class FooBar {};");

    Indexer indexer;
    indexer.build(tempDir);

    ServerConfig config;
    config.projectRoot = tempDir;
    config.useCache = false;

    const Request req = makeQueryRequest(dc::String("FooBar"));
    const dc::String response = handleRequest(req, indexer, config);

    auto parseResult = JsonValue::parse(dc::StringView(response));
    ASSERT_TRUE(parseResult.isOk());
    const auto val = dc::move(parseResult).unwrap();
    const JsonValue* syms = val.get("symbols");
    ASSERT_TRUE(syms != nullptr);
    ASSERT_TRUE(syms->arraySize() >= static_cast<usize>(1));

    const JsonValue& sym = syms->at(0);
    // All four fields must be present and non-empty/valid.
    ASSERT_TRUE(dc::String(sym.getString("name")) == "FooBar");
    ASSERT_TRUE(dc::String(sym.getString("kind")) == "class");
    ASSERT_TRUE(sym.getString("file").getSize() > static_cast<usize>(0));
    ASSERT_TRUE(sym.getNumber("line") >= static_cast<s64>(1));
    // score field must exist and be positive.
    const JsonValue* scoreVal = sym.get("score");
    ASSERT_TRUE(scoreVal != nullptr);
    ASSERT_TRUE(scoreVal->asNumber() > static_cast<s64>(0));

    std::filesystem::remove_all(tempDir);
}

// ---------------------------------------------------------------------------
// handleRequest — Method::Status
// ---------------------------------------------------------------------------

DTEST(handleRequestStatusWhenReady)
{
    const auto tempDir = std::filesystem::temp_directory_path() / "symbols_server_status_ready";
    std::filesystem::create_directories(tempDir);
    writeTempFile(tempDir / "a.cpp", "void foo() {}");
    writeTempFile(tempDir / "b.cpp", "void bar() {}");

    Indexer indexer;
    indexer.build(tempDir);

    ServerConfig config;
    config.projectRoot = tempDir;
    config.useCache = false;

    const Request req = makeRequest(Method::Status, 5);
    const dc::String response = handleRequest(req, indexer, config);

    auto parseResult = JsonValue::parse(dc::StringView(response));
    ASSERT_TRUE(parseResult.isOk());
    const auto val = dc::move(parseResult).unwrap();

    ASSERT_EQ(val.getNumber("id"), static_cast<s64>(5));
    ASSERT_TRUE(dc::String(val.getString("status")) == "ready");
    ASSERT_EQ(val.getNumber("files"), static_cast<s64>(2));
    ASSERT_TRUE(val.getNumber("symbols") >= static_cast<s64>(2));

    std::filesystem::remove_all(tempDir);
}

DTEST(handleRequestStatusWhenNotReady)
{
    Indexer indexer;
    ASSERT_FALSE(indexer.isReady());

    ServerConfig config;
    config.projectRoot = std::filesystem::temp_directory_path();
    config.useCache = false;

    const Request req = makeRequest(Method::Status, 2);
    const dc::String response = handleRequest(req, indexer, config);

    auto parseResult = JsonValue::parse(dc::StringView(response));
    ASSERT_TRUE(parseResult.isOk());
    const auto val = dc::move(parseResult).unwrap();

    ASSERT_EQ(val.getNumber("id"), static_cast<s64>(2));
    ASSERT_TRUE(dc::String(val.getString("status")) == "building");
}

// ---------------------------------------------------------------------------
// handleRequest — Method::Rebuild
// ---------------------------------------------------------------------------

DTEST(handleRequestRebuildReturnsAck)
{
    const auto tempDir = std::filesystem::temp_directory_path() / "symbols_server_rebuild";
    std::filesystem::create_directories(tempDir);
    writeTempFile(tempDir / "a.cpp", "void alpha() {}");

    Indexer indexer;
    indexer.build(tempDir);

    ServerConfig config;
    config.projectRoot = tempDir;
    config.useCache = false;

    const Request req = makeRequest(Method::Rebuild, 10);
    const dc::String response = handleRequest(req, indexer, config);

    auto parseResult = JsonValue::parse(dc::StringView(response));
    ASSERT_TRUE(parseResult.isOk());
    const auto val = dc::move(parseResult).unwrap();

    ASSERT_EQ(val.getNumber("id"), static_cast<s64>(10));
    ASSERT_TRUE(dc::String(val.getString("status")) == "rebuilt");
    ASSERT_TRUE(val.get("error") == nullptr);

    std::filesystem::remove_all(tempDir);
}

DTEST(handleRequestRebuildPicksUpNewFile)
{
    const auto tempDir = std::filesystem::temp_directory_path() / "symbols_server_rebuild_new";
    std::filesystem::create_directories(tempDir);
    writeTempFile(tempDir / "a.cpp", "void existing() {}");

    Indexer indexer;
    indexer.build(tempDir);
    const usize countBefore = indexer.symbolCount();

    // Add a new file before rebuild.
    writeTempFile(tempDir / "b.cpp", "void brandNew() {}");

    ServerConfig config;
    config.projectRoot = tempDir;
    config.useCache = false;

    const Request req = makeRequest(Method::Rebuild, 11);
    [[maybe_unused]] const dc::String response = handleRequest(req, indexer, config);

    // Index should now include the new symbol.
    ASSERT_TRUE(indexer.symbolCount() > countBefore);
    const auto results = indexer.search(dc::StringView("brandNew"), 10);
    ASSERT_TRUE(results.getSize() >= static_cast<u64>(1));

    std::filesystem::remove_all(tempDir);
}

// ---------------------------------------------------------------------------
// handleRequest — Method::Shutdown
// ---------------------------------------------------------------------------

DTEST(handleRequestShutdownReturnsAck)
{
    Indexer indexer;
    ServerConfig config;
    config.projectRoot = std::filesystem::temp_directory_path();
    config.useCache = false;

    const Request req = makeRequest(Method::Shutdown, 99);
    const dc::String response = handleRequest(req, indexer, config);

    auto parseResult = JsonValue::parse(dc::StringView(response));
    ASSERT_TRUE(parseResult.isOk());
    const auto val = dc::move(parseResult).unwrap();

    ASSERT_EQ(val.getNumber("id"), static_cast<s64>(99));
    ASSERT_TRUE(dc::String(val.getString("status")) == "shutdown");
    ASSERT_TRUE(val.get("error") == nullptr);
}

// ---------------------------------------------------------------------------
// handleRequest — Method::Unknown
// ---------------------------------------------------------------------------

DTEST(handleRequestUnknownReturnsError)
{
    Indexer indexer;
    ServerConfig config;
    config.projectRoot = std::filesystem::temp_directory_path();
    config.useCache = false;

    const Request req = makeRequest(Method::Unknown, 7);
    const dc::String response = handleRequest(req, indexer, config);

    auto parseResult = JsonValue::parse(dc::StringView(response));
    ASSERT_TRUE(parseResult.isOk());
    const auto val = dc::move(parseResult).unwrap();

    ASSERT_EQ(val.getNumber("id"), static_cast<s64>(7));
    ASSERT_TRUE(val.getString("error").getSize() > static_cast<usize>(0));
    ASSERT_TRUE(val.get("status") == nullptr);
}

// ---------------------------------------------------------------------------
// sendResponse
// ---------------------------------------------------------------------------

DTEST(sendResponseWritesLineWithNewline)
{
    std::ostringstream oss;
    const dc::String msg("hello world");
    sendResponse(oss, msg);
    ASSERT_TRUE(oss.str() == "hello world\n");
}

DTEST(sendResponseFlushes)
{
    // After sendResponse the stream should be flushed (content visible).
    std::ostringstream oss;
    sendResponse(oss, dc::String("{\"id\":1}"));
    ASSERT_TRUE(oss.str().find("{\"id\":1}") != std::string::npos);
}

// ---------------------------------------------------------------------------
// initializeIndex — no-cache path
// ---------------------------------------------------------------------------

DTEST(initializeIndexFreshBuildWhenNoCacheRequested)
{
    const auto tempDir = std::filesystem::temp_directory_path() / "symbols_server_init_nocache";
    std::filesystem::create_directories(tempDir);
    writeTempFile(tempDir / "a.cpp", "void init1() {}");

    ServerConfig config;
    config.projectRoot = tempDir;
    config.useCache = false;

    Indexer indexer;
    initializeIndex(indexer, config);

    ASSERT_TRUE(indexer.isReady());
    ASSERT_TRUE(indexer.symbolCount() >= static_cast<usize>(1));
    // Cache must NOT have been created.
    ASSERT_FALSE(indexer.hasCacheFile(tempDir));

    std::filesystem::remove_all(tempDir);
}

// ---------------------------------------------------------------------------
// initializeIndex — cache load path
// ---------------------------------------------------------------------------

DTEST(initializeIndexLoadsCacheWhenPresent)
{
    const auto tempDir = std::filesystem::temp_directory_path() / "symbols_server_init_cache";
    std::filesystem::create_directories(tempDir);
    writeTempFile(tempDir / "a.cpp", "void cachedFunc() {}");

    // First pass: build and save cache.
    {
        Indexer indexer;
        indexer.build(tempDir);
        [[maybe_unused]] auto r = indexer.saveCache(tempDir);
    }

    // Second pass: initializeIndex should load from cache.
    ServerConfig config;
    config.projectRoot = tempDir;
    config.useCache = true;

    Indexer indexer2;
    initializeIndex(indexer2, config);

    ASSERT_TRUE(indexer2.isReady());
    const auto results = indexer2.search(dc::StringView("cachedFunc"), 10);
    ASSERT_TRUE(results.getSize() >= static_cast<u64>(1));

    std::filesystem::remove_all(tempDir);
}

DTEST(initializeIndexFallsBackToBuildOnCorruptCache)
{
    const auto tempDir = std::filesystem::temp_directory_path() / "symbols_server_init_corrupt";
    std::filesystem::create_directories(tempDir);
    writeTempFile(tempDir / "a.cpp", "void liveFunc() {}");

    // Write a corrupt cache file.
    const auto cacheDir = tempDir / ".cache";
    std::filesystem::create_directories(cacheDir);
    writeTempFile(cacheDir / "symbols-index.json", "this is not valid json !!!###");

    ServerConfig config;
    config.projectRoot = tempDir;
    config.useCache = true;

    Indexer indexer;
    initializeIndex(indexer, config);

    // Even with a corrupt cache, the indexer should be ready from a fresh build.
    ASSERT_TRUE(indexer.isReady());
    const auto results = indexer.search(dc::StringView("liveFunc"), 10);
    ASSERT_TRUE(results.getSize() >= static_cast<u64>(1));

    std::filesystem::remove_all(tempDir);
}
