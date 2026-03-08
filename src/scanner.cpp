#include <scanner.hpp>

#include <dc/log.hpp>
#include <dc/types.hpp>

namespace symbols {

[[nodiscard]] auto scanDirectory(const std::filesystem::path& root, const dc::List<dc::String>& extensions)
    -> dc::List<std::filesystem::path>
{
    dc::List<std::filesystem::path> files;

    if (!std::filesystem::exists(root) || !std::filesystem::is_directory(root)) {
        LOG_WARNING("Directory does not exist: {}", root.string().c_str());
        return files;
    }

    std::error_code ec;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(
             root, std::filesystem::directory_options::skip_permission_denied, ec)) {
        if (ec) {
            LOG_WARNING("Error iterating directory: {}", ec.message().c_str());
            ec.clear();
            continue;
        }

        if (!entry.is_regular_file())
            continue;

        const auto ext = entry.path().extension().string();
        if (ext.empty())
            continue;

        // Compare extension without leading dot.
        const auto extNoDot = ext.substr(1);

        bool matched = false;
        for (u64 i = 0; i < extensions.getSize(); ++i) {
            if (extNoDot == extensions[i].c_str()) {
                matched = true;
                break;
            }
        }

        if (matched) {
            files.add(entry.path());
        }
    }

    return files;
}

[[nodiscard]] auto defaultCppExtensions() -> dc::List<dc::String>
{
    dc::List<dc::String> exts;
    exts.add(dc::String("cpp"));
    exts.add(dc::String("h"));
    exts.add(dc::String("hpp"));
    exts.add(dc::String("cc"));
    exts.add(dc::String("cxx"));
    exts.add(dc::String("c"));
    exts.add(dc::String("hxx"));
    exts.add(dc::String("ixx"));
    return exts;
}

} // namespace symbols
