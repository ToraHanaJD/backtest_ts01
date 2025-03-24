#pragma once

#include "backtest/data.h"
#include "backtest/execution_client.h"
#include "backtest/clock.h"

namespace backtest {

/**
 * @brief 策略基类
 */
class Strategy {
public:
    virtual ~Strategy() = default;
    
    /**
     * @brief 处理K线数据
     */
    virtual void onBar(const Bar& bar) = 0;
    
    /**
     * @brief 处理报价数据
     */
    virtual void onQuote(const QuoteTick& quote) = 0;
    
    /**
     * @brief 处理成交数据
     */
    virtual void onTrade(const TradeTick& trade) = 0;
    
    /**
     * @brief 处理订单簿变化
     */
    virtual void onOrderBookDelta(const OrderBookDelta& delta) = 0;
    
    /**
     * @brief 处理订单成交
     */
    virtual void onFill(const Fill& fill) = 0;
};

} // namespace backtest 