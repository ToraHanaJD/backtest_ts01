// -------------------------------------------------------------------------------------------------
//  Copyright (C) 2023-2025. All rights reserved.
//
//  Licensed under the MIT License.
// -------------------------------------------------------------------------------------------------

#pragma once

#include <cstdint>
#include <string>
#include <memory>
#include <vector>
#include <atomic>
#include <random>
#include <mutex>
#include <thread>
#include <chrono>

namespace backtest {

using UnixNanos = uint64_t;
using UUID = uint64_t;
using Venue = uint64_t;
using InstrumentId = uint64_t;
using ClientId = uint64_t;
using Price = double;
using Quantity = double;

/**
 * @brief 生成一个线程安全的唯一UUID
 */
inline UUID generateUUID() {
    static std::atomic<uint32_t> counter(1000);
    static thread_local std::mt19937 rng(std::hash<std::thread::id>{}(std::this_thread::get_id()) ^ 
                                         static_cast<unsigned>(std::chrono::high_resolution_clock::now().time_since_epoch().count()));
    static thread_local std::uniform_int_distribution<uint32_t> dist(0, 0xFFFF);
    
    // 获取计数器的下一个值
    uint32_t high = counter.fetch_add(1, std::memory_order_relaxed);
    
    // 使用线程ID和随机值生成低32位
    uint32_t thread_id_hash = static_cast<uint32_t>(std::hash<std::thread::id>{}(std::this_thread::get_id()));
    uint32_t low = (thread_id_hash & 0xFFFF0000) | (dist(rng) & 0x0000FFFF);
    
    // 组合高32位和低32位
    return (static_cast<uint64_t>(high) << 32) | low;
}

/**
 * @brief 数据类型枚举
 */
enum class DataType {
    TRADE_TICK,       // 交易行情
    QUOTE_TICK,       // 报价行情
    BAR,              // K线
    INSTRUMENT,       // 金融工具
    ORDER_BOOK_DELTA, // 订单簿增量更新
    ORDER_BOOK,       // 订单簿快照
    INSTRUMENT_STATUS, // 金融工具状态
    INSTRUMENT_CLOSE, // 金融工具收盘
    UNKNOWN
};

/**
 * @brief 时间区间规范
 */
enum class BarSpecification {
    SECOND_1,
    SECOND_10,
    MINUTE_1,
    MINUTE_5,
    MINUTE_15,
    MINUTE_30,
    HOUR_1,
    HOUR_4,
    DAY_1,
    WEEK_1,
    MONTH_1,
    CUSTOM
};

enum class BarAggregation {
    BID,         // 卖方报价聚合
    ASK,         // 买方报价聚合
    MID,         // 中间价聚合
    TRADE,       // 交易聚合
    VOLUME_WEIGHTED_MID, // 交易量加权中间价
    UNDEFINED
};

class BarType {
public:
    BarType(InstrumentId instrument_id, BarSpecification specification, BarAggregation aggregation);
    
    InstrumentId instrument_id;
    BarSpecification specification;
    BarAggregation aggregation;
    
    std::string toString() const;
    
    bool operator==(const BarType& other) const;
    bool operator!=(const BarType& other) const;
};

/**
 * @brief 数据基类
 */
class Data {
public:
    virtual ~Data() = default;
    
    Data(DataType type, InstrumentId instrument_id, UnixNanos ts_event, UnixNanos ts_init);
    
    DataType type() const { return type_; }
    InstrumentId instrumentId() const { return instrument_id_; }
    UnixNanos tsEvent() const { return ts_event_; }
    UnixNanos tsInit() const { return ts_init_; }
    
    virtual std::string toString() const;
    
private:
    DataType type_;
    InstrumentId instrument_id_;
    UnixNanos ts_event_;
    UnixNanos ts_init_;
};

/**
 * @brief 交易行情数据
 */
class TradeTick : public Data {
public:
    TradeTick(
        InstrumentId instrument_id,
        Price price,
        Quantity size,
        std::string trade_id,
        std::string buyer_order_id,
        std::string seller_order_id,
        bool buyer_maker,
        UnixNanos ts_event,
        UnixNanos ts_init);
    
    Price price() const { return price_; }
    Quantity size() const { return size_; }
    std::string tradeId() const { return trade_id_; }
    std::string buyerOrderId() const { return buyer_order_id_; }
    std::string sellerOrderId() const { return seller_order_id_; }
    bool buyerMaker() const { return buyer_maker_; }
    
    std::string toString() const override;
    
private:
    Price price_;
    Quantity size_;
    std::string trade_id_;
    std::string buyer_order_id_;
    std::string seller_order_id_;
    bool buyer_maker_;
};

class QuoteTick : public Data {
public:
    QuoteTick(
        InstrumentId instrument_id,
        Price bid_price,
        Price ask_price,
        Quantity bid_size,
        Quantity ask_size,
        UnixNanos ts_event,
        UnixNanos ts_init);
    
    Price bidPrice() const { return bid_price_; }
    Price askPrice() const { return ask_price_; }
    Quantity bidSize() const { return bid_size_; }
    Quantity askSize() const { return ask_size_; }
    
    std::string toString() const override;
    
private:
    Price bid_price_;
    Price ask_price_;
    Quantity bid_size_;
    Quantity ask_size_;
};


class Bar : public Data {
public:
    Bar(
        BarType bar_type,
        Price open,
        Price high,
        Price low,
        Price close,
        Quantity volume,
        UnixNanos ts_event,
        UnixNanos ts_init);
    
    BarType barType() const { return bar_type_; }
    Price open() const { return open_; }
    Price high() const { return high_; }
    Price low() const { return low_; }
    Price close() const { return close_; }
    Quantity volume() const { return volume_; }
    
    std::string toString() const override;
    
private:
    BarType bar_type_;
    Price open_;
    Price high_;
    Price low_;
    Price close_;
    Quantity volume_;
};

enum class InstrumentStatusType {
    UNDEFINED,
    PRE_OPEN,     // 开盘前
    OPEN,         // 开盘
    CLOSE,        // 收盘
    PRE_CLOSE,    // 收盘前
    HALT,         // 停牌
    AUCTION,      // 竞价
    SETTLEMENT    // 结算
};

class InstrumentStatus : public Data {
public:
    InstrumentStatus(
        InstrumentId instrument_id,
        InstrumentStatusType status,
        std::string message,
        UnixNanos ts_event,
        UnixNanos ts_init);
    
    InstrumentStatusType status() const { return status_; }
    std::string message() const { return message_; }
    
    std::string toString() const override;
    
private:
    InstrumentStatusType status_;
    std::string message_;
};

template <typename T, typename... Args>
std::unique_ptr<T> makeData(Args&&... args) {
    return std::unique_ptr<T>(new T(std::forward<Args>(args)...));
}

} // namespace backtest 