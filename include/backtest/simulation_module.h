// -------------------------------------------------------------------------------------------------
//  Copyright (C) 2023-2025. All rights reserved.
//
//  Licensed under the MIT License.
// -------------------------------------------------------------------------------------------------

#pragma once

#include "backtest/data.h"

namespace backtest {

/**
 * @brief 模拟模块基类
 */
class SimulationModule {
public:
    virtual ~SimulationModule() = default;
    
    /**
     * @brief 处理模拟事件
     * @param ts_now 当前时间戳
     */
    virtual void process(UnixNanos ts_now) = 0;
};

} // namespace backtest 