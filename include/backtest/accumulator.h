// -------------------------------------------------------------------------------------------------
//  Copyright (C) 2023-2025. All rights reserved.
//
//  Licensed under the MIT License.
// -------------------------------------------------------------------------------------------------

#pragma once

#include <vector>
#include <cstdint>
#include <memory>
#include <algorithm>

#include "backtest/data.h"

namespace backtest {

class TestClock;
class TimeEventHandler;

/**
 * @brief 提供积累和排空时间事件处理程序的方法。
 */
class TimeEventAccumulator {
public:
    TimeEventAccumulator() = default;

    /**
     * @brief 将给定的时钟推进到to_time_ns。
     * 
     * @param clock 要推进的时钟
     * @param to_time_ns 目标时间戳（纳秒）
     * @param set_time 是否设置时钟时间
     */
    void advanceClock(TestClock* clock, UnixNanos to_time_ns, bool set_time);

    /**
     * @brief 按排序顺序清空累积的时间事件处理程序（按事件的ts_event排序）。
     * 
     * @return 排序后的事件处理程序列表
     */
    std::vector<std::unique_ptr<TimeEventHandler>> drain();

private:
    std::vector<std::unique_ptr<TimeEventHandler>> event_handlers_;
};

} // namespace backtest 