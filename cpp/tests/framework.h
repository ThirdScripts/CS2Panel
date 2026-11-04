// A very small test harness: no external dependency, one binary, clear output.
#pragma once

#include <cmath>
#include <cstdio>
#include <functional>
#include <sstream>
#include <string>
#include <vector>

namespace cs2n::test {

struct Failure {
  std::string test;
  std::string expression;
  std::string file;
  int line = 0;
  std::string detail;
};

class Registry {
 public:
  struct Case {
    std::string name;
    std::function<void()> body;
  };

  static Registry& instance() {
    static Registry registry;
    return registry;
  }

  void add(std::string name, std::function<void()> body) { cases_.push_back({std::move(name), std::move(body)}); }

  const std::vector<Case>& cases() const { return cases_; }

  std::vector<Failure>& failures() { return failures_; }
  void addFailure(Failure failure) { failures_.push_back(std::move(failure)); }
  void reset() { failures_.clear(); }

  void setCurrentTest(const std::string& name) { currentTest_ = name; }
  const char* currentTest() const { return currentTest_.c_str(); }

 private:
  std::vector<Case> cases_;
  std::vector<Failure> failures_;
  std::string currentTest_;
};

struct Registrar {
  Registrar(const char* name, std::function<void()> body) { Registry::instance().add(name, std::move(body)); }
};

inline void report(const char* file, int line, const std::string& expression, const std::string& detail,
                   const char* test) {
  Failure failure;
  failure.expression = expression;
  failure.detail = detail;
  failure.file = file;
  failure.line = line;
  failure.test = test;
  Registry::instance().addFailure(std::move(failure));
}

inline bool nearlyEqual(double a, double b, double tolerance) { return std::fabs(a - b) <= tolerance; }

}  // namespace cs2n::test

#define CS2N_TEST(name)                                              \
  static void name();                                                \
  static ::cs2n::test::Registrar name##_registrar(#name, name);      \
  static void name()

#define CHECK_TRUE(expression)                                                       \
  do {                                                                               \
    if (!(expression)) {                                                             \
      ::cs2n::test::report(__FILE__, __LINE__, #expression, "expected true",         \
                           ::cs2n::test::Registry::instance().currentTest());        \
    }                                                                                \
  } while (false)

#define CHECK_FALSE(expression)                                                      \
  do {                                                                               \
    if ((expression)) {                                                              \
      ::cs2n::test::report(__FILE__, __LINE__, #expression, "expected false",        \
                           ::cs2n::test::Registry::instance().currentTest());        \
    }                                                                                \
  } while (false)

#define CHECK_EQ(actual, expected)                                                   \
  do {                                                                               \
    const auto& a = (actual);                                                        \
    const auto& e = (expected);                                                      \
    if (!(a == e)) {                                                                 \
      std::ostringstream detail;                                                     \
      detail << "actual=" << a << " expected=" << e;                                 \
      ::cs2n::test::report(__FILE__, __LINE__, #actual " == " #expected,             \
                           detail.str(), ::cs2n::test::Registry::instance().currentTest()); \
    }                                                                                \
  } while (false)

#define CHECK_NEAR(actual, expected, tolerance)                                      \
  do {                                                                               \
    const double a = static_cast<double>(actual);                                    \
    const double e = static_cast<double>(expected);                                  \
    const double t = static_cast<double>(tolerance);                                 \
    if (!(std::fabs(a - e) <= t)) {                                                  \
      std::ostringstream detail;                                                     \
      detail << "actual=" << a << " expected=" << e << " +/-" << t;                  \
      ::cs2n::test::report(__FILE__, __LINE__, #actual " ~ " #expected, detail.str(), \
                           ::cs2n::test::Registry::instance().currentTest());        \
    }                                                                                \
  } while (false)

#define CHECK_LT(a, b)                                                               \
  do {                                                                               \
    const auto va = (a);                                                             \
    const auto vb = (b);                                                             \
    if (!(va < vb)) {                                                                \
      std::ostringstream detail;                                                     \
      detail << "lhs=" << va << " rhs=" << vb;                                       \
      ::cs2n::test::report(__FILE__, __LINE__, #a " < " #b, detail.str(),            \
                           ::cs2n::test::Registry::instance().currentTest());        \
    }                                                                                \
  } while (false)

#define CHECK_GT(a, b)                                                               \
  do {                                                                               \
    const auto va = (a);                                                             \
    const auto vb = (b);                                                             \
    if (!(va > vb)) {                                                                \
      std::ostringstream detail;                                                     \
      detail << "lhs=" << va << " rhs=" << vb;                                       \
      ::cs2n::test::report(__FILE__, __LINE__, #a " > " #b, detail.str(),            \
                           ::cs2n::test::Registry::instance().currentTest());        \
    }                                                                                \
  } while (false)
