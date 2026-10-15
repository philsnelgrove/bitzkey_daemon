#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "core/Rules.hpp"
#include "core/Simulation.hpp"

// Grown bodies: a Bitzkey made at age 0 isn't feeble in the tests, and a test's few Bitzkeys breed at the usual cost,
// unless a test says so.
struct Grown : doctest::IReporter {
    explicit Grown(const doctest::ContextOptions&) {}
    void test_case_start(const doctest::TestCaseData&) override {
        bitzkey::rules::defaults();  // the defaults, taken before the change
        bitzkey::FEEBLE_YOUTH = 0.0;
        bitzkey::RARE_BREEDING_COST = 1.0;  // the few Bitzkeys of a test breed at the usual cost, unless a test says so
        bitzkey::RARE_PREY = 0;  // a test's lone hunters may breed with no prey alive
        bitzkey::FULL_LITTER_FOOD = 2.0;  // and full litters at twice the need, as the older tests' figures assume
    }
    void report_query(const doctest::QueryData&) override {}
    void test_run_start() override {}
    void test_run_end(const doctest::TestRunStats&) override {}
    void test_case_reenter(const doctest::TestCaseData&) override {}
    void test_case_end(const doctest::CurrentTestCaseStats&) override {}
    void test_case_exception(const doctest::TestCaseException&) override {}
    void subcase_start(const doctest::SubcaseSignature&) override {}
    void subcase_end() override {}
    void log_assert(const doctest::AssertData&) override {}
    void log_message(const doctest::MessageData&) override {}
    void test_case_skipped(const doctest::TestCaseData&) override {}
};
REGISTER_LISTENER("grown", 1, Grown);
