// -------------------------------------------------------------------------------------------------
//  Copyright (C) 2023-2025. All rights reserved.
//
//  Licensed under the MIT License.
// -------------------------------------------------------------------------------------------------

#pragma once

#include <cstdint>
#include <memory>
#include <vector>
#include <string>
#include <functional>
#include <atomic>
#include <thread>
#include <mutex>
#include <condition_variable>
#include "backtest/timer.h"

#if defined(__linux__)
#define BACKTEST_USE_EPOLL 1
#include <sys/epoll.h>
#include <unistd.h>
#include <fcntl.h>
#endif

namespace backtest {

/**
 * @brief 时钟抽象基类
 */
class Clock {
public:
    virtual ~Clock() = default;

    /**
     * @brief 获取当前时间戳（纳秒）
     */
    virtual UnixNanos timestampNs() const = 0;

    /**
     * @brief 设置时间提醒
     * 
     * @param name 提醒名称
     * @param trigger_time_ns 触发时间（纳秒）
     * @param callback 回调函数
     */
    virtual void setTimeAlertNs(const std::string& name, UnixNanos trigger_time_ns,
                              std::function<void(const TimeEvent&)> callback) = 0;
};

/**
 * @brief 用于测试的时钟实现
 */
class TestClock : public Clock {
public:

    TestClock();
    ~TestClock() = default;
    /**
     * @brief 获取当前时间戳（纳秒）
     */
    UnixNanos timestampNs() const override;

    /**
     * @brief 设置时钟的当前时间
     * 
     * @param time_ns 时间戳（纳秒）
     */
    void setTime(UnixNanos time_ns);

    /**
     * @brief 设置时间提醒
     * 
     * @param name 提醒名称
     * @param trigger_time_ns 触发时间（纳秒）
     */
    void setTimeAlertNs(const std::string& name, UnixNanos trigger_time_ns,
                       std::function<void(const TimeEvent&)> callback) override;

    /**
     * @brief 将时钟推进到指定时间
     * 
     * @param to_time_ns 目标时间戳（纳秒）
     * @param set_time 是否设置时钟时间
     * @return 触发的事件列表
     */
    std::vector<TimeEvent> advanceTime(UnixNanos to_time_ns, bool set_time = true);

    /**
     * @brief 匹配事件的处理程序
     * 
     * @param events 事件列表
     * @return 处理程序列表
     */
    std::vector<std::unique_ptr<TimeEventHandler>> matchHandlers(const std::vector<TimeEvent>& events);

private:
    std::atomic<UnixNanos> current_time_ns_;
    std::vector<std::pair<TimeEvent, std::function<void(const TimeEvent&)>>> alerts_;
};

/**
 * @brief 真实时钟实现，提供系统真实时间
 */
class RealtimeClock : public Clock {
public:
    RealtimeClock();
    ~RealtimeClock();

    /**
     * @brief 获取当前时间戳（纳秒）
     * @return 当前系统时间戳（纳秒）
     */
    UnixNanos timestampNs() const override;

    /**
     * @brief 设置时间提醒
     * 
     * @param name 提醒名称
     * @param trigger_time_ns 触发时间（纳秒）
     */
    void setTimeAlertNs(const std::string& name, UnixNanos trigger_time_ns,
                       std::function<void(const TimeEvent&)> callback) override;

private:
    std::vector<std::pair<TimeEvent, std::function<void(const TimeEvent&)>>> alerts_;
    
    std::atomic<bool> running_;
    std::thread timer_thread_;
    
    std::mutex alerts_mutex_;
    
#if BACKTEST_USE_EPOLL
    // Linux下使用epoll机制
    int epoll_fd_;
    int event_fd_;
    void setupEpoll();
    void cleanupEpoll();
    void triggerEpollWakeup();
#else
    // 条件变量用于非Linux平台
    std::condition_variable alerts_cv_;
#endif
    
    // 检查和触发提醒的函数
    void checkAlerts();
};

/**
 * @brief 混合时钟实现，支持在测试和实时模式之间切换
 */
class HybridClock : public Clock {
public:
    /**
     * @brief 创建一个新的HybridClock实例
     * @param realtime 是否初始化为实时模式
     */
    explicit HybridClock(bool realtime = false);

    /**
     * @brief 获取当前时间戳（纳秒）
     */
    UnixNanos timestampNs() const override;

    /**
     * @brief 设置时间提醒
     * 
     * @param name 提醒名称
     * @param trigger_time_ns 触发时间（纳秒）
     */
    void setTimeAlertNs(const std::string& name, UnixNanos trigger_time_ns,
                       std::function<void(const TimeEvent&)> callback) override;

    /**
     * @brief 切换到测试模式
     */
    void makeStatic();

    /**
     * @brief 切换到实时模式
     */
    void makeRealtime();
    
    /**
     * @brief 设置静态时钟时间
     */
    void setStaticTime(UnixNanos time_ns);
    
    /**
     * @brief 推进静态时钟
     */
    std::vector<TimeEvent> advanceStaticTime(UnixNanos to_time_ns);

private:
    std::atomic<bool> is_realtime_;
    
    // 内部的时钟实现
    std::unique_ptr<TestClock> test_clock_;
    std::unique_ptr<RealtimeClock> realtime_clock_;
};

} // namespace backtest 