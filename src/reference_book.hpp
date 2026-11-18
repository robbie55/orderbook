#pragma once

#include <cstddef>
#include <functional>
#include <iterator>
#include <list>
#include <map>
#include <unordered_map>

#include "types.hpp"

// std::map oracle + benchmark baseline. Mirrors ArrayBook's current API, not BookLike
// (Stage 3 changes both). Like ArrayBook: no matching, NewOrder::id ignored, and
// cancel/modify on an unknown id is a precondition violation.
class ReferenceBook {
 public:
  // Reserving the id index up front mirrors ArrayBook's preallocated pool, so the
  // baseline doesn't pay rehashes the array book never pays.
  explicit ReferenceBook(std::size_t expectedOrders = 0) { index_.reserve(expectedOrders); }

  ob::OrderId add(const ob::NewOrder& o) {
    const ob::OrderId id{nextId_++};
    index_.emplace(id,
                   Loc{.side = o.side, .price = o.price, .it = link(id, o.side, o.price, o.qty)});
    return id;
  }

  void cancel(ob::OrderId id) {
    const auto node{index_.find(id)};
    unlink(node->second);
    index_.erase(node);
  }

  // Same branches as ArrayBook::modify: same price and qty not up -> in place (qty 0
  // cancels); price change or qty up -> back of the FIFO at the new price.
  void modify(ob::OrderId id, ob::Price newPrice, ob::Qty newQty) {
    Loc& loc{index_.at(id)};
    if (newPrice == loc.price && newQty <= loc.it->remaining) {
      levelAt(loc.side, loc.price).totalQty -= loc.it->remaining - newQty;
      loc.it->remaining = newQty;
      if (newQty == 0) {
        cancel(id);
      }
      return;
    }
    unlink(loc);
    loc.price = newPrice;
    loc.it = link(id, loc.side, newPrice, newQty);
  }

  ob::Price bestBid() { return bids_.empty() ? ob::kNoBid : bids_.begin()->first; }
  ob::Price bestAsk() { return asks_.empty() ? ob::kNoAsk : asks_.begin()->first; }

  ob::Qty qtyAt(ob::Side s, ob::Price p) {
    return s == ob::Side::kBuy ? qtyIn(bids_, p) : qtyIn(asks_, p);
  }

 private:
  struct Resting {
    ob::OrderId id;
    ob::Qty remaining;
  };
  using Fifo = std::list<Resting>;
  struct Level {
    Fifo fifo;
    ob::Qty totalQty{};
  };
  struct Loc {
    ob::Side side;
    ob::Price price;
    Fifo::iterator it;
  };

  std::map<ob::Price, Level, std::greater<>> bids_;  // begin() = best bid
  std::map<ob::Price, Level, std::less<>> asks_;     // begin() = best ask
  std::unordered_map<ob::OrderId, Loc> index_;
  ob::OrderId nextId_{};

  template <class Map>
  static ob::Qty qtyIn(const Map& m, ob::Price p) {
    const auto lvl{m.find(p)};
    return lvl == m.end() ? 0 : lvl->second.totalQty;
  }

  Level& levelAt(ob::Side s, ob::Price p) { return s == ob::Side::kBuy ? bids_[p] : asks_[p]; }

  Fifo::iterator link(ob::OrderId id, ob::Side s, ob::Price p, ob::Qty q) {
    Level& lvl{levelAt(s, p)};
    lvl.fifo.push_back({.id = id, .remaining = q});
    lvl.totalQty += q;
    return std::prev(lvl.fifo.end());
  }

  void unlink(const Loc& loc) {
    if (loc.side == ob::Side::kBuy) {
      unlinkFrom(bids_, loc);
    } else {
      unlinkFrom(asks_, loc);
    }
  }

  // Erases the level when its FIFO empties so begin() stays the best price.
  template <class Map>
  static void unlinkFrom(Map& m, const Loc& loc) {
    const auto lvl{m.find(loc.price)};
    lvl->second.totalQty -= loc.it->remaining;
    lvl->second.fifo.erase(loc.it);
    if (lvl->second.fifo.empty()) {
      m.erase(lvl);
    }
  }
};
