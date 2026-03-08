#pragma once

#include <dc/list.hpp>
#include <dc/map.hpp>
#include <dc/result.hpp>
#include <dc/string.hpp>
#include <dc/types.hpp>

namespace symbols {

/// Simple JSON value for our protocol.
/// Supports: string, number (s64), bool, null, object, array.
class JsonValue {
public:
    enum class Type : u8 {
        Null,
        Bool,
        Number,
        String,
        Array,
        Object,
    };

    JsonValue();
    ~JsonValue();
    JsonValue(const JsonValue& other);
    auto operator=(const JsonValue& other) -> JsonValue&;
    JsonValue(JsonValue&& other) noexcept;
    auto operator=(JsonValue&& other) noexcept -> JsonValue&;

    // Construction helpers.
    static auto makeNull() -> JsonValue;
    static auto makeBool(bool value) -> JsonValue;
    static auto makeNumber(s64 value) -> JsonValue;
    static auto makeString(dc::String value) -> JsonValue;
    static auto makeArray() -> JsonValue;
    static auto makeObject() -> JsonValue;

    [[nodiscard]] auto type() const -> Type;
    [[nodiscard]] auto isNull() const -> bool;

    // Accessors (return defaults for wrong type).
    [[nodiscard]] auto asBool() const -> bool;
    [[nodiscard]] auto asNumber() const -> s64;
    [[nodiscard]] auto asString() const -> const dc::String&;

    // Array operations.
    auto pushBack(JsonValue value) -> void;
    [[nodiscard]] auto arraySize() const -> usize;
    [[nodiscard]] auto at(usize index) const -> const JsonValue&;

    // Object operations.
    auto set(dc::String key, JsonValue value) -> void;
    [[nodiscard]] auto get(dc::StringView key) const -> const JsonValue*;
    [[nodiscard]] auto getString(dc::StringView key) const -> dc::StringView;
    [[nodiscard]] auto getNumber(dc::StringView key) const -> s64;

    // Serialization.
    [[nodiscard]] auto serialize() const -> dc::String;

    // Parsing.
    [[nodiscard]] static auto parse(dc::StringView input) -> dc::Result<JsonValue, dc::String>;

private:
    Type m_type;

    // Inline storage for simple types.
    s64 m_number;
    bool m_bool;
    dc::String m_string;

    // Heap-allocated for compound types.
    // N=1 avoids sizeof(JsonValue) in the default template arg while the
    // class definition is still incomplete.
    dc::List<JsonValue, 1>* m_array;
    dc::List<dc::String>* m_objectKeys;
    dc::List<JsonValue, 1>* m_objectValues;

    auto cleanup() -> void;
    auto copyFrom(const JsonValue& other) -> void;
};

/// Escape a string for JSON output.
[[nodiscard]] auto jsonEscapeString(dc::StringView input) -> dc::String;

} // namespace symbols
