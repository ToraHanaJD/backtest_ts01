// -------------------------------------------------------------------------------------------------
//  Copyright (C) 2023-2025. All rights reserved.
//
//  Licensed under the MIT License.
// -------------------------------------------------------------------------------------------------

#pragma once

#include <cstdint>
#include <string>
#include <functional>
#include <atomic>
#include <mutex>
#include "backtest/data.h"
namespace backtest {

/**
 * @brief 表示时间事件的类
 */
class TimeEvent {
public:
    /**
     * @brief 创建一个新的TimeEvent
     * 
     * @param name 事件名称
     * @param trigger_time_ns 事件触发时间戳
     */
    TimeEvent(std::string name, UnixNanos trigger_time_ns);
    
    TimeEvent() : name_(""), trigger_time_ns_(0), triggered_(false), valid_(true) {}
    
    TimeEvent(const TimeEvent& other) 
        : name_(other.name_), 
          trigger_time_ns_(other.trigger_time_ns_),
          triggered_(other.triggered_),
          valid_(other.valid_) {}
    
    TimeEvent(TimeEvent&& other) 
        : name_(std::move(other.name_)), 
          trigger_time_ns_(other.trigger_time_ns_),
          triggered_(other.triggered_),
          valid_(other.valid_) {}
    
    TimeEvent& operator=(const TimeEvent& other) {
        if (this != &other) {
            name_ = other.name_;
            trigger_time_ns_ = other.trigger_time_ns_;
            triggered_ = other.triggered_;
            valid_ = other.valid_;
        }
        return *this;
    }
    
    TimeEvent& operator=(TimeEvent&& other) {
        if (this != &other) {
            name_ = std::move(other.name_);
            trigger_time_ns_ = other.trigger_time_ns_;
            triggered_ = other.triggered_;
            valid_ = other.valid_;
        }
        return *this;
    }
    
    /**
     * @brief 获取事件名称
     */
    const std::string& name() const { return name_; }
    
    /**
     * @brief 获取事件触发时间戳
     */
    UnixNanos triggerTimeNs() const { return trigger_time_ns_; }
    
    /**
     * @brief 设置已触发标志
     */
    void setTriggered(bool triggered) { triggered_ = triggered; }
    
    /**
     * @brief 检查事件是否已触发
     */
    bool isTriggered() const { return triggered_; }
    
    /**
     * @brief 使事件无效
     */
    void invalidate() { valid_ = false; }
    
    /**
     * @brief 检查事件是否有效
     */
    bool isValid() const { return valid_; }

private:
    std::string name_;           // 事件名称
    UnixNanos trigger_time_ns_;  // 事件触发时间戳
    bool triggered_;             // 事件是否已触发
    bool valid_;                 // 事件是否有效
};

/**
 * @brief 时间事件回调类型
 */
using TimeEventCallback = std::function<void(const TimeEvent&)>;

/**
 * @brief 时间事件处理程序
 */
class TimeEventHandler {
public:
    /**
     * @brief 创建一个新的TimeEventHandler
     * 
     * @param event 关联的事件
     * @param callback 回调函数
     */
    TimeEventHandler(TimeEvent event, TimeEventCallback callback);
    
    /**
     * @brief 运行处理程序
     */
    void run();

    /// 获取关联的事件
    const TimeEvent& getEvent() const { return event_; }
    
    /// 设置处理程序状态
    enum class Status {
        PENDING,
        RUNNING,
        COMPLETED,
        CANCELLED
    };
    
    Status getStatus() const { 
        return status_; 
    }
    
    void setStatus(Status status) { 
        status_ = status; 
    }
    
    /// 取消处理程序
    void cancel() { 
        Status expected = Status::PENDING;
        if (status_ == expected) {
            status_ = Status::CANCELLED;
        }
    }

private:
    TimeEvent event_;             // 关联的事件
    TimeEventCallback callback_;  // 回调函数
    Status status_{Status::PENDING}; // 处理程序状态
};

} // namespace backtest 