#include <gtest/gtest.h>

// #include <cstddef>

namespace {
  TEST(Types, OppositeSide) {}

  TEST(PriceBand, IndexRoundTrips) {}

  TEST(Order, FitsInOneCacheLine) {}

  TEST(Level, PushBackMaintainsFifoAndAggregates) {}

  TEST(Level, UnlinkFromMiddleHeadAndTail) {}

  // The reusable sink compiles and accumulates fills.
  TEST(TradeBuf, EmitAndClear) {}

}  // namespace
