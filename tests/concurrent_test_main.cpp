// -------------------------------------------------------------------------------------------------
//  Copyright (C) 2023-2025. All rights reserved.
//
//  Licensed under the MIT License.
// -------------------------------------------------------------------------------------------------

#include <iostream>
#include <chrono>
#include <vector>
#include <string>
#include <random>
#include <memory>
#include <iomanip>
#include <cstring>

#include "backtest/concurrency.h"
#include "backtest/parallel_engine.h"
#include "backtest/data.h"
#include "backtest/data_event.h"
#include "backtest/exchange.h"
#include "backtest/clock.h"

using namespace backtest;
using namespace std::chrono;

// 生成随机行情数据用于测试
std::vector<DataEvent> generateRandomQuotes(const std::string& symbol, 
                                           UnixNanos start_time, 
                                           UnixNanos end_time, 
                                           size_t count) {
    std::vector<DataEvent> events;
    std::mt19937 gen(42);
    
    std::uniform_real_distribution<> price_dist(100.0, 200.0);
    std::uniform_real_distribution<> size_dist(1.0, 10.0);
    
    UnixNanos time_range = end_time - start_time;
    UnixNanos step = time_range / count;
    
    for (size_t i = 0; i < count; ++i) {
        UnixNanos timestamp = start_time + i * step;
        
        // 随机生成买卖价格和数量
        double bid_price = price_dist(gen);
        double ask_price = bid_price + 0.1 + (price_dist(gen) / 1000.0);
        double bid_size = size_dist(gen);
        double ask_size = size_dist(gen);
        
        // 创建报价事件
        QuoteTick quote(
            static_cast<InstrumentId>(std::hash<std::string>{}(symbol)), 
            bid_price,
            ask_price,
            bid_size,
            ask_size,
            timestamp,
            timestamp - 1000000
        );
        
        events.push_back(DataEvent(timestamp, quote));
    }
    
    return events;
}

std::vector<DataEvent> generateRandomTrades(const std::string& symbol, 
                                           UnixNanos start_time, 
                                           UnixNanos end_time, 
                                           size_t count) {
    std::vector<DataEvent> events;
    std::mt19937 gen(43); 
    
    std::uniform_real_distribution<> price_dist(100.0, 200.0);
    std::uniform_real_distribution<> size_dist(0.1, 5.0);
    std::uniform_int_distribution<> side_dist(0, 1);
    
    UnixNanos time_range = end_time - start_time;
    UnixNanos step = time_range / count;
    
    for (size_t i = 0; i < count; ++i) {
        UnixNanos timestamp = start_time + i * step;
        
        double price = price_dist(gen);
        double size = size_dist(gen);
        OrderSide side = side_dist(gen) == 0 ? OrderSide::BUY : OrderSide::SELL;
        
        TradeTick trade(
            static_cast<InstrumentId>(std::hash<std::string>{}(symbol)),
            price,
            size,
            std::to_string(generateUUID()),
            std::to_string(generateUUID()),
            std::to_string(generateUUID()),
            side_dist(gen) == 0,
            timestamp,           // 事件时间戳
            timestamp - 1000000  // 初始时间戳
        );
        
        events.push_back(DataEvent(timestamp, trade));
    }
    
    return events;
}

// 测试不同类型锁的性能
void testLockPerformance() {
    std::cout << "\n==== 锁性能测试 ====\n";
    
    const int numThreads = std::thread::hardware_concurrency();
    const int numOperations = 10000000; // 1千万次操作
    
    std::cout << "测试环境: " << numThreads << " 线程, 每个测试执行 " 
              << numOperations << " 次操作\n\n";
    
    std::atomic<int> counter(0);
    
    // std::mutex
    {
        counter.store(0);
        std::mutex mtx;
        
        auto start = high_resolution_clock::now();
        
        std::vector<std::thread> threads;
        for (int i = 0; i < numThreads; ++i) {
            threads.emplace_back([&mtx, &counter, numOps = numOperations / numThreads]() {
                for (int j = 0; j < numOps; ++j) {
                    std::lock_guard<std::mutex> lock(mtx);
                    counter.fetch_add(1, std::memory_order_relaxed);
                }
            });
        }
        
        for (auto& t : threads) {
            t.join();
        }
        
        auto end = high_resolution_clock::now();
        auto duration = duration_cast<milliseconds>(end - start).count();
        
        std::cout << "std::mutex: " << duration << " ms, " 
                  << (numOperations * 1000.0 / duration) << " ops/sec\n";
    }
    
    // 测试SpinLock
    {
        counter.store(0);
        SpinLock spinLock;
        
        auto start = high_resolution_clock::now();
        
        std::vector<std::thread> threads;
        for (int i = 0; i < numThreads; ++i) {
            threads.emplace_back([&spinLock, &counter, numOps = numOperations / numThreads]() {
                for (int j = 0; j < numOps; ++j) {
                    spinLock.lock();
                    counter.fetch_add(1, std::memory_order_relaxed);
                    spinLock.unlock();
                }
            });
        }
        
        for (auto& t : threads) {
            t.join();
        }
        
        auto end = high_resolution_clock::now();
        auto duration = duration_cast<milliseconds>(end - start).count();
        
        std::cout << "SpinLock: " << duration << " ms, " 
                  << (numOperations * 1000.0 / duration) << " ops/sec\n";
    }
    
    // 测试X86HardwareSpinLock
    {
        counter.store(0);
        X86HardwareSpinLock hwLock;
        
        auto start = high_resolution_clock::now();
        
        std::vector<std::thread> threads;
        for (int i = 0; i < numThreads; ++i) {
            threads.emplace_back([&hwLock, &counter, numOps = numOperations / numThreads]() {
                for (int j = 0; j < numOps; ++j) {
                    hwLock.lock();
                    counter.fetch_add(1, std::memory_order_relaxed);
                    hwLock.unlock();
                }
            });
        }
        
        for (auto& t : threads) {
            t.join();
        }
        
        auto end = high_resolution_clock::now();
        auto duration = duration_cast<milliseconds>(end - start).count();
        
        std::cout << "X86HardwareSpinLock: " << duration << " ms, " 
                  << (numOperations * 1000.0 / duration) << " ops/sec\n";
    }
    
    // 测试RWSpinLock (读多写少场景) 90% 读 10% 写操作
    {
        counter.store(0);
        RWSpinLock rwLock;
        
        auto start = high_resolution_clock::now();
        
        std::vector<std::thread> threads;
        for (int i = 0; i < numThreads; ++i) {
            threads.emplace_back([&rwLock, &counter, tid = i, numOps = numOperations / numThreads]() {
                std::mt19937 gen(tid);
                std::uniform_int_distribution<> dist(1, 100);
                
                for (int j = 0; j < numOps; ++j) {
                    int val = dist(gen);
                    if (val <= 90) { 
                        rwLock.lockRead();
                        volatile int dummy = counter.load(std::memory_order_relaxed);
                        (void)dummy;
                        rwLock.unlockRead();
                    } else {
                        rwLock.lockWrite();
                        counter.fetch_add(1, std::memory_order_relaxed);
                        rwLock.unlockWrite();
                    }
                }
            });
        }
        
        for (auto& t : threads) {
            t.join();
        }
        
        auto end = high_resolution_clock::now();
        auto duration = duration_cast<milliseconds>(end - start).count();
        
        std::cout << "RWSpinLock (90% 读, 10% 写): " << duration << " ms, " 
                  << (numOperations * 1000.0 / duration) << " ops/sec\n";
    }
}

// 测试不同类型队列的性能
void testQueuePerformance() {
    std::cout << "\n==== 队列性能测试 ====\n";
    
    const int numThreads = std::thread::hardware_concurrency();
    const int numItems = 1000000; // 每个线程处理100万个项目
    
    std::cout << "测试环境: " << numThreads << " 线程, 每个线程处理 " 
              << numItems << " 个项目\n\n";
    
    // std::queue + mutex
    {
        std::queue<int> stdQueue;
        std::mutex mtx;
        std::atomic<uint64_t> sum_in(0), sum_out(0);
        
        auto start = high_resolution_clock::now();
        
        std::vector<std::thread> producers;
        std::vector<std::thread> consumers;
        
        for (int i = 0; i < numThreads / 2; ++i) {
            producers.emplace_back([&stdQueue, &mtx, &sum_in, i, numItems]() {
                for (int j = 0; j < numItems; ++j) {
                    int val = i * numItems + j;
                    {
                        std::lock_guard<std::mutex> lock(mtx);
                        stdQueue.push(val);
                    }
                    sum_in.fetch_add(val, std::memory_order_relaxed);
                }
            });
        }
        
        for (int i = 0; i < numThreads / 2; ++i) {
            consumers.emplace_back([&stdQueue, &mtx, &sum_out, numItems]() {
                int count = 0;
                while (count < numItems) {
                    bool success = false;
                    int val;
                    {
                        std::lock_guard<std::mutex> lock(mtx);
                        if (!stdQueue.empty()) {
                            val = stdQueue.front();
                            stdQueue.pop();
                            success = true;
                        }
                    }
                    
                    if (success) {
                        sum_out.fetch_add(val, std::memory_order_relaxed);
                        count++;
                    } else {
                        std::this_thread::yield();
                    }
                }
            });
        }
        
        for (auto& t : producers) {
            t.join();
        }
        
        for (auto& t : consumers) {
            t.join();
        }
        
        auto end = high_resolution_clock::now();
        auto duration = duration_cast<milliseconds>(end - start).count();
        
        bool checksum_ok = (sum_in.load() == sum_out.load());
        std::cout << "std::queue + mutex: " << duration << " ms, " 
                  << (numItems * numThreads / 2 * 1000.0 / duration) << " ops/sec, "
                  << "校验: " << (checksum_ok ? "正确" : "错误") << "\n";
    }
    
    // ThreadSafeQueue
    {
        ThreadSafeQueue<int> tsQueue;
        std::atomic<uint64_t> sum_in(0), sum_out(0);
        
        auto start = high_resolution_clock::now();
        
        std::vector<std::thread> producers;
        std::vector<std::thread> consumers;
        
        for (int i = 0; i < numThreads / 2; ++i) {
            producers.emplace_back([&tsQueue, &sum_in, i, numItems]() {
                for (int j = 0; j < numItems; ++j) {
                    int val = i * numItems + j;
                    tsQueue.push(val);
                    sum_in.fetch_add(val, std::memory_order_relaxed);
                }
            });
        }
        
        for (int i = 0; i < numThreads / 2; ++i) {
            consumers.emplace_back([&tsQueue, &sum_out, numItems]() {
                int count = 0;
                while (count < numItems) {
                    int val;
                    if (tsQueue.tryPop(val)) {
                        sum_out.fetch_add(val, std::memory_order_relaxed);
                        count++;
                    } else {
                        std::this_thread::yield();
                    }
                }
            });
        }
        
        for (auto& t : producers) {
            t.join();
        }
        
        for (auto& t : consumers) {
            t.join();
        }
        
        auto end = high_resolution_clock::now();
        auto duration = duration_cast<milliseconds>(end - start).count();
        
        bool checksum_ok = (sum_in.load() == sum_out.load());
        std::cout << "ThreadSafeQueue: " << duration << " ms, " 
                  << (numItems * numThreads / 2 * 1000.0 / duration) << " ops/sec, "
                  << "校验: " << (checksum_ok ? "正确" : "错误") << "\n";
    }
#if defined(__linux__)
    {
        LockFreeQueue<int> queue(numItems * numThreads);
        
        std::atomic<uint64_t> sum_in(0), sum_out(0);
        
        auto start = high_resolution_clock::now();
        

        std::vector<std::thread> producers;
        std::vector<std::thread> consumers;
        
        // 启动生产者
        for (int i = 0; i < numThreads / 2; ++i) {
            producers.emplace_back([&queue, &sum_in, i, numItems]() {
                for (int j = 0; j < numItems; ++j) {
                    int val = i * numItems + j;
                    while (!queue.Push(val)) {
                        // 队列满，休眠一下再重试
                        std::this_thread::yield();
                    }
                    sum_in.fetch_add(val, std::memory_order_relaxed);
                }
            });
        }
        
        for (int i = 0; i < numThreads / 2; ++i) {
            consumers.emplace_back([&queue, &sum_out, numItems]() {
                int count = 0;
                while (count < numItems) {
                    int val;
                    if (queue.Pop(val)) {
                        sum_out.fetch_add(val, std::memory_order_relaxed);
                        count++;
                    } else {
                        std::this_thread::yield();
                    }
                }
            });
        }
        
        for (auto& t : producers) {
            t.join();
        }
        
        for (auto& t : consumers) {
            t.join();
        }
        
        auto end = high_resolution_clock::now();
        auto duration = duration_cast<milliseconds>(end - start).count();
        
        bool checksum_ok = (sum_in.load() == sum_out.load());
        std::cout << "LockFreeQueue: " << duration << " ms, " 
                  << (numItems * numThreads / 2 * 1000.0 / duration) << " ops/sec, "
                  << "校验: " << (checksum_ok ? "正确" : "错误") << "\n";
    }
#endif
    // HighPerformanceQueue
    {
        HighPerformanceQueue<int> hpQueue;
        std::atomic<uint64_t> sum_in(0), sum_out(0);
        
        auto start = high_resolution_clock::now();
        
        std::vector<std::thread> producers;
        std::vector<std::thread> consumers;
        
        for (int i = 0; i < numThreads / 2; ++i) {
            producers.emplace_back([&hpQueue, &sum_in, i, numItems]() {
                for (int j = 0; j < numItems; ++j) {
                    int val = i * numItems + j;
                    hpQueue.push(val);
                    sum_in.fetch_add(val, std::memory_order_relaxed);
                }
            });
        }
        
        for (int i = 0; i < numThreads / 2; ++i) {
            consumers.emplace_back([&hpQueue, &sum_out, numItems]() {
                int count = 0;
                while (count < numItems) {
                    int val;
                    if (hpQueue.try_pop(val)) {
                        sum_out.fetch_add(val, std::memory_order_relaxed);
                        count++;
                    } else {
                        std::this_thread::yield();
                    }
                }
            });
        }
        
        for (auto& t : producers) {
            t.join();
        }
        
        for (auto& t : consumers) {
            t.join();
        }
        
        auto end = high_resolution_clock::now();
        auto duration = duration_cast<milliseconds>(end - start).count();
        
        bool checksum_ok = (sum_in.load() == sum_out.load());
        std::cout << "HighPerformanceQueue: " << duration << " ms, " 
                  << (numItems * numThreads / 2 * 1000.0 / duration) << " ops/sec, "
                  << "校验: " << (checksum_ok ? "正确" : "错误") << "\n";
    }
}

// 测试并行回测引擎性能
void testParallelEngine() {
    std::cout << "\n==== 并行回测引擎性能测试 ====\n";
    
    // 测试参数
    const int numQuotes = 1000000;  // 100万个报价
    const int numTrades = 500000;   // 50万个交易
    
    // 创建回测配置
    BacktestConfig config;
    config.start_time_ns = 1742428800000000000; // 2025-03-19 00:00:00
    config.end_time_ns = 1742515200000000000;   // 2025-03-20 00:00:00
    config.enable_parallel = true;              // 启用并行模式
    config.thread_pool_size = 0;                // 使用默认线程数
    config.batch_size = 10000;                  // 批处理大小
    
    // 初始账户余额
    Money balance("USDT", 1000000.0);
    config.initial_balances.push_back(balance);
    
    // 设置交易所
    config.venue = static_cast<Venue>(std::hash<std::string>{}("BINANCE"));
    
    // 使用并行回测引擎
    {
        std::cout << "并行回测...\n";
        
        ParallelBacktestEngine engine(config);
        
        engine.addDataSource([=](UnixNanos start, UnixNanos end) {
            return generateRandomQuotes("BTC/USDT", start, end, numQuotes);
        });
        
        engine.addDataSource([=](UnixNanos start, UnixNanos end) {
            return generateRandomTrades("BTC/USDT", start, end, numTrades);
        });
        
        engine.addDataSource([=](UnixNanos start, UnixNanos end) {
            return generateRandomQuotes("ETH/USDT", start, end, numQuotes);
        });
        
        engine.addDataSource([=](UnixNanos start, UnixNanos end) {
            return generateRandomTrades("ETH/USDT", start, end, numTrades);
        });
        
        // 设置策略
        engine.setStrategyFactory([](std::shared_ptr<ExecutionClient> exec, std::shared_ptr<Clock> clock) {
        });
        
        auto start = high_resolution_clock::now();
        auto result = engine.run();
        auto end = high_resolution_clock::now();
        
        std::cout << "并行回测完成:\n";
        std::cout << "处理的报价: " << result.processed_quotes << "\n";
        std::cout << "处理的交易: " << result.processed_trades << "\n";
        std::cout << "引擎报告的执行时间: " << result.execution_time_ms << " ms\n";
        
        auto actual_duration = duration_cast<milliseconds>(end - start).count();
        std::cout << "实际执行时间: " << actual_duration << " ms\n";
        
        // 计算吞吐量
        double total_events = result.processed_quotes + result.processed_trades;
        double throughput = total_events * 1000.0 / result.execution_time_ms;
        std::cout << "吞吐量: " << std::fixed << std::setprecision(2) << throughput << " 事件/秒\n";
    }
    
    // 使用顺序回测引擎进行比较
    {
        std::cout << "\n顺序回测...\n";
        
        // 禁用并行模式
        config.enable_parallel = false;
        
        ParallelBacktestEngine engine(config);
        
        engine.addDataSource([=](UnixNanos start, UnixNanos end) {
            return generateRandomQuotes("BTC/USDT", start, end, numQuotes);
        });
        
        engine.addDataSource([=](UnixNanos start, UnixNanos end) {
            return generateRandomTrades("BTC/USDT", start, end, numTrades);
        });
        
        engine.addDataSource([=](UnixNanos start, UnixNanos end) {
            return generateRandomQuotes("ETH/USDT", start, end, numQuotes);
        });
        
        engine.addDataSource([=](UnixNanos start, UnixNanos end) {
            return generateRandomTrades("ETH/USDT", start, end, numTrades);
        });
        
        // 设置策略
        engine.setStrategyFactory([](std::shared_ptr<ExecutionClient> exec, std::shared_ptr<Clock> clock) {
        });
        
        // 运行回测
        auto start = high_resolution_clock::now();
        auto result = engine.run();
        auto end = high_resolution_clock::now();
        
        std::cout << "顺序回测完成:\n";
        std::cout << "处理的报价: " << result.processed_quotes << "\n";
        std::cout << "处理的交易: " << result.processed_trades << "\n";
        std::cout << "引擎报告的执行时间: " << result.execution_time_ms << " ms\n";
        
        auto actual_duration = duration_cast<milliseconds>(end - start).count();
        std::cout << "实际执行时间: " << actual_duration << " ms\n";
        
        // 计算吞吐量
        double total_events = result.processed_quotes + result.processed_trades;
        double throughput = total_events * 1000.0 / result.execution_time_ms;
        std::cout << "吞吐量: " << std::fixed << std::setprecision(2) << throughput << " 事件/秒\n";
    }
}

int main() {
    std::cout << "==================================" << std::endl;
    std::cout << "  高性能并发原语和并行引擎性能测试" << std::endl;
    std::cout << "==================================" << std::endl;
    
    testLockPerformance();
    
    testQueuePerformance();
    
    testParallelEngine();
    
    return 0;
} 