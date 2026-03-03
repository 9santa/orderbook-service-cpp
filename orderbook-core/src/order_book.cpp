#include "order_book.h"

#include <limits>
#include <optional>
#include <stdexcept>

namespace ob {

namespace {
// Market orders are never stored; they just need a price that always crosses.
constexpr PriceTicks kPosInfPrice = std::numeric_limits<PriceTicks>::max();
constexpr PriceTicks kNegInfPrice = std::numeric_limits<PriceTicks>::min();
} // namespace

OrderBook::OrderBook(std::string symbol) : symbol_(std::move(symbol)) {
    if (symbol_.empty()) throw std::invalid_argument("symbol must not be empty");
}

PlaceResult OrderBook::Place(const NewOrder& req) {
    PlaceResult res;

    // Validation
    if (req.symbol != symbol_) { res.error = "wrong symbol for this OrderBook"; return res; }
    if (!isValidQty(req.qty)) { res.error = "qty must be > 0"; return res; }
    if (index_.contains(req.id)) { res.error = "duplicate order id"; return res; }
    if (req.type == OrderType::kLimit && !isValidPrice(req.price)) {
        res.error = "limit price must be > 0"; return res;
    }

    Order incoming;
    incoming.id = req.id;
    incoming.side = req.side;
    incoming.price = (req.type == OrderType::kLimit) ? req.price : (req.side == Side::kBuy ? kPosInfPrice : kNegInfPrice);
    incoming.qty = req.qty;
    incoming.seq = NextSeq();

    res.accepted = true;

    MatchIncoming(incoming, res.trades);

    res.filled_qty = req.qty - incoming.qty;
    res.remaining_qty = incoming.qty;
    res.fully_filled = (incoming.qty == 0);

    // Only LIMIT leftovers rest.
    if (req.type == OrderType::kLimit && incoming.qty > 0) {
        Rest(std::move(incoming));
        res.resting = true;
    }

    return res;
}

CancelResult OrderBook::Cancel(OrderId id) {
    CancelResult res;
    const auto it = index_.find(id);
    if (it == index_.end()) return res;

    const Locator loc = it->second;
    EraseByLocator(id, loc);
    res.canceled = true;
    return res;
}

AmendResult OrderBook::Amend(const AmendOrder& req) {
    AmendResult res;
    const auto it = index_.find(req.id);
    if (it == index_.end()) { res.error = "unknown order id"; return res; }

    Locator loc = it->second;
    Order& current = *loc.it;

    const PriceTicks target_price = req.new_price.value_or(current.price);
    const Qty target_qty = req.new_qty.value_or(current.qty);

    if (!isValidPrice(target_price)) { res.error = "new_price must be > 0"; return res; }
    if (target_qty < 0) { res.error = "new_qty must be >= 0"; return res; }

    // qty==0 cancel
    if (target_qty == 0) {
        (void) Cancel(req.id);
        res.amended = true;
        res.remaining_qty = 0;
        return res;
    }

    const bool price_changed = (target_price != current.price);
    const bool qty_increased = (target_qty > current.qty);

    // Lose priority => remove + reinsert (may match if crossing)
    if (price_changed || qty_increased) {
        const Side side = current.side;

        EraseByLocator(req.id, loc);

        NewOrder new_req;
        new_req.symbol = symbol_;
        new_req.id = req.id;
        new_req.side = side;
        new_req.type = OrderType::kLimit;
        new_req.price = target_price;
        new_req.qty = target_qty;

        auto place_res = Place(new_req);
        if (!place_res.accepted) {
            res.error = place_res.error.empty() ? "amend failed" : place_res.error;
            return res;
        }

        res.amended = true;
        res.remaining_qty = place_res.remaining_qty;
        res.trades = std::move(place_res.trades);
        return res;
    }

    // Same price, qty reduced => keep FIFO position
    current.qty = target_qty;
    res.amended = true;
    res.remaining_qty = current.qty;
    return res;
}

std::optional<PriceTicks> OrderBook::BestBid() const {
    if (bids_.empty()) return std::nullopt;
    return bids_.begin()->first;
}

std::optional<PriceTicks> OrderBook::BestAsk() const {
    if (asks_.empty()) return std::nullopt;
    return asks_.begin()->first;
}

OrderBook::Snapshot OrderBook::GetL2(std::size_t depth) const {
    Snapshot snap;
    snap.bids.reserve(depth);
    snap.asks.reserve(depth);

    std::size_t n = 0;
    for (const auto& [price, level] : bids_) {
        if (n++ >= depth) break;
        snap.bids.push_back(Level{price, level.TotalQty()});
    }

    n = 0;
    for (const auto& [price, level] : asks_) {
        if (n++ >= depth) break;
        snap.asks.push_back(Level{price, level.TotalQty()});
    }

    return snap;
}

bool OrderBook::CrossesSpread(const Order& incoming) const {
    if (incoming.side == Side::kBuy) {
        if (asks_.empty()) return false;
        return asks_.begin()->first <= incoming.price;
    } else {
        if (bids_.empty()) return false;
        return bids_.begin()->first >= incoming.price;
    }
}

void OrderBook::MatchIncoming(Order& incoming, std::vector<Trade>& out_trades) {
    while (incoming.qty > 0 && CrossesSpread(incoming)) {
        if (incoming.side == Side::kBuy) { // buy side
            auto best_it = asks_.begin();
            const PriceTicks exec_price = best_it->first;
            auto& level = best_it->second;

            // Match as much as we can at this price level
            while (incoming.qty > 0 && !level.fifo.empty()) {
                Order& maker = level.fifo.front();

                const Qty exec_qty = std::min(incoming.qty, maker.qty);
                out_trades.push_back(Trade{incoming.id, maker.id, exec_price, exec_qty});

                incoming.qty -= exec_qty;
                maker.qty -= exec_qty;

                if (maker.qty == 0) {
                    index_.erase(maker.id);
                    level.fifo.pop_front();
                }
            }

            if (level.fifo.empty()) asks_.erase(best_it);
        } else { // sell side
            auto best_it = bids_.begin();
            const PriceTicks exec_price = best_it->first;
            auto& level = best_it->second;

            while (incoming.qty > 0 && !level.fifo.empty()) {
                Order& maker = level.fifo.front();

                const Qty exec_qty = std::min(incoming.qty, maker.qty);
                out_trades.push_back(Trade{incoming.id, maker.id, exec_price, exec_qty});

                incoming.qty -= exec_qty;
                maker.qty -= exec_qty;

                if (maker.qty == 0) {
                    index_.erase(maker.id);
                    level.fifo.pop_front();
                }
            }

            if (level.fifo.empty()) bids_.erase(best_it);
        }
    }
}

void OrderBook::Rest(Order&& order) {
    if (order.side == Side::kBuy) {
        // Ensure a price level exists at order.price
        //  try_emplace:
        //      - if bids_ already has key=order.price, returns iterator to existing level
        //      - else inserts (order.price -> PriceLevel{}) and returns iterator to new level
        // It avoids constructing PriceLevel unless insertion actually happens 
        auto [lvl_it, inserted] = bids_.try_emplace(order.price, PriceLevel{});
        (void)inserted; // unused; silence warnings

        // Append the order to the FIFO list at that price
        lvl_it->second.fifo.push_back(std::move(order));

        // Get an iterator to the newly inserted list node
        auto it = std::prev(lvl_it->second.fifo.end());

        // Add a locator into index_: id -> (side, price, iterator)
        index_.emplace(it->id, Locator{it->side, it->price, it});
    } else {
        // Same logic for asks_, but asks_ map is sorted ascending
        auto [lvl_it, inserted] = asks_.try_emplace(order.price, PriceLevel{});
        (void)inserted;

        lvl_it->second.fifo.push_back(std::move(order));
        auto it = std::prev(lvl_it->second.fifo.end());
        index_.emplace(it->id, Locator{it->side, it->price, it});
    }
}

void OrderBook::EraseByLocator(OrderId id, const Locator& loc) {
    if (loc.side == Side::kBuy) {
        auto lvl_it = bids_.find(loc.price);
        if (lvl_it == bids_.end()) return;
        lvl_it->second.fifo.erase(loc.it);
        if (lvl_it->second.fifo.empty()) bids_.erase(lvl_it);
    } else {
        auto lvl_it = asks_.find(loc.price);
        if (lvl_it == asks_.end()) return;
        lvl_it->second.fifo.erase(loc.it);
        if (lvl_it->second.fifo.empty()) asks_.erase(lvl_it);
    }

    index_.erase(id);
}

} // namespace ob
