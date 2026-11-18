#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <numeric>
#include <stats.hpp>
#include <timing.hpp>
#include <vector>
#include <workload.hpp>

#include "types.hpp"

namespace {
  constexpr std::size_t kN{2000};

  bool restingOk(const ob::NewOrder& o) {
    return o.side == ob::Side::kBuy
               ? (o.price >= bench::kMid - bench::kMaxOffset && o.price < bench::kMid)
               : (o.price > bench::kMid && o.price <= bench::kMid + bench::kMaxOffset);
  }
}  // namespace

TEST(Workload, SameSeedSameStream) {
  const auto a{bench::makeWorkload(42, kN)};
  const auto b{bench::makeWorkload(42, kN)};
  const auto c{bench::makeWorkload(43, kN)};
  ASSERT_EQ(a.resting.size(), kN);
  for (std::size_t i{}; i < kN; ++i) {
    EXPECT_EQ(a.resting[i].price, b.resting[i].price);
    EXPECT_EQ(a.resting[i].side, b.resting[i].side);
    EXPECT_EQ(a.resting[i].qty, b.resting[i].qty);
  }
  EXPECT_EQ(a.modifyPx, b.modifyPx);
  EXPECT_EQ(a.cancelOrder, b.cancelOrder);
  EXPECT_NE(a.modifyPx, c.modifyPx);
}

TEST(Workload, RestingInBandNonCrossingQtyAboveOne) {
  const auto w{bench::makeWorkload(7, kN)};
  std::size_t buys{};
  for (const auto& o : w.resting) {
    EXPECT_TRUE(bench::kBand.contains(o.price));
    EXPECT_TRUE(restingOk(o)) << o.price;
    EXPECT_GE(o.qty, 2);  // qty-down phase needs room for qty-1 >= 1
    buys += o.side == ob::Side::kBuy ? 1U : 0U;
  }
  EXPECT_GT(buys, kN / 3);
  EXPECT_LT(buys, 2 * kN / 3);
}

TEST(Workload, ModifyPriceDiffersAndStaysOnSide) {
  const auto w{bench::makeWorkload(7, kN)};
  ASSERT_EQ(w.modifyPx.size(), kN);
  for (std::size_t i{}; i < kN; ++i) {
    ob::NewOrder moved{w.resting[i]};
    moved.price = w.modifyPx[i];
    EXPECT_NE(moved.price, w.resting[i].price);
    EXPECT_TRUE(restingOk(moved)) << moved.price;
  }
}

TEST(Workload, CancelOrderIsPermutation) {
  const auto w{bench::makeWorkload(7, kN)};
  std::vector<std::size_t> sorted{w.cancelOrder};
  std::sort(sorted.begin(), sorted.end());
  std::vector<std::size_t> expect(kN);
  std::iota(expect.begin(), expect.end(), std::size_t{0});
  EXPECT_EQ(sorted, expect);
  EXPECT_NE(w.cancelOrder, expect);
}

TEST(Stats, PercentileNearestRank) {
  std::vector<std::uint64_t> v(1000);
  std::iota(v.begin(), v.end(), std::uint64_t{1});
  EXPECT_EQ(bench::percentile(v, 50.0), 500U);
  EXPECT_EQ(bench::percentile(v, 99.0), 990U);
  EXPECT_EQ(bench::percentile(v, 99.9), 999U);
  EXPECT_EQ(bench::percentile(v, 100.0), 1000U);
  EXPECT_EQ(bench::percentile({7}, 99.9), 7U);
}

TEST(Stats, MedianOddEven) {
  EXPECT_DOUBLE_EQ(bench::median({3.0, 1.0, 2.0}), 2.0);
  EXPECT_DOUBLE_EQ(bench::median({4.0, 1.0, 3.0, 2.0}), 2.5);
}

TEST(Timing, BlocksTimeFullBlocksAndRunEveryOp) {
  std::vector<std::size_t> called;
  std::uint64_t clock{};
  // Fake clock: each read advances 10 ns; each op advances 1 ns.
  const auto now{[&clock] { return clock += 10; }};
  const auto op{[&](std::size_t i) {
    called.push_back(i);
    clock += 1;
  }};
  std::vector<std::uint64_t> samples;
  bench::timeBlocks(10, 4, op, now, samples);

  std::vector<std::size_t> expect(10);
  std::iota(expect.begin(), expect.end(), std::size_t{0});
  EXPECT_EQ(called, expect);  // the partial block still runs, untimed
  ASSERT_EQ(samples.size(), 2U);
  EXPECT_EQ(samples[0], 14U);  // 4 ops + the closing read's 10
  EXPECT_EQ(samples[1], 14U);
}

TEST(Timing, BlockLargerThanNYieldsNoSamples) {
  std::size_t calls{};
  std::uint64_t clock{};
  std::vector<std::uint64_t> samples;
  bench::timeBlocks(3, 64, [&](std::size_t) { ++calls; }, [&clock] { return ++clock; }, samples);
  EXPECT_EQ(calls, 3U);
  EXPECT_TRUE(samples.empty());
}
