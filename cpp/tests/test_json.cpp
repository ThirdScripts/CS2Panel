#include "core/json.h"
#include "tests/framework.h"

using cs2n::JsonValue;

CS2N_TEST(Json_ParsesNestedObject) {
  std::string error;
  JsonValue value;
  const bool ok = JsonValue::parse(R"({"a": 1, "b": [true, null, "x"], "c": {"d": -2.5}})", value, error);
  CHECK_TRUE(ok);
  CHECK_EQ(error, std::string());
  CHECK_NEAR(value["a"].asNumber(), 1.0, 1e-12);
  CHECK_EQ(value["b"].size(), std::size_t(3));
  CHECK_TRUE(value["b"].at(0).asBool());
  CHECK_TRUE(value["b"].at(1).isNull());
  CHECK_EQ(value["b"].at(2).asString(), std::string("x"));
  CHECK_NEAR(value["c"]["d"].asNumber(), -2.5, 1e-12);
}

CS2N_TEST(Json_RejectsMalformedInput) {
  std::string error;
  JsonValue value;
  CHECK_FALSE(JsonValue::parse(R"({"a": )", value, error));
  CHECK_FALSE(error.empty());

  CHECK_FALSE(JsonValue::parse("{\"a\": 01}", value, error));
  CHECK_FALSE(JsonValue::parse("[1, 2,]", value, error));
  CHECK_FALSE(JsonValue::parse("", value, error));
  CHECK_FALSE(JsonValue::parse("tru", value, error));
}

CS2N_TEST(Json_HandlesEscapesAndUnicode) {
  std::string error;
  JsonValue value;
  const std::string input = R"({"text":"line\nbreak and \"quote\" and é"})";
  CHECK_TRUE(JsonValue::parse(input, value, error));
  CHECK_EQ(value["text"].asString(), std::string("line\nbreak and \"quote\" and \xC3\xA9"));
}

CS2N_TEST(Json_RoundTripsThroughDump) {
  std::string error;
  JsonValue original;
  const std::string input = R"({"n":1.5,"s":"q\"w","arr":[1,2,3],"obj":{"k":true}})";
  CHECK_TRUE(JsonValue::parse(input, original, error));
  JsonValue reparsed;
  CHECK_TRUE(JsonValue::parse(original.dump(), reparsed, error));
  CHECK_NEAR(reparsed["n"].asNumber(), 1.5, 1e-12);
  CHECK_EQ(reparsed["s"].asString(), std::string("q\"w"));
  CHECK_EQ(reparsed["arr"].size(), std::size_t(3));
  CHECK_TRUE(reparsed["obj"]["k"].asBool());
}

CS2N_TEST(Json_MissingKeysAreSafeToChain) {
  std::string error;
  JsonValue value;
  CHECK_TRUE(JsonValue::parse("{}", value, error));
  // Chaining into absent keys must not crash and must yield the fallback.
  CHECK_TRUE(value["deeply"]["nested"]["key"].isNull());
  CHECK_NEAR(value["deeply"]["nested"]["key"].asNumber(7.0), 7.0, 1e-12);
}
