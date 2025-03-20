// -------------------------------------------------------------------------------------------------
//  Copyright (C) 2023-2025. All rights reserved.
//
//  Licensed under the MIT License.
// -------------------------------------------------------------------------------------------------

#include "backtest/accumulator.h"
#include "backtest/clock.h"
#include "backtest/timer.h"

namespace backtest {

void TimeEventAccumulator::advanceClock(TestClock* clock, UnixNanos to_time_ns, bool set_time) {
    auto events = clock->advanceTime(to_time_ns, set_time);
    auto handlers = clock->matchHandlers(events);
    event_handlers_.insert(event_handlers_.end(), 
                         std::make_move_iterator(handlers.begin()),
                         std::make_move_iterator(handlers.end()));
}

std::vector<std::unique_ptr<TimeEventHandler>> TimeEventAccumulator::drain() {
    // 按ts_event排序
    std::sort(event_handlers_.begin(), event_handlers_.end(),
              [](const auto& a, const auto& b) {
                  return a->getEvent().triggerTimeNs() < b->getEvent().triggerTimeNs();
              });

    std::vector<std::unique_ptr<TimeEventHandler>> result;
    result.swap(event_handlers_);
    return result;
}

} // namespace backtest 