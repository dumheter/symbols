#include <parser.hpp>

#include <dc/file.hpp>
#include <dc/log.hpp>

#include <tree_sitter/api.h>

#include <algorithm>
#include <cstring>

extern "C" const TSLanguage* tree_sitter_cpp(void);

namespace symbols {

[[nodiscard]] auto symbolKindToString(SymbolKind kind) -> dc::StringView
{
    switch (kind) {
    case SymbolKind::Function:
        return dc::StringView("function");
    case SymbolKind::Class:
        return dc::StringView("class");
    case SymbolKind::Struct:
        return dc::StringView("struct");
    case SymbolKind::Enum:
        return dc::StringView("enum");
    case SymbolKind::Alias:
        return dc::StringView("alias");
    case SymbolKind::Typedef:
        return dc::StringView("typedef");
    case SymbolKind::Macro:
        return dc::StringView("macro");
    case SymbolKind::Variable:
        return dc::StringView("variable");
    }
    return dc::StringView("unknown");
}

[[nodiscard]] auto stringToSymbolKind(dc::StringView str) -> SymbolKind
{
    if (dc::String(str) == "function")
        return SymbolKind::Function;
    if (dc::String(str) == "class")
        return SymbolKind::Class;
    if (dc::String(str) == "struct")
        return SymbolKind::Struct;
    if (dc::String(str) == "enum")
        return SymbolKind::Enum;
    if (dc::String(str) == "alias")
        return SymbolKind::Alias;
    if (dc::String(str) == "typedef")
        return SymbolKind::Typedef;
    if (dc::String(str) == "macro")
        return SymbolKind::Macro;
    if (dc::String(str) == "variable")
        return SymbolKind::Variable;
    return SymbolKind::Function;
}

// Tree-sitter query for extracting C++ symbols.
// Captures: functions (including methods and function templates),
// classes/structs (including class/struct templates), enums,
// type aliases (using =), typedefs, #define macros, and variables.
// Variable captures are filtered after matching so only globals and
// constexpr variables are indexed.
static constexpr const char* kCppSymbolQuery = R"(
(function_definition
  declarator: (function_declarator
    declarator: (_) @func))

(declaration
  declarator: (function_declarator
    declarator: (_) @func))

(field_declaration
  declarator: (function_declarator
    declarator: (_) @func))

(template_declaration
  (function_definition
    declarator: (function_declarator
      declarator: (_) @func)))

(template_declaration
  (declaration
    declarator: (function_declarator
      declarator: (_) @func)))

(class_specifier
  name: (type_identifier) @class)

(struct_specifier
  name: (type_identifier) @struct)

(template_declaration
  (class_specifier
    name: (type_identifier) @class))

(template_declaration
  (struct_specifier
    name: (type_identifier) @struct))

(enum_specifier
  name: (type_identifier) @enum)

(alias_declaration
  name: (type_identifier) @alias)

(template_declaration
  (alias_declaration
    name: (type_identifier) @alias))

(type_definition
  declarator: (type_identifier) @typedef)

(preproc_def
  name: (_) @macro)

(preproc_function_def
  name: (_) @macro)

(declaration
  declarator: (init_declarator
    declarator: (identifier) @variable))

(declaration
  declarator: (init_declarator
    declarator: (pointer_declarator
      declarator: (identifier) @variable)))

(declaration
  declarator: (identifier) @variable)

(declaration
  declarator: (pointer_declarator
    declarator: (identifier) @variable))
)";

struct Parser::Impl {
    TSParser* parser = nullptr;
    TSQuery* query = nullptr;
    TSQueryCursor* cursor = nullptr;
};

/// Helper to compare a StringView with a C string.
static auto svEquals(dc::StringView sv, const char* str) -> bool
{
    const u64 len = static_cast<u64>(std::strlen(str));
    if (sv.getSize() != len)
        return false;
    return std::memcmp(sv.c_str(), str, len) == 0;
}

static auto captureNameToKind(const char* name, u32 len) -> SymbolKind
{
    const dc::StringView sv(name, static_cast<u64>(len));
    if (svEquals(sv, "func"))
        return SymbolKind::Function;
    if (svEquals(sv, "class"))
        return SymbolKind::Class;
    if (svEquals(sv, "struct"))
        return SymbolKind::Struct;
    if (svEquals(sv, "enum"))
        return SymbolKind::Enum;
    if (svEquals(sv, "alias"))
        return SymbolKind::Alias;
    if (svEquals(sv, "typedef"))
        return SymbolKind::Typedef;
    if (svEquals(sv, "macro"))
        return SymbolKind::Macro;
    if (svEquals(sv, "variable"))
        return SymbolKind::Variable;
    return SymbolKind::Function;
}

static auto nodeTypeEquals(TSNode node, const char* type) -> bool
{
    return std::strcmp(ts_node_type(node), type) == 0;
}

static auto findAncestorDeclaration(TSNode node) -> TSNode
{
    TSNode current = node;
    while (!ts_node_is_null(current)) {
        if (nodeTypeEquals(current, "declaration"))
            return current;
        current = ts_node_parent(current);
    }

    return {};
}

static auto declarationHasConstexpr(TSNode declaration, const char* source) -> bool
{
    const u32 childCount = ts_node_child_count(declaration);
    for (u32 i = 0; i < childCount; ++i) {
        const TSNode child = ts_node_child(declaration, i);
        if (ts_node_is_null(child))
            continue;

        if (nodeTypeEquals(child, "constexpr"))
            return true;

        const u32 start = ts_node_start_byte(child);
        const u32 end = ts_node_end_byte(child);
        const u32 size = end - start;
        if (size == 9 && std::memcmp(source + start, "constexpr", 9) == 0)
            return true;
    }

    return false;
}

static auto declarationIsGlobal(TSNode declaration) -> bool
{
    TSNode current = ts_node_parent(declaration);
    while (!ts_node_is_null(current)) {
        if (nodeTypeEquals(current, "compound_statement"))
            return false;
        current = ts_node_parent(current);
    }

    return true;
}

static auto shouldIndexVariable(TSNode capturedNode, const char* source) -> bool
{
    const TSNode declaration = findAncestorDeclaration(capturedNode);
    if (ts_node_is_null(declaration))
        return false;

    if (declarationHasConstexpr(declaration, source))
        return true;

    return declarationIsGlobal(declaration);
}

/// Extract the symbol name from a node's byte range within the source buffer.
static auto extractNodeName(const char* source, u32 start, u32 end) -> dc::String
{
    return dc::String(source + start, static_cast<u64>(end - start));
}

/// Check if a class/struct specifier is a forward declaration (no body).
static auto isForwardDeclaration(SymbolKind kind, TSNode capturedNode) -> bool
{
    if (kind != SymbolKind::Class && kind != SymbolKind::Struct)
        return false;

    const TSNode parent = ts_node_parent(capturedNode);
    if (ts_node_is_null(parent))
        return false;

    const TSNode body = ts_node_child_by_field_name(parent, "body", 4);
    return ts_node_is_null(body);
}

/// Intermediate capture record used for single-pass line counting.
/// Stores the raw byte range into the source buffer instead of copying the
/// name into a dc::String — the allocation is deferred until the symbol
/// survives deduplication and is promoted to a final Symbol.
struct CaptureInfo {
    u32 byteOffset;
    u32 byteEnd;
    SymbolKind kind;
};

Parser::Parser()
    : m_impl(new Impl)
{
    m_impl->parser = ts_parser_new();
    ts_parser_set_language(m_impl->parser, tree_sitter_cpp());

    u32 errorOffset = 0;
    TSQueryError errorType = TSQueryErrorNone;
    m_impl->query = ts_query_new(
        tree_sitter_cpp(), kCppSymbolQuery, static_cast<u32>(std::strlen(kCppSymbolQuery)), &errorOffset, &errorType);

    if (!m_impl->query) {
        LOG_ERROR("Failed to compile tree-sitter query at offset {}, error type {}", errorOffset,
            static_cast<int>(errorType));
    }

    m_impl->cursor = ts_query_cursor_new();
}

Parser::~Parser()
{
    if (m_impl) {
        if (m_impl->cursor)
            ts_query_cursor_delete(m_impl->cursor);
        if (m_impl->query)
            ts_query_delete(m_impl->query);
        if (m_impl->parser)
            ts_parser_delete(m_impl->parser);
        delete m_impl;
    }
}

Parser::Parser(Parser&& other) noexcept
    : m_impl(other.m_impl)
{
    other.m_impl = nullptr;
}

auto Parser::operator=(Parser&& other) noexcept -> Parser&
{
    if (this != &other) {
        if (m_impl) {
            if (m_impl->cursor)
                ts_query_cursor_delete(m_impl->cursor);
            if (m_impl->query)
                ts_query_delete(m_impl->query);
            if (m_impl->parser)
                ts_parser_delete(m_impl->parser);
            delete m_impl;
        }
        m_impl = other.m_impl;
        other.m_impl = nullptr;
    }
    return *this;
}

[[nodiscard]] auto Parser::parseFile(const std::filesystem::path& filePath, dc::StringView relativePath)
    -> dc::Result<dc::List<Symbol>, dc::String>
{
    if (!m_impl || !m_impl->query) {
        return dc::Err<dc::String>(dc::String("Parser not initialized"));
    }

    // Read the file. dc::File::open is an instance method.
    dc::File file;
    auto openResult = file.open(dc::String(filePath.string().c_str()), dc::File::Mode::kRead);
    if (openResult.isErr()) {
        dc::String err("Failed to open file: ");
        err += filePath.string().c_str();
        return dc::Err<dc::String>(dc::move(err));
    }

    auto readResult = file.read();
    if (!readResult.isOk()) {
        dc::String err("Failed to read file: ");
        err += filePath.string().c_str();
        return dc::Err<dc::String>(dc::move(err));
    }

    const dc::String source = dc::move(readResult).unwrap();
    const char* sourcePtr = source.c_str();
    const auto sourceLen = static_cast<u32>(source.getSize());

    // Parse with tree-sitter.
    TSTree* tree = ts_parser_parse_string(m_impl->parser, nullptr, sourcePtr, sourceLen);
    if (!tree) {
        dc::String err("Failed to parse file: ");
        err += filePath.string().c_str();
        return dc::Err<dc::String>(dc::move(err));
    }

    const TSNode root = ts_tree_root_node(tree);

    // relativePath is pre-computed by the caller to avoid repeated
    // std::filesystem::relative() calls on the worker threads.
    const dc::String relativePathStr(relativePath);

    // Execute query.
    // Limit the cursor to nodes that start reasonably close to the
    // translation-unit root. This still avoids descending into the bulk of
    // function-body ASTs while leaving room for declarations nested inside
    // several namespaces and a class body, such as pure virtual methods.
    ts_query_cursor_set_max_start_depth(m_impl->cursor, 16);
    ts_query_cursor_exec(m_impl->cursor, m_impl->query, root);

    TSQueryMatch match;

    // Collect all valid captures first; deduplication happens after sorting.
    // template_declaration patterns fire alongside plain patterns for the same
    // inner node (producing duplicate byte offsets), so we must deduplicate.
    dc::List<CaptureInfo> captures;

    while (ts_query_cursor_next_match(m_impl->cursor, &match)) {
        for (u16 i = 0; i < match.capture_count; ++i) {
            const TSNode node = match.captures[i].node;
            const u32 captureIdx = match.captures[i].index;

            u32 nameLen = 0;
            const char* captureName = ts_query_capture_name_for_id(m_impl->query, captureIdx, &nameLen);
            const SymbolKind kind = captureNameToKind(captureName, nameLen);

            // Skip forward declarations.
            if (isForwardDeclaration(kind, node))
                continue;

            if (kind == SymbolKind::Variable && !shouldIndexVariable(node, sourcePtr))
                continue;

            CaptureInfo cap;
            cap.byteOffset = ts_node_start_byte(node);
            cap.byteEnd = ts_node_end_byte(node);
            cap.kind = kind;
            captures.add(dc::move(cap));
        }
    }

    // Sort by byte offset so that:
    //   1. Duplicate offsets (template + plain patterns for the same node) are
    //      adjacent and can be removed with a single O(1) prev-offset check.
    //   2. Line numbers can be assigned in one forward pass — O(N + S) instead
    //      of O(S * N).
    std::sort(captures.begin(), captures.end(),
        [](const CaptureInfo& a, const CaptureInfo& b) { return a.byteOffset < b.byteOffset; });

    dc::List<Symbol> symbols;
    symbols.reserve(captures.getSize());

    u32 curByte = 0;
    u32 curLine = 1;
    u32 prevOffset = ~static_cast<u32>(0); // sentinel: no previous offset
    for (u64 i = 0; i < captures.getSize(); ++i) {
        CaptureInfo& cap = captures[i];

        // Adjacent-duplicate removal: template patterns produce the same byte
        // offset as the inner plain pattern; skip the duplicate.
        if (cap.byteOffset == prevOffset)
            continue;
        prevOffset = cap.byteOffset;

        while (curByte < cap.byteOffset) {
            if (sourcePtr[curByte] == '\n')
                ++curLine;
            ++curByte;
        }

        Symbol sym;
        sym.name = extractNodeName(sourcePtr, cap.byteOffset, cap.byteEnd);
        sym.kind = cap.kind;
        sym.file = relativePathStr;
        sym.line = curLine;
        symbols.add(dc::move(sym));
    }

    ts_tree_delete(tree);
    return dc::Ok<dc::List<Symbol>>(dc::move(symbols));
}

} // namespace symbols
