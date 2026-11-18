#include <gtest/gtest.h>

#include <reference_book.hpp>

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
