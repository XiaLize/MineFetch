#include "json.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <sstream>
#include <unordered_set>

namespace mf::json {
namespace {

constexpr int kMaxDepth = 256;

class Parser {
public:
    explicit Parser(std::string_view text) : text_(text) {}

    Value parseDocument() {
        skipWhitespace();
        Value result = parseValue(0);
        skipWhitespace();
        if (position_ != text_.size()) {
            fail("文档末尾存在多余内容");
        }
        return result;
    }

private:
    [[noreturn]] void fail(const std::string& message) const {
        size_t line = 1;
        size_t column = 1;
        for (size_t i = 0; i < position_ && i < text_.size(); ++i) {
            if (text_[i] == '\n') {
                ++line;
                column = 1;
            } else {
                ++column;
            }
        }
        std::ostringstream out;
        out << "JSON 解析失败（第 " << line << " 行，第 " << column << " 列）: " << message;
        throw ParseError(out.str());
    }

    char peek() const {
        return position_ < text_.size() ? text_[position_] : '\0';
    }

    char take() {
        if (position_ >= text_.size()) {
            fail("内容意外结束");
        }
        return text_[position_++];
    }

    void expect(std::string_view token) {
        if (text_.substr(position_, token.size()) != token) {
            fail("期望 '" + std::string(token) + "'");
        }
        position_ += token.size();
    }

    void skipWhitespace() {
        while (position_ < text_.size()) {
            const char c = text_[position_];
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
                ++position_;
            } else {
                break;
            }
        }
    }

    Value parseValue(int depth) {
        if (depth > kMaxDepth) {
            fail("嵌套层级过深");
        }
        skipWhitespace();
        if (position_ >= text_.size()) {
            fail("期望一个 JSON 值");
        }
        switch (peek()) {
            case 'n':
                expect("null");
                return Value();
            case 't':
                expect("true");
                return Value::makeBool(true);
            case 'f':
                expect("false");
                return Value::makeBool(false);
            case '"':
                return Value::makeString(parseString());
            case '[':
                return parseArray(depth);
            case '{':
                return parseObject(depth);
            default:
                if (peek() == '-' || (peek() >= '0' && peek() <= '9')) {
                    return parseNumber();
                }
                fail("无法识别的值");
        }
    }

    Value parseArray(int depth) {
        expect("[");
        std::vector<Value> values;
        skipWhitespace();
        if (peek() == ']') {
            ++position_;
            return Value::makeArray(std::move(values));
        }
        for (;;) {
            values.push_back(parseValue(depth + 1));
            skipWhitespace();
            char c = take();
            if (c == ']') {
                break;
            }
            if (c != ',') {
                --position_;
                fail("数组元素之间缺少逗号");
            }
            skipWhitespace();
            if (peek() == ']') {
                fail("数组末尾不允许多余逗号");
            }
        }
        return Value::makeArray(std::move(values));
    }

    Value parseObject(int depth) {
        expect("{");
        std::vector<std::pair<std::string, Value>> members;
        std::unordered_set<std::string> keys;
        skipWhitespace();
        if (peek() == '}') {
            ++position_;
            return Value::makeObject(std::move(members));
        }
        for (;;) {
            skipWhitespace();
            if (peek() != '"') {
                fail("对象键必须是字符串");
            }
            std::string key = parseString();
            if (!keys.insert(key).second) {
                fail("对象中存在重复键 '" + key + "'");
            }
            skipWhitespace();
            if (take() != ':') {
                --position_;
                fail("对象键后缺少冒号");
            }
            members.emplace_back(std::move(key), parseValue(depth + 1));
            skipWhitespace();
            char c = take();
            if (c == '}') {
                break;
            }
            if (c != ',') {
                --position_;
                fail("对象成员之间缺少逗号");
            }
            skipWhitespace();
            if (peek() == '}') {
                fail("对象末尾不允许多余逗号");
            }
        }
        return Value::makeObject(std::move(members));
    }

    static void appendUtf8(std::string& out, uint32_t codePoint) {
        if (codePoint <= 0x7F) {
            out.push_back(static_cast<char>(codePoint));
        } else if (codePoint <= 0x7FF) {
            out.push_back(static_cast<char>(0xC0 | (codePoint >> 6)));
            out.push_back(static_cast<char>(0x80 | (codePoint & 0x3F)));
        } else if (codePoint <= 0xFFFF) {
            out.push_back(static_cast<char>(0xE0 | (codePoint >> 12)));
            out.push_back(static_cast<char>(0x80 | ((codePoint >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (codePoint & 0x3F)));
        } else {
            out.push_back(static_cast<char>(0xF0 | (codePoint >> 18)));
            out.push_back(static_cast<char>(0x80 | ((codePoint >> 12) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | ((codePoint >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (codePoint & 0x3F)));
        }
    }

    uint32_t parseHex4() {
        if (position_ + 4 > text_.size()) {
            fail("Unicode 转义不完整");
        }
        uint32_t value = 0;
        for (int i = 0; i < 4; ++i) {
            const char c = text_[position_++];
            value <<= 4;
            if (c >= '0' && c <= '9') {
                value |= static_cast<uint32_t>(c - '0');
            } else if (c >= 'a' && c <= 'f') {
                value |= static_cast<uint32_t>(c - 'a' + 10);
            } else if (c >= 'A' && c <= 'F') {
                value |= static_cast<uint32_t>(c - 'A' + 10);
            } else {
                fail("Unicode 转义包含非法字符");
            }
        }
        return value;
    }

    std::string parseString() {
        if (take() != '"') {
            fail("期望字符串");
        }
        std::string out;
        for (;;) {
            if (position_ >= text_.size()) {
                fail("字符串没有结束引号");
            }
            const unsigned char c = static_cast<unsigned char>(text_[position_++]);
            if (c == '"') {
                break;
            }
            if (c < 0x20) {
                fail("字符串中包含未转义的控制字符");
            }
            if (c != '\\') {
                out.push_back(static_cast<char>(c));
                continue;
            }
            if (position_ >= text_.size()) {
                fail("转义序列不完整");
            }
            const char escape = text_[position_++];
            switch (escape) {
                case '"':
                    out.push_back('"');
                    break;
                case '\\':
                    out.push_back('\\');
                    break;
                case '/':
                    out.push_back('/');
                    break;
                case 'b':
                    out.push_back('\b');
                    break;
                case 'f':
                    out.push_back('\f');
                    break;
                case 'n':
                    out.push_back('\n');
                    break;
                case 'r':
                    out.push_back('\r');
                    break;
                case 't':
                    out.push_back('\t');
                    break;
                case 'u': {
                    uint32_t codePoint = parseHex4();
                    if (codePoint >= 0xD800 && codePoint <= 0xDBFF) {
                        if (position_ + 2 > text_.size() || text_[position_] != '\\' ||
                            text_[position_ + 1] != 'u') {
                            fail("高代理项后缺少低代理项");
                        }
                        position_ += 2;
                        const uint32_t low = parseHex4();
                        if (low < 0xDC00 || low > 0xDFFF) {
                            fail("代理项组合无效");
                        }
                        codePoint = 0x10000 + ((codePoint - 0xD800) << 10) + (low - 0xDC00);
                    } else if (codePoint >= 0xDC00 && codePoint <= 0xDFFF) {
                        fail("字符串中出现孤立的低代理项");
                    }
                    appendUtf8(out, codePoint);
                    break;
                }
                default:
                    fail("未知的转义字符");
            }
        }
        return out;
    }

    Value parseNumber() {
        const size_t start = position_;
        if (peek() == '-') {
            ++position_;
        }
        if (peek() == '0') {
            ++position_;
            if (peek() >= '0' && peek() <= '9') {
                fail("数字不允许包含前导零");
            }
        } else if (peek() >= '1' && peek() <= '9') {
            while (peek() >= '0' && peek() <= '9') {
                ++position_;
            }
        } else {
            fail("数字格式无效");
        }
        if (peek() == '.') {
            ++position_;
            if (!(peek() >= '0' && peek() <= '9')) {
                fail("小数点后缺少数字");
            }
            while (peek() >= '0' && peek() <= '9') {
                ++position_;
            }
        }
        if (peek() == 'e' || peek() == 'E') {
            ++position_;
            if (peek() == '+' || peek() == '-') {
                ++position_;
            }
            if (!(peek() >= '0' && peek() <= '9')) {
                fail("指数部分缺少数字");
            }
            while (peek() >= '0' && peek() <= '9') {
                ++position_;
            }
        }
        const std::string token(text_.substr(start, position_ - start));
        char* end = nullptr;
        const double value = std::strtod(token.c_str(), &end);
        if (end == nullptr || *end != '\0') {
            fail("数字格式无效");
        }
        if (!std::isfinite(value)) {
            fail("数字超出可表示范围");
        }
        return Value::makeNumber(value);
    }

    std::string_view text_;
    size_t position_ = 0;
};

void appendEscaped(std::string& out, std::string_view text) {
    static constexpr char kHex[] = "0123456789abcdef";
    out.push_back('"');
    for (unsigned char c : text) {
        switch (c) {
            case '"':
                out += "\\\"";
                break;
            case '\\':
                out += "\\\\";
                break;
            case '\b':
                out += "\\b";
                break;
            case '\f':
                out += "\\f";
                break;
            case '\n':
                out += "\\n";
                break;
            case '\r':
                out += "\\r";
                break;
            case '\t':
                out += "\\t";
                break;
            default:
                if (c < 0x20) {
                    out += "\\u00";
                    out.push_back(kHex[c >> 4]);
                    out.push_back(kHex[c & 0x0F]);
                } else {
                    out.push_back(static_cast<char>(c));
                }
                break;
        }
    }
    out.push_back('"');
}

void appendNumber(std::string& out, double value) {
    if (!std::isfinite(value)) {
        out += "0";
        return;
    }
    char buffer[64];
    if (value == std::floor(value) && std::fabs(value) < 1e15) {
        std::snprintf(buffer, sizeof(buffer), "%.0f", value);
    } else {
        std::snprintf(buffer, sizeof(buffer), "%.17g", value);
    }
    out += buffer;
}

void appendIndent(std::string& out, int depth) {
    out.push_back('\n');
    out.append(static_cast<size_t>(depth) * 2, ' ');
}

void appendValue(std::string& out, const Value& value, bool pretty, int depth) {
    switch (value.type()) {
        case Type::Null:
            out += "null";
            return;
        case Type::Bool:
            out += value.asBool() ? "true" : "false";
            return;
        case Type::Number:
            appendNumber(out, value.asNumber());
            return;
        case Type::String:
            appendEscaped(out, value.asString());
            return;
        case Type::Array: {
            const auto& items = value.asArray();
            out.push_back('[');
            for (size_t i = 0; i < items.size(); ++i) {
                if (i != 0) {
                    out.push_back(',');
                }
                if (pretty) {
                    appendIndent(out, depth + 1);
                }
                appendValue(out, items[i], pretty, depth + 1);
            }
            if (pretty && !items.empty()) {
                appendIndent(out, depth);
            }
            out.push_back(']');
            return;
        }
        case Type::Object: {
            const auto& members = value.members();
            out.push_back('{');
            for (size_t i = 0; i < members.size(); ++i) {
                if (i != 0) {
                    out.push_back(',');
                }
                if (pretty) {
                    appendIndent(out, depth + 1);
                }
                appendEscaped(out, members[i].first);
                out.push_back(':');
                if (pretty) {
                    out.push_back(' ');
                }
                appendValue(out, members[i].second, pretty, depth + 1);
            }
            if (pretty && !members.empty()) {
                appendIndent(out, depth);
            }
            out.push_back('}');
            return;
        }
    }
}

}  // namespace

Value Value::makeBool(bool value) {
    Value result;
    result.type_ = Type::Bool;
    result.bool_ = value;
    return result;
}

Value Value::makeNumber(double value) {
    Value result;
    result.type_ = Type::Number;
    result.number_ = value;
    return result;
}

Value Value::makeString(std::string value) {
    Value result;
    result.type_ = Type::String;
    result.string_ = std::move(value);
    return result;
}

Value Value::makeArray(std::vector<Value> value) {
    Value result;
    result.type_ = Type::Array;
    result.array_ = std::move(value);
    return result;
}

Value Value::makeObject(std::vector<std::pair<std::string, Value>> value) {
    Value result;
    result.type_ = Type::Object;
    result.object_ = std::move(value);
    return result;
}

bool Value::asBool(bool fallback) const {
    return type_ == Type::Bool ? bool_ : fallback;
}

double Value::asNumber(double fallback) const {
    return type_ == Type::Number ? number_ : fallback;
}

int64_t Value::asInt64(int64_t fallback) const {
    if (type_ != Type::Number || number_ < static_cast<double>(std::numeric_limits<int64_t>::min()) ||
        number_ > static_cast<double>(std::numeric_limits<int64_t>::max())) {
        return fallback;
    }
    return static_cast<int64_t>(number_);
}

uint64_t Value::asUInt64(uint64_t fallback) const {
    if (type_ != Type::Number || number_ < 0.0 ||
        number_ > static_cast<double>(std::numeric_limits<uint64_t>::max())) {
        return fallback;
    }
    return static_cast<uint64_t>(number_);
}

const std::string& Value::asString() const {
    if (type_ != Type::String) {
        throw std::runtime_error("JSON 值不是字符串");
    }
    return string_;
}

const std::vector<Value>& Value::asArray() const {
    if (type_ != Type::Array) {
        throw std::runtime_error("JSON 值不是数组");
    }
    return array_;
}

const std::vector<std::pair<std::string, Value>>& Value::members() const {
    if (type_ != Type::Object) {
        throw std::runtime_error("JSON 值不是对象");
    }
    return object_;
}

const Value* Value::find(std::string_view key) const {
    if (type_ != Type::Object) {
        return nullptr;
    }
    for (const auto& member : object_) {
        if (member.first == key) {
            return &member.second;
        }
    }
    return nullptr;
}

const Value& Value::at(std::string_view key) const {
    const Value* value = find(key);
    if (value == nullptr) {
        throw std::runtime_error("JSON 对象缺少键 '" + std::string(key) + "'");
    }
    return *value;
}

bool Value::set(std::string_view key, Value value) {
    if (type_ != Type::Object) {
        return false;
    }
    for (auto& member : object_) {
        if (member.first == key) {
            member.second = std::move(value);
            return true;
        }
    }
    object_.emplace_back(std::string(key), std::move(value));
    return true;
}

std::string Value::getString(std::string_view key, std::string fallback) const {
    const Value* value = find(key);
    return value != nullptr && value->isString() ? value->asString() : std::move(fallback);
}

int64_t Value::getInt64(std::string_view key, int64_t fallback) const {
    const Value* value = find(key);
    return value != nullptr ? value->asInt64(fallback) : fallback;
}

uint64_t Value::getUInt64(std::string_view key, uint64_t fallback) const {
    const Value* value = find(key);
    return value != nullptr ? value->asUInt64(fallback) : fallback;
}

bool Value::getBool(std::string_view key, bool fallback) const {
    const Value* value = find(key);
    return value != nullptr ? value->asBool(fallback) : fallback;
}

Value parse(std::string_view text) {
    if (text.size() >= 3 && static_cast<unsigned char>(text[0]) == 0xEF &&
        static_cast<unsigned char>(text[1]) == 0xBB &&
        static_cast<unsigned char>(text[2]) == 0xBF) {
        text.remove_prefix(3);
    }
    Parser parser(text);
    return parser.parseDocument();
}

std::string stringify(const Value& value, bool pretty) {
    std::string result;
    appendValue(result, value, pretty, 0);
    if (pretty) {
        result.push_back('\n');
    }
    return result;
}

}  // namespace mf::json
