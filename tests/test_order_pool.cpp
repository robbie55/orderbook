#include <cstdint>
#include <new>
#include <vector>

#include <gtest/gtest.h>

#include "order.hpp"
#include "order_pool.hpp"

namespace {

using namespace ob;

TEST(OrderPool, AllocateReturnsAlignedStorage) {
    OrderPool<Order, 16> pool{};
    for (int i = 0; i < 16; ++i) {
        Order* o = pool.allocate();
        ASSERT_NE(o, nullptr);
        const auto addr = reinterpret_cast<std::uintptr_t>(o);
        EXPECT_EQ(addr % alignof(Order), 0U) << "node not aligned for T";
    }
}

TEST(OrderPool, ExhaustionThrowsBadAlloc) {
    OrderPool<Order, 4> pool{};
    std::vector<Order*> live;
    for (int i = 0; i < 4; ++i) {
        live.push_back(pool.allocate());
    }
    EXPECT_EQ(pool.in_use(), 4U);
    EXPECT_THROW((void)pool.allocate(), std::bad_alloc);

    // Freeing one makes exactly one more allocation succeed.
    pool.deallocate(live.back());
    live.pop_back();
    EXPECT_NO_THROW({ live.push_back(pool.allocate()); });
    EXPECT_THROW((void)pool.allocate(), std::bad_alloc);
}

TEST(OrderPool, DeallocateReusesStorage) {
    OrderPool<Order> pool{2};
    Order* a = pool.allocate();
    Order* b = pool.allocate();
    pool.deallocate(a);
    Order* c = pool.allocate(); // should hand back a freed cell (LIFO free list)
    EXPECT_EQ(c, a);
    pool.deallocate(b);
    pool.deallocate(c);
    EXPECT_EQ(pool.in_use(), 0U);
}

TEST(OrderPool, CreateDestroyRoundTrip) {
    OrderPool<Order> pool{8};
    Order* o = pool.create(Order{.id = 42,
                                 .price = 10'050,
                                 .qty = 100,
                                 .remaining = 100,
                                 .prev = nullptr,
                                 .next = nullptr,
                                 .side = Side::Buy});
    ASSERT_NE(o, nullptr);
    EXPECT_EQ(o->id, 42U);
    EXPECT_EQ(o->price, 10'050);
    EXPECT_EQ(o->remaining, 100U);
    pool.destroy(o);
    EXPECT_EQ(pool.in_use(), 0U);
}

TEST(OrderPool, NoLeakAfterChurn) {
    OrderPool<Order> pool{64};
    std::vector<Order*> live;
    for (int round = 0; round < 1000; ++round) {
        // Allocate a batch, free half, keep churning within capacity.
        while (pool.in_use() < 64 && live.size() < 64) {
            live.push_back(pool.create(Order{}));
        }
        for (int k = 0; k < 32 && !live.empty(); ++k) {
            pool.destroy(live.back());
            live.pop_back();
        }
    }
    for (Order* o : live) {
        pool.destroy(o);
    }
    EXPECT_EQ(pool.in_use(), 0U);
}

} // namespace
