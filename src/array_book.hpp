#pragma once

#include <book.hpp>
#include <cassert>
#include <vector>

#include "level.hpp"
#include "order.hpp"
#include "order_pool.hpp"
#include "types.hpp"

template <size_t BlockCount = 10000>
class ArrayBook {
 private:
  ob::OrderPool<ob::Order, BlockCount> pool_;
  ob::PriceBand band_{};
  std::vector<ob::Level> asks_;
  std::vector<ob::Level> bids_;

  // direct access to asks_ and bids_ best Level, updated in bestOnInsert and bestOnEmpty
  std::size_t bestAskIdx_;
  std::size_t bestBidIdx_;

  ob::Order* buildOrder(const ob::NewOrder& o);

  std::vector<ob::Level>& sideVec(ob::Side s);
  [[nodiscard]] std::size_t idxOf(ob::Price p) const;

  void link(ob::Order* o, std::size_t idx);

  void unlink(ob::Order* o, std::size_t idx);

  // called when adding any order, any new order has the potential for being new best
  void bestOnInsert(ob::Side s, std::size_t idx);

  // called only when cancelling an order leads to an empty level
  void bestOnEmpty(ob::Side s, std::size_t idx);

 public:
  ArrayBook() = delete;

  explicit ArrayBook(ob::PriceBand band);

  ArrayBook(const ArrayBook& other) = delete;
  ArrayBook& operator=(const ArrayBook& rhs) = delete;
  ArrayBook(ArrayBook&& other) = delete;
  ArrayBook& operator=(ArrayBook&& rhs) = delete;

  ~ArrayBook() = default;

  void add(const ob::NewOrder& newOrder);

  void cancel(ob::OrderId id);

  void modify(ob::OrderId id, ob::Price newPrice, ob::Qty newQty);

  ob::Price bestAsk();
  ob::Price bestBid();

  ob::Qty qtyAt(ob::Side side, ob::Price price);
};

template <size_t BlockCount>
ob::Order* ArrayBook<BlockCount>::buildOrder(const ob::NewOrder& o) {
  ob::Order* order{pool_.allocate()};
  order->id = static_cast<ob::OrderId>(pool_.getIndex(order));
  order->price = o.price;
  order->qty = o.qty;
  order->remaining = o.qty;
  order->side = o.side;
  return order;
}

template <size_t BlockCount>
std::vector<ob::Level>& ArrayBook<BlockCount>::sideVec(ob::Side s) {
  return s == ob::Side::kBuy ? bids_ : asks_;
}

template <size_t BlockCount>
std::size_t ArrayBook<BlockCount>::idxOf(ob::Price p) const {
  assert(p >= band_.floor && p <= band_.ceil && "Invalid price given to idxOf");
  return band_.index(p);
}

template <size_t BlockCount>
void ArrayBook<BlockCount>::link(ob::Order* o, std::size_t idx) {
  std::vector<ob::Level>& side{sideVec(o->side)};
  auto& lvl{side[idx]};

  lvl.pushBack(o);
  bestOnInsert(o->side, idx);
}

template <size_t BlockCount>
void ArrayBook<BlockCount>::unlink(ob::Order* o, std::size_t idx) {
  std::vector<ob::Level>& side{sideVec(o->side)};
  auto& lvl{side[idx]};

  lvl.unlink(o);

  if (lvl.empty()) {
    bestOnEmpty(o->side, idx);
  }
}

// called when adding any order, any new order has the potential for being new best
template <size_t BlockCount>
void ArrayBook<BlockCount>::bestOnInsert(ob::Side s, std::size_t idx) {
  std::vector<ob::Level>& side{sideVec(s)};
  auto& best{s == ob::Side::kBuy ? bestBidIdx_ : bestAskIdx_};

  if (best == side.size() ||
      ((s == ob::Side::kBuy && idx > best) || (s == ob::Side::kSell && idx < best))) {
    best = idx;
  }
}

// called only when cancelling an order leads to an empty level
template <size_t BlockCount>
void ArrayBook<BlockCount>::bestOnEmpty(ob::Side s, std::size_t idx) {
  auto& best = (s == ob::Side::kBuy) ? bestBidIdx_ : bestAskIdx_;
  if (idx != best) {
    return;  // a non-top level emptied
  }

  std::vector<ob::Level>& side = sideVec(s);
  if (s == ob::Side::kBuy) {  // next best bid is lower
    for (std::size_t i = best; i-- > 0;) {
      if (!side[i].empty()) {
        best = i;
        return;
      }
    }
  } else {  // next best ask is higher
    for (std::size_t i = best + 1; i < side.size(); ++i) {
      if (!side[i].empty()) {
        best = i;
        return;
      }
    }
  }
  best = side.size();  // walked off the band → side empty
}

template <size_t BlockCount>
ArrayBook<BlockCount>::ArrayBook(ob::PriceBand band)
    : band_{band},
      asks_{band.size()},
      bids_{band.size()},
      bestAskIdx_{band.size()},
      bestBidIdx_{band.size()} {}

template <size_t BlockCount>
void ArrayBook<BlockCount>::add(const ob::NewOrder& newOrder) {
  // TODO: unconditional link for now, needs matching
  ob::Order* o{buildOrder(newOrder)};
  auto idx{idxOf(o->price)};
  link(o, idx);
}

template <size_t BlockCount>
void ArrayBook<BlockCount>::cancel(ob::OrderId id) {
  ob::Order* o{pool_.at(static_cast<std::size_t>(id))};

  auto idx{idxOf(o->price)};
  unlink(o, idx);

  pool_.deallocate(o);
}

template <size_t BlockCount>
void ArrayBook<BlockCount>::modify(ob::OrderId id, ob::Price newPrice, ob::Qty newQty) {
  // if we decrement qty, in place, if we bump qty or mutate price, cancel and readd
  // TODO: staleness check, here and in cancel
  ob::Order* o{pool_.at(static_cast<std::size_t>(id))};

  const auto priceChange{newPrice != o->price};
  const auto qtyUp{newQty > o->remaining};
  const auto idx{idxOf(o->price)};
  auto& side{sideVec(o->side)};

  if (!priceChange && !qtyUp) {
    // qty down
    auto& lvl{side[idx]};

    lvl.totalQty -= (o->remaining - newQty);
    o->remaining = newQty;

    if (o->remaining == 0) {
      cancel(id);
    }

    return;
  }

  // unlink and readd, new timestamp
  unlink(o, idx);
  o->remaining = newQty;
  o->price = newPrice;
  const auto newIdx{idxOf(newPrice)};
  link(o, newIdx);
}

template <size_t BlockCount>
ob::Price ArrayBook<BlockCount>::bestAsk() {
  // sentinel check
  if (bestAskIdx_ == band_.size()) {
    return ob::kNoAsk;
  }

  auto& lvl{asks_[bestAskIdx_]};

  return lvl.head->price;
}

template <size_t BlockCount>
ob::Price ArrayBook<BlockCount>::bestBid() {
  // sentinel check
  if (bestBidIdx_ == band_.size()) {
    return ob::kNoBid;
  }

  auto& lvl{bids_[bestBidIdx_]};

  return lvl.head->price;
}

template <size_t BlockCount>
ob::Qty ArrayBook<BlockCount>::qtyAt(ob::Side s, ob::Price p) {
  auto& side{sideVec(s)};
  auto& lvl{side[idxOf(p)]};

  return lvl.totalQty;
}
