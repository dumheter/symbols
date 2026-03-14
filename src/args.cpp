#include <args.hpp>

#include <cstring>

namespace symbols {

auto parseArgs(int argc, const char* const* argv) -> dc::Result<ParsedArgs, dc::String>
{
    ParsedArgs result;
    result.config.useCache = true;

    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--root") == 0) {
            if (i + 1 >= argc)
                return dc::Err<dc::String>(dc::String("--root requires a value"));
            result.config.projectRoot = argv[++i];
        } else if (std::strcmp(argv[i], "--search-dir") == 0) {
            if (i + 1 >= argc)
                return dc::Err<dc::String>(dc::String("--search-dir requires a value"));
            result.config.searchDirs.add(dc::String(argv[++i]));
        } else if (std::strcmp(argv[i], "--no-cache") == 0) {
            result.config.useCache = false;
        } else if (std::strcmp(argv[i], "--help") == 0) {
            result.helpRequested = true;
            return dc::Ok<ParsedArgs>(dc::move(result));
        } else {
            dc::String err("Unknown option: ");
            err += argv[i];
            return dc::Err<dc::String>(dc::move(err));
        }
    }

    if (result.config.projectRoot.empty())
        return dc::Err<dc::String>(dc::String("--root is required"));

    return dc::Ok<ParsedArgs>(dc::move(result));
}

} // namespace symbols
