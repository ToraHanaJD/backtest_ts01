// -------------------------------------------------------------------------------------------------
//  Copyright (C) 2023-2025. All rights reserved.
//
//  Licensed under the MIT License.
// -------------------------------------------------------------------------------------------------

#include "backtest/exchange.h"
#include "backtest/clock.h"
#include "backtest/simulation_module.h"
#include "backtest/execution_client.h"
#include <stdexcept>
#include <memory>
#include <random>
#include <sstream>
#include <iomanip>
#include <iostream>

namespace backtest {

// 前向声明调试辅助函数
inline std::string debugUUID(UUID id);
inline std::string debugVenue(Venue id);
inline std::string debugInstrumentId(InstrumentId id);
inline std::string debugClientId(ClientId id);

// Order 类实现
Order::Order(
    UUID client_order_id,
    Venue venue,
    InstrumentId instrument_id,
    OrderSide side,
    OrderType type,
    Quantity quantity,
    Price price,
    TimeInForce time_in_force,
    UnixNanos timestamp)
    : client_order_id_(std::move(client_order_id))
    , venue_(std::move(venue))
    , instrument_id_(std::move(instrument_id))
    , side_(side)
    , type_(type)
    , quantity_(quantity)
    , filled_quantity_(0)
    , price_(price)
    , time_in_force_(time_in_force)
    , timestamp_(timestamp)
    , status_(OrderStatus::INITIALIZED) {
}

std::string Order::toString() const {
    return "Order(" + 
           std::to_string(client_order_id_) + "," +
           std::to_string(venue_) + "," +
           std::to_string(instrument_id_) + "," +
           (side_ == OrderSide::BUY ? "BUY" : "SELL") + "," +
           std::to_string(static_cast<int>(type_)) + "," +
           std::to_string(quantity_) + "," +
           std::to_string(price_) + "," +
           std::to_string(static_cast<int>(time_in_force_)) + "," +
           std::to_string(timestamp_) + "," +
           std::to_string(static_cast<int>(status_)) + ")";
}

// Fill 类实现
Fill::Fill(
    UUID fill_id,
    UUID order_id,
    InstrumentId instrument_id,
    OrderSide side,
    Price price,
    Quantity quantity,
    UnixNanos timestamp)
    : fill_id_(std::move(fill_id))
    , order_id_(std::move(order_id))
    , instrument_id_(std::move(instrument_id))
    , side_(side)
    , price_(price)
    , quantity_(quantity)
    , timestamp_(timestamp)
    , commission_(Money("USDT", 0.0)) {  // 默认手续费为0
}

std::string Fill::toString() const {
    return "Fill(" +
           std::to_string(fill_id_) + "," +
           std::to_string(order_id_) + "," +
           std::to_string(instrument_id_) + "," +
           (side_ == OrderSide::BUY ? "BUY" : "SELL") + "," +
           std::to_string(price_) + "," +
           std::to_string(quantity_) + "," +
           std::to_string(timestamp_) + "," +
           commission_.toString() + ")";
}

// AccountBalance 类实现
AccountBalance::AccountBalance(Currency currency, double free, double locked)
    : currency_(std::move(currency))
    , free_(free)
    , locked_(locked) {
}

void AccountBalance::lockAmount(double amount) {
    if (amount > free_) {
        throw std::runtime_error("Insufficient free balance");
    }
    free_ -= amount;
    locked_ += amount;
}

void AccountBalance::unlockAmount(double amount) {
    if (amount > locked_) {
        throw std::runtime_error("Insufficient locked balance");
    }
    locked_ -= amount;
    free_ += amount;
}

void AccountBalance::transferLockedToFree(double amount) {
    if (amount > locked_) {
        throw std::runtime_error("Insufficient locked balance");
    }
    locked_ -= amount;
    free_ += amount;
}

std::string AccountBalance::toString() const {
    return "AccountBalance(" +
           currency_ + "," +
           std::to_string(free_) + "," +
           std::to_string(locked_) + ")";
}

// Account 类实现
Account::Account(UUID account_id, Venue venue, AccountType type, std::vector<AccountBalance> balances)
    : account_id_(std::move(account_id))
    , venue_(std::move(venue))
    , type_(type)
    , balances_(std::move(balances)) {
    
    // 打印初始余额信息
    std::cout << "Initializing account with balances:" << std::endl;
    for (const auto& balance : balances_) {
        std::cout << "Currency: " << balance.currency() 
                  << ", Free: " << balance.free()
                  << ", Locked: " << balance.locked() << std::endl;
    }
}

AccountBalance* Account::getBalance(const Currency& currency) {
    for (auto& balance : balances_) {
        if (balance.currency() == currency) {
            return &balance;
        }
    }
    return nullptr;
}

const AccountBalance* Account::getBalance(const Currency& currency) const {
    for (const auto& balance : balances_) {
        if (balance.currency() == currency) {
            return &balance;
        }
    }
    return nullptr;
}

void Account::addBalance(const AccountBalance& balance) {
    auto* existing = getBalance(balance.currency());
    if (existing) {
        existing->addFree(balance.free());
        existing->addLocked(balance.locked());
    } else {
        balances_.push_back(balance);
    }
}

void Account::updateBalance(const Currency& currency, double free, double locked) {
    auto* balance = getBalance(currency);
    if (balance) {
        balance->addFree(free);
        balance->addLocked(locked);
    } else {
        balances_.emplace_back(currency, free, locked);
    }
}

std::string Account::toString() const {
    std::string result = "Account(" +
                        std::to_string(account_id_) + "," +
                        std::to_string(venue_) + "," +
                        std::to_string(static_cast<int>(type_)) + ",[";
    
    for (size_t i = 0; i < balances_.size(); ++i) {
        result += balances_[i].toString();
        if (i < balances_.size() - 1) {
            result += ",";
        }
    }
    
    result += "])";
    return result;
}

TradingCommand::TradingCommand()
    : type_(Type::CANCEL_ALL_ORDERS)
    , id_(0)
    , order_(nullptr) {
}

TradingCommand::TradingCommand(Type type, UUID id, std::shared_ptr<Order> order)
    : type_(type)
    , id_(std::move(id))
    , order_(std::move(order)) {
}

std::string TradingCommand::toString() const {
    return "TradingCommand(" +
           std::to_string(static_cast<int>(type_)) + "," +
           std::to_string(id_) + "," +
           (order_ ? order_->toString() : "null") + ")";
}

InflightCommand::InflightCommand(UnixNanos ts, uint32_t counter, TradingCommand command)
    : ts(ts)
    , counter(counter)
    , command(std::move(command)) {
}

SimulatedExchange::SimulatedExchange(
    Venue venue,
    OmsType oms_type,
    AccountType account_type,
    std::vector<Money> starting_balances,
    BookType book_type,
    double default_leverage,
    bool frozen_account,
    bool bar_execution,
    bool reject_stop_orders,
    bool support_gtd_orders,
    bool support_contingent_orders,
    bool use_position_ids,
    bool use_random_ids,
    bool use_reduce_only,
    bool use_message_queue)
    : venue_(std::move(venue))
    , oms_type_(oms_type)
    , account_type_(account_type)
    , starting_balances_(std::move(starting_balances))
    , book_type_(book_type)
    , default_leverage_(default_leverage)
    , base_currency_(starting_balances_.empty() ? "" : starting_balances_[0].currency)
    , modules_()
    , frozen_account_(frozen_account)
    , bar_execution_(bar_execution)
    , reject_stop_orders_(reject_stop_orders)
    , support_gtd_orders_(support_gtd_orders)
    , support_contingent_orders_(support_contingent_orders)
    , use_position_ids_(use_position_ids)
    , use_random_ids_(use_random_ids)
    , use_reduce_only_(use_reduce_only)
    , use_message_queue_(use_message_queue)
    , last_process_time_(0) {
}

SimulatedExchange::~SimulatedExchange() = default;

void SimulatedExchange::registerClient(std::shared_ptr<ExecutionClient> client) {
    execution_client_ = std::move(client);
}

void SimulatedExchange::addInstrument(const InstrumentId& instrument_id) {
    if (order_books_.find(instrument_id) != order_books_.end()) {
        return;
    }
    
    // 1. 创建OrderBook并立即添加到order_books_映射中
    auto* order_book = createOrderBook(instrument_id);
    order_books_[instrument_id] = std::unique_ptr<OrderBook>(order_book);
    
    // 2. 确保订单簿初始化成功
    if (!order_books_[instrument_id]) {
        throw std::runtime_error("Failed to create order book for instrument: " + std::to_string(instrument_id));
    }
    
    // 3. 现在创建匹配引擎
    auto* matching_engine = createMatchingEngine(instrument_id);
    
    // 4. 添加到matching_engines_映射中
    matching_engines_[instrument_id] = std::unique_ptr<OrderMatchingEngine>(matching_engine);
    
    std::cout << "已添加交易品种: " << instrument_id 
              << ", 订单簿类型: " << static_cast<int>(book_type_) << std::endl;
}

void SimulatedExchange::initializeAccount() {
    if (!account_) {
        std::cout << "Initializing account with balances:" << std::endl;
        std::vector<AccountBalance> balances;
        
        // 检查 starting_balances_ 是否为空
        if (starting_balances_.empty()) {
            std::cout << "Warning: No starting balances provided!" << std::endl;
            // 添加默认的 USDT 余额
            balances.emplace_back("USDT", 10000.0, 0.0);
        } else {
            for (const auto& money : starting_balances_) {
                std::cout << "Initializing balance - Currency: " << money.currency 
                          << ", Amount: " << money.amount << std::endl;
                balances.emplace_back(money.currency, money.amount, 0.0);
            }
        }
        
        // 创建账户
        account_ = std::make_unique<Account>(
            generateUUID(),
            venue_,
            account_type_,
            std::move(balances)
        );
        
        // 验证账户初始化
        std::cout << "Account initialized successfully:" << std::endl;
        std::cout << "Account ID: " << account_->accountId() << std::endl;
        std::cout << "Venue: " << account_->venue() << std::endl;
        std::cout << "Account Type: " << static_cast<int>(account_->type()) << std::endl;
        std::cout << "Balances:" << std::endl;
        for (const auto& balance : account_->balances()) {
            std::cout << "  Currency: " << balance.currency() 
                      << ", Free: " << balance.free()
                      << ", Locked: " << balance.locked() << std::endl;
        }
    } else {
        std::cout << "Account already initialized" << std::endl;
    }
}

const OrderBook* SimulatedExchange::getOrderBook(const InstrumentId& instrument_id) const {
    auto it = order_books_.find(instrument_id);
    return it != order_books_.end() ? it->second.get() : nullptr;
}

Price SimulatedExchange::bestBidPrice(const InstrumentId& instrument_id) const {
    const auto* book = getOrderBook(instrument_id);
    return book ? book->bestBidPrice() : 0;
}

Price SimulatedExchange::bestAskPrice(const InstrumentId& instrument_id) const {
    const auto* book = getOrderBook(instrument_id);
    return book ? book->bestAskPrice() : 0;
}

const Account* SimulatedExchange::getAccount() const {
    return account_.get();
}

void SimulatedExchange::adjustAccount(const Money& adjustment) {
    if (!account_) {
        throw std::runtime_error("Account not initialized");
    }
    
    auto* balance = account_->getBalance(adjustment.currency);
    if (balance) {
        balance->addFree(adjustment.amount);
    } else {
        account_->addBalance(AccountBalance(adjustment.currency, adjustment.amount, 0.0));
    }
}

void SimulatedExchange::addModule(std::unique_ptr<SimulationModule> module) {
    modules_.push_back(std::move(module));
}

void SimulatedExchange::send(const TradingCommand& command) {
    if (use_message_queue_){
#if debug
        std::cout << "send command to message queue" << std::endl;
#endif
        message_queue_.push(command);
    } else {
#if debug
        std::cout << "send command to processTradingCommand" << std::endl;
#endif
        processTradingCommand(command);
    }
}

void SimulatedExchange::processQuoteTick(const QuoteTick& quote) {
    auto it = matching_engines_.find(quote.instrumentId());
    if (it != matching_engines_.end()) {
        it->second->processQuoteTick(quote);
    }
}

void SimulatedExchange::processTradeTick(const TradeTick& trade) {
    auto it = matching_engines_.find(trade.instrumentId());
    if (it != matching_engines_.end()) {
        it->second->processTradeTick(trade);
    }
}

void SimulatedExchange::processBar(const Bar& bar) {
    if (!bar_execution_) {
        return;
    }
    
    auto instrument_id = bar.barType().instrument_id;
    auto it = matching_engines_.find(instrument_id);
    if (it != matching_engines_.end()) {
        it->second->processBar(bar);
    }
}

void SimulatedExchange::processOrderBookDelta(const OrderBookDelta& delta) {
    auto it = matching_engines_.find(delta.instrumentId());
    if (it != matching_engines_.end()) {
        it->second->processOrderBookDelta(delta);
    }
}

void SimulatedExchange::processOrderBookDeltas(const OrderBookDeltas& deltas) {
    const auto& delta_vector = deltas.deltas();
    for (size_t i = 0; i < delta_vector.size(); ++i) {
        processOrderBookDelta(delta_vector[i]);
    }
}

void SimulatedExchange::processInstrumentStatus(const InstrumentStatus& status) {
    // TODO: 实现金融工具状态更新处理
}

void SimulatedExchange::process(UnixNanos ts_now) {
    last_process_time_ = ts_now;
    
    if (use_message_queue_) {
        TradingCommand command;
        while (message_queue_.try_pop(command)) {
            processTradingCommand(command);
        }
    }
    
    while (!inflight_queue_.empty()) {
        const auto& command = inflight_queue_.top();
        if (command.ts > ts_now) {
            break;
        }
        
        processTradingCommand(command.command);
        inflight_queue_.pop();
        
        auto it = inflight_counter_.find(command.ts);
        if (it != inflight_counter_.end()) {
            if (--it->second == 0) {
                inflight_counter_.erase(it);
            }
        }
    }
    
    for (const auto& module : modules_) {
        module->process(ts_now);
    }
}

void SimulatedExchange::reset() {
    order_books_.clear();
    
    matching_engines_.clear();
    
    TradingCommand cmd;
    while (message_queue_.try_pop(cmd)) {}
    
    while (!inflight_queue_.empty()) {
        inflight_queue_.pop();
    }
    inflight_counter_.clear();
    
    account_.reset();
    
    orders_.clear();
    fills_.clear();
    
    last_process_time_ = 0;
}

void SimulatedExchange::processTradingCommand(const TradingCommand& command) {
    switch (command.type()) {
        case TradingCommand::Type::SUBMIT_ORDER: {
            if (!command.order()) {
                throw std::runtime_error("No order in submit command");
            }
            auto* matching_engine = matching_engines_[command.order()->instrumentId()].get();
            if (!matching_engine) {
                throw std::runtime_error("No matching engine for instrument");
            }
#if debug
            std::cout << "MatchEngine Found" << std::endl;
#endif
            // Add order to orders collection
            orders_[command.order()->clientOrderId()] = command.order();
            
            // Submit order and process possible fills
            auto fills = matching_engine->submitOrder(command.order(), last_process_time_);
#if debug
            std::cout << "MatchEngine Submitted" << std::endl;
#endif
            for (const auto& fill : fills) {
                generateFill(command.order(), fill.price(), fill.quantity(), fill.timestamp());
            }
#if debug
            std::cout << "order submit OK" << std::endl;
#endif
            break;
        }
        
        case TradingCommand::Type::CANCEL_ORDER: {
            auto order_it = orders_.find(command.id());
            if (order_it == orders_.end()) {
                // If order not found locally, try to search in all matching engines
                bool canceled = false;
                
                for (auto& entry : matching_engines_) {
                    if (entry.second->cancelOrder(command.id())) {
                        std::cout << "Order canceled, ID: " << command.id() << std::endl;
                        canceled = true;
                        break;
                    }
                }
                
                if (!canceled) {
                    std::cout << "Warning: Order not found for cancellation, ID: " << command.id() << std::endl;
                }
            } else {
                // If order found, cancel directly in corresponding matching engine
                auto order = order_it->second;
                auto instrument_id = order->instrumentId();
                
                auto* matching_engine = matching_engines_[instrument_id].get();
                if (!matching_engine) {
                    throw std::runtime_error("No matching engine for instrument " + std::to_string(instrument_id));
                }
                
                if (matching_engine->cancelOrder(command.id())) {
                    std::cout << "Order canceled, ID: " << command.id() << std::endl;
                } else {
                    std::cout << "Warning: Order not found in Match Engine, OrderID: " << command.id() << std::endl;
                }
            }
            break;
        }
        
        case TradingCommand::Type::MODIFY_ORDER: {
            if (!command.order()) {
                throw std::runtime_error("No order in modify command");
            }
            
            UUID order_id = command.id();
            auto it = orders_.find(order_id);
            if (it == orders_.end()) {
                throw std::runtime_error("Order not found for modification");
            }
            
            auto original_order = it->second;
            auto* matching_engine = matching_engines_[original_order->instrumentId()].get();
            if (!matching_engine) {
                throw std::runtime_error("No matching engine for instrument");
            }
            
            matching_engine->cancelOrder(order_id);
            
            auto new_order = std::make_shared<Order>(
                generateUUID(),
                original_order->venue(),
                original_order->instrumentId(),
                original_order->side(),
                original_order->type(),
                command.order()->quantity(),  // 使用新的数量
                command.order()->price(),     // 使用新的价格
                original_order->timeInForce(),
                last_process_time_
            );
            
            // 提交新订单
            auto fills = matching_engine->submitOrder(new_order, last_process_time_);
            for (const auto& fill : fills) {
                generateFill(new_order, fill.price(), fill.quantity(), fill.timestamp());
            }
            
            // 保存新订单
            orders_[new_order->clientOrderId()] = new_order;
            
            break;
        }
        
        case TradingCommand::Type::CANCEL_ALL_ORDERS: {
            for (auto& entry : matching_engines_) {
                entry.second->clear();
            }
            break;
        }
    }
}

void SimulatedExchange::generateFill(
    std::shared_ptr<Order> order,
    Price price,
    Quantity quantity,
    UnixNanos timestamp) {
    
    auto fill = Fill(
        generateUUID(),
        order->clientOrderId(),
        order->instrumentId(),
        order->side(),
        price,
        quantity,
        timestamp
    );
    
    fills_.push_back(fill);
    updateAccount(fill);
    
    if (execution_client_) {
        // 通知执行客户端有新的成交信息
        if (order->isFilled()) {
            order->setStatus(OrderStatus::FILLED);
        } else {
            order->setStatus(OrderStatus::PARTIALLY_FILLED);
        }
        
        // 使用notifyFill方法通知客户端
        execution_client_->notifyFill(fill, order);
    }
}

OrderBook* SimulatedExchange::createOrderBook(const InstrumentId& instrument_id) {
    return new OrderBook(instrument_id, book_type_);
}

OrderMatchingEngine* SimulatedExchange::createMatchingEngine(const InstrumentId& instrument_id) {
    auto it = order_books_.find(instrument_id);
    if (it == order_books_.end() || !it->second) {
        throw std::runtime_error("Cannot create matching engine: order book not found for instrument " + std::to_string(instrument_id));
    }
    
    auto* order_book = it->second.get();
    if (!order_book) {
        throw std::runtime_error("Order book pointer is null for instrument " + std::to_string(instrument_id));
    }
    
    return new OrderMatchingEngine(instrument_id, order_book);
}

std::pair<UnixNanos, uint32_t> SimulatedExchange::generateInflightCommand(const TradingCommand& command) {
    UnixNanos ts = last_process_time_;
    uint32_t counter = 0;
    
    auto it = inflight_counter_.find(ts);
    if (it != inflight_counter_.end()) {
        counter = it->second;
    }
    
    inflight_counter_[ts] = counter + 1;
    return {ts, counter};
}

void SimulatedExchange::updateAccount(const Fill& fill) {
    if (!account_ || frozen_account_) {
        return;
    }
    
    std::cout << "Updating account for fill: " << fill.toString() << std::endl;
    
    // 计算手续费
    double commission = fill.commission().as_f64();
    std::cout << "Commission: " << commission << std::endl;
    
    // 获取基础货币和报价货币
    std::string instrument_str = debugInstrumentId(fill.instrumentId());
    std::cout << "Instrument: " << instrument_str << std::endl;
    
    size_t slash_pos = instrument_str.find('/');
    
    if (slash_pos == std::string::npos) {
        // 单资产交易
        std::cout << "Processing single asset trade" << std::endl;
        
        // 使用交易品种ID作为资产的货币代码
        std::string asset_currency = std::to_string(fill.instrumentId());
        std::cout << "Asset currency: " << asset_currency << std::endl;
        
        if (fill.side() == OrderSide::BUY) {
            // 买入：增加资产，减少基础货币
            double asset_quantity = fill.quantity();
            double currency_amount = fill.price() * fill.quantity() + commission;
            
            std::cout << "Buy order: " << std::endl;
            std::cout << "  Asset quantity: " << asset_quantity << std::endl;
            std::cout << "  Currency amount: " << currency_amount << std::endl;
            
            // 先检查余额是否足够
            auto* currency_balance = account_->getBalance(base_currency_);
            if (!currency_balance || currency_balance->free() < currency_amount) {
                std::stringstream ss;
                ss << "Insufficient balance for buy order: required=" << currency_amount 
                   << ", available=" << (currency_balance ? currency_balance->free() : 0);
                throw std::runtime_error(ss.str());
            }
            
            // 更新余额
            account_->updateBalance(base_currency_, -currency_amount, 0.0);
            account_->updateBalance(asset_currency, asset_quantity, 0.0);
            
            std::cout << "Updated balances after buy:" << std::endl;
            std::cout << "  " << base_currency_ << ": " << currency_balance->free() << std::endl;
            std::cout << "  " << asset_currency << ": " << account_->getBalance(asset_currency)->free() << std::endl;
        } else {
            // 卖出：减少资产，增加基础货币
            double asset_quantity = fill.quantity();
            double currency_amount = fill.price() * fill.quantity() - commission;
            
            std::cout << "Sell order: " << std::endl;
            std::cout << "  Asset quantity: " << asset_quantity << std::endl;
            std::cout << "  Currency amount: " << currency_amount << std::endl;
            
            // 先检查资产余额是否足够
            auto* asset_balance = account_->getBalance(asset_currency);
            if (!asset_balance || asset_balance->free() < asset_quantity) {
                std::stringstream ss;
                ss << "Insufficient balance for sell order: required=" << asset_quantity 
                   << ", available=" << (asset_balance ? asset_balance->free() : 0);
                throw std::runtime_error(ss.str());
            }
            
            // 更新余额
            account_->updateBalance(asset_currency, -asset_quantity, 0.0);
            account_->updateBalance(base_currency_, currency_amount, 0.0);
            
            std::cout << "Updated balances after sell:" << std::endl;
            std::cout << "  " << base_currency_ << ": " << account_->getBalance(base_currency_)->free() << std::endl;
            std::cout << "  " << asset_currency << ": " << asset_balance->free() << std::endl;
        }
    } else {
        // 交易对交易
        std::string base_currency = instrument_str.substr(0, slash_pos);
        std::string quote_currency = instrument_str.substr(slash_pos + 1);
        
        if (fill.side() == OrderSide::BUY) {
            // 买入：增加基础货币，减少报价货币
            double base_amount = fill.quantity();
            double quote_amount = fill.price() * fill.quantity() + commission;
            
            // 先检查报价货币余额是否足够
            auto* quote_balance = account_->getBalance(quote_currency);
            if (!quote_balance || quote_balance->free() < quote_amount) {
                std::stringstream ss;
                ss << "Insufficient balance for buy order: required=" << quote_amount 
                   << ", available=" << (quote_balance ? quote_balance->free() : 0);
                throw std::runtime_error(ss.str());
            }
            
            account_->updateBalance(base_currency, base_amount, 0.0);
            account_->updateBalance(quote_currency, -quote_amount, 0.0);
        } else {
            // 卖出：减少基础货币，增加报价货币
            double base_amount = fill.quantity();
            double quote_amount = fill.price() * fill.quantity() - commission;
            
            // 先检查基础货币余额是否足够
            auto* base_balance = account_->getBalance(base_currency);
            if (!base_balance || base_balance->free() < base_amount) {
                std::stringstream ss;
                ss << "Insufficient balance for sell order: required=" << base_amount 
                   << ", available=" << (base_balance ? base_balance->free() : 0);
                throw std::runtime_error(ss.str());
            }
            
            account_->updateBalance(base_currency, -base_amount, 0.0);
            account_->updateBalance(quote_currency, quote_amount, 0.0);
        }
    }

    // 如果设置了执行客户端，通知账户状态更新
    if (execution_client_) {
        std::vector<AccountBalance> balances;
        for (const auto& balance : account_->balances()) {
            balances.push_back(balance);
        }
        
        // 通知执行客户端账户状态已更新
        execution_client_->generateAccountState(balances, true, fill.timestamp());
    }
}

// OrderMatchingEngine 类实现
OrderMatchingEngine::OrderMatchingEngine(InstrumentId instrument_id, OrderBook* order_book)
    : instrument_id_(instrument_id)
    , order_book_(order_book)
    , order_counter_(0) {
}

void OrderMatchingEngine::processQuoteTick(const QuoteTick& quote) {
    // 更新订单簿
    if (order_book_) {
        // 创建两个增量更新
        OrderBookDelta bid_delta(
            instrument_id_,
            OrderBookAction::UPDATE,
            true,
            quote.bidPrice(),
            quote.bidSize(),
            quote.tsEvent(),
            quote.tsInit()
        );
        
        OrderBookDelta ask_delta(
            instrument_id_,
            OrderBookAction::UPDATE,
            false,
            quote.askPrice(),
            quote.askSize(),
            quote.tsEvent(),
            quote.tsInit()
        );
        
        order_book_->applyDelta(bid_delta);
        order_book_->applyDelta(ask_delta);
    }
    
    // 检查止损单和止损限价单是否被触发
    for (auto it = open_orders_.begin(); it != open_orders_.end();) {
        auto order = it->second;
        bool triggered = false;
        
        if (order->type() == OrderType::STOP || order->type() == OrderType::STOP_LIMIT) {
            Price trigger_price = order->price();
            
            // 检查是否触发
            if (isPriceCrossTrigger(order->side(), quote.bidPrice(), trigger_price) ||
                isPriceCrossTrigger(order->side(), quote.askPrice(), trigger_price)) {
                triggered = true;
            }
        }
        
        if (triggered) {
            // 处理触发的订单
            std::vector<Fill> fills = matchOrder(order, quote.tsEvent());
            
            // 从开放订单列表中移除已经完全成交的订单
            if (order->isFilled()) {
                UUID order_id = order->clientOrderId();
                removeFromOpenOrders(order_id);
                it = open_orders_.begin(); // 重新开始遍历，因为可能已经修改了容器
            } else {
                ++it;
            }
        } else {
            ++it;
        }
    }
}

void OrderMatchingEngine::processTradeTick(const TradeTick& trade) {
    // 对于每个交易行情，检查是否有触发的止损单或限价单
    Price trade_price = trade.price();
    
    for (auto it = open_orders_.begin(); it != open_orders_.end();) {
        auto order = it->second;
        bool triggered = false;
        bool matched = false;
        
        // 检查止损单是否触发
        if (order->type() == OrderType::STOP || order->type() == OrderType::STOP_LIMIT) {
            if (isPriceCrossTrigger(order->side(), trade_price, order->price())) {
                triggered = true;
            }
        }
        
        // 检查限价单是否可以成交
        if (order->type() == OrderType::LIMIT || 
            (order->type() == OrderType::STOP_LIMIT && triggered)) {
            if ((order->side() == OrderSide::BUY && trade_price <= order->price()) ||
                (order->side() == OrderSide::SELL && trade_price >= order->price())) {
                matched = true;
            }
        }
        
        // 处理成交
        if (matched || triggered && order->type() == OrderType::STOP) {
            std::vector<Fill> fills = matchOrder(order, trade.tsEvent());
            
            // 从开放订单列表中移除已经完全成交的订单
            if (order->isFilled()) {
                UUID order_id = order->clientOrderId();
                removeFromOpenOrders(order_id);
                it = open_orders_.begin(); // 重新开始遍历，因为可能已经修改了容器
            } else {
                ++it;
            }
        } else {
            ++it;
        }
    }
}

void OrderMatchingEngine::processBar(const Bar& bar) {
    // 处理K线数据，可以按照以下顺序处理价格：开盘价、最高价、最低价、收盘价
    processBarPrice(bar.barType().instrument_id, bar.open(), bar.tsEvent());
    processBarPrice(bar.barType().instrument_id, bar.high(), bar.tsEvent());
    processBarPrice(bar.barType().instrument_id, bar.low(), bar.tsEvent());
    processBarPrice(bar.barType().instrument_id, bar.close(), bar.tsEvent());
}

void OrderMatchingEngine::processBarPrice(InstrumentId instrument_id, Price price, UnixNanos timestamp) {
    for (auto it = open_orders_.begin(); it != open_orders_.end();) {
        auto order = it->second;
        bool triggered = false;
        bool matched = false;
        
        // 检查止损单是否触发
        if (order->type() == OrderType::STOP || order->type() == OrderType::STOP_LIMIT) {
            if (isPriceCrossTrigger(order->side(), price, order->price())) {
                triggered = true;
            }
        }
        
        // 检查限价单是否可以成交
        if (order->type() == OrderType::LIMIT || 
            (order->type() == OrderType::STOP_LIMIT && triggered)) {
            if ((order->side() == OrderSide::BUY && price <= order->price()) ||
                (order->side() == OrderSide::SELL && price >= order->price())) {
                matched = true;
            }
        }
        
        // 处理成交
        if (matched || triggered && order->type() == OrderType::STOP) {
            std::vector<Fill> fills = matchOrder(order, timestamp);
            
            // 从开放订单列表中移除已经完全成交的订单
            if (order->isFilled()) {
                UUID order_id = order->clientOrderId();
                removeFromOpenOrders(order_id);
                it = open_orders_.begin(); // 重新开始遍历，因为可能已经修改了容器
            } else {
                ++it;
            }
        } else {
            ++it;
        }
    }
}

void OrderMatchingEngine::processOrderBookDelta(const OrderBookDelta& delta) {
    if (order_book_) {
        order_book_->applyDelta(delta);
    }
    
    Price price = delta.price();
    bool is_bid = delta.isBid();
    
    for (auto it = open_orders_.begin(); it != open_orders_.end();) {
        auto order = it->second;
        bool matched = false;
        
        // 买单匹配卖单，卖单匹配买单
        if ((order->side() == OrderSide::BUY && !is_bid) || 
            (order->side() == OrderSide::SELL && is_bid)) {
            
            if (order->type() == OrderType::LIMIT) {
                if ((order->side() == OrderSide::BUY && price <= order->price()) ||
                    (order->side() == OrderSide::SELL && price >= order->price())) {
                    matched = true;
                }
            }
        }
        
        if (matched) {
            std::vector<Fill> fills = matchOrder(order, delta.tsEvent());
            
            // 从开放订单列表中移除已经完全成交的订单
            if (order->isFilled()) {
                UUID order_id = order->clientOrderId();
                removeFromOpenOrders(order_id);
                it = open_orders_.begin(); // 重新开始遍历，因为可能已经修改了容器
            } else {
                ++it;
            }
        } else {
            ++it;
        }
    }
}

void OrderMatchingEngine::processOrderBookDeltas(const OrderBookDeltas& deltas) {
    const auto& delta_vector = deltas.deltas();
    for (const auto& delta : delta_vector) {
        processOrderBookDelta(delta);
    }
}

std::vector<Fill> OrderMatchingEngine::submitOrder(std::shared_ptr<Order> order, UnixNanos timestamp) {
    std::vector<Fill> fills;
    
    order->setStatus(OrderStatus::ACCEPTED);
    
    // 先添加到开放订单列表，包括市价单
    addToOpenOrders(order);
    
    // 市价单和限价单都尝试立即成交
    if (order->type() == OrderType::MARKET) {
        fills = matchOrder(order, timestamp);
    } else if (order->type() == OrderType::LIMIT) {
        Price best_price = 0;
        if (order->side() == OrderSide::BUY) {
            best_price = order_book_->bestAskPrice();
            if (best_price > 0 && best_price <= order->price()) {
                fills = matchOrder(order, timestamp);
            }
        } else {
            best_price = order_book_->bestBidPrice();
            if (best_price > 0 && best_price >= order->price()) {
                fills = matchOrder(order, timestamp);
            }
        }
    }
    
    // 如果订单已完全成交，从开放订单列表中移除
    if (order->isFilled()) {
        removeFromOpenOrders(order->clientOrderId());
    }
    
    return fills;
}

bool OrderMatchingEngine::cancelOrder(const UUID& order_id) {
    auto it = open_orders_.find(order_id);
    if (it != open_orders_.end()) {
        auto order = it->second;
        order->setStatus(OrderStatus::CANCELED);
        removeFromOpenOrders(order_id);
        return true;
    }
    return false;
}

std::vector<std::shared_ptr<Order>> OrderMatchingEngine::getOpenOrders() const {
    std::vector<std::shared_ptr<Order>> result;
    result.reserve(open_orders_.size());
    
    for (const auto& pair : open_orders_) {
        result.push_back(pair.second);
    }
    
    return result;
}

std::vector<std::shared_ptr<Order>> OrderMatchingEngine::getOpenBidOrders() const {
    std::vector<std::shared_ptr<Order>> result;
    
    for (const auto& pair : open_orders_) {
        if (pair.second->side() == OrderSide::BUY) {
            result.push_back(pair.second);
        }
    }
    
    return result;
}

std::vector<std::shared_ptr<Order>> OrderMatchingEngine::getOpenAskOrders() const {
    std::vector<std::shared_ptr<Order>> result;
    
    for (const auto& pair : open_orders_) {
        if (pair.second->side() == OrderSide::SELL) {
            result.push_back(pair.second);
        }
    }
    
    return result;
}

void OrderMatchingEngine::clear() {
    open_orders_.clear();
    bid_price_index_.clear();
    ask_price_index_.clear();
}

std::vector<Fill> OrderMatchingEngine::matchOrder(std::shared_ptr<Order> order, UnixNanos timestamp) {
    std::vector<Fill> fills;
    
    if (order->remainingQuantity() <= 0) {
        return fills;
    }
    
    if (order->status() == OrderStatus::CANCELED || 
        order->status() == OrderStatus::REJECTED || 
        order->status() == OrderStatus::EXPIRED) {
        return fills;
    }
    
    Price match_price = 0;
    Quantity match_quantity = 0;
    
    if (order->side() == OrderSide::BUY) {
        // 对于买单，尝试以最优卖价成交
        match_price = order_book_->bestAskPrice();
        if (match_price <= 0 || match_price == 100000.0) { // 检查是否为默认的大数值
            // 如果没有卖单，可以视为找不到对手方
            if (order->type() == OrderType::MARKET) {
                // 对于市价单，强制以订单中的价格成交
                match_price = order->price();
                match_quantity = order->remainingQuantity();
            } else {
                return fills; // 限价单无法成交
            }
        } else {
            if (order->type() == OrderType::LIMIT && match_price > order->price()) {
                return fills; // 限价单无法以高于限价的价格成交
            }
            
            match_quantity = std::min(order->remainingQuantity(), order_book_->askSize(match_price));
            // 确保市价单可以完全成交
            if (order->type() == OrderType::MARKET && match_quantity < order->remainingQuantity()) {
                match_quantity = order->remainingQuantity();
            }
        }
    } else {
        // 对于卖单，尝试以最优买价成交
        match_price = order_book_->bestBidPrice();
        if (match_price <= 0) {
            // 如果没有买单，可以视为找不到对手方
            if (order->type() == OrderType::MARKET) {
                // 对于市价单，强制以订单中的价格成交
                match_price = order->price();
                match_quantity = order->remainingQuantity();
            } else {
                return fills; // 限价单无法成交
            }
        } else {
            if (order->type() == OrderType::LIMIT && match_price < order->price()) {
                return fills; // 限价单无法以低于限价的价格成交
            }
            
            match_quantity = std::min(order->remainingQuantity(), order_book_->bidSize(match_price));
            // 确保市价单可以完全成交
            if (order->type() == OrderType::MARKET && match_quantity < order->remainingQuantity()) {
                match_quantity = order->remainingQuantity();
            }
        }
    }
    
    if (match_quantity > 0) {
        // 创建成交记录
        Fill fill = createFill(order->clientOrderId(), match_price, match_quantity, timestamp);
        fills.push_back(fill);
        
        // 更新订单的已成交数量
        order->setFilledQuantity(order->filledQuantity() + match_quantity);
        
        // 更新订单状态
        if (order->isFilled()) {
            order->setStatus(OrderStatus::FILLED);
        } else {
            order->setStatus(OrderStatus::PARTIALLY_FILLED);
        }
        
        // 更新订单簿
        if (order_book_) {
            OrderBookAction action = OrderBookAction::UPDATE;
            Quantity remaining_size = 0;
            
            if (order->side() == OrderSide::BUY) {
                remaining_size = order_book_->askSize(match_price);
                if (match_quantity >= remaining_size) {
                    action = OrderBookAction::DELETE;
                    remaining_size = 0;
                } else {
                    remaining_size -= match_quantity;
                }
                
                OrderBookDelta delta(
                    instrument_id_,
                    action,
                    false, // 更新卖盘
                    match_price,
                    remaining_size,
                    timestamp,
                    timestamp
                );
                
                order_book_->applyDelta(delta);
            } else {
                remaining_size = order_book_->bidSize(match_price);
                if (match_quantity >= remaining_size) {
                    action = OrderBookAction::DELETE;
                    remaining_size = 0;
                } else {
                    remaining_size -= match_quantity;
                }
                
                OrderBookDelta delta(
                    instrument_id_,
                    action,
                    true, // 更新买盘
                    match_price,
                    remaining_size,
                    timestamp,
                    timestamp
                );
                
                order_book_->applyDelta(delta);
            }
        }
        
        // 如果订单未完全成交，尝试继续匹配
        if (!order->isFilled()) {
            auto more_fills = matchOrder(order, timestamp);
            fills.insert(fills.end(), more_fills.begin(), more_fills.end());
        }
    }
    
    return fills;
}

Fill OrderMatchingEngine::createFill(const UUID& order_id, Price price, Quantity quantity, UnixNanos timestamp) {
    auto it = open_orders_.find(order_id);
    if (it == open_orders_.end()) {
        throw std::runtime_error("Order not found");
    }
    
    auto order = it->second;
    
    // 计算手续费（这里使用0.1%作为示例）
    double commission_rate = 0.001;
    double commission_amount = static_cast<double>(price) * static_cast<double>(quantity) * commission_rate;
    Money commission("USDT", commission_amount);
    
    Fill fill(
        generateUUID(),
        order_id,
        instrument_id_,
        order->side(),
        price,
        quantity,
        timestamp
    );
    
    // 设置手续费
    fill.setCommission(commission);
    
    return fill;
}

void OrderMatchingEngine::addToOpenOrders(std::shared_ptr<Order> order) {
    UUID order_id = order->clientOrderId();
    Price price = order->price();
    
    // 添加到开放订单列表
    open_orders_[order_id] = order;
    
    // 添加到价格索引
    if (order->side() == OrderSide::BUY) {
        bid_price_index_[price].push_back(order_id);
    } else {
        ask_price_index_[price].push_back(order_id);
    }
}

void OrderMatchingEngine::removeFromOpenOrders(const UUID& order_id) {
    auto it = open_orders_.find(order_id);
    if (it != open_orders_.end()) {
        auto order = it->second;
        Price price = order->price();
        
        // 从价格索引中移除
        if (order->side() == OrderSide::BUY) {
            auto& orders = bid_price_index_[price];
            orders.erase(std::remove(orders.begin(), orders.end(), order_id), orders.end());
            if (orders.empty()) {
                bid_price_index_.erase(price);
            }
        } else {
            auto& orders = ask_price_index_[price];
            orders.erase(std::remove(orders.begin(), orders.end(), order_id), orders.end());
            if (orders.empty()) {
                ask_price_index_.erase(price);
            }
        }
        
        // 从开放订单列表中移除
        open_orders_.erase(order_id);
    }
}

bool OrderMatchingEngine::isPriceCrossTrigger(OrderSide side, Price price, Price trigger_price) const {
    if (side == OrderSide::BUY) {
        // 买单触发：价格上升到触发价或以上
        return price >= trigger_price;
    } else {
        // 卖单触发：价格下降到触发价或以下
        return price <= trigger_price;
    }
}

// 调试辅助函数实现
inline std::string debugUUID(UUID id) {
    return "UUID:" + std::to_string(id);
}

inline std::string debugVenue(Venue id) {
    return "VENUE:" + std::to_string(id);
}

inline std::string debugInstrumentId(InstrumentId id) {
    return "INSTR:" + std::to_string(id);
}

inline std::string debugClientId(ClientId id) {
    return "CLIENT:" + std::to_string(id);
}

} // namespace backtest 