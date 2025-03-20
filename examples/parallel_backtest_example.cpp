#include <iostream>
#include <vector>
#include <memory>
#include <chrono>
#include <random>
#include <iomanip>
#include <cmath>
#include <unordered_map>

#include "backtest/parallel_engine.h"
#include "backtest/data.h"
#include "backtest/exchange.h"
#include "backtest/execution_client.h"
#include "backtest/clock.h"

using namespace backtest;

// 创建辅助函数来为示例生成ID
inline InstrumentId createInstrumentId(uint64_t id) {
    return id;
}

inline Venue createVenue(uint64_t id) {
    return id;
}

// 生成随机K线数据（供回测使用）
std::vector<DataEvent> generateRandomBarData(InstrumentId instrument_id, int64_t start_time, int64_t end_time, 
                                            int64_t bar_interval_ns, int seed = 42) {
    std::vector<DataEvent> events;
    std::mt19937 gen(seed);
    std::normal_distribution<> price_change(0.0, 0.02); // 均值为0，标准差为0.02的正态分布
    std::uniform_real_distribution<> volume_dist(10.0, 100.0); // 交易量在10到100之间均匀分布
    
    double last_price = 100.0; // 起始价格
    
    // 创建一个基本的BarType
    BarType bar_type(createInstrumentId(1001), BarSpecification::MINUTE_1, BarAggregation::TRADE);
    
    for (int64_t time = start_time; time < end_time; time += bar_interval_ns) {
        double change = price_change(gen);
        last_price *= (1.0 + change);
        
        double open = last_price;
        double high = open * (1.0 + std::abs(price_change(gen)));
        double low = open * (1.0 - std::abs(price_change(gen)));
        double close = open * (1.0 + price_change(gen));
        
        // 确保high是最高价，low是最低价
        high = std::max({open, high, close});
        low = std::min({open, low, close});
        
        double volume = volume_dist(gen);
        
        // 创建Bar对象
        Bar bar(
            bar_type,
            open,
            high,
            low,
            close,
            volume,
            time,
            time
        );
        
        // 添加到事件列表
        events.emplace_back(time, bar);
    }
    
    return events;
}

// 简单的移动平均策略
class SimpleMovingAverageStrategy {
public:
    SimpleMovingAverageStrategy(
        std::shared_ptr<ExecutionClient> client,
        std::shared_ptr<Clock> clock,
        InstrumentId instrument,
        int short_window, 
        int long_window
    ) 
        : client_(client)
        , clock_(clock)
        , instrument_(instrument)
        , short_window_(short_window)
        , long_window_(long_window) {
        
        prices_ = std::vector<double>();
        position_ = 0.0;
        
        // 打印策略初始化信息
        std::cout << "Strategy initialized with short MA: " << short_window_ 
                  << " and long MA: " << long_window_ << std::endl;
    }
    
    void onBar(const Bar& bar) {
        // 我们需要从bar中提取instrumentId
        BarType bar_type = bar.barType();
        InstrumentId bar_instrument = bar_type.instrument_id;
        
        if (bar_instrument != instrument_) {
            return;
        }
        
        // 添加收盘价到我们的价格历史记录
        prices_.push_back(bar.close());
        
        // 如果我们有足够的数据来计算长期移动平均线
        if (prices_.size() >= (size_t)long_window_) {
            double short_ma = calculateMA(short_window_);
            double long_ma = calculateMA(long_window_);
            
            // 当短期MA穿过长期MA时买入，反之卖出
            if (short_ma > long_ma && position_ <= 0) {
                // 如果我们持有空仓或没有仓位，则买入
                double price = bar.close();
                double quantity = 1.0; // 简单起见，每次交易1个单位
                
                std::shared_ptr<Order> order = std::make_shared<Order>(
                    generateUUID(),
                    client_->venue(), 
                    instrument_,
                    OrderSide::BUY,
                    OrderType::MARKET,
                    quantity,
                    price,
                    TimeInForce::IOC,
                    clock_->timestampNs()
                );
                
                client_->submitOrder(order);
                position_ += quantity;
                
                std::cout << "BUY " << quantity << " at price " << price 
                          << " at time " << clock_->timestampNs() << std::endl;
            }
            else if (short_ma < long_ma && position_ >= 0) {
                // 如果我们持有多仓或没有仓位，则卖出
                double price = bar.close();
                double quantity = 1.0; // 简单起见，每次交易1个单位
                
                std::shared_ptr<Order> order = std::make_shared<Order>(
                    generateUUID(),
                    client_->venue(),
                    instrument_, 
                    OrderSide::SELL,
                    OrderType::MARKET,
                    quantity,
                    price,
                    TimeInForce::IOC,
                    clock_->timestampNs()
                );
                
                client_->submitOrder(order);
                position_ -= quantity;
                
                std::cout << "SELL " << quantity << " at price " << price 
                          << " at time " << clock_->timestampNs() << std::endl;
            }
        }
    }
    
private:
    // 计算移动平均
    double calculateMA(int window) {
        int start_idx = std::max(0, (int)prices_.size() - window);
        int end_idx = prices_.size();
        
        double sum = 0.0;
        for (int i = start_idx; i < end_idx; ++i) {
            sum += prices_[i];
        }
        
        return sum / (end_idx - start_idx);
    }
    
    std::shared_ptr<ExecutionClient> client_;
    std::shared_ptr<Clock> clock_;
    InstrumentId instrument_;
    int short_window_;
    int long_window_;
    std::vector<double> prices_;
    double position_;
};

int main() {
    // 回测配置
    BacktestConfig config;
    config.start_time_ns = 1000000000;  // 开始时间（纳秒）
    config.end_time_ns = 2000000000;    // 结束时间（纳秒）
    config.batch_size = 1000;           // 批处理大小
    
    // 初始资金
    std::vector<Money> initial_balances;
    initial_balances.emplace_back("USDT", 10000.0);
    config.initial_balances = initial_balances;
    
    // 创建交易所配置
    config.venue = createVenue(1);
    config.oms_type = OmsType::NETTING;
    config.account_type = AccountType::CASH;
    config.book_type = BookType::L3;
    config.default_leverage = 1.0;
    
    // 创建并配置回测引擎
    ParallelBacktestEngine engine(config);
    
    // 设置数据源（通过Lambda函数提供数据）
    InstrumentId btcusdt = createInstrumentId(1001); // BTC-USDT
    
    engine.addDataSource([btcusdt, &config](UnixNanos start, UnixNanos end) {
        return generateRandomBarData(
            btcusdt,
            start,
            end,
            1000000, // 1毫秒间隔
            42       // 随机种子
        );
    });
    
    // 设置策略工厂
    engine.setStrategyFactory([btcusdt](std::shared_ptr<ExecutionClient> client, std::shared_ptr<Clock> clock) {
        auto strategy = std::make_shared<SimpleMovingAverageStrategy>(
            client,
            clock,
            btcusdt,
            5,  // 短期MA窗口
            20  // 长期MA窗口
        );
        
        // 在这里，系统应该注册相应的回调函数
        // 由于这是一个简化版的例子，我们只是创建了策略实例
    });
    
    // 运行回测
    std::cout << "Starting backtest..." << std::endl;
    auto start_time = std::chrono::high_resolution_clock::now();
    
    BacktestResult result = engine.run();
    
    auto end_time = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
    
    // 打印回测结果
    std::cout << "Backtest completed in " << duration.count() << " ms" << std::endl;
    std::cout << "Processed " << result.processed_bars << " bars" << std::endl;
    std::cout << "Processed " << result.processed_trades << " trades" << std::endl;
    std::cout << "Processed " << result.processed_quotes << " quotes" << std::endl;
    std::cout << "Processed " << result.processed_orders << " orders" << std::endl;
    std::cout << "Filled " << result.filled_orders << " orders" << std::endl;
    
    // 打印最终账户余额
    std::cout << "\nFinal account balances:" << std::endl;
    for (const auto& balance : result.final_balances) {
        std::cout << balance.currency() << ": " << std::fixed << std::setprecision(4)
                  << balance.total() << " (Free: " << balance.free()
                  << ", Locked: " << balance.locked() << ")" << std::endl;
    }
    
    return 0;
} 