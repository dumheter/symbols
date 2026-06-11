#include <dc/dtest.hpp>
#include <dc/log.hpp>
#include <dc/time.hpp>
#include <logging.hpp>

#include <filesystem>
#include <fstream>
#include <string>

DTEST(makeLogFilePathUsesRequestedDirectoryAndLogExtension)
{
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "symbols_logging_path_test";
    const std::filesystem::path path = symbols::makeLogFilePath(dir);
    const std::string filename = path.filename().string();

    ASSERT_EQ(path.parent_path().string(), dir.string());
    ASSERT_TRUE(filename.rfind("symbols-", 0) == 0);
    ASSERT_TRUE(path.extension().string() == ".log");
}

DTEST(attachFileLoggerWritesLogMessages)
{
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "symbols_logging_write_test";
    auto attachResult = symbols::attachFileLogger(dir);
    ASSERT_TRUE(attachResult.isOk());

    const std::filesystem::path logPath = dc::move(attachResult).unwrap();
    LOG_INFO("persistent log test marker");
    dc::sleepMs(50);

    std::ifstream in(logPath);
    ASSERT_TRUE(in.is_open());
    const std::string contents((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    ASSERT_TRUE(contents.find("persistent log test marker") != std::string::npos);
}
