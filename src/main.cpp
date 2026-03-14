#include <dc/job_system.hpp>
#include <dc/log.hpp>

#include <args.hpp>
#include <server.hpp>

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

    auto argsResult = symbols::parseArgs(argc, const_cast<const char**>(argv));
    if (!argsResult.isOk()) {
        std::println(stderr, "Error: {}", dc::move(argsResult).unwrapErr().c_str());
        printUsage(argv[0]);
        dc::log::deinit();
        return 1;
    }

    auto parsed = dc::move(argsResult).unwrap();

    if (parsed.helpRequested) {
        printUsage(argv[0]);
        dc::log::deinit();
        return 0;
    }

    // Normalize to absolute path.
    parsed.config.projectRoot = std::filesystem::absolute(parsed.config.projectRoot);

    if (!std::filesystem::exists(parsed.config.projectRoot)) {
        std::println(stderr, "Error: project root does not exist: {}", parsed.config.projectRoot.string());
        dc::log::deinit();
        return 1;
    }

    dc::JobSystem jobSystem;
    parsed.config.jobSystem = &jobSystem;

    const s32 result = symbols::runServer(parsed.config);

    dc::log::deinit();
    return result;
}
