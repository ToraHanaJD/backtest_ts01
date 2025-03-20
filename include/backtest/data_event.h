// -------------------------------------------------------------------------------------------------
//  Copyright (C) 2023-2025. All rights reserved.
//
//  Licensed under the MIT License.
// -------------------------------------------------------------------------------------------------

#pragma once

#include "backtest/data.h"
#include "backtest/order_book.h"
#include <memory>
#include <variant>

namespace backtest {

/**
 * @brief 数据事件类型
 */
enum class DataEventType {
    QUOTE_TICK,
    TRADE_TICK,
    BAR,
    ORDER_BOOK_DELTA,
    INSTRUMENT_STATUS
};

/**
 * @brief 数据事件
 */
struct DataEvent {
    DataEventType type;
    
    UnixNanos timestamp;
    
    std::variant<QuoteTick, TradeTick, Bar, OrderBookDelta, InstrumentStatus> data;
    
    DataEvent(UnixNanos ts, const QuoteTick& q)
        : type(DataEventType::QUOTE_TICK), timestamp(ts), data(q) {}
    
    DataEvent(UnixNanos ts, const TradeTick& t)
        : type(DataEventType::TRADE_TICK), timestamp(ts), data(t) {}
    
    DataEvent(UnixNanos ts, const Bar& b)
        : type(DataEventType::BAR), timestamp(ts), data(b) {}
    
    DataEvent(UnixNanos ts, const OrderBookDelta& d)
        : type(DataEventType::ORDER_BOOK_DELTA), timestamp(ts), data(d) {}
    
    DataEvent(UnixNanos ts, const InstrumentStatus& s)
        : type(DataEventType::INSTRUMENT_STATUS), timestamp(ts), data(s) {}
    
    DataEvent() = default;
    
    ~DataEvent() = default;
    
    DataEvent(const DataEvent&) = default;
    
    DataEvent(DataEvent&&) = default;
    
    DataEvent& operator=(const DataEvent&) = default;
    
    DataEvent& operator=(DataEvent&&) = default;
    
    bool operator<(const DataEvent& other) const {
        return timestamp < other.timestamp;
    }
    
    const QuoteTick& quote() const { 
        return std::get<QuoteTick>(data); 
    }
    
    const TradeTick& trade() const { 
        return std::get<TradeTick>(data); 
    }
    
    const Bar& bar() const { 
        return std::get<Bar>(data); 
    }
    
    const OrderBookDelta& delta() const { 
        return std::get<OrderBookDelta>(data); 
    }
    
    const InstrumentStatus& status() const { 
        return std::get<InstrumentStatus>(data); 
    }
};

} // namespace backtest 