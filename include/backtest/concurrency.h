#pragma once

#include <atomic>
#include <condition_variable>
#include <functional>
#include <future>
#include <mutex>
#include <queue>
#include <vector>
#include <memory>
#include <type_traits>
#include <cassert>
#include <thread>
#include <chrono>

namespace backtest {
class ReadWriteLock {
public:
    ReadWriteLock() : readers_(0), writers_(0), write_waiters_(0) {}

    void lockRead() {
        std::unique_lock<std::mutex> lock(mutex_);
        read_cv_.wait(lock, [this] {
            return writers_ == 0 && write_waiters_ == 0;
        });
        ++readers_;
    }

    void unlockRead() {
        std::unique_lock<std::mutex> lock(mutex_);
        --readers_;
        if (readers_ == 0) {
            write_cv_.notify_one();
        }
    }

    void lockWrite() {
        std::unique_lock<std::mutex> lock(mutex_);
        ++write_waiters_;
        write_cv_.wait(lock, [this] {
            return readers_ == 0 && writers_ == 0;
        });
        --write_waiters_;
        ++writers_;
    }

    void unlockWrite() {
        std::unique_lock<std::mutex> lock(mutex_);
        --writers_;
        if (write_waiters_ > 0) {
            write_cv_.notify_one();
        } else {
            read_cv_.notify_all();
        }
    }

private:
    std::mutex mutex_;
    std::condition_variable read_cv_;
    std::condition_variable write_cv_;
    int readers_;
    int writers_;
    int write_waiters_;
};

/**
 * @brief 读锁的 RAII
 */
class ReadLock {
public:
    explicit ReadLock(ReadWriteLock& lock) : lock_(lock) {
        lock_.lockRead();
    }
    ~ReadLock() {
        lock_.unlockRead();
    }
private:
    ReadWriteLock& lock_;
    ReadLock(const ReadLock&) = delete;
    ReadLock& operator=(const ReadLock&) = delete;
};

/**
 * @brief 写锁的 RAII
 */
class WriteLock {
public:
    explicit WriteLock(ReadWriteLock& lock) : lock_(lock) {
        lock_.lockWrite();
    }
    ~WriteLock() {
        lock_.unlockWrite();
    }
private:
    ReadWriteLock& lock_;
    WriteLock(const WriteLock&) = delete;
    WriteLock& operator=(const WriteLock&) = delete;
};

/**
 * @brief 线程安全队列
 */
template<typename T>
class ThreadSafeQueue {
public:
    ThreadSafeQueue() = default;

    void push(T item) {
        std::lock_guard<std::mutex> lock(mutex_);
        queue_.push(std::move(item));
        cv_.notify_one();
    }

    bool tryPop(T& item) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (queue_.empty()) {
            return false;
        }
        item = std::move(queue_.front());
        queue_.pop();
        return true;
    }

    std::shared_ptr<T> tryPop() {
        std::lock_guard<std::mutex> lock(mutex_);
        if (queue_.empty()) {
            return nullptr;
        }
        std::shared_ptr<T> res(std::make_shared<T>(std::move(queue_.front())));
        queue_.pop();
        return res;
    }

    void waitAndPop(T& item) {
        std::unique_lock<std::mutex> lock(mutex_);
        cv_.wait(lock, [this] { return !queue_.empty(); });
        item = std::move(queue_.front());
        queue_.pop();
    }

    std::shared_ptr<T> waitAndPop() {
        std::unique_lock<std::mutex> lock(mutex_);
        cv_.wait(lock, [this] { return !queue_.empty(); });
        std::shared_ptr<T> res(std::make_shared<T>(std::move(queue_.front())));
        queue_.pop();
        return res;
    }

    bool empty() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return queue_.empty();
    }

    size_t size() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return queue_.size();
    }

    template<typename Container>
    bool tryPopBatch(Container& container, size_t maxItems) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (queue_.empty()) {
            return false;
        }
        
        size_t count = std::min(queue_.size(), maxItems);
        for (size_t i = 0; i < count; ++i) {
            container.push_back(std::move(queue_.front()));
            queue_.pop();
        }
        return true;
    }
    
    template<typename Iterator>
    void pushBatch(Iterator first, Iterator last) {
        std::lock_guard<std::mutex> lock(mutex_);
        bool wasEmpty = queue_.empty();
        size_t count = 0;
        
        while (first != last) {
            queue_.push(std::move(*first));
            ++first;
            ++count;
        }
        
        if (count > 0) {
            if (wasEmpty || count > 1) {
                cv_.notify_all();
            } else {
                cv_.notify_one();
            }
        }
    }
    
    template<typename Rep, typename Period>
    bool waitForItems(size_t count, const std::chrono::duration<Rep, Period>& timeout) {
        std::unique_lock<std::mutex> lock(mutex_);
        return cv_.wait_for(lock, timeout, [this, count] {
            return queue_.size() >= count;
        });
    }
    
    void clear() {
        std::lock_guard<std::mutex> lock(mutex_);
        while (!queue_.empty()) {
            queue_.pop();
        }
    }

private:
    mutable std::mutex mutex_;
    std::queue<T> queue_;
    std::condition_variable cv_;
};

/**
 * @brief 线程池
 */
class ThreadPool {
public:
    explicit ThreadPool(size_t numThreads = 0) 
        : stop_(false) {
        if (numThreads == 0) {
            numThreads = std::thread::hardware_concurrency();
            numThreads = numThreads ? numThreads : 1;
        }
        
        workers_.reserve(numThreads);
        for (size_t i = 0; i < numThreads; ++i) {
            workers_.emplace_back([this] {
                while (true) {
                    std::function<void()> task;
                    
                    {
                        std::unique_lock<std::mutex> lock(queue_mutex_);
                        
                        condition_.wait(lock, [this] {
                            return stop_ || !tasks_.empty();
                        });
                        
                        if (stop_ && tasks_.empty()) {
                            return;
                        }
                        
                        task = std::move(tasks_.front());
                        tasks_.pop();
                    }
                    
                    task();
                    
                    {
                        std::lock_guard<std::mutex> lock(count_mutex_);
                        ++tasks_completed_;
                    }
                }
            });
        }
    }
    
    ~ThreadPool() {
        {
            std::lock_guard<std::mutex> lock(queue_mutex_);
            stop_ = true;
        }
        
        condition_.notify_all();
        
        for (auto& worker : workers_) {
            if (worker.joinable()) {
                worker.join();
            }
        }
    }
    
    template<typename F, typename... Args>
    auto enqueue(F&& f, Args&&... args) -> std::future<typename std::invoke_result<F, Args...>::type> {
        using return_type = typename std::invoke_result<F, Args...>::type;
        
        auto task = std::make_shared<std::packaged_task<return_type()>>(
            std::bind(std::forward<F>(f), std::forward<Args>(args)...)
        );
        
        std::future<return_type> res = task->get_future();
        
        {
            std::lock_guard<std::mutex> lock(queue_mutex_);
            
            if (stop_) {
                throw std::runtime_error("ThreadPool: enqueue on stopped ThreadPool");
            }
            
            tasks_.emplace([task]() { (*task)(); });
            
            ++tasks_submitted_;
        }
        
        condition_.notify_one();
        
        return res;
    }
    
    size_t pendingTasks() const {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        return tasks_.size();
    }
    
    size_t submittedTasks() const {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        return tasks_submitted_;
    }
    
    size_t completedTasks() const {
        std::lock_guard<std::mutex> lock(count_mutex_);
        return tasks_completed_;
    }
    
    size_t threadCount() const {
        return workers_.size();
    }
    
    void waitForAllTasks() {
        while (true) {
            {
                std::lock_guard<std::mutex> lock_queue(queue_mutex_);
                std::lock_guard<std::mutex> lock_count(count_mutex_);
                
                if (tasks_.empty() && tasks_submitted_ == tasks_completed_) {
                    return;
                }
            }
            
            std::this_thread::yield();
        }
    }

private:
    std::vector<std::thread> workers_;
    std::queue<std::function<void()>> tasks_;
    mutable std::mutex queue_mutex_;
    mutable std::mutex count_mutex_;
    std::condition_variable condition_;
    std::atomic<bool> stop_;
    size_t tasks_submitted_ = 0;
    size_t tasks_completed_ = 0;
};

/**
 * @brief 原子时钟类，rust里面实现了
 */
class AtomicClock {
public:
    AtomicClock(int64_t initialTime = 0) : timestamp_(initialTime) {}

    int64_t now() const {
        return timestamp_.load(std::memory_order_acquire);
    }

    void setTime(int64_t time) {
        timestamp_.store(time, std::memory_order_release);
    }

    int64_t incrementTime(int64_t delta) {
        return timestamp_.fetch_add(delta, std::memory_order_acq_rel) + delta;
    }

    int64_t getMonotonicTime() {
        int64_t current = now();
        while (true) {
            int64_t next = current + 1;
            if (timestamp_.compare_exchange_strong(current, next, 
                                                std::memory_order_acq_rel, 
                                                std::memory_order_acquire)) {
                return next;
            }
        }
    }

private:
    std::atomic<int64_t> timestamp_;
};

/**
 * @brief 自旋锁实现
 */
class SpinLock {
public:
    SpinLock() : flag_(false) {}

    class LockGuard {
    public:
        explicit LockGuard(SpinLock& lock) : lock_(lock) {
            lock_.lock();
        }
        
        ~LockGuard() {
            lock_.unlock();
        }
        
    private:
        SpinLock& lock_;
        LockGuard(const LockGuard&) = delete;
        LockGuard& operator=(const LockGuard&) = delete;
    };
    void lock() {
        bool expected = false;
        if (flag_.compare_exchange_strong(expected, true, 
                                       std::memory_order_acquire, 
                                       std::memory_order_relaxed)) {
            return;
        }
        
        int spin_count = 0;
        static const int max_spin = 32;
        
        while (true) {
            expected = false;
            if (flag_.compare_exchange_weak(expected, true,
                                          std::memory_order_acquire,
                                          std::memory_order_relaxed)) {
                return;
            }
            
            if (spin_count < max_spin) {
                for (int i = 0; i < (1 << spin_count); ++i) {
                    #if defined(_MSC_VER) && (defined(_M_IX86) || defined(_M_X64))
                        _mm_pause();
                    #elif defined(__GNUC__) && (defined(__i386__) || defined(__x86_64__))
                        __builtin_ia32_pause();
                    #else
                        std::this_thread::yield();
                    #endif
                }
                ++spin_count;
            } else {
                std::this_thread::yield();
            }
        }
    }

    void unlock() {
        flag_.store(false, std::memory_order_release);
    }

    bool try_lock() {
        bool expected = false;
        return flag_.compare_exchange_strong(expected, true,
                                           std::memory_order_acquire,
                                           std::memory_order_relaxed);
    }

private:
    std::atomic<bool> flag_;
};

/**
 * @brief X86平台自旋锁，效果不佳
 */
class X86HardwareSpinLock {
public:
    X86HardwareSpinLock() : locked_(0) {}

#if defined(__GNUC__) || defined(__clang__)
    void lock() {
        while (__sync_lock_test_and_set(&locked_, 1)) {
            __asm__ __volatile__("pause" ::: "memory");
        }
        __sync_synchronize();
    }
    
    void unlock() {
        __sync_synchronize();
        __sync_lock_release(&locked_);
    }
    
    bool try_lock() {
        if (!__sync_lock_test_and_set(&locked_, 1)) {
            __sync_synchronize();
            return true;
        }
        return false;
    }
#elif defined(_MSC_VER)
    void lock() {
        while (_InterlockedExchange(&locked_, 1L)) {
            _mm_pause();
        }
        _ReadWriteBarrier();
    }
    
    void unlock() {
        _ReadWriteBarrier();
        _InterlockedExchange(&locked_, 0L);
    }
    
    bool try_lock() {
        if (!_InterlockedExchange(&locked_, 1L)) {
            _ReadWriteBarrier();
            return true;
        }
        return false;
    }
#else
    void lock() {
        bool expected = false;
        while (!locked_.compare_exchange_strong(expected, true, 
                                             std::memory_order_acquire, 
                                             std::memory_order_relaxed)) {
            expected = false;
            #if defined(_MSC_VER)
                _mm_pause();
            #elif defined(__GNUC__) || defined(__clang__)
                __builtin_ia32_pause();
            #else
                std::this_thread::yield();
            #endif
        }
    }
    
    void unlock() {
        locked_.store(false, std::memory_order_release);
    }
    
    bool try_lock() {
        bool expected = false;
        return locked_.compare_exchange_strong(expected, true,
                                            std::memory_order_acquire,
                                            std::memory_order_relaxed);
    }
#endif

private:
#if defined(__GNUC__) || defined(__clang__)
    volatile int locked_;
#elif defined(_MSC_VER)
    volatile long locked_;
#else
    std::atomic<bool> locked_;
#endif
};

/**
 * @brief X86硬件自旋锁的RAII
 */
class X86HardwareLockGuard {
public:
    explicit X86HardwareLockGuard(X86HardwareSpinLock& lock) : lock_(lock) {
        lock_.lock();
    }
    
    ~X86HardwareLockGuard() {
        lock_.unlock();
    }
    
private:
    X86HardwareSpinLock& lock_;
    X86HardwareLockGuard(const X86HardwareLockGuard&) = delete;
    X86HardwareLockGuard& operator=(const X86HardwareLockGuard&) = delete;
};

/**
 * @brief X86平台内存屏障工具类
 */
class X86MemoryBarrier {
public:
    static inline void loadFence() {
#if defined(__GNUC__) || defined(__clang__)
        __asm__ __volatile__("lfence" ::: "memory");
#elif defined(_MSC_VER)
        _mm_lfence();
#else
        std::atomic_thread_fence(std::memory_order_acquire);
#endif
    }
    
    static inline void storeFence() {
#if defined(__GNUC__) || defined(__clang__)
        __asm__ __volatile__("sfence" ::: "memory");
#elif defined(_MSC_VER)
        _mm_sfence();
#else
        std::atomic_thread_fence(std::memory_order_release);
#endif
    }
    
    static inline void fullFence() {
#if defined(__GNUC__) || defined(__clang__)
        __asm__ __volatile__("mfence" ::: "memory");
#elif defined(_MSC_VER)
        _mm_mfence();
#else
        std::atomic_thread_fence(std::memory_order_seq_cst);
#endif
    }
    
    static inline void pause() {
#if defined(__GNUC__) || defined(__clang__)
        __asm__ __volatile__("pause" ::: "memory");
#elif defined(_MSC_VER)
        _mm_pause();
#else
        std::this_thread::yield();
#endif
    }
};
#if defined(__linux__)
#include "common.h"
#include <unistd.h>
#include <semaphore.h>

template <typename T, bool block_pop, bool block_push, bool busy_wait = false>
class RingBuffer
{
private:
    enum State
    {
        IDLE = 0,       // 该slot没有数据
        PUTTING,        // 有一个线程正在放入数据
        VALID,          // 该slot有数据
        TAKING,         // 有一个线程正在取出数据
    };

    size_t capacity;            // 容量
    T* slots;                   // 所有的slot
    State* states;              // 每一个slot的状态
    std::atomic<size_t> head;   // 逻辑上的头（不回滚）
    std::atomic<size_t> tail;   // 逻辑上的尾（不回滚）
    sem_t data_available;       // 可以取得的数据个数
    sem_t slot_available;       // 可以使用的slot个数

private:
    void wait()
    {
        if(!busy_wait)
            sleep(0);
    }

public:
    RingBuffer(size_t _capacity) : capacity(_capacity), head(0), tail(0)
    {
        assert(capacity > 0);
        slots = new T[capacity];
        states = new State[capacity];
        for(size_t i = 0; i < capacity; i++)
            states[i] = State::IDLE;
        if(block_pop)
            DO_WITH_ASSERT(sem_init(&data_available, 0, 0), _ret_ == 0);
        if(block_push)
            DO_WITH_ASSERT(sem_init(&slot_available, 0, capacity), _ret_ == 0);
    }

    ~RingBuffer()
    {
        delete[] slots;
        delete[] states;
        if(block_pop)
            DO_WITH_ASSERT(sem_destroy(&data_available), _ret_ == 0);
        if(block_push)
            DO_WITH_ASSERT(sem_destroy(&slot_available), _ret_ == 0);
    }

    void Push(T data)
    {
        if(block_push)
            DO_WITH_ASSERT(sem_wait(&slot_available), _ret_ == 0);
        auto index = (tail++) % capacity;
        auto* slot = slots + index;
        auto* state = states + index;
        while(!__sync_bool_compare_and_swap(state, State::IDLE, State::PUTTING))
            wait();
        *slot = data;
        DO_WITH_ASSERT(__sync_lock_test_and_set(state, State::VALID), _ret_ == State::PUTTING);
        if(block_pop)
            DO_WITH_ASSERT(sem_post(&data_available), _ret_ == 0);
    }

    T Pop()
    {
        if(block_pop)
            DO_WITH_ASSERT(sem_wait(&data_available), _ret_ == 0);
        auto index = (head++) % capacity;
        auto* slot = slots + index;
        auto* state = states + index;
        while(!__sync_bool_compare_and_swap(state, State::VALID, State::TAKING))
            wait();
        T data = *slot;
        DO_WITH_ASSERT(__sync_lock_test_and_set(state, State::IDLE), _ret_ == State::TAKING);
        if(block_push)
            DO_WITH_ASSERT(sem_post(&slot_available), _ret_ == 0);
        return data;
    }

    size_t Length() const
    {
        size_t head_snap = head.load();
        size_t tail_snap = tail.load();
        if(head_snap < tail_snap)
            return tail_snap - head_snap;
        else
            return 0;
    }
};

template <typename T>
class LockFreeQueue 
{
private:
    RingBuffer<T, false, false, false> buffer;

public:
    LockFreeQueue(size_t capacity) : buffer(capacity) {}
    
    ~LockFreeQueue() {}
    
    bool Push(const T& data) {
        try {
            buffer.Push(data);
            return true;
        } catch (...) {
            return false;
        }
    }
    
    bool Pop(T& result) {
        try {
            result = buffer.Pop();
            return true;
        } catch (...) {
            return false;
        }
    }
    
    // 获取当前队列长度
    const size_t Size() const {
        return buffer.Length();
    }
    
    bool IsEmpty() const {
        return Size() == 0;
    }
};

#endif

/**
 * @brief 高性能队列(基于std::queue + SpinLock)，如果std::queue换成lockfree的queue，性能会更好，但是lockfreequeue有一个坑，就是pop之前必须保证不是empty()
 */
template <typename T>
class HighPerformanceQueue {
public:
    HighPerformanceQueue() = default;
    ~HighPerformanceQueue() = default;

    void push(const T& item) {
        SpinLock::LockGuard lock(mutex_);
        queue_.push(item);
    }
    
    void push(T&& item) {
        SpinLock::LockGuard lock(mutex_);
        queue_.push(std::move(item));
    }
    
    bool try_pop(T& item) {
        SpinLock::LockGuard lock(mutex_);
        if (queue_.empty()) {
            return false;
        }
        item = std::move(queue_.front());
        queue_.pop();
        return true;
    }
    
    bool empty() const {
        SpinLock::LockGuard lock(mutex_);
        return queue_.empty();
    }
    
    size_t size() const {
        SpinLock::LockGuard lock(mutex_);
        return queue_.size();
    }
    
    void clear() {
        SpinLock::LockGuard lock(mutex_);
        while (!queue_.empty()) {
            queue_.pop();
        }
    }

private:
    std::queue<T> queue_;
    mutable SpinLock mutex_;
    
    HighPerformanceQueue(const HighPerformanceQueue&) = delete;
    HighPerformanceQueue& operator=(const HighPerformanceQueue&) = delete;
};

/**
 * @brief 读写自旋锁
 */
class RWSpinLock {
private:
    std::atomic<uint32_t> state_;
    
    static constexpr uint32_t WRITE_LOCK_BIT = 1;
    static constexpr uint32_t READ_LOCK_INCREMENT = (1 << 16);
    
public:
    RWSpinLock() : state_(0) {}
    
    void lockRead() {
        uint32_t expected, desired;
        int spin_count = 0;
        
        do {
            expected = state_.load(std::memory_order_relaxed);
            
            while (expected & WRITE_LOCK_BIT) {
                X86MemoryBarrier::pause();
                
                if (++spin_count > 1000) {
                    std::this_thread::yield();
                    spin_count = 0;
                }
                
                expected = state_.load(std::memory_order_relaxed);
            }
            
            desired = expected + READ_LOCK_INCREMENT;
            
        } while (!state_.compare_exchange_weak(expected, desired, 
                                            std::memory_order_acquire, 
                                            std::memory_order_relaxed));
    }
    
    void unlockRead() {
        state_.fetch_sub(READ_LOCK_INCREMENT, std::memory_order_release);
    }
    
    bool tryLockRead() {
        uint32_t expected = state_.load(std::memory_order_relaxed);
        
        if (expected & WRITE_LOCK_BIT) {
            return false;
        }
        
        uint32_t desired = expected + READ_LOCK_INCREMENT;
        
        return state_.compare_exchange_strong(expected, desired, 
                                           std::memory_order_acquire, 
                                           std::memory_order_relaxed);
    }
    
    void lockWrite() {
        int spin_count = 0;
        
        uint32_t expected = 0;
        while (!state_.compare_exchange_weak(expected, WRITE_LOCK_BIT, 
                                          std::memory_order_acquire, 
                                          std::memory_order_relaxed)) {
            X86MemoryBarrier::pause();
            
            if (++spin_count > 1000) {
                std::this_thread::yield();
                spin_count = 0;
            }
            
            expected = 0;
        }
    }
    
    void unlockWrite() {
        state_.store(0, std::memory_order_release);
    }
    
    bool tryLockWrite() {
        uint32_t expected = 0;
        return state_.compare_exchange_strong(expected, WRITE_LOCK_BIT, 
                                          std::memory_order_acquire,
                                          std::memory_order_relaxed);
    }
    
    uint16_t getReadLockCount() const {
        return state_.load(std::memory_order_relaxed) >> 16;
    }
    
    bool isWriteLocked() const {
        return (state_.load(std::memory_order_relaxed) & WRITE_LOCK_BIT) != 0;
    }
};

/**
 * @brief 读自旋锁的 RAII 包装器
 */
class ReadSpinLockGuard {
public:
    explicit ReadSpinLockGuard(RWSpinLock& lock) : lock_(lock) {
        lock_.lockRead();
    }
    ~ReadSpinLockGuard() {
        lock_.unlockRead();
    }

private:
    RWSpinLock& lock_;
    
    ReadSpinLockGuard(const ReadSpinLockGuard&) = delete;
    ReadSpinLockGuard& operator=(const ReadSpinLockGuard&) = delete;
};

/**
 * @brief 写自旋锁的 RAII 包装器
 */
class WriteSpinLockGuard {
public:
    explicit WriteSpinLockGuard(RWSpinLock& lock) : lock_(lock) {
        lock_.lockWrite();
    }
    ~WriteSpinLockGuard() {
        lock_.unlockWrite();
    }

private:
    RWSpinLock& lock_;
    
    WriteSpinLockGuard(const WriteSpinLockGuard&) = delete;
    WriteSpinLockGuard& operator=(const WriteSpinLockGuard&) = delete;
};

} // namespace backtest 