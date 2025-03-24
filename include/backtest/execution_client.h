// -------------------------------------------------------------------------------------------------
//  Copyright (C) 2023-2025. All rights reserved.
//
//  Licensed under the MIT License.
// -------------------------------------------------------------------------------------------------

#pragma once

#include <cstdint>
#include <string>
#include <memory>
#include <unordered_map>
#include <functional>
#include "backtest/exchange.h"

namespace backtest {

// 前向声明
class Clock;
class SimulatedExchange;

// 添加账户状态更新回调类型定义
using AccountStateCallback = std::function<void(const std::vector<AccountBalance>&, UnixNanos)>;

/**
 * @brief 执行客户端抽象接口
 */
class ExecutionClient {
public:
    virtual ~ExecutionClient() = default;
    
    // 客户端ID
    virtual UUID clientId() const = 0;
    
    // 账户ID
    virtual UUID accountId() const = 0;
    
    // 交易所
    virtual Venue venue() const = 0;
    
    // 订单管理系统类型
    virtual OmsType omsType() const = 0;
    
    // 获取账户
    virtual const Account* getAccount() const = 0;
    
    // 生成账户状态
    virtual void generateAccountState(
        const std::vector<AccountBalance>& balances, 
        bool reported, 
        UnixNanos ts_event) = 0;
    
    // 开始
    virtual void start() = 0;
    
    // 停止
    virtual void stop() = 0;
    
    // 是否已连接
    virtual bool isConnected() const = 0;
    
    // 提交订单
    virtual void submitOrder(std::shared_ptr<Order> order) = 0;
    
    // 取消订单
    virtual void cancelOrder(const UUID& order_id) = 0;
    
    // 取消所有订单
    virtual void cancelAllOrders() = 0;
    
    // 修改订单
    virtual void modifyOrder(
        const UUID& order_id, 
        Price new_price, 
        Quantity new_quantity) = 0;
    
    // 查询订单
    virtual void queryOrder(const UUID& order_id) = 0;
    
    // 通知订单成交
    virtual void notifyFill(const Fill& fill, std::shared_ptr<Order> order) = 0;
    
    // 设置账户状态回调
    virtual void setAccountStateCallback(AccountStateCallback callback) = 0;
};

/**
 * @brief 回测执行客户端实现
 */
class BacktestExecutionClient : public ExecutionClient {
public:
    BacktestExecutionClient(
        UUID trader_id,
        UUID account_id,
        std::shared_ptr<SimulatedExchange> exchange,
        std::shared_ptr<Clock> clock,
        bool routing = false,
        bool frozen_account = false);
    
    // 客户端ID
    UUID clientId() const override;
    
    // 账户ID
    UUID accountId() const override;
    
    // 交易所
    Venue venue() const override;
    
    // 订单管理系统类型
    OmsType omsType() const override;
    
    // 获取账户
    const Account* getAccount() const override;
    
    // 生成账户状态
    void generateAccountState(
        const std::vector<AccountBalance>& balances, 
        bool reported, 
        UnixNanos ts_event) override;
    
    // 开始
    void start() override;
    
    // 停止
    void stop() override;
    
    // 是否已连接
    bool isConnected() const override;
    
    // 提交订单
    void submitOrder(std::shared_ptr<Order> order) override;
    
    // 取消订单
    void cancelOrder(const UUID& order_id) override;
    
    // 取消所有订单
    void cancelAllOrders() override;
    
    // 修改订单
    void modifyOrder(
        const UUID& order_id, 
        Price new_price, 
        Quantity new_quantity) override;
    
    // 查询订单
    void queryOrder(const UUID& order_id) override;
    
    // 通知订单成交
    void notifyFill(const Fill& fill, std::shared_ptr<Order> order) override;
    
    // 设置账户状态回调
    void setAccountStateCallback(AccountStateCallback callback) override;

private:
    UUID trader_id_;
    UUID account_id_;
    std::shared_ptr<SimulatedExchange> exchange_;
    std::shared_ptr<Clock> clock_;
    bool is_connected_;
    bool routing_;
    bool frozen_account_;
    
    // 缓存数据, 可以放入redis
    std::unordered_map<UUID, Order> orders_;
    std::unordered_map<InstrumentId, std::vector<UUID>> orders_by_instrument_;
    
    // 账户状态回调
    AccountStateCallback account_state_callback_;
};

} // namespace backtest 