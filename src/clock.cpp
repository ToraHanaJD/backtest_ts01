// -------------------------------------------------------------------------------------------------
//  Copyright (C) 2023-2025. All rights reserved.
//
//  Licensed under the MIT License.
// -------------------------------------------------------------------------------------------------

#include "backtest/clock.h"
#include "backtest/timer.h"
#include <chrono>
#include <algorithm>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <limits>

#if BACKTEST_USE_EPOLL
#include <sys/eventfd.h>
#include <errno.h>
#include <string.h>
#endif

namespace backtest {

/**
 * TestClock 实现
 */
TestClock::TestClock() : current_time_ns_(0) {}

UnixNanos TestClock::timestampNs() const {
    return current_time_ns_.load(std::memory_order_acquire);
}

void TestClock::setTime(UnixNanos time_ns) {
    current_time_ns_.store(time_ns, std::memory_order_release);
}

void TestClock::setTimeAlertNs(const std::string& name, UnixNanos trigger_time_ns,
                             std::function<void(const TimeEvent&)> callback) {
    TimeEvent event(name, trigger_time_ns);
    alerts_.emplace_back(event, callback);
    
    // 按触发时间排序，以便高效查找
    std::sort(alerts_.begin(), alerts_.end(), 
              [](const auto& a, const auto& b) {
                  return a.first.triggerTimeNs() < b.first.triggerTimeNs();
              });
}

std::vector<TimeEvent> TestClock::advanceTime(UnixNanos to_time_ns, bool set_time) {
    std::vector<TimeEvent> triggered_events;
    
    UnixNanos current = current_time_ns_.load(std::memory_order_acquire);
    
    if (to_time_ns <= current) {
        return triggered_events;
    }
    
    // 如果设置时间标志为true，设置新时间
    if (set_time) {
        current_time_ns_.store(to_time_ns, std::memory_order_release);
    }
    
    for (const auto& alert_pair : alerts_) {
        const TimeEvent& event = alert_pair.first;
        UnixNanos trigger_time = event.triggerTimeNs();
        
        // 如果触发时间小于等于目标时间，且大于当前时间，则触发
        if (trigger_time <= to_time_ns && trigger_time > current) {
            triggered_events.push_back(event);
        }
        
        // 如果当前提醒的触发时间已经大于目标时间，可以提前退出循环
        if (trigger_time > to_time_ns) {
            break;
        }
    }
    
    return triggered_events;
}

std::vector<std::unique_ptr<TimeEventHandler>> TestClock::matchHandlers(const std::vector<TimeEvent>& events) {
    std::vector<std::unique_ptr<TimeEventHandler>> handlers;
    
    for (const auto& event : events) {
        for (const auto& alert_pair : alerts_) {
            if (alert_pair.first.name() == event.name() && 
                alert_pair.first.triggerTimeNs() == event.triggerTimeNs()) {
                
                auto handler = std::make_unique<TimeEventHandler>(event, alert_pair.second);
                handlers.push_back(std::move(handler));
                break;
            }
        }
    }
    
    return handlers;
}

/**
 * RealtimeClock 实现
 */
RealtimeClock::RealtimeClock() : running_(true) {
#if BACKTEST_USE_EPOLL
    // 设置epoll
    setupEpoll();
    
    // 创建后台线程，使用epoll进行高效等待
    timer_thread_ = std::thread([this]() {
        const int MAX_EVENTS = 5;
        struct epoll_event events[MAX_EVENTS];
        
        while (running_.load(std::memory_order_acquire)) {
            // 计算下一个触发时间
            UnixNanos next_alert_time = std::numeric_limits<UnixNanos>::max();
            
            {
                std::lock_guard<std::mutex> lock(alerts_mutex_);
                if (!alerts_.empty()) {
                    next_alert_time = alerts_.front().first.triggerTimeNs();
                }
            }
            
            // 计算等待超时时间（如果有的话）
            int timeout = -1; // 默认无限等待
            
            if (next_alert_time != std::numeric_limits<UnixNanos>::max()) {
                UnixNanos now = timestampNs();
                if (next_alert_time <= now) {
                    // 已经到达触发时间，立即检查
                    checkAlerts();
                    continue;
                } else {
                    // 计算毫秒超时时间
                    timeout = static_cast<int>((next_alert_time - now) / 1000000); // 转换为毫秒
                    // 至少等待1毫秒
                    if (timeout <= 0) {
                        timeout = 1;
                    }
                }
            }
            
            // 使用epoll等待事件或超时
            int num_events = epoll_wait(epoll_fd_, events, MAX_EVENTS, timeout);
            
            if (num_events == -1) {
                if (errno != EINTR) {
                    // 发生错误，但忽略EINTR（被信号中断）
                    // 在实际应用中可能需要记录日志
                }
                continue;
            }
            
            // 处理所有事件
            bool has_eventfd_trigger = false;
            for (int i = 0; i < num_events; i++) {
                if (events[i].data.fd == event_fd_) {
                    has_eventfd_trigger = true;
                    
                    // 清除eventfd计数
                    uint64_t value;
                    read(event_fd_, &value, sizeof(value));
                }
            }
            
            // 检查触发条件
            if (has_eventfd_trigger || num_events == 0) { // 收到事件或超时
                if (running_.load(std::memory_order_acquire)) {
                    checkAlerts();
                }
            }
        }
    });
#else
    timer_thread_ = std::thread([this]() {
        while (running_.load(std::memory_order_acquire)) {
            UnixNanos next_alert_time = std::numeric_limits<UnixNanos>::max();
            
            {
                std::lock_guard<std::mutex> lock(alerts_mutex_);
                if (!alerts_.empty()) {
                    next_alert_time = alerts_.front().first.triggerTimeNs();
                }
            }
            
            if (next_alert_time != std::numeric_limits<UnixNanos>::max()) {
                UnixNanos now = timestampNs();
                if (next_alert_time <= now) {
                    checkAlerts();
                } else {
                    auto wait_time_ns = next_alert_time - now;
                    
                    std::unique_lock<std::mutex> lock(alerts_mutex_);
                    alerts_cv_.wait_for(lock, 
                                      std::chrono::nanoseconds(wait_time_ns),
                                      [this]() { return !running_.load(std::memory_order_acquire); });
                    
                    if (running_.load(std::memory_order_acquire)) {
                        checkAlerts();
                    }
                }
            } else {
                std::unique_lock<std::mutex> lock(alerts_mutex_);
                alerts_cv_.wait(lock, 
                               [this]() { 
                                   return !alerts_.empty() || !running_.load(std::memory_order_acquire); 
                               });
                
                if (running_.load(std::memory_order_acquire)) {
                    checkAlerts();
                }
            }
        }
    });
#endif
}

RealtimeClock::~RealtimeClock() {
    running_.store(false, std::memory_order_release);
    
#if BACKTEST_USE_EPOLL
    triggerEpollWakeup();
    
    if (timer_thread_.joinable()) {
        timer_thread_.join();
    }
    
    cleanupEpoll();
#else
    alerts_cv_.notify_one();
    
    if (timer_thread_.joinable()) {
        timer_thread_.join();
    }
#endif
}

#if BACKTEST_USE_EPOLL
void RealtimeClock::setupEpoll() {
    epoll_fd_ = epoll_create1(0);
    if (epoll_fd_ == -1) {
        throw std::runtime_error("Failed to create epoll instance: " + std::string(strerror(errno)));
    }
    
    event_fd_ = eventfd(0, EFD_NONBLOCK);
    if (event_fd_ == -1) {
        close(epoll_fd_);
        throw std::runtime_error("Failed to create eventfd: " + std::string(strerror(errno)));
    }
    
    // 将eventfd添加到epoll监控
    struct epoll_event ev;
    ev.events = EPOLLIN;
    ev.data.fd = event_fd_;
    if (epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, event_fd_, &ev) == -1) {
        close(event_fd_);
        close(epoll_fd_);
        throw std::runtime_error("Failed to add eventfd to epoll: " + std::string(strerror(errno)));
    }
}

void RealtimeClock::cleanupEpoll() {
    if (event_fd_ != -1) {
        close(event_fd_);
        event_fd_ = -1;
    }
    
    if (epoll_fd_ != -1) {
        close(epoll_fd_);
        epoll_fd_ = -1;
    }
}

void RealtimeClock::triggerEpollWakeup() {
    if (event_fd_ != -1) {
        uint64_t value = 1;
        write(event_fd_, &value, sizeof(value));
    }
}
#endif

UnixNanos RealtimeClock::timestampNs() const {
    auto now = std::chrono::system_clock::now();
    auto duration = now.time_since_epoch();
    auto nanos = std::chrono::duration_cast<std::chrono::nanoseconds>(duration).count();
    return static_cast<UnixNanos>(nanos);
}

void RealtimeClock::setTimeAlertNs(const std::string& name, UnixNanos trigger_time_ns,
                                 std::function<void(const TimeEvent&)> callback) {
    TimeEvent event(name, trigger_time_ns);
    
#if BACKTEST_USE_EPOLL
    {
        std::lock_guard<std::mutex> lock(alerts_mutex_);
        alerts_.emplace_back(event, callback);
        
        std::sort(alerts_.begin(), alerts_.end(), 
                [](const auto& a, const auto& b) {
                    return a.first.triggerTimeNs() < b.first.triggerTimeNs();
                });
    }
    
    triggerEpollWakeup();
#else
    {
        std::lock_guard<std::mutex> lock(alerts_mutex_);
        alerts_.emplace_back(event, callback);
        
        std::sort(alerts_.begin(), alerts_.end(), 
                [](const auto& a, const auto& b) {
                    return a.first.triggerTimeNs() < b.first.triggerTimeNs();
                });
    }
    
    alerts_cv_.notify_one();
#endif
    
    checkAlerts();
}

void RealtimeClock::checkAlerts() {
    UnixNanos current = timestampNs();
    std::vector<std::pair<TimeEvent, std::function<void(const TimeEvent&)>>> triggered;
    
    {
#if BACKTEST_USE_EPOLL
        std::lock_guard<std::mutex> lock(alerts_mutex_);
#else
        std::lock_guard<std::mutex> lock(alerts_mutex_);
#endif
        auto it = alerts_.begin();
        while (it != alerts_.end()) {
            if (it->first.triggerTimeNs() <= current) {
                triggered.emplace_back(std::move(*it));
                it = alerts_.erase(it);
            } else {
                // 因为已经排序，一旦遇到未触发的，后面都不会触发
                break;
            }
        }
    }
    
    for (const auto& alert_pair : triggered) {
        alert_pair.second(alert_pair.first);
    }
}

/**
 * HybridClock 实现
 */
HybridClock::HybridClock(bool realtime)
    : is_realtime_(realtime),
      test_clock_(std::make_unique<TestClock>()),
      realtime_clock_(std::make_unique<RealtimeClock>()) {
}

UnixNanos HybridClock::timestampNs() const {
    if (is_realtime_.load(std::memory_order_acquire)) {
        return realtime_clock_->timestampNs();
    } else {
        return test_clock_->timestampNs();
    }
}

void HybridClock::setTimeAlertNs(const std::string& name, UnixNanos trigger_time_ns,
                                std::function<void(const TimeEvent&)> callback) {
    if (is_realtime_.load(std::memory_order_acquire)) {
        realtime_clock_->setTimeAlertNs(name, trigger_time_ns, callback);
    } else {
        test_clock_->setTimeAlertNs(name, trigger_time_ns, callback);
    }
}

void HybridClock::makeStatic() {
    bool expected = true;
    if (is_realtime_.compare_exchange_strong(expected, false, 
                                          std::memory_order_acq_rel,
                                          std::memory_order_acquire)) {
        // 如果成功从实时切换到静态，则设置静态时钟的初始时间为当前实时时间
        test_clock_->setTime(realtime_clock_->timestampNs());
    }
}

void HybridClock::makeRealtime() {
    is_realtime_.store(true, std::memory_order_release);
}

void HybridClock::setStaticTime(UnixNanos time_ns) {
    if (!is_realtime_.load(std::memory_order_acquire)) {
        test_clock_->setTime(time_ns);
    }
}

std::vector<TimeEvent> HybridClock::advanceStaticTime(UnixNanos to_time_ns) {
    if (!is_realtime_.load(std::memory_order_acquire)) {
        return test_clock_->advanceTime(to_time_ns, true);
    }
    return {};
}

} // namespace backtest 