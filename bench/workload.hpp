#pragma once

#include <cstddef>
#include <cstdint>
#include <random>
#include <utility>
#include <vector>

#include "types.hpp"

// Seeded synthetic order streams. Draws use `rng() % range` and a hand-rolled
// Fisher-Yates: mt19937_64's output is fixed by the standard, but uniform_int_distribution
// and std::shuffle differ between libc++ and libstdc++, so seeds wouldn't reproduce on CI.
namespace bench {
  inline constexpr ob::PriceBand kBand{.floor = 9000, .ceil = 11000};
  inline constexpr ob::Price kMid{10000};
  inline constexpr ob::Price kMaxOffset{50};  // resting prices are mid -/+ [1..50]
  inline constexpr ob::Qty kMaxQty{100};
  // ArrayBook pool size for every bench config; ReferenceBook reserves the same.
  inline constexpr std::size_t kCap{16384};

  class Rng {
   public:
    explicit Rng(std::uint64_t seed) : gen_{seed} {}
    std::uint64_t below(std::uint64_t range) { return gen_() % range; }
    ob::Side side() { return below(2) == 0 ? ob::Side::kBuy : ob::Side::kSell; }
    // Non-crossing: buys strictly below mid, sells strictly above.
    ob::Price price(ob::Side s) {
      const auto off{1 + static_cast<ob::Price>(below(static_cast<std::uint64_t>(kMaxOffset)))};
      return s == ob::Side::kBuy ? kMid - off : kMid + off;
    }
    ob::Price priceOtherThan(ob::Side s, ob::Price old) {
      ob::Price p{price(s)};
      while (p == old) {
        p = price(s);
      }
      return p;
    }
    // >= 2 so the qty-down phase (qty - 1) never reaches 0.
    ob::Qty qty() { return 2 + static_cast<ob::Qty>(below(kMaxQty - 1)); }

   private:
    std::mt19937_64 gen_;
  };

  inline std::vector<ob::NewOrder> makeResting(Rng& rng, std::size_t n) {
    std::vector<ob::NewOrder> out(n);
    for (auto& o : out) {
      o.side = rng.side();
      o.price = rng.price(o.side);
      o.qty = rng.qty();
    }
    return out;
  }

  inline std::vector<ob::Price> makeModifyPrices(Rng& rng,
                                                 const std::vector<ob::NewOrder>& resting) {
    std::vector<ob::Price> out;
    out.reserve(resting.size());
    for (const auto& o : resting) {
      out.push_back(rng.priceOtherThan(o.side, o.price));
    }
    return out;
  }

  inline std::vector<std::size_t> makeCancelOrder(Rng& rng, std::size_t n) {
    std::vector<std::size_t> out(n);
    for (std::size_t i{}; i < n; ++i) {
      out[i] = i;
    }
    for (std::size_t i{n}; i > 1; --i) {
      std::swap(out[i - 1], out[static_cast<std::size_t>(rng.below(i))]);
    }
    return out;
  }

  // Phase inputs for the batch/block benches: handle i is resting[i] throughout.
  struct Workload {
    std::vector<ob::NewOrder> resting;
    std::vector<ob::Price> modifyPx;
    std::vector<std::size_t> cancelOrder;
  };

  inline Workload makeWorkload(std::uint64_t seed, std::size_t n) {
    Rng rng{seed};
    Workload w;
    w.resting = makeResting(rng, n);
    w.modifyPx = makeModifyPrices(rng, w.resting);
    w.cancelOrder = makeCancelOrder(rng, n);
    return w;
  }
}  // namespace bench
