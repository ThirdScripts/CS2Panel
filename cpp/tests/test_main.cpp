// Test runner. Usage: cs2n_tests [filter] [--verbose]
#include <cstring>
#include <exception>
#include <string>

#include "tests/framework.h"

int main(int argc, char** argv) {
  std::string filter;
  bool verbose = false;
  for (int i = 1; i < argc; ++i) {
    if (std::strcmp(argv[i], "--verbose") == 0) {
      verbose = true;
    } else {
      filter = argv[i];
    }
  }

  auto& registry = cs2n::test::Registry::instance();
  int ran = 0;
  int failed = 0;

  for (const auto& testCase : registry.cases()) {
    if (!filter.empty() && testCase.name.find(filter) == std::string::npos) continue;
    registry.setCurrentTest(testCase.name);
    const std::size_t before = registry.failures().size();
    try {
      testCase.body();
    } catch (const std::exception& exception) {
      cs2n::test::report("<runner>", 0, "no uncaught exception", exception.what(), testCase.name.c_str());
    } catch (...) {
      cs2n::test::report("<runner>", 0, "no uncaught exception", "unknown exception", testCase.name.c_str());
    }
    ++ran;
    const bool ok = registry.failures().size() == before;
    if (!ok) ++failed;
    if (verbose || !ok) {
      std::printf("%s %s\n", ok ? "[PASS]" : "[FAIL]", testCase.name.c_str());
      if (!ok) {
        for (std::size_t i = before; i < registry.failures().size(); ++i) {
          const auto& failure = registry.failures()[i];
          std::printf("       %s:%d  %s\n         %s\n", failure.file.c_str(), failure.line,
                      failure.expression.c_str(), failure.detail.c_str());
        }
      }
    }
  }

  std::printf("\n%d tests, %d failed\n", ran, failed);
  if (ran == 0) {
    std::fprintf(stderr, "No tests matched filter: %s\n", filter.c_str());
    return 2;
  }
  return failed == 0 ? 0 : 1;
}
