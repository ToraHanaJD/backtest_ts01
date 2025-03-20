// -------------------------------------------------------------------------------------------------
//  Copyright (C) 2023-2025. All rights reserved.
//
//  Licensed under the MIT License.
// -------------------------------------------------------------------------------------------------

#include "backtest/clock.h"
#include <iostream>
#include <sstream>
#include <chrono>
#include <atomic>
#include <vector>
#include <thread>
#include <iomanip>
#include <cassert>
#include <mutex>

using namespace backtest;

// 互斥锁用于保护输出
std::mutex cout_mutex;

// 格式化输出纳秒时间戳
std::string formatTime(UnixNanos time_ns) {
    auto time_ms = time_ns / 1000000;
    auto ms = time_ms % 1000;
    auto time_sec = time_ms / 1000;
    auto sec = time_sec % 60;
    auto min = (time_sec / 60) % 60;
    auto hour = (time_sec / 3600) % 24;
    
    std::stringstream ss;
    ss << std::setfill('0') << std::setw(2) << hour << ":"
       << std::setfill('0') << std::setw(2) << min << ":"
       << std::setfill('0') << std::setw(2) << sec << "."
       << std::setfill('0') << std::setw(3) << ms;
    
    return ss.str();
}

// BASIC
void testBasicFunctionality() {
    std::cout << "\n======BASIC======\n" << std::endl;
    
    RealtimeClock clock;
    
    UnixNanos now = clock.timestampNs();
    std::cout << "CURRENT TIME: " << formatTime(now) << std::endl;
    
    std::cout << "休眠500毫秒..." << std::endl;
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    
    UnixNanos after = clock.timestampNs();
    std::cout << "CURRENT TIME: " << formatTime(after) << std::endl;
    
    UnixNanos diff = after - now;
    std::cout << "时间差: " << diff / 1000000 << "毫秒" << std::endl;
    
    assert(diff >= 450000000 && diff <= 600000000);
    
    std::cout << "基本功能测试通过!" << std::endl;
}

// 测试单个提醒
void testSingleAlert() {
    std::cout << "\n===== 测试单个提醒 =====\n" << std::endl;
    
    RealtimeClock clock;
    std::atomic<bool> alert_triggered{false};
    
    UnixNanos now = clock.timestampNs();
    std::cout << "CURRENT TIME: " << formatTime(now) << std::endl;
    
    // 设置300毫秒后触发的提醒
    UnixNanos trigger_time = now + 300000000; // 300毫秒
    std::cout << "设置提醒在: " << formatTime(trigger_time) << std::endl;
    
    clock.setTimeAlertNs("test_alert", trigger_time, 
        [&alert_triggered](const TimeEvent& event) {
            std::lock_guard<std::mutex> lock(cout_mutex);
            std::cout << "提醒触发: " << event.name() 
                      << " 在时间: " << formatTime(event.triggerTimeNs()) << std::endl;
            alert_triggered.store(true);
        });
    
    std::cout << "等待提醒触发..." << std::endl;
    
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    
    assert(alert_triggered.load());
    std::cout << "单个提醒测试通过!" << std::endl;
}

// 测试多个提醒
void testMultipleAlerts() {
    std::cout << "\n===== 测试多个提醒 =====\n" << std::endl;
    
    RealtimeClock clock;
    std::atomic<int> alerts_triggered{0};
    
    UnixNanos now = clock.timestampNs();
    std::cout << "CURRENT TIME: " << formatTime(now) << std::endl;
    
    const int NUM_ALERTS = 5;
    for (int i = 0; i < NUM_ALERTS; i++) {
        UnixNanos delay = (i + 1) * 100000000; // 100毫秒递增
        UnixNanos trigger_time = now + delay;
        
        std::string alert_name = "alert_" + std::to_string(i+1);
        std::cout << "设置提醒 " << alert_name << " 在: " 
                  << formatTime(trigger_time) << " (+" << delay/1000000 << "ms)" << std::endl;
        
        clock.setTimeAlertNs(alert_name, trigger_time, 
            [&alerts_triggered, alert_name](const TimeEvent& event) {
                std::lock_guard<std::mutex> lock(cout_mutex);
                std::cout << "提醒触发: " << alert_name 
                          << " 在时间: " << formatTime(event.triggerTimeNs()) << std::endl;
                alerts_triggered++;
            });
    }
    
    std::cout << "等待所有提醒触发..." << std::endl;
    std::this_thread::sleep_for(std::chrono::milliseconds(600)); // 等待足够长时间
    
    assert(alerts_triggered.load() == NUM_ALERTS);
    std::cout << "多个提醒测试通过! 触发了 " << alerts_triggered.load() << " 个提醒" << std::endl;
}

// 测试乱序添加提醒
void testOutOfOrderAlerts() {
    std::cout << "\n======测试乱序添加提醒======\n" << std::endl;
    
    RealtimeClock clock;
    std::vector<std::string> triggered_alerts;
    std::mutex alerts_mutex;
    
    // 获取当前时间
    UnixNanos now = clock.timestampNs();
    std::cout << "CURRENT TIME: " << formatTime(now) << std::endl;
    
    // 乱序添加提醒（先添加较晚的提醒）
    UnixNanos trigger_time_3 = now + 300000000; // 300毫秒
    std::cout << "设置提醒3在: " << formatTime(trigger_time_3) << std::endl;
    
    clock.setTimeAlertNs("alert_3", trigger_time_3, 
        [&triggered_alerts, &alerts_mutex](const TimeEvent& event) {
            std::lock_guard<std::mutex> lock(cout_mutex);
            std::cout << "提醒触发: " << event.name() 
                      << " 在时间: " << formatTime(event.triggerTimeNs()) << std::endl;
            
            std::lock_guard<std::mutex> alerts_lock(alerts_mutex);
            triggered_alerts.push_back(event.name());
        });
    
    UnixNanos trigger_time_1 = now + 100000000; // 100毫秒
    std::cout << "设置提醒1在: " << formatTime(trigger_time_1) << std::endl;
    
    clock.setTimeAlertNs("alert_1", trigger_time_1, 
        [&triggered_alerts, &alerts_mutex](const TimeEvent& event) {
            std::lock_guard<std::mutex> lock(cout_mutex);
            std::cout << "提醒触发: " << event.name() 
                      << " 在时间: " << formatTime(event.triggerTimeNs()) << std::endl;
            
            std::lock_guard<std::mutex> alerts_lock(alerts_mutex);
            triggered_alerts.push_back(event.name());
        });
    
    UnixNanos trigger_time_2 = now + 200000000; // 200毫秒
    std::cout << "设置提醒2在: " << formatTime(trigger_time_2) << std::endl;
    
    clock.setTimeAlertNs("alert_2", trigger_time_2, 
        [&triggered_alerts, &alerts_mutex](const TimeEvent& event) {
            std::lock_guard<std::mutex> lock(cout_mutex);
            std::cout << "提醒触发: " << event.name() 
                      << " 在时间: " << formatTime(event.triggerTimeNs()) << std::endl;
            
            std::lock_guard<std::mutex> alerts_lock(alerts_mutex);
            triggered_alerts.push_back(event.name());
        });
    
    // 等待所有提醒触发
    std::cout << "等待所有提醒触发..." << std::endl;
    std::this_thread::sleep_for(std::chrono::milliseconds(400)); // 等待足够长时间
    
    // 验证所有提醒已按顺序触发
    std::lock_guard<std::mutex> alerts_lock(alerts_mutex);
    assert(triggered_alerts.size() == 3);
    
    std::cout << "触发顺序：";
    for (const auto& alert : triggered_alerts) {
        std::cout << alert << " ";
    }
    std::cout << std::endl;
    
    assert(triggered_alerts[0] == "alert_1");
    assert(triggered_alerts[1] == "alert_2");
    assert(triggered_alerts[2] == "alert_3");
    
    std::cout << "=====PASSED=====乱序添加提醒测试通过！提醒按照正确的时间顺序触发" << std::endl;
}

// 测试性能（添加大量提醒）
void testPerformance() {
    std::cout << "\n===== PERFORMANCE =====\n" << std::endl;
    
    RealtimeClock clock;
    std::atomic<int> alerts_triggered{0};
    
    // 获取当前时间
    UnixNanos now = clock.timestampNs();
    std::cout << "CURRENT TIME: " << formatTime(now) << std::endl;
    
    // 添加大量提醒
    const int NUM_ALERTS = 1000;
    std::cout << "添加 " << NUM_ALERTS << " 个提醒..." << std::endl;
    
    auto start_time = std::chrono::high_resolution_clock::now();
    
    for (int i = 0; i < NUM_ALERTS; i++) {
        // 所有提醒在100-200毫秒内触发
        UnixNanos delay = 100000000 + (i * 100000); // 100毫秒 + 增量
        UnixNanos trigger_time = now + delay;
        
        clock.setTimeAlertNs("perf_alert_" + std::to_string(i), trigger_time, 
            [&alerts_triggered](const TimeEvent&) {
                alerts_triggered++;
            });
    }
    
    auto end_time = std::chrono::high_resolution_clock::now();
    auto add_duration = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time).count();
    
    std::cout << "添加 " << NUM_ALERTS << " 个提醒用时: " << add_duration << " 微秒" << std::endl;
    std::cout << "平均每个提醒添加用时: " << static_cast<double>(add_duration) / NUM_ALERTS << " 微秒" << std::endl;
    
    // 等待所有提醒触发
    std::cout << "等待所有提醒触发..." << std::endl;
    
    // 等待足够长的时间
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    
    // 验证所有提醒已触发
    int triggered = alerts_triggered.load();
    std::cout << "触发了 " << triggered << " 个提醒 (预期 " << NUM_ALERTS << ")" << std::endl;
    assert(triggered == NUM_ALERTS);
    
    std::cout << "=====PERFORMANCE TEST PASSED!=====" << std::endl;
}

// 测试紧急提醒（立即需要触发的提醒）
void testImmediateAlert() {
    std::cout << "\n===== 测试紧急提醒 =====\n" << std::endl;
    
    RealtimeClock clock;
    std::atomic<bool> alert_triggered{false};
    
    // 获取当前时间
    UnixNanos now = clock.timestampNs();
    std::cout << "当前时间: " << formatTime(now) << std::endl;
    
    // 设置一个已经过期的提醒（应该立即触发）
    UnixNanos trigger_time = now - 1000000; // 1毫秒前
    std::cout << "设置过期提醒在: " << formatTime(trigger_time) << " (当前时间-1ms)" << std::endl;
    
    auto start_time = std::chrono::high_resolution_clock::now();
    
    clock.setTimeAlertNs("immediate_alert", trigger_time, 
        [&alert_triggered, &start_time](const TimeEvent& event) {
            auto triggered_time = std::chrono::high_resolution_clock::now();
            auto duration = std::chrono::duration_cast<std::chrono::microseconds>(
                triggered_time - start_time).count();
            
            std::lock_guard<std::mutex> lock(cout_mutex);
            std::cout << "紧急提醒触发: " << event.name() 
                      << " 触发延迟: " << duration << " 微秒" << std::endl;
            alert_triggered.store(true);
        });
    
    // 等待一小段时间，确保提醒有时间触发
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    
    // 验证提醒已触发
    assert(alert_triggered.load());
    std::cout << "紧急提醒测试通过!" << std::endl;
}

int main() {
    std::cout << "=== RealtimeClock 测试开始 ===\n" << std::endl;
    
#ifdef BACKTEST_USE_EPOLL
    std::cout << "epoll VERSION\n" << std::endl;
#else
    std::cout << "CV VERSION\n" << std::endl;
#endif
    
    try {
        testBasicFunctionality();
        testSingleAlert();
        testMultipleAlerts();
        testOutOfOrderAlerts();
        testImmediateAlert();
        testPerformance();
        
        std::cout << "\n====PASS====" << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "FAILED: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
} 