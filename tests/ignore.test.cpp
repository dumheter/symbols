#include <dc/dtest.hpp>
#include <ignore.hpp>

#include <filesystem>
#include <fstream>

using namespace symbols;

// ---------------------------------------------------------------------------
// IgnoreList::parse — basic cases
// ---------------------------------------------------------------------------

DTEST(parseEmptyTextProducesNoRules)
{
    const auto list = IgnoreList::parse(dc::StringView(""));
    ASSERT_EQ(list.ruleCount(), static_cast<usize>(0));
}

DTEST(parseCommentsAndBlankLinesAreSkipped)
{
    const auto text = dc::StringView("# this is a comment\n"
                                     "\n"
                                     "# another comment\n");
    const auto list = IgnoreList::parse(text);
    ASSERT_EQ(list.ruleCount(), static_cast<usize>(0));
}

DTEST(parseExactPathRule)
{
    const auto list = IgnoreList::parse(dc::StringView("build/foo/parser.c\n"));
    ASSERT_EQ(list.ruleCount(), static_cast<usize>(1));
    ASSERT_TRUE(list.isIgnored(dc::StringView("build/foo/parser.c")));
}

DTEST(parseExactPathDoesNotMatchSubpaths)
{
    const auto list = IgnoreList::parse(dc::StringView("build/foo/parser.c\n"));
    ASSERT_FALSE(list.isIgnored(dc::StringView("build/foo/parser.c.bak")));
    ASSERT_FALSE(list.isIgnored(dc::StringView("build/foo")));
    ASSERT_FALSE(list.isIgnored(dc::StringView("other/parser.c")));
}

DTEST(parseDirGlobRule)
{
    const auto list = IgnoreList::parse(dc::StringView("build/*\n"));
    ASSERT_EQ(list.ruleCount(), static_cast<usize>(1));
    ASSERT_TRUE(list.isIgnored(dc::StringView("build/anything.cpp")));
    ASSERT_TRUE(list.isIgnored(dc::StringView("build/sub/deep/file.h")));
}

DTEST(parseDirGlobDoesNotMatchOtherPrefixes)
{
    const auto list = IgnoreList::parse(dc::StringView("build/*\n"));
    ASSERT_FALSE(list.isIgnored(dc::StringView("src/main.cpp")));
    ASSERT_FALSE(list.isIgnored(dc::StringView("build_release/foo.cpp")));
}

DTEST(parseMultipleRules)
{
    const auto text = dc::StringView("# ignore both build dirs\n"
                                     "build/*\n"
                                     "build_release/*\n"
                                     "# and one specific file\n"
                                     "vendor/huge.c\n");
    const auto list = IgnoreList::parse(text);
    ASSERT_EQ(list.ruleCount(), static_cast<usize>(3));
    ASSERT_TRUE(list.isIgnored(dc::StringView("build/parser.c")));
    ASSERT_TRUE(list.isIgnored(dc::StringView("build_release/parser.c")));
    ASSERT_TRUE(list.isIgnored(dc::StringView("vendor/huge.c")));
    ASSERT_FALSE(list.isIgnored(dc::StringView("src/main.cpp")));
}

// ---------------------------------------------------------------------------
// Path normalisation — backslashes and "./" prefix
// ---------------------------------------------------------------------------

DTEST(backslashPathIsNormalisedToForwardSlash)
{
    const auto list = IgnoreList::parse(dc::StringView("build/foo/parser.c\n"));
    // Windows-style path supplied by the caller should still match.
    ASSERT_TRUE(list.isIgnored(dc::StringView("build\\foo\\parser.c")));
}

DTEST(leadingDotSlashIsStripped)
{
    const auto list = IgnoreList::parse(dc::StringView("build/foo/parser.c\n"));
    ASSERT_TRUE(list.isIgnored(dc::StringView("./build/foo/parser.c")));
}

DTEST(globWithBackslashInput)
{
    const auto list = IgnoreList::parse(dc::StringView("build/*\n"));
    ASSERT_TRUE(list.isIgnored(dc::StringView("build\\sub\\file.cpp")));
}

// ---------------------------------------------------------------------------
// Windows-style CRLF line endings
// ---------------------------------------------------------------------------

DTEST(crlfLineEndingsAreParsedCorrectly)
{
    const auto text = dc::StringView("build/*\r\nvendor/huge.c\r\n");
    const auto list = IgnoreList::parse(text);
    ASSERT_EQ(list.ruleCount(), static_cast<usize>(2));
    ASSERT_TRUE(list.isIgnored(dc::StringView("build/anything.cpp")));
    ASSERT_TRUE(list.isIgnored(dc::StringView("vendor/huge.c")));
}

// ---------------------------------------------------------------------------
// IgnoreList::loadFromDirectory
// ---------------------------------------------------------------------------

DTEST(loadFromDirectoryReturnsEmptyWhenNoFile)
{
    const auto tempDir = std::filesystem::temp_directory_path() / "symbols_ignore_no_file";
    std::filesystem::create_directories(tempDir);
    // Make sure there is no .symbols-ignore here.
    std::filesystem::remove(tempDir / ".symbols-ignore");

    const auto list = IgnoreList::loadFromDirectory(tempDir);
    ASSERT_EQ(list.ruleCount(), static_cast<usize>(0));

    std::filesystem::remove_all(tempDir);
}

DTEST(loadFromDirectoryParsesFile)
{
    const auto tempDir = std::filesystem::temp_directory_path() / "symbols_ignore_load";
    std::filesystem::create_directories(tempDir);

    {
        std::ofstream f((tempDir / ".symbols-ignore").string());
        f << "build/*\n";
        f << "vendor/big.c\n";
    }

    const auto list = IgnoreList::loadFromDirectory(tempDir);
    ASSERT_EQ(list.ruleCount(), static_cast<usize>(2));
    ASSERT_TRUE(list.isIgnored(dc::StringView("build/parser.c")));
    ASSERT_TRUE(list.isIgnored(dc::StringView("vendor/big.c")));
    ASSERT_FALSE(list.isIgnored(dc::StringView("src/main.cpp")));

    std::filesystem::remove_all(tempDir);
}

// ---------------------------------------------------------------------------
// Edge cases
// ---------------------------------------------------------------------------

DTEST(emptyIgnoreListIgnoresNothing)
{
    const IgnoreList list;
    ASSERT_FALSE(list.isIgnored(dc::StringView("anything/at/all.cpp")));
}

DTEST(fileWithNoTrailingNewline)
{
    // No '\n' at end — the last rule must still be picked up.
    const auto list = IgnoreList::parse(dc::StringView("build/parser.c"));
    ASSERT_EQ(list.ruleCount(), static_cast<usize>(1));
    ASSERT_TRUE(list.isIgnored(dc::StringView("build/parser.c")));
}
