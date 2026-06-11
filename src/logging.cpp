#include <logging.hpp>

#include <dc/log.hpp>
#include <dc/time.hpp>

#include <cstdlib>
#include <fstream>
#include <memory>
#include <print>
#include <string>
#include <system_error>

#ifdef _WIN32
#include <process.h>
#else
#include <unistd.h>
#endif

namespace symbols {

static auto currentProcessId() -> u32
{
#ifdef _WIN32
    return static_cast<u32>(_getpid());
#else
    return static_cast<u32>(getpid());
#endif
}

static auto levelName(dc::log::Level level) -> const char*
{
    switch (level) {
    case dc::log::Level::Verbose:
        return "verbose";
    case dc::log::Level::Info:
        return "info";
    case dc::log::Level::Warning:
        return "warning";
    case dc::log::Level::Error:
        return "error";
    case dc::log::Level::Raw:
        return "raw";
    case dc::log::Level::None:
        return "none";
    }

    return "unknown";
}

static auto pathFromEnv(const char* name) -> std::filesystem::path
{
#ifdef _WIN32
    char* value = nullptr;
    usize valueSize = 0;
    const errno_t result = _dupenv_s(&value, &valueSize, name);
    std::unique_ptr<char, decltype(&std::free)> valueGuard(value, &std::free);
    if (result != 0 || value == nullptr || value[0] == '\0')
        return {};

    return std::filesystem::path(value);
#else
    const char* value = std::getenv(name);
    if (value == nullptr || value[0] == '\0')
        return {};

    return std::filesystem::path(value);
#endif
}

auto defaultLogDirectory() -> std::filesystem::path
{
#ifdef _WIN32
    const std::filesystem::path appData = pathFromEnv("APPDATA");
    if (!appData.empty())
        return appData / "symbols" / "logs";
#endif

    const std::filesystem::path home = pathFromEnv("HOME");
    if (!home.empty())
        return home / ".symbols" / "logs";

    std::error_code ec;
    const std::filesystem::path temp = std::filesystem::temp_directory_path(ec);
    if (!ec)
        return temp / "symbols" / "logs";

    return std::filesystem::path("symbols") / "logs";
}

auto makeLogFilePath(const std::filesystem::path& logDirectory) -> std::filesystem::path
{
    const dc::Timestamp timestamp = dc::makeTimestamp();
    const std::string filename = std::format("symbols-{:04}{:02}{:02}-{:02}{:02}{:06.3f}-{}.log", timestamp.year,
        static_cast<u32>(timestamp.month), static_cast<u32>(timestamp.day), static_cast<u32>(timestamp.hour),
        static_cast<u32>(timestamp.minute), timestamp.second, currentProcessId());

    return logDirectory / filename;
}

auto attachFileLogger(const std::filesystem::path& logDirectory) -> dc::Result<std::filesystem::path, dc::String>
{
    std::error_code ec;
    std::filesystem::create_directories(logDirectory, ec);
    if (ec) {
        dc::String error("failed to create log directory: ");
        error += logDirectory.string().c_str();
        error += " (";
        error += ec.message().c_str();
        error += ")";
        return dc::Err<dc::String>(dc::move(error));
    }

    const std::filesystem::path logPath = makeLogFilePath(logDirectory);
    auto stream = std::make_shared<std::ofstream>(logPath, std::ios::out | std::ios::app);
    if (!stream->is_open()) {
        dc::String error("failed to open log file: ");
        error += logPath.string().c_str();
        return dc::Err<dc::String>(dc::move(error));
    }

    dc::log::getGlobalLogger().attachSink(
        [stream](const dc::log::Payload& payload, dc::log::Level level) {
            if (payload.level < level)
                return;

            if (payload.level == dc::log::Level::Raw) {
                *stream << payload.msg.c_str();
            } else {
                *stream << std::format("[{} {:04}-{:02}-{:02} {:02}:{:02}:{:09.6f} {}:{}] {}\n",
                    levelName(payload.level), payload.timestamp.year, static_cast<u32>(payload.timestamp.month),
                    static_cast<u32>(payload.timestamp.day), static_cast<u32>(payload.timestamp.hour),
                    static_cast<u32>(payload.timestamp.minute), payload.timestamp.second, payload.fileName,
                    payload.lineno, payload.msg.c_str());
            }
            stream->flush();
        },
        "symbols-file");

    return dc::Ok<std::filesystem::path>(logPath);
}

} // namespace symbols
