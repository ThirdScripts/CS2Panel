#include "core/json.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace cs2n {
namespace {

const JsonValue& nullValue() {
  static const JsonValue value;
  return value;
}

class Parser {
 public:
  explicit Parser(const std::string& text) : text_(text) {}

  bool run(JsonValue& out, std::string& error) {
    skipWhitespace();
    if (!parseValue(out, 0)) {
      error = error_ + " at offset " + std::to_string(position_);
      return false;
    }
    skipWhitespace();
    if (position_ != text_.size()) {
      error = "Trailing characters at offset " + std::to_string(position_);
      return false;
    }
    return true;
  }

 private:
  static constexpr int kMaxDepth = 64;

  bool fail(const std::string& message) {
    if (error_.empty()) error_ = message;
    return false;
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

  bool literal(const char* word) {
    const std::size_t length = std::strlen(word);
    if (text_.compare(position_, length, word) != 0) return false;
    position_ += length;
    return true;
  }

  bool parseValue(JsonValue& out, int depth) {
    if (depth > kMaxDepth) return fail("Nesting too deep");
    if (position_ >= text_.size()) return fail("Unexpected end of input");
    const char c = text_[position_];
    if (c == '{') return parseObject(out, depth);
    if (c == '[') return parseArray(out, depth);
    if (c == '"') {
      std::string value;
      if (!parseString(value)) return fail(error_);
      out = JsonValue::makeString(std::move(value));
      return true;
    }
    if (literal("true")) { out = JsonValue::makeBool(true); return true; }
    if (literal("false")) { out = JsonValue::makeBool(false); return true; }
    if (literal("null")) { out = JsonValue::makeNull(); return true; }
    return parseNumber(out);
  }

  bool parseObject(JsonValue& out, int depth) {
    ++position_;  // '{'
    out = JsonValue::makeObject();
    skipWhitespace();
    if (position_ < text_.size() && text_[position_] == '}') { ++position_; return true; }
    for (;;) {
      skipWhitespace();
      if (position_ >= text_.size() || text_[position_] != '"') return fail("Expected object key");
      std::string key;
      if (!parseString(key)) return fail(error_);
      skipWhitespace();
      if (position_ >= text_.size() || text_[position_] != ':') return fail("Expected ':'");
      ++position_;
      skipWhitespace();
      JsonValue value;
      if (!parseValue(value, depth + 1)) return false;
      out.set(key, std::move(value));
      skipWhitespace();
      if (position_ >= text_.size()) return fail("Unterminated object");
      if (text_[position_] == ',') { ++position_; continue; }
      if (text_[position_] == '}') { ++position_; return true; }
      return fail("Expected ',' or '}'");
    }
  }

  bool parseArray(JsonValue& out, int depth) {
    ++position_;  // '['
    out = JsonValue::makeArray();
    skipWhitespace();
    if (position_ < text_.size() && text_[position_] == ']') { ++position_; return true; }
    for (;;) {
      skipWhitespace();
      JsonValue value;
      if (!parseValue(value, depth + 1)) return false;
      out.append(std::move(value));
      skipWhitespace();
      if (position_ >= text_.size()) return fail("Unterminated array");
      if (text_[position_] == ',') { ++position_; continue; }
      if (text_[position_] == ']') { ++position_; return true; }
      return fail("Expected ',' or ']'");
    }
  }

  bool parseString(std::string& out) {
    ++position_;  // opening quote
    out.clear();
    while (position_ < text_.size()) {
      const char c = text_[position_++];
      if (c == '"') return true;
      if (c != '\\') {
        out.push_back(c);
        continue;
      }
      if (position_ >= text_.size()) return false;
      const char escaped = text_[position_++];
      switch (escaped) {
        case '"': out.push_back('"'); break;
        case '\\': out.push_back('\\'); break;
        case '/': out.push_back('/'); break;
        case 'b': out.push_back('\b'); break;
        case 'f': out.push_back('\f'); break;
        case 'n': out.push_back('\n'); break;
        case 'r': out.push_back('\r'); break;
        case 't': out.push_back('\t'); break;
        case 'u': {
          if (position_ + 4 > text_.size()) return false;
          unsigned code = 0;
          for (int i = 0; i < 4; ++i) {
            const char h = text_[position_++];
            code <<= 4;
            if (h >= '0' && h <= '9') code |= static_cast<unsigned>(h - '0');
            else if (h >= 'a' && h <= 'f') code |= static_cast<unsigned>(h - 'a' + 10);
            else if (h >= 'A' && h <= 'F') code |= static_cast<unsigned>(h - 'A' + 10);
            else return false;
          }
          appendUtf8(out, code);
          break;
        }
        default: return false;
      }
    }
    return false;
  }

  static void appendUtf8(std::string& out, unsigned code) {
    if (code < 0x80) {
      out.push_back(static_cast<char>(code));
    } else if (code < 0x800) {
      out.push_back(static_cast<char>(0xC0 | (code >> 6)));
      out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
    } else {
      out.push_back(static_cast<char>(0xE0 | (code >> 12)));
      out.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3F)));
      out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
    }
  }

  bool parseNumber(JsonValue& out) {
    const std::size_t start = position_;
    if (position_ < text_.size() && text_[position_] == '-') ++position_;

    // Integer part: either a single zero or a digit sequence without leading zeros.
    if (position_ >= text_.size() || text_[position_] < '0' || text_[position_] > '9') {
      return fail("Number requires at least one digit");
    }
    bool leadingZero = text_[position_] == '0';
    bool digits = true;
    ++position_;
    if (leadingZero && position_ < text_.size() && text_[position_] >= '0' && text_[position_] <= '9') {
      return fail("Leading zeros are not allowed in a number");
    }
    while (position_ < text_.size() && text_[position_] >= '0' && text_[position_] <= '9') ++position_;

    if (position_ < text_.size() && text_[position_] == '.') {
      ++position_;
      const std::size_t fractionStart = position_;
      while (position_ < text_.size() && text_[position_] >= '0' && text_[position_] <= '9') ++position_;
      if (position_ == fractionStart) return fail("Fraction requires at least one digit");
    }
    if (!digits) return fail("Invalid number");
    if (position_ < text_.size() && (text_[position_] == 'e' || text_[position_] == 'E')) {
      ++position_;
      if (position_ < text_.size() && (text_[position_] == '+' || text_[position_] == '-')) ++position_;
      const std::size_t exponentStart = position_;
      while (position_ < text_.size() && text_[position_] >= '0' && text_[position_] <= '9') ++position_;
      if (position_ == exponentStart) return fail("Invalid exponent");
    }
    out = JsonValue::makeNumber(std::strtod(text_.substr(start, position_ - start).c_str(), nullptr));
    return true;
  }

  const std::string& text_;
  std::size_t position_ = 0;
  std::string error_;
};

std::string escape(const std::string& text) {
  std::string out;
  out.reserve(text.size() + 8);
  for (const char c : text) {
    switch (c) {
      case '"': out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\n': out += "\\n"; break;
      case '\r': out += "\\r"; break;
      case '\t': out += "\\t"; break;
      default:
        if (static_cast<unsigned char>(c) < 0x20) {
          char buffer[8];
          std::snprintf(buffer, sizeof(buffer), "\\u%04x", static_cast<unsigned char>(c));
          out += buffer;
        } else {
          out.push_back(c);
        }
    }
  }
  return out;
}

}  // namespace

JsonValue JsonValue::makeNull() { return {}; }
JsonValue JsonValue::makeBool(bool value) { JsonValue v; v.type_ = Type::Bool; v.boolean_ = value; return v; }
JsonValue JsonValue::makeNumber(double value) { JsonValue v; v.type_ = Type::Number; v.number_ = value; return v; }
JsonValue JsonValue::makeString(std::string value) { JsonValue v; v.type_ = Type::String; v.string_ = std::move(value); return v; }
JsonValue JsonValue::makeArray() { JsonValue v; v.type_ = Type::Array; v.array_ = std::make_shared<JsonArray>(); return v; }
JsonValue JsonValue::makeObject() { JsonValue v; v.type_ = Type::Object; v.object_ = std::make_shared<JsonObject>(); return v; }

const JsonValue& JsonValue::operator[](const std::string& key) const {
  if (!object_) return nullValue();
  const auto it = object_->find(key);
  return it == object_->end() ? nullValue() : it->second;
}

bool JsonValue::has(const std::string& key) const { return object_ && object_->count(key) > 0; }

JsonValue& JsonValue::set(const std::string& key, JsonValue value) {
  if (!object_) { type_ = Type::Object; object_ = std::make_shared<JsonObject>(); }
  (*object_)[key] = std::move(value);
  return *this;
}

const JsonArray& JsonValue::items() const {
  static const JsonArray empty;
  return array_ ? *array_ : empty;
}

std::size_t JsonValue::size() const { return array_ ? array_->size() : 0; }

const JsonValue& JsonValue::at(std::size_t index) const {
  if (!array_ || index >= array_->size()) return nullValue();
  return (*array_)[index];
}

JsonValue& JsonValue::append(JsonValue value) {
  if (!array_) { type_ = Type::Array; array_ = std::make_shared<JsonArray>(); }
  array_->push_back(std::move(value));
  return *this;
}

void JsonValue::dumpTo(std::string& out, int indent, int depth) const {
  const std::string pad = indent > 0 ? std::string(static_cast<std::size_t>(indent * (depth + 1)), ' ') : "";
  const std::string closePad = indent > 0 ? std::string(static_cast<std::size_t>(indent * depth), ' ') : "";
  switch (type_) {
    case Type::Null: out += "null"; break;
    case Type::Bool: out += boolean_ ? "true" : "false"; break;
    case Type::Number: {
      char buffer[32];
      std::snprintf(buffer, sizeof(buffer), "%.17g", number_);
      out += buffer;
      break;
    }
    case Type::String: out += '"' + escape(string_) + '"'; break;
    case Type::Array: {
      if (array_->empty()) { out += "[]"; break; }
      out += '[';
      bool first = true;
      for (const auto& item : *array_) {
        if (!first) out += ',';
        first = false;
        if (indent > 0) { out += '\n'; out += pad; }
        item.dumpTo(out, indent, depth + 1);
      }
      if (indent > 0) { out += '\n'; out += closePad; }
      out += ']';
      break;
    }
    case Type::Object: {
      if (object_->empty()) { out += "{}"; break; }
      out += '{';
      bool first = true;
      for (const auto& [key, value] : *object_) {
        if (!first) out += ',';
        first = false;
        if (indent > 0) { out += '\n'; out += pad; }
        out += '"' + escape(key) + "\":";
        if (indent > 0) out += ' ';
        value.dumpTo(out, indent, depth + 1);
      }
      if (indent > 0) { out += '\n'; out += closePad; }
      out += '}';
      break;
    }
  }
}

std::string JsonValue::dump(int indent) const {
  std::string out;
  dumpTo(out, indent, 0);
  return out;
}

bool JsonValue::parse(const std::string& text, JsonValue& out, std::string& error) {
  Parser parser(text);
  return parser.run(out, error);
}

}  // namespace cs2n
