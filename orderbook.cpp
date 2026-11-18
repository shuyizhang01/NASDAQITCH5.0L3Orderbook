#include <cstdint>
#include <iostream>
#include <vector>
#include "orderbook.h"
#include <iomanip>
#include <optional>
#include <format>
#include <cstring>
#include <algorithm>



void OrderBook::add_order(Order& add_order) {
    // std::cout << add_order.reference_number << '\n';
    // if (add_order.reference_number > max) {
    //     max = add_order.reference_number;
    // }
    // std::cout << max << '\n';
    // std::cout << orders.size() << '\n';
    Order* add_order_ptr = order_pool.allocate(add_order);

    // orders.emplace(add_order.reference_number, add_order_ptr);
    SymbolBook& symbol_book = symbol_books[add_order.stock_locate];
    // std::cout << "Order peak: " << order_pool.peak_live_count() << '\n';
    // std::cout << "PL peak: " << symbol_book.pl_pool.peak_live_count() << '\n';

    //std::cout << "Symbol Book Size: " << symbol_books.size() << '\n';
    //std::cout << "Stock ID: " << add_order.stock_locate << "Bid/Ask Size: " << symbol_book.bids.size() << "/" << symbol_book.asks.size() << '\n';
    //std::cout << "Corresponding Price Pool INFO: " << "Available: " << symbol_book.pl_pool.available() << "Live Count: " << symbol_book.pl_pool.live_count() << '\n';

    const bool is_bid = add_order.side == 'B';
    auto& levels = symbol_book.sides[is_bid];

    // std::cout << "Ask size: " << symbol_book.asks.size() << "Bid size" << symbol_book.bids.size() << '\n';
    auto pl_it = symbol_book.find(add_order.price, is_bid);

    if (pl_it != levels.end() && pl_it->price == add_order.price) [[likely]] {
        pl_it->price_level->total_shares += add_order.shares;
        ++pl_it->price_level->order_count;
    } else {
        PriceLevel* pl_ptr = symbol_book.pl_pool.allocate(
            add_order.shares,
            1);
        pl_it = levels.emplace(pl_it, add_order.price, pl_ptr); //insert with {} compiles down to the same Assembly during optimization
    }
    add_order_ptr->price_level = pl_it->price_level; //keep price level for future reference
    
    orders.emplace(add_order.reference_number, add_order_ptr);
    // if (pl_it->price_level->head == nullptr) { // attach order to the end of the queue for a specific pricelevel
    //     pl_it->price_level->head =  add_order_ptr;
    //     pl_it->price_level->tail =  add_order_ptr;
    // } else {
    //     add_order_ptr->prev = pl_it->price_level->tail;
    //     pl_it->price_level->tail->next =  add_order_ptr;
    //     pl_it->price_level->tail =  add_order_ptr;
    // }
    add_order_ptr->prev = pl_it->price_level->tail;
    // add_order_with_mpid_ptr->next = nullptr;
    Order** link = pl_it->price_level->tail ? &pl_it->price_level->tail->next : &pl_it->price_level->head;
    *link = add_order_ptr;
    pl_it->price_level->tail = add_order_ptr;

}


void OrderBook::add_order_with_mpid(Order& add_order_with_mpid) {
    // std::cout << add_order.reference_number << '\n';
    Order* add_order_with_mpid_ptr = order_pool.allocate(add_order_with_mpid);

    // orders.emplace(add_order.reference_number, add_order_ptr);
    SymbolBook& symbol_book = symbol_books[add_order_with_mpid.stock_locate];
    // std::cout << "Symbol Book Size: " << symbol_books.size() << '\n';
    // std::cout << "Stock ID: " << add_order.stock_locate << "Bid/Ask Size: " << symbol_book.bids.size() << "/" << symbol_book.asks.size() << '\n';
    // std::cout << "Corresponding Price Pool INFO: " << "Available: " << symbol_book.pl_pool.available() << "Live Count: " << symbol_book.pl_pool.live_count() << '\n';

    const bool is_bid = add_order_with_mpid.side == 'B';
    auto& levels = symbol_book.sides[is_bid];

    // std::cout << "Ask size: " << symbol_book.asks.size() << "Bid size" << symbol_book.bids.size() << '\n';
    auto pl_it = symbol_book.find(add_order_with_mpid.price, is_bid);
    if (pl_it != levels.end() && pl_it->price == add_order_with_mpid.price) [[likely]] {
        pl_it->price_level->total_shares += add_order_with_mpid.shares;
        ++pl_it->price_level->order_count;
    } else {
        PriceLevel* pl_ptr = symbol_book.pl_pool.allocate(
            add_order_with_mpid.shares,
            1);
        pl_it = levels.emplace(pl_it, add_order_with_mpid.price, pl_ptr); //insert with {} compiles down to the same Assembly during optimization
    }
    add_order_with_mpid_ptr->price_level = pl_it->price_level; //keep price level for future reference
    orders.emplace(add_order_with_mpid.reference_number, add_order_with_mpid_ptr);
    // if (pl_it->price_level->head == nullptr) { // attach order to the end of the queue for a specific pricelevel
    //     pl_it->price_level->head =  add_order_with_mpid_ptr;
    //     pl_it->price_level->tail =  add_order_with_mpid_ptr;
    // } else {
    //     add_order_with_mpid_ptr->prev = pl_it->price_level->tail;
    //     pl_it->price_level->tail->next =  add_order_with_mpid_ptr;
    //     pl_it->price_level->tail =  add_order_with_mpid_ptr;
    // }

    add_order_with_mpid_ptr->prev = pl_it->price_level->tail;
    // add_order_with_mpid_ptr->next = nullptr;
    Order** link = pl_it->price_level->tail ? &pl_it->price_level->tail->next : &pl_it->price_level->head;
    *link = add_order_with_mpid_ptr;
    pl_it->price_level->tail = add_order_with_mpid_ptr;


}



void OrderBook::execute_order(const Order& execute_order) {

    const auto it = orders.find(execute_order.reference_number);
    if (it == orders.end()) {
        return;
    }

    it->second->shares -= execute_order.shares;

    const auto pl_ptr = it->second->price_level;
    pl_ptr->total_shares -= execute_order.shares;

    uint16_t stock_locate = it->second->stock_locate;
    char side = it->second->side;
    uint32_t price = it->second->price;

    if (it->second->shares == 0) {
        --pl_ptr->order_count;

        Order** prev_next = it->second->prev ? &it->second->next : &pl_ptr->head;
        Order** next_prev = it->second->next ? &it->second->prev : &pl_ptr->tail;

        *prev_next = it->second->next;
        *next_prev = it->second->prev;

        order_pool.deallocate(it->second);
        orders.erase(it);
    }
    if (pl_ptr->total_shares == 0) {
        SymbolBook& symbol_book = symbol_books[stock_locate];
        const bool is_bid = side == 'B';
        auto& levels = symbol_book.sides[is_bid];
        auto pl_it = symbol_book.find(price, is_bid);
        levels.erase(pl_it);
        symbol_book.pl_pool.deallocate(pl_ptr);
    }


}
void OrderBook::execute_order_with_price(const Order& execute_order_with_price) {

    const auto it = orders.find(execute_order_with_price.reference_number);
    if (it == orders.end()) {
        return;
    }

    it->second->shares -= execute_order_with_price.shares;

    const auto pl_ptr = it->second->price_level;
    pl_ptr->total_shares -= execute_order_with_price.shares;

    uint16_t stock_locate = it->second->stock_locate;
    char side = it->second->side;
    uint32_t price = it->second->price;

    if (it->second->shares == 0) {
        --pl_ptr->order_count;

        Order** prev_next = it->second->prev ? &it->second->next : &pl_ptr->head;
        Order** next_prev = it->second->next ? &it->second->prev : &pl_ptr->tail;

        *prev_next = it->second->next;
        *next_prev = it->second->prev;


        order_pool.deallocate(it->second);
        orders.erase(it);
    }
    if (pl_ptr->total_shares == 0) {
        SymbolBook& symbol_book = symbol_books[stock_locate];
        const bool is_bid = side == 'B';
        auto& levels = symbol_book.sides[is_bid];
        auto pl_it = symbol_book.find(price, is_bid);
        levels.erase(pl_it);
        symbol_book.pl_pool.deallocate(pl_ptr);


    }


}




// void OrderBook::execute_order(const OrderExecutedWithPrice& order_executed_with_price) {
//
//     auto it = orders.find(order_executed_with_price.reference_number);
//     if (it == orders.end()) {
//         return;
//     }
//
//     it->second->shares -= order_executed_with_price.executed_shares;
//
//     const auto pl_it = it->second->price_level;
//
//     pl_it->total_shares -= order_executed_with_price.executed_shares;
//
//     if (it->second->shares == 0) {
//         --pl_it->order_count;
//         if (it->second->prev == nullptr) { //check if the order to cancel is at the head of the linkedlist
//             pl_it->head = pl_it->head->next;
//         } else {
//             it->second->prev->next = it->second->next;
//         }
//         if (it->second->next == nullptr) { //check if the order to cancel is at the tail of the linkedlist
//             pl_it->tail = it->second->prev;
//         } else {
//             it->second->next->prev = it->second->prev;
//         }
//         order_pool.deallocate(it->second);
//         orders.erase(it);
//     }
// }
void OrderBook::cancel_order(const Order& cancel_order) {

    const auto it = orders.find(cancel_order.reference_number);
    if (it == orders.end()) {
        return;
    }

    it->second->shares -= cancel_order.shares;

    const auto pl_ptr = it->second->price_level;

    pl_ptr->total_shares -= cancel_order.shares;

    uint16_t stock_locate = it->second->stock_locate;
    char side = it->second->side;
    uint32_t price = it->second->price;

    if (it->second->shares == 0) {
        --pl_ptr->order_count;
        Order** prev_next = it->second->prev ? &it->second->next : &pl_ptr->head;
        Order** next_prev = it->second->next ? &it->second->prev : &pl_ptr->tail;

        *prev_next = it->second->next;
        *next_prev = it->second->prev;



        order_pool.deallocate(it->second);
        orders.erase(it);
    }

    if (pl_ptr->total_shares == 0) {
        SymbolBook& symbol_book = symbol_books[stock_locate];
        const bool is_bid = side == 'B';
        auto& levels = symbol_book.sides[is_bid];
        auto pl_it = symbol_book.find(price, is_bid);
        levels.erase(pl_it);
        symbol_book.pl_pool.deallocate(pl_ptr);
    }

}
void OrderBook::delete_order(const Order& delete_order) {

    const auto it = orders.find(delete_order.reference_number);
    if (it == orders.end()) {
        return;
    }

    // no need to set shares to zero since order will be deleted
    const auto pl_ptr = it->second->price_level;
    if (pl_ptr == nullptr) {
        std::cout << "nullptr" << pl_ptr << '\n';
    }

    pl_ptr->total_shares -= it->second->shares;
    --pl_ptr->order_count;
    // if (it->second->prev == nullptr) { //check if the order to cancel is at the head of the linkedlist
    //     pl_ptr->head = pl_ptr->head->next;
    // } else {
    //     it->second->prev->next = it->second->next;
    // }
    // if (it->second->next == nullptr) { //check if the order to cancel is at the tail of the linkedlist
    //     pl_ptr->tail = it->second->prev;
    // } else {
    //     it->second->next->prev = it->second->prev;
    // }
    Order** prev_next = it->second->prev ? &it->second->next : &pl_ptr->head;
    Order** next_prev = it->second->next ? &it->second->prev : &pl_ptr->tail;

    *prev_next = it->second->next;
    *next_prev = it->second->prev;



    if (pl_ptr->total_shares == 0) {
        SymbolBook& symbol_book = symbol_books[it->second->stock_locate];
        const bool is_bid = it->second->side == 'B';
        auto& levels = symbol_book.sides[is_bid];
        auto pl_it = symbol_book.find(it->second->price, is_bid);
        levels.erase(pl_it);
        symbol_book.pl_pool.deallocate(pl_ptr);

    }

    order_pool.deallocate(it->second);
    orders.erase(it);

}

void OrderBook::replace_order(Order& order_replace) {
    const auto it_old = orders.find(order_replace.reference_number);
    assert(orders.end() == orders.find(order_replace.new_reference_number));

    if (it_old == orders.end()) {
        return;
    }
    // if (it_old->second == nullptr) {
    //     std::cout << "isnullptr" << '\n';
    // }

    char side = it_old->second->side;
    uint16_t stock_locate = it_old->second->stock_locate;

    const auto old_pl_ptr = it_old->second->price_level;



    // if (it_old->second->prev == nullptr) { //check if the order to cancel is at the head of the linkedlist
    //     if (old_pl->head == nullptr) {
    //         std::cout << "list is empty" << '\n';
    //     }
    //     old_pl->head = old_pl->head->next;
    // } else {
    //     it_old->second->prev->next = it_old->second->next;
    // }
    // if (it_old->second->next == nullptr) { //check if the order to cancel is at the tail of the linkedlist
    //     old_pl->tail = it_old->second->prev;
    // } else {
    //     it_old->second->next->prev = it_old->second->prev;
    // }
    Order** prev_next = it_old->second->prev ? &it_old->second->next : &old_pl_ptr->head;
    Order** next_prev = it_old->second->next ? &it_old->second->prev : &old_pl_ptr->tail;

    *prev_next = it_old->second->next;
    *next_prev = it_old->second->prev;


    if (it_old->second->price == order_replace.price) {
        // trivial update PL+Order and modify reference number if replace order resizes an existing order
        old_pl_ptr->total_shares += order_replace.shares - it_old->second->shares;
        it_old->second->shares = order_replace.shares;

        Order* updated_order_ptr = it_old->second;

        orders.erase(it_old);
        //set order back to default state and change reference number
        updated_order_ptr->reference_number = order_replace.new_reference_number;
        updated_order_ptr->next = nullptr;
        updated_order_ptr->prev = nullptr;

        // don't deallocate, we still have the order in memory, we just need to change the updated order's reference number

        updated_order_ptr->prev = old_pl_ptr->tail;
        // add_order_with_mpid_ptr->next = nullptr;
        Order** link = old_pl_ptr->tail ? &old_pl_ptr->tail->next : &old_pl_ptr->head;
        *link = updated_order_ptr;
        old_pl_ptr->tail = updated_order_ptr;


        orders.insert_or_assign(order_replace.new_reference_number, updated_order_ptr);

        return;
    }

    old_pl_ptr->total_shares -= it_old->second->shares;
    --old_pl_ptr->order_count;

    order_pool.deallocate(it_old->second);
    orders.erase(it_old);

    //insert replacement order

    SymbolBook& symbol_book = symbol_books[stock_locate];


    Order* replace_order_ptr =  order_pool.allocate(Order{
        .reference_number = order_replace.new_reference_number,
        .shares =  order_replace.shares,
        .price = order_replace.price,
        .stock_locate = stock_locate,
        .side = side,
    });
    const bool is_bid = (side == 'B');
    auto& levels = symbol_book.sides[is_bid];
    // std::cout << "Ask size: " << symbol_book.asks.size() << "Bid size" << symbol_book.bids.size() << '\n';
    auto pl_it = symbol_book.find(order_replace.price, is_bid);
    if (pl_it != levels.end() && pl_it->price == order_replace.price) [[likely]] {
        pl_it->price_level->total_shares += order_replace.shares;
        ++pl_it->price_level->order_count;
    } else {
        PriceLevel* pl_ptr = symbol_book.pl_pool.allocate(
            order_replace.shares,
            1);
        pl_it = levels.emplace(pl_it, order_replace.price, pl_ptr); //insert with {} compiles down to the same Assembly during optimization
    }
    replace_order_ptr->price_level = pl_it->price_level; //keep price level for future reference
    orders.insert_or_assign(order_replace.new_reference_number, replace_order_ptr);
    if (pl_it->price_level->head == nullptr) { // attach order to the end of the queue for a specific pricelevel
        pl_it->price_level->head = replace_order_ptr;
        pl_it->price_level->tail = replace_order_ptr;
    } else {
        replace_order_ptr->prev = pl_it->price_level->tail;
        pl_it->price_level->tail->next = replace_order_ptr;
        pl_it->price_level->tail = replace_order_ptr;
    }
    replace_order_ptr->prev = pl_it->price_level->tail;
    // add_order_with_mpid_ptr->next = nullptr;
    Order** link = pl_it->price_level->tail ? &pl_it->price_level->tail->next : &pl_it->price_level->head;
    *link = replace_order_ptr;
    pl_it->price_level->tail = replace_order_ptr;

}
//
//     if (side == 'B') {
//         // add order to replace
//         auto pl_it = symbol_book.find(order_replace.price, true);
//         if (pl_it != symbol_book.bids.end() && pl_it->price == order_replace.price) [[likely]] {
//             pl_it->price_level->total_shares += order_replace.shares;
//             ++pl_it->price_level->order_count;
//         } else {
//             PriceLevel* pl_ptr = symbol_book.pl_pool.allocate(
//             order_replace.shares,
//             1);
//             pl_it = symbol_book.bids.emplace(pl_it, order_replace.price, pl_ptr); //insert with {} compiles down to the same Assembly during optimization
//         }
//
//         if (pl_it->price_level->head == nullptr) { //insert new order
//             pl_it->price_level->head = replace_order_ptr;
//             pl_it->price_level->tail = replace_order_ptr;
//         } else {
//             replace_order_ptr->prev = pl_it->price_level->tail;
//             pl_it->price_level->tail->next = replace_order_ptr;
//             pl_it->price_level->tail = replace_order_ptr;
//         }
//         replace_order_ptr->price_level = pl_it->price_level;
//         orders.insert_or_assign(order_replace.new_order_reference_number, replace_order_ptr);
//
//     } else if (side == 'S') {
//         // add order to replace
//         auto pl_it = symbol_book.find(order_replace.price, false);
//         if (pl_it != symbol_book.asks.end() && pl_it->price == order_replace.price) [[likely]] {
//             pl_it->price_level->total_shares += order_replace.shares;
//             ++pl_it->price_level->order_count;
//         } else {
//             PriceLevel* pl_ptr = symbol_book.pl_pool.allocate(
//             order_replace.shares,
//             1);
//             pl_it = symbol_book.asks.emplace(pl_it, order_replace.price, pl_ptr); //insert with {} compiles down to the same Assembly during optimization
//         }
//         if (pl_it->price_level->head == nullptr) { //insert new order
//             pl_it->price_level->head = replace_order_ptr;
//             pl_it->price_level->tail = replace_order_ptr;
//         } else {
//             replace_order_ptr->prev = pl_it->price_level->tail;
//             pl_it->price_level->tail->next = replace_order_ptr;
//             pl_it->price_level->tail = replace_order_ptr;
//         }
//         replace_order_ptr->price_level = pl_it->price_level;
//         orders.insert_or_assign(order_replace.new_order_reference_number, replace_order_ptr);
//     }
// }
// std::unordered_map<std::string, SymbolBook> OrderBook::build_snapshot() const {
//     for (const auto& [reference_number, order] : orders ) {
//         SymbolBook& book = snapshot[order.stock];
//         if (order.side == 'B') {
//             PriceLevel& price_level = book.bids[order.price];
//             price_level.total_shares += order.shares;
//             ++price_level.order_count;
//         } else if (order.side == 'S') {
//             PriceLevel& price_level = book.asks[order.price];
//             price_level.total_shares += order.shares;
//             ++price_level.order_count;
//         }
//     }
//     return snapshot;
// }

// void print_symbolbook(const SymbolBook& symbolbook, std::string stock_ticker) {
//     std::cout << "         " << stock_ticker << " ORDER BOOK" << '\n';
//     std::cout << std::setfill(' ') << std::setw(10) << "BIDS";
//     std::cout << std::setw(18) << "ASKS" << '\n';
//     std::cout << std::setw(6) << "PRICE";
//     std::cout << std::setw(9) << "SIZE";
//     std::cout << std::setw(10) << "PRICE";
//     std::cout << std::setw(9) << "SIZE" << '\n';
//     auto itbids = symbolbook.bids.begin();
//     auto itasks = symbolbook.asks.begin();
//     auto endbids = symbolbook.bids.end();
//     auto endasks = symbolbook.asks.end();
//
//     while (itbids != symbolbook.bids.end() && itasks != symbolbook.asks.end()) {
//         if (itbids != endbids) {
//             uint64_t price = itbids->first;
//             uint64_t total_shares = itbids->second.total_shares;
//             std::cout << std::setfill(' ') << std::setw(5) << price / 10000 << "." << std::setfill('0') << std::setw(4) << price % 10000;
//             std::cout << std::setfill(' ') << " " << std::setw(4) << total_shares;
//         } else {
//             std::cout << std::setw(13);
//         }
//         if (itasks != endasks) {
//             uint64_t price = itasks->first;
//             uint64_t total_shares = itasks->second.total_shares;
//             std::cout << "   " << std::setw(5) << price / 10000 << "." << std::setfill('0') << std::setw(4) << price % 10000;
//             std::cout << std::setfill(' ') << " " << std::setw(4) << total_shares << '\n';
//         } else {
//             std::cout << std::setw(13) << '\n';
//         }
//         ++itbids;
//         ++itasks;
//     }
//     if (const auto bestbid = symbolbook.get_best_bid()) {
//         std::cout << "Best Bid: " << *bestbid / 10000 << "." << *bestbid % 10000 << '\n';
//     }
//     if (const auto bestask = symbolbook.get_best_ask()) {
//         std::cout << "Best Ask: " << *bestask / 10000 << "." << *bestask % 10000 << '\n';
//     }
//     if (const auto spread = symbolbook.get_spread()) {
//         std::cout << "Spread: " << *spread / 10000 << "." << *spread % 10000 << '\n';
//     }
//     if (const auto mid = symbolbook.get_midprice()) {
//         std::cout << "Mid: " << *mid / 10000 << "." << *mid % 10000 << '\n';
//     }
//     std::cout << "Bid depth: " << symbolbook.get_bid_depth() << '\n';
//     std::cout << "Ask depth: " << symbolbook.get_ask_depth() << '\n';
//     if (const auto imbalance = symbolbook.get_imbalance()) {
//         std::cout << "Imbalance: " << *imbalance << '\n';
//     }
// }




// const AddOrder* OrderBook::find_order(uint64_t reference_number) const {
//     auto it = orders.find(reference_number);
//     if (it != orders.end()) {
//         return it->second;
//     } else {
//         return nullptr;
//     }
// }
std::size_t OrderBook::size() const {
    return orders.size();
}

std::optional<uint32_t> SymbolBook::get_best_bid() const {
    if (!sides[1].empty()) {
        return sides[1].end()->price;
    } else {
        return std::nullopt;
    }

}
std::optional<uint32_t> SymbolBook::get_best_ask() const {
    if (!sides[0].empty()) {
        return sides[0].end()->price;
    } else {
        return std::nullopt;
    }
}
std::optional<uint32_t> SymbolBook::get_spread() const {
    if (!sides[0].empty() && !sides[1].empty()) {
        return sides[0].end()->price-sides[1].end()->price;
    } else {
        return std::nullopt;
    }
}
std::optional<uint32_t> SymbolBook::get_midprice() const {
    if (!sides[0].empty() && !sides[1].empty()) {
        return (sides[0].end()->price+sides[1].end()->price) / static_cast<uint32_t>(2);
    } else {
        return std::nullopt;
    }
}
uint64_t SymbolBook::get_bid_depth() const {
    uint64_t biddepth{};
    for (const auto& ple : sides[0]) {
        biddepth += ple.price_level->total_shares;
    }
    return biddepth;
}
uint64_t SymbolBook::get_ask_depth() const {
    uint64_t askdepth{};
    for (const auto& ple : sides[0]) {
        askdepth += ple.price_level->total_shares;
    }
    return askdepth;
}
std::optional<double> SymbolBook::get_imbalance() const {
    if (!sides[0].empty() && !sides[1].empty()) {
        const auto biddepth = static_cast<double>(get_bid_depth());
        const auto askdepth = static_cast<double>(get_ask_depth());
        return (biddepth - askdepth) / (biddepth + askdepth);
    } else {
        return std::nullopt;
    }
}






// uint64_t read_be(const uint8_t* data, size_t n) {
//     uint64_t value{};
//     for (size_t i{}; i < n; ++i) {
//         value = (value << 8) | data[i];
//     }
//     return value;
// }


//optimized: specify type of order field and use template func read_be to read bytes in little endian
Order decode_add_order(const std::span<const uint8_t>& message) {
    // std::span does not impliiclty convert to pointer, so do .data() on it
    // uint16_t stock_locate = read_be(message.data() + 1, 2);
    // uint64_t order_reference_num = read_be(message.data()+11, 8);
    // uint64_t buy_sell = read_be(message.data()+19, 1);
    // uint64_t shares = read_be(message.data()+20, 4);
    // uint64_t price = read_be(message.data() + 32, 4);
    return Order{
        .reference_number = read_be<uint64_t>(message.data() + 11),
        .shares = read_be<uint32_t>(message.data() + 20),
        .price = read_be<uint32_t>(message.data() + 32),
        .stock_locate = read_be<uint16_t>(message.data()+1),
        .type = 'A',
        .side = static_cast<char>(message[19]),
    };
}
    // std::memcpy(add_order_with_MPID.stock.data(), message.data() + 24, 8);
    // std::memcpy(add_order_with_MPID.market_participant_id.data(), message.data() + 36, 4);

    //





Order decode_add_order_with_MPID(const std::span<const uint8_t>& message) {
    // uint16_t stock_locate = read_be(message.data() + 1, 2);
    // uint64_t order_reference_num = read_be(message.data()+11, 8);
    // uint64_t buy_sell = read_be(message.data()+19, 1);
    // uint64_t shares = read_be(message.data()+20, 4);
    // uint64_t price = read_be(message.data() + 32, 4);
    // Order add_order_with_mpid {
    //     .reference_number = read_be<uint64_t>(message.data() + 11),
    //     .stock_locate = read_be<uint16_t>(message.data()+1),
    //     .side = static_cast<char>(message[19]),
    //     .shares = read_be<uint32_t>(message.data() + 20),
    //     .price = read_be<uint32_t>(message.data() + 32),
    // };
    return Order{
        .reference_number = read_be<uint64_t>(message.data() + 11),
        .shares = read_be<uint32_t>(message.data() + 20),
        .price = read_be<uint32_t>(message.data() + 32),
        // .market_participant_id = read_be<uint32_t>(message.data() + 36),
        .stock_locate = read_be<uint16_t>(message.data()+1),
        .type = 'F',
        .side = static_cast<char>(message[19]),
    };
}


Order decode_order_executed(const std::span<const uint8_t>& message) {
    // uint16_t stock_locate = read_be(message.data() + 1, 2);
    // uint64_t order_reference_num = read_be(message.data()+11, 8);
    // uint64_t executed_shares = read_be(message.data()+19, 4);
    // uint64_t match_number = read_be(message.data() + 23, 8);
    return Order{
        .reference_number = read_be<uint64_t>(message.data() + 11),
        .shares = read_be<uint32_t>(message.data() + 19),
        // .price = read_be<uint32_t>(message.data() + 32),
        // .market_participant_id = read_be<uint32_t>(message.data() + 36),
        .stock_locate = read_be<uint16_t>(message.data()+1),
        .type = 'E'
        // .side = static_cast<char>(message[19]),
    };
}
Order decode_order_executed_with_price(const std::span<const uint8_t>& message) {
    // uint16_t stock_locate = read_be(message.data() + 1, 2);
    // uint64_t order_reference_num = read_be(message.data()+11, 8);
    // uint64_t executed_shares = read_be(message.data()+19, 4);
    // uint64_t match_number = read_be(message.data()+23, 8);
    // char printable = static_cast<char>(read_be(message.data()+31, 1));
    // uint64_t execution_price = read_be(message.data() + 32, 4);
    return Order{
        .reference_number = read_be<uint64_t>(message.data() + 11),
        .shares = read_be<uint32_t>(message.data() + 20),
        .price = read_be<uint32_t>(message.data() + 32),
        // .market_participant_id = read_be<uint32_t>(message.data() + 36),
        .stock_locate = read_be<uint16_t>(message.data()+1),
        .type = 'C',
    };
}
Order decode_order_cancel(const std::span<const uint8_t>& message) {
    // uint64_t order_reference_num = read_be(message.data()+11, 8);
    // uint64_t cancelled_shares = read_be(message.data()+19, 4);
    return Order{
        .reference_number = read_be<uint64_t>(message.data()+11),
        .shares = read_be<uint32_t>(message.data()+19),
        .type = 'X'
    };
}
Order decode_order_delete(const std::span<const uint8_t>& message) {
    // uint64_t order_reference_num = read_be(message.data()+11, 8);
    return Order{
        .reference_number = read_be<uint64_t>(message.data()+11),
        .type = 'D'
    };
}
Order decode_order_replace(const std::span<const uint8_t>& message) {

    // uint16_t stock_locate = read_be(message.data() + 1, 2);
    // uint64_t original_reference_num = read_be(message.data()+11, 8);
    // uint64_t new_reference_num = read_be(message.data()+19, 8);
    // uint64_t shares = read_be(message.data()+27, 4);
    // uint64_t price = read_be(message.data() + 31, 4);

    return Order{
        .reference_number = read_be<uint64_t>(message.data()+11),
        .new_reference_number = read_be<uint64_t>(message.data()+11),
        .shares = read_be<uint32_t>(message.data()+27),
        .price = read_be<uint32_t>(message.data()+31),
        .stock_locate = read_be<uint16_t>(message.data()+1),
        .type = 'U'
    };
}





