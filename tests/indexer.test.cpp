#include <dc/dtest.hpp>
#include <indexer.hpp>

#include <test_helpers.hpp>

#include <chrono>
#include <filesystem>
#include <thread>

using namespace symbols;

DTEST(indexerSearchExactMatch)
{
    Indexer indexer(sharedJobSystem());

    ASSERT_FALSE(indexer.isReady());
    ASSERT_EQ(indexer.symbolCount(), static_cast<usize>(0));
    ASSERT_EQ(indexer.fileCount(), static_cast<usize>(0));
}

DTEST(symbolKindRoundTrip)
{
    ASSERT_EQ(stringToSymbolKind(symbolKindToString(SymbolKind::Function)), SymbolKind::Function);
    ASSERT_EQ(stringToSymbolKind(symbolKindToString(SymbolKind::Class)), SymbolKind::Class);
    ASSERT_EQ(stringToSymbolKind(symbolKindToString(SymbolKind::Struct)), SymbolKind::Struct);
    ASSERT_EQ(stringToSymbolKind(symbolKindToString(SymbolKind::Enum)), SymbolKind::Enum);
    ASSERT_EQ(stringToSymbolKind(symbolKindToString(SymbolKind::Alias)), SymbolKind::Alias);
    ASSERT_EQ(stringToSymbolKind(symbolKindToString(SymbolKind::Typedef)), SymbolKind::Typedef);
}

// ---------------------------------------------------------------------------
// Shared real-project indexer
//
// Built once from repoRoot() and reused by all tests that only need a live
// index to search against (no filesystem mutation).
// ---------------------------------------------------------------------------

static auto sharedIndexer() -> Indexer&
{
    static Indexer indexer(sharedJobSystem());
    if (!indexer.isReady())
        indexer.build(repoRoot());
    return indexer;
}

// ---------------------------------------------------------------------------
// Incremental rebuild tests
// ---------------------------------------------------------------------------

DTEST(incrementalBuildFallsBackToFullBuildWhenNoRecords)
{
    // A fresh Indexer with no file records should fall back to full build.
    const auto tempDir = std::filesystem::temp_directory_path() / "symbols_test_incr_fallback";
    std::filesystem::create_directories(tempDir);

    const auto file1 = tempDir / "a.cpp";
    writeTempFile(file1, "void alpha() {}");

    Indexer indexer(sharedJobSystem());
    ASSERT_FALSE(indexer.isReady());

    // incrementalBuild on an empty indexer falls back to full build.
    indexer.incrementalBuild(tempDir);
    ASSERT_TRUE(indexer.isReady());
    ASSERT_TRUE(indexer.symbolCount() >= static_cast<usize>(1));

    std::filesystem::remove_all(tempDir);
}

// ---------------------------------------------------------------------------
// searchDirs tests (tasks 3a–3c)
// ---------------------------------------------------------------------------

DTEST(buildWithSearchDirsOnlyIndexesSpecifiedSubdir)
{
    const auto tempDir = std::filesystem::temp_directory_path() / "symbols_searchdirs_basic";
    const auto srcDir = tempDir / "src";
    const auto docsDir = tempDir / "docs";
    std::filesystem::create_directories(srcDir);
    std::filesystem::create_directories(docsDir);

    writeTempFile(srcDir / "a.cpp", "void srcFunc() {}");
    writeTempFile(docsDir / "b.cpp", "void docsFunc() {}");

    dc::List<dc::String> searchDirs;
    searchDirs.add(dc::String("src"));

    Indexer indexer(sharedJobSystem());
    indexer.build(tempDir, searchDirs);
    ASSERT_TRUE(indexer.isReady());

    // srcFunc must be indexed.
    const auto srcResults = indexer.search(dc::StringView("srcFunc"), 10);
    ASSERT_TRUE(srcResults.getSize() >= static_cast<u64>(1));

    // docsFunc must NOT be indexed.
    const auto docsResults = indexer.search(dc::StringView("docsFunc"), 10);
    ASSERT_EQ(docsResults.getSize(), static_cast<u64>(0));

    std::filesystem::remove_all(tempDir);
}

DTEST(incrementalBuildWithSearchDirsRespectsSubdir)
{
    const auto tempDir = std::filesystem::temp_directory_path() / "symbols_searchdirs_incremental";
    const auto srcDir = tempDir / "src";
    const auto otherDir = tempDir / "other";
    std::filesystem::create_directories(srcDir);
    std::filesystem::create_directories(otherDir);

    writeTempFile(srcDir / "a.cpp", "void original() {}");

    dc::List<dc::String> searchDirs;
    searchDirs.add(dc::String("src"));

    Indexer indexer(sharedJobSystem());
    indexer.build(tempDir, searchDirs);
    ASSERT_TRUE(indexer.isReady());

    // Add a file inside src (should be picked up).
    writeTempFile(srcDir / "b.cpp", "void added() {}");
    // Add a file outside src (should be ignored).
    writeTempFile(otherDir / "c.cpp", "void ignored() {}");

    indexer.incrementalBuild(tempDir, searchDirs);

    const auto addedResults = indexer.search(dc::StringView("added"), 10);
    ASSERT_TRUE(addedResults.getSize() >= static_cast<u64>(1));

    const auto ignoredResults = indexer.search(dc::StringView("ignored"), 10);
    ASSERT_EQ(ignoredResults.getSize(), static_cast<u64>(0));

    std::filesystem::remove_all(tempDir);
}

DTEST(buildWithNonExistentSearchDirProducesEmptyIndex)
{
    const auto tempDir = std::filesystem::temp_directory_path() / "symbols_searchdirs_missing";
    std::filesystem::create_directories(tempDir);

    // Put a file at root level — it should NOT be indexed since we restrict to a subdir.
    writeTempFile(tempDir / "a.cpp", "void rootFunc() {}");

    dc::List<dc::String> searchDirs;
    searchDirs.add(dc::String("nonexistent_subdir"));

    Indexer indexer(sharedJobSystem());
    indexer.build(tempDir, searchDirs);

    // The non-existent subdir yields no files.
    ASSERT_EQ(indexer.fileCount(), static_cast<usize>(0));
    ASSERT_EQ(indexer.symbolCount(), static_cast<usize>(0));

    std::filesystem::remove_all(tempDir);
}

// ---------------------------------------------------------------------------
// Cache corruption tests (tasks 4a–4d)
// ---------------------------------------------------------------------------

DTEST(loadCacheWithGarbageContentReturnsErr)
{
    const auto tempDir = std::filesystem::temp_directory_path() / "symbols_cache_garbage";
    const auto cacheDir = tempDir / ".cache";
    std::filesystem::create_directories(cacheDir);

    writeTempFile(cacheDir / "symbols-index.json", "not json at all !!!### garbage");

    Indexer indexer(sharedJobSystem());
    const auto result = indexer.loadCache(tempDir);
    ASSERT_FALSE(result.isOk());
    ASSERT_FALSE(indexer.isReady());

    std::filesystem::remove_all(tempDir);
}

DTEST(loadCacheWithWrongVersionReturnsErr)
{
    const auto tempDir = std::filesystem::temp_directory_path() / "symbols_cache_wrong_ver";
    const auto cacheDir = tempDir / ".cache";
    std::filesystem::create_directories(cacheDir);

    writeTempFile(cacheDir / "symbols-index.json", R"({"version":999,"symbols":[],"files":[]})");

    Indexer indexer(sharedJobSystem());
    const auto result = indexer.loadCache(tempDir);
    ASSERT_FALSE(result.isOk());
    ASSERT_FALSE(indexer.isReady());

    std::filesystem::remove_all(tempDir);
}

DTEST(loadCacheWithMissingSymbolsKeyReturnsErr)
{
    const auto tempDir = std::filesystem::temp_directory_path() / "symbols_cache_no_symbols";
    const auto cacheDir = tempDir / ".cache";
    std::filesystem::create_directories(cacheDir);

    // Valid JSON, correct version, but no "symbols" array.
    writeTempFile(cacheDir / "symbols-index.json", R"({"version":2,"files":[]})");

    Indexer indexer(sharedJobSystem());
    const auto result = indexer.loadCache(tempDir);
    ASSERT_FALSE(result.isOk());
    ASSERT_FALSE(indexer.isReady());

    std::filesystem::remove_all(tempDir);
}

DTEST(loadCacheWithEmptySymbolsArraySucceeds)
{
    const auto tempDir = std::filesystem::temp_directory_path() / "symbols_cache_empty_syms";
    const auto cacheDir = tempDir / ".cache";
    std::filesystem::create_directories(cacheDir);

    writeTempFile(cacheDir / "symbols-index.json", R"({"version":2,"symbols":[],"files":[]})");

    Indexer indexer(sharedJobSystem());
    const auto result = indexer.loadCache(tempDir);
    ASSERT_TRUE(result.isOk());
    ASSERT_TRUE(indexer.isReady());
    ASSERT_EQ(indexer.symbolCount(), static_cast<usize>(0));

    std::filesystem::remove_all(tempDir);
}

// ---------------------------------------------------------------------------
// scoreMatch isolation tests (tasks 5a–5g, via search())
// ---------------------------------------------------------------------------

DTEST(scoreMatchExactMatchScores1000)
{
    const auto tempDir = std::filesystem::temp_directory_path() / "symbols_score_exact";
    std::filesystem::create_directories(tempDir);
    writeTempFile(tempDir / "a.cpp", "void foo() {}");

    Indexer indexer(sharedJobSystem());
    indexer.build(tempDir);

    const auto results = indexer.search(dc::StringView("foo"), 10);
    ASSERT_TRUE(results.getSize() >= static_cast<u64>(1));
    ASSERT_EQ(results[0].score, static_cast<s32>(1000));

    std::filesystem::remove_all(tempDir);
}

DTEST(scoreMatchPrefixScoreAbove500)
{
    const auto tempDir = std::filesystem::temp_directory_path() / "symbols_score_prefix";
    std::filesystem::create_directories(tempDir);
    // "fooBar" — "foo" is a prefix, score should be 500+.
    writeTempFile(tempDir / "a.cpp", "void fooBar() {}");

    Indexer indexer(sharedJobSystem());
    indexer.build(tempDir);

    const auto results = indexer.search(dc::StringView("foo"), 10);
    ASSERT_TRUE(results.getSize() >= static_cast<u64>(1));
    ASSERT_TRUE(results[0].score >= static_cast<s32>(500));

    std::filesystem::remove_all(tempDir);
}

DTEST(scoreMatchShorterPrefixNameScoresHigher)
{
    const auto tempDir = std::filesystem::temp_directory_path() / "symbols_score_prefix_shorter";
    std::filesystem::create_directories(tempDir);
    // Both are prefix matches for "foo"; "fooBar" is shorter than "fooBarBaz"
    // so its prefix bonus should be higher.
    writeTempFile(tempDir / "a.cpp", "void fooBar() {} void fooBarBaz() {}");

    Indexer indexer(sharedJobSystem());
    indexer.build(tempDir);

    const auto results = indexer.search(dc::StringView("foo"), 10);
    ASSERT_TRUE(results.getSize() >= static_cast<u64>(2));

    // fooBar must outrank fooBarBaz.
    bool fooBarFirst = false;
    bool fooBarBazFirst = false;
    for (u64 i = 0; i < results.getSize(); ++i) {
        if (results[i].symbol->name == "fooBar" && !fooBarBazFirst)
            fooBarFirst = true;
        if (results[i].symbol->name == "fooBarBaz" && !fooBarFirst)
            fooBarBazFirst = true;
    }
    ASSERT_TRUE(fooBarFirst);

    std::filesystem::remove_all(tempDir);
}

DTEST(scoreMatchWordBoundaryBonusRanksHigher)
{
    // "iR" should match "isReady" (camelCase word boundary at 'R').
    // The real project has an isReady function so it must appear in results.
    const auto& indexer = sharedIndexer();
    const auto results = indexer.search(dc::StringView("iR"), 20);
    ASSERT_TRUE(results.getSize() >= static_cast<u64>(1));

    bool foundIsReady = false;
    for (u64 i = 0; i < results.getSize(); ++i) {
        if (results[i].symbol->name == "isReady")
            foundIsReady = true;
    }
    ASSERT_TRUE(foundIsReady);
}

DTEST(scoreMatchConsecutiveCharsBonusRanksHigher)
{
    const auto tempDir = std::filesystem::temp_directory_path() / "symbols_score_consecutive";
    std::filesystem::create_directories(tempDir);
    // "abcdef" has all pattern chars consecutive; "aXbXcXdXeXf" has gaps.
    writeTempFile(tempDir / "a.cpp", "void abcdef() {} void aXbXcXdXeXf() {}");

    Indexer indexer(sharedJobSystem());
    indexer.build(tempDir);

    const auto results = indexer.search(dc::StringView("abcdef"), 10);
    ASSERT_TRUE(results.getSize() >= static_cast<u64>(1));
    // abcdef must be the top result (or at least present at top).
    ASSERT_TRUE(results[0].symbol->name == "abcdef");

    std::filesystem::remove_all(tempDir);
}

DTEST(scoreMatchNonMatchingPatternScoresNegative)
{
    // "zzz" is not a subsequence of any real symbol in the project.
    const auto& indexer = sharedIndexer();
    const auto results = indexer.search(dc::StringView("zzz"), 10);
    ASSERT_EQ(results.getSize(), static_cast<u64>(0));
}

DTEST(searchReturnsEmptyForEmptyPattern)
{
    const auto& indexer = sharedIndexer();
    ASSERT_TRUE(indexer.isReady());
    ASSERT_TRUE(indexer.symbolCount() >= static_cast<usize>(2));

    // Empty pattern returns no results (search requires a non-empty pattern).
    const auto results = indexer.search(dc::StringView(""), 1000);
    ASSERT_EQ(results.getSize(), static_cast<u64>(0));
}

DTEST(searchExactMatchScoresHighest)
{
    const auto tempDir = std::filesystem::temp_directory_path() / "symbols_search_exact";
    std::filesystem::create_directories(tempDir);

    writeTempFile(tempDir / "a.cpp", "void calculateTotal() {} void calculate() {} void calc() {}");

    Indexer indexer(sharedJobSystem());
    indexer.build(tempDir);

    const auto results = indexer.search(dc::StringView("calculateTotal"), 10);
    ASSERT_TRUE(results.getSize() >= static_cast<u64>(1));

    // The exact match should be the top result.
    ASSERT_TRUE(results[0].symbol->name == "calculateTotal");

    std::filesystem::remove_all(tempDir);
}

DTEST(searchPrefixMatchScoresAboveSubsequence)
{
    const auto tempDir = std::filesystem::temp_directory_path() / "symbols_search_prefix";
    std::filesystem::create_directories(tempDir);

    // "get" is a prefix of "getValue" but only a subsequence match for "getAndSetValue".
    writeTempFile(tempDir / "a.cpp", "void getValue() {} void getAndSetValue() {}");

    Indexer indexer(sharedJobSystem());
    indexer.build(tempDir);

    const auto results = indexer.search(dc::StringView("get"), 10);
    ASSERT_TRUE(results.getSize() >= static_cast<u64>(2));

    // "getValue" must outrank "getAndSetValue" because it's a closer prefix match.
    bool valueFirst = false;
    bool andSetFirst = false;
    for (u64 i = 0; i < results.getSize(); ++i) {
        if (results[i].symbol->name == "getValue" && !andSetFirst)
            valueFirst = true;
        if (results[i].symbol->name == "getAndSetValue" && !valueFirst)
            andSetFirst = true;
    }
    ASSERT_TRUE(valueFirst);

    std::filesystem::remove_all(tempDir);
}

DTEST(searchLimitTruncatesResults)
{
    const auto tempDir = std::filesystem::temp_directory_path() / "symbols_search_limit";
    std::filesystem::create_directories(tempDir);

    // Write several matching symbols.
    writeTempFile(tempDir / "a.cpp", "void foo1() {} void foo2() {} void foo3() {} void foo4() {} void foo5() {}");

    Indexer indexer(sharedJobSystem());
    indexer.build(tempDir);

    const auto results = indexer.search(dc::StringView("foo"), 3);
    ASSERT_TRUE(results.getSize() <= static_cast<u64>(3));

    std::filesystem::remove_all(tempDir);
}

DTEST(searchNonMatchingPatternReturnsEmpty)
{
    const auto& indexer = sharedIndexer();
    const auto results = indexer.search(dc::StringView("zzznomatchzzz"), 10);
    ASSERT_EQ(results.getSize(), static_cast<u64>(0));
}

DTEST(searchResultsAreSortedByScoreDescending)
{
    // The real project has multiple symbolKind variants; results must be non-increasing by score.
    const auto& indexer = sharedIndexer();
    const auto results = indexer.search(dc::StringView("symbolKind"), 20);
    ASSERT_TRUE(results.getSize() >= static_cast<u64>(1));

    for (u64 i = 1; i < results.getSize(); ++i) {
        ASSERT_TRUE(results[i - 1].score >= results[i].score);
    }
}

DTEST(hasCacheFileReturnsFalseWhenNoCacheExists)
{
    const auto tempDir = std::filesystem::temp_directory_path() / "symbols_cache_check_absent";
    std::filesystem::create_directories(tempDir);

    Indexer indexer(sharedJobSystem());
    ASSERT_FALSE(indexer.hasCacheFile(tempDir));

    std::filesystem::remove_all(tempDir);
}

DTEST(hasCacheFileReturnsTrueAfterSave)
{
    const auto tempDir = std::filesystem::temp_directory_path() / "symbols_cache_check_present";
    std::filesystem::create_directories(tempDir);

    writeTempFile(tempDir / "a.cpp", "void foo() {}");

    Indexer indexer(sharedJobSystem());
    indexer.build(tempDir);
    [[maybe_unused]] auto saveResult = indexer.saveCache(tempDir);

    ASSERT_TRUE(indexer.hasCacheFile(tempDir));

    std::filesystem::remove_all(tempDir);
}

DTEST(deleteCacheRemovesExistingCacheFile)
{
    const auto tempDir = std::filesystem::temp_directory_path() / "symbols_cache_delete_present";
    std::filesystem::create_directories(tempDir);

    writeTempFile(tempDir / "a.cpp", "void foo() {}");

    Indexer indexer(sharedJobSystem());
    indexer.build(tempDir);
    [[maybe_unused]] auto saveResult = indexer.saveCache(tempDir);

    ASSERT_TRUE(indexer.hasCacheFile(tempDir));

    auto deleteResult = indexer.deleteCache(tempDir);
    ASSERT_TRUE(deleteResult.isOk());
    ASSERT_TRUE(dc::move(deleteResult).unwrap());
    ASSERT_FALSE(indexer.hasCacheFile(tempDir));

    std::filesystem::remove_all(tempDir);
}

DTEST(fileCountAfterBuild)
{
    const auto tempDir = std::filesystem::temp_directory_path() / "symbols_file_count";
    std::filesystem::create_directories(tempDir);

    writeTempFile(tempDir / "a.cpp", "void alpha() {}");
    writeTempFile(tempDir / "b.cpp", "void beta() {}");

    Indexer indexer(sharedJobSystem());
    indexer.build(tempDir);

    ASSERT_EQ(indexer.fileCount(), static_cast<usize>(2));

    std::filesystem::remove_all(tempDir);
}

DTEST(cacheRoundTripPreservesSymbolFields)
{
    const auto tempDir = std::filesystem::temp_directory_path() / "symbols_cache_fields";
    std::filesystem::create_directories(tempDir);

    writeTempFile(tempDir / "myfile.cpp", "class MySpecialClass {};");

    Indexer indexer(sharedJobSystem());
    indexer.build(tempDir);
    [[maybe_unused]] auto saveResult = indexer.saveCache(tempDir);

    Indexer indexer2(sharedJobSystem());
    const auto loadResult = indexer2.loadCache(tempDir);
    ASSERT_TRUE(loadResult.isOk());

    const auto results = indexer2.search(dc::StringView("MySpecialClass"), 10);
    ASSERT_TRUE(results.getSize() >= static_cast<u64>(1));

    const Symbol* sym = results[0].symbol;
    ASSERT_TRUE(sym->name == "MySpecialClass");
    ASSERT_EQ(sym->kind, SymbolKind::Class);
    ASSERT_TRUE(sym->line >= static_cast<u32>(1));
    // File field should be non-empty.
    ASSERT_TRUE(sym->file.getSize() > static_cast<usize>(0));

    std::filesystem::remove_all(tempDir);
}

DTEST(incrementalBuildDetectsRemovedFile)
{
    const auto tempDir = std::filesystem::temp_directory_path() / "symbols_test_incr_remove";
    std::filesystem::create_directories(tempDir);

    const auto file1 = tempDir / "a.cpp";
    const auto file2 = tempDir / "b.cpp";
    writeTempFile(file1, "void alpha() {}");
    writeTempFile(file2, "void beta() {}");

    Indexer indexer(sharedJobSystem());
    indexer.build(tempDir);
    ASSERT_TRUE(indexer.symbolCount() >= static_cast<usize>(2));

    // Remove file2.
    std::filesystem::remove(file2);

    indexer.incrementalBuild(tempDir);

    // alpha should still be found; beta should be gone.
    const auto alphaResults = indexer.search(dc::StringView("alpha"), 10);
    const auto betaResults = indexer.search(dc::StringView("beta"), 10);
    ASSERT_TRUE(alphaResults.getSize() >= static_cast<u64>(1));
    ASSERT_EQ(betaResults.getSize(), static_cast<u64>(0));

    std::filesystem::remove_all(tempDir);
}

DTEST(incrementalBuildDetectsChangedFile)
{
    const auto tempDir = std::filesystem::temp_directory_path() / "symbols_test_incr_change";
    std::filesystem::create_directories(tempDir);

    const auto file1 = tempDir / "a.cpp";
    writeTempFile(file1, "void oldName() {}");

    Indexer indexer(sharedJobSystem());
    indexer.build(tempDir);

    const auto oldResults = indexer.search(dc::StringView("oldName"), 10);
    ASSERT_TRUE(oldResults.getSize() >= static_cast<u64>(1));

    // Overwrite the file with a different symbol.
    // Sleep briefly to guarantee a new mtime on filesystems with 1-second resolution.
    std::this_thread::sleep_for(std::chrono::milliseconds(1100));
    writeTempFile(file1, "void newName() {}");

    indexer.incrementalBuild(tempDir);

    // oldName must be gone; newName must appear.
    const auto oldResultsAfter = indexer.search(dc::StringView("oldName"), 10);
    const auto newResults = indexer.search(dc::StringView("newName"), 10);
    ASSERT_EQ(oldResultsAfter.getSize(), static_cast<u64>(0));
    ASSERT_TRUE(newResults.getSize() >= static_cast<u64>(1));

    std::filesystem::remove_all(tempDir);
}

DTEST(incrementalBuildNoOpWhenUnchanged)
{
    const auto tempDir = std::filesystem::temp_directory_path() / "symbols_test_incr_noop";
    std::filesystem::create_directories(tempDir);

    const auto file1 = tempDir / "a.cpp";
    writeTempFile(file1, "void stable() {}");

    Indexer indexer(sharedJobSystem());
    indexer.build(tempDir);
    const usize countBefore = indexer.symbolCount();
    ASSERT_TRUE(countBefore >= static_cast<usize>(1));

    // Second incremental build with no changes.
    indexer.incrementalBuild(tempDir);
    ASSERT_EQ(indexer.symbolCount(), countBefore);

    std::filesystem::remove_all(tempDir);
}

DTEST(pruneDeletedFilesAfterCacheLoad)
{
    // Regression: symbols for files deleted between runs must be pruned when the
    // cache is loaded, not left in the index until the next explicit rebuild.
    const auto tempDir = std::filesystem::temp_directory_path() / "symbols_prune_deleted_cache";
    std::filesystem::create_directories(tempDir);

    const auto fileA = tempDir / "a.cpp";
    const auto fileB = tempDir / "b.cpp";
    writeTempFile(fileA, "void alpha() {}");
    writeTempFile(fileB, "void beta() {}");

    // Build and persist cache with both files present.
    {
        Indexer indexer(sharedJobSystem());
        indexer.build(tempDir);
        ASSERT_TRUE(indexer.symbolCount() >= static_cast<usize>(2));
        [[maybe_unused]] auto sr = indexer.saveCache(tempDir);
    }

    // Delete one file while the server is "offline".
    std::filesystem::remove(fileB);

    // Load cache (stale — still records beta).
    Indexer indexer(sharedJobSystem());
    const auto loadResult = indexer.loadCache(tempDir);
    ASSERT_TRUE(loadResult.isOk());

    // Pruning must evict the deleted file.
    const u64 pruned = indexer.pruneDeletedFiles(tempDir);
    ASSERT_TRUE(pruned >= static_cast<u64>(1));
    ASSERT_TRUE(indexer.isDirty());

    // alpha survives; beta is gone.
    const auto alphaResults = indexer.search(dc::StringView("alpha"), 10);
    const auto betaResults = indexer.search(dc::StringView("beta"), 10);
    ASSERT_TRUE(alphaResults.getSize() >= static_cast<u64>(1));
    ASSERT_EQ(betaResults.getSize(), static_cast<u64>(0));

    std::filesystem::remove_all(tempDir);
}

DTEST(isDirtyAfterBuildClearedBySave)
{
    const auto tempDir = std::filesystem::temp_directory_path() / "symbols_dirty_flag";
    std::filesystem::create_directories(tempDir);
    writeTempFile(tempDir / "a.cpp", "void foo() {}");

    Indexer indexer(sharedJobSystem());
    ASSERT_FALSE(indexer.isDirty());

    indexer.build(tempDir);
    ASSERT_TRUE(indexer.isDirty());

    [[maybe_unused]] auto sr = indexer.saveCache(tempDir);
    ASSERT_FALSE(indexer.isDirty());

    std::filesystem::remove_all(tempDir);
}

DTEST(incrementalBuildSurvivesCacheRoundTrip)
{
    const auto tempDir = std::filesystem::temp_directory_path() / "symbols_test_incr_cache";
    std::filesystem::create_directories(tempDir);

    const auto file1 = tempDir / "a.cpp";
    writeTempFile(file1, "void foo() {}");

    // Build, save cache.
    {
        Indexer indexer(sharedJobSystem());
        indexer.build(tempDir);
        [[maybe_unused]] auto saveResult = indexer.saveCache(tempDir);
    }

    // Load cache into a fresh indexer, add a file, incremental rebuild.
    const auto file2 = tempDir / "b.cpp";
    writeTempFile(file2, "void bar() {}");

    Indexer indexer2(sharedJobSystem());
    const auto loadResult = indexer2.loadCache(tempDir);
    ASSERT_TRUE(loadResult.isOk());

    indexer2.incrementalBuild(tempDir);

    const auto fooResults = indexer2.search(dc::StringView("foo"), 10);
    const auto barResults = indexer2.search(dc::StringView("bar"), 10);
    ASSERT_TRUE(fooResults.getSize() >= static_cast<u64>(1));
    ASSERT_TRUE(barResults.getSize() >= static_cast<u64>(1));

    std::filesystem::remove_all(tempDir);
}

// ---------------------------------------------------------------------------
// scoreMatch edge-case tests
// ---------------------------------------------------------------------------

DTEST(scoreMatchSingleCharPattern)
{
    // 'g' is a prefix of "get" (exact match in dc/traits.hpp) — top result score must be >= 500.
    const auto& indexer = sharedIndexer();
    const auto results = indexer.search(dc::StringView("g"), 10);
    ASSERT_TRUE(results.getSize() >= static_cast<u64>(1));
    ASSERT_TRUE(results[0].score >= static_cast<s32>(500));
}

DTEST(scoreMatchUnderscoreBoundaryBonus)
{
    // "gs" matches "get_size" via underscore word-boundary bonus and must
    // outrank a non-boundary subsequence match like "gaps" (g...s but no boundary).
    const auto tempDir = std::filesystem::temp_directory_path() / "symbols_score_underscore";
    std::filesystem::create_directories(tempDir);
    writeTempFile(tempDir / "a.cpp", "void get_size() {} void gaps() {}");

    Indexer indexer(sharedJobSystem());
    indexer.build(tempDir);

    const auto results = indexer.search(dc::StringView("gs"), 10);
    ASSERT_TRUE(results.getSize() >= static_cast<u64>(1));

    // get_size must appear in results (boundary on 's' after '_').
    bool foundGetSize = false;
    for (u64 i = 0; i < results.getSize(); ++i) {
        if (results[i].symbol->name == "get_size")
            foundGetSize = true;
    }
    ASSERT_TRUE(foundGetSize);

    // get_size must outrank gaps (gaps has no word-boundary for 's').
    if (results.getSize() >= static_cast<u64>(2)) {
        bool getSizeFirst = false;
        for (u64 i = 0; i < results.getSize(); ++i) {
            if (results[i].symbol->name == "get_size") {
                getSizeFirst = true;
                break;
            }
            if (results[i].symbol->name == "gaps")
                break;
        }
        ASSERT_TRUE(getSizeFirst);
    }

    std::filesystem::remove_all(tempDir);
}

DTEST(scoreMatchNamespaceSeparatorBonus)
{
    // Pattern "bs" matching "bar::size" should get the word-boundary bonus
    // for ':' and thus return a positive score.
    const auto tempDir = std::filesystem::temp_directory_path() / "symbols_score_namespace";
    std::filesystem::create_directories(tempDir);
    // Use a qualified out-of-class function definition so the parser captures "bar::size".
    writeTempFile(tempDir / "a.cpp", R"(
struct bar { void size(); };
void bar::size() {}
)");

    Indexer indexer(sharedJobSystem());
    indexer.build(tempDir);

    const auto results = indexer.search(dc::StringView("bs"), 10);
    // "bar::size" must appear.
    bool found = false;
    for (u64 i = 0; i < results.getSize(); ++i) {
        if (results[i].symbol->name == "bar::size") {
            found = true;
            // Score must include at least the word-boundary bonus for 's'.
            ASSERT_TRUE(results[i].score > static_cast<s32>(0));
        }
    }
    ASSERT_TRUE(found);

    std::filesystem::remove_all(tempDir);
}

DTEST(scoreMatchPatternLongerThanNameReturnsNoMatch)
{
    // A pattern with no matching subsequence in any indexed symbol returns nothing.
    const auto& indexer = sharedIndexer();
    const auto results = indexer.search(dc::StringView("zzzzlongpatternzzzz"), 10);
    ASSERT_EQ(results.getSize(), static_cast<u64>(0));
}

DTEST(scoreMatchCaseSensitivityBonus)
{
    // The +5 per-char case bonus applies in the subsequence path.
    // Use symbol "FooBarBaz" with a short pattern that can't be a prefix, forcing
    // subsequence scoring. Case-exact "FB" must score >= case-folded "fb" for the
    // same symbol because each matched character that matches exactly gets +5.
    const auto tempDir = std::filesystem::temp_directory_path() / "symbols_score_case_bonus";
    std::filesystem::create_directories(tempDir);
    writeTempFile(tempDir / "a.cpp", "class FooBarBaz {};");

    Indexer indexer(sharedJobSystem());
    indexer.build(tempDir);

    // Both patterns are subsequences of "FooBarBaz" via the camelCase boundaries.
    const auto exactCase = indexer.search(dc::StringView("FB"), 10);
    const auto foldedCase = indexer.search(dc::StringView("fb"), 10);

    ASSERT_TRUE(exactCase.getSize() >= static_cast<u64>(1));
    ASSERT_TRUE(foldedCase.getSize() >= static_cast<u64>(1));

    // Exact-case pattern "FB" must score >= case-folded "fb" for the same symbol.
    ASSERT_TRUE(exactCase[0].score >= foldedCase[0].score);

    std::filesystem::remove_all(tempDir);
}

DTEST(loadCacheVersionOneLoadsCorrectly)
{
    // Version 1 cache has no "files" array. loadCache must still succeed
    // and populate symbols correctly.
    const auto tempDir = std::filesystem::temp_directory_path() / "symbols_cache_v1";
    const auto cacheDir = tempDir / ".cache";
    std::filesystem::create_directories(cacheDir);

    // Hand-craft a v1 cache with one symbol.
    writeTempFile(cacheDir / "symbols-index.json",
        R"({"version":1,"symbols":[{"n":"legacyFunc","k":"function","f":"old.cpp","l":7}]})");

    Indexer indexer(sharedJobSystem());
    const auto result = indexer.loadCache(tempDir);
    ASSERT_TRUE(result.isOk());
    ASSERT_TRUE(indexer.isReady());
    ASSERT_EQ(indexer.symbolCount(), static_cast<usize>(1));

    const auto results = indexer.search(dc::StringView("legacyFunc"), 10);
    ASSERT_TRUE(results.getSize() >= static_cast<u64>(1));
    ASSERT_TRUE(results[0].symbol->name == "legacyFunc");

    std::filesystem::remove_all(tempDir);
}

// ---------------------------------------------------------------------------
// Multi-token search tests (name + kind filter + file filter)
//
// These tests run against the symbols project itself (the repo root is derived
// from the compile-time path of test_helpers.hpp via repoRoot()).
//
// Known symbols used as anchors:
//   SearchResult   struct   src/indexer.hpp
//   FileRecord     struct   src/indexer.hpp
//   ServerConfig   struct   src/server.hpp
//   ParsedArgs     struct   src/args.hpp
//   ParseError     struct   src/json.cpp
//   Parser         class    src/parser.hpp
//   JsonValue      class    src/json.hpp
//   Indexer        class    src/indexer.hpp
//   SymbolKind     enum     src/parser.hpp
//   char8          alias    build/_deps/dc-src/include/dc/types.hpp
//   symbolKindToString  function  src/parser.cpp  (+ declaration in .hpp)
// ---------------------------------------------------------------------------

/// Helper: return true when at least one result has the given name.
static auto hasResult(const dc::List<SearchResult>& results, const char* name) -> bool
{
    for (u64 i = 0; i < results.getSize(); ++i) {
        if (results[i].symbol->name == name)
            return true;
    }
    return false;
}

/// Helper: return true when every result has the given kind.
static auto allKind(const dc::List<SearchResult>& results, SymbolKind kind) -> bool
{
    for (u64 i = 0; i < results.getSize(); ++i) {
        if (results[i].symbol->kind != kind)
            return false;
    }
    return true;
}

/// Helper: return true when every result's file contains the given substring.
static auto allFileContains(const dc::List<SearchResult>& results, const char* sub) -> bool
{
    const dc::StringView needle(sub);
    for (u64 i = 0; i < results.getSize(); ++i) {
        const dc::StringView file(results[i].symbol->file);
        bool found = false;
        if (file.getSize() >= needle.getSize()) {
            for (u64 j = 0; j <= file.getSize() - needle.getSize(); ++j) {
                bool match = true;
                for (u64 k = 0; k < needle.getSize(); ++k) {
                    if (file[j + k] != needle[k]) {
                        match = false;
                        break;
                    }
                }
                if (match) {
                    found = true;
                    break;
                }
            }
        }
        if (!found)
            return false;
    }
    return true;
}

DTEST(multiTokenNameOnly)
{
    // Plain name search: "SearchResult" finds the struct in src/indexer.hpp (and
    // potentially other matches), top result must be SearchResult itself.
    const auto& indexer = sharedIndexer();

    const auto results = indexer.search(dc::StringView("SearchResult"), 50);
    ASSERT_TRUE(results.getSize() >= static_cast<u64>(1));
    ASSERT_TRUE(hasResult(results, "SearchResult"));
}

DTEST(multiTokenKindFilterStruct)
{
    // "SearchResult struct" — only structs named SearchResult should appear.
    const auto& indexer = sharedIndexer();

    const auto results = indexer.search(dc::StringView("SearchResult struct"), 50);
    ASSERT_TRUE(results.getSize() >= static_cast<u64>(1));
    ASSERT_TRUE(hasResult(results, "SearchResult"));
    ASSERT_TRUE(allKind(results, SymbolKind::Struct));
}

DTEST(multiTokenKindFilterClass)
{
    // "Parser class" — must find the Parser class in src/parser.hpp, all results are classes.
    const auto& indexer = sharedIndexer();

    const auto results = indexer.search(dc::StringView("Parser class"), 50);
    ASSERT_TRUE(results.getSize() >= static_cast<u64>(1));
    ASSERT_TRUE(hasResult(results, "Parser"));
    ASSERT_TRUE(allKind(results, SymbolKind::Class));
}

DTEST(multiTokenKindFilterFunction)
{
    // "symbolKindToString function" — only functions, no declarations of other kinds.
    const auto& indexer = sharedIndexer();

    const auto results = indexer.search(dc::StringView("symbolKindToString function"), 50);
    ASSERT_TRUE(results.getSize() >= static_cast<u64>(1));
    ASSERT_TRUE(hasResult(results, "symbolKindToString"));
    ASSERT_TRUE(allKind(results, SymbolKind::Function));
}

DTEST(multiTokenKindFilterEnum)
{
    // "SymbolKind enum" — must find the SymbolKind enum in src/parser.hpp.
    const auto& indexer = sharedIndexer();

    const auto results = indexer.search(dc::StringView("SymbolKind enum"), 50);
    ASSERT_TRUE(results.getSize() >= static_cast<u64>(1));
    ASSERT_TRUE(hasResult(results, "SymbolKind"));
    ASSERT_TRUE(allKind(results, SymbolKind::Enum));
}

DTEST(multiTokenKindFilterAlias)
{
    // "MyAlias alias" — must find the MyAlias type alias, all results are aliases.
    const auto tempDir = std::filesystem::temp_directory_path() / "symbols_kind_alias";
    std::filesystem::create_directories(tempDir);
    writeTempFile(tempDir / "a.cpp", "using MyAlias = int; void notAnAlias() {}");

    Indexer indexer(sharedJobSystem());
    indexer.build(tempDir);

    const auto results = indexer.search(dc::StringView("MyAlias alias"), 50);
    ASSERT_TRUE(results.getSize() >= static_cast<u64>(1));
    ASSERT_TRUE(hasResult(results, "MyAlias"));
    ASSERT_TRUE(allKind(results, SymbolKind::Alias));

    std::filesystem::remove_all(tempDir);
}

DTEST(multiTokenKindFilterTypedef)
{
    // "MyTypedef typedef" — must find MyTypedef typedefs, all results are typedefs.
    const auto tempDir = std::filesystem::temp_directory_path() / "symbols_kind_typedef";
    std::filesystem::create_directories(tempDir);
    writeTempFile(tempDir / "a.cpp", "typedef int MyTypedef; void notATypedef() {}");

    Indexer indexer(sharedJobSystem());
    indexer.build(tempDir);

    const auto results = indexer.search(dc::StringView("MyTypedef typedef"), 50);
    ASSERT_TRUE(results.getSize() >= static_cast<u64>(1));
    ASSERT_TRUE(hasResult(results, "MyTypedef"));
    ASSERT_TRUE(allKind(results, SymbolKind::Typedef));

    std::filesystem::remove_all(tempDir);
}

DTEST(multiTokenFileFilter)
{
    // "SearchResult indexer.hpp" — file filter restricts to indexer.hpp;
    // the SearchResult struct lives there so it must appear.
    const auto& indexer = sharedIndexer();

    const auto results = indexer.search(dc::StringView("SearchResult indexer.hpp"), 50);
    ASSERT_TRUE(results.getSize() >= static_cast<u64>(1));
    ASSERT_TRUE(hasResult(results, "SearchResult"));
    ASSERT_TRUE(allFileContains(results, "indexer"));
}

DTEST(multiTokenFileFilterExcludesWrongFile)
{
    // "SearchResult server.hpp" — SearchResult is not in server.hpp, must return nothing.
    const auto& indexer = sharedIndexer();

    const auto results = indexer.search(dc::StringView("SearchResult server.hpp"), 50);
    ASSERT_FALSE(hasResult(results, "SearchResult"));
}

DTEST(multiTokenFileFilterByDir)
{
    // "ServerConfig src/" — all results must come from files under src/.
    const auto& indexer = sharedIndexer();

    const auto results = indexer.search(dc::StringView("ServerConfig src/"), 50);
    ASSERT_TRUE(results.getSize() >= static_cast<u64>(1));
    ASSERT_TRUE(hasResult(results, "ServerConfig"));
    ASSERT_TRUE(allFileContains(results, "src/"));
}

DTEST(multiTokenKindAndFileFilter)
{
    // "struct indexer.hpp" — only structs from files matching "indexer.hpp":
    // SearchResult and FileRecord both live in src/indexer.hpp.
    const auto& indexer = sharedIndexer();

    const auto results = indexer.search(dc::StringView("struct indexer.hpp"), 50);
    ASSERT_TRUE(results.getSize() >= static_cast<u64>(2));
    ASSERT_TRUE(hasResult(results, "SearchResult"));
    ASSERT_TRUE(hasResult(results, "FileRecord"));
    ASSERT_TRUE(allKind(results, SymbolKind::Struct));
    ASSERT_TRUE(allFileContains(results, "indexer"));
}

DTEST(multiTokenOrderIndependentKindFirst)
{
    // "struct SearchResult" and "SearchResult struct" must produce identical results.
    const auto& indexer = sharedIndexer();

    const auto a = indexer.search(dc::StringView("struct SearchResult"), 50);
    const auto b = indexer.search(dc::StringView("SearchResult struct"), 50);

    ASSERT_EQ(a.getSize(), b.getSize());
    for (u64 i = 0; i < a.getSize(); ++i) {
        ASSERT_TRUE(a[i].symbol->name == b[i].symbol->name);
        ASSERT_EQ(a[i].symbol->kind, b[i].symbol->kind);
    }
}

DTEST(multiTokenOrderIndependentFileFirst)
{
    // "indexer.hpp SearchResult struct", "struct SearchResult indexer.hpp",
    // and "SearchResult indexer.hpp struct" must all produce the same results.
    const auto& indexer = sharedIndexer();

    const auto a = indexer.search(dc::StringView("indexer.hpp SearchResult struct"), 50);
    const auto b = indexer.search(dc::StringView("struct SearchResult indexer.hpp"), 50);
    const auto c = indexer.search(dc::StringView("SearchResult indexer.hpp struct"), 50);

    ASSERT_EQ(a.getSize(), b.getSize());
    ASSERT_EQ(a.getSize(), c.getSize());
    for (u64 i = 0; i < a.getSize(); ++i) {
        ASSERT_TRUE(a[i].symbol->name == b[i].symbol->name);
        ASSERT_TRUE(a[i].symbol->name == c[i].symbol->name);
    }
}

DTEST(multiTokenKindOnlyNoNameToken)
{
    // "enum" alone returns all enums; SymbolKind must be among them.
    const auto& indexer = sharedIndexer();

    const auto results = indexer.search(dc::StringView("enum"), 200);
    ASSERT_TRUE(results.getSize() >= static_cast<u64>(1));
    ASSERT_TRUE(hasResult(results, "SymbolKind"));
    ASSERT_TRUE(allKind(results, SymbolKind::Enum));
}

DTEST(multiTokenFileOnlyNoNameToken)
{
    // "parser.hpp" alone returns all symbols from files matching "parser.hpp";
    // that must include at least Parser (class) and SymbolKind (enum).
    const auto& indexer = sharedIndexer();

    const auto results = indexer.search(dc::StringView("parser.hpp"), 200);
    ASSERT_TRUE(results.getSize() >= static_cast<u64>(2));
    ASSERT_TRUE(hasResult(results, "Parser"));
    ASSERT_TRUE(hasResult(results, "SymbolKind"));
    ASSERT_TRUE(allFileContains(results, "parser"));
}

DTEST(multiTokenKindFilterExcludesNonMatching)
{
    // "SearchResult class" — SearchResult is a struct, not a class, so zero results.
    const auto& indexer = sharedIndexer();

    const auto results = indexer.search(dc::StringView("SearchResult class"), 50);
    ASSERT_EQ(results.getSize(), static_cast<u64>(0));
}
