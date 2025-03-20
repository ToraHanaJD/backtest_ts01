// -------------------------------------------------------------------------------------------------
//  Copyright (C) 2023-2025. All rights reserved.
//
//  Licensed under the MIT License.
// -------------------------------------------------------------------------------------------------

#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <map>
#include <memory>
#include <optional>
#include "backtest/data.h"

namespace backtest {

/**
 * @brief 订单簿类型枚举
 */
enum class BookType {
    L1,    // 一级行情
    L2,    // 二级行情
    L3     // 三级行情
};

/**
 * @brief 订单簿更新类型
 */
enum class OrderBookAction {
    ADD,       // 添加
    UPDATE,    // 更新
    DELETE     // 删除
};

/**
 * @brief 订单簿价格级别
 */
class OrderBookLevel {
public:
    OrderBookLevel(Price price, Quantity size);
    
    Price price() const { return price_; }
    Quantity size() const { return size_; }
    
    std::string toString() const;

private:
    Price price_;
    Quantity size_;
};

/**
 * @brief 订单簿更新数据
 */
class OrderBookDelta : public Data {
public:
    OrderBookDelta(
        InstrumentId instrument_id,
        OrderBookAction action,
        bool is_bid,
        Price price,
        Quantity size,
        UnixNanos ts_event,
        UnixNanos ts_init);
    
    OrderBookAction action() const { return action_; }
    bool isBid() const { return is_bid_; }
    Price price() const { return price_; }
    Quantity size() const { return size_; }
    
    std::string toString() const override;
    
private:
    OrderBookAction action_;
    bool is_bid_;
    Price price_;
    Quantity size_;
};

/**
 * @brief 订单簿更新集合
 */
class OrderBookDeltas : public Data {
public:
    OrderBookDeltas(
        InstrumentId instrument_id,
        std::vector<OrderBookDelta> deltas,
        UnixNanos ts_event,
        UnixNanos ts_init);
    
    const std::vector<OrderBookDelta>& deltas() const { return deltas_; }
    
    std::string toString() const override;
    
private:
    std::vector<OrderBookDelta> deltas_;
};

/**
 * @brief 订单簿
 */
class OrderBook {
public:
    OrderBook(InstrumentId instrument_id, BookType type = BookType::L2);
    
    /**
     * @brief 应用订单簿更新
     */
    void applyDelta(const OrderBookDelta& delta);
    
    /**
     * @brief 应用多个订单簿更新
     */
    void applyDeltas(const OrderBookDeltas& deltas);
    
    /**
     * @brief 获取最优买价
     */
    Price bestBidPrice() const;
    
    /**
     * @brief 获取最优卖价
     */
    Price bestAskPrice() const;
    
    /**
     * @brief 获取指定价格的买单数量
     */
    Quantity bidSize(Price price) const;
    
    /**
     * @brief 获取指定价格的卖单数量
     */
    Quantity askSize(Price price) const;
    
    /**
     * @brief 获取所有买单价格级别
     */
    std::vector<OrderBookLevel> bids() const;
    
    /**
     * @brief 获取所有卖单价格级别
     */
    std::vector<OrderBookLevel> asks() const;
    
    /**
     * @brief 获取中间价
     */
    Price midPrice() const;
    
    /**
     * @brief 获取当前价差
     */
    Price spread() const;
    
    /**
     * @brief 判断订单簿是否为空
     */
    bool isEmpty() const;
    
    /**
     * @brief 清空订单簿
     */
    void clear();
    
    /**
     * @brief 获取金融工具ID
     */
    InstrumentId instrumentId() const { return instrument_id_; }
    
    /**
     * @brief 获取订单簿类型
     */
    BookType type() const { return type_; }
    
    /**
     * @brief 获取订单簿字符串表示
     */
    std::string toString() const;
    
    /**
     * @brief 获取指定深度的订单簿状态
     */
    std::vector<OrderBookLevel> bids(size_t depth) const;
    std::vector<OrderBookLevel> asks(size_t depth) const;
    
private:
    InstrumentId instrument_id_;
    BookType type_;
    std::map<Price, Quantity, std::greater<Price>> bids_;
    std::map<Price, Quantity> asks_;
};

} // namespace backtest 