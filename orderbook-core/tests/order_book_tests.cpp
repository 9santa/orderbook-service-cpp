#include "../include/order_book.h"

#include <cstdlib>
#include <exception>
#include <iostream>

#define EXPECT_TRUE(cond) do { \
    if (!(cond)) { \
        std::cerr << "EXPECT_TRUE failed at " << __FILE__ << ":" << __LINE__ << "\n"; \
        std::exit(1); \
    } \
} while (0)

#define EXPECT_EQ(a, b) do { \
    auto _a = (a); auto _b = (b); \
    if (!(_a == _b)) { \
        std::cerr << "EXPECT_EQ failed at " << __FILE__ << ":" << __LINE__ \
                  << " got=" << _a << " expected=" << _b << "\n"; \
        std::exit(1); \
    } \
} while (0)

using ob::AmendOrder;
using ob::NewOrder;
using ob::OrderBook;
using ob::OrderType;
using ob::Side;

static void Test_PartialFillAndRest() {
    OrderBook book("BTC-USD");

    auto order1 = NewOrder{.symbol="BTC-USD", .id=1, .side=Side::kSell,
                                     .type=OrderType::kLimit, .price=101, .qty=7};

    book.Place(order1);

    auto place_res = book.Place(NewOrder{.symbol="BTC-USD", .id=2, .side=Side::kBuy,
                                                           .type=OrderType::kLimit, .price=105, .qty=5});

    EXPECT_TRUE(place_res.accepted);
    EXPECT_EQ(place_res.filled_qty, 5);
    EXPECT_EQ(place_res.trades.size(), 1u);
    EXPECT_EQ(place_res.trades[0].price, 101);
    EXPECT_EQ(place_res.trades[0].qty, 5);

    auto l2 = book.GetL2(10);
    EXPECT_EQ(l2.asks.size(), 1u);
    EXPECT_EQ(l2.asks[0].qty, 2); // 7-5=2 left
}

static void Test_MarketSweepsMultipleLevels() {
    OrderBook book("AAPL");
    book.Place(NewOrder{.symbol="AAPL", .id=10, .side=Side::kBuy,
                      .type=OrderType::kLimit, .price=99, .qty=2});
    book.Place(NewOrder{.symbol="AAPL", .id=11, .side=Side::kBuy,
                      .type=OrderType::kLimit, .price=98, .qty=3});

    auto res = book.Place(NewOrder{.symbol="AAPL", .id=20, .side=Side::kSell,
                                                     .type=OrderType::kMarket, .qty=4});

    EXPECT_EQ(res.trades.size(), 2u);
    EXPECT_EQ(res.trades[0].price, 99);
    EXPECT_EQ(res.trades[1].price, 98);

    auto l2 = book.GetL2(10);
    EXPECT_EQ(l2.bids.size(), 1u);
    EXPECT_EQ(l2.asks.size(), 0u);
    EXPECT_EQ(l2.bids[0].price, 98);
    EXPECT_EQ(l2.bids[0].qty, 1);
}

static void Test_AmendQtyDecreaseKeepsPriority() {
    OrderBook book("T");

    book.Place(NewOrder{.symbol="T", .id=1, .side=Side::kSell,
                      .type=OrderType::kLimit, .price=100, .qty=5});
    book.Place(NewOrder{.symbol="T", .id=2, .side=Side::kSell,
                      .type=OrderType::kLimit, .price=100, .qty=5});

    auto a = book.Amend(AmendOrder{.id=1, .new_qty=1});
    EXPECT_TRUE(a.amended);
    EXPECT_EQ(a.remaining_qty, 1);

    auto r = book.Place(NewOrder{.symbol="T", .id=3, .side=Side::kBuy,
                                                   .type=OrderType::kLimit, .price=100, .qty=2});

    EXPECT_EQ(r.trades.size(), 2u);
    EXPECT_EQ(r.trades[0].maker_id, 1u);
    EXPECT_EQ(r.trades[0].taker_id, 3u);
    EXPECT_EQ(r.trades[1].maker_id, 2u);
}

int main() {
    try {
        Test_PartialFillAndRest();
        Test_MarketSweepsMultipleLevels();
        Test_AmendQtyDecreaseKeepsPriority();
    } catch (const std::exception& e) {
        std::cerr << "Unhandled exception: " << e.what() << "\n";
        return 1;
    }
    std::cout << "All tests passes\n";
    return 0;
}
