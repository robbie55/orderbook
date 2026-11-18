#include <gtest/gtest.h>

#include <array_book.hpp>
#include <cstdint>
#include <memory>
#include <random>
#include <reference_book.hpp>
#include <string>
#include <vector>

#include "types.hpp"

namespace {
  ob::NewOrder buy(ob::Price p, ob::Qty q) {
    return {.side = ob::Side::kBuy, .price = p, .qty = q};
  }
  ob::NewOrder sell(ob::Price p, ob::Qty q) {
    return {.side = ob::Side::kSell, .price = p, .qty = q};
  }
}  // namespace

TEST(ReferenceBook, EmptyBookReturnsSentinels) {
  ReferenceBook book;
  EXPECT_EQ(book.bestBid(), ob::kNoBid);
  EXPECT_EQ(book.bestAsk(), ob::kNoAsk);
  EXPECT_EQ(book.qtyAt(ob::Side::kBuy, 100), 0);
}

TEST(ReferenceBook, AddSetsBestAndQty) {
  ReferenceBook book;
  book.add(buy(99, 5));
  book.add(buy(100, 3));
  book.add(sell(102, 4));
  book.add(sell(101, 2));
  EXPECT_EQ(book.bestBid(), 100);
  EXPECT_EQ(book.bestAsk(), 101);
  book.add(buy(100, 7));
  EXPECT_EQ(book.qtyAt(ob::Side::kBuy, 100), 10);
}

TEST(ReferenceBook, CancelLastAtBestMovesBestInward) {
  ReferenceBook book;
  book.add(buy(99, 5));
  const auto top{book.add(buy(100, 3))};
  book.add(sell(102, 4));
  const auto ask{book.add(sell(101, 2))};
  book.cancel(top);
  book.cancel(ask);
  EXPECT_EQ(book.bestBid(), 99);
  EXPECT_EQ(book.bestAsk(), 102);
  EXPECT_EQ(book.qtyAt(ob::Side::kBuy, 100), 0);
}

TEST(ReferenceBook, CancelLastOrderEmptiesSide) {
  ReferenceBook book;
  const auto b{book.add(buy(100, 3))};
  const auto s{book.add(sell(101, 3))};
  book.cancel(b);
  book.cancel(s);
  EXPECT_EQ(book.bestBid(), ob::kNoBid);
  EXPECT_EQ(book.bestAsk(), ob::kNoAsk);
}

TEST(ReferenceBook, CancelNonBestKeepsBest) {
  ReferenceBook book;
  const auto low{book.add(buy(99, 5))};
  book.add(buy(100, 3));
  book.cancel(low);
  EXPECT_EQ(book.bestBid(), 100);
  EXPECT_EQ(book.qtyAt(ob::Side::kBuy, 99), 0);
}

TEST(ReferenceBook, ModifyQtyDownInPlace) {
  ReferenceBook book;
  const auto id{book.add(buy(100, 10))};
  book.add(buy(100, 5));
  book.modify(id, 100, 4);
  EXPECT_EQ(book.qtyAt(ob::Side::kBuy, 100), 9);
  EXPECT_EQ(book.bestBid(), 100);
}

TEST(ReferenceBook, ModifyQtyUpSamePrice) {
  ReferenceBook book;
  const auto id{book.add(sell(101, 2))};
  book.modify(id, 101, 6);
  EXPECT_EQ(book.qtyAt(ob::Side::kSell, 101), 6);
  EXPECT_EQ(book.bestAsk(), 101);
}

TEST(ReferenceBook, ModifyPriceMovesQtyBetweenLevels) {
  ReferenceBook book;
  const auto id{book.add(buy(100, 10))};
  book.add(buy(98, 1));
  book.modify(id, 97, 8);
  EXPECT_EQ(book.qtyAt(ob::Side::kBuy, 100), 0);
  EXPECT_EQ(book.qtyAt(ob::Side::kBuy, 97), 8);
  EXPECT_EQ(book.bestBid(), 98);
}

TEST(ReferenceBook, ModifyQtyToZeroRemovesOrder) {
  ReferenceBook book;
  const auto id{book.add(sell(105, 3))};
  book.modify(id, 105, 0);
  EXPECT_EQ(book.qtyAt(ob::Side::kSell, 105), 0);
  EXPECT_EQ(book.bestAsk(), ob::kNoAsk);
}

TEST(ReferenceBook, ModifiedOrderStillCancellable) {
  ReferenceBook book;
  const auto id{book.add(buy(100, 10))};
  book.modify(id, 101, 10);
  book.modify(id, 101, 4);
  book.cancel(id);
  EXPECT_EQ(book.bestBid(), ob::kNoBid);
  EXPECT_EQ(book.qtyAt(ob::Side::kBuy, 101), 0);
}

// ---- Differential: ArrayBook vs ReferenceBook on identical seeded op streams ----

namespace {
  constexpr ob::PriceBand kSmallBand{.floor = 9950, .ceil = 10050};
  constexpr std::size_t kDiffOps{5000};
  constexpr std::size_t kSweepEvery{256};

  // One live order as the test sees it; ids differ per book, so ops go through handles.
  struct Handle {
    ob::OrderId arrayId;
    ob::OrderId refId;
    ob::Side side;
    ob::Price price;
    ob::Qty qty;
  };

  class DiffRun {
   public:
    explicit DiffRun(std::uint64_t seed)
        : seed_{seed}, rng_{seed}, array_{std::make_unique<ArrayBook<kDiffOps>>(kSmallBand)} {}

    void run() {
      for (op_ = 0; op_ < kDiffOps; ++op_) {
        const ob::Price touched{step()};
        if (!sameTop() || !sameQtyBothSides(touched)) {
          return;
        }
        if (op_ % kSweepEvery == 0 && !sweep()) {
          return;
        }
      }
      sweep();
    }

   private:
    std::uint64_t seed_;
    std::mt19937_64 rng_;
    std::unique_ptr<ArrayBook<kDiffOps>> array_;
    ReferenceBook ref_;
    std::vector<Handle> live_;
    std::size_t op_{};

    std::uint64_t draw(std::uint64_t range) { return rng_() % range; }
    ob::Price drawPrice() {
      return kSmallBand.floor + static_cast<ob::Price>(draw(kSmallBand.size()));
    }
    ob::Qty drawQty() { return 1 + static_cast<ob::Qty>(draw(100)); }

    // Applies one random op to both books; returns a price it touched (for spot checks).
    ob::Price step() {
      if (live_.empty() || draw(4) == 0) {
        return add();
      }
      const std::size_t h{static_cast<std::size_t>(draw(live_.size()))};
      Handle& o{live_[h]};
      const ob::Price old{o.price};
      switch (draw(6)) {
        case 0:
          array_->cancel(o.arrayId);
          ref_.cancel(o.refId);
          kill(h);
          return old;
        case 1: {  // price change, any qty
          ob::Price p{drawPrice()};
          while (p == old) {
            p = drawPrice();
          }
          modify(h, p, drawQty());
          return old;
        }
        case 2:  // qty up, same price
          modify(h, old, o.qty + drawQty());
          return old;
        case 3:  // qty down, may hit 0
          modify(h, old, static_cast<ob::Qty>(draw(static_cast<std::uint64_t>(o.qty))));
          return old;
        case 4:  // no-op
          modify(h, old, o.qty);
          return old;
        default:
          return add();
      }
    }

    ob::Price add() {
      const ob::NewOrder o{.side = draw(2) == 0 ? ob::Side::kBuy : ob::Side::kSell,
                           .price = drawPrice(),
                           .qty = drawQty()};
      live_.push_back({.arrayId = array_->add(o),
                       .refId = ref_.add(o),
                       .side = o.side,
                       .price = o.price,
                       .qty = o.qty});
      return o.price;
    }

    void modify(std::size_t h, ob::Price p, ob::Qty q) {
      Handle& o{live_[h]};
      array_->modify(o.arrayId, p, q);
      ref_.modify(o.refId, p, q);
      o.price = p;
      o.qty = q;
      if (q == 0) {
        kill(h);
      }
    }

    void kill(std::size_t h) {
      live_[h] = live_.back();
      live_.pop_back();
    }

    std::string where() const {
      return "seed " + std::to_string(seed_) + " op " + std::to_string(op_);
    }

    bool sameTop() {
      if (array_->bestBid() != ref_.bestBid() || array_->bestAsk() != ref_.bestAsk()) {
        ADD_FAILURE() << where() << ": best bid/ask array " << array_->bestBid() << "/"
                      << array_->bestAsk() << " ref " << ref_.bestBid() << "/" << ref_.bestAsk();
        return false;
      }
      return true;
    }

    bool sameQty(ob::Side s, ob::Price p) {
      if (array_->qtyAt(s, p) != ref_.qtyAt(s, p)) {
        ADD_FAILURE() << where() << ": qtyAt(" << (s == ob::Side::kBuy ? "buy" : "sell") << ", "
                      << p << ") array " << array_->qtyAt(s, p) << " ref " << ref_.qtyAt(s, p);
        return false;
      }
      return true;
    }

    bool sameQtyBothSides(ob::Price p) {
      return sameQty(ob::Side::kBuy, p) && sameQty(ob::Side::kSell, p);
    }

    bool sweep() {
      for (ob::Price p{kSmallBand.floor}; p <= kSmallBand.ceil; ++p) {
        if (!sameQtyBothSides(p)) {
          return false;
        }
      }
      return true;
    }
  };
}  // namespace

TEST(Differential, ArrayBookMatchesReferenceBook) {
  for (std::uint64_t seed{1}; seed <= 20; ++seed) {
    DiffRun{seed}.run();
    if (HasFailure()) {
      return;
    }
  }
}
