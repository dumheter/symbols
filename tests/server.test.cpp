#include <dc/dtest.hpp>
#include <indexer.hpp>
#include <json.hpp>
#include <protocol.hpp>
#include <server.hpp>

#include <test_helpers.hpp>

#include <chrono>
#include <filesystem>
#include <sstream>
#include <thread>
#include <vector>

using namespace symbols;

// Shared real-project indexer for server tests that only need a ready index.
static auto sharedIndexer() -> Indexer&
{
    static Indexer indexer(sharedJobSystem());
    if (!indexer.isReady())
        indexer.build(repoRoot());
    return indexer;
}

static auto sharedConfig() -> ServerConfig
{
    ServerConfig config;
    config.projectRoot = repoRoot();
    config.useCache = false;
    return config;
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
    auto& indexer = sharedIndexer();
    const auto config = sharedConfig();

    // "scoreMatch" is a real function in the project — exact match must be top result.
    const Request req = makeQueryRequest(dc::String("scoreMatch"));
    const dc::String response = handleRequest(req, indexer, config);

    auto parseResult = JsonValue::parse(dc::StringView(response));
    ASSERT_TRUE(parseResult.isOk());
    const auto val = dc::move(parseResult).unwrap();

    ASSERT_EQ(val.getNumber("id"), static_cast<s64>(1));
    const JsonValue* syms = val.get("symbols");
    ASSERT_TRUE(syms != nullptr);
    ASSERT_TRUE(syms->arraySize() >= static_cast<usize>(1));

    const JsonValue& first = syms->at(0);
    ASSERT_TRUE(dc::String(first.getString("name")) == "scoreMatch");
}

DTEST(handleRequestQueryEmptyResultsWhenNoMatch)
{
    auto& indexer = sharedIndexer();
    const auto config = sharedConfig();

    const Request req = makeQueryRequest(dc::String("zzznomatch"));
    const dc::String response = handleRequest(req, indexer, config);

    auto parseResult = JsonValue::parse(dc::StringView(response));
    ASSERT_TRUE(parseResult.isOk());
    const auto val = dc::move(parseResult).unwrap();
    const JsonValue* syms = val.get("symbols");
    ASSERT_TRUE(syms != nullptr);
    ASSERT_EQ(syms->arraySize(), static_cast<usize>(0));
}

DTEST(handleRequestQuerySymbolHasExpectedFields)
{
    // JsonValue is a real class in src/json.hpp — check all fields are present.
    auto& indexer = sharedIndexer();
    const auto config = sharedConfig();

    const Request req = makeQueryRequest(dc::String("JsonValue"));
    const dc::String response = handleRequest(req, indexer, config);

    auto parseResult = JsonValue::parse(dc::StringView(response));
    ASSERT_TRUE(parseResult.isOk());
    const auto val = dc::move(parseResult).unwrap();
    const JsonValue* syms = val.get("symbols");
    ASSERT_TRUE(syms != nullptr);
    ASSERT_TRUE(syms->arraySize() >= static_cast<usize>(1));

    // Find the class entry among the results (constructors also match, so scan for kind=="class").
    const JsonValue* classSym = nullptr;
    for (usize i = 0; i < syms->arraySize(); ++i) {
        const JsonValue& s = syms->at(i);
        if (dc::String(s.getString("kind")) == "class") {
            classSym = &s;
            break;
        }
    }
    ASSERT_TRUE(classSym != nullptr);
    ASSERT_TRUE(dc::String(classSym->getString("name")) == "JsonValue");
    ASSERT_TRUE(classSym->getString("file").getSize() > static_cast<usize>(0));
    ASSERT_TRUE(classSym->getNumber("line") >= static_cast<s64>(1));
    const JsonValue* scoreVal = classSym->get("score");
    ASSERT_TRUE(scoreVal != nullptr);
    ASSERT_TRUE(scoreVal->asNumber() > static_cast<s64>(0));
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

    Indexer indexer(sharedJobSystem());
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
    Indexer indexer(sharedJobSystem());
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
    auto& indexer = sharedIndexer();
    auto config = sharedConfig();

    const Request req = makeRequest(Method::Rebuild, 10);
    const dc::String response = handleRequest(req, indexer, config);

    auto parseResult = JsonValue::parse(dc::StringView(response));
    ASSERT_TRUE(parseResult.isOk());
    const auto val = dc::move(parseResult).unwrap();

    ASSERT_EQ(val.getNumber("id"), static_cast<s64>(10));
    ASSERT_TRUE(dc::String(val.getString("status")) == "rebuilt");
    ASSERT_TRUE(val.get("error") == nullptr);
}

DTEST(handleRequestRebuildPicksUpNewFile)
{
    const auto tempDir = std::filesystem::temp_directory_path() / "symbols_server_rebuild_new";
    std::filesystem::create_directories(tempDir);
    writeTempFile(tempDir / "a.cpp", "void existing() {}");

    Indexer indexer(sharedJobSystem());
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

DTEST(handleRequestForceRebuildReturnsAck)
{
    const auto tempDir = std::filesystem::temp_directory_path() / "symbols_server_force_rebuild_ack";
    std::filesystem::create_directories(tempDir);
    writeTempFile(tempDir / "a.cpp", "void oldForce() {}");

    Indexer indexer(sharedJobSystem());
    indexer.build(tempDir);
    [[maybe_unused]] auto saveResult = indexer.saveCache(tempDir);

    std::this_thread::sleep_for(std::chrono::milliseconds(1100));
    writeTempFile(tempDir / "a.cpp", "void newForce() {}");

    ServerConfig config;
    config.projectRoot = tempDir;
    config.useCache = true;

    const Request req = makeRequest(Method::ForceRebuild, 12);
    const dc::String response = handleRequest(req, indexer, config);

    auto parseResult = JsonValue::parse(dc::StringView(response));
    ASSERT_TRUE(parseResult.isOk());
    const auto val = dc::move(parseResult).unwrap();

    ASSERT_EQ(val.getNumber("id"), static_cast<s64>(12));
    ASSERT_TRUE(dc::String(val.getString("status")) == "rebuilt");
    ASSERT_TRUE(val.get("error") == nullptr);

    ASSERT_EQ(indexer.search(dc::StringView("oldForce"), 10).getSize(), static_cast<u64>(0));
    ASSERT_TRUE(indexer.search(dc::StringView("newForce"), 10).getSize() >= static_cast<u64>(1));
    // Cache save now happens in runServerLoop after the response is sent, not inside handleRequest.
    ASSERT_TRUE(indexer.isDirty());

    std::filesystem::remove_all(tempDir);
}

// ---------------------------------------------------------------------------
// handleRequest — Method::Shutdown
// ---------------------------------------------------------------------------

DTEST(handleRequestShutdownReturnsAck)
{
    Indexer indexer(sharedJobSystem());
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
    Indexer indexer(sharedJobSystem());
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

    Indexer indexer(sharedJobSystem());
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
        Indexer indexer(sharedJobSystem());
        indexer.build(tempDir);
        [[maybe_unused]] auto r = indexer.saveCache(tempDir);
    }

    // Second pass: initializeIndex should load from cache.
    ServerConfig config;
    config.projectRoot = tempDir;
    config.useCache = true;

    Indexer indexer2(sharedJobSystem());
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

    Indexer indexer(sharedJobSystem());
    initializeIndex(indexer, config);

    // Even with a corrupt cache, the indexer should be ready from a fresh build.
    ASSERT_TRUE(indexer.isReady());
    const auto results = indexer.search(dc::StringView("liveFunc"), 10);
    ASSERT_TRUE(results.getSize() >= static_cast<u64>(1));

    std::filesystem::remove_all(tempDir);
}

DTEST(initializeIndexFirstRunCreatesCacheFile)
{
    // useCache=true but no cache file exists yet.
    // initializeIndex must build fresh AND create the cache file on disk.
    const auto tempDir = std::filesystem::temp_directory_path() / "symbols_server_init_firstrun";
    std::filesystem::create_directories(tempDir);
    writeTempFile(tempDir / "a.cpp", "void firstRun() {}");

    ServerConfig config;
    config.projectRoot = tempDir;
    config.useCache = true;

    Indexer indexer(sharedJobSystem());
    ASSERT_FALSE(indexer.hasCacheFile(tempDir));

    initializeIndex(indexer, config);

    ASSERT_TRUE(indexer.isReady());
    // Cache file must have been written.
    ASSERT_TRUE(indexer.hasCacheFile(tempDir));

    std::filesystem::remove_all(tempDir);
}

DTEST(runServerLoopSavesCacheAfterRebuild)
{
    // When useCache=true, runServerLoop must save the cache file *after* sending
    // the rebuilt response (non-blocking: Emacs gets the ack before the disk write).
    const auto tempDir = std::filesystem::temp_directory_path() / "symbols_server_rebuild_cache";
    std::filesystem::create_directories(tempDir);
    writeTempFile(tempDir / "a.cpp", "void cacheMe() {}");

    Indexer indexer(sharedJobSystem());
    indexer.build(tempDir);

    ServerConfig config;
    config.projectRoot = tempDir;
    config.useCache = true;

    // No cache yet before the rebuild.
    ASSERT_FALSE(indexer.hasCacheFile(tempDir));

    std::istringstream in("{\"id\":1,\"method\":\"rebuild\"}\n{\"id\":2,\"method\":\"shutdown\"}\n");
    std::ostringstream out;
    [[maybe_unused]] const s32 ret = runServerLoop(in, out, indexer, config);

    // Cache must now exist — saved by the loop after sending the rebuilt response.
    ASSERT_TRUE(indexer.hasCacheFile(tempDir));

    std::filesystem::remove_all(tempDir);
}

// ---------------------------------------------------------------------------
// runServerLoop integration tests
// ---------------------------------------------------------------------------

DTEST(runServerLoopRespondsToQuery)
{
    auto& indexer = sharedIndexer();
    const auto config = sharedConfig();

    std::istringstream in(R"({"id":1,"method":"query","params":{"pattern":"scoreMatch","limit":10}})"
                          "\n");
    std::ostringstream out;

    const s32 ret = runServerLoop(in, out, indexer, config);
    ASSERT_EQ(ret, static_cast<s32>(0));

    auto parseResult = JsonValue::parse(dc::StringView(out.str().c_str(), static_cast<u64>(out.str().size())));
    ASSERT_TRUE(parseResult.isOk());
    const auto val = dc::move(parseResult).unwrap();

    ASSERT_EQ(val.getNumber("id"), static_cast<s64>(1));
    const JsonValue* syms = val.get("symbols");
    ASSERT_TRUE(syms != nullptr);
    ASSERT_TRUE(syms->arraySize() >= static_cast<usize>(1));
}

DTEST(runServerLoopRespondsToStatus)
{
    auto& indexer = sharedIndexer();
    const auto config = sharedConfig();

    std::istringstream in(R"({"id":2,"method":"status"})"
                          "\n");
    std::ostringstream out;

    [[maybe_unused]] const auto ret = runServerLoop(in, out, indexer, config);

    auto parseResult = JsonValue::parse(dc::StringView(out.str().c_str(), static_cast<u64>(out.str().size())));
    ASSERT_TRUE(parseResult.isOk());
    const auto val = dc::move(parseResult).unwrap();

    ASSERT_EQ(val.getNumber("id"), static_cast<s64>(2));
    ASSERT_TRUE(dc::String(val.getString("status")) == "ready");
}

DTEST(runServerLoopShutdownExitsLoop)
{
    Indexer indexer(sharedJobSystem());

    ServerConfig config;
    config.projectRoot = std::filesystem::temp_directory_path();
    config.useCache = false;

    std::istringstream in(R"({"id":99,"method":"shutdown"})"
                          "\n");
    std::ostringstream out;

    const s32 ret = runServerLoop(in, out, indexer, config);
    ASSERT_EQ(ret, static_cast<s32>(0));

    auto parseResult = JsonValue::parse(dc::StringView(out.str().c_str(), static_cast<u64>(out.str().size())));
    ASSERT_TRUE(parseResult.isOk());
    const auto val = dc::move(parseResult).unwrap();

    ASSERT_EQ(val.getNumber("id"), static_cast<s64>(99));
    ASSERT_TRUE(dc::String(val.getString("status")) == "shutdown");
}

DTEST(runServerLoopEofExits)
{
    // Empty input → immediate EOF → loop exits cleanly.
    Indexer indexer(sharedJobSystem());

    ServerConfig config;
    config.projectRoot = std::filesystem::temp_directory_path();
    config.useCache = false;

    std::istringstream in("");
    std::ostringstream out;

    const s32 ret = runServerLoop(in, out, indexer, config);
    ASSERT_EQ(ret, static_cast<s32>(0));
    // No response written for empty input.
    ASSERT_TRUE(out.str().empty());
}

DTEST(runServerLoopSkipsBlankLines)
{
    auto& indexer = sharedIndexer();
    const auto config = sharedConfig();

    // Two blank lines (one with CR), then a real query, then shutdown.
    std::istringstream in("\r\n"
                          "\n"
                          R"({"id":3,"method":"query","params":{"pattern":"scoreMatch","limit":10}})"
                          "\n"
                          R"({"id":4,"method":"shutdown"})"
                          "\n");
    std::ostringstream out;

    [[maybe_unused]] const auto ret2 = runServerLoop(in, out, indexer, config);

    // Output should contain exactly two response lines.
    const std::string output = out.str();
    u64 lineCount = 0;
    for (const char c : output) {
        if (c == '\n')
            ++lineCount;
    }
    ASSERT_EQ(lineCount, static_cast<u64>(2));
}

DTEST(runServerLoopReturnsErrorOnBadJson)
{
    Indexer indexer(sharedJobSystem());

    ServerConfig config;
    config.projectRoot = std::filesystem::temp_directory_path();
    config.useCache = false;

    // First line is bad JSON, second is shutdown so the loop terminates.
    std::istringstream in("not valid json at all\n"
                          R"({"id":5,"method":"shutdown"})"
                          "\n");
    std::ostringstream out;

    [[maybe_unused]] const auto ret3 = runServerLoop(in, out, indexer, config);

    // First response must be an error.
    const std::string output = out.str();
    const auto firstNewline = output.find('\n');
    ASSERT_TRUE(firstNewline != std::string::npos);

    const std::string firstLine = output.substr(0, firstNewline);
    auto parseResult = JsonValue::parse(dc::StringView(firstLine.c_str(), static_cast<u64>(firstLine.size())));
    ASSERT_TRUE(parseResult.isOk());
    const auto val = dc::move(parseResult).unwrap();
    ASSERT_TRUE(val.getString("error").getSize() > static_cast<usize>(0));
}

// ---------------------------------------------------------------------------
// handleRequest — Method::RebuildFile
// ---------------------------------------------------------------------------

DTEST(handleRequestRebuildFileReturnsAck)
{
    const auto tempDir = std::filesystem::temp_directory_path() / "symbols_server_rbfile_ack";
    std::filesystem::create_directories(tempDir);
    writeTempFile(tempDir / "a.cpp", "void ackFunc() {}");

    Indexer indexer(sharedJobSystem());
    indexer.build(tempDir);

    ServerConfig config;
    config.projectRoot = tempDir;
    config.useCache = false;

    Request req;
    req.id = 20;
    req.method = Method::RebuildFile;
    req.file = dc::String((tempDir / "a.cpp").string().c_str());
    req.limit = 200;

    const dc::String response = handleRequest(req, indexer, config);

    auto parseResult = JsonValue::parse(dc::StringView(response));
    ASSERT_TRUE(parseResult.isOk());
    const auto val = dc::move(parseResult).unwrap();
    ASSERT_EQ(val.getNumber("id"), static_cast<s64>(20));
    ASSERT_TRUE(dc::String(val.getString("status")) == "rebuilt");
    ASSERT_TRUE(val.get("error") == nullptr);

    std::filesystem::remove_all(tempDir);
}

DTEST(handleRequestRebuildFileEmptyPathReturnsError)
{
    Indexer indexer(sharedJobSystem());

    ServerConfig config;
    config.projectRoot = std::filesystem::temp_directory_path();
    config.useCache = false;

    Request req;
    req.id = 21;
    req.method = Method::RebuildFile;
    // file is intentionally left empty
    req.limit = 200;

    const dc::String response = handleRequest(req, indexer, config);

    auto parseResult = JsonValue::parse(dc::StringView(response));
    ASSERT_TRUE(parseResult.isOk());
    const auto val = dc::move(parseResult).unwrap();
    ASSERT_EQ(val.getNumber("id"), static_cast<s64>(21));
    ASSERT_TRUE(val.getString("error").getSize() > static_cast<usize>(0));
}

DTEST(handleRequestRebuildFileUpdatesSymbols)
{
    // Simulate the Emacs on-save hook: write a file, build the index,
    // overwrite the file with new content, send rebuildFile — the updated
    // symbol must now appear and the old one must be gone.
    const auto tempDir = std::filesystem::temp_directory_path() / "symbols_server_rbfile_update";
    std::filesystem::create_directories(tempDir);
    const auto filePath = tempDir / "a.cpp";
    writeTempFile(filePath, "void beforeSave() {}");

    Indexer indexer(sharedJobSystem());
    indexer.build(tempDir);
    ASSERT_TRUE(indexer.search(dc::StringView("beforeSave"), 5).getSize() >= static_cast<u64>(1));

    // User edits and saves the file.
    writeTempFile(filePath, "void afterSave() {}");

    ServerConfig config;
    config.projectRoot = tempDir;
    config.useCache = false;

    Request req;
    req.id = 22;
    req.method = Method::RebuildFile;
    req.file = dc::String(filePath.string().c_str());
    req.limit = 200;

    [[maybe_unused]] const dc::String response = handleRequest(req, indexer, config);

    // Old symbol gone, new symbol present.
    ASSERT_EQ(indexer.search(dc::StringView("beforeSave"), 5).getSize(), static_cast<u64>(0));
    ASSERT_TRUE(indexer.search(dc::StringView("afterSave"), 5).getSize() >= static_cast<u64>(1));

    std::filesystem::remove_all(tempDir);
}

// ---------------------------------------------------------------------------
// runServerLoop — rebuildFile via JSON protocol (on-save integration)
// ---------------------------------------------------------------------------

DTEST(runServerLoopRebuildFileMidSessionUpdatesSymbol)
{
    // Full end-to-end: index a file, overwrite it, send rebuildFile through
    // the JSON loop, then query to confirm the index reflects the new content.
    const auto tempDir = std::filesystem::temp_directory_path() / "symbols_server_rbfile_loop";
    std::filesystem::create_directories(tempDir);
    const auto filePath = tempDir / "a.cpp";
    writeTempFile(filePath, "void oldFunc() {}");

    Indexer indexer(sharedJobSystem());
    indexer.build(tempDir);

    // Overwrite the file before sending rebuildFile (simulates user saving).
    writeTempFile(filePath, "void newFunc() {}");

    ServerConfig config;
    config.projectRoot = tempDir;
    config.useCache = false;

    // Build the rebuildFile JSON with the absolute path (forward slashes for JSON safety).
    const std::string rebuildMsg
        = std::string(R"({"id":1,"method":"rebuildFile","params":{"file":")") + filePath.generic_string() + R"("}})";

    std::istringstream in(rebuildMsg + "\n" + R"({"id":2,"method":"query","params":{"pattern":"newFunc","limit":10}})"
        + "\n" + R"({"id":3,"method":"shutdown"})" + "\n");
    std::ostringstream out;

    [[maybe_unused]] const auto ret = runServerLoop(in, out, indexer, config);

    // Collect response lines.
    const std::string output = out.str();
    std::vector<std::string> lines;
    std::istringstream lineStream(output);
    std::string l;
    while (std::getline(lineStream, l)) {
        if (!l.empty() && l.back() == '\r')
            l.pop_back();
        if (!l.empty())
            lines.push_back(l);
    }
    // 3 responses: rebuildFile ack, query result, shutdown ack.
    ASSERT_EQ(lines.size(), static_cast<usize>(3));

    // id:2 query must find the new symbol.
    auto parseResult = JsonValue::parse(dc::StringView(lines[1].c_str(), static_cast<u64>(lines[1].size())));
    ASSERT_TRUE(parseResult.isOk());
    const auto val = dc::move(parseResult).unwrap();
    ASSERT_EQ(val.getNumber("id"), static_cast<s64>(2));
    const JsonValue* syms = val.get("symbols");
    ASSERT_TRUE(syms != nullptr);
    ASSERT_TRUE(syms->arraySize() >= static_cast<usize>(1));
    ASSERT_TRUE(dc::String(syms->at(0).getString("name")) == "newFunc");

    std::filesystem::remove_all(tempDir);
}
