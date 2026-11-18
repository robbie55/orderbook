#pragma once

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace bench {
  inline double median(std::vector<double> v) {
    assert(!v.empty());
    std::sort(v.begin(), v.end());
    const std::size_t mid{v.size() / 2};
    return v.size() % 2 == 1 ? v[mid] : (v[mid - 1] + v[mid]) / 2.0;
  }

  // Nearest-rank percentile of an ascending vector; p in percent (99.9 = p99.9).
  inline std::uint64_t percentile(const std::vector<std::uint64_t>& sorted, double p) {
    assert(!sorted.empty());
    const auto n{static_cast<double>(sorted.size())};
    // epsilon: 99.9 / 100 * 1000 lands a hair above 999 in binary and would round to 1000.
    const auto rank{static_cast<std::size_t>(std::ceil(std::max(0.0, p / 100.0 * n - 1e-9)))};
    return sorted[std::clamp<std::size_t>(rank, 1, sorted.size()) - 1];
  }
}  // namespace bench
