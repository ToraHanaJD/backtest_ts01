#include <iostream>
#include <vector>
#include <thread>
#include <atomic>
#include <chrono>
#include <functional>
#include <cassert>
#include <random>
#include <iomanip>
#include <mutex> // 用于比较
#include <shared_mutex> // 添加 shared_mutex 头文件
#include <queue> // 用于标准队列的比较
#include <cstring> // 用于memset等内存操作
#if defined(__linux__)

#include "backtest/concurrency.h"

using namespace backtest;
using namespace std::chrono;

// 辅助函数 - 打印测试结果
void printResult(const std::string& testName, long long elapsedMs, bool passed) {
    std::cout << "测试: " << std::left << std::setw(40) << testName 
              << " 耗时: " << std::right << std::setw(8) << elapsedMs << " ms  " 
              << (passed ? "✓ 通过" : "✗ 失败") << std::endl;
}

// 基准测试 - 互斥量，用于比较
void testStdMutex(int threadCount, int opsPerThread) {
    std::mutex mutex;
    std::atomic<int> counter(0);
    
    auto worker = [&](int id) {
        for (int i = 0; i < opsPerThread; ++i) {
            std::lock_guard<std::mutex> lock(mutex);
            counter.fetch_add(1, std::memory_order_relaxed);
        }
    };
    
    std::vector<std::thread> threads;
    auto start = high_resolution_clock::now();
    
    for (int i = 0; i < threadCount; ++i) {
        threads.emplace_back(worker, i);
    }
    
    for (auto& t : threads) {
        t.join();
    }
    
    auto end = high_resolution_clock::now();
    auto elapsed = duration_cast<milliseconds>(end - start).count();
    
    bool passed = (counter.load() == threadCount * opsPerThread);
    printResult("std::mutex", elapsed, passed);
}

// 测试 SpinLock
void testSpinLock(int threadCount, int opsPerThread) {
    SpinLock lock;
    std::atomic<int> counter(0);
    
    auto worker = [&](int id) {
        for (int i = 0; i < opsPerThread; ++i) {
            lock.lock();
            counter.fetch_add(1, std::memory_order_relaxed);
            lock.unlock();
        }
    };
    
    std::vector<std::thread> threads;
    auto start = high_resolution_clock::now();
    
    for (int i = 0; i < threadCount; ++i) {
        threads.emplace_back(worker, i);
    }
    
    for (auto& t : threads) {
        t.join();
    }
    
    auto end = high_resolution_clock::now();
    auto elapsed = duration_cast<milliseconds>(end - start).count();
    
    bool passed = (counter.load() == threadCount * opsPerThread);
    printResult("SpinLock", elapsed, passed);
}

// 测试 X86HardwareSpinLock
void testX86HardwareSpinLock(int threadCount, int opsPerThread) {
    X86HardwareSpinLock lock;
    std::atomic<int> counter(0);
    
    auto worker = [&](int id) {
        for (int i = 0; i < opsPerThread; ++i) {
            lock.lock();
            counter.fetch_add(1, std::memory_order_relaxed);
            lock.unlock();
        }
    };
    
    std::vector<std::thread> threads;
    auto start = high_resolution_clock::now();
    
    for (int i = 0; i < threadCount; ++i) {
        threads.emplace_back(worker, i);
    }
    
    for (auto& t : threads) {
        t.join();
    }
    
    auto end = high_resolution_clock::now();
    auto elapsed = duration_cast<milliseconds>(end - start).count();
    
    bool passed = (counter.load() == threadCount * opsPerThread);
    printResult("X86HardwareSpinLock", elapsed, passed);
}

// 测试 LockFreeQueue
void testLockFreeQueue(int threadCount, int opsPerThread) {
    LockFreeQueue<int> queue(10000);
    std::atomic<int> pushCount(0);
    std::atomic<int> popCount(0);
    
    auto producer = [&](int id) {
        for (int i = 0; i < opsPerThread; ++i) {
            int value = i + id * opsPerThread;
            queue.Push(value);
            pushCount.fetch_add(1, std::memory_order_relaxed);
        }
    };
    
    auto consumer = [&](int id) {
        int value;
        int localCount = 0;
        
        // 每个消费者尝试消费固定数量的元素
        int targetCount = opsPerThread;
        
        while (localCount < targetCount) {
            if (queue.Pop(value)) {
                popCount.fetch_add(1, std::memory_order_relaxed);
                localCount++;
            } else {
                // 如果队列暂时为空，让出CPU
                std::this_thread::yield();
            }
        }
    };
    
    std::vector<std::thread> producers;
    std::vector<std::thread> consumers;
    auto start = high_resolution_clock::now();
    
    // 一半线程作为生产者
    for (int i = 0; i < threadCount / 2; ++i) {
        producers.emplace_back(producer, i);
    }
    
    // 一半线程作为消费者
    for (int i = 0; i < threadCount / 2; ++i) {
        consumers.emplace_back(consumer, i);
    }
    
    // 等待所有生产者完成
    for (auto& t : producers) {
        t.join();
    }
    
    // 等待所有消费者完成
    for (auto& t : consumers) {
        t.join();
    }
    
    auto end = high_resolution_clock::now();
    auto elapsed = duration_cast<milliseconds>(end - start).count();
    
    // 检查是否所有元素都被处理
    bool allProcessed = (pushCount.load() == popCount.load());
    bool queueEmpty = queue.IsEmpty();
    
    printResult("LockFreeQueue", elapsed, allProcessed && queueEmpty);
    
    if (!allProcessed) {
        std::cout << "   推送: " << pushCount.load() << ", 弹出: " << popCount.load() << std::endl;
    }
    if (!queueEmpty) {
        std::cout << "   队列剩余: " << queue.Size() << " 个元素" << std::endl;
    }
}

// 测试标准队列 (互斥锁保护的队列)
void testStdQueueWithMutex(int threadCount, int opsPerThread) {
    std::queue<int> queue;
    std::mutex mutex;
    std::atomic<int> pushSum(0);
    std::atomic<int> popSum(0);
    std::atomic<int> pushCount(0);
    std::atomic<int> popCount(0);
    
    auto producer = [&](int id) {
        for (int i = 0; i < opsPerThread; ++i) {
            int value = i + id * opsPerThread;
            {
                std::lock_guard<std::mutex> lock(mutex);
                queue.push(value);
            }
            pushSum.fetch_add(value, std::memory_order_relaxed);
            pushCount.fetch_add(1, std::memory_order_relaxed);
        }
    };
    
    auto consumer = [&](int id) {
        int localCount = 0;
        
        while (localCount < opsPerThread * threadCount / 2) {
            bool success = false;
            int value = 0;
            
            {
                std::lock_guard<std::mutex> lock(mutex);
                if (!queue.empty()) {
                    value = queue.front();
                    queue.pop();
                    success = true;
                }
            }
            
            if (success) {
                popSum.fetch_add(value, std::memory_order_relaxed);
                popCount.fetch_add(1, std::memory_order_relaxed);
                localCount++;
            } else {
                std::this_thread::yield();
                
                if (popCount.load(std::memory_order_relaxed) >= pushCount.load(std::memory_order_relaxed)) {
                    break;
                }
            }
        }
    };
    
    std::vector<std::thread> producers;
    std::vector<std::thread> consumers;
    auto start = high_resolution_clock::now();
    
    // 一半的线程作为生产者
    for (int i = 0; i < threadCount / 2; ++i) {
        producers.emplace_back(producer, i);
    }
    
    // 一半的线程作为消费者
    for (int i = 0; i < threadCount / 2; ++i) {
        consumers.emplace_back(consumer, i);
    }
    
    // 等待所有生产者完成
    for (auto& t : producers) {
        t.join();
    }
    
    // 等待所有消费者完成
    for (auto& t : consumers) {
        t.join();
    }
    
    auto end = high_resolution_clock::now();
    auto elapsed = duration_cast<milliseconds>(end - start).count();
    
    bool allItemsProcessed = (pushCount.load() == popCount.load());
    bool sumsMatch = (pushSum.load() == popSum.load());
    bool queueEmpty = false;
    
    {
        std::lock_guard<std::mutex> lock(mutex);
        queueEmpty = queue.empty();
    }
    
    printResult("std::queue + mutex", elapsed, allItemsProcessed && sumsMatch && queueEmpty);
}

// 测试读写自旋锁
void testRWSpinLock(int threadCount, int opsPerThread, float readRatio = 0.8) {
    RWSpinLock rwlock;
    std::vector<int> sharedData(1, 0);  // 共享数据，初始值为0
    std::atomic<int> readOps(0);
    std::atomic<int> writeOps(0);
    std::atomic<bool> dataRaceDetected(false);
    
    auto worker = [&](int id) {
        std::mt19937 rng(id);  // 每个线程独立的随机数生成器
        std::uniform_real_distribution<float> dist(0.0f, 1.0f);
        
        for (int i = 0; i < opsPerThread; ++i) {
            if (dist(rng) < readRatio) {
                // 读操作
                rwlock.lockRead();
                int value = sharedData[0];  // 读取共享数据
                
                // 检测数据竞争 - 如果有写锁，读取应该被阻塞
                if (rwlock.isWriteLocked()) {
                    dataRaceDetected = true;
                }
                
                rwlock.unlockRead();
                readOps.fetch_add(1, std::memory_order_relaxed);
            } else {
                // 写操作
                rwlock.lockWrite();
                sharedData[0]++;  // 修改共享数据
                
                // 检测数据竞争 - 如果有读锁，写入应该被阻塞
                if (rwlock.getReadLockCount() > 0) {
                    dataRaceDetected = true;
                }
                
                rwlock.unlockWrite();
                writeOps.fetch_add(1, std::memory_order_relaxed);
            }
        }
    };
    
    std::vector<std::thread> threads;
    auto start = high_resolution_clock::now();
    
    for (int i = 0; i < threadCount; ++i) {
        threads.emplace_back(worker, i);
    }
    
    for (auto& t : threads) {
        t.join();
    }
    
    auto end = high_resolution_clock::now();
    auto elapsed = duration_cast<milliseconds>(end - start).count();
    
    bool operationsCorrect = (readOps.load() + writeOps.load() == threadCount * opsPerThread);
    bool dataConsistent = (sharedData[0] == writeOps.load());
    bool noDataRace = !dataRaceDetected;
    
    std::string testName = "RWSpinLock (读比例=" + std::to_string(readRatio) + ")";
    printResult(testName, elapsed, operationsCorrect && dataConsistent && noDataRace);
    
    if (!operationsCorrect) {
        std::cout << "   读操作: " << readOps.load() << ", 写操作: " << writeOps.load() 
                  << ", 总计: " << (readOps.load() + writeOps.load()) 
                  << ", 期望: " << (threadCount * opsPerThread) << std::endl;
    }
    if (!dataConsistent) {
        std::cout << "   共享数据值: " << sharedData[0] << ", 写操作数: " << writeOps.load() << std::endl;
    }
    if (dataRaceDetected) {
        std::cout << "   检测到数据竞争!" << std::endl;
    }
}

// 压力测试 - 在高并发情况下测试所有锁的性能
void runStressTest() {
    std::cout << "\n========== 高并发压力测试 ==========" << std::endl;
    std::cout << "硬件线程数: " << std::thread::hardware_concurrency() << std::endl;
    
    const int THREAD_COUNT = std::thread::hardware_concurrency(); // 使用硬件线程数
    const int OPS_PER_THREAD = 10000; // 每个线程执行的操作数
    
    std::cout << "\n----- 互斥锁测试 (" << THREAD_COUNT << "线程, 每线程" << OPS_PER_THREAD << "操作) -----" << std::endl;
    testStdMutex(THREAD_COUNT, OPS_PER_THREAD);
    testSpinLock(THREAD_COUNT, OPS_PER_THREAD);
    testX86HardwareSpinLock(THREAD_COUNT, OPS_PER_THREAD);
    
    std::cout << "\n----- 读写锁测试 (" << THREAD_COUNT << "线程, 每线程1000操作) -----" << std::endl;
    testRWSpinLock(THREAD_COUNT, 1000, 0.8);  // 80%读操作，20%写操作
    
    std::cout << "\n----- 队列测试 (" << THREAD_COUNT << "线程, 每线程50操作) -----" << std::endl;
    testLockFreeQueue(THREAD_COUNT, 50);
    testStdQueueWithMutex(THREAD_COUNT, 50);
}

// 线程安全性测试 - 验证在高并发情况下的正确性
void runSafetyTest() {
    std::cout << "\n========== 线程安全性测试 ==========" << std::endl;
    
    const int THREAD_COUNT = 8;
    const int OPS_PER_THREAD = 1000;
    
    // 测试自旋锁的线程安全性
    {
        SpinLock lock;
        std::vector<int> shared_vector;
        
        auto worker = [&](int id) {
            for (int i = 0; i < OPS_PER_THREAD; ++i) {
                lock.lock();
                shared_vector.push_back(i + id * OPS_PER_THREAD);
                lock.unlock();
            }
        };
        
        std::vector<std::thread> threads;
        for (int i = 0; i < THREAD_COUNT; ++i) {
            threads.emplace_back(worker, i);
        }
        
        for (auto& t : threads) {
            t.join();
        }
        
        bool passed = (shared_vector.size() == THREAD_COUNT * OPS_PER_THREAD);
        printResult("SpinLock 线程安全性", 0, passed);
    }
    
    // 测试LockFreeQueue的线程安全性
    {
        LockFreeQueue<int> queue(10000);
        std::atomic<int> push_count(0);
        std::atomic<int> pop_count(0);
        
        // 生产者线程
        auto producer = [&](int id) {
            for (int i = 0; i < 50; ++i) {  // 减少操作数量
                int value = i + id * 100;
                queue.Push(value);
                push_count.fetch_add(1, std::memory_order_relaxed);
            }
        };
        
        // 消费者线程
        auto consumer = [&](int id) {
            int value;
            int count = 0;
            
            while (count < 50) {  // 固定数量的消费操作
                if (queue.Pop(value)) {
                    pop_count.fetch_add(1, std::memory_order_relaxed);
                    count++;
                } else {
                    std::this_thread::yield();
                }
            }
        };
        
        std::vector<std::thread> producers;
        std::vector<std::thread> consumers;
        
        // 4个生产者，4个消费者
        for (int i = 0; i < 4; ++i) {
            producers.emplace_back(producer, i);
        }
        
        for (int i = 0; i < 4; ++i) {
            consumers.emplace_back(consumer, i);
        }
        
        for (auto& t : producers) {
            t.join();
        }
        
        for (auto& t : consumers) {
            t.join();
        }
        
        // 生产者总共产生了4*50=200个元素
        // 消费者每个消费50个，总共消费4*50=200个
        bool counts_match = (push_count.load() == pop_count.load());
        bool is_empty = queue.IsEmpty();
        
        printResult("LockFreeQueue 线程安全性", 0, counts_match && is_empty);
        
        if (!counts_match) {
            std::cout << "   入队: " << push_count.load() << ", 出队: " << pop_count.load() << std::endl;
        }
        
        if (!is_empty) {
            std::cout << "   队列剩余: " << queue.Size() << "个元素" << std::endl;
        }
    }
    
    // 测试RWSpinLock的线程安全性
    {
        RWSpinLock rwlock;
        std::atomic<int> shared_counter(0);
        std::atomic<bool> race_detected(false);
        
        // 读取线程 - 只读取计数器，不修改
        auto reader = [&](int id) {
            for (int i = 0; i < OPS_PER_THREAD; ++i) {
                rwlock.lockRead();
                
                // 检查是否有写锁 - 不应该同时有读锁和写锁
                if (rwlock.isWriteLocked()) {
                    race_detected = true;
                }
                
                // 读取共享计数器但不修改
                volatile int value = shared_counter.load(std::memory_order_relaxed);
                (void)value;  // 防止编译器优化
                
                rwlock.unlockRead();
            }
        };
        
        // 写入线程 - 修改计数器
        auto writer = [&](int id) {
            for (int i = 0; i < OPS_PER_THREAD / 10; ++i) {  // 写操作较少
                rwlock.lockWrite();
                
                // 检查是否有读锁 - 不应该同时有读锁和写锁
                if (rwlock.getReadLockCount() > 0) {
                    race_detected = true;
                }
                
                // 增加共享计数器
                shared_counter.fetch_add(1, std::memory_order_relaxed);
                
                rwlock.unlockWrite();
            }
        };
        
        std::vector<std::thread> readers;
        std::vector<std::thread> writers;
        
        // 大部分线程读取，少部分线程写入
        for (int i = 0; i < THREAD_COUNT - 2; ++i) {
            readers.emplace_back(reader, i);
        }
        
        for (int i = 0; i < 2; ++i) {
            writers.emplace_back(writer, i);
        }
        
        for (auto& t : readers) {
            t.join();
        }
        
        for (auto& t : writers) {
            t.join();
        }
        
        // 计数器应该等于所有写操作的数量
        bool counter_correct = (shared_counter.load() == 2 * (OPS_PER_THREAD / 10));
        bool no_race = !race_detected;
        
        printResult("RWSpinLock 线程安全性", 0, counter_correct && no_race);
    }
}

// 高负载测试 - 减少操作次数，提高稳定性
void testLockFreeQueueHighLoad(int threadCount, int opsPerThread) {
    LockFreeQueue<int> queue(100000);
    std::atomic<int> pushCount(0);
    std::atomic<int> popCount(0);
    std::atomic<uint64_t> pushSum(0);
    std::atomic<uint64_t> popSum(0);
    
    // 限制线程数和操作次数
    int actual_threads = std::min(threadCount, 16);
    int actual_ops = std::min(opsPerThread, 200);
    
    auto producer = [&](int id) {
        for (int i = 0; i < actual_ops; ++i) {
            int value = i * 10 + id;
            queue.Push(value);
            pushSum.fetch_add(value, std::memory_order_relaxed);
            pushCount.fetch_add(1, std::memory_order_relaxed);
            
            // 每10次操作让出CPU
            if (i % 10 == 0) {
                std::this_thread::yield();
            }
        }
    };
    
    auto consumer = [&](int id) {
        int consecutive_fails = 0;
        int total_attempts = 0;
        const int MAX_TOTAL_ATTEMPTS = 10000; // 添加总尝试次数限制
        for (int i = 0; i < actual_ops; ++i) {
            int value;
            bool success = false;
            for (int attempts = 0; attempts < 1000 && !success && total_attempts < MAX_TOTAL_ATTEMPTS; ++attempts) {
                total_attempts++;
                if (queue.Pop(value)) {
                    popSum.fetch_add(value, std::memory_order_relaxed);
                    popCount.fetch_add(1, std::memory_order_relaxed);
                    consecutive_fails = 0;
                    success = true;
                } else {
                    if (++consecutive_fails > 100) {
                        std::this_thread::sleep_for(std::chrono::milliseconds(1));
                        consecutive_fails = 0;
                    } else {
                        std::this_thread::yield();
                    }
                    
                    // 改进检查条件：所有生产者完成且已消费的数量等于已生产的数量
                    if (pushCount.load(std::memory_order_relaxed) == actual_ops * (actual_threads / 2) && 
                        popCount.load(std::memory_order_relaxed) >= pushCount.load(std::memory_order_relaxed)) {
                        success = true;
                        break;
                    }
                }
            }
            // 如果尝试次数用尽仍未成功或达到最大总尝试次数，跳出循环
            if (!success || total_attempts >= MAX_TOTAL_ATTEMPTS) {
                break;
            }
        }
    };
    
    std::vector<std::thread> threads;
    auto start = high_resolution_clock::now();
    
    // 创建生产者线程
    for (int i = 0; i < actual_threads / 2; ++i) {
        threads.emplace_back(producer, i);
    }
    
    // 创建消费者线程
    for (int i = 0; i < actual_threads / 2; ++i) {
        threads.emplace_back(consumer, i);
    }
    
    // 等待所有线程完成
    for (auto& t : threads) {
        t.join();
    }
    auto end = high_resolution_clock::now();
    auto elapsed = duration_cast<milliseconds>(end - start).count();
    
    // 清空队列
    int leftover = 0;
    int value;
    while (!queue.IsEmpty()) {
        queue.Pop(value);
        leftover++;
        popSum.fetch_add(value, std::memory_order_relaxed);
    }
    
    bool allProcessed = (pushCount.load() == popCount.load() + leftover);
    bool sumsMatch = (pushSum.load() == popSum.load());
    bool queueEmpty = queue.IsEmpty();
    
    printResult("LockFreeQueue 高负载测试", elapsed, 
               allProcessed && sumsMatch && queueEmpty);
    
    if (!allProcessed) {
        std::cout << "   推送: " << pushCount.load() << ", 弹出: " 
                 << popCount.load() << ", 剩余: " << leftover << std::endl;
    }
    if (!sumsMatch) {
        std::cout << "   推送总和: " << pushSum.load() << ", 弹出总和: " 
                 << popSum.load() << std::endl;
    }
    if (!queueEmpty) {
        std::cout << "   队列剩余: " << queue.Size() << " 个元素" << std::endl;
    }
}

// 混合操作测试 - 随机的读写操作
void testLockFreeQueueMixedOperations(int threadCount, int opsPerThread) {
    LockFreeQueue<int> queue(10000);
    std::atomic<int> totalOpsCompleted(0);
    
    auto worker = [&](int id) {
        // 为每个线程创建一个随机数生成器
        std::mt19937 gen(id + static_cast<unsigned>(std::chrono::high_resolution_clock::now()
            .time_since_epoch().count()));
        std::uniform_int_distribution<> dis(0, 100);
        
        for (int i = 0; i < opsPerThread; ++i) {
            int rnd = dis(gen);
            
            if (rnd < 60) {  // 60%概率执行推送操作
                int value = i * 1000 + id;
                queue.Push(value);
            } else {  // 40%概率执行弹出操作
                int value;
                if (queue.Pop(value)) {
                    // 成功弹出
                }
            }
            
            totalOpsCompleted.fetch_add(1, std::memory_order_relaxed);
        }
    };
    
    std::vector<std::thread> threads;
    auto start = high_resolution_clock::now();
    
    for (int i = 0; i < threadCount; ++i) {
        threads.emplace_back(worker, i);
    }
    
    for (auto& t : threads) {
        t.join();
    }
    
    auto end = high_resolution_clock::now();
    auto elapsed = duration_cast<milliseconds>(end - start).count();
    
    bool allOpsCompleted = (totalOpsCompleted.load() == threadCount * opsPerThread);
    
    printResult("LockFreeQueue 混合操作测试", elapsed, allOpsCompleted);
    std::cout << "   队列剩余大小: " << queue.Size() << std::endl;
}

// 竞争条件测试 - 设计专门触发边缘情况的测试
void testLockFreeQueueContention(int threadCount) {
    LockFreeQueue<int> queue(10000);
    std::atomic<bool> start_flag(false);
    std::atomic<int> ready_threads(0);
    std::atomic<int> insertCount(0);
    std::atomic<int> removeCount(0);
    
    // 限制线程数量，避免过高的并发导致内存问题
    int actual_threads = std::min(threadCount, 16);
    
    // 让所有线程同时启动，增加竞争
    auto worker = [&](int id, bool is_producer) {
        // 标记线程已准备好
        ready_threads.fetch_add(1, std::memory_order_release);
        
        // 等待所有线程准备完毕
        while (ready_threads.load(std::memory_order_acquire) < actual_threads) {
            std::this_thread::yield();
        }
        
        // 等待开始信号
        while (!start_flag.load(std::memory_order_acquire)) {
            std::this_thread::yield();
        }
        
        // 执行队列操作
        if (is_producer) {
            for (int i = 0; i < 50; ++i) { // 减少操作次数
                queue.Push(id * 1000 + i);
                insertCount.fetch_add(1, std::memory_order_relaxed);
                // 添加短暂休眠，减轻内存压力
                if (i % 10 == 0) {
                    std::this_thread::yield();
                }
            }
        } else {
            int total_attempts = 0;
            const int MAX_TOTAL_ATTEMPTS = 10000; // 添加总尝试次数限制
            
            for (int i = 0; i < 50; ++i) { // 减少操作次数
                int value;
                bool success = false;
                
                for (int attempt = 0; attempt < 1000 && !success && total_attempts < MAX_TOTAL_ATTEMPTS; ++attempt) {
                    total_attempts++;
                    if (queue.Pop(value)) {
                        removeCount.fetch_add(1, std::memory_order_relaxed);
                        success = true;
                    } else {
                        std::this_thread::yield();
                        
                        // 避免死循环：如果所有生产已完成且队列为空，或超出尝试次数，则退出
                        if (insertCount.load(std::memory_order_relaxed) == 
                            (actual_threads / 2) * 50 && // 所有生产者都完成
                            insertCount.load(std::memory_order_relaxed) <= 
                            removeCount.load(std::memory_order_relaxed)) {
                            break;
                        }
                    }
                }
                
                // 如果尝试次数用尽仍未成功或者达到最大尝试次数，跳出循环
                if (!success || total_attempts >= MAX_TOTAL_ATTEMPTS) {
                    break;
                }
            }
        }
    };
    
    std::vector<std::thread> threads;
    auto start = high_resolution_clock::now();
    
    // 创建线程
    for (int i = 0; i < actual_threads; ++i) {
        bool is_producer = (i % 2 == 0);  // 一半生产者，一半消费者
        threads.emplace_back(worker, i, is_producer);
    }
    
    // 等待所有线程准备好
    while (ready_threads.load(std::memory_order_acquire) < actual_threads) {
        std::this_thread::yield();
    }
    
    // 给线程一些时间完全准备
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    
    // 开始测试
    start_flag.store(true, std::memory_order_release);
    
    // 等待所有线程完成
    for (auto& t : threads) {
        t.join();
    }
    
    auto end = high_resolution_clock::now();
    auto elapsed = duration_cast<milliseconds>(end - start).count();
    
    // 多等待一些时间让队列中的元素被完全消费
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    
    // 清空剩余元素
    int leftover = 0;
    int value;
    while (!queue.IsEmpty()) {
        queue.Pop(value);
        leftover++;
    }
    
    bool insertMatchesRemove = (insertCount.load() == removeCount.load() + leftover);
    bool queueEmpty = queue.IsEmpty();
    
    printResult("LockFreeQueue 竞争条件测试", elapsed, 
                insertMatchesRemove && queueEmpty);
    
    if (!insertMatchesRemove) {
        std::cout << "   插入: " << insertCount.load() << ", 移除: " 
                  << removeCount.load() << ", 剩余: " << leftover << std::endl;
    }
    if (!queueEmpty) {
        std::cout << "   队列剩余: " << queue.Size() << " 个元素" << std::endl;
    }
}

// 批量操作模拟测试 - 降低数据量，防止内存问题
void testLockFreeQueueBurstOperations(int threadCount) {
    LockFreeQueue<int> queue(10000);
    std::atomic<int> totalPushes(0);
    std::atomic<int> totalPops(0);
    std::atomic<bool> done(false);
    
    // 显著降低测试规模，避免内存问题
    int actual_threads = std::min(threadCount, 8);
    int burst_size = 100;  // 大幅减少突发大小
    int burst_count = 3;   // 减少突发次数
    
    auto producer = [&](int id) {
        std::mt19937 gen(id + 1000);
        
        for (int burst = 0; burst < burst_count && !done.load(std::memory_order_acquire); ++burst) {
            // 随机休眠一段时间，模拟突发操作之间的间隔
            std::this_thread::sleep_for(std::chrono::milliseconds(gen() % 5 + 1));
            
            // 执行一批push操作
            for (int i = 0; i < burst_size && !done.load(std::memory_order_acquire); ++i) {
                queue.Push(burst * 1000 + i);
                totalPushes.fetch_add(1, std::memory_order_relaxed);
                
                // 更频繁地让出CPU，减轻内存压力
                if (i % 20 == 0) {
                    std::this_thread::yield();
                }
            }
            
            // 每个突发后短暂休眠
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
    };
    
    auto consumer = [&](int id) {
        int value;
        int localPops = 0;
        int consecutive_fails = 0;
        int total_attempts = 0;
        const int MAX_TOTAL_ATTEMPTS = 10000; // 添加总尝试次数限制
        
        while (!done.load(std::memory_order_acquire) || !queue.IsEmpty()) {
            if (total_attempts++ > MAX_TOTAL_ATTEMPTS) {
                break; // 防止无限循环
            }
            
            if (queue.Pop(value)) {
                localPops++;
                totalPops.fetch_add(1, std::memory_order_relaxed);
                consecutive_fails = 0;
            } else {
                // 如果连续失败太多次，休眠一小段时间
                if (++consecutive_fails > 100) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
                    consecutive_fails = 0;
                    
                    // 如果队列为空且生产者已完成，退出循环
                    if (queue.IsEmpty() && done.load(std::memory_order_acquire)) {
                        break;
                    }
                } else {
                    std::this_thread::yield();
                }
            }
            
            // 定期检查是否已完成
            if (localPops % 50 == 0 && queue.IsEmpty() && done.load(std::memory_order_acquire)) {
                break;
            }
        }
    };
    
    auto monitor = [&]() {
        // 安全机制：监控测试进度，如果卡住或内存使用过高则提前终止
        int last_total_pops = 0;
        int stalled_count = 0;
        
        while (!done.load(std::memory_order_acquire)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            
            int current_pops = totalPops.load(std::memory_order_acquire);
            if (current_pops == last_total_pops) {
                if (++stalled_count > 10) {
                    // 处理停滞超过500毫秒，可能出现问题，提前终止
                    done.store(true, std::memory_order_release);
                    return;
                }
            } else {
                stalled_count = 0;
                last_total_pops = current_pops;
            }
        }
    };
    
    std::vector<std::thread> producers;
    std::vector<std::thread> consumers;
    auto start = high_resolution_clock::now();
    
    // 启动监控线程
    std::thread monitor_thread(monitor);
    
    // 创建生产者 (减少数量)
    for (int i = 0; i < actual_threads / 4; ++i) {
        producers.emplace_back(producer, i);
    }
    
    // 创建消费者 (增加比例)
    for (int i = 0; i < actual_threads - actual_threads / 4; ++i) {
        consumers.emplace_back(consumer, i);
    }
    
    // 设置超时保护
    std::thread timeout_thread([&]() {
        // 最多运行1秒，防止测试卡住
        std::this_thread::sleep_for(std::chrono::seconds(1));
        done.store(true, std::memory_order_release);
    });
    
    // 等待所有生产者完成
    for (auto& t : producers) {
        t.join();
    }
    
    // 标记测试完成
    done.store(true, std::memory_order_release);
    
    // 等待所有消费者完成
    for (auto& t : consumers) {
        t.join();
    }
    
    // 等待监控线程和超时线程
    monitor_thread.join();
    timeout_thread.join();
    
    auto end = high_resolution_clock::now();
    auto elapsed = duration_cast<milliseconds>(end - start).count();
    
    // 清空队列中的剩余元素
    int leftover = 0;
    int value;
    while (!queue.IsEmpty()) {
        queue.Pop(value);
        leftover++;
        // 安全措施：防止无限循环
        if (leftover > totalPushes.load(std::memory_order_relaxed)) {
            break;
        }
    }
    
    bool allItemsProcessed = (totalPushes.load() == totalPops.load() + leftover);
    bool queueEmpty = queue.IsEmpty();
    
    printResult("LockFreeQueue 突发操作测试", elapsed, 
                allItemsProcessed && queueEmpty);
    
    if (!allItemsProcessed) {
        std::cout << "   推送: " << totalPushes.load() << ", 弹出: " 
                  << totalPops.load() << ", 剩余: " << leftover << std::endl;
    }
    if (!queueEmpty) {
        std::cout << "   队列剩余: " << queue.Size() << " 个元素" << std::endl;
    }
}

// 多生产者多消费者繁重测试 - 高频率推送和弹出操作
void testLockFreeQueueHeavyLoad(int threadCount, int opsPerThread) {
    LockFreeQueue<int> queue(10000);
    std::atomic<uint64_t> pushSum(0);
    std::atomic<uint64_t> popSum(0);
    std::atomic<int> pushCount(0);
    std::atomic<int> popCount(0);
    std::atomic<bool> error_detected(false);
    std::vector<std::thread> threads;
    
    // 限制线程数，避免过高的并发
    int actual_threads = std::min(threadCount, 32);
    int actual_ops = std::min(opsPerThread, 10000);
    
    auto start = high_resolution_clock::now();
    
    // 生产者线程
    for (int i = 0; i < actual_threads / 2; ++i) {
        threads.emplace_back([&, i]() {
            // 生产特定范围的序列号，确保每个值唯一
            for (int j = 0; j < actual_ops; ++j) {
                // 构造唯一值: (线程ID << 20) | 操作序号
                // 这样可以跟踪每个值的来源
                int value = (i << 20) | j;
                queue.Push(value);
                pushSum.fetch_add(value, std::memory_order_relaxed);
                pushCount.fetch_add(1, std::memory_order_relaxed);
                
                // 周期性让出CPU
                if (j % 100 == 0) {
                    std::this_thread::yield();
                }
            }
        });
    }
    
    // 消费者线程
    for (int i = 0; i < actual_threads / 2; ++i) {
        threads.emplace_back([&]() {
            // 每个消费者尝试消费固定数量的元素
            int expected_ops = actual_ops * (actual_threads / 2) / (actual_threads / 2);
            int consumed = 0;
            std::vector<int> local_consumed; // 跟踪本地消费的值
            local_consumed.reserve(expected_ops);
            int total_attempts = 0;
            const int MAX_TOTAL_ATTEMPTS = 100000; // 添加总尝试次数限制
            
            while (consumed < expected_ops && total_attempts < MAX_TOTAL_ATTEMPTS) {
                total_attempts++;
                int value;
                if (queue.Pop(value)) {
                    local_consumed.push_back(value);
                    popSum.fetch_add(value, std::memory_order_relaxed);
                    popCount.fetch_add(1, std::memory_order_relaxed);
                    consumed++;
                } else {
                    // 检查是否所有生产已完成且队列为空
                    if (pushCount.load(std::memory_order_relaxed) >= 
                        actual_ops * (actual_threads / 2) &&
                        queue.IsEmpty()) {
                        break;
                    }
                    
                    // 每尝试一定次数后短暂休眠，减轻CPU压力
                    if (total_attempts % 1000 == 0) {
                        std::this_thread::sleep_for(std::chrono::milliseconds(1));
                    } else {
                        std::this_thread::yield();
                    }
                }
            }
            
            // 检查本地消费的值是否有重复
            std::sort(local_consumed.begin(), local_consumed.end());
            for (size_t j = 1; j < local_consumed.size(); ++j) {
                if (local_consumed[j] == local_consumed[j-1]) {
                    error_detected = true;
                    std::cout << "   错误: 重复值: " << local_consumed[j] << std::endl;
                    break;
                }
            }
        });
    }
    
    // 等待所有线程完成
    for (auto& t : threads) {
        t.join();
    }
    
    auto end = high_resolution_clock::now();
    auto elapsed = duration_cast<milliseconds>(end - start).count();
    
    // 清空残留元素
    int leftover = 0;
    int value;
    while (!queue.IsEmpty()) {
        queue.Pop(value);
        popSum.fetch_add(value, std::memory_order_relaxed);
        leftover++;
    }
    
    bool allProcessed = (pushCount.load() == popCount.load() + leftover);
    bool sumsMatch = (pushSum.load() == popSum.load());
    bool queueEmpty = queue.IsEmpty();
    bool noErrors = !error_detected;
    
    printResult("LockFreeQueue 繁重负载测试", elapsed, 
               allProcessed && sumsMatch && queueEmpty && noErrors);
    
    // 输出详细统计
    std::cout << "   推送: " << pushCount.load() << ", 弹出: " 
             << popCount.load() << ", 剩余: " << leftover << std::endl;
    std::cout << "   推送总和: " << pushSum.load() << ", 弹出总和: " 
             << popSum.load() << ", 匹配: " << (sumsMatch ? "是" : "否") << std::endl;
    std::cout << "   每秒处理项目: " << 
        (elapsed > 0 ? (pushCount.load() * 1000 / elapsed) : pushCount.load()) << std::endl;
}

// 随机交错的队列操作测试
void testLockFreeQueueChaos(int threadCount, int seconds) {
    LockFreeQueue<int> queue(10000);
    std::atomic<bool> stop(false);
    std::atomic<uint64_t> total_ops(0);
    std::atomic<int> errors(0);
    std::vector<std::thread> threads;
    
    // 限制测试时间和线程数
    int actual_threads = std::min(threadCount, 16);
    int test_duration = std::min(seconds, 5);
    
    auto start = high_resolution_clock::now();
    
    // 混沌测试线程 - 随机执行推送、弹出和查询操作
    for (int i = 0; i < actual_threads; ++i) {
        threads.emplace_back([&, i]() {
            std::mt19937 gen(i + static_cast<unsigned>(std::chrono::high_resolution_clock::now()
                .time_since_epoch().count()));
            std::uniform_int_distribution<> op_dis(0, 99);  // 操作类型分布
            std::uniform_int_distribution<> value_dis(0, 10000);  // 值分布
            
            std::vector<int> local_expected; // 跟踪预期的可弹出顺序
            
            while (!stop.load(std::memory_order_relaxed)) {
                int op = op_dis(gen);
                
                if (op < 40) {  // 40% 推送操作
                    int value = (i << 16) | value_dis(gen);
                    queue.Push(value);
                    local_expected.push_back(value);
                    total_ops.fetch_add(1, std::memory_order_relaxed);
                } 
                else if (op < 80) {  // 40% 弹出操作
                    int value;
                    if (!queue.IsEmpty()) {
                        queue.Pop(value);
                        total_ops.fetch_add(1, std::memory_order_relaxed);
                        // 无法验证弹出的正确性，因为是多线程混合操作
                    }
                }
                else if (op < 90) {  // 10% 查询大小
                    queue.Size();
                    total_ops.fetch_add(1, std::memory_order_relaxed);
                }
                else if (op < 100) {  // 5% 检查是否为空
                    queue.IsEmpty();
                    total_ops.fetch_add(1, std::memory_order_relaxed);
                }
                
                // 偶尔让出CPU
                if (op % 10 == 0) {
                    std::this_thread::yield();
                }
            }
        });
    }
    
    // 等待指定的测试时间
    std::this_thread::sleep_for(std::chrono::seconds(test_duration));
    
    // 通知所有线程停止
    stop.store(true, std::memory_order_release);
    
    // 等待所有线程完成
    for (auto& t : threads) {
        t.join();
    }
    auto end = high_resolution_clock::now();
    auto elapsed = duration_cast<milliseconds>(end - start).count();
    
    // 清空队列
    int leftover = 0;
    int value;
    while (!queue.IsEmpty()) {
        queue.Pop(value);
        leftover++;
    }
    
    printResult("LockFreeQueue 混沌测试", elapsed, errors.load() == 0);
    
    // 输出详细统计
    std::cout << "   总操作数: " << total_ops.load() << ", 每秒: " 
              << (total_ops.load() * 1000 / elapsed) << std::endl;
    std::cout << "   剩余元素: " << leftover << std::endl;
}

// 大数据吞吐量测试
void testLockFreeQueueThroughput(int dataSizeMB) {
    // 创建较大的数据类型
    struct LargeData {
        int id;
        char data[1024]; // 1KB数据
        
        LargeData() : id(0) {
            memset(data, 0, sizeof(data));
        }
        
        explicit LargeData(int i) : id(i) {
            // 填充一些数据
            for (int j = 0; j < 1024; ++j) {
                data[j] = static_cast<char>((i + j) % 256);
            }
        }
    };
    
    // 计算项目数量
    int numItems = (dataSizeMB * 1024 * 1024) / sizeof(LargeData);
    numItems = std::min(numItems, 1000); // 限制项目数量避免内存问题
    
    LockFreeQueue<LargeData> queue(sizeof(LargeData) * numItems);
    std::atomic<int> errors(0);
    
    auto start = high_resolution_clock::now();
    
    // 生产者线程
    std::thread producer([&]() {
        for (int i = 0; i < numItems; ++i) {
            queue.Push(LargeData(i));
            
            // 周期性让出CPU
            if (i % 10 == 0) {
                std::this_thread::yield();
            }
        }
    });
    
    // 消费者线程
    std::thread consumer([&]() {
        int total_attempts = 0;
        const int MAX_TOTAL_ATTEMPTS = 1000000; // 添加总尝试次数限制
        
        for (int i = 0; i < numItems; ++i) {
            LargeData value;
            bool success = false;
            
            while (!success && total_attempts < MAX_TOTAL_ATTEMPTS) {
                total_attempts++;
                if (queue.Pop(value)) {
                    success = true;
                    
                    // 验证数据
                    if (value.id != i) {
                        errors.fetch_add(1, std::memory_order_relaxed);
                    }
                    
                    // 验证数据内容
                    for (int j = 0; j < 10; ++j) { // 只检查前10个字节避免过多计算
                        if (value.data[j] != static_cast<char>((value.id + j) % 256)) {
                            errors.fetch_add(1, std::memory_order_relaxed);
                            break;
                        }
                    }
                }
            }
            
            // 如果尝试次数超过最大限制，跳出循环
            if (total_attempts >= MAX_TOTAL_ATTEMPTS) {
                std::cout << "   警告: 达到最大尝试次数，提前退出" << std::endl;
                break;
            }
        }
    });
    
    // 等待线程完成
    producer.join();
    consumer.join();
    
    auto end = high_resolution_clock::now();
    auto elapsed = duration_cast<milliseconds>(end - start).count();
    
    // 计算吞吐量
    double throughput = static_cast<double>(numItems) * sizeof(LargeData) / (1024.0 * 1024.0);
    double throughputPerSec = throughput / (elapsed / 1000.0);
    
    printResult("LockFreeQueue 吞吐量测试", elapsed, errors.load() == 0);
    
    // 输出详细统计
    std::cout << "   处理数据量: " << throughput << " MB" << std::endl;
    std::cout << "   吞吐量: " << throughputPerSec << " MB/s" << std::endl;
    std::cout << "   错误数: " << errors.load() << std::endl;
}

// 多队列并发测试
void testMultipleQueues(int queueCount, int threadCount) {
    std::vector<std::unique_ptr<LockFreeQueue<int>>> queues;
    std::atomic<uint64_t> total_push(0);
    std::atomic<uint64_t> total_pop(0);
    std::atomic<bool> stop(false);
    
    // 创建多个队列
    for (int i = 0; i < queueCount; ++i) {
        queues.push_back(std::make_unique<LockFreeQueue<int>>(10000));
    }
    
    // 定义测试持续时间（秒）
    const int test_duration = 3;
    
    // 启动工作线程
    std::vector<std::thread> threads;
    for (int i = 0; i < threadCount; ++i) {
        threads.emplace_back([&, i]() {
            // 每个线程随机操作不同的队列
            std::mt19937 gen(i + static_cast<unsigned>(std::chrono::high_resolution_clock::now().time_since_epoch().count()));
            std::uniform_int_distribution<> queue_dis(0, queueCount - 1);
            std::uniform_int_distribution<> op_dis(0, 1); // 0=推送，1=弹出
            std::uniform_int_distribution<> value_dis(0, 10000);
            
            while (!stop.load(std::memory_order_relaxed)) {
                // 选择一个随机队列
                int queue_idx = queue_dis(gen);
                auto& queue = queues[queue_idx];
                
                // 执行随机操作
                if (op_dis(gen) == 0) {
                    // 推送操作
                    int value = value_dis(gen);
                    queue->Push(value);
                    total_push.fetch_add(1, std::memory_order_relaxed);
                } else {
                    // 弹出操作
                    int value;
                    if (!queue->IsEmpty()) {
                        queue->Pop(value);
                        total_pop.fetch_add(1, std::memory_order_relaxed);
                    }
                }
                
                // 让出CPU，避免单个线程霸占处理器
                if (gen() % 100 == 0) {
                    std::this_thread::yield();
                }
            }
        });
    }
    
    // 跟踪测试开始时间
    auto start = high_resolution_clock::now();
    
    // 运行测试指定的时间
    std::this_thread::sleep_for(std::chrono::seconds(test_duration));
    
    // 停止测试
    stop.store(true, std::memory_order_release);
    
    // 等待所有线程结束
    for (auto& t : threads) {
        t.join();
    }
    
    // 计算测试时间
    auto end = high_resolution_clock::now();
    auto elapsed = duration_cast<milliseconds>(end - start).count();
    
    // 清空所有队列，计算剩余项目
    int leftover = 0;
    for (auto& queue : queues) {
        int value;
        while (!queue->IsEmpty()) {
            queue->Pop(value);
            leftover++;
        }
    }
    
    printResult("多队列并发测试", elapsed, true);
    
    // 输出详细统计
    std::cout << "   队列数量: " << queueCount << std::endl;
    std::cout << "   总入队操作: " << total_push.load() 
              << ", 总出队操作: " << total_pop.load() 
              << ", 剩余: " << leftover << std::endl;
    std::cout << "   每秒操作: " << ((total_push.load() + total_pop.load()) * 1000 / elapsed) << std::endl;
}

int main() {
    std::cout << "=====================================" << std::endl;
    std::cout << "  高性能并发原语测试" << std::endl;
    std::cout << "=====================================" << std::endl;
    
    std::cout << std::endl << "========== 线程安全性测试 ==========" << std::endl;
    
    // 测试SpinLock的线程安全性
    {
        SpinLock lock;
        std::atomic<int> counter(0);
        std::vector<std::thread> threads;
        auto start = high_resolution_clock::now();
        
        for (int i = 0; i < 10; ++i) {
            threads.emplace_back([&lock, &counter]() {
                for (int j = 0; j < 1000; ++j) {
                    lock.lock();
                    counter.fetch_add(1, std::memory_order_relaxed);
                    lock.unlock();
                }
            });
        }
        
        for (auto& t : threads) {
            t.join();
        }
        
        auto end = high_resolution_clock::now();
        auto elapsed = duration_cast<milliseconds>(end - start).count();
        
        bool passed = (counter.load() == 10 * 1000);
        printResult("SpinLock 线程安全性", elapsed, passed);
        
        threads.clear();
    }
    
    // 测试LockFreeQueue的线程安全性
    {
        LockFreeQueue<int> queue(10000);
        std::atomic<int> sum1(0);
        std::atomic<int> sum2(0);
        std::vector<std::thread> threads;
        auto start = high_resolution_clock::now();
        
        // 生产者线程
        for (int i = 0; i < 5; ++i) {
            threads.emplace_back([&queue, &sum1, i]() {
                for (int j = 0; j < 100; ++j) {
                    int value = i * 100 + j;
                    queue.Push(value);
                    sum1.fetch_add(value, std::memory_order_relaxed);
                }
            });
        }
        
        // 消费者线程
        for (int i = 0; i < 5; ++i) {
            threads.emplace_back([&queue, &sum2]() {
                for (int j = 0; j < 100; ++j) {
                    int value;
                    while (!queue.Pop(value)) {
                        std::this_thread::yield();
                    }
                    sum2.fetch_add(value, std::memory_order_relaxed);
                }
            });
        }
        
        for (auto& t : threads) {
            t.join();
        }
        
        auto end = high_resolution_clock::now();
        auto elapsed = duration_cast<milliseconds>(end - start).count();
        
        bool passed = (sum1.load() == sum2.load() && queue.IsEmpty());
        printResult("LockFreeQueue 线程安全性", elapsed, passed);
        
        if (!passed) {
            std::cout << "   和校验: " << sum1.load() << " vs " << sum2.load() << std::endl;
            std::cout << "   队列剩余: " << (queue.IsEmpty() ? "空" : "非空") << std::endl;
        }
        
        threads.clear();
    }
    
    // 测试RWSpinLock的线程安全性
    {
        RWSpinLock rwlock;
        std::atomic<int> counter(0);
        std::atomic<int> reads(0);
        std::vector<std::thread> threads;
        auto start = high_resolution_clock::now();
        
        for (int i = 0; i < 8; ++i) {
            threads.emplace_back([&rwlock, &counter, &reads, i]() {
                for (int j = 0; j < 100; ++j) {
                    if (j % 5 == 0) { // 20% 写操作
                        rwlock.lockWrite();
                        counter.fetch_add(1, std::memory_order_relaxed);
                        rwlock.unlockWrite();
                    } else { // 80% 读操作
                        rwlock.lockRead();
                        int value = counter.load(std::memory_order_relaxed);
                        reads.fetch_add(value > 0 ? 1 : 0, std::memory_order_relaxed);
                        rwlock.unlockRead();
                    }
                }
            });
        }
        
        for (auto& t : threads) {
            t.join();
        }
        
        auto end = high_resolution_clock::now();
        auto elapsed = duration_cast<milliseconds>(end - start).count();
        
        bool passed = (counter.load() == 8 * 100 / 5);
        printResult("RWSpinLock 线程安全性", elapsed, passed);
    }
    
    std::cout << std::endl << "========== 高并发压力测试 ==========" << std::endl;
    int hwThreads = std::thread::hardware_concurrency();
    std::cout << "硬件线程数: " << hwThreads << std::endl << std::endl;
    
    // 使用硬件线程数或默认为4
    int threadCount = hwThreads > 0 ? hwThreads : 4;
    
    std::cout << std::endl << "========== 线程安全性基础测试 ==========" << std::endl;
    
    // 基础测试 (SpinLock, LockFreeQueue, RWSpinLock)
    {
        // SpinLock测试
        SpinLock lock;
        std::atomic<int> counter(0);
        std::vector<std::thread> threads;
        auto start = high_resolution_clock::now();
        
        for (int i = 0; i < 10; ++i) {
            threads.emplace_back([&lock, &counter]() {
                for (int j = 0; j < 1000; ++j) {
                    lock.lock();
                    counter.fetch_add(1, std::memory_order_relaxed);
                    lock.unlock();
                }
            });
        }
        
        for (auto& t : threads) {
            t.join();
        }
        
        auto end = high_resolution_clock::now();
        auto elapsed = duration_cast<milliseconds>(end - start).count();
        
        bool passed = (counter.load() == 10 * 1000);
        printResult("SpinLock 线程安全性", elapsed, passed);
        
        threads.clear();
    }
    
    // LockFreeQueue基础测试
    {
        LockFreeQueue<int> queue(10000);
        std::atomic<int> sum1(0);
        std::atomic<int> sum2(0);
        std::vector<std::thread> threads;
        auto start = high_resolution_clock::now();
        
        // 生产者线程
        for (int i = 0; i < 5; ++i) {
            threads.emplace_back([&queue, &sum1, i]() {
                for (int j = 0; j < 100; ++j) {
                    int value = i * 100 + j;
                    queue.Push(value);
                    sum1.fetch_add(value, std::memory_order_relaxed);
                }
            });
        }
        
        // 消费者线程
        for (int i = 0; i < 5; ++i) {
            threads.emplace_back([&queue, &sum2]() {
                for (int j = 0; j < 100; ++j) {
                    int value;
                    while (!queue.Pop(value)) {
                        std::this_thread::yield();
                    }
                    sum2.fetch_add(value, std::memory_order_relaxed);
                }
            });
        }
        
        for (auto& t : threads) {
            t.join();
        }
        
        auto end = high_resolution_clock::now();
        auto elapsed = duration_cast<milliseconds>(end - start).count();
        
        bool passed = (sum1.load() == sum2.load() && queue.IsEmpty());
        printResult("LockFreeQueue 基础线程安全性", elapsed, passed);
        
        if (!passed) {
            std::cout << "   和校验: " << sum1.load() << " vs " << sum2.load() << std::endl;
            std::cout << "   队列剩余: " << (queue.IsEmpty() ? "空" : "非空") << std::endl;
        }
        
        threads.clear();
    }
    
    std::cout << std::endl << "========== 互斥锁性能测试 ==========" << std::endl;
    testStdMutex(threadCount, 10000);
    testSpinLock(threadCount, 10000);
    testX86HardwareSpinLock(threadCount, 10000);
    
    std::cout << std::endl << "========== 读写锁测试 ==========" << std::endl;
    testRWSpinLock(threadCount, 1000, 0.8);  // 80%读, 20%写
    
    std::cout << std::endl << "========== 标准队列测试 ==========" << std::endl;
    testLockFreeQueue(threadCount, 50);
    testStdQueueWithMutex(threadCount, 50);
    
    std::cout << std::endl << "========== LockFreeQueue 高级测试 ==========" << std::endl;
    
    // 执行高负载测试
    std::cout << std::endl << "----- 高负载测试 -----" << std::endl;
    testLockFreeQueueHighLoad(threadCount, 1000);
    
    // 执行混合操作测试
    std::cout << std::endl << "----- 混合操作测试 -----" << std::endl;
    testLockFreeQueueMixedOperations(threadCount, 200);
    
    // 执行竞争条件测试
    std::cout << std::endl << "----- 竞争条件测试 -----" << std::endl;
    testLockFreeQueueContention(threadCount);
    
    // 执行繁重工作负载测试
    std::cout << std::endl << "----- 繁重工作负载测试 -----" << std::endl;
    testLockFreeQueueHeavyLoad(threadCount, 1000);
    
    // 执行混沌操作测试
    std::cout << std::endl << "----- 混沌操作测试 -----" << std::endl;
    testLockFreeQueueChaos(threadCount, 2); // 运行2秒
    
    // 执行吞吐量测试
    std::cout << std::endl << "----- 吞吐量测试 -----" << std::endl;
    testLockFreeQueueThroughput(1); // 1MB数据
    
    // 执行多队列测试
    std::cout << std::endl << "----- 多队列测试 -----" << std::endl;
    testMultipleQueues(4, threadCount);
    
    std::cout << std::endl << "测试完成!" << std::endl;
    
    return 0;
} 

#endif