// The random number generator and number formatting must match CPython exactly; the expected
// values here were produced by CPython 3.14.

#include <doctest/doctest.h>

#include <vector>

#include "core/PyFormat.hpp"
#include "core/PyRandom.hpp"

using namespace bitzkey;

TEST_CASE("PyRandom reproduces CPython's random.Random(42)") {
    PyRandom r(42);
    CHECK(r.random() == 0.6394267984578837);
    CHECK(r.random() == 0.025010755222666936);
    CHECK(r.getrandbits(64) == 4517457392071889495ULL);
    CHECK(r.getrandbits(7) == 28);
    std::vector<std::int64_t> ranges;
    for (int i = 0; i < 5; ++i) ranges.push_back(r.randrange(10));
    CHECK(ranges == std::vector<std::int64_t>{2, 1, 8, 1, 9});
    CHECK(r.randrange(3, 17) == 9);
    CHECK(r.randint(-2, 2) == -2);
    CHECK(r.gauss(0, 0.05) == 0.03450864995823009);
    CHECK(r.gauss(0, 0.05) == 0.006537320388314196);
    CHECK(r.gauss(1, 2) == 0.5364142783239576);
    std::vector<int> items = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9};
    r.shuffle(items);
    CHECK(items == std::vector<int>{7, 5, 0, 2, 4, 9, 1, 6, 8, 3});
    const std::array<int, 3> steps = {-1, 0, 1};
    CHECK(r.choice(steps) == 1);
    CHECK(r.uniform(-0.3, 0.3) == -0.04643136110789847);
}

TEST_CASE("PyRandom seeds as CPython does for large and zero seeds") {
    CHECK(PyRandom(12345678901234ULL).random() == 0.02504137575317178);
    CHECK(PyRandom(0).random() == 0.8444218515250481);
}

TEST_CASE("a saved state replays the same numbers, gauss's spare included") {
    PyRandom r(7);
    r.gauss(0, 1);
    const auto state = r.getstate();
    const double a = r.gauss(0, 1), b = r.random();
    r.setstate(state);
    CHECK(r.gauss(0, 1) == a);
    CHECK(r.random() == b);
}

TEST_CASE("floats are written as Python's repr writes them") {
    CHECK(py_float_repr(1e-05) == "1e-05");
    CHECK(py_float_repr(0.0001) == "0.0001");
    CHECK(py_float_repr(123.456) == "123.456");
    CHECK(py_float_repr(1e16) == "1e+16");
    CHECK(py_float_repr(1.5e16) == "1.5e+16");
    CHECK(py_float_repr(0.1) == "0.1");
    CHECK(py_float_repr(2.0) == "2.0");
    CHECK(py_float_repr(-0.0) == "-0.0");
    CHECK(py_float_repr(1234567890123456.0) == "1234567890123456.0");
    CHECK(py_float_repr(5e-324) == "5e-324");
}

TEST_CASE("rounding follows Python") {
    CHECK(py_round(2.675, 2) == 2.67);
    CHECK(py_round(0.1234565, 6) == 0.123456);
    CHECK(py_round(2.5) == 2);
    CHECK(py_round(3.5) == 4);
    CHECK(py_mod(-1, 10) == 9);
    CHECK(py_mod(11, 10) == 1);
}

TEST_CASE("sum() of floats is compensated, as in Python 3.12+") {
    const std::vector<double> values = {0.1, 0.1, 0.1, 0.1, 0.1, 0.1, 0.1, 0.1, 0.1, 0.1};
    CHECK(py_sum(values) == 1.0);  // plain left-to-right addition gives 0.9999999999999999
}
