#pragma once

#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace mf::json {

enum class Type {
    Null,
    Bool,
    Number,
    String,
    Array,
    Object,
};

class ParseError : public std::runtime_error {
public:
    explicit ParseError(const std::string& message) : std::runtime_error(message) {}
};

class Value {
public:
    Value() = default;

    static Value makeBool(bool value);
    static Value makeNumber(double value);
    static Value makeString(std::string value);
    static Value makeArray(std::vector<Value> value);
    static Value makeObject(std::vector<std::pair<std::string, Value>> value);

    Type type() const { return type_; }
    bool isNull() const { return type_ == Type::Null; }
    bool isBool() const { return type_ == Type::Bool; }
    bool isNumber() const { return type_ == Type::Number; }
    bool isString() const { return type_ == Type::String; }
    bool isArray() const { return type_ == Type::Array; }
    bool isObject() const { return type_ == Type::Object; }

    bool asBool(bool fallback = false) const;
    double asNumber(double fallback = 0.0) const;
    int64_t asInt64(int64_t fallback = 0) const;
    uint64_t asUInt64(uint64_t fallback = 0) const;
    const std::string& asString() const;

    const std::vector<Value>& asArray() const;
    const std::vector<std::pair<std::string, Value>>& members() const;

    const Value* find(std::string_view key) const;
    const Value& at(std::string_view key) const;
    bool set(std::string_view key, Value value);

    std::string getString(std::string_view key, std::string fallback = {}) const;
    int64_t getInt64(std::string_view key, int64_t fallback = 0) const;
    uint64_t getUInt64(std::string_view key, uint64_t fallback = 0) const;
    bool getBool(std::string_view key, bool fallback = false) const;

private:
    Type type_ = Type::Null;
    bool bool_ = false;
    double number_ = 0.0;
    std::string string_;
    std::vector<Value> array_;
    std::vector<std::pair<std::string, Value>> object_;
};

Value parse(std::string_view text);
std::string stringify(const Value& value, bool pretty = true);

}  // namespace mf::json
