// -------------------------------------------------------------------------------------------------
//  Copyright (C) 2023-2025. All rights reserved.
//
//  Licensed under the MIT License.
// -------------------------------------------------------------------------------------------------

#include "backtest/timer.h"

namespace backtest {

TimeEvent::TimeEvent(std::string name, UnixNanos trigger_time_ns)
    : name_(std::move(name)), 
      trigger_time_ns_(trigger_time_ns) {
}

TimeEventHandler::TimeEventHandler(TimeEvent event, TimeEventCallback callback)
    : event_(std::move(event)), 
      callback_(std::move(callback)) {
}

void TimeEventHandler::run() {
    // 如果事件已取消或无效，不执行回调
    if (getStatus() == Status::CANCELLED || !event_.isValid()) {
        return;
    }
    
    // 尝试设置状态为运行中
    Status expected = Status::PENDING;
    if (status_ == expected) {
        status_ = Status::RUNNING;
    } else {
        return;
    }
    
    try {
        callback_(event_);
        
        event_.setTriggered(true);
        
        status_ = Status::COMPLETED;
    } catch (...) {
        // 出现异常时，恢复状态为待定
        status_ = Status::PENDING;
        throw;
    }
}

} // namespace backtest 