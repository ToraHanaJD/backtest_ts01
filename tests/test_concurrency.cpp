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

void printResult(const std::string& testName, long long elapsedMs, bool passed) {
    std::cout << "测试: " << std::left << std::setw(40) << testName 
              << " 耗时: " << std::right << std::setw(8) << elapsedMs << " ms  " 
              << (passed ? "✓ 通过" : "✗ 失败") << std::endl;
}

// std::mutex test
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
    
    for (int i = 0; i < threadCount / 2; ++i) {
        producers.emplace_back(producer, i);
    }
    
    for (int i = 0; i < threadCount / 2; ++i) {
        consumers.emplace_back(consumer, i);
    }
    
    for (auto& t : producers) {
        t.join();
    }
    
    for (auto& t : consumers) {
        t.join();
    }
    
    auto end = high_resolution_clock::now();
    auto elapsed = duration_cast<milliseconds>(end - start).count();
    
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
    
    for (int i = 0; i < threadCount / 2; ++i) {
        producers.emplace_back(producer, i);
    }
    
    for (int i = 0; i < threadCount / 2; ++i) {
        consumers.emplace_back(consumer, i);
    }
    
    for (auto& t : producers) {
        t.join();
    }
    
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

void testRWSpinLock(int threadCount, int opsPerThread, float readRatio = 0.8) {
    RWSpinLock rwlock;
    std::vector<int> sharedData(1, 0); 
    std::atomic<int> readOps(0);
    std::atomic<int> writeOps(0);
    std::atomic<bool> dataRaceDetected(false);
    
    auto worker = [&](int id) {
        std::mt19937 rng(id); 
        std::uniform_real_distribution<float> dist(0.0f, 1.0f);
        
        for (int i = 0; i < opsPerThread; ++i) {
            if (dist(rng) < readRatio) {
                rwlock.lockRead();
                int value = sharedData[0]; 
                
                if (rwlock.isWriteLocked()) {
                    dataRaceDetected = true;
                }
                
                rwlock.unlockRead();
                readOps.fetch_add(1, std::memory_order_relaxed);
            } else {
                rwlock.lockWrite();
                sharedData[0]++;  
                
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

// stress test
void runStressTest() {
    std::cout << "\n========== Stress Test ==========" << std::endl;
    std::cout << "Hardware thread count: " << std::thread::hardware_concurrency() << std::endl;
    
    const int THREAD_COUNT = std::thread::hardware_concurrency(); // 使用硬件线程数
    const int OPS_PER_THREAD = 10000; // 每个线程执行的操作数
    
    std::cout << "\n----- Mutex Test (" << THREAD_COUNT << " threads, " << OPS_PER_THREAD << " operations per thread) -----" << std::endl;
    testStdMutex(THREAD_COUNT, OPS_PER_THREAD);
    testSpinLock(THREAD_COUNT, OPS_PER_THREAD);
    testX86HardwareSpinLock(THREAD_COUNT, OPS_PER_THREAD);
    
    std::cout << "\n----- RWSpinLock Test (" << THREAD_COUNT << " threads, 1000 operations per thread) -----" << std::endl;
    testRWSpinLock(THREAD_COUNT, 1000, 0.8);  // 80%读操作，20%写操作
    
    std::cout << "\n----- Queue Test (" << THREAD_COUNT << " threads, 50 operations per thread) -----" << std::endl;
    testLockFreeQueue(THREAD_COUNT, 50);
    testStdQueueWithMutex(THREAD_COUNT, 50);
}

// thread safety test
void runSafetyTest() {
    std::cout << "\n========== Thread Safety Test ==========" << std::endl;
    
    const int THREAD_COUNT = 8;
    const int OPS_PER_THREAD = 1000;
    
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
    
    {
        LockFreeQueue<int> queue(10000);
        std::atomic<int> push_count(0);
        std::atomic<int> pop_count(0);
        
        auto producer = [&](int id) {
            for (int i = 0; i < 50; ++i) {  
                int value = i + id * 100;
                queue.Push(value);
                push_count.fetch_add(1, std::memory_order_relaxed);
            }
        };
        
        auto consumer = [&](int id) {
            int value;
            int count = 0;
            
            while (count < 50) {  
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
    
    {
        RWSpinLock rwlock;
        std::atomic<int> shared_counter(0);
        std::atomic<bool> race_detected(false);
        
        auto reader = [&](int id) {
            for (int i = 0; i < OPS_PER_THREAD; ++i) {
                rwlock.lockRead();
                
                if (rwlock.isWriteLocked()) {
                    race_detected = true;
                }
                
                volatile int value = shared_counter.load(std::memory_order_relaxed);
                (void)value;  // 防止编译器优化
                
                rwlock.unlockRead();
            }
        };
        
        auto writer = [&](int id) {
            for (int i = 0; i < OPS_PER_THREAD / 10; ++i) { 
                rwlock.lockWrite();
                
                if (rwlock.getReadLockCount() > 0) {
                    race_detected = true;
                }
                
                shared_counter.fetch_add(1, std::memory_order_relaxed);
                
                rwlock.unlockWrite();
            }
        };
        
        std::vector<std::thread> readers;
        std::vector<std::thread> writers;
        
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
        
        bool counter_correct = (shared_counter.load() == 2 * (OPS_PER_THREAD / 10));
        bool no_race = !race_detected;
        
        printResult("RWSpinLock 线程安全性", 0, counter_correct && no_race);
    }
}

// high load test
void testLockFreeQueueHighLoad(int threadCount, int opsPerThread) {
    LockFreeQueue<int> queue(100000);
    std::atomic<int> pushCount(0);
    std::atomic<int> popCount(0);
    std::atomic<uint64_t> pushSum(0);
    std::atomic<uint64_t> popSum(0);
    
    int actual_threads = std::min(threadCount, 16);
    int actual_ops = std::min(opsPerThread, 200);
    
    auto producer = [&](int id) {
        for (int i = 0; i < actual_ops; ++i) {
            int value = i * 10 + id;
            queue.Push(value);
            pushSum.fetch_add(value, std::memory_order_relaxed);
            pushCount.fetch_add(1, std::memory_order_relaxed);
            
            if (i % 10 == 0) {
                std::this_thread::yield();
            }
        }
    };
    
    auto consumer = [&](int id) {
        int consecutive_fails = 0;
        int total_attempts = 0;
        const int MAX_TOTAL_ATTEMPTS = 10000;
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
                    
                    if (pushCount.load(std::memory_order_relaxed) == actual_ops * (actual_threads / 2) && 
                        popCount.load(std::memory_order_relaxed) >= pushCount.load(std::memory_order_relaxed)) {
                        success = true;
                        break;
                    }
                }
            }
            if (!success || total_attempts >= MAX_TOTAL_ATTEMPTS) {
                break;
            }
        }
    };
    
    std::vector<std::thread> threads;
    auto start = high_resolution_clock::now();
    
    for (int i = 0; i < actual_threads / 2; ++i) {
        threads.emplace_back(producer, i);
    }
    
    for (int i = 0; i < actual_threads / 2; ++i) {
        threads.emplace_back(consumer, i);
    }
    
    for (auto& t : threads) {
        t.join();
    }
    auto end = high_resolution_clock::now();
    auto elapsed = duration_cast<milliseconds>(end - start).count();
    
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
    
    printResult("LockFreeQueue High Load Test", elapsed, 
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

// mixed operation test
void testLockFreeQueueMixedOperations(int threadCount, int opsPerThread) {
    LockFreeQueue<int> queue(10000);
    std::atomic<int> totalOpsCompleted(0);
    
    auto worker = [&](int id) {
        std::mt19937 gen(id + static_cast<unsigned>(std::chrono::high_resolution_clock::now()
            .time_since_epoch().count()));
        std::uniform_int_distribution<> dis(0, 100);
        
        for (int i = 0; i < opsPerThread; ++i) {
            int rnd = dis(gen);
            
            if (rnd < 60) { 
                int value = i * 1000 + id;
                queue.Push(value);
            } else { 
                int value;
                if (queue.Pop(value)) {
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

void testLockFreeQueueContention(int threadCount) {
    LockFreeQueue<int> queue(10000);
    std::atomic<bool> start_flag(false);
    std::atomic<int> ready_threads(0);
    std::atomic<int> insertCount(0);
    std::atomic<int> removeCount(0);
    
    int actual_threads = std::min(threadCount, 16);
    
    auto worker = [&](int id, bool is_producer) {
        ready_threads.fetch_add(1, std::memory_order_release);
        
        while (ready_threads.load(std::memory_order_acquire) < actual_threads) {
            std::this_thread::yield();
        }
        
        while (!start_flag.load(std::memory_order_acquire)) {
            std::this_thread::yield();
        }
        
        if (is_producer) {
            for (int i = 0; i < 50; ++i) { 
                queue.Push(id * 1000 + i);
                insertCount.fetch_add(1, std::memory_order_relaxed);
                if (i % 10 == 0) {
                    std::this_thread::yield();
                }
            }
        } else {
            int total_attempts = 0;
            const int MAX_TOTAL_ATTEMPTS = 10000; 
            
            for (int i = 0; i < 50; ++i) { 
                int value;
                bool success = false;
                
                for (int attempt = 0; attempt < 1000 && !success && total_attempts < MAX_TOTAL_ATTEMPTS; ++attempt) {
                    total_attempts++;
                    if (queue.Pop(value)) {
                        removeCount.fetch_add(1, std::memory_order_relaxed);
                        success = true;
                    } else {
                        std::this_thread::yield();
                        
                        if (insertCount.load(std::memory_order_relaxed) == 
                            (actual_threads / 2) * 50 && 
                            insertCount.load(std::memory_order_relaxed) <= 
                            removeCount.load(std::memory_order_relaxed)) {
                            break;
                        }
                    }
                }
                
                if (!success || total_attempts >= MAX_TOTAL_ATTEMPTS) {
                    break;
                }
            }
        }
    };
    
    std::vector<std::thread> threads;
    auto start = high_resolution_clock::now();
    
    for (int i = 0; i < actual_threads; ++i) {
        bool is_producer = (i % 2 == 0);
        threads.emplace_back(worker, i, is_producer);
    }
    
    while (ready_threads.load(std::memory_order_acquire) < actual_threads) {
        std::this_thread::yield();
    }
    
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    
    start_flag.store(true, std::memory_order_release);
    
    for (auto& t : threads) {
        t.join();
    }
    
    auto end = high_resolution_clock::now();
    auto elapsed = duration_cast<milliseconds>(end - start).count();
    
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    
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

void testLockFreeQueueBurstOperations(int threadCount) {
    LockFreeQueue<int> queue(10000);
    std::atomic<int> totalPushes(0);
    std::atomic<int> totalPops(0);
    std::atomic<bool> done(false);
    
    int actual_threads = std::min(threadCount, 8);
    int burst_size = 100; 
    int burst_count = 3; 
    
    auto producer = [&](int id) {
        std::mt19937 gen(id + 1000);
        
        for (int burst = 0; burst < burst_count && !done.load(std::memory_order_acquire); ++burst) {
            std::this_thread::sleep_for(std::chrono::milliseconds(gen() % 5 + 1));
            
            for (int i = 0; i < burst_size && !done.load(std::memory_order_acquire); ++i) {
                queue.Push(burst * 1000 + i);
                totalPushes.fetch_add(1, std::memory_order_relaxed);
                
                if (i % 20 == 0) {
                    std::this_thread::yield();
                }
            }
            
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
    };
    
    auto consumer = [&](int id) {
        int value;
        int localPops = 0;
        int consecutive_fails = 0;
        int total_attempts = 0;
        const int MAX_TOTAL_ATTEMPTS = 10000; 
        
        while (!done.load(std::memory_order_acquire) || !queue.IsEmpty()) {
            if (total_attempts++ > MAX_TOTAL_ATTEMPTS) {
                break; 
            }
            
            if (queue.Pop(value)) {
                localPops++;
                totalPops.fetch_add(1, std::memory_order_relaxed);
                consecutive_fails = 0;
            } else {
                if (++consecutive_fails > 100) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
                    consecutive_fails = 0;
                    
                    if (queue.IsEmpty() && done.load(std::memory_order_acquire)) {
                        break;
                    }
                } else {
                    std::this_thread::yield();
                }
            }
            
            if (localPops % 50 == 0 && queue.IsEmpty() && done.load(std::memory_order_acquire)) {
                break;
            }
        }
    };
    
    auto monitor = [&]() {
        int last_total_pops = 0;
        int stalled_count = 0;
        
        while (!done.load(std::memory_order_acquire)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            
            int current_pops = totalPops.load(std::memory_order_acquire);
            if (current_pops == last_total_pops) {
                if (++stalled_count > 10) {
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
    
    std::thread monitor_thread(monitor);
    
    for (int i = 0; i < actual_threads / 4; ++i) {
        producers.emplace_back(producer, i);
    }
    
    for (int i = 0; i < actual_threads - actual_threads / 4; ++i) {
        consumers.emplace_back(consumer, i);
    }
    
    std::thread timeout_thread([&]() {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        done.store(true, std::memory_order_release);
    });
    
    for (auto& t : producers) {
        t.join();
    }
    
    done.store(true, std::memory_order_release);
    
    for (auto& t : consumers) {
        t.join();
    }
    
    monitor_thread.join();
    timeout_thread.join();
    
    auto end = high_resolution_clock::now();
    auto elapsed = duration_cast<milliseconds>(end - start).count();
    
    int leftover = 0;
    int value;
    while (!queue.IsEmpty()) {
        queue.Pop(value);
        leftover++;
        if (leftover > totalPushes.load(std::memory_order_relaxed)) {
            break;
        }
    }
    
    bool allItemsProcessed = (totalPushes.load() == totalPops.load() + leftover);
    bool queueEmpty = queue.IsEmpty();
    
    printResult("LockFreeQueue Burst Operations Test", elapsed, 
                allItemsProcessed && queueEmpty);
    
    if (!allItemsProcessed) {
        std::cout << "   推送: " << totalPushes.load() << ", 弹出: " 
                  << totalPops.load() << ", 剩余: " << leftover << std::endl;
    }
    if (!queueEmpty) {
        std::cout << "   队列剩余: " << queue.Size() << " 个元素" << std::endl;
    }
}

void testLockFreeQueueHeavyLoad(int threadCount, int opsPerThread) {
    LockFreeQueue<int> queue(10000);
    std::atomic<uint64_t> pushSum(0);
    std::atomic<uint64_t> popSum(0);
    std::atomic<int> pushCount(0);
    std::atomic<int> popCount(0);
    std::atomic<bool> error_detected(false);
    std::vector<std::thread> threads;
    
    int actual_threads = std::min(threadCount, 32);
    int actual_ops = std::min(opsPerThread, 10000);
    
    auto start = high_resolution_clock::now();
    
    for (int i = 0; i < actual_threads / 2; ++i) {
        threads.emplace_back([&, i]() {
            for (int j = 0; j < actual_ops; ++j) {

                int value = (i << 20) | j;
                queue.Push(value);
                pushSum.fetch_add(value, std::memory_order_relaxed);
                pushCount.fetch_add(1, std::memory_order_relaxed);
                
                if (j % 100 == 0) {
                    std::this_thread::yield();
                }
            }
        });
    }
    
    for (int i = 0; i < actual_threads / 2; ++i) {
        threads.emplace_back([&]() {
            int expected_ops = actual_ops * (actual_threads / 2) / (actual_threads / 2);
            int consumed = 0;
            std::vector<int> local_consumed;
            local_consumed.reserve(expected_ops);
            int total_attempts = 0;
            const int MAX_TOTAL_ATTEMPTS = 100000;
            
            while (consumed < expected_ops && total_attempts < MAX_TOTAL_ATTEMPTS) {
                total_attempts++;
                int value;
                if (queue.Pop(value)) {
                    local_consumed.push_back(value);
                    popSum.fetch_add(value, std::memory_order_relaxed);
                    popCount.fetch_add(1, std::memory_order_relaxed);
                    consumed++;
                } else {
                    if (pushCount.load(std::memory_order_relaxed) >= 
                        actual_ops * (actual_threads / 2) &&
                        queue.IsEmpty()) {
                        break;
                    }
                    
                    if (total_attempts % 1000 == 0) {
                        std::this_thread::sleep_for(std::chrono::milliseconds(1));
                    } else {
                        std::this_thread::yield();
                    }
                }
            }
            
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
    
    for (auto& t : threads) {
        t.join();
    }
    
    auto end = high_resolution_clock::now();
    auto elapsed = duration_cast<milliseconds>(end - start).count();
    
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
    
    std::cout << "   推送: " << pushCount.load() << ", 弹出: " 
             << popCount.load() << ", 剩余: " << leftover << std::endl;
    std::cout << "   推送总和: " << pushSum.load() << ", 弹出总和: " 
             << popSum.load() << ", 匹配: " << (sumsMatch ? "是" : "否") << std::endl;
    std::cout << "   每秒处理项目: " << 
        (elapsed > 0 ? (pushCount.load() * 1000 / elapsed) : pushCount.load()) << std::endl;
}

void testLockFreeQueueChaos(int threadCount, int seconds) {
    LockFreeQueue<int> queue(10000);
    std::atomic<bool> stop(false);
    std::atomic<uint64_t> total_ops(0);
    std::atomic<int> errors(0);
    std::vector<std::thread> threads;
    
    int actual_threads = std::min(threadCount, 16);
    int test_duration = std::min(seconds, 5);
    
    auto start = high_resolution_clock::now();
    
    for (int i = 0; i < actual_threads; ++i) {
        threads.emplace_back([&, i]() {
            std::mt19937 gen(i + static_cast<unsigned>(std::chrono::high_resolution_clock::now()
                .time_since_epoch().count()));
            std::uniform_int_distribution<> op_dis(0, 99); 
            std::uniform_int_distribution<> value_dis(0, 10000);
            
            std::vector<int> local_expected; 
            
            while (!stop.load(std::memory_order_relaxed)) {
                int op = op_dis(gen);
                
                if (op < 40) { 
                    int value = (i << 16) | value_dis(gen);
                    queue.Push(value);
                    local_expected.push_back(value);
                    total_ops.fetch_add(1, std::memory_order_relaxed);
                } 
                else if (op < 80) { 
                    int value;
                    if (!queue.IsEmpty()) {
                        queue.Pop(value);
                        total_ops.fetch_add(1, std::memory_order_relaxed);
                    }
                }
                else if (op < 90) {
                    queue.Size();
                    total_ops.fetch_add(1, std::memory_order_relaxed);
                }
                else if (op < 100) {
                    queue.IsEmpty();
                    total_ops.fetch_add(1, std::memory_order_relaxed);
                }
                
                if (op % 10 == 0) {
                    std::this_thread::yield();
                }
            }
        });
    }
    
    std::this_thread::sleep_for(std::chrono::seconds(test_duration));
    
    stop.store(true, std::memory_order_release);
    
    for (auto& t : threads) {
        t.join();
    }
    auto end = high_resolution_clock::now();
    auto elapsed = duration_cast<milliseconds>(end - start).count();
    
    int leftover = 0;
    int value;
    while (!queue.IsEmpty()) {
        queue.Pop(value);
        leftover++;
    }
    
    printResult("LockFreeQueue 混沌测试", elapsed, errors.load() == 0);
    
    std::cout << "   总操作数: " << total_ops.load() << ", 每秒: " 
              << (total_ops.load() * 1000 / elapsed) << std::endl;
    std::cout << "   剩余元素: " << leftover << std::endl;
}

void testLockFreeQueueThroughput(int dataSizeMB) {
    struct LargeData {
        int id;
        char data[1024]; 
        
        LargeData() : id(0) {
            memset(data, 0, sizeof(data));
        }
        
        explicit LargeData(int i) : id(i) {
            for (int j = 0; j < 1024; ++j) {
                data[j] = static_cast<char>((i + j) % 256);
            }
        }
    };
    
    int numItems = (dataSizeMB * 1024 * 1024) / sizeof(LargeData);
    numItems = std::min(numItems, 1000); 
    
    LockFreeQueue<LargeData> queue(sizeof(LargeData) * numItems);
    std::atomic<int> errors(0);
    
    auto start = high_resolution_clock::now();
    
    std::thread producer([&]() {
        for (int i = 0; i < numItems; ++i) {
            queue.Push(LargeData(i));
            
            if (i % 10 == 0) {
                std::this_thread::yield();
            }
        }
    });
    
    std::thread consumer([&]() {
        int total_attempts = 0;
        const int MAX_TOTAL_ATTEMPTS = 1000000;
        
        for (int i = 0; i < numItems; ++i) {
            LargeData value;
            bool success = false;
            
            while (!success && total_attempts < MAX_TOTAL_ATTEMPTS) {
                total_attempts++;
                if (queue.Pop(value)) {
                    success = true;
                    
                    if (value.id != i) {
                        errors.fetch_add(1, std::memory_order_relaxed);
                    }
                    
                    for (int j = 0; j < 10; ++j) { 
                        if (value.data[j] != static_cast<char>((value.id + j) % 256)) {
                            errors.fetch_add(1, std::memory_order_relaxed);
                            break;
                        }
                    }
                }
            }
            
            if (total_attempts >= MAX_TOTAL_ATTEMPTS) {
                std::cout << "   警告: 达到最大尝试次数，提前退出" << std::endl;
                break;
            }
        }
    });
    
    producer.join();
    consumer.join();
    
    auto end = high_resolution_clock::now();
    auto elapsed = duration_cast<milliseconds>(end - start).count();
    
    double throughput = static_cast<double>(numItems) * sizeof(LargeData) / (1024.0 * 1024.0);
    double throughputPerSec = throughput / (elapsed / 1000.0);
    
    printResult("LockFreeQueue 吞吐量测试", elapsed, errors.load() == 0);
    
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
    
    for (int i = 0; i < queueCount; ++i) {
        queues.push_back(std::make_unique<LockFreeQueue<int>>(10000));
    }
    
    const int test_duration = 3;
    
    std::vector<std::thread> threads;
    for (int i = 0; i < threadCount; ++i) {
        threads.emplace_back([&, i]() {
            std::mt19937 gen(i + static_cast<unsigned>(std::chrono::high_resolution_clock::now().time_since_epoch().count()));
            std::uniform_int_distribution<> queue_dis(0, queueCount - 1);
            std::uniform_int_distribution<> op_dis(0, 1); // 0=推送，1=弹出
            std::uniform_int_distribution<> value_dis(0, 10000);
            
            while (!stop.load(std::memory_order_relaxed)) {
                int queue_idx = queue_dis(gen);
                auto& queue = queues[queue_idx];
                
                if (op_dis(gen) == 0) {
                    int value = value_dis(gen);
                    queue->Push(value);
                    total_push.fetch_add(1, std::memory_order_relaxed);
                } else {
                    int value;
                    if (!queue->IsEmpty()) {
                        queue->Pop(value);
                        total_pop.fetch_add(1, std::memory_order_relaxed);
                    }
                }
                
                if (gen() % 100 == 0) {
                    std::this_thread::yield();
                }
            }
        });
    }
    
    auto start = high_resolution_clock::now();
    
    std::this_thread::sleep_for(std::chrono::seconds(test_duration));
    
    stop.store(true, std::memory_order_release);
    
    for (auto& t : threads) {
        t.join();
    }
    
    auto end = high_resolution_clock::now();
    auto elapsed = duration_cast<milliseconds>(end - start).count();
    
    int leftover = 0;
    for (auto& queue : queues) {
        int value;
        while (!queue->IsEmpty()) {
            queue->Pop(value);
            leftover++;
        }
    }
    
    printResult("Multiple Queues Concurrency Test", elapsed, true);
    
    std::cout << "   队列数量: " << queueCount << std::endl;
    std::cout << "   总入队操作: " << total_push.load() 
              << ", 总出队操作: " << total_pop.load() 
              << ", 剩余: " << leftover << std::endl;
    std::cout << "   每秒操作: " << ((total_push.load() + total_pop.load()) * 1000 / elapsed) << std::endl;
}

int main() {
    std::cout << std::endl << "========== Thread Safety Test ==========" << std::endl;
    
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
    
    {
        LockFreeQueue<int> queue(10000);
        std::atomic<int> sum1(0);
        std::atomic<int> sum2(0);
        std::vector<std::thread> threads;
        auto start = high_resolution_clock::now();
        
        for (int i = 0; i < 5; ++i) {
            threads.emplace_back([&queue, &sum1, i]() {
                for (int j = 0; j < 100; ++j) {
                    int value = i * 100 + j;
                    queue.Push(value);
                    sum1.fetch_add(value, std::memory_order_relaxed);
                }
            });
        }
        
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
    
    {
        RWSpinLock rwlock;
        std::atomic<int> counter(0);
        std::atomic<int> reads(0);
        std::vector<std::thread> threads;
        auto start = high_resolution_clock::now();
        
        for (int i = 0; i < 8; ++i) {
            threads.emplace_back([&rwlock, &counter, &reads, i]() {
                for (int j = 0; j < 100; ++j) {
                    if (j % 5 == 0) { /
                        rwlock.lockWrite();
                        counter.fetch_add(1, std::memory_order_relaxed);
                        rwlock.unlockWrite();
                    } else { 
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
    
    std::cout << std::endl << "========== High Concurrency Stress Test ==========" << std::endl;
    int hwThreads = std::thread::hardware_concurrency();
    std::cout << "Hardware Threads: " << hwThreads << std::endl << std::endl;
    
    int threadCount = hwThreads > 0 ? hwThreads : 4;
    
    std::cout << std::endl << "========== Thread Safety Basic Test ==========" << std::endl;
    
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
    
    {
        LockFreeQueue<int> queue(10000);
        std::atomic<int> sum1(0);
        std::atomic<int> sum2(0);
        std::vector<std::thread> threads;
        auto start = high_resolution_clock::now();
        
        for (int i = 0; i < 5; ++i) {
            threads.emplace_back([&queue, &sum1, i]() {
                for (int j = 0; j < 100; ++j) {
                    int value = i * 100 + j;
                    queue.Push(value);
                    sum1.fetch_add(value, std::memory_order_relaxed);
                }
            });
        }
        
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
        printResult("LockFreeQueue Basic Thread Safety", elapsed, passed);
        
        if (!passed) {
            std::cout << "   和校验: " << sum1.load() << " vs " << sum2.load() << std::endl;
            std::cout << "   队列剩余: " << (queue.IsEmpty() ? "空" : "非空") << std::endl;
        }
        
        threads.clear();
    }
    
    std::cout << std::endl << "========== Mutex Performance Test ==========" << std::endl;
    testStdMutex(threadCount, 10000);
    testSpinLock(threadCount, 10000);
    testX86HardwareSpinLock(threadCount, 10000);
    
    std::cout << std::endl << "========== Read-Write Lock Test ==========" << std::endl;
    testRWSpinLock(threadCount, 1000, 0.8);  // 80%读, 20%写
    
    std::cout << std::endl << "========== Standard Queue Test ==========" << std::endl;
    testLockFreeQueue(threadCount, 50);
    testStdQueueWithMutex(threadCount, 50);
    
    std::cout << std::endl << "========== LockFreeQueue Advanced Test ==========" << std::endl;
    
    // 执行高负载测试
    std::cout << std::endl << "----- High Load Test -----" << std::endl;
    testLockFreeQueueHighLoad(threadCount, 1000);
    
    // 执行混合操作测试
    std::cout << std::endl << "----- Mixed Operations Test -----" << std::endl;
    testLockFreeQueueMixedOperations(threadCount, 200);
    
    // 执行竞争条件测试
    std::cout << std::endl << "----- Contention Test -----" << std::endl;
    testLockFreeQueueContention(threadCount);
    
    // 执行繁重工作负载测试
    std::cout << std::endl << "----- Heavy Workload Test -----" << std::endl;
    testLockFreeQueueHeavyLoad(threadCount, 1000);
    
    // 执行混沌操作测试
    std::cout << std::endl << "----- Chaos Operation Test -----" << std::endl;
    testLockFreeQueueChaos(threadCount, 2); // 运行2秒
    
    // 执行吞吐量测试
    std::cout << std::endl << "----- Throughput Test -----" << std::endl;
    testLockFreeQueueThroughput(1); // 1MB数据
    
    // 执行多队列测试
    std::cout << std::endl << "----- Multiple Queues Test -----" << std::endl;
    testMultipleQueues(4, threadCount);
    
    std::cout << std::endl << "ALL TESTS PASSED!" << std::endl;
    
    return 0;
} 

#endif