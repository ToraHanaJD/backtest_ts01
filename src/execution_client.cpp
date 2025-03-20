// -------------------------------------------------------------------------------------------------
//  Copyright (C) 2023-2025. All rights reserved.
//
//  Licensed under the MIT License.
// -------------------------------------------------------------------------------------------------

#include "backtest/execution_client.h"
#include "backtest/exchange.h"
#include "backtest/clock.h"
#include <stdexcept>
#include <iostream>
#include <iomanip>
#include <sstream>

namespace backtest {

extern UUID generateUUID();

// time format
std::string formatTime(UnixNanos time_ns) {
    auto time_ms = time_ns / 1000000;
    auto ms = time_ms % 1000;
    auto time_sec = time_ms / 1000;
    auto sec = time_sec % 60;
    auto min = (time_sec / 60) % 60;
    auto hour = (time_sec / 3600) % 24;
    
    std::stringstream ss;
    ss << std::setfill('0') << std::setw(2) << hour << ":"
       << std::setfill('0') << std::setw(2) << min << ":"
       << std::setfill('0') << std::setw(2) << sec << "."
       << std::setfill('0') << std::setw(3) << ms;
    
    return ss.str();
}

// default account state callback
void defaultAccountStateCallback(const std::vector<AccountBalance>& balances, UnixNanos timestamp) {
    double total_value = 0.0;
    std::string base_currency;
    
    for (const auto& balance : balances) {
        if (balance.currency() == "USD" || balance.currency() == "USDT") {
            base_currency = balance.currency();
            total_value += balance.total();
            break;
        }
    }
    
    if (base_currency.empty() && !balances.empty()) {
        // 如果没有找到USD或USDT，默认使用第一个货币作为基础货币
        base_currency = balances[0].currency();
    }
    
    std::cout << "\n==== Account Asset Report - " << formatTime(timestamp) << " ====\n";
    std::cout << "Asset Details:\n";
    std::cout << std::left << std::setw(10) << "Currency" 
              << std::right << std::setw(15) << "Available" 
              << std::right << std::setw(15) << "Locked"
              << std::right << std::setw(15) << "Total"
              << std::right << std::setw(15) << "Equivalent (" << base_currency << ")" << std::endl;
    
    std::cout << std::string(60, '-') << std::endl;
    
    for (const auto& balance : balances) {
        if (balance.total() > 0) {
            // todo : 这里可以添加汇率转换计算
            double equivalent_value = balance.total();
            if (balance.currency() != base_currency) {
                // todo : 实际应用中应当使用真实汇率
                equivalent_value = balance.total(); // * exchange_rate;
            }
            
            total_value += equivalent_value;
            
            std::cout << std::left << std::setw(10) << balance.currency() 
                      << std::right << std::setw(15) << std::fixed << std::setprecision(8) << balance.free()
                      << std::right << std::setw(15) << std::fixed << std::setprecision(8) << balance.locked()
                      << std::right << std::setw(15) << std::fixed << std::setprecision(8) << balance.total()
                      << std::right << std::setw(15) << std::fixed << std::setprecision(2) << equivalent_value
                      << std::endl;
        }
    }
    
    std::cout << std::string(60, '-') << std::endl;
    std::cout << "总资产估值: " << std::fixed << std::setprecision(2) << total_value << " " << base_currency << std::endl;
    std::cout << "======================================\n" << std::endl;
    
    // todo : 这里可以添加以下功能：
    // 1. 风险度计算
    // 2. 保证金水平监控
    // 3. 账户净值变化跟踪
    // 4. 触发风险预警
    // 5. 更新性能指标（如收益率、夏普比率等）
    // 6. 将账户状态数据写入数据库或日志
}

BacktestExecutionClient::BacktestExecutionClient(
    UUID trader_id,
    UUID account_id,
    std::shared_ptr<SimulatedExchange> exchange,
    std::shared_ptr<Clock> clock,
    bool routing,
    bool frozen_account)
    : trader_id_(std::move(trader_id))
    , account_id_(std::move(account_id))
    , exchange_(std::move(exchange))
    , clock_(std::move(clock))
    , is_connected_(false)
    , routing_(routing)
    , frozen_account_(frozen_account)
    , account_state_callback_(defaultAccountStateCallback) {
}

UUID BacktestExecutionClient::clientId() const {
    return trader_id_;
}

UUID BacktestExecutionClient::accountId() const {
    return account_id_;
}

Venue BacktestExecutionClient::venue() const {
    return exchange_->venue();
}

OmsType BacktestExecutionClient::omsType() const {
    return exchange_->omsType();
}

const Account* BacktestExecutionClient::getAccount() const {
    return exchange_->getAccount();
}

void BacktestExecutionClient::generateAccountState(
    const std::vector<AccountBalance>& balances,
    bool reported,
    UnixNanos ts_event) {
    if (!is_connected_) {
        throw std::runtime_error("Client not connected");
    }
    
    if (exchange_->getAccount()) {
        // 获取账户当前所有余额
        std::vector<AccountBalance> current_balances;
        
        if (balances.empty()) {
            // 如果没有提供余额，从账户获取
            const Account* account = exchange_->getAccount();
            if (account) {
                current_balances = account->balances(); // 使用已存在的balances()方法
            }
        } else {
            current_balances = balances;
        }
        
        // 通知账户状态已更新（始终调用回调）
        if (account_state_callback_) {
            account_state_callback_(current_balances, ts_event);
        }
        
        if (reported) {
            std::cout << "Account State Updated - Time: " << formatTime(ts_event) << std::endl;
            std::cout << "   AccountID: " << account_id_ << std::endl;
            std::cout << "   Balance Count: " << current_balances.size() << std::endl;
            std::cout << "---------------------------------------------------" << std::endl;
        }
    }
}

void BacktestExecutionClient::start() {
    if (is_connected_) {
        return;
    }
    
    is_connected_ = true;
}

void BacktestExecutionClient::stop() {
    if (!is_connected_) {
        return;
    }
    
    is_connected_ = false;
    cancelAllOrders();
}

bool BacktestExecutionClient::isConnected() const {
    return is_connected_;
}

void BacktestExecutionClient::submitOrder(std::shared_ptr<Order> order) {
    if (!is_connected_) {
        throw std::runtime_error("Client not connected");
    }
    
    if (frozen_account_) {
        throw std::runtime_error("Account is frozen");
    }
    
    if (!order) {
        throw std::runtime_error("Order is null");
    }
    
    UUID client_order_id = order->clientOrderId();
    
    // 用当前时间
    UnixNanos timestamp = clock_->timestampNs();
    
    // 保留原始订单的client_order_id
    auto new_order = std::make_shared<Order>(
        client_order_id,
        venue(),
        order->instrumentId(),
        order->side(),
        order->type(),
        order->quantity(),
        order->price(),
        order->timeInForce(),
        timestamp
    );

    try {
        orders_.emplace(client_order_id, *new_order);
        
        auto instrument_id = new_order->instrumentId();
        auto it = orders_by_instrument_.find(instrument_id);
        if (it == orders_by_instrument_.end()) {
            std::vector<UUID> order_ids = {client_order_id};
            orders_by_instrument_.insert(std::make_pair(instrument_id, std::move(order_ids)));
        } else {
            it->second.push_back(client_order_id);
        }
        std::cout << "Submit Order" << std::endl;
        TradingCommand command(TradingCommand::Type::SUBMIT_ORDER, client_order_id, new_order);
        exchange_->send(command);
        
        std::cout << "Order Submit Success, ID: " << client_order_id 
                  << ", InstrumentID: " << order->instrumentId()
                  << ", Price: " << order->price()
                  << ", Quantity: " << order->quantity() << std::endl;
    }
    catch (const std::exception& e) {
        std::cerr << "Order Submit Failed: " << e.what() << std::endl;
        throw;
    }
}

void BacktestExecutionClient::cancelOrder(const UUID& order_id) {
    if (!is_connected_) {
        throw std::runtime_error("Client not connected");
    }
    
    if (frozen_account_) {
        throw std::runtime_error("Account is frozen");
    }
    
    auto it = orders_.find(order_id);
    if (it == orders_.end()) {
        throw std::runtime_error("Order not found");
    }
    TradingCommand command(TradingCommand::Type::CANCEL_ORDER, order_id);
    exchange_->send(command);
}

void BacktestExecutionClient::cancelAllOrders() {
    if (!is_connected_) {
        throw std::runtime_error("Client not connected");
    }
    
    if (frozen_account_) {
        throw std::runtime_error("Account is frozen");
    }
    
    TradingCommand command(TradingCommand::Type::CANCEL_ALL_ORDERS, generateUUID());
    exchange_->send(command);
    
    orders_.clear();
    orders_by_instrument_.clear();
}

void BacktestExecutionClient::modifyOrder(
    const UUID& order_id,
    Price new_price,
    Quantity new_quantity) {
    if (!is_connected_) {
        throw std::runtime_error("Client not connected");
    }
    
    if (frozen_account_) {
        throw std::runtime_error("Account is frozen");
    }
    
    auto it = orders_.find(order_id);
    if (it == orders_.end()) {
        throw std::runtime_error("Order not found");
    }
    
    Order original_order = it->second;
    auto modified_order = std::make_shared<Order>(
        generateUUID(),
        original_order.venue(),
        original_order.instrumentId(),
        original_order.side(),
        original_order.type(),
        new_quantity,
        new_price,
        original_order.timeInForce(),
        clock_->timestampNs()
    );
    
    TradingCommand command(TradingCommand::Type::MODIFY_ORDER, order_id, modified_order);
    exchange_->send(command);
}

void BacktestExecutionClient::queryOrder(const UUID& order_id) {
}

void BacktestExecutionClient::notifyFill(const Fill& fill, std::shared_ptr<Order> order) {
    if (!is_connected_) {
        return;
    }
    
    UUID order_id = order->clientOrderId();
    auto it = orders_.find(order_id);
    
    if (it != orders_.end()) {
        it->second.setStatus(order->status());
        it->second.setFilledQuantity(order->filledQuantity());
    } else {
        orders_.emplace(
            order_id,
            Order(
                order->clientOrderId(),
                order->venue(),
                order->instrumentId(),
                order->side(),
                order->type(),
                order->quantity(),
                order->price(),
                order->timeInForce(),
                order->timestamp()
            )
        );
        
        InstrumentId instrument_id = order->instrumentId();
        auto inst_it = orders_by_instrument_.find(instrument_id);
        if (inst_it != orders_by_instrument_.end()) {
            inst_it->second.push_back(order_id);
        } else {
            orders_by_instrument_.insert({instrument_id, {order_id}});
        }
    }
    
    UnixNanos current_time = clock_->timestampNs();
    
    std::cout << "Order Fill Notice - Time: " << formatTime(current_time) << std::endl
              << "   OrderID: " << order_id << std::endl
              << "   InstrumentID: " << order->instrumentId() << std::endl
              << "   Side: " << (order->side() == OrderSide::BUY ? "Buy" : "Sell") << std::endl
              << "   Price: " << fill.price() << std::endl
              << "   Quantity: " << fill.quantity() << std::endl
              << "   Order Status: " << (order->status() == OrderStatus::FILLED ? "Filled" : "Partially Filled") << std::endl
              << "   Filled Quantity: " << order->filledQuantity() << " / " << order->quantity() << std::endl
              << "   Fill Time: " << formatTime(fill.timestamp()) << std::endl
              << "---------------------------------------------------" << std::endl;
    
    // todo:可以添加其他处理逻辑，如：触发回调或事件或者执行后续的交易策略
}

void BacktestExecutionClient::setAccountStateCallback(AccountStateCallback callback) {
    if (callback) {
        account_state_callback_ = std::move(callback);
    } else {
        // 如果传入空回调，设置为默认回调
        account_state_callback_ = defaultAccountStateCallback;
    }
}

} // namespace backtest 