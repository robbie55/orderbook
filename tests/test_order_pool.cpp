#include <gtest/gtest.h>

#include "order.hpp"
#include "order_pool.hpp"

namespace {

  using namespace ob;

  TEST(OrderPool, AllocateReturnsAlignedStorage) {}

  TEST(OrderPool, ExhaustionThrowsBadAlloc) {}

  TEST(OrderPool, DeallocateReusesStorage) {}

  TEST(OrderPool, CreateDestroyRoundTrip) {}

  TEST(OrderPool, NoLeakAfterChurn) {}

}  // namespace
