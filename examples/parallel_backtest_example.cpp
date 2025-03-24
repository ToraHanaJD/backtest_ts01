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

inline InstrumentId createInstrumentId(uint64_t id) {
    return id;
}

inline Venue createVenue(uint64_t id) {
    return id;
}

// Generate random bar data
std::vector<DataEvent> generateRandomBarData(InstrumentId instrument_id, int64_t start_time, int64_t end_time, 
                                            int64_t bar_interval_ns, int seed = 42) {
    std::vector<DataEvent> events;
    std::mt19937 gen(seed);
    std::normal_distribution<> price_change(0.0, 0.02); // Normal distribution with mean 0 and std dev 0.02
    std::uniform_real_distribution<> volume_dist(10.0, 100.0); // Volume between 10 and 100
    
    double last_price = 100.0; // Initial price
    
    // Create a basic BarType
    BarType bar_type(instrument_id, BarSpecification::MINUTE_1, BarAggregation::TRADE);
    
    for (int64_t time = start_time; time < end_time; time += bar_interval_ns) {
        double change = price_change(gen);
        last_price *= (1.0 + change);
        
        double open = last_price;
        double high = open * (1.0 + std::abs(price_change(gen)));
        double low = open * (1.0 - std::abs(price_change(gen)));
        double close = open * (1.0 + price_change(gen));
        
        high = std::max({open, high, close});
        low = std::min({open, low, close});
        
        double volume = volume_dist(gen);
        
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
        
        // Add Bar data event
        events.emplace_back(time, bar);
        
        OrderBookDelta bid_delta(
            instrument_id,
            OrderBookAction::ADD,  // Add
            true,                  // Is bid
            low * 0.99,            // Bid price slightly lower than low
            volume * 2,            // Size is twice the volume
            time,
            time
        );
        events.emplace_back(time, bid_delta);
        
        // Create ask order book delta
        OrderBookDelta ask_delta(
            instrument_id,
            OrderBookAction::ADD,  // Add
            false,                 // Is ask
            high * 1.01,           // Ask price slightly higher than high
            volume * 2,            // Size is twice the volume
            time,
            time
        );
        events.emplace_back(time, ask_delta);
        

        OrderBookDelta market_bid_delta(
            instrument_id,
            OrderBookAction::ADD,
            true,                  // Bid
            close,                 // Current close price
            volume,                // Volume size
            time,
            time
        );
        events.emplace_back(time, market_bid_delta);
        
        OrderBookDelta market_ask_delta(
            instrument_id,
            OrderBookAction::ADD,
            false,                 // Ask
            close,                 // Current close price
            volume,                // Volume size
            time,
            time
        );
        events.emplace_back(time, market_ask_delta);
    }
    
    return events;
}

// Simple moving average strategy
class SimpleMovingAverageStrategy : public Strategy {
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
        
        // Print strategy initialization info
        std::cout << "Strategy initialized with short MA: " << short_window_ 
                  << " and long MA: " << long_window_ << std::endl;
    }
    
    void onBar(const Bar& bar) override {
        BarType bar_type = bar.barType();
        InstrumentId bar_instrument = bar_type.instrument_id;
        
        if (bar_instrument != instrument_) {
            return;
        }
        
        prices_.push_back(bar.close());
        
        if (prices_.size() >= (size_t)long_window_) {
            double short_ma = calculateMA(short_window_);
            double long_ma = calculateMA(long_window_);
            
            if (short_ma > long_ma && position_ <= 0) {
                double price = bar.close();
                double quantity = 1.0;
                
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
                double price = bar.close();
                double quantity = 1.0;
                
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
    
    void onQuote(const QuoteTick& quote) override {}
    void onTrade(const TradeTick& trade) override {}
    void onOrderBookDelta(const OrderBookDelta& delta) override {}
    void onFill(const Fill& fill) override {}
    
private:
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
    BacktestConfig config;
    config.start_time_ns = 1000000000;  // Start time (nanoseconds)
    config.end_time_ns = 2000000000;    // End time (nanoseconds)
    config.batch_size = 1000;           // Batch size
    config.enable_parallel = false;     // Disable parallel processing, use sequential
    
    std::vector<Money> initial_balances;
    initial_balances.emplace_back("USDT", 10000.0);
    config.initial_balances = initial_balances;
    
    config.venue = createVenue(1);
    config.oms_type = OmsType::NETTING;
    config.account_type = AccountType::CASH;
    config.book_type = BookType::L3;
    config.default_leverage = 1.0;
    
    ParallelBacktestEngine engine(config);
    
    InstrumentId btcusdt = createInstrumentId(1001);
    
    auto exchange = engine.getExchange();
    exchange->addInstrument(btcusdt);
    exchange->initializeAccount();
    
    engine.addDataSource([btcusdt, &config](UnixNanos start, UnixNanos end) {
        return generateRandomBarData(
            btcusdt,
            start,
            end,
            1000000, // 1ms interval
            42       // Random seed
        );
    });
    
    // Set strategy factory
    engine.setStrategyFactory([btcusdt](std::shared_ptr<ExecutionClient> client, std::shared_ptr<Clock> clock) {
        return std::make_shared<SimpleMovingAverageStrategy>(
            client,
            clock,
            btcusdt,
            5,  // short ma window
            20  // long ma window
        );
    });
    
    std::cout << "Starting backtest..." << std::endl;
    auto start_time = std::chrono::high_resolution_clock::now();
    
    BacktestResult result = engine.run();
    
    auto end_time = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
    
    std::cout << "Backtest completed in " << duration.count() << " ms" << std::endl;
    std::cout << "Processed " << result.processed_bars << " bars" << std::endl;
    std::cout << "Processed " << result.processed_trades << " trades" << std::endl;
    std::cout << "Processed " << result.processed_quotes << " quotes" << std::endl;
    std::cout << "Processed " << result.processed_orders << " orders" << std::endl;
    std::cout << "Filled " << result.filled_orders << " orders" << std::endl;
    
    // Print final account balances
    std::cout << "\nFinal account balances:" << std::endl;
    for (const auto& balance : result.final_balances) {
        std::cout << balance.currency() << ": " << std::fixed << std::setprecision(4)
                  << balance.total() << " (Free: " << balance.free()
                  << ", Locked: " << balance.locked() << ")" << std::endl;
    }
    
    return 0;
} 