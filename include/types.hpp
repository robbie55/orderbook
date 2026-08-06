#ifndef OB_TYPES_HPP
#define OB_TYPES_HPP

#include <cstddef>
#include <cstdint>

// Core value types for the order book.
//
// Design note: prices are INTEGER TICKS. There is no floating point anywhere in
// the book or matching engine. One `Price` unit == one tick; if an instrument
// quotes in $0.01 increments, callers scale to ticks at the feed boundary.
namespace ob {

  using Price = std::int64_t;     // integer ticks; never floating point
  using Qty = std::uint64_t;      // shares/contracts/lots
  using OrderId = std::uint64_t;  // exchange/session order reference

  enum class Side : std::uint8_t { kBuy = 0, kSell = 1 };
  enum class OrderType : std::uint8_t { kLimit = 0, kMarket = 1, kIoc = 2, kFok = 3 };

  [[nodiscard]] constexpr Side Opposite(Side s) noexcept { return s == Side::kBuy ? Side::kSell : Side::kBuy; }

  // A command entering the book. For Market orders `price` is ignored.
  struct NewOrder {
    OrderId id{};
    Side side{Side::kBuy};
    OrderType type{OrderType::kLimit};
    Price price{};
    Qty qty{};
  };

  // A fill. Price is always the PASSIVE (resting) order's price, per exchange
  // convention — the aggressor takes liquidity at the maker's posted price.
  struct Trade {
    OrderId taker{};  // aggressing order
    OrderId maker{};  // resting order that was hit
    Price price{};    // passive order's price
    Qty qty{};        // filled quantity
    Side takerSize{Side::kBuy};
  };

  // Maps a contiguous band of integer tick prices onto array indices for the
  // direct-indexed book. The band is sized once at construction and never grows,
  // so indices (and any pointers into the level array) are stable for its life.
  struct PriceBand {
    Price floor{};  // lowest representable price (inclusive)
    Price ceil{};   // highest representable price (inclusive)

    [[nodiscard]] constexpr std::size_t size() const noexcept { return static_cast<std::size_t>(ceil - floor) + 1U; }
    [[nodiscard]] constexpr bool contains(Price p) const noexcept { return p >= floor && p <= ceil; }
    [[nodiscard]] constexpr std::size_t index(Price p) const noexcept { return static_cast<std::size_t>(p - floor); }
    [[nodiscard]] constexpr Price priceAt(std::size_t i) const noexcept { return floor + static_cast<Price>(i); }
  };

  // Sentinels for "no bid" / "no ask" so best-price queries stay branch-light.
  inline constexpr Price kNoBid = INT64_MIN;
  inline constexpr Price kNoAsk = INT64_MAX;

}  // namespace ob

#endif  // OB_TYPES_HPP
