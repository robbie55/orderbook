#include <gtest/gtest.h>

#include <algorithm>
#include <array>
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

// ---- steady-state mixed stream ----

namespace {
  constexpr std::size_t kMixN{2000};
  constexpr std::size_t kMixOps{20000};
  constexpr bench::Mix kMix{.add = 33, .modifyPx = 33, .cancel = 34};

  // Replays a stream's handle bookkeeping; fails on any op against a dead handle.
  struct Replay {
    std::vector<bool> alive;
    std::vector<ob::NewOrder> state;
    std::size_t live{};
    std::size_t minLive{SIZE_MAX};
    std::size_t maxLive{};
    std::array<std::size_t, 3> counts{};

    explicit Replay(const bench::MixedStream& s) : alive(s.preload.size(), true), state(s.preload) {
      live = s.preload.size();
      for (const auto& op : s.ops) {
        apply(op);
        minLive = std::min(minLive, live);
        maxLive = std::max(maxLive, live);
        if (::testing::Test::HasFailure()) {
          return;
        }
      }
    }

    void apply(const bench::Op& op) {
      switch (op.kind) {
        case bench::OpKind::kAdd:
          ++counts[0];
          ASSERT_EQ(op.handle, alive.size()) << "adds take the next fresh handle";
          EXPECT_TRUE(restingOk(op.order));
          alive.push_back(true);
          state.push_back(op.order);
          ++live;
          break;
        case bench::OpKind::kModifyPx: {
          ++counts[1];
          ASSERT_LT(op.handle, alive.size());
          ASSERT_TRUE(alive[op.handle]) << "modify on dead handle";
          ob::NewOrder& o{state[op.handle]};
          EXPECT_NE(op.px, o.price);
          EXPECT_EQ(op.qty, o.qty);
          o.price = op.px;
          EXPECT_TRUE(restingOk(o));
          break;
        }
        case bench::OpKind::kCancel:
          ++counts[2];
          ASSERT_LT(op.handle, alive.size());
          ASSERT_TRUE(alive[op.handle]) << "cancel on dead handle";
          alive[op.handle] = false;
          --live;
          break;
        default:
          FAIL() << "unexpected op kind";
      }
    }
  };
}  // namespace

TEST(Mixed, DeterministicPerSeed) {
  const auto a{bench::makeMixed(5, kMixN, kMixOps, kMix, bench::kCap)};
  const auto b{bench::makeMixed(5, kMixN, kMixOps, kMix, bench::kCap)};
  const auto c{bench::makeMixed(6, kMixN, kMixOps, kMix, bench::kCap)};
  ASSERT_EQ(a.ops.size(), kMixOps);
  ASSERT_EQ(a.preload.size(), kMixN);
  bool differs{false};
  for (std::size_t i{}; i < kMixOps; ++i) {
    EXPECT_EQ(a.ops[i].kind, b.ops[i].kind);
    EXPECT_EQ(a.ops[i].handle, b.ops[i].handle);
    EXPECT_EQ(a.ops[i].px, b.ops[i].px);
    differs = differs || a.ops[i].handle != c.ops[i].handle;
  }
  EXPECT_TRUE(differs);
}

TEST(Mixed, NeverTouchesDeadHandleAndStaysInBounds) {
  for (std::uint64_t seed{1}; seed <= 5; ++seed) {
    const Replay r{bench::makeMixed(seed, kMixN, kMixOps, kMix, bench::kCap)};
    EXPECT_EQ(r.counts[0] + r.counts[1] + r.counts[2], kMixOps);
    EXPECT_GE(r.minLive, kMixN * 95 / 100);
    EXPECT_LE(r.maxLive, kMixN * 105 / 100);
  }
}

TEST(Mixed, LiveCountRespectsCap) {
  // Add-heavy mix pushes against the upper bound; cap below 1.05n must win.
  const bench::Mix addHeavy{.add = 80, .modifyPx = 10, .cancel = 10};
  const Replay r{bench::makeMixed(3, kMixN, kMixOps, addHeavy, kMixN + 10)};
  EXPECT_EQ(r.counts[0] + r.counts[1] + r.counts[2], kMixOps);
  EXPECT_EQ(r.maxLive, kMixN + 10);
  EXPECT_LE(r.maxLive, kMixN + 10);
}

TEST(Mixed, RealisedMixWithinTwoPoints) {
  const Replay r{bench::makeMixed(9, kMixN, kMixOps, kMix, bench::kCap)};
  const std::array<unsigned, 3> want{kMix.add, kMix.modifyPx, kMix.cancel};
  for (std::size_t k{}; k < 3; ++k) {
    const double pct{100.0 * static_cast<double>(r.counts[k]) / static_cast<double>(kMixOps)};
    EXPECT_NEAR(pct, want[k], 2.0) << "kind " << k;
  }
}
