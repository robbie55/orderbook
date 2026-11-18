#pragma once

#include <algorithm>
#include <cassert>
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

  enum class OpKind : std::uint8_t { kAdd, kModifyPx, kCancel };
  // kAdd uses `order` and takes handle = next fresh index; kModifyPx uses px/qty.
  struct Op {
    OpKind kind;
    std::size_t handle;
    ob::NewOrder order;
    ob::Price px;
    ob::Qty qty;
  };
  struct Mix {
    unsigned add, modifyPx, cancel;  // percent, sums to 100
  };
  struct MixedStream {
    std::vector<ob::NewOrder> preload;  // handles 0..n-1
    std::vector<Op> ops;
  };

  // Preload n resting orders, then `ops` interleaved ops on live handles. Kind is drawn by
  // `mix`, except an add is forced when live <= 0.95n and a cancel when live >= min(1.05n,
  // cap), so the resting set (and its cache footprint) stays ~n.
  inline MixedStream makeMixed(std::uint64_t seed, std::size_t n, std::size_t ops, Mix mix,
                               std::size_t cap) {
    assert(mix.add + mix.modifyPx + mix.cancel == 100);
    const std::size_t lo{(n * 95 + 99) / 100};
    const std::size_t hi{std::min(n * 105 / 100, cap)};
    assert(n >= lo && n <= hi);

    Rng rng{seed};
    MixedStream s;
    s.preload = makeResting(rng, n);
    std::vector<ob::NewOrder> state{s.preload};  // current side/price/qty per handle
    std::vector<std::size_t> live(n);
    for (std::size_t i{}; i < n; ++i) {
      live[i] = i;
    }

    s.ops.reserve(ops);
    for (std::size_t k{}; k < ops; ++k) {
      const auto roll{rng.below(100)};
      OpKind kind{roll < mix.add                  ? OpKind::kAdd
                  : roll < mix.add + mix.modifyPx ? OpKind::kModifyPx
                                                  : OpKind::kCancel};
      if (kind == OpKind::kCancel && live.size() <= lo) {
        kind = OpKind::kAdd;
      } else if (kind == OpKind::kAdd && live.size() >= hi) {
        kind = OpKind::kCancel;
      }

      Op op{.kind = kind, .handle = 0, .order = {}, .px = 0, .qty = 0};
      if (kind == OpKind::kAdd) {
        op.order.side = rng.side();
        op.order.price = rng.price(op.order.side);
        op.order.qty = rng.qty();
        op.handle = state.size();
        live.push_back(op.handle);
        state.push_back(op.order);
      } else {
        const auto j{static_cast<std::size_t>(rng.below(live.size()))};
        op.handle = live[j];
        ob::NewOrder& o{state[op.handle]};
        if (kind == OpKind::kModifyPx) {
          o.price = rng.priceOtherThan(o.side, o.price);
          op.px = o.price;
          op.qty = o.qty;
        } else {
          live[j] = live.back();
          live.pop_back();
        }
      }
      s.ops.push_back(op);
    }
    return s;
  }
}  // namespace bench
