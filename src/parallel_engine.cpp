// -------------------------------------------------------------------------------------------------
//  Copyright (C) 2023-2025. All rights reserved.
//
//  Licensed under the MIT License.
// -------------------------------------------------------------------------------------------------

#include "backtest/parallel_engine.h"
#include <algorithm>
#include <chrono>
#include <iostream>
#include <thread>
#include <vector>

namespace backtest {

ParallelBacktestEngine::ParallelBacktestEngine(BacktestConfig config)
    : config_(std::move(config)) {
    
    // 初始化线程池
    size_t thread_count = config_.thread_pool_size;
    if (thread_count == 0) {
        thread_count = std::thread::hardware_concurrency();
    }
    
    if (config_.enable_parallel) {
        thread_pool_ = std::make_unique<ThreadPool>(thread_count);
    }
    
    // 初始化时钟
    clock_ = std::make_shared<TestClock>();
    clock_->setTime(config_.start_time_ns);
    
    // 初始化交易所
    exchange_ = std::make_shared<SimulatedExchange>(
        config_.venue,
        config_.oms_type,
        config_.account_type,
        config_.initial_balances,
        config_.book_type,
        config_.default_leverage,
        false, // frozen_account
        true,  // bar_execution
        false, // reject_stop_orders
        true,  // support_gtd_orders
        true,  // support_contingent_orders
        false, // use_position_ids
        true,  // use_random_ids
        false, // use_reduce_only
        true   // use_message_queue
    );
    
    // 初始化执行客户端 - 使用data.h中定义的generateUUID函数
    UUID trader_id = generateUUID();
    UUID account_id = generateUUID();
    
    execution_client_ = std::make_shared<BacktestExecutionClient>(
        trader_id,  // trader_id
        account_id, // account_id
        exchange_,
        clock_,
        false,  // routing
        false   // frozen_account
    );
    
    exchange_->registerClient(execution_client_);
}

ParallelBacktestEngine::~ParallelBacktestEngine() {
    // 确保线程池先被销毁
    thread_pool_.reset();
}

void ParallelBacktestEngine::addDataSource(
    std::function<std::vector<DataEvent>(UnixNanos, UnixNanos)> data_source) {
    data_sources_.push_back(std::move(data_source));
}

void ParallelBacktestEngine::setStrategyFactory(
    std::function<std::shared_ptr<Strategy>(std::shared_ptr<ExecutionClient>, std::shared_ptr<Clock>)> strategy_factory) {
    strategy_factory_ = std::move(strategy_factory);
}

std::shared_ptr<SimulatedExchange> ParallelBacktestEngine::getExchange() const {
    return exchange_;
}

std::shared_ptr<ExecutionClient> ParallelBacktestEngine::getExecutionClient() const {
    return execution_client_;
}

std::shared_ptr<Clock> ParallelBacktestEngine::getClock() const {
    return clock_;
}

std::vector<DataEvent> ParallelBacktestEngine::loadAndSortData() {
    std::vector<DataEvent> all_data;
    
    for (const auto& data_source : data_sources_) {
        auto data = data_source(config_.start_time_ns, config_.end_time_ns);
        all_data.insert(all_data.end(), data.begin(), data.end());
    }
    
    std::sort(all_data.begin(), all_data.end());
    
    return all_data;
}

void ParallelBacktestEngine::processDataEvent(const DataEvent& event, BacktestResult& result) {
    clock_->setTime(event.timestamp);
    
    // 处理数据事件
    switch (event.type) {
        case DataEventType::QUOTE_TICK:
            exchange_->processQuoteTick(event.quote());
            result.processed_quotes++;
            if (strategy_) {
                strategy_->onQuote(event.quote());
            }
            break;
            
        case DataEventType::TRADE_TICK:
            exchange_->processTradeTick(event.trade());
            result.processed_trades++;
            if (strategy_) {
                strategy_->onTrade(event.trade());
            }
            break;
            
        case DataEventType::BAR:
            exchange_->processBar(event.bar());
            result.processed_bars++;
            if (strategy_) {
                strategy_->onBar(event.bar());
            }
            break;
            
        case DataEventType::ORDER_BOOK_DELTA:
            exchange_->processOrderBookDelta(event.delta());
            if (strategy_) {
                strategy_->onOrderBookDelta(event.delta());
            }
            break;
            
        case DataEventType::INSTRUMENT_STATUS:
            exchange_->processInstrumentStatus(event.status());
            break;
    }
    
    // 处理交易所内部逻辑
    exchange_->process(event.timestamp);
    
    // 更新订单统计
    result.processed_orders = exchange_->orderCount();
    result.filled_orders = exchange_->fillCount();
}

void ParallelBacktestEngine::processDataBatchesParallel(
    const std::vector<DataEvent>& sorted_data,
    BacktestResult& result) {
    
    const size_t data_size = sorted_data.size();
    const size_t batch_size = config_.batch_size;
    const size_t num_batches = (data_size + batch_size - 1) / batch_size;
    
    // 为每个批次创建一个处理任务
    std::vector<std::future<void>> futures;
    SpinLock result_mutex;
    
    for (size_t batch_idx = 0; batch_idx < num_batches; ++batch_idx) {
        const size_t start_idx = batch_idx * batch_size;
        const size_t end_idx = std::min(start_idx + batch_size, data_size);
        
        // 提交到线程池
        futures.push_back(thread_pool_->enqueue([this, &sorted_data, &result, &result_mutex, start_idx, end_idx]() {
            BacktestResult batch_result;
            
            for (size_t i = start_idx; i < end_idx; ++i) {
                processDataEvent(sorted_data[i], batch_result);
            }
            
            {
                result_mutex.lock();
                result.processed_quotes += batch_result.processed_quotes;
                result.processed_trades += batch_result.processed_trades;
                result.processed_bars += batch_result.processed_bars;
                result.processed_orders += batch_result.processed_orders;
                result.filled_orders += batch_result.filled_orders;
                result_mutex.unlock();
            }
        }));
    }
    
    for (auto& future : futures) {
        future.wait();
    }
}

void ParallelBacktestEngine::processDataSequential(
    const std::vector<DataEvent>& sorted_data,
    BacktestResult& result) {
    
    for (const auto& event : sorted_data) {
        processDataEvent(event, result);
    }
}

BacktestResult ParallelBacktestEngine::run() {
    BacktestResult result;
    
    auto start_time = std::chrono::high_resolution_clock::now();
    
    // 启动执行客户端
    execution_client_->start();
    
    // 创建策略实例
    if (strategy_factory_) {
        strategy_ = strategy_factory_(execution_client_, clock_);
        if (strategy_) {
            std::cout << "Strategy created successfully" << std::endl;
        }
    }
    
    auto sorted_data = loadAndSortData();
    std::cout << "Loaded " << sorted_data.size() << " data events" << std::endl;
    
    if (config_.enable_parallel && thread_pool_) {
        processDataBatchesParallel(sorted_data, result);
    } else {
        processDataSequential(sorted_data, result);
    }
    
    auto end_time = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
    
    result.execution_time_ms = duration.count();
    
    // 确保从交易所获取最终的订单统计
    result.processed_orders = exchange_->orderCount();
    result.filled_orders = exchange_->fillCount();
    
    if (const auto* account = execution_client_->getAccount()) {
        result.final_balances = account->balances();
    }
    
    return result;
}

} // namespace backtest 