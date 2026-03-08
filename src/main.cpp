#include <dc/log.hpp>
#include <dc/string.hpp>

#include <server.hpp>

#include <cstring>
#include <filesystem>
#include <print>

static auto printUsage(const char* programName) -> void
{
    std::println(stderr,
        "Usage: {} --root <project_root> [options]\n"
        "\n"
        "Options:\n"
        "  --root <path>       Project root directory (required)\n"
        "  --search-dir <path> Subdirectory to scan (can be repeated)\n"
        "  --no-cache          Disable disk cache\n"
        "  --help              Show this help message\n"
        "\n"
        "The server reads JSON requests from stdin and writes\n"
        "JSON responses to stdout (one per line).\n"
        "\n"
        "Request format:\n"
        "  {{\"id\":1,\"method\":\"query\",\"params\":{{\"pattern\":\"Foo\",\"limit\":200}}}}\n"
        "  {{\"id\":2,\"method\":\"status\"}}\n"
        "  {{\"id\":3,\"method\":\"rebuild\"}}\n"
        "  {{\"id\":4,\"method\":\"shutdown\"}}",
        programName);
}

int main(int argc, char** argv)
{
    // Route all log output to stderr so that stdout carries only JSON protocol messages.
    dc::log::getGlobalLogger().detachSink("default").attachSink(
        [](const dc::log::Payload& payload, dc::log::Level level) {
            if (payload.level >= level)
                std::println(stderr, "{}:{} {}", payload.fileName, payload.lineno, payload.msg.c_str());
        },
        "stderr");
    dc::log::init();

    symbols::ServerConfig config;
    config.useCache = true;

    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--root") == 0 && i + 1 < argc) {
            config.projectRoot = std::filesystem::path(argv[++i]);
        } else if (std::strcmp(argv[i], "--search-dir") == 0 && i + 1 < argc) {
            config.searchDirs.add(dc::String(argv[++i]));
        } else if (std::strcmp(argv[i], "--no-cache") == 0) {
            config.useCache = false;
        } else if (std::strcmp(argv[i], "--help") == 0) {
            printUsage(argv[0]);
            dc::log::deinit();
            return 0;
        } else {
            std::println(stderr, "Unknown option: {}", argv[i]);
            printUsage(argv[0]);
            dc::log::deinit();
            return 1;
        }
    }

    if (config.projectRoot.empty()) {
        std::println(stderr, "Error: --root is required\n");
        printUsage(argv[0]);
        dc::log::deinit();
        return 1;
    }

    // Normalize to absolute path.
    config.projectRoot = std::filesystem::absolute(config.projectRoot);

    if (!std::filesystem::exists(config.projectRoot)) {
        std::println(stderr, "Error: project root does not exist: {}", config.projectRoot.string());
        dc::log::deinit();
        return 1;
    }

    const s32 result = symbols::runServer(config);

    dc::log::deinit();
    return result;
}
