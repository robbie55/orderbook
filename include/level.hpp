#ifndef OB_LEVEL_HPP
#define OB_LEVEL_HPP

#include "order.hpp"
#include "types.hpp"

namespace ob {

// A single price level: an intrusive FIFO of resting orders plus O(1)
// aggregates. Price is IMPLICIT from the level's array index, so it is not
// stored here (saves 8 bytes on a hot, densely-instantiated struct).
//
// `total_qty` and `order_count` are maintained incrementally so market-data
// queries and the FOK pre-scan are O(1) rather than O(list length).
struct Level {
    Order* head{};               // oldest resting order (front of FIFO)
    Order* tail{};               // newest resting order (back of FIFO)
    Qty total_qty{};             // sum of remaining qty across the level
    std::uint32_t order_count{}; // number of resting orders

    [[nodiscard]] bool empty() const noexcept { return head == nullptr; }

    // Append a resting order at the back (loses to all existing time priority).
    void push_back(Order* o) noexcept {
        o->prev = tail;
        o->next = nullptr;

        if (tail != nullptr) {
            tail->next = o;
        } else {
            head = o;
        }

        tail = o;
        total_qty += o->remaining;
        ++order_count;
    }

    // Unlink an order that belongs to this level. O(1);
    void unlink(Order* o) noexcept {
        if (o->prev != nullptr) {
            o->prev->next = o->next;
        } else {
            head = o->next;
        }

        if (o->next != nullptr) {
            o->next->prev = o->prev;
        } else {
            tail = o->prev;
        }

        o->prev = nullptr;
        o->next = nullptr;
        --order_count;
    }
};

} // namespace ob

#endif // OB_LEVEL_HPP
