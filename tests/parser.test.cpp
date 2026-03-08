#include <dc/dtest.hpp>
#include <dc/file.hpp>
#include <parser.hpp>

#include <filesystem>

using namespace symbols;

// Helper: create a temporary file, parse it, return symbols.
static auto parseSourceString(const char* source) -> dc::List<Symbol>
{
    const auto tempDir = std::filesystem::temp_directory_path();
    const auto tempFile = tempDir / "symbols_test_temp.cpp";
    {
        dc::File file;
        auto openResult = file.open(dc::String(tempFile.string().c_str()), dc::File::Mode::kWrite);
        if (openResult.isOk()) {
            [[maybe_unused]] auto wr = file.write(dc::String(source));
        }
    }

    Parser parser;
    auto result = parser.parseFile(tempFile, tempDir);
    std::filesystem::remove(tempFile);

    if (result.isOk()) {
        return dc::move(result).unwrap();
    }
    return {};
}

DTEST(parseFunction)
{
    const auto symbols = parseSourceString("void foo() {}");
    ASSERT_TRUE(symbols.getSize() >= 1);

    bool found = false;
    for (u64 i = 0; i < symbols.getSize(); ++i) {
        if (symbols[i].name == "foo") {
            ASSERT_EQ(symbols[i].kind, SymbolKind::Function);
            found = true;
            break;
        }
    }
    ASSERT_TRUE(found);
}

DTEST(parseClass)
{
    const auto symbols = parseSourceString("class MyClass {};");
    ASSERT_TRUE(symbols.getSize() >= 1);

    bool found = false;
    for (u64 i = 0; i < symbols.getSize(); ++i) {
        if (symbols[i].name == "MyClass") {
            ASSERT_EQ(symbols[i].kind, SymbolKind::Class);
            found = true;
            break;
        }
    }
    ASSERT_TRUE(found);
}

DTEST(parseStruct)
{
    const auto symbols = parseSourceString("struct MyStruct { int x; };");
    ASSERT_TRUE(symbols.getSize() >= 1);

    bool found = false;
    for (u64 i = 0; i < symbols.getSize(); ++i) {
        if (symbols[i].name == "MyStruct") {
            ASSERT_EQ(symbols[i].kind, SymbolKind::Struct);
            found = true;
            break;
        }
    }
    ASSERT_TRUE(found);
}

DTEST(parseEnum)
{
    const auto symbols = parseSourceString("enum Color { Red, Green, Blue };");
    ASSERT_TRUE(symbols.getSize() >= 1);

    bool found = false;
    for (u64 i = 0; i < symbols.getSize(); ++i) {
        if (symbols[i].name == "Color") {
            ASSERT_EQ(symbols[i].kind, SymbolKind::Enum);
            found = true;
            break;
        }
    }
    ASSERT_TRUE(found);
}

DTEST(parseAlias)
{
    const auto symbols = parseSourceString("using StringList = int;");
    ASSERT_TRUE(symbols.getSize() >= 1);

    bool found = false;
    for (u64 i = 0; i < symbols.getSize(); ++i) {
        if (symbols[i].name == "StringList") {
            ASSERT_EQ(symbols[i].kind, SymbolKind::Alias);
            found = true;
            break;
        }
    }
    ASSERT_TRUE(found);
}

DTEST(parseTypedef)
{
    const auto symbols = parseSourceString("typedef int MyInt;");
    ASSERT_TRUE(symbols.getSize() >= 1);

    bool found = false;
    for (u64 i = 0; i < symbols.getSize(); ++i) {
        if (symbols[i].name == "MyInt") {
            ASSERT_EQ(symbols[i].kind, SymbolKind::Typedef);
            found = true;
            break;
        }
    }
    ASSERT_TRUE(found);
}

DTEST(parseForwardDeclarationSkipped)
{
    const auto symbols = parseSourceString("class ForwardOnly;");

    for (u64 i = 0; i < symbols.getSize(); ++i) {
        ASSERT_TRUE(symbols[i].name != "ForwardOnly");
    }
}

DTEST(parseQualifiedFunction)
{
    const auto symbols = parseSourceString(
        R"(
class Foo {
    void bar();
};
void Foo::bar() {}
)");

    bool found = false;
    for (u64 i = 0; i < symbols.getSize(); ++i) {
        if (symbols[i].name == "Foo::bar" && symbols[i].kind == SymbolKind::Function) {
            found = true;
            break;
        }
    }
    ASSERT_TRUE(found);
}

DTEST(parseMultipleSymbols)
{
    const auto symbols = parseSourceString(R"(
class Widget {};
struct Point { int x; int y; };
enum Direction { North, South };
void update() {}
using Id = int;
typedef float Score;
)");

    ASSERT_TRUE(symbols.getSize() >= 6);
}

DTEST(parseFunctionTemplate)
{
    const auto symbols = parseSourceString(R"(
template <typename T>
T maxOf(T a, T b) { return a > b ? a : b; }
)");

    bool found = false;
    for (u64 i = 0; i < symbols.getSize(); ++i) {
        if (symbols[i].name == "maxOf" && symbols[i].kind == SymbolKind::Function) {
            found = true;
            break;
        }
    }
    ASSERT_TRUE(found);

    // Must appear exactly once (no duplicate from template + plain patterns).
    u64 count = 0;
    for (u64 i = 0; i < symbols.getSize(); ++i) {
        if (symbols[i].name == "maxOf")
            ++count;
    }
    ASSERT_EQ(count, static_cast<u64>(1));
}

DTEST(parseClassTemplate)
{
    const auto symbols = parseSourceString(R"(
template <typename T>
class Container {};
)");

    bool found = false;
    for (u64 i = 0; i < symbols.getSize(); ++i) {
        if (symbols[i].name == "Container" && symbols[i].kind == SymbolKind::Class) {
            found = true;
            break;
        }
    }
    ASSERT_TRUE(found);

    // Must appear exactly once.
    u64 count = 0;
    for (u64 i = 0; i < symbols.getSize(); ++i) {
        if (symbols[i].name == "Container")
            ++count;
    }
    ASSERT_EQ(count, static_cast<u64>(1));
}

DTEST(parseStructTemplate)
{
    const auto symbols = parseSourceString(R"(
template <typename T, typename U>
struct Pair { T first; U second; };
)");

    bool found = false;
    for (u64 i = 0; i < symbols.getSize(); ++i) {
        if (symbols[i].name == "Pair" && symbols[i].kind == SymbolKind::Struct) {
            found = true;
            break;
        }
    }
    ASSERT_TRUE(found);

    u64 count = 0;
    for (u64 i = 0; i < symbols.getSize(); ++i) {
        if (symbols[i].name == "Pair")
            ++count;
    }
    ASSERT_EQ(count, static_cast<u64>(1));
}

DTEST(parseAliasTemplate)
{
    const auto symbols = parseSourceString(R"(
template <typename T>
using Vec = int;
)");

    bool found = false;
    for (u64 i = 0; i < symbols.getSize(); ++i) {
        if (symbols[i].name == "Vec" && symbols[i].kind == SymbolKind::Alias) {
            found = true;
            break;
        }
    }
    ASSERT_TRUE(found);

    u64 count = 0;
    for (u64 i = 0; i < symbols.getSize(); ++i) {
        if (symbols[i].name == "Vec")
            ++count;
    }
    ASSERT_EQ(count, static_cast<u64>(1));
}

DTEST(variablesNotIndexed)
{
    const auto symbols = parseSourceString(R"(
int globalVar = 42;
static float kPi = 3.14f;
const char* kName = "hello";
)");

    for (u64 i = 0; i < symbols.getSize(); ++i) {
        ASSERT_TRUE(symbols[i].name != "globalVar");
        ASSERT_TRUE(symbols[i].name != "kPi");
        ASSERT_TRUE(symbols[i].name != "kName");
    }
}

// ---------------------------------------------------------------------------
// Symbol metadata: line numbers and relative file paths
// ---------------------------------------------------------------------------

DTEST(parseSymbolLineNumber)
{
    // "foo" is defined on line 3 of the temp file.
    const auto symbols = parseSourceString(R"(
// line 2: comment
void foo() {}
)");

    bool found = false;
    for (u64 i = 0; i < symbols.getSize(); ++i) {
        if (symbols[i].name == "foo") {
            // Line numbers are 1-based; foo is on line 3.
            ASSERT_EQ(symbols[i].line, static_cast<u32>(3));
            found = true;
            break;
        }
    }
    ASSERT_TRUE(found);
}

DTEST(parseSymbolFileIsRelativePath)
{
    const auto tempDir = std::filesystem::temp_directory_path();
    const auto tempFile = tempDir / "symbols_test_relpath.cpp";
    {
        dc::File file;
        auto openResult = file.open(dc::String(tempFile.string().c_str()), dc::File::Mode::kWrite);
        if (openResult.isOk()) {
            [[maybe_unused]] auto wr = file.write(dc::String("void relPathFunc() {}"));
        }
    }

    Parser parser;
    auto result = parser.parseFile(tempFile, tempDir);
    std::filesystem::remove(tempFile);

    ASSERT_TRUE(result.isOk());
    const auto syms = dc::move(result).unwrap();

    bool found = false;
    for (u64 i = 0; i < syms.getSize(); ++i) {
        if (syms[i].name == "relPathFunc") {
            // The file field must be a relative path (just the filename, no absolute prefix).
            const dc::StringView fileView = syms[i].file.toView();
            // A relative path must not start with a drive letter or slash.
            ASSERT_TRUE(fileView.getSize() > static_cast<usize>(0));
            ASSERT_TRUE(fileView[0] != '/' && fileView[0] != '\\');
            // Must not be an absolute path (no colon on Windows).
            bool hasColon = false;
            for (u64 j = 0; j < fileView.getSize(); ++j) {
                if (fileView[j] == ':')
                    hasColon = true;
            }
            ASSERT_FALSE(hasColon);
            found = true;
            break;
        }
    }
    ASSERT_TRUE(found);
}

DTEST(parseEmptyFileProducesNoSymbols)
{
    const auto symbols = parseSourceString("");
    ASSERT_EQ(symbols.getSize(), static_cast<usize>(0));
}

DTEST(parseNonExistentFileReturnsError)
{
    const auto fakePath = std::filesystem::temp_directory_path() / "symbols_no_such_file_xyz.cpp";
    std::filesystem::remove(fakePath); // ensure it doesn't exist

    Parser parser;
    auto result = parser.parseFile(fakePath, std::filesystem::temp_directory_path());
    ASSERT_FALSE(result.isOk());
}

DTEST(parseEnumClass)
{
    const auto symbols = parseSourceString("enum class Direction { North, South, East, West };");

    bool found = false;
    for (u64 i = 0; i < symbols.getSize(); ++i) {
        if (symbols[i].name == "Direction") {
            ASSERT_EQ(symbols[i].kind, SymbolKind::Enum);
            found = true;
            break;
        }
    }
    ASSERT_TRUE(found);
}

DTEST(parseMethodDeclarationInsideClass)
{
    const auto symbols = parseSourceString(R"(
class Widget {
    void draw();
    int getWidth() const;
};
)");

    bool foundDraw = false;
    bool foundGetWidth = false;
    for (u64 i = 0; i < symbols.getSize(); ++i) {
        if (symbols[i].name == "draw" && symbols[i].kind == SymbolKind::Function)
            foundDraw = true;
        if (symbols[i].name == "getWidth" && symbols[i].kind == SymbolKind::Function)
            foundGetWidth = true;
    }
    ASSERT_TRUE(foundDraw);
    ASSERT_TRUE(foundGetWidth);
}
