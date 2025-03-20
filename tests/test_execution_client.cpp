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

extern std::string formatTime(UnixNanos time_ns);

const Venue BINANCE_VENUE = 1001;  // 交易所标识

void printTestHeader(const std::string& test_name) {
    std::cout << "\n==================================" << std::endl;
    std::cout << "  测试: " << test_name << std::endl;
    std::cout << "==================================\n" << std::endl;
}

void testBasicConnection() {
    printTestHeader("BASIC");
    
    auto clock = std::make_shared<TestClock>();
    
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
    
    exchange->initializeAccount();
    
    const Account* exchange_account = exchange->getAccount();
    assert(exchange_account != nullptr); // 确保账户初始化成功
    UUID account_id = exchange_account->accountId();
    
    UUID trader_id = generateUUID();
    auto client = std::make_shared<BacktestExecutionClient>(
        trader_id,
        account_id,
        exchange,
        clock,
        false, // routing
        false  // frozen_account
    );
    
    client->start();
    assert(client->isConnected());
    std::cout << "Execution client connected to exchange: " << client->venue() << std::endl;
    
    exchange->registerClient(client);
    
    // Verify account data
    const Account* client_account = client->getAccount();
    assert(client_account != nullptr);
    assert(client_account->accountId() == account_id);
    
    std::cout << "Account initialized successfully, ID: " << client_account->accountId() << std::endl;
    
    // Generate account state report
    client->generateAccountState({}, true, clock->timestampNs());
    
    std::cout << "Basic connection test passed!" << std::endl;
}

// Test for order submission and query
void testOrderSubmission() {
    printTestHeader("Order submission and query");
    
    auto clock = std::make_shared<TestClock>();
    clock->setTime(1000000000); 
    
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
    
    std::cout << "Exchange initialized, adding trading instrument..." << std::endl;
    
    exchange->initializeAccount();
    
    InstrumentId btc_usdt = 123456;
    std::cout << "Adding trading instrument: " << btc_usdt << std::endl;
    exchange->addInstrument(btc_usdt);
    
    // Check order book status
    const OrderBook* order_book = exchange->getOrderBook(btc_usdt);
    if (order_book) {
        std::cout << "Order book initialized successfully: " << btc_usdt << std::endl;
    } else {
        std::cerr << "Error: Order book initialization failed" << std::endl;
        assert(false);
    }
    
    const Account* exchange_account = exchange->getAccount();
    assert(exchange_account != nullptr);
    UUID account_id = exchange_account->accountId();
    std::cout << "USE ACCOUNT ID: " << account_id << std::endl;
    
    UUID trader_id = generateUUID();
    auto client = std::make_shared<BacktestExecutionClient>(
        trader_id,
        account_id,
        exchange,
        clock,
        false,
        false
    );
    
    client->start();
    std::cout << "Execution client connected" << std::endl;
    exchange->registerClient(client);
    std::cout << "Registered execution client to exchange" << std::endl;
    
    // Order submission
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
        client->submitOrder(order);
        std::cout << "Order submitted successfully" << std::endl;
        
        exchange->process(clock->timestampNs());
        std::cout << "Exchange processed" << std::endl;
    }
    catch (const std::exception& e) {
        std::cerr << "Order submission failed: " << e.what() << std::endl;
        assert(false);
    }
    
    client->generateAccountState({}, true, clock->timestampNs());
    
    std::cout << "Order submission test passed!" << std::endl;
}

// Test for 订单成交和成交通知
void testOrderExecution() {
    printTestHeader("Order execution and notification");
    
    auto clock = std::make_shared<TestClock>();
    clock->setTime(1000000000); // 设置初始时间
    
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
    
    exchange->initializeAccount();
    InstrumentId btc_usdt = 123456;
    exchange->addInstrument(btc_usdt);
    
    const Account* exchange_account = exchange->getAccount();
    assert(exchange_account != nullptr);
    UUID account_id = exchange_account->accountId();
    
    UUID trader_id = generateUUID();
    auto client = std::make_shared<BacktestExecutionClient>(
        trader_id,
        account_id,
        exchange,
        clock,
        false,
        false
    );
    
    client->start();
    exchange->registerClient(client);
    
    // submit buy order
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
    
    std::cout << "Submit buy order: " << buy_order->clientOrderId() << std::endl;
    client->submitOrder(buy_order);
    
    // submit sell order
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
    
    std::cout << "Submit sell order (trigger execution): " << sell_order->clientOrderId() << std::endl;
    
    client->submitOrder(sell_order);
    
    clock->advanceTime(clock->timestampNs() + 1000000, true);
    exchange->process(clock->timestampNs());
    
    // 生成账户状态报告，查看成交后的账户变化
    client->generateAccountState({}, true, clock->timestampNs());
    
    std::cout << "Order execution test passed!" << std::endl;
}

// 测试4: 订单取消
void testOrderCancellation() {
    printTestHeader("Order cancellation");
    
    auto clock = std::make_shared<TestClock>();
    clock->setTime(1000000000);
    
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
    
    exchange->initializeAccount();
    InstrumentId btc_usdt = 123456;
    exchange->addInstrument(btc_usdt);
    
    const Account* exchange_account = exchange->getAccount();
    assert(exchange_account != nullptr);
    UUID account_id = exchange_account->accountId();
    
    UUID trader_id = generateUUID();
    auto client = std::make_shared<BacktestExecutionClient>(
        trader_id,
        account_id,
        exchange,
        clock,
        false,
        false
    );
    
    client->start();
    exchange->registerClient(client);
    
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
    std::cout << "Submit buy order: " << order_id << std::endl;
    client->submitOrder(order);
    
    // 等待一段时间
    clock->advanceTime(clock->timestampNs() + 1000000, true);
    exchange->process(clock->timestampNs());
    
    std::cout << "Cancel order: " << order_id << std::endl;
    client->cancelOrder(order_id);
    
    clock->advanceTime(clock->timestampNs() + 1000000, true);
    exchange->process(clock->timestampNs());
    
    // 生成账户状态报告
    client->generateAccountState({}, true, clock->timestampNs());
    
    std::cout << "订单取消测试通过！" << std::endl;
}

int main() {
    try {
        testBasicConnection();
        testOrderSubmission();
        testOrderExecution();
        testOrderCancellation();
        
        std::cout << "\nAll tests passed!" << std::endl;
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Test failed: " << e.what() << std::endl;
        return 1;
    }
} 