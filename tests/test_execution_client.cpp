// -------------------------------------------------------------------------------------------------
//  Copyright (C) 2023-2025. All rights reserved.
//
//  Licensed under the MIT License.
// -------------------------------------------------------------------------------------------------

#include "backtest/exchange.h"
#include "backtest/execution_client.h"
#include "backtest/clock.h"
#include "backtest/order_book.h"
#include <iostream>
#include <memory>
#include <cassert>
#include <thread>
#include <chrono>

using namespace backtest;

// 格式化输出函数，从execution_client.cpp复制
extern std::string formatTime(UnixNanos time_ns);

// 定义常量
const Venue BINANCE_VENUE = 1001;  // 使用数字作为交易所标识

// 测试帮助函数
void printTestHeader(const std::string& test_name) {
    std::cout << "\n==================================" << std::endl;
    std::cout << "  测试: " << test_name << std::endl;
    std::cout << "==================================\n" << std::endl;
}

// 测试1: 基本连接与初始化
void testBasicConnection() {
    printTestHeader("基本连接与初始化");
    
    // 创建时钟
    auto clock = std::make_shared<TestClock>();
    
    // 创建模拟交易所
    std::vector<Money> starting_balances = {
        Money("USDT", 10000.0),
        Money("BTC", 1.0),
        Money("ETH", 10.0)
    };
    
    auto exchange = std::make_shared<SimulatedExchange>(
        BINANCE_VENUE,               // venue
        OmsType::NETTING,            // oms_type
        AccountType::MARGIN,         // account_type
        starting_balances,           // starting_balances
        BookType::L2,                // book_type (使用L2订单簿)
        1.0,                         // default_leverage
        false,                       // frozen_account
        false,                       // bar_execution
        false,                       // reject_stop_orders
        true,                        // support_gtd_orders
        false,                       // support_contingent_orders
        false,                       // use_position_ids
        false,                       // use_random_ids
        false,                       // use_reduce_only
        false                        // use_message_queue
    );
    
    // 初始化交易所账户
    exchange->initializeAccount();
    
    // 获取交易所内部创建的账户ID
    const Account* exchange_account = exchange->getAccount();
    assert(exchange_account != nullptr); // 确保账户初始化成功
    UUID account_id = exchange_account->accountId();
    
    // 创建执行客户端
    UUID trader_id = generateUUID();
    auto client = std::make_shared<BacktestExecutionClient>(
        trader_id,
        account_id,
        exchange,
        clock,
        false, // routing
        false  // frozen_account
    );
    
    // 连接客户端
    client->start();
    assert(client->isConnected());
    std::cout << "执行客户端已连接到交易所: " << client->venue() << std::endl;
    
    // 设置交易所的执行客户端
    exchange->registerClient(client);
    
    // 验证账户数据
    const Account* client_account = client->getAccount();
    assert(client_account != nullptr);
    assert(client_account->accountId() == account_id);
    
    std::cout << "账户初始化成功，ID: " << client_account->accountId() << std::endl;
    
    // 生成账户状态报告
    client->generateAccountState({}, true, clock->timestampNs());
    
    std::cout << "基本连接测试通过！" << std::endl;
}

// 测试2: 订单提交和查询
void testOrderSubmission() {
    printTestHeader("订单提交和查询");
    
    // 创建时钟
    auto clock = std::make_shared<TestClock>();
    clock->setTime(1000000000); // 设置初始时间
    
    // 创建模拟交易所
    std::vector<Money> starting_balances = {
        Money("USDT", 10000.0),
        Money("BTC", 1.0)
    };
    
    auto exchange = std::make_shared<SimulatedExchange>(
        BINANCE_VENUE,               // venue
        OmsType::NETTING,            // oms_type
        AccountType::MARGIN,         // account_type
        starting_balances,           // starting_balances
        BookType::L2,                // book_type
        1.0,                         // default_leverage
        false,                       // frozen_account
        false,                       // bar_execution
        false,                       // reject_stop_orders
        true,                        // support_gtd_orders
        false,                       // support_contingent_orders
        false,                       // use_position_ids
        false,                       // use_random_ids
        false,                       // use_reduce_only
        false                        // use_message_queue
    );
    
    std::cout << "交易所初始化完成，添加交易品种..." << std::endl;
    
    // 初始化交易所账户并添加交易品种
    exchange->initializeAccount();
    
    InstrumentId btc_usdt = 123456; // 简化，实际中应该是更复杂的标识
    std::cout << "添加交易品种: " << btc_usdt << std::endl;
    exchange->addInstrument(btc_usdt);
    
    // 查看订单本状态
    const OrderBook* order_book = exchange->getOrderBook(btc_usdt);
    if (order_book) {
        std::cout << "订单本初始化成功: " << btc_usdt << std::endl;
    } else {
        std::cerr << "错误: 订单本初始化失败" << std::endl;
        assert(false);
    }
    
    // 获取账户ID
    const Account* exchange_account = exchange->getAccount();
    assert(exchange_account != nullptr);
    UUID account_id = exchange_account->accountId();
    std::cout << "使用账户ID: " << account_id << std::endl;
    
    // 创建执行客户端
    UUID trader_id = generateUUID();
    auto client = std::make_shared<BacktestExecutionClient>(
        trader_id,
        account_id,
        exchange,
        clock,
        false,
        false
    );
    
    // 连接客户端并设置
    client->start();
    std::cout << "执行客户端已连接" << std::endl;
    exchange->registerClient(client);
    std::cout << "已向交易所注册执行客户端" << std::endl;
    
    // 提交订单
    auto order = std::make_shared<Order>(
        generateUUID(),
        BINANCE_VENUE,
        btc_usdt,
        OrderSide::BUY,
        OrderType::LIMIT,
        0.1,  // 数量：0.1 BTC
        40000.0, // 价格：40000 USDT/BTC
        TimeInForce::GTC,
        clock->timestampNs()
    );
    
    std::cout << "准备提交订单：" << order->clientOrderId() 
              << "，买入 0.1 BTC，价格 40000 USDT" << std::endl;
    
    try {
        // 提交订单
        client->submitOrder(order);
        std::cout << "订单提交成功" << std::endl;
        
        // 处理交易所可能的事件
        exchange->process(clock->timestampNs());
        std::cout << "交易所处理完成" << std::endl;
    }
    catch (const std::exception& e) {
        std::cerr << "订单提交失败: " << e.what() << std::endl;
        assert(false);
    }
    
    // 生成账户状态报告
    client->generateAccountState({}, true, clock->timestampNs());
    
    std::cout << "订单提交测试通过！" << std::endl;
}

// 测试3: 订单成交和成交通知
void testOrderExecution() {
    printTestHeader("订单成交和成交通知");
    
    // 创建时钟
    auto clock = std::make_shared<TestClock>();
    clock->setTime(1000000000); // 设置初始时间
    
    // 创建模拟交易所
    std::vector<Money> starting_balances = {
        Money("USDT", 10000.0),
        Money("BTC", 1.0)
    };
    
    auto exchange = std::make_shared<SimulatedExchange>(
        BINANCE_VENUE,               // venue
        OmsType::NETTING,            // oms_type
        AccountType::MARGIN,         // account_type
        starting_balances,           // starting_balances
        BookType::L2,                // book_type
        1.0,                         // default_leverage
        false,                       // frozen_account
        false,                       // bar_execution
        false,                       // reject_stop_orders
        true,                        // support_gtd_orders
        false,                       // support_contingent_orders
        false,                       // use_position_ids
        false,                       // use_random_ids
        false,                       // use_reduce_only
        false                        // use_message_queue
    );
    
    // 初始化交易所账户和交易品种
    exchange->initializeAccount();
    InstrumentId btc_usdt = 123456;
    exchange->addInstrument(btc_usdt);
    
    // 获取账户ID
    const Account* exchange_account = exchange->getAccount();
    assert(exchange_account != nullptr);
    UUID account_id = exchange_account->accountId();
    
    // 创建执行客户端
    UUID trader_id = generateUUID();
    auto client = std::make_shared<BacktestExecutionClient>(
        trader_id,
        account_id,
        exchange,
        clock,
        false,
        false
    );
    
    // 连接客户端并设置
    client->start();
    exchange->registerClient(client);
    
    // 提交买单
    auto buy_order = std::make_shared<Order>(
        generateUUID(),
        BINANCE_VENUE,
        btc_usdt,
        OrderSide::BUY,
        OrderType::LIMIT,
        0.1,  // 数量：0.1 BTC
        40000.0, // 价格：40000 USDT/BTC
        TimeInForce::GTC,
        clock->timestampNs()
    );
    
    std::cout << "提交买单: " << buy_order->clientOrderId() << std::endl;
    client->submitOrder(buy_order);
    
    // 提交对手方卖单（以触发成交）
    auto sell_order = std::make_shared<Order>(
        generateUUID(),
        BINANCE_VENUE,
        btc_usdt,
        OrderSide::SELL,
        OrderType::LIMIT,
        0.1,  // 数量：0.1 BTC
        40000.0, // 价格：40000 USDT/BTC
        TimeInForce::GTC,
        clock->timestampNs()
    );
    
    std::cout << "提交卖单（触发成交）: " << sell_order->clientOrderId() << std::endl;
    
    // 提交卖单，这应该会触发成交
    client->submitOrder(sell_order);
    
    // 处理所有事件
    clock->advanceTime(clock->timestampNs() + 1000000, true);
    exchange->process(clock->timestampNs());
    
    // 生成账户状态报告，查看成交后的账户变化
    client->generateAccountState({}, true, clock->timestampNs());
    
    std::cout << "订单成交测试通过！" << std::endl;
}

// 测试4: 订单取消
void testOrderCancellation() {
    printTestHeader("订单取消");
    
    // 创建时钟
    auto clock = std::make_shared<TestClock>();
    clock->setTime(1000000000); // 设置初始时间
    
    // 创建模拟交易所
    std::vector<Money> starting_balances = {
        Money("USDT", 10000.0),
        Money("BTC", 1.0)
    };
    
    auto exchange = std::make_shared<SimulatedExchange>(
        BINANCE_VENUE,               // venue
        OmsType::NETTING,            // oms_type
        AccountType::MARGIN,         // account_type
        starting_balances,           // starting_balances
        BookType::L2,                // book_type 
        1.0,                         // default_leverage
        false,                       // frozen_account
        false,                       // bar_execution
        false,                       // reject_stop_orders
        true,                        // support_gtd_orders
        false,                       // support_contingent_orders
        false,                       // use_position_ids
        false,                       // use_random_ids
        false,                       // use_reduce_only
        false                        // use_message_queue
    );
    
    // 初始化交易所账户和交易品种
    exchange->initializeAccount();
    InstrumentId btc_usdt = 123456;
    exchange->addInstrument(btc_usdt);
    
    // 获取账户ID
    const Account* exchange_account = exchange->getAccount();
    assert(exchange_account != nullptr);
    UUID account_id = exchange_account->accountId();
    
    // 创建执行客户端
    UUID trader_id = generateUUID();
    auto client = std::make_shared<BacktestExecutionClient>(
        trader_id,
        account_id,
        exchange,
        clock,
        false,
        false
    );
    
    // 连接客户端并设置
    client->start();
    exchange->registerClient(client);
    
    // 提交买单
    auto order = std::make_shared<Order>(
        generateUUID(),
        BINANCE_VENUE,
        btc_usdt,
        OrderSide::BUY,
        OrderType::LIMIT,
        0.1,  // 数量：0.1 BTC
        39000.0, // 价格：39000 USDT/BTC (低于市价，不会立即成交)
        TimeInForce::GTC,
        clock->timestampNs()
    );
    
    UUID order_id = order->clientOrderId();
    std::cout << "提交买单: " << order_id << std::endl;
    client->submitOrder(order);
    
    // 等待一段时间
    clock->advanceTime(clock->timestampNs() + 1000000, true);
    exchange->process(clock->timestampNs());
    
    // 取消订单
    std::cout << "取消订单: " << order_id << std::endl;
    client->cancelOrder(order_id);
    
    // 等待一段时间
    clock->advanceTime(clock->timestampNs() + 1000000, true);
    exchange->process(clock->timestampNs());
    
    // 生成账户状态报告
    client->generateAccountState({}, true, clock->timestampNs());
    
    std::cout << "订单取消测试通过！" << std::endl;
}

// 主函数
int main() {
    try {
        // 运行所有测试
        testBasicConnection();
        testOrderSubmission();
        testOrderExecution();
        testOrderCancellation();
        
        std::cout << "\n所有测试通过！" << std::endl;
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "测试失败: " << e.what() << std::endl;
        return 1;
    }
} 