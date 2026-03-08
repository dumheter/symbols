#pragma once

#include <dc/list.hpp>
#include <dc/map.hpp>
#include <dc/result.hpp>
#include <dc/string.hpp>
#include <dc/types.hpp>

#include <parser.hpp>

#include <filesystem>

namespace symbols {

/// A scored search result.
struct SearchResult {
    const Symbol* symbol;
    s32 score;
};

/// Tracks per-file modification time for incremental rebuilds.
struct FileRecord {
    s64 mtime; ///< Last modification time in nanoseconds since epoch.
};

/// Index of all symbols in a project.
/// Supports building from disk, caching, and fuzzy searching.
class Indexer {
public:
    Indexer();
    ~Indexer();

    Indexer(const Indexer&) = delete;
    auto operator=(const Indexer&) -> Indexer& = delete;
    Indexer(Indexer&&) noexcept;
    auto operator=(Indexer&&) noexcept -> Indexer&;

    /// Build the index by scanning all files in the project (full rebuild).
    /// @param projectRoot The root directory of the project.
    /// @param searchDirs Optional subdirectories to limit scanning to.
    ///        If empty, scans from projectRoot.
    auto build(const std::filesystem::path& projectRoot, const dc::List<dc::String>& searchDirs = {}) -> void;

    /// Incrementally rebuild: re-parse only changed/new files, drop removed files.
    /// Requires that the index has previously been built or loaded from cache.
    /// Falls back to a full build if the existing file record map is empty.
    /// @param projectRoot The root directory of the project.
    /// @param searchDirs Optional subdirectories to limit scanning to.
    ///        If empty, scans from projectRoot.
    auto incrementalBuild(const std::filesystem::path& projectRoot, const dc::List<dc::String>& searchDirs = {})
        -> void;

    /// Save the index to a cache file in the project directory.
    /// Returns Ok(true) on success.
    [[nodiscard]] auto saveCache(const std::filesystem::path& projectRoot) -> dc::Result<bool, dc::String>;

    /// Load the index from a cache file.
    /// Returns Ok(true) on success.
    [[nodiscard]] auto loadCache(const std::filesystem::path& projectRoot) -> dc::Result<bool, dc::String>;
    /// Check if a cache file exists and is valid.
    [[nodiscard]] auto hasCacheFile(const std::filesystem::path& projectRoot) const -> bool;

    /// Search for symbols matching a pattern.
    /// Returns up to `limit` results sorted by relevance score.
    [[nodiscard]] auto search(dc::StringView pattern, u32 limit) const -> dc::List<SearchResult>;

    /// Get total number of indexed symbols.
    [[nodiscard]] auto symbolCount() const -> usize;

    /// Get total number of indexed files.
    [[nodiscard]] auto fileCount() const -> usize;

    /// Check if the index is ready.
    [[nodiscard]] auto isReady() const -> bool;

private:
    /// Score a symbol name against a search pattern.
    /// Higher score = better match. Returns -1 for no match.
    [[nodiscard]] auto scoreMatch(dc::StringView name, dc::StringView pattern) const -> s32;

    dc::List<Symbol> m_symbols;
    /// Map from relative file path string to FileRecord (mtime).
    dc::Map<dc::String, FileRecord> m_fileRecords;
    usize m_fileCount;
    bool m_ready;
};

} // namespace symbols
