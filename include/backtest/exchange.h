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
#include <queue>
#include <memory>
#include <functional>
#include <unordered_map>
#include <deque>

#include "backtest/data.h"
#include "backtest/order_book.h"
#include "backtest/concurrency.h"

namespace backtest {

// 前向声明
class Clock;
class TestClock;
class ExecutionClient;
class OrderMatchingEngine;
class SimulationModule;

/**
 * @brief 账户类型枚举
 */
enum class AccountType {
    CASH,       // 现金账户
    MARGIN      // 保证金账户
};

/**
 * @brief 订单管理系统类型
 */
enum class OmsType {
    NETTING,    // 净额交易，同方向订单合并
    HEDGING     // 对冲交易，每个订单单独计算
};

/**
 * @brief 货币类型
 */
using Currency = std::string;

/**
 * @brief 金额类型
 */
struct Money {
    Currency currency;
    double amount;

    Money(Currency curr, double amt) : currency(std::move(curr)), amount(amt) {}
    Money() : currency(""), amount(0.0) {}

    // 获取金额值
    double as_f64() const { return amount; }

    // 加法
    Money operator+(const Money& other) const {
        if (currency != other.currency) {
            throw std::runtime_error("Cannot add money with different currencies");
        }
        return Money(currency, amount + other.amount);
    }

    // 减法
    Money operator-(const Money& other) const {
        if (currency != other.currency) {
            throw std::runtime_error("Cannot subtract money with different currencies");
        }
        return Money(currency, amount - other.amount);
    }

    // 乘法
    Money operator*(double factor) const {
        return Money(currency, amount * factor);
    }

    // 等于
    bool operator==(const Money& other) const {
        return currency == other.currency && amount == other.amount;
    }

    // 不等于
    bool operator!=(const Money& other) const {
        return !(*this == other);
    }

    // 小于
    bool operator<(const Money& other) const {
        if (currency != other.currency) {
            throw std::runtime_error("Cannot compare money with different currencies");
        }
        return amount < other.amount;
    }

    // 字符串表示
    std::string toString() const {
        return currency + " " + std::to_string(amount);
    }
};

/**
 * @brief 订单方向
 */
enum class OrderSide {
    BUY,
    SELL
};

/**
 * @brief 订单类型
 */
enum class OrderType {
    MARKET,     // 市价单
    LIMIT,      // 限价单
    STOP,       // 止损单
    STOP_LIMIT, // 止损限价单
    TRAILING_STOP, // 追踪止损单
    OCO         // One-Cancels-Other
};

/**
 * @brief 订单有效期类型
 */
enum class TimeInForce {
    GTC,        // Good Till Canceled
    IOC,        // Immediate or Cancel
    FOK,        // Fill or Kill
    GTD,        // Good Till Date
    DAY         // Day Order
};

/**
 * @brief 订单状态
 */
enum class OrderStatus {
    INITIALIZED,
    SUBMITTED,
    ACCEPTED,
    REJECTED,
    CANCELED,
    EXPIRED,
    FILLED,
    PARTIALLY_FILLED
};

/**
 * @brief 订单实体
 */
class Order {
public:
    Order(
        UUID client_order_id,
        Venue venue,
        InstrumentId instrument_id,
        OrderSide side,
        OrderType type,
        Quantity quantity,
        Price price,
        TimeInForce time_in_force,
        UnixNanos timestamp);

    UUID clientOrderId() const { return client_order_id_; }
    Venue venue() const { return venue_; }
    InstrumentId instrumentId() const { return instrument_id_; }
    OrderSide side() const { return side_; }
    OrderType type() const { return type_; }
    Quantity quantity() const { return quantity_; }
    Price price() const { return price_; }
    TimeInForce timeInForce() const { return time_in_force_; }
    UnixNanos timestamp() const { return timestamp_; }
    OrderStatus status() const { return status_; }
    
    void setStatus(OrderStatus status) { status_ = status; }
    void setFilledQuantity(Quantity filled) { filled_quantity_ = filled; }
    Quantity filledQuantity() const { return filled_quantity_; }
    Quantity remainingQuantity() const { return quantity_ - filled_quantity_; }
    
    bool isFilled() const { return filled_quantity_ >= quantity_; }
    bool isPartiallyFilled() const { return filled_quantity_ > 0 && filled_quantity_ < quantity_; }
    
    std::string toString() const;

private:
    UUID client_order_id_;
    Venue venue_;
    InstrumentId instrument_id_;
    OrderSide side_;
    OrderType type_;
    Quantity quantity_;
    Quantity filled_quantity_;
    Price price_;
    TimeInForce time_in_force_;
    UnixNanos timestamp_;
    OrderStatus status_;
};

/**
 * @brief 成交记录
 */
class Fill {
public:
    Fill(
        UUID fill_id,
        UUID order_id,
        InstrumentId instrument_id,
        OrderSide side,
        Price price,
        Quantity quantity,
        UnixNanos timestamp);
    
    UUID fillId() const { return fill_id_; }
    UUID orderId() const { return order_id_; }
    InstrumentId instrumentId() const { return instrument_id_; }
    OrderSide side() const { return side_; }
    Price price() const { return price_; }
    Quantity quantity() const { return quantity_; }
    UnixNanos timestamp() const { return timestamp_; }
    Money commission() const { return commission_; }
    void setCommission(const Money& commission) { commission_ = commission; }
    
    std::string toString() const;

private:
    UUID fill_id_;
    UUID order_id_;
    InstrumentId instrument_id_;
    OrderSide side_;
    Price price_;
    Quantity quantity_;
    UnixNanos timestamp_;
    Money commission_;  // 手续费
};

/**
 * @brief 账户余额
 */
class AccountBalance {
public:
    AccountBalance(Currency currency, double free, double locked);
    
    Currency currency() const { return currency_; }
    double free() const { return free_; }
    double locked() const { return locked_; }
    double total() const { return free_ + locked_; }
    
    void addFree(double amount) {
        if (free_ + amount < 0) {
            throw std::runtime_error("Insufficient free balance");
        }
        free_ += amount;
    }

    void addLocked(double amount) {
        if (locked_ + amount < 0) {
            throw std::runtime_error("Insufficient locked balance");
        }
        locked_ += amount;
    }
    void lockAmount(double amount);
    void unlockAmount(double amount);
    void transferLockedToFree(double amount);
    
    std::string toString() const;

private:
    Currency currency_;
    double free_;     // 可用余额
    double locked_;   // 锁定余额
};

/**
 * @brief 账户
 */
class Account {
public:
    Account(UUID account_id, Venue venue, AccountType type, std::vector<AccountBalance> balances);
    
    UUID accountId() const { return account_id_; }
    Venue venue() const { return venue_; }
    AccountType type() const { return type_; }
    const std::vector<AccountBalance>& balances() const { return balances_; }
    
    AccountBalance* getBalance(const Currency& currency);
    const AccountBalance* getBalance(const Currency& currency) const;
    
    void addBalance(const AccountBalance& balance);
    void updateBalance(const Currency& currency, double free, double locked);
    
    std::string toString() const;

private:
    UUID account_id_;
    Venue venue_;
    AccountType type_;
    std::vector<AccountBalance> balances_;
};

/**
 * @brief 交易命令
 */
class TradingCommand {
public:
    enum class Type {
        SUBMIT_ORDER,
        CANCEL_ORDER,
        MODIFY_ORDER,
        CANCEL_ALL_ORDERS
    };
    
    // 添加默认构造函数
    TradingCommand();
    TradingCommand(Type type, UUID id, std::shared_ptr<Order> order = nullptr);
    
    Type type() const { return type_; }
    UUID id() const { return id_; }
    std::shared_ptr<Order> order() const { return order_; }
    
    std::string toString() const;

private:
    Type type_;
    UUID id_;
    std::shared_ptr<Order> order_;
};

/**
 * @brief 队列中的命令
 */
struct InflightCommand {
    UnixNanos ts;
    uint32_t counter;
    TradingCommand command;
    
    InflightCommand(UnixNanos ts, uint32_t counter, TradingCommand command);
    
    // 用于优先队列排序，时间戳小的先执行，相同时间戳则counter小的先执行
    bool operator<(const InflightCommand& other) const {
        if (ts != other.ts) {
            return ts > other.ts; // 小的优先级高
        }
        return counter > other.counter; // 小的优先级高
    }
};

/**
 * @brief 模拟交易所
 */
class SimulatedExchange {
public:
    SimulatedExchange(
        Venue venue,
        OmsType oms_type,
        AccountType account_type,
        std::vector<Money> starting_balances,
        BookType book_type = BookType::L2,
        double default_leverage = 1.0,
        bool frozen_account = false,
        bool bar_execution = true,
        bool reject_stop_orders = true,
        bool support_gtd_orders = true,
        bool support_contingent_orders = true,
        bool use_position_ids = true,
        bool use_random_ids = false,
        bool use_reduce_only = true,
        bool use_message_queue = true);
    
    ~SimulatedExchange();
    
    // 注册执行客户端
    void registerClient(std::shared_ptr<ExecutionClient> client);
    
    // 添加金融工具
    void addInstrument(const InstrumentId& instrument_id);
    
    // 初始化账户
    void initializeAccount();
    
    // 获取订单簿
    const OrderBook* getOrderBook(const InstrumentId& instrument_id) const;
    
    // 获取最优买价
    Price bestBidPrice(const InstrumentId& instrument_id) const;
    
    // 获取最优卖价
    Price bestAskPrice(const InstrumentId& instrument_id) const;
    
    // 获取账户
    const Account* getAccount() const;
    
    // 调整账户余额
    void adjustAccount(const Money& adjustment);
    
    // 添加模拟模块
    void addModule(std::unique_ptr<SimulationModule> module);
    
    // 发送交易命令
    void send(const TradingCommand& command);
    
    // 处理行情tick
    void processQuoteTick(const QuoteTick& quote);
    
    // 处理交易tick
    void processTradeTick(const TradeTick& trade);
    
    // 处理bar 
    void processBar(const Bar& bar);
    
    // 处理order book增量更新
    void processOrderBookDelta(const OrderBookDelta& delta);
    
    // 处理order book更新集合
    void processOrderBookDeltas(const OrderBookDeltas& deltas);
    
    // 处理金融工具状态更新
    void processInstrumentStatus(const InstrumentStatus& status);
    
    // process 入口
    void process(UnixNanos ts_now);
    
    // 重置
    void reset();
    
    Venue venue() const { return venue_; }
    OmsType omsType() const { return oms_type_; }
    AccountType accountType() const { return account_type_; }
    BookType bookType() const { return book_type_; }

    // 提供给测试用的getter
    UnixNanos lastProcessTime() const { return last_process_time_; }
    size_t orderCount() const { return orders_.size(); }
    size_t fillCount() const { return fills_.size(); }

private:
    // 处理交易命令
    void processTradingCommand(const TradingCommand& command);
    
    // 生成成交单
    void generateFill(std::shared_ptr<Order> order, Price price, Quantity quantity, UnixNanos timestamp);
    
    // 生成订单簿
    OrderBook* createOrderBook(const InstrumentId& instrument_id);
    
    // 生成撮合引擎
    OrderMatchingEngine* createMatchingEngine(const InstrumentId& instrument_id);

    // 生成在途命令
    std::pair<UnixNanos, uint32_t> generateInflightCommand(const TradingCommand& command);

    // 更新账户状态
    void updateAccount(const Fill& fill);

private:
    Venue venue_;
    OmsType oms_type_;
    AccountType account_type_;
    std::vector<Money> starting_balances_;
    BookType book_type_;
    double default_leverage_;
    std::shared_ptr<ExecutionClient> execution_client_;
    Currency base_currency_;
    
    // 金融工具和订单簿
    std::unordered_map<InstrumentId, std::unique_ptr<OrderBook>> order_books_;
    
    // 金融工具和撮合引擎
    std::unordered_map<InstrumentId, std::unique_ptr<OrderMatchingEngine>> matching_engines_;
    
    // 模拟模块
    std::vector<std::unique_ptr<SimulationModule>> modules_;
    
    // 时钟
    TestClock* clock_;
    
    // 消息队列（使用高性能队列实现）
    HighPerformanceQueue<TradingCommand> message_queue_;
    
    // 在途命令队列（使用优先队列，按时间戳排序）
    std::priority_queue<InflightCommand> inflight_queue_;
    
    // 在途命令计数器（按时间戳分组）
    std::unordered_map<UnixNanos, uint32_t> inflight_counter_;
    
    // 账户
    std::unique_ptr<Account> account_;
    
    // 订单
    std::unordered_map<UUID, std::shared_ptr<Order>> orders_;
    
    // 成交记录
    std::vector<Fill> fills_;
    
    // 配置选项
    bool frozen_account_;
    bool bar_execution_;
    bool reject_stop_orders_;
    bool support_gtd_orders_;
    bool support_contingent_orders_;
    bool use_position_ids_;
    bool use_random_ids_;
    bool use_reduce_only_;
    bool use_message_queue_;
    
    // 最后处理时间
    UnixNanos last_process_time_;
};

// 订单撮合引擎
class OrderMatchingEngine {
public:
    OrderMatchingEngine(InstrumentId instrument_id, OrderBook* order_book);
    
    // 处理报价行情
    void processQuoteTick(const QuoteTick& quote);
    
    // 处理交易行情
    void processTradeTick(const TradeTick& trade);
    
    // 处理K线数据
    void processBar(const Bar& bar);
    
    // 处理订单簿更新
    void processOrderBookDelta(const OrderBookDelta& delta);
    
    // 处理订单簿更新集合
    void processOrderBookDeltas(const OrderBookDeltas& deltas);
    
    // 提交订单
    std::vector<Fill> submitOrder(std::shared_ptr<Order> order, UnixNanos timestamp);
    
    // 取消订单
    bool cancelOrder(const UUID& order_id);
    
    // 获取当前未成交订单
    std::vector<std::shared_ptr<Order>> getOpenOrders() const;
    
    // 获取当前未成交买单
    std::vector<std::shared_ptr<Order>> getOpenBidOrders() const;
    
    // 获取当前未成交卖单
    std::vector<std::shared_ptr<Order>> getOpenAskOrders() const;
    
    // 清空
    void clear();
    
    // 获取订单簿
    const OrderBook* getOrderBook() const { return order_book_; }
    InstrumentId instrumentId() const { return instrument_id_; }

private:
    // 匹配订单
    std::vector<Fill> matchOrder(std::shared_ptr<Order> order, UnixNanos timestamp);
    
    // 生成成交记录
    Fill createFill(const UUID& order_id, Price price, Quantity quantity, UnixNanos timestamp);
    
    // 添加到开放订单列表
    void addToOpenOrders(std::shared_ptr<Order> order);
    
    // 从开放订单列表移除
    void removeFromOpenOrders(const UUID& order_id);
    
    // 检查价格是否穿过止损触发价格
    bool isPriceCrossTrigger(OrderSide side, Price price, Price trigger_price) const;
    
    // 处理K线价格
    void processBarPrice(InstrumentId instrument_id, Price price, UnixNanos timestamp);

private:
    InstrumentId instrument_id_;
    OrderBook* order_book_;
    
    // 开放订单
    std::unordered_map<UUID, std::shared_ptr<Order>> open_orders_;
    
    // 价格索引（价格映射到包含该价格的订单ID列表）
    std::map<Price, std::vector<UUID>, std::greater<Price>> bid_price_index_; // 买单降序
    std::map<Price, std::vector<UUID>> ask_price_index_; // 卖单升序
    
    // 订单计数器（用于生成Fill ID）
    uint64_t order_counter_;
};

} // namespace backtest
 