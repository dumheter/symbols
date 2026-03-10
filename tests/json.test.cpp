#include <dc/dtest.hpp>
#include <json.hpp>

using namespace symbols;

DTEST(jsonParseEmptyObject)
{
    auto result = JsonValue::parse("{}");
    ASSERT_TRUE(result.isOk());
    const auto val = dc::move(result).unwrap();
    ASSERT_EQ(val.type(), JsonValue::Type::Object);
}

DTEST(jsonParseSimpleObject)
{
    auto result = JsonValue::parse(R"({"id":42,"method":"query"})");
    ASSERT_TRUE(result.isOk());
    const auto val = dc::move(result).unwrap();
    ASSERT_EQ(val.type(), JsonValue::Type::Object);
    ASSERT_EQ(val.getNumber("id"), 42);
    ASSERT_TRUE(dc::String(val.getString("method")) == "query");
}

DTEST(jsonParseNestedObject)
{
    auto result = JsonValue::parse(R"({"id":1,"method":"query","params":{"pattern":"Foo","limit":100}})");
    ASSERT_TRUE(result.isOk());
    const auto val = dc::move(result).unwrap();
    ASSERT_EQ(val.getNumber("id"), 1);

    const JsonValue* params = val.get("params");
    ASSERT_TRUE(params != nullptr);
    ASSERT_TRUE(dc::String(params->getString("pattern")) == "Foo");
    ASSERT_EQ(params->getNumber("limit"), 100);
}

DTEST(jsonParseArray)
{
    auto result = JsonValue::parse(R"([1,2,3])");
    ASSERT_TRUE(result.isOk());
    const auto val = dc::move(result).unwrap();
    ASSERT_EQ(val.type(), JsonValue::Type::Array);
    ASSERT_EQ(val.arraySize(), static_cast<usize>(3));
    ASSERT_EQ(val.at(0).asNumber(), 1);
    ASSERT_EQ(val.at(1).asNumber(), 2);
    ASSERT_EQ(val.at(2).asNumber(), 3);
}

DTEST(jsonParseBoolAndNull)
{
    auto result = JsonValue::parse(R"({"a":true,"b":false,"c":null})");
    ASSERT_TRUE(result.isOk());
    const auto val = dc::move(result).unwrap();

    const JsonValue* a = val.get("a");
    ASSERT_TRUE(a != nullptr);
    ASSERT_TRUE(a->asBool());

    const JsonValue* b = val.get("b");
    ASSERT_TRUE(b != nullptr);
    ASSERT_FALSE(b->asBool());

    const JsonValue* c = val.get("c");
    ASSERT_TRUE(c != nullptr);
    ASSERT_TRUE(c->isNull());
}

DTEST(jsonParseEscapedString)
{
    auto result = JsonValue::parse(R"({"path":"src\\main.cpp"})");
    ASSERT_TRUE(result.isOk());
    const auto val = dc::move(result).unwrap();
    ASSERT_TRUE(dc::String(val.getString("path")) == "src\\main.cpp");
}

DTEST(jsonSerializeRoundTrip)
{
    auto obj = JsonValue::makeObject();
    obj.set(dc::String("id"), JsonValue::makeNumber(42));
    obj.set(dc::String("name"), JsonValue::makeString(dc::String("hello")));

    auto arr = JsonValue::makeArray();
    arr.pushBack(JsonValue::makeNumber(1));
    arr.pushBack(JsonValue::makeNumber(2));
    obj.set(dc::String("items"), dc::move(arr));

    const dc::String serialized = obj.serialize();

    auto result = JsonValue::parse(dc::StringView(serialized));
    ASSERT_TRUE(result.isOk());
    const auto parsed = dc::move(result).unwrap();
    ASSERT_EQ(parsed.getNumber("id"), 42);
    ASSERT_TRUE(dc::String(parsed.getString("name")) == "hello");

    const JsonValue* items = parsed.get("items");
    ASSERT_TRUE(items != nullptr);
    ASSERT_EQ(items->arraySize(), static_cast<usize>(2));
}

DTEST(jsonEscapeSpecialChars)
{
    const dc::String input("hello\nworld\t\"quoted\"");
    const dc::String escaped = jsonEscapeString(dc::StringView(input));
    ASSERT_TRUE(escaped.getSize() > input.getSize());
    // Ensure the escaped string contains backslash-n, not a literal newline.
    // dc::String has no operator[] -- use toView() which has it.
    const dc::StringView escapedView = escaped.toView();
    bool hasEscapedNewline = false;
    for (u64 i = 0; i < escapedView.getSize() - 1; ++i) {
        if (escapedView[i] == '\\' && escapedView[i + 1] == 'n') {
            hasEscapedNewline = true;
            break;
        }
    }
    ASSERT_TRUE(hasEscapedNewline);
}

// ---------------------------------------------------------------------------
// Error paths
// ---------------------------------------------------------------------------

DTEST(jsonParseInvalidInputReturnsErr)
{
    auto result = JsonValue::parse(dc::StringView("not json at all"));
    ASSERT_FALSE(result.isOk());
}

DTEST(jsonParseEmptyInputReturnsErr)
{
    auto result = JsonValue::parse(dc::StringView(""));
    ASSERT_FALSE(result.isOk());
}

DTEST(jsonParseUnterminatedStringReturnsErr)
{
    auto result = JsonValue::parse(dc::StringView("{\"key\":\"unterminated}"));
    ASSERT_FALSE(result.isOk());
}

DTEST(jsonParseMalformedBoolReturnsErr)
{
    auto result = JsonValue::parse(dc::StringView("tru")); // truncated "true"
    ASSERT_FALSE(result.isOk());
}

DTEST(jsonParseMalformedNullReturnsErr)
{
    auto result = JsonValue::parse(dc::StringView("nul")); // truncated "null"
    ASSERT_FALSE(result.isOk());
}

// ---------------------------------------------------------------------------
// Negative numbers
// ---------------------------------------------------------------------------

DTEST(jsonParseNegativeNumber)
{
    auto result = JsonValue::parse(dc::StringView("{\"x\":-42}"));
    ASSERT_TRUE(result.isOk());
    const auto val = dc::move(result).unwrap();
    ASSERT_EQ(val.getNumber("x"), static_cast<s64>(-42));
}

// ---------------------------------------------------------------------------
// set() upsert behavior
// ---------------------------------------------------------------------------

DTEST(jsonObjectSetOverwritesExistingKey)
{
    auto obj = JsonValue::makeObject();
    obj.set(dc::String("k"), JsonValue::makeNumber(1));
    obj.set(dc::String("k"), JsonValue::makeNumber(99)); // overwrite

    ASSERT_EQ(obj.getNumber("k"), static_cast<s64>(99));
}

// ---------------------------------------------------------------------------
// Wrong-type accessor defaults
// ---------------------------------------------------------------------------

DTEST(jsonAccessorDefaultsOnWrongType)
{
    const auto num = JsonValue::makeNumber(7);
    // asBool() on a Number should return the default (false).
    ASSERT_FALSE(num.asBool());
    // asString() on a Number should return an empty string.
    ASSERT_EQ(num.asString().getSize(), static_cast<usize>(0));

    const auto b = JsonValue::makeBool(true);
    // asNumber() on a Bool should return the default (0).
    ASSERT_EQ(b.asNumber(), static_cast<s64>(0));
}

// ---------------------------------------------------------------------------
// Missing key accessors
// ---------------------------------------------------------------------------

DTEST(jsonGetMissingKeyReturnsNull)
{
    auto result = JsonValue::parse(dc::StringView("{\"a\":1}"));
    ASSERT_TRUE(result.isOk());
    const auto val = dc::move(result).unwrap();
    ASSERT_TRUE(val.get("nonexistent") == nullptr);
}

DTEST(jsonGetNumberMissingKeyReturnsZero)
{
    auto result = JsonValue::parse(dc::StringView("{}"));
    ASSERT_TRUE(result.isOk());
    const auto val = dc::move(result).unwrap();
    ASSERT_EQ(val.getNumber("missing"), static_cast<s64>(0));
}

DTEST(jsonGetStringMissingKeyReturnsEmpty)
{
    auto result = JsonValue::parse(dc::StringView("{}"));
    ASSERT_TRUE(result.isOk());
    const auto val = dc::move(result).unwrap();
    ASSERT_EQ(val.getString("missing").getSize(), static_cast<usize>(0));
}

// ---------------------------------------------------------------------------
// \uXXXX unicode escape replacement (task 6a)
// ---------------------------------------------------------------------------

DTEST(jsonParseUnicodeEscapeReplacedWithQuestionMark)
{
    // The parser stubs \uXXXX as "?" — verify that behaviour.
    auto result = JsonValue::parse(dc::StringView(R"({"val":"\u0041"})"));
    ASSERT_TRUE(result.isOk());
    const auto val = dc::move(result).unwrap();
    // \u0041 is 'A' but the parser substitutes '?'.
    ASSERT_TRUE(dc::String(val.getString("val")) == "?");
}

// ---------------------------------------------------------------------------
// Float / decimal truncation (task 6b)
// ---------------------------------------------------------------------------

DTEST(jsonParseDecimalNumberTruncatesToInteger)
{
    auto result = JsonValue::parse(dc::StringView(R"({"x":3.14})"));
    ASSERT_TRUE(result.isOk());
    const auto val = dc::move(result).unwrap();
    // The parser reads s64 via atoll — fractional part is silently truncated.
    ASSERT_EQ(val.getNumber("x"), static_cast<s64>(3));
}

DTEST(jsonParseNegativeDecimalTruncatesToInteger)
{
    auto result = JsonValue::parse(dc::StringView(R"({"x":-9.99})"));
    ASSERT_TRUE(result.isOk());
    const auto val = dc::move(result).unwrap();
    ASSERT_EQ(val.getNumber("x"), static_cast<s64>(-9));
}

// ---------------------------------------------------------------------------
// jsonEscapeString — full coverage of all 7 escape sequences (task 6c)
// ---------------------------------------------------------------------------

DTEST(jsonEscapeStringEscapesBackspace)
{
    const dc::String input("\b");
    const dc::String escaped = jsonEscapeString(dc::StringView(input));
    ASSERT_TRUE(escaped == "\\b");
}

DTEST(jsonEscapeStringEscapesFormFeed)
{
    const dc::String input("\f");
    const dc::String escaped = jsonEscapeString(dc::StringView(input));
    ASSERT_TRUE(escaped == "\\f");
}

DTEST(jsonEscapeStringEscapesCarriageReturn)
{
    const dc::String input("\r");
    const dc::String escaped = jsonEscapeString(dc::StringView(input));
    ASSERT_TRUE(escaped == "\\r");
}

DTEST(jsonEscapeStringEscapesBackslash)
{
    const dc::String input("\\");
    const dc::String escaped = jsonEscapeString(dc::StringView(input));
    ASSERT_TRUE(escaped == "\\\\");
}

DTEST(jsonEscapeStringEscapesAllSevenChars)
{
    // Build a string containing all 7 special chars and verify the escaped
    // version has the right length (each single char becomes a 2-char sequence).
    const dc::String input("\"\\\b\f\n\r\t");
    const dc::String escaped = jsonEscapeString(dc::StringView(input));
    // 7 input chars → 14 output chars.
    ASSERT_EQ(escaped.getSize(), static_cast<usize>(14));
}

// ---------------------------------------------------------------------------
// JsonValue copy constructor (task 6d)
// ---------------------------------------------------------------------------

DTEST(jsonValueCopyConstructorProducesIndependentCopy)
{
    auto original = JsonValue::makeObject();
    original.set(dc::String("key"), JsonValue::makeNumber(42));

    // Copy-construct.
    const JsonValue copy(original);
    ASSERT_EQ(copy.getNumber("key"), static_cast<s64>(42));

    // Mutate the copy — original must be unaffected.
    // (We can only verify via serialise/re-parse since set() modifies in place.)
    original.set(dc::String("key"), JsonValue::makeNumber(99));
    ASSERT_EQ(original.getNumber("key"), static_cast<s64>(99));
    ASSERT_EQ(copy.getNumber("key"), static_cast<s64>(42));
}

DTEST(jsonValueCopyConstructorCopiesArray)
{
    auto original = JsonValue::makeArray();
    original.pushBack(JsonValue::makeNumber(1));
    original.pushBack(JsonValue::makeNumber(2));

    const JsonValue copy(original);
    ASSERT_EQ(copy.arraySize(), static_cast<usize>(2));
    ASSERT_EQ(copy.at(0).asNumber(), static_cast<s64>(1));
    ASSERT_EQ(copy.at(1).asNumber(), static_cast<s64>(2));
}

// ---------------------------------------------------------------------------
// JsonValue copy assignment (task 6e)
// ---------------------------------------------------------------------------

DTEST(jsonValueCopyAssignmentOverwritesExistingValue)
{
    auto obj = JsonValue::makeObject();
    obj.set(dc::String("x"), JsonValue::makeNumber(7));

    // Assign a Number over the Object.
    const JsonValue num = JsonValue::makeNumber(55);
    obj = num;

    ASSERT_EQ(obj.type(), JsonValue::Type::Number);
    ASSERT_EQ(obj.asNumber(), static_cast<s64>(55));
}

DTEST(jsonValueCopyAssignmentSelfAssignIsNoop)
{
    auto val = JsonValue::makeNumber(123);
    // Self-assignment must not corrupt the object.
    // Use a reference to avoid the -Wself-assign compiler warning.
    JsonValue& ref = val;
    val = ref;
    ASSERT_EQ(val.asNumber(), static_cast<s64>(123));
}
