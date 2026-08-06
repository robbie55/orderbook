#ifndef OB_ORDER_HPP
#define OB_ORDER_HPP

#include "types.hpp"

namespace ob {

  // An intrusive order node. Lives in a per-price FIFO doubly-linked list and is
  // allocated from the object pool. Deliberately holds NO back-pointer to its
  // owning Level: the level is recovered in O(1) from `price` via PriceBand, which
  // keeps the node one cache line and avoids a dangling pointer if a level array
  // were ever relocated. `prev`/`next` thread the FIFO (head = oldest = highest
  // time priority).
  //
  // Extends the `{ id, price }` seed from the pool-allocator project.
  struct Order {
    OrderId id{};
    Price price{};    // resting price in ticks (unused semantics for in-flight market orders)
    Qty qty{};        // original submitted quantity
    Qty remaining{};  // unfilled quantity; 0 => fully filled
    Order* prev{};    // older order at this level (nullptr => head)
    Order* next{};    // newer order at this level (nullptr => tail)
    Side side{Side::kBuy};
  };

  // One cache line is the budget. If this ever fails, something non-essential
  // crept into the hot node — push it out to a cold side-table keyed by id.
  static_assert(sizeof(Order) <= 64, "Order node must fit in a single cache line");
  static_assert(alignof(Order) <= 64, "Order alignment must not exceed a cache line");

}  // namespace ob

#endif  // OB_ORDER_HPP
