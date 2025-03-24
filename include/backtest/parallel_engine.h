// -------------------------------------------------------------------------------------------------
//  Copyright (C) 2023-2025. All rights reserved.
//
//  Licensed under the MIT License.
// -------------------------------------------------------------------------------------------------

#pragma once

#include <vector>
#include <memory>
#include <string>
#include <unordered_map>
#include <future>
#include <functional>

#include "backtest/concurrency.h"
#include "backtest/data.h"
#include "backtest/data_event.h"
#include "backtest/exchange.h"
#include "backtest/clock.h"
#include "backtest/execution_client.h"
#include "backtest/strategy.h"

namespace backtest {

/**
 * @brief 回测配置结构体
 */
struct BacktestConfig {
    // 回测开始时间
    UnixNanos start_time_ns{0};
    // 回测结束时间
    UnixNanos end_time_ns{0};
    // 是否启用并行模式
    bool enable_parallel{true};
    
    // 线程池大小（如果为0，则使用可用CPU核心数）
    size_t thread_pool_size{0};
    
    size_t batch_size{1000};
    
    bool fast_mode{true};
    
    UnixNanos min_time_step_ns{1000000}; // 1毫秒
    
    size_t buffer_size{10000};
    
    // 初始账户余额
    std::vector<Money> initial_balances;
    
    // 交易所设置
    Venue venue;
    OmsType oms_type{OmsType::NETTING};
    AccountType account_type{AccountType::CASH};
    BookType book_type{BookType::L3};
    double default_leverage{1.0};
};

/**
 * @brief 回测结果结构体
 */
struct BacktestResult {
    // 回测执行时间（毫秒）
    int64_t execution_time_ms{0};
    
    // 处理的订单数量
    size_t processed_orders{0};
    
    // 成交的订单数量
    size_t filled_orders{0};
    
    // 处理的报价数量
    size_t processed_quotes{0};
    
    // 处理的交易数量
    size_t processed_trades{0};
    
    // 处理的K线数量
    size_t processed_bars{0};
    
    // 结束账户状态
    std::vector<AccountBalance> final_balances;
    
    // 回测期间的所有成交记录
    std::vector<Fill> fills;
    
    // 回测期间的所有订单记录
    std::vector<Order> orders;
};

/**
 * @brief 并行回测引擎类
 */
class ParallelBacktestEngine {
public:
    /**
     * @brief 创建一个新的并行回测引擎
     * 
     * @param config 回测配置
     */
    explicit ParallelBacktestEngine(BacktestConfig config);
    
    /**
     * @brief 析构函数
     */
    ~ParallelBacktestEngine();
    
    /**
     * @brief 添加数据源
     */
    using DataSourceFunction = std::function<std::vector<DataEvent>(UnixNanos, UnixNanos)>;
    void addDataSource(DataSourceFunction data_source);
    
    /**
     * @brief 设置策略工厂函数
     * 
     * @param strategy_factory 策略工厂函数，接收执行客户端和时钟，返回策略实例
     */
    void setStrategyFactory(std::function<std::shared_ptr<Strategy>(std::shared_ptr<ExecutionClient>, std::shared_ptr<Clock>)> strategy_factory);
    
    /**
     * @brief 运行回测
     */
    BacktestResult run();
    
    /**
     * @brief 获取交易所实例
     */
    std::shared_ptr<SimulatedExchange> getExchange() const;
    
    /**
     * @brief 获取执行客户端
     */
    std::shared_ptr<ExecutionClient> getExecutionClient() const;
    
    /**
     * @brief 获取时钟实例
     */
    std::shared_ptr<Clock> getClock() const;

private:
    // 回测配置
    BacktestConfig config_;
    
    std::unique_ptr<ThreadPool> thread_pool_;
    
    std::shared_ptr<SimulatedExchange> exchange_;
    
    std::shared_ptr<ExecutionClient> execution_client_;
    
    std::shared_ptr<TestClock> clock_;
    
    // 策略实例
    std::shared_ptr<Strategy> strategy_;
    
    // 策略工厂函数
    std::function<std::shared_ptr<Strategy>(std::shared_ptr<ExecutionClient>, std::shared_ptr<Clock>)> strategy_factory_;
    
    // 数据源列表
    std::vector<DataSourceFunction> data_sources_;
    
    // 数据队列, 可以更换
    HighPerformanceQueue<DataEvent> data_queue_;
    
    std::vector<DataEvent> loadAndSortData();
    
    // 并行处理数据批次
    void processDataBatchesParallel(const std::vector<DataEvent>& sorted_data, BacktestResult& result);
    
    // 顺序处理数据
    void processDataSequential(const std::vector<DataEvent>& sorted_data, BacktestResult& result);
    
    // 处理单个数据事件
    void processDataEvent(const DataEvent& event, BacktestResult& result);
};

} // namespace backtest 