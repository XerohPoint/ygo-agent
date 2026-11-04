// Written-specification conformance fixtures. See conformance_harness.h for why
// this is a separate executable from edison_rules.
#include "conformance_harness.h"
#include "cards.h"
#include "rule_tests.h"
#include "ruling_tests.h"

int main(int argc, char** argv) {
  using namespace conformance;
  if (argc < 3) {
    std::cerr << "Usage: edison_conformance DATABASE SCRIPT_DIRECTORY [--trace] [--test=NNN] [--list] [--fail-fast]\n";
    return 2;
  }
  bool trace = false, list_only = false, fail_fast = false;
  std::string filter;
  for (int i = 3; i < argc; ++i) {
    std::string arg = argv[i];
    if (arg == "--trace") trace = true;
    else if (arg == "--list") list_only = true;
    else if (arg == "--fail-fast") fail_fast = true;
    else if (arg.rfind("--test=", 0) == 0) filter = arg.substr(7);
    else { std::cerr << "Unknown argument: " << arg << '\n'; return 2; }
  }
  try {
    ConfAssets assets(argv[1], argv[2]);
    int total = 0, failures = 0;
    Registry registry;
    registry.test = [&](const std::string& number, const std::string& title, const std::string& expected, bool control,
                        const std::function<void()>& body) {
      const std::string id = number.substr(0, 3);
      const std::string label = (control ? "CONTROL " : "EDISON RULE TEST ") + number + " - " + title;
      if (!filter.empty() && id != filter) return;
      if (list_only) { std::cout << id << '\t' << number << '\t' << (control ? "control" : "rule") << '\t' << title << '\n'; return; }
      if (fail_fast && failures) return;
      ++total;
      std::cout << '\n' << label << "\nExpected: " << expected << '\n';
      try {
        body();
        assets.check();
        std::cout << "RESULT: PASS\n";
      } catch (const RuleMismatch& e) {
        ++failures;
        std::cout << "RESULT: FAIL [RULE] " << e.what() << '\n';
      } catch (const std::exception& e) {
        ++failures;
        assets.error.clear();
        std::cout << "RESULT: FAIL [SETUP] " << e.what() << '\n';
      }
    };
    register_rule_tests(registry, assets, trace);
    register_ruling_tests(registry, assets, trace);
    if (list_only) return 0;
    require(total > 0, "No conformance cases matched the requested test number");
    std::cout << "\nSUMMARY: " << total - failures << " PASS, " << failures << " FAIL\n";
    return failures ? 1 : 0;
  } catch (const std::exception& e) {
    std::cerr << "RESULT: FAIL [SETUP] " << e.what() << '\n';
    return 1;
  }
}
