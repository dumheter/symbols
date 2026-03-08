#include <json.hpp>

#include <dc/assert.hpp>

#include <cstdio>
#include <cstring>

namespace symbols {

// ============================================================================
// JsonValue implementation
// ============================================================================

JsonValue::JsonValue()
    : m_type(Type::Null)
    , m_number(0)
    , m_bool(false)
    , m_array(nullptr)
    , m_objectKeys(nullptr)
    , m_objectValues(nullptr)
{
}

JsonValue::~JsonValue()
{
    cleanup();
}

auto JsonValue::cleanup() -> void
{
    delete m_array;
    m_array = nullptr;
    delete m_objectKeys;
    m_objectKeys = nullptr;
    delete m_objectValues;
    m_objectValues = nullptr;
}

auto JsonValue::copyFrom(const JsonValue& other) -> void
{
    m_type = other.m_type;
    m_number = other.m_number;
    m_bool = other.m_bool;
    m_string = other.m_string;

    if (other.m_array)
        m_array = new dc::List<JsonValue, 1>(*other.m_array);
    if (other.m_objectKeys)
        m_objectKeys = new dc::List<dc::String>(*other.m_objectKeys);
    if (other.m_objectValues)
        m_objectValues = new dc::List<JsonValue, 1>(*other.m_objectValues);
}

JsonValue::JsonValue(const JsonValue& other)
    : m_type(Type::Null)
    , m_number(0)
    , m_bool(false)
    , m_array(nullptr)
    , m_objectKeys(nullptr)
    , m_objectValues(nullptr)
{
    copyFrom(other);
}

auto JsonValue::operator=(const JsonValue& other) -> JsonValue&
{
    if (this != &other) {
        cleanup();
        m_array = nullptr;
        m_objectKeys = nullptr;
        m_objectValues = nullptr;
        copyFrom(other);
    }
    return *this;
}

JsonValue::JsonValue(JsonValue&& other) noexcept
    : m_type(other.m_type)
    , m_number(other.m_number)
    , m_bool(other.m_bool)
    , m_string(dc::move(other.m_string))
    , m_array(other.m_array)
    , m_objectKeys(other.m_objectKeys)
    , m_objectValues(other.m_objectValues)
{
    other.m_type = Type::Null;
    other.m_array = nullptr;
    other.m_objectKeys = nullptr;
    other.m_objectValues = nullptr;
}

auto JsonValue::operator=(JsonValue&& other) noexcept -> JsonValue&
{
    if (this != &other) {
        cleanup();
        m_type = other.m_type;
        m_number = other.m_number;
        m_bool = other.m_bool;
        m_string = dc::move(other.m_string);
        m_array = other.m_array;
        m_objectKeys = other.m_objectKeys;
        m_objectValues = other.m_objectValues;
        other.m_type = Type::Null;
        other.m_array = nullptr;
        other.m_objectKeys = nullptr;
        other.m_objectValues = nullptr;
    }
    return *this;
}

auto JsonValue::makeNull() -> JsonValue
{
    return JsonValue();
}

auto JsonValue::makeBool(bool value) -> JsonValue
{
    JsonValue v;
    v.m_type = Type::Bool;
    v.m_bool = value;
    return v;
}

auto JsonValue::makeNumber(s64 value) -> JsonValue
{
    JsonValue v;
    v.m_type = Type::Number;
    v.m_number = value;
    return v;
}

auto JsonValue::makeString(dc::String value) -> JsonValue
{
    JsonValue v;
    v.m_type = Type::String;
    v.m_string = dc::move(value);
    return v;
}

auto JsonValue::makeArray() -> JsonValue
{
    JsonValue v;
    v.m_type = Type::Array;
    v.m_array = new dc::List<JsonValue, 1>();
    return v;
}

auto JsonValue::makeObject() -> JsonValue
{
    JsonValue v;
    v.m_type = Type::Object;
    v.m_objectKeys = new dc::List<dc::String>();
    v.m_objectValues = new dc::List<JsonValue, 1>();
    return v;
}

auto JsonValue::type() const -> Type
{
    return m_type;
}
auto JsonValue::isNull() const -> bool
{
    return m_type == Type::Null;
}
auto JsonValue::asBool() const -> bool
{
    return m_type == Type::Bool && m_bool;
}

auto JsonValue::asNumber() const -> s64
{
    return m_type == Type::Number ? m_number : 0;
}

auto JsonValue::asString() const -> const dc::String&
{
    static const dc::String kEmpty;
    return m_type == Type::String ? m_string : kEmpty;
}

auto JsonValue::pushBack(JsonValue value) -> void
{
    DC_ASSERT(m_type == Type::Array, "pushBack called on non-array");
    if (m_array)
        m_array->add(dc::move(value));
}

auto JsonValue::arraySize() const -> usize
{
    return (m_type == Type::Array && m_array) ? m_array->getSize() : 0;
}

auto JsonValue::at(usize index) const -> const JsonValue&
{
    DC_ASSERT(m_type == Type::Array && m_array, "at() called on non-array");
    return (*m_array)[index];
}

auto JsonValue::set(dc::String key, JsonValue value) -> void
{
    DC_ASSERT(m_type == Type::Object, "set() called on non-object");
    if (!m_objectKeys || !m_objectValues)
        return;
    // Update existing key if present.
    for (u64 i = 0; i < m_objectKeys->getSize(); ++i) {
        if ((*m_objectKeys)[i] == key) {
            (*m_objectValues)[i] = dc::move(value);
            return;
        }
    }
    m_objectKeys->add(dc::move(key));
    m_objectValues->add(dc::move(value));
}

auto JsonValue::get(dc::StringView key) const -> const JsonValue*
{
    if (m_type != Type::Object || !m_objectKeys || !m_objectValues)
        return nullptr;
    for (u64 i = 0; i < m_objectKeys->getSize(); ++i) {
        if ((*m_objectKeys)[i] == key.c_str())
            return &(*m_objectValues)[i];
    }
    return nullptr;
}

auto JsonValue::getString(dc::StringView key) const -> dc::StringView
{
    const JsonValue* val = get(key);
    if (val && val->m_type == Type::String)
        return dc::StringView(val->m_string);
    return dc::StringView();
}

auto JsonValue::getNumber(dc::StringView key) const -> s64
{
    const JsonValue* val = get(key);
    return (val && val->m_type == Type::Number) ? val->m_number : 0;
}

// ============================================================================
// Serialization
// ============================================================================

auto jsonEscapeString(dc::StringView input) -> dc::String
{
    dc::String result;
    for (u64 i = 0; i < input.getSize(); ++i) {
        const char c = input[i]; // StringView has operator[]
        switch (c) {
        case '"':
            result += "\\\"";
            break;
        case '\\':
            result += "\\\\";
            break;
        case '\b':
            result += "\\b";
            break;
        case '\f':
            result += "\\f";
            break;
        case '\n':
            result += "\\n";
            break;
        case '\r':
            result += "\\r";
            break;
        case '\t':
            result += "\\t";
            break;
        default:
            result += dc::String(&c, 1);
            break;
        }
    }
    return result;
}

auto JsonValue::serialize() const -> dc::String
{
    switch (m_type) {
    case Type::Null:
        return dc::String("null");
    case Type::Bool:
        return dc::String(m_bool ? "true" : "false");
    case Type::Number: {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%lld", static_cast<long long>(m_number));
        return dc::String(buf);
    }
    case Type::String: {
        dc::String r("\"");
        r += jsonEscapeString(dc::StringView(m_string));
        r += "\"";
        return r;
    }
    case Type::Array: {
        dc::String r("[");
        if (m_array) {
            for (u64 i = 0; i < m_array->getSize(); ++i) {
                if (i > 0)
                    r += ",";
                r += (*m_array)[i].serialize();
            }
        }
        r += "]";
        return r;
    }
    case Type::Object: {
        dc::String r("{");
        if (m_objectKeys && m_objectValues) {
            for (u64 i = 0; i < m_objectKeys->getSize(); ++i) {
                if (i > 0)
                    r += ",";
                r += "\"";
                r += jsonEscapeString(dc::StringView((*m_objectKeys)[i]));
                r += "\":";
                r += (*m_objectValues)[i].serialize();
            }
        }
        r += "}";
        return r;
    }
    }
    return dc::String("null");
}

// ============================================================================
// Parsing
// ============================================================================

namespace {

    struct ParseError {
        dc::String message;
    };

    struct JsonParser {
        const char* data;
        s64 pos;
        s64 len;

        auto skipWhitespace() -> void
        {
            while (pos < len) {
                const char c = data[pos];
                if (c == ' ' || c == '\t' || c == '\n' || c == '\r')
                    ++pos;
                else
                    break;
            }
        }

        auto peek() const -> char
        {
            return pos < len ? data[pos] : '\0';
        }

        // Returns false and sets err if the expected char is not found.
        auto expect(char c, dc::String& err) -> bool
        {
            skipWhitespace();
            if (pos >= len || data[pos] != c) {
                char buf[64];
                std::snprintf(buf, sizeof(buf), "Expected '%c' at position %lld", c, static_cast<long long>(pos));
                err = dc::String(buf);
                return false;
            }
            ++pos;
            return true;
        }

        auto parseValue(JsonValue& out, dc::String& err) -> bool
        {
            skipWhitespace();
            if (pos >= len) {
                err = dc::String("Unexpected end of input");
                return false;
            }
            const char c = peek();
            if (c == '"')
                return parseString(out, err);
            if (c == '{')
                return parseObject(out, err);
            if (c == '[')
                return parseArray(out, err);
            if (c == 't' || c == 'f')
                return parseBool(out, err);
            if (c == 'n')
                return parseNull(out, err);
            if (c == '-' || (c >= '0' && c <= '9'))
                return parseNumber(out, err);
            char buf[64];
            std::snprintf(buf, sizeof(buf), "Unexpected '%c' at position %lld", c, static_cast<long long>(pos));
            err = dc::String(buf);
            return false;
        }

        auto parseString(JsonValue& out, dc::String& err) -> bool
        {
            ++pos; // skip '"'
            dc::String result;
            while (pos < len) {
                const char c = data[pos++];
                if (c == '"') {
                    out = JsonValue::makeString(dc::move(result));
                    return true;
                }
                if (c == '\\') {
                    if (pos >= len) {
                        err = dc::String("Unexpected end of escape");
                        return false;
                    }
                    const char e = data[pos++];
                    switch (e) {
                    case '"':
                        result += "\"";
                        break;
                    case '\\':
                        result += "\\";
                        break;
                    case '/':
                        result += "/";
                        break;
                    case 'b':
                        result += "\b";
                        break;
                    case 'f':
                        result += "\f";
                        break;
                    case 'n':
                        result += "\n";
                        break;
                    case 'r':
                        result += "\r";
                        break;
                    case 't':
                        result += "\t";
                        break;
                    case 'u':
                        pos += 4;
                        result += "?";
                        break; // skip unicode
                    default:
                        result += dc::String(&e, 1);
                        break;
                    }
                } else {
                    result += dc::String(&c, 1);
                }
            }
            err = dc::String("Unterminated string");
            return false;
        }

        auto parseNumber(JsonValue& out, dc::String& err) -> bool
        {
            const s64 start = pos;
            if (peek() == '-')
                ++pos;
            while (pos < len && data[pos] >= '0' && data[pos] <= '9')
                ++pos;
            if (pos < len && data[pos] == '.') { // skip decimal part
                ++pos;
                while (pos < len && data[pos] >= '0' && data[pos] <= '9')
                    ++pos;
            }
            char buf[64];
            const s64 numLen = pos - start;
            if (numLen >= static_cast<s64>(sizeof(buf))) {
                err = dc::String("Number too long");
                return false;
            }
            std::memcpy(buf, data + start, static_cast<usize>(numLen));
            buf[numLen] = '\0';
            out = JsonValue::makeNumber(std::atoll(buf));
            return true;
        }

        auto parseBool(JsonValue& out, [[maybe_unused]] dc::String& err) -> bool
        {
            if (pos + 4 <= len && std::strncmp(data + pos, "true", 4) == 0) {
                pos += 4;
                out = JsonValue::makeBool(true);
                return true;
            }
            if (pos + 5 <= len && std::strncmp(data + pos, "false", 5) == 0) {
                pos += 5;
                out = JsonValue::makeBool(false);
                return true;
            }
            err = dc::String("Invalid boolean");
            return false;
        }

        auto parseNull(JsonValue& out, [[maybe_unused]] dc::String& err) -> bool
        {
            if (pos + 4 <= len && std::strncmp(data + pos, "null", 4) == 0) {
                pos += 4;
                out = JsonValue::makeNull();
                return true;
            }
            err = dc::String("Invalid null");
            return false;
        }

        auto parseArray(JsonValue& out, dc::String& err) -> bool
        {
            ++pos; // skip '['
            out = JsonValue::makeArray();
            skipWhitespace();
            if (peek() == ']') {
                ++pos;
                return true;
            }
            while (true) {
                JsonValue elem;
                if (!parseValue(elem, err))
                    return false;
                out.pushBack(dc::move(elem));
                skipWhitespace();
                if (peek() == ']') {
                    ++pos;
                    return true;
                }
                if (!expect(',', err))
                    return false;
            }
        }

        auto parseObject(JsonValue& out, dc::String& err) -> bool
        {
            ++pos; // skip '{'
            out = JsonValue::makeObject();
            skipWhitespace();
            if (peek() == '}') {
                ++pos;
                return true;
            }
            while (true) {
                skipWhitespace();
                if (peek() != '"') {
                    err = dc::String("Expected string key");
                    return false;
                }
                JsonValue keyVal;
                if (!parseString(keyVal, err))
                    return false;
                dc::String key = keyVal.asString(); // copy the string value out
                if (!expect(':', err))
                    return false;
                JsonValue val;
                if (!parseValue(val, err))
                    return false;
                out.set(dc::move(key), dc::move(val));
                skipWhitespace();
                if (peek() == '}') {
                    ++pos;
                    return true;
                }
                if (!expect(',', err))
                    return false;
            }
        }
    };

} // anonymous namespace

auto JsonValue::parse(dc::StringView input) -> dc::Result<JsonValue, dc::String>
{
    JsonParser p;
    p.data = input.c_str();
    p.pos = 0;
    p.len = static_cast<s64>(input.getSize());

    JsonValue result;
    dc::String err;
    if (!p.parseValue(result, err))
        return dc::Err<dc::String>(dc::move(err));
    return dc::Ok<JsonValue>(dc::move(result));
}

} // namespace symbols
