#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace bench {
  // Runs op(0..n-1), timing each full block of k consecutive ops with one now() pair and
  // appending the block's elapsed time to `out`. The trailing partial block runs untimed
  // so book state stays identical to the batch mode. `out` must have capacity reserved:
  // push_back sits between blocks, but a reallocation there would still perturb timing.
  template <class Op, class Now>
  void timeBlocks(std::size_t n, std::size_t k, Op op, Now now, std::vector<std::uint64_t>& out) {
    const std::size_t full{n / k * k};
    std::size_t i{};
    while (i < full) {
      const std::uint64_t t0{now()};
      for (const std::size_t end{i + k}; i < end; ++i) {
        op(i);
      }
      const std::uint64_t t1{now()};
      out.push_back(t1 - t0);
    }
    for (; i < n; ++i) {
      op(i);
    }
  }
}  // namespace bench
