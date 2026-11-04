// Minimal, dependency-free JSON reader/writer used for GSI payloads and NavMesh assets.
#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace cs2n {

class JsonValue;
using JsonArray = std::vector<JsonValue>;
using JsonObject = std::map<std::string, JsonValue>;

class JsonValue {
 public:
  enum class Type { Null, Bool, Number, String, Array, Object };

  JsonValue() = default;
  static JsonValue makeNull();
  static JsonValue makeBool(bool value);
  static JsonValue makeNumber(double value);
  static JsonValue makeString(std::string value);
  static JsonValue makeArray();
  static JsonValue makeObject();

  [[nodiscard]] Type type() const { return type_; }
  [[nodiscard]] bool isNull() const { return type_ == Type::Null; }
  [[nodiscard]] bool isBool() const { return type_ == Type::Bool; }
  [[nodiscard]] bool isNumber() const { return type_ == Type::Number; }
  [[nodiscard]] bool isString() const { return type_ == Type::String; }
  [[nodiscard]] bool isArray() const { return type_ == Type::Array; }
  [[nodiscard]] bool isObject() const { return type_ == Type::Object; }

  [[nodiscard]] bool asBool(bool fallback = false) const { return isBool() ? boolean_ : fallback; }
  [[nodiscard]] double asNumber(double fallback = 0.0) const { return isNumber() ? number_ : fallback; }
  [[nodiscard]] std::int64_t asInt(std::int64_t fallback = 0) const {
    return isNumber() ? static_cast<std::int64_t>(number_) : fallback;
  }
  [[nodiscard]] const std::string& asString() const {
    static const std::string empty;
    return isString() ? string_ : empty;
  }

  // Object access; returns a shared null for missing keys so chaining is safe.
  [[nodiscard]] const JsonValue& operator[](const std::string& key) const;
  [[nodiscard]] bool has(const std::string& key) const;
  JsonValue& set(const std::string& key, JsonValue value);

  // Array access.
  [[nodiscard]] const JsonArray& items() const;
  [[nodiscard]] std::size_t size() const;
  [[nodiscard]] const JsonValue& at(std::size_t index) const;
  JsonValue& append(JsonValue value);

  [[nodiscard]] std::string dump(int indent = 0) const;

  // Returns false and fills `error` on malformed input.
  static bool parse(const std::string& text, JsonValue& out, std::string& error);

 private:
  void dumpTo(std::string& out, int indent, int depth) const;

  Type type_ = Type::Null;
  bool boolean_ = false;
  double number_ = 0.0;
  std::string string_;
  std::shared_ptr<JsonArray> array_;
  std::shared_ptr<JsonObject> object_;
};

}  // namespace cs2n
