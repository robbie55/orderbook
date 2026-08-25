#include <gtest/gtest.h>

#include "array_book.hpp"
#include "level.hpp"
#include "order.hpp"
#include "types.hpp"

namespace {

  using namespace ob;

  // ------------------------------------------------------------------ empty book
  TEST(ArrayBookBest, EmptyBookHasNoBidNoAsk) {
    PriceBand band{.floor = 9000, .ceil = 11000};
    ArrayBook<10000> arrayBook{band};
  }

  // ------------------------------------------------------------------------- add
  TEST(ArrayBookAdd, FirstBidSetsBestBid) {}
  TEST(ArrayBookAdd, FirstAskSetsBestAsk) {}
  TEST(ArrayBookAdd, HigherBidImprovesBestBid) {}
  TEST(ArrayBookAdd, LowerBidDoesNotMoveBestBid) {}
  TEST(ArrayBookAdd, LowerAskImprovesBestAsk) {}
  TEST(ArrayBookAdd, HigherAskDoesNotMoveBestAsk) {}
  TEST(ArrayBookAdd, SamePriceOrdersQueueFifo) {}  // head == first, order_count == 2
  TEST(ArrayBookAdd, SamePriceAccumulatesTotalQty) {}
  TEST(ArrayBookAdd, BestBidStaysBelowBestAsk) {}
  TEST(ArrayBookAdd, OrdersAtBandFloorAndCeil) {}  // extreme indices are valid

  // ---------------------------------------------------------------------- cancel
  TEST(ArrayBookCancel, RemovesLoneOrderEmptiesLevel) {}
  TEST(ArrayBookCancel, CancelBestBidAdvancesToNextLower) {}
  TEST(ArrayBookCancel, CancelBestAskAdvancesToNextHigher) {}
  TEST(ArrayBookCancel, AdvanceScanSkipsEmptyLevels) {}  // gap between best and next live
  TEST(ArrayBookCancel, CancelNonBestLevelLeavesBestUnchanged) {}
  TEST(ArrayBookCancel, CancelLastOrderOnSideBecomesEmpty) {}  // best -> kNoBid / kNoAsk
  TEST(ArrayBookCancel, CancelMiddleOfFifoPreservesNeighbors) {}
  TEST(ArrayBookCancel, UpdatesTotalQtyAndOrderCount) {}
  TEST(ArrayBookCancel, FreedSlotIsReusedByNextAdd) {}  // id == pool slot

  // ---------------------------------------------------------------------- modify
  TEST(ArrayBookModify, QtyDownKeepsTimePriority) {}  // stays head, totalQty -= delta
  TEST(ArrayBookModify, QtyDownToZeroCancelsOrder) {}
  TEST(ArrayBookModify, QtyUpMovesToBackOfSameLevel) {}  // loses priority, level unchanged
  TEST(ArrayBookModify, RepriceMovesOrderToNewLevel) {}  // old level empties, new holds it
  TEST(ArrayBookModify, RepriceToBetterPriceImprovesBest) {}
  TEST(ArrayBookModify, RepriceEmptyingBestAdvancesBest) {}
  TEST(ArrayBookModify, PreservesOrderIdAcrossReprice) {}
  TEST(ArrayBookModify, TotalQtyConsistentOnBothLevelsAfterReprice) {}

}  // namespace
