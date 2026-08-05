#include <cstdint>
#include <new>
#include <vector>

#include <gtest/gtest.h>

#include "object_pool.hpp"
#include "order.hpp"

namespace {

using namespace ob;

TEST(ObjectPool, AllocateReturnsAlignedStorage) {
}

TEST(ObjectPool, ExhaustionThrowsBadAlloc) {
}

TEST(ObjectPool, DeallocateReusesStorage) {
}

TEST(ObjectPool, CreateDestroyRoundTrip) {
}

TEST(ObjectPool, NoLeakAfterChurn) {
}

} // namespace
