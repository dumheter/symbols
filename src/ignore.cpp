#include <ignore.hpp>

#include <dc/file.hpp>
#include <dc/log.hpp>

namespace symbols {

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

/// Normalise a path string to forward slashes and strip a leading "./", if any.
static auto normalisePath(dc::StringView sv) -> dc::String
{
    dc::String out;
    for (u64 i = 0; i < sv.getSize(); ++i)
        out += (sv[i] == '\\') ? '/' : sv[i];

    // Strip a leading "./"
    if (out.getSize() >= 2 && out.toView()[0] == '.' && out.toView()[1] == '/')
        out = dc::String(out.c_str() + 2);

    return out;
}

/// Return true if `sv` starts with the given prefix.
static auto startsWith(dc::StringView sv, dc::StringView prefix) -> bool
{
    if (prefix.getSize() > sv.getSize())
        return false;
    for (u64 i = 0; i < prefix.getSize(); ++i) {
        if (sv[i] != prefix[i])
            return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// IgnoreList::parse
// ---------------------------------------------------------------------------

auto IgnoreList::parse(dc::StringView text) -> IgnoreList
{
    IgnoreList list;

    u64 i = 0;
    const u64 len = text.getSize();

    while (i < len) {
        // Find end of current line.
        u64 lineEnd = i;
        while (lineEnd < len && text[lineEnd] != '\n')
            ++lineEnd;

        // Build the line (strip trailing '\r').
        u64 lineLen = lineEnd - i;
        if (lineLen > 0 && text[i + lineLen - 1] == '\r')
            --lineLen;

        if (lineLen > 0) {
            dc::String line;
            for (u64 j = i; j < i + lineLen; ++j)
                line += text[j];

            const dc::StringView lv(line);

            // Skip comments and blank lines.
            if (lv[0] != '#') {
                dc::String pattern = normalisePath(lv);
                const dc::StringView pv(pattern);

                bool isGlob = false;
                // Detect trailing "/*" — directory wildcard.
                if (pv.getSize() >= 2 && pv[pv.getSize() - 1] == '*' && pv[pv.getSize() - 2] == '/') {
                    isGlob = true;
                    // Strip the trailing '*' so the stored prefix is "dir/".
                    dc::String trimmed;
                    for (u64 j = 0; j < pv.getSize() - 1; ++j)
                        trimmed += pv[j];
                    pattern = dc::move(trimmed);
                }

                IgnoreRule rule;
                rule.pattern = dc::move(pattern);
                rule.isGlob = isGlob;
                list.m_rules.add(dc::move(rule));
            }
        }

        i = lineEnd + 1; // skip past '\n'
    }

    return list;
}

// ---------------------------------------------------------------------------
// IgnoreList::loadFromDirectory
// ---------------------------------------------------------------------------

auto IgnoreList::loadFromDirectory(const std::filesystem::path& projectRoot) -> IgnoreList
{
    const auto ignoreFile = projectRoot / ".symbols-ignore";
    if (!std::filesystem::exists(ignoreFile)) {
        LOG_INFO("No .symbols-ignore found; indexing without ignore rules");
        return IgnoreList {};
    }

    dc::File file;
    auto openResult = file.open(dc::String(ignoreFile.string().c_str()), dc::File::Mode::kRead);
    if (openResult.isErr()) {
        LOG_WARNING("Could not open .symbols-ignore: {}", ignoreFile.string().c_str());
        return IgnoreList {};
    }

    auto readResult = file.read();
    if (!readResult.isOk()) {
        LOG_WARNING("Could not read .symbols-ignore: {}", ignoreFile.string().c_str());
        return IgnoreList {};
    }

    const dc::String content = dc::move(readResult).unwrap();
    auto list = IgnoreList::parse(dc::StringView(content));
    LOG_INFO("Applying .symbols-ignore with {} rule(s)", list.ruleCount());
    return list;
}

// ---------------------------------------------------------------------------
// IgnoreList::isIgnored
// ---------------------------------------------------------------------------

auto IgnoreList::isIgnored(dc::StringView relativePath) const -> bool
{
    // Normalise the incoming path to forward slashes.
    dc::String normPath = normalisePath(relativePath);
    const dc::StringView path(normPath);

    for (u64 i = 0; i < m_rules.getSize(); ++i) {
        const IgnoreRule& rule = m_rules[i];
        const dc::StringView pattern(rule.pattern);

        if (rule.isGlob) {
            // pattern is "prefix/" — match any path that starts with it.
            if (startsWith(path, pattern))
                return true;
        } else {
            // Exact relative-path match.
            if (path.getSize() == pattern.getSize()) {
                bool eq = true;
                for (u64 j = 0; j < pattern.getSize(); ++j) {
                    if (path[j] != pattern[j]) {
                        eq = false;
                        break;
                    }
                }
                if (eq)
                    return true;
            }
        }
    }

    return false;
}

} // namespace symbols
