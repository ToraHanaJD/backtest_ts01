// -------------------------------------------------------------------------------------------------
//  Copyright (C) 2023-2025. All rights reserved.
//
//  Licensed under the MIT License.
// -------------------------------------------------------------------------------------------------

#include "backtest/data.h"
#include <sstream>
#include <iomanip>

namespace backtest {

// BarType 实现
BarType::BarType(InstrumentId instrument_id, BarSpecification specification, BarAggregation aggregation)
    : instrument_id(std::move(instrument_id))
    , specification(specification)
    , aggregation(aggregation) {
}

std::string BarType::toString() const {
    std::stringstream ss;
    ss << instrument_id << "-";
    
    // 转换规格为字符串
    switch (specification) {
        case BarSpecification::SECOND_1: ss << "1S"; break;
        case BarSpecification::SECOND_10: ss << "10S"; break;
        case BarSpecification::MINUTE_1: ss << "1M"; break;
        case BarSpecification::MINUTE_5: ss << "5M"; break;
        case BarSpecification::MINUTE_15: ss << "15M"; break;
        case BarSpecification::MINUTE_30: ss << "30M"; break;
        case BarSpecification::HOUR_1: ss << "1H"; break;
        case BarSpecification::HOUR_4: ss << "4H"; break;
        case BarSpecification::DAY_1: ss << "1D"; break;
        case BarSpecification::WEEK_1: ss << "1W"; break;
        case BarSpecification::MONTH_1: ss << "1M"; break;
        case BarSpecification::CUSTOM: ss << "CUSTOM"; break;
    }
    
    ss << "-";
    
    // 转换聚合类型为字符串
    switch (aggregation) {
        case BarAggregation::BID: ss << "BID"; break;
        case BarAggregation::ASK: ss << "ASK"; break;
        case BarAggregation::MID: ss << "MID"; break;
        case BarAggregation::TRADE: ss << "TRADE"; break;
        case BarAggregation::VOLUME_WEIGHTED_MID: ss << "VWMID"; break;
        case BarAggregation::UNDEFINED: ss << "UNDEFINED"; break;
    }
    
    return ss.str();
}

bool BarType::operator==(const BarType& other) const {
    return instrument_id == other.instrument_id &&
           specification == other.specification &&
           aggregation == other.aggregation;
}

bool BarType::operator!=(const BarType& other) const {
    return !(*this == other);
}

// Data 基类实现
Data::Data(DataType type, InstrumentId instrument_id, UnixNanos ts_event, UnixNanos ts_init)
    : type_(type)
    , instrument_id_(std::move(instrument_id))
    , ts_event_(ts_event)
    , ts_init_(ts_init) {
}

std::string Data::toString() const {
    std::stringstream ss;
    ss << "Data{type=";
    
    switch (type_) {
        case DataType::TRADE_TICK: ss << "TRADE_TICK"; break;
        case DataType::QUOTE_TICK: ss << "QUOTE_TICK"; break;
        case DataType::BAR: ss << "BAR"; break;
        case DataType::INSTRUMENT: ss << "INSTRUMENT"; break;
        case DataType::ORDER_BOOK_DELTA: ss << "ORDER_BOOK_DELTA"; break;
        case DataType::ORDER_BOOK: ss << "ORDER_BOOK"; break;
        case DataType::INSTRUMENT_STATUS: ss << "INSTRUMENT_STATUS"; break;
        case DataType::INSTRUMENT_CLOSE: ss << "INSTRUMENT_CLOSE"; break;
        case DataType::UNKNOWN: ss << "UNKNOWN"; break;
    }
    
    ss << ", instrument=" << instrument_id_
       << ", ts_event=" << ts_event_
       << ", ts_init=" << ts_init_ << "}";
    
    return ss.str();
}

// TradeTick 实现
TradeTick::TradeTick(
    InstrumentId instrument_id,
    Price price,
    Quantity size,
    std::string trade_id,
    std::string buyer_order_id,
    std::string seller_order_id,
    bool buyer_maker,
    UnixNanos ts_event,
    UnixNanos ts_init)
    : Data(DataType::TRADE_TICK, std::move(instrument_id), ts_event, ts_init)
    , price_(price)
    , size_(size)
    , trade_id_(std::move(trade_id))
    , buyer_order_id_(std::move(buyer_order_id))
    , seller_order_id_(std::move(seller_order_id))
    , buyer_maker_(buyer_maker) {
}

std::string TradeTick::toString() const {
    std::stringstream ss;
    ss << "TradeTick{" << Data::toString().substr(5)
       << ", price=" << std::fixed << std::setprecision(8) << price_
       << ", size=" << std::fixed << std::setprecision(8) << size_
       << ", trade_id=" << trade_id_
       << ", buyer_maker=" << (buyer_maker_ ? "true" : "false") << "}";
    
    return ss.str();
}

// QuoteTick 实现
QuoteTick::QuoteTick(
    InstrumentId instrument_id,
    Price bid_price,
    Price ask_price,
    Quantity bid_size,
    Quantity ask_size,
    UnixNanos ts_event,
    UnixNanos ts_init)
    : Data(DataType::QUOTE_TICK, std::move(instrument_id), ts_event, ts_init)
    , bid_price_(bid_price)
    , ask_price_(ask_price)
    , bid_size_(bid_size)
    , ask_size_(ask_size) {
}

std::string QuoteTick::toString() const {
    std::stringstream ss;
    ss << "QuoteTick{" << Data::toString().substr(5)
       << ", bid_price=" << std::fixed << std::setprecision(8) << bid_price_
       << ", ask_price=" << std::fixed << std::setprecision(8) << ask_price_
       << ", bid_size=" << std::fixed << std::setprecision(8) << bid_size_
       << ", ask_size=" << std::fixed << std::setprecision(8) << ask_size_ << "}";
    
    return ss.str();
}

// Bar 实现
Bar::Bar(
    BarType bar_type,
    Price open,
    Price high,
    Price low,
    Price close,
    Quantity volume,
    UnixNanos ts_event,
    UnixNanos ts_init)
    : Data(DataType::BAR, bar_type.instrument_id, ts_event, ts_init)
    , bar_type_(std::move(bar_type))
    , open_(open)
    , high_(high)
    , low_(low)
    , close_(close)
    , volume_(volume) {
}

std::string Bar::toString() const {
    std::stringstream ss;
    ss << "Bar{" << Data::toString().substr(5)
       << ", bar_type=" << bar_type_.toString()
       << ", O=" << std::fixed << std::setprecision(8) << open_
       << ", H=" << std::fixed << std::setprecision(8) << high_
       << ", L=" << std::fixed << std::setprecision(8) << low_
       << ", C=" << std::fixed << std::setprecision(8) << close_
       << ", V=" << std::fixed << std::setprecision(8) << volume_ << "}";
    
    return ss.str();
}

// InstrumentStatus 实现
InstrumentStatus::InstrumentStatus(
    InstrumentId instrument_id,
    InstrumentStatusType status,
    std::string message,
    UnixNanos ts_event,
    UnixNanos ts_init)
    : Data(DataType::INSTRUMENT_STATUS, std::move(instrument_id), ts_event, ts_init)
    , status_(status)
    , message_(std::move(message)) {
}

std::string InstrumentStatus::toString() const {
    std::stringstream ss;
    ss << "InstrumentStatus{" << Data::toString().substr(5)
       << ", status=";
    
    switch (status_) {
        case InstrumentStatusType::UNDEFINED: ss << "UNDEFINED"; break;
        case InstrumentStatusType::PRE_OPEN: ss << "PRE_OPEN"; break;
        case InstrumentStatusType::OPEN: ss << "OPEN"; break;
        case InstrumentStatusType::CLOSE: ss << "CLOSE"; break;
        case InstrumentStatusType::PRE_CLOSE: ss << "PRE_CLOSE"; break;
        case InstrumentStatusType::HALT: ss << "HALT"; break;
        case InstrumentStatusType::AUCTION: ss << "AUCTION"; break;
        case InstrumentStatusType::SETTLEMENT: ss << "SETTLEMENT"; break;
    }
    
    ss << ", message=" << message_ << "}";
    
    return ss.str();
}

} // namespace backtest 