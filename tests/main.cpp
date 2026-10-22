#define DOCTEST_CONFIG_IMPLEMENT
#include <doctest/doctest.h>

#include <cstddef>
#include <cstdio>
#include <optional>
#include <print>
#include <span>
#include <string_view>

// The entry point of every test executable: doctest's, plus a guard. A run whose --test-case filter
// matches no test case fails, because ctest registers one entry per test name and an entry whose
// filter matches nothing (a name split across string literals, a test renamed since the last
// configure) would otherwise pass without running anything. A test that skips itself by returning,
// such as one on the user's disc when DWE_DISC is not set, still matches its filter. Skip by
// returning, not with doctest::skip(): a case with that decorator matches no filter.

namespace {

// Lets a run whose --test-case filter matches nothing pass: for the catch-all entries, which
// usually find nothing.
constexpr std::string_view AllowNoMatchOption = "--dwe-allow-no-match";
constexpr int NoMatchExitCode = 2; // the guard's test in CMakeLists.txt expects it
constexpr std::string_view NoMatchMessage =
    "[dwe] the --test-case filter matches no test case: the test was renamed or its name is split across string "
    "literals; reconfigure, or keep the name in one literal";

// doctest reads its options with any number of leading dashes and an optional "dt-" prefix.
constexpr std::string_view DoctestOptionPrefix = "dt-";
constexpr std::string_view TestCaseFilterOption = "test-case=";
constexpr std::string_view TestCaseFilterShortOption = "tc=";

bool isTestCaseFilter(std::string_view argument) {
  const std::size_t dashes = argument.find_first_not_of('-');
  if (dashes == 0 || dashes == std::string_view::npos) {
    return false;
  }
  argument.remove_prefix(dashes);
  if (argument.starts_with(DoctestOptionPrefix)) {
    argument.remove_prefix(DoctestOptionPrefix.size());
  }
  return argument.starts_with(TestCaseFilterOption) ||
         argument.starts_with(TestCaseFilterShortOption);
}

struct GuardOptions {
  bool hasTestCaseFilter = false;
  bool allowNoMatch = false;
};

GuardOptions guardOptionsOf(std::span<const char* const> arguments) {
  GuardOptions options;
  for (const std::string_view argument : arguments) {
    options.hasTestCaseFilter = options.hasTestCaseFilter || isTestCaseFilter(argument);
    options.allowNoMatch = options.allowNoMatch || argument == AllowNoMatchOption;
  }
  return options;
}

// How many test cases passed the filters of the last run; empty when nothing ran, as for --help or
// --list-test-cases.
std::optional<unsigned>& casesPassingFilters() {
  static std::optional<unsigned> count;
  return count;
}

// A doctest listener that records the count of the run.
class FilterMatchListener final : public doctest::IReporter {
public:
  explicit FilterMatchListener(const doctest::ContextOptions& /*options*/) {}

  void report_query(const doctest::QueryData& /*data*/) override {}
  void test_run_start() override {}
  void test_run_end(const doctest::TestRunStats& stats) override {
    casesPassingFilters() = stats.numTestCasesPassingFilters;
  }
  void test_case_start(const doctest::TestCaseData& /*data*/) override {}
  void test_case_reenter(const doctest::TestCaseData& /*data*/) override {}
  void test_case_end(const doctest::CurrentTestCaseStats& /*stats*/) override {}
  void test_case_exception(const doctest::TestCaseException& /*exception*/) override {}
  void subcase_start(const doctest::SubcaseSignature& /*signature*/) override {}
  void subcase_end() override {}
  void log_assert(const doctest::AssertData& /*data*/) override {}
  void log_message(const doctest::MessageData& /*data*/) override {}
  void test_case_skipped(const doctest::TestCaseData& /*data*/) override {}
};

} // namespace

DOCTEST_REGISTER_LISTENER("dwe-filter-match", 1, FilterMatchListener);

int main(int argc, char** argv) {
  doctest::Context context{argc, argv};
  const int result = context.run();
  if (context.shouldExit() || result != 0) {
    return result;
  }
  const GuardOptions options =
      guardOptionsOf(std::span<const char* const>{argv, static_cast<std::size_t>(argc)});
  if (options.hasTestCaseFilter && !options.allowNoMatch && casesPassingFilters() == 0U) {
    std::println(stderr, "{}", NoMatchMessage);
    return NoMatchExitCode;
  }
  return 0;
}
