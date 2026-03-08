#include <dc/dtest.hpp>
#include <dc/file.hpp>
#include <indexer.hpp>

#include <chrono>
#include <filesystem>
#include <thread>

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

DTEST(indexerSearchExactMatch)
{
    Indexer indexer;

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
// Incremental rebuild tests
// ---------------------------------------------------------------------------

DTEST(incrementalBuildFallsBackToFullBuildWhenNoRecords)
{
    // A fresh Indexer with no file records should fall back to full build.
    const auto tempDir = std::filesystem::temp_directory_path() / "symbols_test_incr_fallback";
    std::filesystem::create_directories(tempDir);

    const auto file1 = tempDir / "a.cpp";
    writeTempFile(file1, "void alpha() {}");

    Indexer indexer;
    ASSERT_FALSE(indexer.isReady());

    // incrementalBuild on an empty indexer falls back to full build.
    indexer.incrementalBuild(tempDir);
    ASSERT_TRUE(indexer.isReady());
    ASSERT_TRUE(indexer.symbolCount() >= static_cast<usize>(1));

    std::filesystem::remove_all(tempDir);
}

// ---------------------------------------------------------------------------
// search() tests
// ---------------------------------------------------------------------------

DTEST(searchReturnsEmptyWhenNotReady)
{
    Indexer indexer;
    ASSERT_FALSE(indexer.isReady());
    const auto results = indexer.search(dc::StringView("anything"), 10);
    ASSERT_EQ(results.getSize(), static_cast<u64>(0));
}

DTEST(searchReturnsEmptyForEmptyPattern)
{
    const auto tempDir = std::filesystem::temp_directory_path() / "symbols_search_empty_pattern";
    std::filesystem::create_directories(tempDir);

    writeTempFile(tempDir / "a.cpp", "void alpha() {} void beta() {}");

    Indexer indexer;
    indexer.build(tempDir);
    ASSERT_TRUE(indexer.isReady());
    ASSERT_TRUE(indexer.symbolCount() >= static_cast<usize>(2));

    // Empty pattern returns no results (search requires a non-empty pattern).
    const auto results = indexer.search(dc::StringView(""), 1000);
    ASSERT_EQ(results.getSize(), static_cast<u64>(0));

    std::filesystem::remove_all(tempDir);
}

DTEST(searchExactMatchScoresHighest)
{
    const auto tempDir = std::filesystem::temp_directory_path() / "symbols_search_exact";
    std::filesystem::create_directories(tempDir);

    writeTempFile(tempDir / "a.cpp", "void calculateTotal() {} void calculate() {} void calc() {}");

    Indexer indexer;
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

    Indexer indexer;
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

    Indexer indexer;
    indexer.build(tempDir);

    const auto results = indexer.search(dc::StringView("foo"), 3);
    ASSERT_TRUE(results.getSize() <= static_cast<u64>(3));

    std::filesystem::remove_all(tempDir);
}

DTEST(searchNonMatchingPatternReturnsEmpty)
{
    const auto tempDir = std::filesystem::temp_directory_path() / "symbols_search_nomatch";
    std::filesystem::create_directories(tempDir);

    writeTempFile(tempDir / "a.cpp", "void alpha() {}");

    Indexer indexer;
    indexer.build(tempDir);

    const auto results = indexer.search(dc::StringView("zzznomatchzzz"), 10);
    ASSERT_EQ(results.getSize(), static_cast<u64>(0));

    std::filesystem::remove_all(tempDir);
}

DTEST(searchResultsAreSortedByScoreDescending)
{
    const auto tempDir = std::filesystem::temp_directory_path() / "symbols_search_sorted";
    std::filesystem::create_directories(tempDir);

    writeTempFile(tempDir / "a.cpp", "void myFunc() {} void myFuncHelper() {} void myFuncHelperExtended() {}");

    Indexer indexer;
    indexer.build(tempDir);

    const auto results = indexer.search(dc::StringView("myFunc"), 10);
    ASSERT_TRUE(results.getSize() >= static_cast<u64>(1));

    // Verify scores are non-increasing.
    for (u64 i = 1; i < results.getSize(); ++i) {
        ASSERT_TRUE(results[i - 1].score >= results[i].score);
    }

    std::filesystem::remove_all(tempDir);
}

DTEST(hasCacheFileReturnsFalseWhenNoCacheExists)
{
    const auto tempDir = std::filesystem::temp_directory_path() / "symbols_cache_check_absent";
    std::filesystem::create_directories(tempDir);

    Indexer indexer;
    ASSERT_FALSE(indexer.hasCacheFile(tempDir));

    std::filesystem::remove_all(tempDir);
}

DTEST(hasCacheFileReturnsTrueAfterSave)
{
    const auto tempDir = std::filesystem::temp_directory_path() / "symbols_cache_check_present";
    std::filesystem::create_directories(tempDir);

    writeTempFile(tempDir / "a.cpp", "void foo() {}");

    Indexer indexer;
    indexer.build(tempDir);
    [[maybe_unused]] auto saveResult = indexer.saveCache(tempDir);

    ASSERT_TRUE(indexer.hasCacheFile(tempDir));

    std::filesystem::remove_all(tempDir);
}

DTEST(fileCountAfterBuild)
{
    const auto tempDir = std::filesystem::temp_directory_path() / "symbols_file_count";
    std::filesystem::create_directories(tempDir);

    writeTempFile(tempDir / "a.cpp", "void alpha() {}");
    writeTempFile(tempDir / "b.cpp", "void beta() {}");

    Indexer indexer;
    indexer.build(tempDir);

    ASSERT_EQ(indexer.fileCount(), static_cast<usize>(2));

    std::filesystem::remove_all(tempDir);
}

DTEST(cacheRoundTripPreservesSymbolFields)
{
    const auto tempDir = std::filesystem::temp_directory_path() / "symbols_cache_fields";
    std::filesystem::create_directories(tempDir);

    writeTempFile(tempDir / "myfile.cpp", "class MySpecialClass {};");

    Indexer indexer;
    indexer.build(tempDir);
    [[maybe_unused]] auto saveResult = indexer.saveCache(tempDir);

    Indexer indexer2;
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

    Indexer indexer;
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

    Indexer indexer;
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

    Indexer indexer;
    indexer.build(tempDir);
    const usize countBefore = indexer.symbolCount();
    ASSERT_TRUE(countBefore >= static_cast<usize>(1));

    // Second incremental build with no changes.
    indexer.incrementalBuild(tempDir);
    ASSERT_EQ(indexer.symbolCount(), countBefore);

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
        Indexer indexer;
        indexer.build(tempDir);
        [[maybe_unused]] auto saveResult = indexer.saveCache(tempDir);
    }

    // Load cache into a fresh indexer, add a file, incremental rebuild.
    const auto file2 = tempDir / "b.cpp";
    writeTempFile(file2, "void bar() {}");

    Indexer indexer2;
    const auto loadResult = indexer2.loadCache(tempDir);
    ASSERT_TRUE(loadResult.isOk());

    indexer2.incrementalBuild(tempDir);

    const auto fooResults = indexer2.search(dc::StringView("foo"), 10);
    const auto barResults = indexer2.search(dc::StringView("bar"), 10);
    ASSERT_TRUE(fooResults.getSize() >= static_cast<u64>(1));
    ASSERT_TRUE(barResults.getSize() >= static_cast<u64>(1));

    std::filesystem::remove_all(tempDir);
}
