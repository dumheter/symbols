#include <server.hpp>

#include <dc/log.hpp>

#include <indexer.hpp>
#include <json.hpp>
#include <parser.hpp>
#include <protocol.hpp>

#include <iostream>
#include <ostream>
#include <string>

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif

namespace symbols {

auto sendResponse(std::ostream& out, const dc::String& json) -> void
{
    out << json.c_str() << '\n';
    out.flush();
}

auto handleRequest(const Request& req, Indexer& indexer, const ServerConfig& config) -> dc::String
{
    switch (req.method) {
    case Method::Query: {
        auto results = indexer.search(dc::StringView(req.pattern), static_cast<u32>(req.limit));
        auto symbolsArray = JsonValue::makeArray();
        for (u64 i = 0; i < results.getSize(); ++i) {
            const SearchResult& sr = results[i];
            auto sym = JsonValue::makeObject();
            sym.set(dc::String("name"), JsonValue::makeString(dc::String(sr.symbol->name)));
            sym.set(dc::String("kind"), JsonValue::makeString(dc::String(symbolKindToString(sr.symbol->kind))));
            sym.set(dc::String("file"), JsonValue::makeString(dc::String(sr.symbol->file)));
            sym.set(dc::String("line"), JsonValue::makeNumber(static_cast<s64>(sr.symbol->line)));
            sym.set(dc::String("score"), JsonValue::makeNumber(static_cast<s64>(sr.score)));
            symbolsArray.pushBack(dc::move(sym));
        }
        return buildQueryResponse(req.id, symbolsArray);
    }

    case Method::Status: {
        const dc::StringView status = indexer.isReady() ? dc::StringView("ready") : dc::StringView("building");
        return buildStatusResponse(
            req.id, status, static_cast<s64>(indexer.fileCount()), static_cast<s64>(indexer.symbolCount()));
    }

    case Method::Rebuild: {
        LOG_INFO("Rebuilding index (incremental)...");
        indexer.incrementalBuild(config.projectRoot, config.searchDirs, config.diagnostics);
        if (config.useCache) {
            auto r = indexer.saveCache(config.projectRoot);
            if (!r.isOk())
                LOG_WARNING("Failed to save cache after rebuild");
        }
        return buildAckResponse(req.id, "rebuilt");
    }

    case Method::RebuildFile: {
        if (req.file.getSize() == 0)
            return buildErrorResponse(req.id, "rebuildFile requires a non-empty file path");
        LOG_INFO("Rebuilding single file: {}", req.file.c_str());
        indexer.rebuildFile(std::filesystem::path(req.file.c_str()), config.projectRoot);
        return buildAckResponse(req.id, "rebuilt");
    }

    case Method::Shutdown:
        return buildAckResponse(req.id, "shutdown");

    case Method::Unknown:
        return buildErrorResponse(req.id, "Unknown method");
    }

    return buildErrorResponse(req.id, "Internal error");
}

auto initializeIndex(Indexer& indexer, const ServerConfig& config) -> void
{
    if (config.useCache && indexer.hasCacheFile(config.projectRoot)) {
        LOG_INFO("Loading index from cache...");
        auto loadResult = indexer.loadCache(config.projectRoot);
        if (loadResult.isOk()) {
            LOG_INFO("Cache loaded successfully");
        } else {
            LOG_WARNING("Cache load failed, building fresh index");
            indexer.build(config.projectRoot, config.searchDirs, config.diagnostics);
            if (config.useCache) {
                auto r = indexer.saveCache(config.projectRoot);
                if (!r.isOk())
                    LOG_WARNING("Failed to save cache");
            }
        }
    } else {
        LOG_INFO("Building index...");
        indexer.build(config.projectRoot, config.searchDirs, config.diagnostics);
        if (config.useCache) {
            auto r = indexer.saveCache(config.projectRoot);
            if (!r.isOk())
                LOG_WARNING("Failed to save cache");
        }
    }
}

auto runServerLoop(std::istream& in, std::ostream& out, Indexer& indexer, const ServerConfig& config) -> s32
{
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty())
            continue;
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        if (line.empty())
            continue;

        const dc::StringView lineView(line.c_str(), static_cast<u64>(line.size()));
        auto reqResult = parseRequest(lineView);
        if (!reqResult.isOk()) {
            LOG_WARNING("Failed to parse request: {}", dc::move(reqResult).unwrapErr().c_str());
            sendResponse(out, buildErrorResponse(0, "Failed to parse request"));
            continue;
        }

        const Request req = dc::move(reqResult).unwrap();
        if (req.method == Method::Shutdown) {
            sendResponse(out, handleRequest(req, indexer, config));
            LOG_INFO("Shutdown requested, exiting");
            break;
        }

        sendResponse(out, handleRequest(req, indexer, config));
    }

    LOG_INFO("Symbol server exiting");
    return 0;
}

auto runServer(const ServerConfig& config) -> s32
{
#ifdef _WIN32
    _setmode(_fileno(stdin), _O_BINARY);
    _setmode(_fileno(stdout), _O_BINARY);
#endif

    LOG_INFO("Symbol server starting for project: {}", config.projectRoot.string().c_str());

    Indexer indexer(*config.jobSystem);
    initializeIndex(indexer, config);

    sendResponse(std::cout,
        buildStatusResponse(
            0, "ready", static_cast<s64>(indexer.fileCount()), static_cast<s64>(indexer.symbolCount())));

    return runServerLoop(std::cin, std::cout, indexer, config);
}

} // namespace symbols
