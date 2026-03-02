#pragma once

#include <cstdint>
#include <string>

namespace ob {

// Price in integer ticks (e.g., cents). No doubles for determinism.
using PriceTicks = std::int64_t;

// Quantity in integer lots (e.g., shares, contracts)
using Qty = std::int64_t;

// Generated unique ID.
using OrderId = std::uint64_t;

// Buy / Sell
enum class Side : std::uint8_t {
    kBuy = 0,
    kSell = 1
};

// Limit / Market Order Type
enum class OrderType : std::uint8_t {
    kLimit = 0,
    kMarket = 1
};

// A fill produced by matching.
struct Trade {
    OrderId taker_id{}; // incoming
    OrderId maker_id{}; // resting
    PriceTicks price{}; // execution price = maker price
    Qty qty{};
};

// L2 snapshot (aggregated quantity by price level)
struct Level {
    PriceTicks price{}; // price level
    Qty qty{};          // total quantity available at that price (summed across all resting orders)
};

// Small validations
inline constexpr bool isValidPrice(PriceTicks p) noexcept { return p > 0; }
inline constexpr bool isValidQty(Qty q) noexcept { return q > 0; }

inline std::string ToString(Side s) {
    return (s == Side::kBuy) ? "buy" : "sell";
}

} // namespace ob
