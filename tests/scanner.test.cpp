#include <dc/dtest.hpp>
#include <scanner.hpp>

#include <filesystem>
#include <fstream>

using namespace symbols;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static auto makeFile(const std::filesystem::path& path) -> void
{
    // Touch a file (empty content is fine for scanner tests).
    std::ofstream(path.string()).flush();
}

static auto containsFile(const dc::List<std::filesystem::path>& files, const std::filesystem::path& target) -> bool
{
    for (u64 i = 0; i < files.getSize(); ++i) {
        if (files[i] == target)
            return true;
    }
    return false;
}

// ---------------------------------------------------------------------------
// defaultCppExtensions
// ---------------------------------------------------------------------------

DTEST(defaultCppExtensionsNotEmpty)
{
    const auto exts = defaultCppExtensions();
    ASSERT_TRUE(exts.getSize() > static_cast<usize>(0));
}

DTEST(defaultCppExtensionsContainsCommonExtensions)
{
    const auto exts = defaultCppExtensions();

    bool hasCpp = false;
    bool hasHpp = false;
    bool hasH = false;
    bool hasC = false;
    for (u64 i = 0; i < exts.getSize(); ++i) {
        if (exts[i] == "cpp")
            hasCpp = true;
        else if (exts[i] == "hpp")
            hasHpp = true;
        else if (exts[i] == "h")
            hasH = true;
        else if (exts[i] == "c")
            hasC = true;
    }
    ASSERT_TRUE(hasCpp);
    ASSERT_TRUE(hasHpp);
    ASSERT_TRUE(hasH);
    ASSERT_TRUE(hasC);
}

// ---------------------------------------------------------------------------
// scanDirectory — basic behavior
// ---------------------------------------------------------------------------

DTEST(scanDirectoryFindsMatchingFiles)
{
    const auto tempDir = std::filesystem::temp_directory_path() / "symbols_scan_basic";
    std::filesystem::create_directories(tempDir);

    const auto cppFile = tempDir / "main.cpp";
    const auto hppFile = tempDir / "util.hpp";
    makeFile(cppFile);
    makeFile(hppFile);

    dc::List<dc::String> exts;
    exts.add(dc::String("cpp"));
    exts.add(dc::String("hpp"));

    const auto files = scanDirectory(tempDir, exts);
    ASSERT_TRUE(containsFile(files, cppFile));
    ASSERT_TRUE(containsFile(files, hppFile));

    std::filesystem::remove_all(tempDir);
}

DTEST(scanDirectoryIgnoresNonMatchingExtensions)
{
    const auto tempDir = std::filesystem::temp_directory_path() / "symbols_scan_ignore";
    std::filesystem::create_directories(tempDir);

    makeFile(tempDir / "readme.md");
    makeFile(tempDir / "data.json");
    makeFile(tempDir / "script.py");
    makeFile(tempDir / "keep.cpp");

    dc::List<dc::String> exts;
    exts.add(dc::String("cpp"));

    const auto files = scanDirectory(tempDir, exts);
    ASSERT_EQ(files.getSize(), static_cast<usize>(1));
    ASSERT_TRUE(containsFile(files, tempDir / "keep.cpp"));

    std::filesystem::remove_all(tempDir);
}

DTEST(scanDirectoryRecursivelyFindsNestedFiles)
{
    const auto tempDir = std::filesystem::temp_directory_path() / "symbols_scan_recursive";
    const auto subDir = tempDir / "sub" / "deep";
    std::filesystem::create_directories(subDir);

    const auto topFile = tempDir / "top.cpp";
    const auto deepFile = subDir / "deep.cpp";
    makeFile(topFile);
    makeFile(deepFile);

    dc::List<dc::String> exts;
    exts.add(dc::String("cpp"));

    const auto files = scanDirectory(tempDir, exts);
    ASSERT_TRUE(containsFile(files, topFile));
    ASSERT_TRUE(containsFile(files, deepFile));
    ASSERT_TRUE(files.getSize() >= static_cast<usize>(2));

    std::filesystem::remove_all(tempDir);
}

DTEST(scanDirectoryReturnsEmptyForNonExistentRoot)
{
    const auto fakePath = std::filesystem::temp_directory_path() / "symbols_scan_does_not_exist_xyz";
    // Ensure it does not exist.
    std::filesystem::remove_all(fakePath);

    dc::List<dc::String> exts;
    exts.add(dc::String("cpp"));

    const auto files = scanDirectory(fakePath, exts);
    ASSERT_EQ(files.getSize(), static_cast<usize>(0));
}

DTEST(scanDirectoryReturnsEmptyForEmptyExtensionList)
{
    const auto tempDir = std::filesystem::temp_directory_path() / "symbols_scan_no_exts";
    std::filesystem::create_directories(tempDir);

    makeFile(tempDir / "main.cpp");
    makeFile(tempDir / "util.hpp");

    const dc::List<dc::String> exts;
    const auto files = scanDirectory(tempDir, exts);
    ASSERT_EQ(files.getSize(), static_cast<usize>(0));

    std::filesystem::remove_all(tempDir);
}

DTEST(scanDirectoryReturnsEmptyForEmptyDirectory)
{
    const auto tempDir = std::filesystem::temp_directory_path() / "symbols_scan_empty_dir";
    std::filesystem::create_directories(tempDir);

    dc::List<dc::String> exts;
    exts.add(dc::String("cpp"));

    const auto files = scanDirectory(tempDir, exts);
    ASSERT_EQ(files.getSize(), static_cast<usize>(0));

    std::filesystem::remove_all(tempDir);
}

DTEST(scanDirectoryFilesHaveAbsolutePaths)
{
    const auto tempDir = std::filesystem::temp_directory_path() / "symbols_scan_abspath";
    std::filesystem::create_directories(tempDir);
    makeFile(tempDir / "abs.cpp");

    dc::List<dc::String> exts;
    exts.add(dc::String("cpp"));

    const auto files = scanDirectory(tempDir, exts);
    ASSERT_EQ(files.getSize(), static_cast<usize>(1));
    ASSERT_TRUE(files[0].is_absolute());

    std::filesystem::remove_all(tempDir);
}

DTEST(scanDirectoryWithDefaultExtensions)
{
    const auto tempDir = std::filesystem::temp_directory_path() / "symbols_scan_default_exts";
    std::filesystem::create_directories(tempDir);

    makeFile(tempDir / "main.cpp");
    makeFile(tempDir / "util.hpp");
    makeFile(tempDir / "api.h");
    makeFile(tempDir / "impl.cc");
    makeFile(tempDir / "README.md"); // should be excluded

    const auto files = scanDirectory(tempDir, defaultCppExtensions());
    ASSERT_TRUE(files.getSize() >= static_cast<usize>(4));

    bool hasMd = false;
    for (u64 i = 0; i < files.getSize(); ++i) {
        if (files[i].extension() == ".md")
            hasMd = true;
    }
    ASSERT_FALSE(hasMd);

    std::filesystem::remove_all(tempDir);
}
