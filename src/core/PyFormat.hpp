#pragma once
// Python's numeric conventions where they differ from C++'s, so results and exports match the
// Python version exactly.

#include <cmath>
#include <span>
#include <string>

namespace bitzkey {

// round(x): halves round to the even neighbour.
inline double py_round(double value) { return std::nearbyint(value); }

// round(x, digits): the double nearest the decimal rounded to that many places.
double py_round(double value, int digits);

// x % n with Python's sign rule: the result takes the divisor's sign.
inline int py_mod(int value, int divisor) {
    const int result = value % divisor;
    return (result != 0 && ((result < 0) != (divisor < 0))) ? result + divisor : result;
}

// sum() of floats, which Python (3.12+) adds with Neumaier's compensated summation.
double py_sum(std::span<const double> values);

// repr(float): the shortest string that reads back as the same float, in Python's layout.
std::string py_float_repr(double value);

// format(value, ">{width}.0%"): a percentage with no decimals, right-aligned.
std::string py_percent(double value, int width);

}  // namespace bitzkey
