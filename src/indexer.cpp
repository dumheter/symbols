#include <indexer.hpp>

#include <dc/file.hpp>
#include <dc/job_system.hpp>
#include <dc/log.hpp>
#include <dc/time.hpp>

#include <json.hpp>
#include <scanner.hpp>

#include <algorithm>
#include <cstring>
#include <system_error>
#include <vector>

namespace symbols {

static constexpr const char* kCacheDir = ".cache";
static constexpr const char* kCacheFilename = "symbols-index.json";

/// Read a file's last-write-time as nanoseconds since epoch.
/// Returns -1 on error.
static auto getFileMtime(const std::filesystem::path& path) -> s64
{
    std::error_code ec;
    const auto ftime = std::filesystem::last_write_time(path, ec);
    if (ec)
        return -1;
    return static_cast<s64>(ftime.time_since_epoch().count());
}

Indexer::Indexer(dc::JobSystem& jobSystem)
    : m_jobSystem(&jobSystem)
    , m_fileCount(0)
    , m_ready(false)
{
}

Indexer::~Indexer() = default;

Indexer::Indexer(Indexer&& other) noexcept
    : m_jobSystem(other.m_jobSystem)
    , m_symbols(dc::move(other.m_symbols))
    , m_fileRecords(dc::move(other.m_fileRecords))
    , m_fileCount(other.m_fileCount)
    , m_ready(other.m_ready)
{
    other.m_jobSystem = nullptr;
    other.m_fileCount = 0;
    other.m_ready = false;
}

auto Indexer::operator=(Indexer&& other) noexcept -> Indexer&
{
    if (this != &other) {
        m_jobSystem = other.m_jobSystem;
        m_symbols = dc::move(other.m_symbols);
        m_fileRecords = dc::move(other.m_fileRecords);
        m_fileCount = other.m_fileCount;
        m_ready = other.m_ready;
        other.m_jobSystem = nullptr;
        other.m_fileCount = 0;
        other.m_ready = false;
    }
    return *this;
}

auto Indexer::build(const std::filesystem::path& projectRoot, const dc::List<dc::String>& searchDirs) -> void
{
    dc::Stopwatch timer;

    const auto extensions = defaultCppExtensions();

    dc::List<std::filesystem::path> scanRoots;
    if (searchDirs.getSize() > 0) {
        for (u64 i = 0; i < searchDirs.getSize(); ++i)
            scanRoots.add(projectRoot / searchDirs[i].c_str());
    } else {
        scanRoots.add(projectRoot);
    }

    dc::List<std::filesystem::path> allFiles;
    for (u64 i = 0; i < scanRoots.getSize(); ++i) {
        auto files = scanDirectory(scanRoots[i], extensions);
        for (u64 j = 0; j < files.getSize(); ++j)
            allFiles.add(dc::move(files[j]));
    }

    m_fileCount = allFiles.getSize();
    LOG_INFO("Found {} files to scan", allFiles.getSize());

    m_symbols.clear();
    m_fileRecords = dc::Map<dc::String, FileRecord>();

    const u32 nFiles = static_cast<u32>(allFiles.getSize());
    std::vector<dc::Result<dc::List<Symbol>, dc::String>> results;
    results.reserve(nFiles);
    for (u32 i = 0; i < nFiles; ++i)
        results.push_back(dc::Err<dc::String>(dc::String("not started")));

    dc::List<dc::Job> jobs;
    jobs.reserve(nFiles);
    for (u32 i = 0; i < nFiles; ++i) {
        jobs.add(dc::Job { [&, i] {
            thread_local Parser tlsParser;
            results[i] = tlsParser.parseFile(allFiles[i], projectRoot);
        } });
    }

    if (jobs.getSize() > 0)
        m_jobSystem->add(jobs).await();

    s64 errorCount = 0;
    for (u32 i = 0; i < nFiles; ++i) {
        if (results[i].isOk()) {
            auto fileSymbols = dc::move(results[i]).unwrap();

            // Record mtime using relative path (same key as symbol.file).
            if (fileSymbols.getSize() > 0) {
                const dc::String relPath(fileSymbols[0].file);
                const s64 mtime = getFileMtime(allFiles[i]);
                auto* rec = m_fileRecords.insert(relPath);
                if (rec)
                    rec->mtime = mtime;
            } else {
                // File parsed successfully but had no symbols — still track it.
                std::error_code ec;
                const auto relPath = std::filesystem::relative(allFiles[i], projectRoot, ec);
                if (!ec) {
                    const dc::String key(relPath.string().c_str());
                    const s64 mtime = getFileMtime(allFiles[i]);
                    auto* rec = m_fileRecords.insert(key);
                    if (rec)
                        rec->mtime = mtime;
                }
            }

            for (u64 j = 0; j < fileSymbols.getSize(); ++j)
                m_symbols.add(dc::move(fileSymbols[j]));
        } else {
            ++errorCount;
        }
    }

    timer.stop();
    m_ready = true;

    LOG_INFO("Indexed {} symbols from {} files in {:.2f}s ({} errors)", m_symbols.getSize(), allFiles.getSize(),
        timer.fs(), errorCount);
}
auto Indexer::incrementalBuild(const std::filesystem::path& projectRoot, const dc::List<dc::String>& searchDirs) -> void
{
    // If we have no existing file records, fall back to a full build.
    if (m_fileRecords.getSize() == 0) {
        LOG_INFO("No existing file records; falling back to full build");
        build(projectRoot, searchDirs);
        return;
    }

    dc::Stopwatch timer;

    const auto extensions = defaultCppExtensions();

    dc::List<std::filesystem::path> scanRoots;
    if (searchDirs.getSize() > 0) {
        for (u64 i = 0; i < searchDirs.getSize(); ++i)
            scanRoots.add(projectRoot / searchDirs[i].c_str());
    } else {
        scanRoots.add(projectRoot);
    }

    dc::List<std::filesystem::path> allFiles;
    for (u64 i = 0; i < scanRoots.getSize(); ++i) {
        auto files = scanDirectory(scanRoots[i], extensions);
        for (u64 j = 0; j < files.getSize(); ++j)
            allFiles.add(dc::move(files[j]));
    }

    // Build a map of all current files: relative path -> absolute path.
    dc::Map<dc::String, bool> currentFileSet;
    for (u64 i = 0; i < allFiles.getSize(); ++i) {
        std::error_code ec;
        const auto rel = std::filesystem::relative(allFiles[i], projectRoot, ec);
        if (!ec) {
            auto* slot = currentFileSet.insert(dc::String(rel.string().c_str()));
            if (slot)
                *slot = true;
        }
    }

    // Identify removed files: tracked in m_fileRecords but no longer on disk.
    // Use removeIf to also clean up m_fileRecords in the same pass.
    dc::Map<dc::String, bool> evictSet;
    m_fileRecords.removeIf([&](const dc::Map<dc::String, FileRecord>::Entry& entry) -> bool {
        if (!currentFileSet.tryGet(entry.key)) {
            // File was deleted — mark for symbol eviction.
            auto* slot = evictSet.insert(entry.key);
            if (slot)
                *slot = true;
            return true; // remove from m_fileRecords
        }
        return false;
    });

    // Identify changed and new files (mtime mismatch or not yet tracked).
    dc::List<std::filesystem::path> filesToParse;
    for (u64 i = 0; i < allFiles.getSize(); ++i) {
        std::error_code ec;
        const auto rel = std::filesystem::relative(allFiles[i], projectRoot, ec);
        if (ec)
            continue;
        const dc::String key(rel.string().c_str());
        const s64 diskMtime = getFileMtime(allFiles[i]);
        const auto* rec = m_fileRecords.tryGet(key);
        if (!rec || rec->value.mtime != diskMtime) {
            filesToParse.add(allFiles[i]);
            // Add to evict set so old symbols for this file are removed.
            if (!evictSet.tryGet(key)) {
                auto* slot = evictSet.insert(key);
                if (slot)
                    *slot = true;
            }
        }
    }

    const u64 removedCount = evictSet.getSize() - filesToParse.getSize();
    const u64 changedCount = filesToParse.getSize();

    LOG_INFO("Incremental rebuild: {} changed/new, {} removed (of {} total)", changedCount, removedCount,
        allFiles.getSize());

    if (evictSet.getSize() == 0) {
        // Nothing to do.
        m_fileCount = allFiles.getSize();
        m_ready = true;
        timer.stop();
        LOG_INFO("Index already up to date ({:.2f}s)", timer.fs());
        return;
    }

    // Evict stale symbols in a single pass (stable removal).
    {
        u64 writeIdx = 0;
        for (u64 i = 0; i < m_symbols.getSize(); ++i) {
            if (!evictSet.tryGet(m_symbols[i].file)) {
                if (writeIdx != i)
                    m_symbols[writeIdx] = dc::move(m_symbols[i]);
                ++writeIdx;
            }
        }
        m_symbols.resize(writeIdx);
    }

    // Re-parse changed/new files in parallel and insert their symbols.
    const u32 nToParse = static_cast<u32>(filesToParse.getSize());
    std::vector<dc::Result<dc::List<Symbol>, dc::String>> results;
    results.reserve(nToParse);
    for (u32 i = 0; i < nToParse; ++i)
        results.push_back(dc::Err<dc::String>(dc::String("not started")));

    dc::List<dc::Job> jobs;
    jobs.reserve(nToParse);
    for (u32 i = 0; i < nToParse; ++i) {
        jobs.add(dc::Job { [&, i] {
            thread_local Parser tlsParser;
            results[i] = tlsParser.parseFile(filesToParse[i], projectRoot);
        } });
    }

    if (jobs.getSize() > 0)
        m_jobSystem->add(jobs).await();

    s64 errorCount = 0;
    for (u32 i = 0; i < nToParse; ++i) {
        if (results[i].isOk()) {
            auto fileSymbols = dc::move(results[i]).unwrap();

            // Update mtime record.
            std::error_code ec;
            const auto rel = std::filesystem::relative(filesToParse[i], projectRoot, ec);
            if (!ec) {
                const dc::String key(rel.string().c_str());
                const s64 mtime = getFileMtime(filesToParse[i]);
                auto* rec = m_fileRecords.tryGet(key);
                if (rec) {
                    rec->value.mtime = mtime;
                } else {
                    auto* newRec = m_fileRecords.insert(key);
                    if (newRec)
                        newRec->mtime = mtime;
                }
            }

            for (u64 j = 0; j < fileSymbols.getSize(); ++j)
                m_symbols.add(dc::move(fileSymbols[j]));
        } else {
            ++errorCount;
        }
    }

    m_fileCount = allFiles.getSize();
    m_ready = true;
    timer.stop();

    LOG_INFO("Incremental rebuild done: {} symbols total in {:.2f}s ({} errors)", m_symbols.getSize(), timer.fs(),
        errorCount);
}

auto Indexer::saveCache(const std::filesystem::path& projectRoot) -> dc::Result<bool, dc::String>
{
    const auto cacheDir = projectRoot / kCacheDir;
    std::error_code ec;
    std::filesystem::create_directories(cacheDir, ec);
    if (ec) {
        dc::String err("Failed to create cache directory: ");
        err += ec.message().c_str();
        return dc::Err<dc::String>(dc::move(err));
    }

    const auto cachePath = cacheDir / kCacheFilename;

    // Build JSON manually for performance.
    dc::String json;
    json += "{\"version\":2,\"symbols\":[";

    for (u64 i = 0; i < m_symbols.getSize(); ++i) {
        if (i > 0)
            json += ",";
        const Symbol& sym = m_symbols[i];
        json += "{\"n\":\"";
        json += jsonEscapeString(dc::StringView(sym.name));
        json += "\",\"k\":\"";
        json += symbolKindToString(sym.kind);
        json += "\",\"f\":\"";
        json += jsonEscapeString(dc::StringView(sym.file));
        json += "\",\"l\":";
        char lineBuf[16];
        std::snprintf(lineBuf, sizeof(lineBuf), "%u", sym.line);
        json += lineBuf;
        json += "}";
    }
    json += "],\"files\":[";

    // Emit all file records tracked in m_fileRecords.
    bool firstFile = true;
    for (const auto& entry : m_fileRecords) {
        if (!firstFile)
            json += ",";
        firstFile = false;
        json += "{\"p\":\"";
        json += jsonEscapeString(dc::StringView(entry.key));
        json += "\",\"m\":";
        char mtimeBuf[32];
        std::snprintf(mtimeBuf, sizeof(mtimeBuf), "%lld", static_cast<long long>(entry.value.mtime));
        json += mtimeBuf;
        json += "}";
    }
    json += "]}";

    // dc::File::open is an instance method.
    dc::File file;
    auto openResult = file.open(dc::String(cachePath.string().c_str()), dc::File::Mode::kWrite);
    if (openResult.isErr()) {
        dc::String err("Failed to open cache file for writing: ");
        err += cachePath.string().c_str();
        return dc::Err<dc::String>(dc::move(err));
    }
    [[maybe_unused]] auto writeResult = file.write(json);

    LOG_INFO("Saved {} symbols to cache: {}", m_symbols.getSize(), cachePath.string().c_str());
    return dc::Ok<bool>(true);
}

auto Indexer::loadCache(const std::filesystem::path& projectRoot) -> dc::Result<bool, dc::String>
{
    const auto cachePath = projectRoot / kCacheDir / kCacheFilename;

    dc::File file;
    auto openResult = file.open(dc::String(cachePath.string().c_str()), dc::File::Mode::kRead);
    if (openResult.isErr())
        return dc::Err<dc::String>(dc::String("Cache file not found"));

    auto readResult = file.read();
    if (!readResult.isOk())
        return dc::Err<dc::String>(dc::String("Failed to read cache file"));

    const dc::String content = dc::move(readResult).unwrap();

    auto parseResult = JsonValue::parse(dc::StringView(content));
    if (!parseResult.isOk()) {
        dc::String err("Failed to parse cache: ");
        err += dc::move(parseResult).unwrapErr();
        return dc::Err<dc::String>(dc::move(err));
    }

    const JsonValue root = dc::move(parseResult).unwrap();

    const s64 version = static_cast<s64>(root.getNumber("version"));
    if (version != 1 && version != 2)
        return dc::Err<dc::String>(dc::String("Unsupported cache version"));

    const JsonValue* symbolsArray = root.get("symbols");
    if (!symbolsArray || symbolsArray->type() != JsonValue::Type::Array)
        return dc::Err<dc::String>(dc::String("Invalid cache format"));

    m_symbols.clear();
    m_fileRecords = dc::Map<dc::String, FileRecord>();

    const usize count = symbolsArray->arraySize();
    m_symbols.reserve(count);

    for (usize i = 0; i < count; ++i) {
        const JsonValue& entry = symbolsArray->at(i);
        Symbol sym;
        sym.name = dc::String(entry.getString("n"));
        sym.kind = stringToSymbolKind(entry.getString("k"));
        sym.file = dc::String(entry.getString("f"));
        sym.line = static_cast<u32>(entry.getNumber("l"));
        m_symbols.add(dc::move(sym));
    }

    // Load file records if present (version 2+).
    const JsonValue* filesArray = root.get("files");
    if (filesArray && filesArray->type() == JsonValue::Type::Array) {
        const usize fileCount = filesArray->arraySize();
        for (usize i = 0; i < fileCount; ++i) {
            const JsonValue& entry = filesArray->at(i);
            dc::String path(entry.getString("p"));
            const s64 mtime = static_cast<s64>(entry.getNumber("m"));
            auto* rec = m_fileRecords.insert(path);
            if (rec)
                rec->mtime = mtime;
        }
    }

    // Count unique files.
    dc::Map<dc::String, bool> uniqueFiles;
    for (u64 i = 0; i < m_symbols.getSize(); ++i) {
        if (!uniqueFiles.tryGet(m_symbols[i].file)) {
            auto* val = uniqueFiles.insert(m_symbols[i].file);
            if (val)
                *val = true;
        }
    }
    m_fileCount = uniqueFiles.getSize();

    m_ready = true;
    LOG_INFO("Loaded {} symbols from cache (version {})", m_symbols.getSize(), version);
    return dc::Ok<bool>(true);
}

auto Indexer::hasCacheFile(const std::filesystem::path& projectRoot) const -> bool
{
    const auto cachePath = projectRoot / kCacheDir / kCacheFilename;
    return std::filesystem::exists(cachePath);
}

// ============================================================================
// Fuzzy matching and search
// ============================================================================

/// Check whether a character suggests a token is a file path (not a name/kind token).
static auto isFileToken(dc::StringView token) -> bool
{
    for (u64 i = 0; i < token.getSize(); ++i) {
        const char c = token[i];
        if (c == '/' || c == '\\' || c == '.')
            return true;
    }
    return false;
}

/// Case-insensitive equality for two StringViews.
static auto equalsIgnoreCase(dc::StringView a, dc::StringView b) -> bool
{
    if (a.getSize() != b.getSize())
        return false;
    auto toLower = [](char c) -> char { return (c >= 'A' && c <= 'Z') ? static_cast<char>(c + 32) : c; };
    for (u64 i = 0; i < a.getSize(); ++i) {
        if (toLower(a[i]) != toLower(b[i]))
            return false;
    }
    return true;
}

/// Map a token to a SymbolKind if it matches a known kind name, otherwise returns nullopt-style via out param.
static auto tokenToKind(dc::StringView token, SymbolKind& out) -> bool
{
    if (equalsIgnoreCase(token, dc::StringView("function"))) {
        out = SymbolKind::Function;
        return true;
    }
    if (equalsIgnoreCase(token, dc::StringView("class"))) {
        out = SymbolKind::Class;
        return true;
    }
    if (equalsIgnoreCase(token, dc::StringView("struct"))) {
        out = SymbolKind::Struct;
        return true;
    }
    if (equalsIgnoreCase(token, dc::StringView("enum"))) {
        out = SymbolKind::Enum;
        return true;
    }
    if (equalsIgnoreCase(token, dc::StringView("alias"))) {
        out = SymbolKind::Alias;
        return true;
    }
    if (equalsIgnoreCase(token, dc::StringView("typedef"))) {
        out = SymbolKind::Typedef;
        return true;
    }
    return false;
}

/// Split a StringView on ASCII space characters into a list of non-empty tokens.
static auto splitOnSpaces(dc::StringView input) -> dc::List<dc::String>
{
    dc::List<dc::String> tokens;
    u64 start = 0;
    for (u64 i = 0; i <= input.getSize(); ++i) {
        const bool atEnd = (i == input.getSize());
        if (atEnd || input[i] == ' ') {
            if (i > start) {
                dc::String tok;
                for (u64 j = start; j < i; ++j)
                    tok += input[j];
                tokens.add(dc::move(tok));
            }
            start = i + 1;
        }
    }
    return tokens;
}

auto Indexer::scoreMatch(dc::StringView name, dc::StringView pattern) const -> s32
{
    if (pattern.getSize() == 0)
        return 0;
    if (name.getSize() == 0 || pattern.getSize() > name.getSize())
        return -1;

    auto toLower = [](char c) -> char { return (c >= 'A' && c <= 'Z') ? static_cast<char>(c + 32) : c; };

    // Exact match.
    if (name.getSize() == pattern.getSize()) {
        bool exact = true;
        for (u64 i = 0; i < name.getSize(); ++i) {
            if (toLower(name[i]) != toLower(pattern[i])) {
                exact = false;
                break;
            }
        }
        if (exact)
            return 1000;
    }

    // Prefix match.
    {
        bool isPrefix = true;
        for (u64 i = 0; i < pattern.getSize(); ++i) {
            if (toLower(name[i]) != toLower(pattern[i])) {
                isPrefix = false;
                break;
            }
        }
        if (isPrefix) {
            const s32 bonus = static_cast<s32>(100 - (name.getSize() - pattern.getSize()));
            return 500 + (bonus > 0 ? bonus : 0);
        }
    }

    // Subsequence matching.
    u64 patternIdx = 0;
    s32 score = 0;
    s64 lastMatchIdx = -1;

    auto isWordBoundary = [](dc::StringView str, u64 idx) -> bool {
        if (idx == 0)
            return true;
        const char prev = str[idx - 1];
        const char curr = str[idx];
        if (prev == '_' || prev == ':')
            return true;
        if (prev >= 'a' && prev <= 'z' && curr >= 'A' && curr <= 'Z')
            return true;
        return false;
    };

    for (u64 nameIdx = 0; nameIdx < name.getSize() && patternIdx < pattern.getSize(); ++nameIdx) {
        if (toLower(name[nameIdx]) == toLower(pattern[patternIdx])) {
            if (lastMatchIdx >= 0 && static_cast<s64>(nameIdx) == lastMatchIdx + 1)
                score += 10;
            if (isWordBoundary(name, nameIdx))
                score += 20;
            if (name[nameIdx] == pattern[patternIdx])
                score += 5;
            lastMatchIdx = static_cast<s64>(nameIdx);
            ++patternIdx;
        }
    }

    if (patternIdx < pattern.getSize())
        return -1;

    score -= static_cast<s32>(name.getSize()) / 4;
    return score > 0 ? score : 1;
}

auto Indexer::search(dc::StringView pattern, u32 limit) const -> dc::List<SearchResult>
{
    dc::List<SearchResult> matches;
    if (!m_ready || pattern.getSize() == 0)
        return matches;

    // Parse the pattern into tokens separated by spaces.
    // Each token is classified as:
    //   - A kind filter  : exactly matches a known kind name (struct, class, function, enum, alias, typedef)
    //   - A file token   : contains '.', '/', or '\' — contributes to a file substring filter
    //   - A name token   : everything else — concatenated to form the name fuzzy query
    const auto tokens = splitOnSpaces(pattern);

    bool hasKindFilter = false;
    SymbolKind kindFilter = SymbolKind::Function;
    dc::String nameQuery;
    dc::String fileQuery; // concatenated file tokens (no separator — fuzzy match)

    for (u64 i = 0; i < tokens.getSize(); ++i) {
        const dc::StringView tok(tokens[i]);
        SymbolKind k = SymbolKind::Function;
        if (tokenToKind(tok, k)) {
            hasKindFilter = true;
            kindFilter = k;
        } else if (isFileToken(tok)) {
            fileQuery += tokens[i];
        } else {
            nameQuery += tokens[i];
        }
    }

    for (u64 i = 0; i < m_symbols.getSize(); ++i) {
        const Symbol& sym = m_symbols[i];

        // Kind filter — hard exclusion.
        if (hasKindFilter && sym.kind != kindFilter)
            continue;

        // File filter — substring match (case-insensitive) on the relative file path.
        if (fileQuery.getSize() > 0) {
            const dc::StringView fileSv(sym.file);
            const dc::StringView fq(fileQuery);
            // Check whether fq is a subsequence of fileSv (reuse scoreMatch logic).
            const s32 fileScore = scoreMatch(fileSv, fq);
            if (fileScore < 0)
                continue;
        }

        // Name query — fuzzy score.  If nameQuery is empty (user only typed kind/file tokens)
        // we accept all surviving symbols with score 0.
        s32 score = 0;
        if (nameQuery.getSize() > 0) {
            score = scoreMatch(dc::StringView(sym.name), dc::StringView(nameQuery));
            if (score < 0)
                continue;
        }

        SearchResult r;
        r.symbol = &sym;
        r.score = score;
        matches.add(r);
    }

    if (matches.getSize() > 1) {
        std::sort(matches.begin(), matches.end(),
            [](const SearchResult& a, const SearchResult& b) { return a.score > b.score; });
    }

    if (matches.getSize() > static_cast<u64>(limit))
        matches.resize(static_cast<u64>(limit));

    return matches;
}

auto Indexer::symbolCount() const -> usize
{
    return m_symbols.getSize();
}
auto Indexer::fileCount() const -> usize
{
    return m_fileCount;
}
auto Indexer::isReady() const -> bool
{
    return m_ready;
}

} // namespace symbols
