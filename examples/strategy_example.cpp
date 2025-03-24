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
#include <string>
#include <vector>
#include <map>
#include <fstream>
#include <iomanip>

using namespace backtest;

// market data
struct MarketData {
    enum class Type {
        QUOTE, TRADE, BAR
    };
    
    Type type;
    InstrumentId instrument_id;
    UnixNanos timestamp;
    
    std::shared_ptr<QuoteTick> quote_ptr;
    std::shared_ptr<TradeTick> trade_ptr;
    std::shared_ptr<Bar> bar_ptr;
    
    MarketData(const QuoteTick& q) 
        : type(Type::QUOTE), instrument_id(q.instrumentId()), timestamp(q.tsEvent()),
          quote_ptr(std::make_shared<QuoteTick>(q)) {}
        
    MarketData(const TradeTick& t) 
        : type(Type::TRADE), instrument_id(t.instrumentId()), timestamp(t.tsEvent()),
          trade_ptr(std::make_shared<TradeTick>(t)) {}
    
    MarketData(const Bar& b) 
        : type(Type::BAR), instrument_id(b.instrumentId()), timestamp(b.tsEvent()),
          bar_ptr(std::make_shared<Bar>(b)) {}
          
    const QuoteTick& quote() const { return *quote_ptr; }
    const TradeTick& trade() const { return *trade_ptr; }
    const Bar& bar() const { return *bar_ptr; }
};

// strategy
class SimpleMovingAverageStrategy {
public:
    SimpleMovingAverageStrategy(
        std::shared_ptr<BacktestExecutionClient> client,
        InstrumentId instrument_id,
        int fast_period = 10,
        int slow_period = 20)
        : client_(client)
        , instrument_id_(instrument_id)
        , fast_period_(fast_period)
        , slow_period_(slow_period)
        , price_history_()
        , current_position_(0.0)
        , fast_ma_(0.0)
        , slow_ma_(0.0) {
    }
    
    // onMarketData函数
    void onMarketData(const MarketData& data) {
        // 仅处理交易品种相关的数据
        if (data.instrument_id != instrument_id_) {
            return;
        }
        
        double price = 0.0;
        if (data.type == MarketData::Type::QUOTE) {
            price = (data.quote().bidPrice() + data.quote().askPrice()) / 2.0;
        } else if (data.type == MarketData::Type::TRADE) {
            price = data.trade().price();
        } else if (data.type == MarketData::Type::BAR) {
            price = data.bar().close();
        } else {
            return;
        }
        
        // record price
        price_history_.push_back(price);
        
        if (price_history_.size() < static_cast<size_t>(slow_period_)) {
            return;
        }
        
        // drop old data
        if (price_history_.size() > static_cast<size_t>(slow_period_ * 2)) {
            price_history_.erase(price_history_.begin());
        }
        
        // calculate fast ma
        fast_ma_ = 0.0;
        for (size_t i = price_history_.size() - fast_period_; i < price_history_.size(); ++i) {
            fast_ma_ += price_history_[i];
        }
        fast_ma_ /= fast_period_;
        
        // calculate slow ma
        slow_ma_ = 0.0;
        for (size_t i = price_history_.size() - slow_period_; i < price_history_.size(); ++i) {
            slow_ma_ += price_history_[i];
        }
        slow_ma_ /= slow_period_;
        
        executeTradingLogic(price, data.timestamp);
    }
    
    // trading logic
    void executeTradingLogic(double current_price, UnixNanos timestamp) {
        if (fast_ma_ > slow_ma_ && current_position_ <= 0) {
            if (current_position_ < 0) {
                closePosition(timestamp);
            }
            openLongPosition(current_price, timestamp);
        } else if (fast_ma_ < slow_ma_ && current_position_ >= 0) {
            if (current_position_ > 0) {
                closePosition(timestamp);
            }
            openShortPosition(current_price, timestamp);
        }
        
        // record trade
        trade_history_.push_back({
            timestamp,
            current_price,
            fast_ma_,
            slow_ma_,
            current_position_
        });
    }
    
    // open long
    void openLongPosition(double price, UnixNanos timestamp) {
        double quantity = 0.1;
        current_position_ = quantity;
        
        auto order = std::make_shared<Order>(
            generateUUID(),
            client_->venue(),
            instrument_id_,
            OrderSide::BUY,
            OrderType::LIMIT,
            quantity,
            price,
            TimeInForce::GTC,
            timestamp
        );
        
        std::cout << "[策略信号]开多仓: 价格=" << price << ", 数量=" << quantity 
                  << ", 快线=" << fast_ma_ << ", 慢线=" << slow_ma_ << std::endl;
        
        try {
            client_->submitOrder(order);
        } catch (const std::exception& e) {
            std::cerr << "订单提交失败: " << e.what() << std::endl;
        }
    }
    
    // open short
    void openShortPosition(double price, UnixNanos timestamp) {
        double quantity = 0.1;
        current_position_ = -quantity;
        
        auto order = std::make_shared<Order>(
            generateUUID(),
            client_->venue(),
            instrument_id_,
            OrderSide::SELL,
            OrderType::LIMIT,
            quantity,
            price,
            TimeInForce::GTC,
            timestamp
        );
        
        std::cout << "[Strategy Signal] Open Short Position: price=" << price << ", quantity=" << quantity 
                  << ", fast_ma=" << fast_ma_ << ", slow_ma=" << slow_ma_ << std::endl;
        
        try {
            client_->submitOrder(order);
        } catch (const std::exception& e) {
            std::cerr << "Order submission failed: " << e.what() << std::endl;
        }
    }
    
    // close
    void closePosition(UnixNanos timestamp) {
        OrderSide side = current_position_ > 0 ? OrderSide::SELL : OrderSide::BUY;
        double quantity = std::abs(current_position_);
        
        std::cout << "[Strategy Signal] Close Position: " << (side == OrderSide::SELL ? "sell" : "buy")
                  << ", quantity=" << quantity << std::endl;
        
        auto order = std::make_shared<Order>(
            generateUUID(),
            client_->venue(),
            instrument_id_,
            side,
            OrderType::MARKET,
            quantity,
            0.0,
            TimeInForce::IOC,
            timestamp
        );
        
        try {
            client_->submitOrder(order);
            current_position_ = 0.0;
        } catch (const std::exception& e) {
            std::cerr << "Close position order submission failed: " << e.what() << std::endl;
        }
    }
    
    struct TradeRecord {
        UnixNanos timestamp;
        double price;
        double fast_ma;
        double slow_ma;
        double position;
    };
    
    const std::vector<TradeRecord>& getTradeHistory() const {
        return trade_history_;
    }
    
private:
    std::shared_ptr<BacktestExecutionClient> client_;
    InstrumentId instrument_id_;
    int fast_period_;
    int slow_period_;
    std::vector<double> price_history_;
    double current_position_;
    double fast_ma_;
    double slow_ma_;
    std::vector<TradeRecord> trade_history_;
};

// backtest result analysis sample
void analyzeTradingResults(
    const Account* account, 
    std::shared_ptr<BacktestExecutionClient> client,
    const SimpleMovingAverageStrategy& strategy) {
    
    std::cout << "\n-------------Backtest Result Analysis-------------\n" << std::endl;
    
    // 账户情况
    std::cout << "Account ID: " << account->accountId() << std::endl;
    std::cout << "Account Type: " << static_cast<int>(account->type()) << std::endl;
    
    // 余额
    std::cout << "\nAsset Situation:" << std::endl;
    double total_value = 0.0;
    for (const auto& balance : account->balances()) {
        std::cout << "  " << balance.currency() << ": "
                  << "Available=" << balance.free()
                  << ", Locked=" << balance.locked()
                  << ", Total=" << balance.total() << std::endl;
        
        if (balance.currency() == "USDT") {
            total_value += balance.total();
        } else if (balance.currency() == "BTC") {
            // 假设价格为50000
            total_value += balance.total() * 50000.0;
        }
    }
    std::cout << "Total Value(USDT): " << total_value << std::endl;
    
    // 交易记录
    const auto& trade_history = strategy.getTradeHistory();
    
    if (trade_history.empty()) {
        std::cout << "\nNo trading records." << std::endl;
        return;
    }
    
    // 计算交易统计数据
    int total_trades = 0;
    int winning_trades = 0;
    int losing_trades = 0;
    double total_profit = 0.0;
    double max_profit = 0.0;
    double max_loss = 0.0;
    double entry_price = 0.0;
    double position = 0.0;
    
    for (size_t i = 1; i < trade_history.size(); ++i) {
        const auto& prev = trade_history[i-1];
        const auto& curr = trade_history[i];
        
        if (prev.position == 0.0 && curr.position != 0.0) {
            entry_price = curr.price;
        } else if (prev.position != 0.0 && (curr.position == 0.0 || (prev.position > 0) != (curr.position > 0))) {
            total_trades++;
            double exit_price = curr.price;
            double profit = 0.0;
            
            if (prev.position > 0) {
                profit = (exit_price - entry_price) * std::abs(prev.position);
            } else {
                profit = (entry_price - exit_price) * std::abs(prev.position);
            }
            
            total_profit += profit;
            
            if (profit > 0) {
                winning_trades++;
                max_profit = std::max(max_profit, profit);
            } else {
                losing_trades++;
                max_loss = std::min(max_loss, profit);
            }
            
            if (curr.position != 0.0) {
                entry_price = curr.price;
            }
        }
        
        position = curr.position;
    }
    
    // 输出交易统计
    std::cout << "\nTrading Statistics:" << std::endl;
    std::cout << "Total trades: " << total_trades << std::endl;
    std::cout << "Winning trades: " << winning_trades << std::endl;
    std::cout << "Losing trades: " << losing_trades << std::endl;
    
    if (total_trades > 0) {
        double win_rate = static_cast<double>(winning_trades) / total_trades * 100.0;
        std::cout << "Win rate: " << std::fixed << std::setprecision(2) << win_rate << "%" << std::endl;
        std::cout << "Total profit: " << std::fixed << std::setprecision(4) << total_profit << " USDT" << std::endl;
        std::cout << "Average profit per trade: " << std::fixed << std::setprecision(4) << total_profit / total_trades << " USDT" << std::endl;
        std::cout << "Maximum single trade profit: " << std::fixed << std::setprecision(4) << max_profit << " USDT" << std::endl;
        std::cout << "Maximum single trade loss: " << std::fixed << std::setprecision(4) << -max_loss << " USDT" << std::endl;
        
        if (losing_trades > 0 && max_loss != 0) {
            std::cout << "Profit/Loss ratio: " << std::fixed << std::setprecision(2) << (max_profit / (-max_loss)) << std::endl;
        }
    }
    
    // output last record
    if (!trade_history.empty()) {
        const auto& last = trade_history.back();
        std::cout << "\nFinal State:" << std::endl;
        std::cout << "Price: " << last.price << std::endl;
        std::cout << "Fast MA: " << last.fast_ma << std::endl;
        std::cout << "Slow MA: " << last.slow_ma << std::endl;
        std::cout << "Position: " << last.position << std::endl;
    }
    
    // save trade history to csv file
    std::ofstream csv_file("backtest_results.csv");
    if (csv_file.is_open()) {
        csv_file << "Timestamp,Price,FastMA,SlowMA,Position" << std::endl;
        for (const auto& record : trade_history) {
            csv_file << record.timestamp << ","
                     << record.price << ","
                     << record.fast_ma << ","
                     << record.slow_ma << ","
                     << record.position << std::endl;
        }
        
        csv_file.close();
        std::cout << "\nCSV file saved to backtest_results.csv" << std::endl;
    } else {
        std::cerr << "Failed to create CSV file" << std::endl;
    }
    
    std::cout << "\n------------------Analysis completed-----------------\n" << std::endl;
}

std::vector<MarketData> loadMarketDataFromCSV(
    const std::string& filename, InstrumentId instrument_id) {
    
    std::vector<MarketData> data_feed;
    std::ifstream file(filename);
    
    if (!file.is_open()) {
        std::cerr << "Failed to open file: " << filename << std::endl;
        return data_feed;
    }
    
    std::string line;

    std::getline(file, line);
    
    while (std::getline(file, line)) {
        std::istringstream ss(line);
        std::string field;
        

        std::getline(ss, field, ',');
        UnixNanos timestamp = std::stoull(field);
        
        std::getline(ss, field, ',');
        double open = std::stod(field);

        std::getline(ss, field, ',');
        double high = std::stod(field);
        
        std::getline(ss, field, ',');
        double low = std::stod(field);
        
        std::getline(ss, field, ',');
        double close = std::stod(field);
        
        std::getline(ss, field, ',');
        double volume = std::stod(field);
        
        BarType bar_type(instrument_id, BarSpecification::MINUTE_1, BarAggregation::TRADE);
        Bar bar(
            bar_type,
            open,
            high,
            low,
            close,
            volume,
            timestamp,
            timestamp
        );
        
        data_feed.push_back(MarketData(bar));
        
        QuoteTick quote(
            instrument_id,
            close - 0.5, 
            volume / 10,
            close + 0.5, 
            volume / 15,
            timestamp,
            timestamp
        );
        data_feed.push_back(MarketData(quote));
        
        TradeTick trade(
            instrument_id,
            close,
            volume / 100,
            "trade-" + std::to_string(timestamp),
            "buy-" + std::to_string(timestamp),
            "sell-" + std::to_string(timestamp),
            false,
            timestamp,
            timestamp
        );
        data_feed.push_back(MarketData(trade));
    }
    
    file.close();
    std::cout << "Loaded " << data_feed.size() << " market data records from file\n" << std::endl;
    return data_feed;
}

// main function - execute backtest
int main() {
    try {
        // create instrument id
        InstrumentId btc_usdt = 123456;
        
        auto clock = std::make_shared<TestClock>();
        
        std::vector<Money> starting_balances = {
            Money("USDT", 10000.0),
            Money("BTC", 1.0)
        };
        
        auto exchange = std::make_shared<SimulatedExchange>(
            1001,                       // venue - 交易所ID
            OmsType::NETTING,           // oms_type - 订单管理系统类型
            AccountType::MARGIN,        // account_type - 账户类型
            starting_balances,          // starting_balances - 初始资金
            BookType::L2,               // book_type - 订单簿类型
            1.0,                        // default_leverage - 默认杠杆
            false,                      // frozen_account - 是否冻结账户
            false,                      // bar_execution - 是否支持K线驱动交易
            false,                      // reject_stop_orders - 是否拒绝止损单
            true,                       // support_gtd_orders - 是否支持GTD订单
            false,                      // support_contingent_orders - 是否支持条件单
            false,                      // use_position_ids - 是否使用仓位ID
            false,                      // use_random_ids - 是否使用随机ID
            false,                      // use_reduce_only - 是否仅减仓
            false                       // use_message_queue - 是否使用消息队列
        );
        
        exchange->initializeAccount();
        exchange->addInstrument(btc_usdt);
        
        const Account* exchange_account = exchange->getAccount();
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
        
        // load historical market data
        std::vector<MarketData> data_feed = loadMarketDataFromCSV("market_data.csv", btc_usdt);
        
        // no market data, generate simulated data for testing
        if (data_feed.empty()) {
            std::cout << "Generating market data for testing...\n" << std::endl;

            double price = 50000.0;
            for (int i = 0; i < 100; ++i) {
                UnixNanos timestamp = 1000000000 + i * 60000000000; // 假设每分钟一条数据
                
                if (i < 30) {
                    price += 100.0;
                } else if (i < 70) {
                    price -= 100.0;
                } else {
                    price += 120.0;
                }
                
                // 创建Bar数据
                BarType bar_type(btc_usdt, BarSpecification::MINUTE_1, BarAggregation::TRADE);
                Bar bar(
                    bar_type,
                    price - 50,
                    price + 100,
                    price - 100,
                    price,
                    10.0 + (rand() % 100) / 10.0,
                    timestamp,
                    timestamp
                );
                
                data_feed.push_back(MarketData(bar));
            }
        }
        
        SimpleMovingAverageStrategy myStrategy(client, btc_usdt, 5, 15);
        
        for (const auto& marketData : data_feed) {
            clock->setTime(marketData.timestamp);
            
            if (marketData.type == MarketData::Type::QUOTE) {
                exchange->processQuoteTick(marketData.quote());
            } else if (marketData.type == MarketData::Type::TRADE) {
                exchange->processTradeTick(marketData.trade());
            } else if (marketData.type == MarketData::Type::BAR) {
                exchange->processBar(marketData.bar());
            }
            
            exchange->process(clock->timestampNs());
            
            myStrategy.onMarketData(marketData);
        }
        
        analyzeTradingResults(exchange->getAccount(), client, myStrategy);
        
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Error occurred during backtest: " << e.what() << std::endl;
        return 1;
    }
} 