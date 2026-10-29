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
        bitzkey::PREDATOR_FULL_LITTER_FOOD = 2.0;
        bitzkey::GRAZER_RARE_BREEDING_COST = 1.0;
        // The older tests' figures count years of 360 turns, and their animals don't sleep through the night, unless
        // a test says so.
        bitzkey::DEFAULT_YEAR_LENGTH = bitzkey::YEAR_LENGTH = 360;
        bitzkey::SLEEP = 0;
        // and count other spans in the turns of the eight-turn days they were written for, unless a test says so.
        bitzkey::CORPSE_TURNS = 30;
        bitzkey::GESTATION = {10, 50};
        bitzkey::FRESH_KILL_TURNS = 3;
        bitzkey::PLANT_GESTATION = 10;
        bitzkey::PLANT_RECOVERY = 8;
        bitzkey::GRASS_DIEBACK = 20;
        bitzkey::GRASS_OUTLOOK = 150;
        bitzkey::MEMORY_TURNS = 100;
        bitzkey::SOCIAL_MEMORY = 0.8;
        bitzkey::LEARNING_FLOOR = 0.0;  // the dullest learn nothing, as when the older tests were written, unless a test says so
        bitzkey::WEATHER_GRACE = 0.0;  // the weather harms a test's animals at once, unless a test says so
        bitzkey::SMALL_HIDING = 0.0;  // a test's hunter strikes what it strikes at, unless a test says so
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
