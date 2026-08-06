#ifndef OB_BOOK_HPP
#define OB_BOOK_HPP

#include <concepts>
#include <vector>

#include "types.hpp"

namespace ob {

  // reuseable trade sink
  struct TradeBuf {
    std::vector<Trade> fills;

    void clear() noexcept { fills.clear(); }
    void emit(const Trade& t) { fills.push_back(t); }
    [[nodiscard]] bool empty() const noexcept { return fills.empty(); }
    [[nodiscard]] std::size_t size() const noexcept { return fills.size(); }
  };

  // use concept over inheritance
  template <class B>
  concept BookLike = requires(B b, const NewOrder& o, OrderId id, Qty q, Price p, TradeBuf& tb) {
    { b.submit(o, tb) } -> std::same_as<void>;  // match + (maybe) rest remainder
    { b.cancel(id) } -> std::same_as<bool>;     // false if unknown/already gone
    { b.modify(id, q, p, tb) } -> std::same_as<bool>;
    { b.best_bid() } -> std::same_as<Price>;  // kNoBid if empty
    { b.best_ask() } -> std::same_as<Price>;  // kNoAsk if empty
    { b.qty_at(Side::kBuy, p) } -> std::same_as<Qty>;
  };

}  // namespace ob

#endif  // OB_BOOK_HPP
