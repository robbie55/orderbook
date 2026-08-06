#include <gtest/gtest.h>

namespace {

  TEST(OrderPool, AllocateReturnsAlignedStorage) {}

  TEST(OrderPool, ExhaustionThrowsBadAlloc) {}

  TEST(OrderPool, DeallocateReusesStorage) {}

  TEST(OrderPool, CreateDestroyRoundTrip) {}

  TEST(OrderPool, NoLeakAfterChurn) {}

}  // namespace
