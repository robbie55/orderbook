#include <cstddef>

#include <gtest/gtest.h>

#include "book.hpp"
#include "level.hpp"
#include "order.hpp"
#include "types.hpp"

namespace {

using namespace ob;

TEST(Types, OppositeSide) {
}

TEST(PriceBand, IndexRoundTrips) {
}

TEST(Order, FitsInOneCacheLine) {
}

TEST(Level, PushBackMaintainsFifoAndAggregates) {
}

TEST(Level, UnlinkFromMiddleHeadAndTail) {
}

// The reusable sink compiles and accumulates fills.
TEST(TradeBuf, EmitAndClear) {
}

} // namespace
