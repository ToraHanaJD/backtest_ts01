// -------------------------------------------------------------------------------------------------
//  Copyright (C) 2023-2025. All rights reserved.
//
//  Licensed under the MIT License.
// -------------------------------------------------------------------------------------------------

#include "backtest/order_book.h"
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <limits>

namespace backtest {

OrderBookLevel::OrderBookLevel(Price price, Quantity size)
    : price_(price)
    , size_(size) {
}

std::string OrderBookLevel::toString() const {
    std::stringstream ss;
    ss << std::fixed << std::setprecision(8) << price_ << "@" << size_;
    return ss.str();
}

OrderBookDelta::OrderBookDelta(
    InstrumentId instrument_id,
    OrderBookAction action,
    bool is_bid,
    Price price,
    Quantity size,
    UnixNanos ts_event,
    UnixNanos ts_init)
    : Data(DataType::ORDER_BOOK_DELTA, std::move(instrument_id), ts_event, ts_init)
    , action_(action)
    , is_bid_(is_bid)
    , price_(price)
    , size_(size) {
}

std::string OrderBookDelta::toString() const {
    std::stringstream ss;
    ss << "OrderBookDelta{" << Data::toString().substr(5)
       << ", action=";
    
    switch (action_) {
        case OrderBookAction::ADD: ss << "ADD"; break;
        case OrderBookAction::UPDATE: ss << "UPDATE"; break;
        case OrderBookAction::DELETE: ss << "DELETE"; break;
    }
    
    ss << ", is_bid=" << (is_bid_ ? "true" : "false")
       << ", price=" << std::fixed << std::setprecision(8) << price_
       << ", size=" << std::fixed << std::setprecision(8) << size_ << "}";
    
    return ss.str();
}

OrderBookDeltas::OrderBookDeltas(
    InstrumentId instrument_id,
    std::vector<OrderBookDelta> deltas,
    UnixNanos ts_event,
    UnixNanos ts_init)
    : Data(DataType::ORDER_BOOK_DELTA, std::move(instrument_id), ts_event, ts_init)
    , deltas_(std::move(deltas)) {
}

std::string OrderBookDeltas::toString() const {
    std::stringstream ss;
    ss << "OrderBookDeltas{" << Data::toString().substr(5)
       << ", deltas=[";
    
    for (size_t i = 0; i < deltas_.size(); ++i) {
        if (i > 0) {
            ss << ", ";
        }
        ss << deltas_[i].toString();
    }
    
    ss << "]}";
    
    return ss.str();
}

OrderBook::OrderBook(InstrumentId instrument_id, BookType type)
    : instrument_id_(std::move(instrument_id))
    , type_(type) {
}

void OrderBook::applyDelta(const OrderBookDelta& delta) {
    if (delta.instrumentId() != instrument_id_) {
        return; // ignore unknown instrument id
    }
    
    if (delta.isBid()) {
        // Bid
        switch (delta.action()) {
            case OrderBookAction::ADD:
            case OrderBookAction::UPDATE:
                if (delta.size() > 0) {
                    bids_[delta.price()] = delta.size();
                } else {
                    bids_.erase(delta.price());
                }
                break;
            case OrderBookAction::DELETE:
                bids_.erase(delta.price());
                break;
        }
    } else {
        // Sell
        switch (delta.action()) {
            case OrderBookAction::ADD:
            case OrderBookAction::UPDATE:
                if (delta.size() > 0) {
                    asks_[delta.price()] = delta.size();
                } else {
                    asks_.erase(delta.price());
                }
                break;
            case OrderBookAction::DELETE:
                asks_.erase(delta.price());
                break;
        }
    }
}

void OrderBook::applyDeltas(const OrderBookDeltas& deltas) {
    for (const auto& delta : deltas.deltas()) {
        applyDelta(delta);
    }
}

Price OrderBook::bestBidPrice() const {
    if (bids_.empty()) {
        return 0.0;
    }
    return bids_.begin()->first;
}

Price OrderBook::bestAskPrice() const {
    if (asks_.empty()) {
        return std::numeric_limits<Price>::max();
    }
    return asks_.begin()->first;
}

Quantity OrderBook::bidSize(Price price) const {
    auto it = bids_.find(price);
    if (it != bids_.end()) {
        return it->second;
    }
    return 0.0;
}

Quantity OrderBook::askSize(Price price) const {
    auto it = asks_.find(price);
    if (it != asks_.end()) {
        return it->second;
    }
    return 0.0;
}

std::vector<OrderBookLevel> OrderBook::bids(size_t depth) const {
    std::vector<OrderBookLevel> result;
    result.reserve(std::min(depth, bids_.size()));
    
    size_t count = 0;
    for (const auto& bid : bids_) {
        if (count >= depth) break;
        result.emplace_back(bid.first, bid.second);
        count++;
    }
    
    return result;
}

std::vector<OrderBookLevel> OrderBook::asks(size_t depth) const {
    std::vector<OrderBookLevel> result;
    result.reserve(std::min(depth, asks_.size()));
    
    size_t count = 0;
    for (const auto& ask : asks_) {
        if (count >= depth) break;
        result.emplace_back(ask.first, ask.second);
        count++;
    }
    
    return result;
}

// 原有的无参数版本方法现在用新的方法实现
std::vector<OrderBookLevel> OrderBook::bids() const {
    return bids(bids_.size());
}

std::vector<OrderBookLevel> OrderBook::asks() const {
    return asks(asks_.size());
}

Price OrderBook::midPrice() const {
    if (bids_.empty() || asks_.empty()) {
        return 0.0;
    }
    
    Price best_bid = bestBidPrice();
    Price best_ask = bestAskPrice();
    
    return (best_bid + best_ask) / 2.0;
}

Price OrderBook::spread() const {
    if (bids_.empty() || asks_.empty()) {
        return 0.0;
    }
    
    Price best_bid = bestBidPrice();
    Price best_ask = bestAskPrice();
    
    return best_ask - best_bid;
}

bool OrderBook::isEmpty() const {
    return bids_.empty() && asks_.empty();
}

void OrderBook::clear() {
    bids_.clear();
    asks_.clear();
}

std::string OrderBook::toString() const {
    std::stringstream ss;
    ss << "OrderBook{instrument=" << instrument_id_
       << ", type=";
    
    switch (type_) {
        case BookType::L1: ss << "L1"; break;
        case BookType::L2: ss << "L2"; break;
        case BookType::L3: ss << "L3"; break;
    }
    
    ss << ", bids=[";
    
    size_t i = 0;
    for (const auto& bid : bids_) {
        if (i++ > 0) {
            ss << ", ";
        }
        ss << OrderBookLevel(bid.first, bid.second).toString();
        
        // 对于L1只显示最优价格
        if (type_ == BookType::L1 && i >= 1) {
            break;
        }
    }
    
    ss << "], asks=[";
    
    i = 0;
    for (const auto& ask : asks_) {
        if (i++ > 0) {
            ss << ", ";
        }
        ss << OrderBookLevel(ask.first, ask.second).toString();
        
        // 对于L1只显示最优价格
        if (type_ == BookType::L1 && i >= 1) {
            break;
        }
    }
    
    ss << "]}";
    
    return ss.str();
}

} // namespace backtest 