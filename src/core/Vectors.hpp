// Arithmetic over runs of numbers, for the brains: four at a time where the processor can, with the same result
// to the last bit as one at a time, so a world runs the same on any machine and matches the Python version.
#pragma once

#include <cstddef>

namespace bitzkey::vectors {

inline constexpr std::size_t MOST = 512;  // the longest run dot takes

// Whether the processor is going four at a time. `use_wide` turns that off and on, for the tests, and answers
// what it was; on a processor that can't, it stays off.
bool wide();
bool use_wide(bool on);

double dot(const double* first, const double* second, std::size_t count);  // added up as numpy's sum would
void scale(double* values, std::size_t count, double by);  // values *= by
void add_scaled(double* to, const double* from, std::size_t count, double by);  // to += by * from
void add_scaled_twice(double* to, const double* from, std::size_t count, double outer, double inner);  // to += outer * (inner * from)

}  // namespace bitzkey::vectors
