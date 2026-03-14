#include <dc/dtest.hpp>

#include <args.hpp>

using namespace symbols;

// Helper: build a null-terminated argv array from a list of string literals.
// Lifetime is tied to the local const char* pointers — only use within the test.
#define MAKE_ARGV(...)                                                                                                 \
    const char* argvArr[] = { __VA_ARGS__, nullptr };                                                                  \
    const int argcVal = static_cast<int>((sizeof(argvArr) / sizeof(argvArr[0])) - 1)

// ---------------------------------------------------------------------------
// Error paths
// ---------------------------------------------------------------------------

DTEST(parseArgsRequiresRoot)
{
    // No arguments at all → missing --root → Err.
    MAKE_ARGV("symbols.exe");
    const auto result = parseArgs(argcVal, argvArr);
    ASSERT_FALSE(result.isOk());
}

DTEST(parseArgsMissingRootValue)
{
    // --root with no following value → Err.
    MAKE_ARGV("symbols.exe", "--root");
    const auto result = parseArgs(argcVal, argvArr);
    ASSERT_FALSE(result.isOk());
}

DTEST(parseArgsMissingSearchDirValue)
{
    // --search-dir with no following value → Err.
    MAKE_ARGV("symbols.exe", "--root", "/tmp", "--search-dir");
    const auto result = parseArgs(argcVal, argvArr);
    ASSERT_FALSE(result.isOk());
}

DTEST(parseArgsUnknownOption)
{
    // Unrecognised flag → Err.
    MAKE_ARGV("symbols.exe", "--root", "/tmp", "--badoption");
    const auto result = parseArgs(argcVal, argvArr);
    ASSERT_FALSE(result.isOk());
}

// ---------------------------------------------------------------------------
// Success paths
// ---------------------------------------------------------------------------

DTEST(parseArgsHelpFlag)
{
    // --help → Ok with helpRequested=true; --root is not required.
    MAKE_ARGV("symbols.exe", "--help");
    const auto result = parseArgs(argcVal, argvArr);
    ASSERT_TRUE(result.isOk());
    // Need a mutable copy to unwrap.
    auto parsed = dc::move(const_cast<dc::Result<ParsedArgs, dc::String>&>(result)).unwrap();
    ASSERT_TRUE(parsed.helpRequested);
}

DTEST(parseArgsRootOnly)
{
    MAKE_ARGV("symbols.exe", "--root", "/tmp/myproject");
    auto result = parseArgs(argcVal, argvArr);
    ASSERT_TRUE(result.isOk());
    auto parsed = dc::move(result).unwrap();
    ASSERT_FALSE(parsed.helpRequested);
    // projectRoot must be set (path comparison via string).
    ASSERT_TRUE(parsed.config.projectRoot == std::filesystem::path("/tmp/myproject"));
    // Cache is enabled by default.
    ASSERT_TRUE(parsed.config.useCache);
    // No search dirs.
    ASSERT_EQ(parsed.config.searchDirs.getSize(), static_cast<usize>(0));
}

DTEST(parseArgsNoCache)
{
    MAKE_ARGV("symbols.exe", "--root", "/tmp", "--no-cache");
    auto result = parseArgs(argcVal, argvArr);
    ASSERT_TRUE(result.isOk());
    auto parsed = dc::move(result).unwrap();
    ASSERT_FALSE(parsed.config.useCache);
}

DTEST(parseArgsMultipleSearchDirs)
{
    MAKE_ARGV("symbols.exe", "--root", "/tmp", "--search-dir", "src", "--search-dir", "include");
    auto result = parseArgs(argcVal, argvArr);
    ASSERT_TRUE(result.isOk());
    auto parsed = dc::move(result).unwrap();
    ASSERT_EQ(parsed.config.searchDirs.getSize(), static_cast<usize>(2));
    ASSERT_TRUE(parsed.config.searchDirs[0] == "src");
    ASSERT_TRUE(parsed.config.searchDirs[1] == "include");
}
