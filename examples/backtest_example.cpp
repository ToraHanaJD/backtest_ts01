#include <iostream>
#include <iomanip>
#include <memory>
#include <vector>

#include "backtest/data.h"
#include "backtest/exchange.h"
#include "backtest/execution_client.h"
#include "backtest/clock.h"

using namespace backtest;

// 创建一个辅助函数来为示例生成ID
inline InstrumentId createInstrumentId(uint64_t id) {
    return id;
}

inline Venue createVenue(uint64_t id) {
    return id;
}

int main() {
    // 创建时钟
    auto clock = std::make_shared<TestClock>();
    
    // 设置初始资金
    std::vector<Money> initial_balances;
    initial_balances.emplace_back("USDT", 10000.0);
    
    // 创建交易所
    auto exchange = std::make_shared<SimulatedExchange>(
        createVenue(1),  // 使用数字ID代替字符串
        OmsType::NETTING,
        AccountType::CASH,
        initial_balances,
        BookType::L3,
        1.0,
        false, // frozen_account
        true,  // bar_execution
        false, // reject_stop_orders
        true,  // support_gtd_orders
        false, // support_contingent_orders
        false, // use_position_ids
        true,  // use_random_ids
        false, // use_reduce_only
        true   // use_message_queue
    );
    
    // 创建执行客户端
    auto client = std::make_shared<BacktestExecutionClient>(
        generateUUID(),  // trader_id
        generateUUID(),  // account_id
        exchange,
        clock,
        false,  // routing
        false   // frozen_account
    );
    
    // 注册执行客户端到交易所
    exchange->registerClient(client);
    
    // 添加交易对
    InstrumentId btcusdt = createInstrumentId(1001);  // 使用数字ID
    exchange->addInstrument(btcusdt);
    
    // 初始化账户
    exchange->initializeAccount();
    
    // 设置初始价格
    QuoteTick quote(
        btcusdt,
        40000.0, // bid_price
        40010.0, // ask_price
        1.0,     // bid_size
        1.0,     // ask_size
        1000000, // ts_event
        1000000  // ts_init
    );
    
    clock->setTime(1000000); // 时间戳设置为1000000纳秒
    exchange->processQuoteTick(quote);
    exchange->process(clock->timestampNs());
    
    // 提交买入订单
    auto buy_order = std::make_shared<Order>(
        generateUUID(),
        client->venue(),
        btcusdt,
        OrderSide::BUY,
        OrderType::LIMIT,
        0.1,        // 数量
        40000.0,     // 价格
        TimeInForce::GTC,
        clock->timestampNs()
    );
    
    std::cout << "Submitting BUY order: " << buy_order->toString() << std::endl;
    client->submitOrder(buy_order);
    
    // 处理时间推进
    clock->setTime(2000000);
    exchange->process(clock->timestampNs());
    
    // 创建一个匹配的市场卖单来触发成交
    TradeTick trade(
        btcusdt,
        40000.0,  // price
        0.5,      // size
        "1",      // trade_id
        "2",      // buyer_order_id
        "3",      // seller_order_id
        false,    // buyer_maker
        2000000,  // ts_event
        2000000   // ts_init
    );
    
    std::cout << "Processing trade at price: " << trade.price() << std::endl;
    exchange->processTradeTick(trade);
    exchange->process(clock->timestampNs());
    
    // 查看账户余额
    std::cout << "\nAccount balance after BUY order executed:" << std::endl;
    const Account* account = client->getAccount();
    if (account) {
        for (const auto& balance : account->balances()) {
            std::cout << balance.currency() << ": " << std::fixed << std::setprecision(2) 
                      << balance.total() << " (Free: " << balance.free() 
                      << ", Locked: " << balance.locked() << ")" << std::endl;
        }
    }
    
    // 假设BTC价格上涨，提交卖出订单
    clock->setTime(3000000);
    
    // 更新报价
    QuoteTick updated_quote(
        btcusdt,
        42000.0, // bid_price
        42010.0, // ask_price
        1.0,     // bid_size
        1.0,     // ask_size
        3000000, // ts_event
        3000000  // ts_init
    );
    exchange->processQuoteTick(updated_quote);
    exchange->process(clock->timestampNs());
    
    // 提交卖出订单
    auto sell_order = std::make_shared<Order>(
        generateUUID(),
        client->venue(),
        btcusdt,
        OrderSide::SELL,
        OrderType::LIMIT,
        0.1,        // 数量
        42000.0,     // 价格
        TimeInForce::GTC,
        clock->timestampNs()
    );
    
    std::cout << "\nSubmitting SELL order: " << sell_order->toString() << std::endl;
    client->submitOrder(sell_order);
    
    // 处理时间推进
    clock->setTime(4000000);
    exchange->process(clock->timestampNs());
    
    // 创建一个匹配的市场买单来触发成交
    TradeTick updated_trade(
        btcusdt,
        42000.0,  // price
        0.5,      // size
        "4",      // trade_id
        "5",      // buyer_order_id
        "6",      // seller_order_id
        true,     // buyer_maker
        4000000,  // ts_event
        4000000   // ts_init
    );
    
    std::cout << "Processing trade at price: " << updated_trade.price() << std::endl;
    exchange->processTradeTick(updated_trade);
    exchange->process(clock->timestampNs());
    
    // 查看账户余额
    std::cout << "\nAccount balance after SELL order executed:" << std::endl;
    if (account) {
        for (const auto& balance : account->balances()) {
            std::cout << balance.currency() << ": " << std::fixed << std::setprecision(2) 
                      << balance.total() << " (Free: " << balance.free() 
                      << ", Locked: " << balance.locked() << ")" << std::endl;
        }
    }
    
    // 计算收益
    double profit = 0.1 * (42000.0 - 40000.0);
    std::cout << "\nProfit from BTCUSDT trading: " << std::fixed << std::setprecision(2) 
              << profit << " USDT" << std::endl;
    
    return 0;
} 