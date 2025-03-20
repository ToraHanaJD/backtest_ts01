// -------------------------------------------------------------------------------------------------
//  Copyright (C) 2023-2025. All rights reserved.
//
//  Licensed under the MIT License.
// -------------------------------------------------------------------------------------------------

#include <iostream>
#include <iomanip>
#include <vector>
#include <string>
#include <cassert>

#include "backtest/order_book.h"
#include "backtest/data.h"

using namespace backtest;

#define TEST_ASSERT(condition, message) \
    do { \
        if (!(condition)) { \
            std::cerr << "测试失败: " << message << " at line " << __LINE__ << std::endl; \
            return false; \
        } \
    } while (0)

bool testOrderBookInitialState() {
    std::cout << "测试订单簿初始状态..." << std::endl;
    
    InstrumentId instrument_id = 12345;
    OrderBook book(instrument_id, BookType::L2);
    
    TEST_ASSERT(book.instrumentId() == instrument_id, "订单簿的金融工具ID应该与初始化的ID匹配");
    TEST_ASSERT(book.isEmpty(), "新创建的订单簿应该为空");
    TEST_ASSERT(book.bids().empty(), "新创建的订单簿不应有买单");
    TEST_ASSERT(book.asks().empty(), "新创建的订单簿不应有卖单");
    TEST_ASSERT(book.type() == BookType::L2, "订单簿类型应为L2");
    
    TEST_ASSERT(book.bestBidPrice() == 0.0, "空订单簿的最优买价应为0");
    TEST_ASSERT(book.bestAskPrice() == std::numeric_limits<double>::max(), "空订单簿的最优卖价应为最大值");
    TEST_ASSERT(book.midPrice() == 0.0, "空订单簿的中间价应为0");
    TEST_ASSERT(book.spread() == 0.0, "空订单簿的价差应为0");
    
    std::cout << "订单簿初始状态测试通过" << std::endl;
    return true;
}

bool testAddOrders() {
    std::cout << "测试添加订单..." << std::endl;
    
    InstrumentId instrument_id = 12345;
    OrderBook book(instrument_id, BookType::L2);
    UnixNanos now = 1000000000;
    
    // 添加买单
    OrderBookDelta delta1(instrument_id, OrderBookAction::ADD, true, 100.0, 1.5, now, now);
    book.applyDelta(delta1);
    
    TEST_ASSERT(!book.isEmpty(), "添加买单后订单簿不应为空");
    TEST_ASSERT(book.bids().size() == 1, "应该有一个买单价格级别");
    TEST_ASSERT(book.bestBidPrice() == 100.0, "最优买价应为100.0");
    TEST_ASSERT(book.bidSize(100.0) == 1.5, "价格为100.0的买单数量应为1.5");
    TEST_ASSERT(book.midPrice() == 0.0, "只有买单时中间价应为0");
    TEST_ASSERT(book.spread() == 0.0, "只有买单时价差应为0");
    
    // 添加卖单
    OrderBookDelta delta2(instrument_id, OrderBookAction::ADD, false, 101.0, 2.0, now, now);
    book.applyDelta(delta2);
    
    TEST_ASSERT(!book.isEmpty(), "同时有买单和卖单时订单簿不应为空");
    TEST_ASSERT(book.asks().size() == 1, "应该有一个卖单价格级别");
    TEST_ASSERT(book.bestAskPrice() == 101.0, "最优卖价应为101.0");
    TEST_ASSERT(book.askSize(101.0) == 2.0, "价格为101.0的卖单数量应为2.0");
    
    // 验证中间价和价差
    TEST_ASSERT(std::abs(book.midPrice() - 100.5) < 0.0001, "中间价应为100.5");
    TEST_ASSERT(std::abs(book.spread() - 1.0) < 0.0001, "价差应为1.0");
    
    // 再添加几个买单和卖单
    OrderBookDelta delta3(instrument_id, OrderBookAction::ADD, true, 99.5, 3.0, now, now);
    OrderBookDelta delta4(instrument_id, OrderBookAction::ADD, true, 99.0, 5.0, now, now);
    OrderBookDelta delta5(instrument_id, OrderBookAction::ADD, false, 101.5, 2.5, now, now);
    OrderBookDelta delta6(instrument_id, OrderBookAction::ADD, false, 102.0, 1.0, now, now);
    
    book.applyDelta(delta3);
    book.applyDelta(delta4);
    book.applyDelta(delta5);
    book.applyDelta(delta6);
    
    // 验证买单和卖单数量
    TEST_ASSERT(book.bids().size() == 3, "应该有三个买单价格级别");
    TEST_ASSERT(book.asks().size() == 3, "应该有三个卖单价格级别");
    
    // 验证买单按价格降序排列
    std::vector<OrderBookLevel> bids = book.bids();
    TEST_ASSERT(bids[0].price() > bids[1].price() && bids[1].price() > bids[2].price(), 
               "买单应按价格降序排列");
    
    // 验证卖单按价格升序排列
    std::vector<OrderBookLevel> asks = book.asks();
    TEST_ASSERT(asks[0].price() < asks[1].price() && asks[1].price() < asks[2].price(), 
               "卖单应按价格升序排列");
    
    std::cout << "添加订单测试通过" << std::endl;
    return true;
}

bool testUpdateOrders() {
    std::cout << "测试更新订单..." << std::endl;
    
    InstrumentId instrument_id = 12345;
    OrderBook book(instrument_id, BookType::L2);
    UnixNanos now = 1000000000;
    
    // 添加一些初始订单
    OrderBookDelta delta1(instrument_id, OrderBookAction::ADD, true, 100.0, 1.5, now, now);
    OrderBookDelta delta2(instrument_id, OrderBookAction::ADD, false, 101.0, 2.0, now, now);
    book.applyDelta(delta1);
    book.applyDelta(delta2);
    
    // 更新买单
    OrderBookDelta updateBid(instrument_id, OrderBookAction::UPDATE, true, 100.0, 3.0, now, now);
    book.applyDelta(updateBid);
    
    TEST_ASSERT(book.bidSize(100.0) == 3.0, "更新后价格为100.0的买单数量应为3.0");
    
    // 更新卖单
    OrderBookDelta updateAsk(instrument_id, OrderBookAction::UPDATE, false, 101.0, 4.0, now, now);
    book.applyDelta(updateAsk);
    
    TEST_ASSERT(book.askSize(101.0) == 4.0, "更新后价格为101.0的卖单数量应为4.0");
    
    std::cout << "更新订单测试通过" << std::endl;
    return true;
}

// 测试删除订单
bool testDeleteOrders() {
    std::cout << "测试删除订单..." << std::endl;
    
    InstrumentId instrument_id = 12345;
    OrderBook book(instrument_id, BookType::L2);
    UnixNanos now = 1000000000;
    
    // 添加一些初始订单
    OrderBookDelta delta1(instrument_id, OrderBookAction::ADD, true, 100.0, 1.5, now, now);
    OrderBookDelta delta2(instrument_id, OrderBookAction::ADD, true, 99.0, 2.0, now, now);
    OrderBookDelta delta3(instrument_id, OrderBookAction::ADD, false, 101.0, 2.0, now, now);
    OrderBookDelta delta4(instrument_id, OrderBookAction::ADD, false, 102.0, 3.0, now, now);
    
    book.applyDelta(delta1);
    book.applyDelta(delta2);
    book.applyDelta(delta3);
    book.applyDelta(delta4);
    
    // 删除一个买单价格级别
    OrderBookDelta deleteBid(instrument_id, OrderBookAction::DELETE, true, 100.0, 0.0, now, now);
    book.applyDelta(deleteBid);
    
    TEST_ASSERT(book.bids().size() == 1, "删除后应该只有一个买单价格级别");
    TEST_ASSERT(book.bestBidPrice() == 99.0, "删除后的最优买价应为99.0");
    TEST_ASSERT(book.bidSize(100.0) == 0.0, "删除后价格为100.0的买单数量应为0");
    
    // 删除一个卖单价格级别
    OrderBookDelta deleteAsk(instrument_id, OrderBookAction::DELETE, false, 101.0, 0.0, now, now);
    book.applyDelta(deleteAsk);
    
    TEST_ASSERT(book.asks().size() == 1, "删除后应该只有一个卖单价格级别");
    TEST_ASSERT(book.bestAskPrice() == 102.0, "删除后的最优卖价应为102.0");
    TEST_ASSERT(book.askSize(101.0) == 0.0, "删除后价格为101.0的卖单数量应为0");
    
    std::cout << "删除订单测试通过" << std::endl;
    return true;
}

// 测试批量更新
bool testBatchUpdates() {
    std::cout << "Test Batch Updates..." << std::endl;
    
    InstrumentId instrument_id = 12345;
    OrderBook book(instrument_id, BookType::L2);
    UnixNanos now = 1000000000;
    
    // 创建一批更新
    std::vector<OrderBookDelta> deltas;
    
    // 添加买单
    deltas.emplace_back(instrument_id, OrderBookAction::ADD, true, 100.0, 1.5, now, now);
    deltas.emplace_back(instrument_id, OrderBookAction::ADD, true, 99.5, 2.0, now, now);
    deltas.emplace_back(instrument_id, OrderBookAction::ADD, true, 99.0, 3.0, now, now);
    
    // 添加卖单
    deltas.emplace_back(instrument_id, OrderBookAction::ADD, false, 101.0, 2.0, now, now);
    deltas.emplace_back(instrument_id, OrderBookAction::ADD, false, 101.5, 2.5, now, now);
    deltas.emplace_back(instrument_id, OrderBookAction::ADD, false, 102.0, 1.0, now, now);
    
    // 创建批量更新对象
    OrderBookDeltas batchUpdate(instrument_id, deltas, now, now);
    
    // 应用批量更新
    book.applyDeltas(batchUpdate);
    
    TEST_ASSERT(book.bids().size() == 3, "应该有三个买单价格级别");
    TEST_ASSERT(book.asks().size() == 3, "应该有三个卖单价格级别");
    TEST_ASSERT(book.bestBidPrice() == 100.0, "最优买价应为100.0");
    TEST_ASSERT(book.bestAskPrice() == 101.0, "最优卖价应为101.0");
    
    std::vector<OrderBookDelta> deltas2;
    
    deltas2.emplace_back(instrument_id, OrderBookAction::UPDATE, true, 100.0, 5.0, now, now);
    deltas2.emplace_back(instrument_id, OrderBookAction::DELETE, true, 99.0, 0.0, now, now);
    deltas2.emplace_back(instrument_id, OrderBookAction::UPDATE, false, 101.0, 3.0, now, now);
    deltas2.emplace_back(instrument_id, OrderBookAction::DELETE, false, 102.0, 0.0, now, now);
    
    OrderBookDeltas batchUpdate2(instrument_id, deltas2, now, now);
    book.applyDeltas(batchUpdate2);
    
    TEST_ASSERT(book.bids().size() == 2, "应该有两个买单价格级别");
    TEST_ASSERT(book.asks().size() == 2, "应该有两个卖单价格级别");
    TEST_ASSERT(book.bidSize(100.0) == 5.0, "价格为100.0的买单数量应为5.0");
    TEST_ASSERT(book.bidSize(99.0) == 0.0, "价格为99.0的买单数量应为0.0");
    TEST_ASSERT(book.askSize(101.0) == 3.0, "价格为101.0的卖单数量应为3.0");
    TEST_ASSERT(book.askSize(102.0) == 0.0, "价格为102.0的卖单数量应为0.0");
    
    std::cout << "批量更新测试通过" << std::endl;
    return true;
}

bool testClearOrderBook() {
    std::cout << "测试清空订单簿..." << std::endl;
    
    InstrumentId instrument_id = 12345;
    OrderBook book(instrument_id, BookType::L2);
    UnixNanos now = 1000000000;
    
    OrderBookDelta delta1(instrument_id, OrderBookAction::ADD, true, 100.0, 1.5, now, now);
    OrderBookDelta delta2(instrument_id, OrderBookAction::ADD, false, 101.0, 2.0, now, now);
    book.applyDelta(delta1);
    book.applyDelta(delta2);
    
    TEST_ASSERT(!book.isEmpty(), "添加订单后订单簿不应为空");
    
    book.clear();
    
    TEST_ASSERT(book.isEmpty(), "清空后订单簿应为空");
    TEST_ASSERT(book.bids().empty(), "清空后不应有买单");
    TEST_ASSERT(book.asks().empty(), "清空后不应有卖单");
    
    std::cout << "清空订单簿测试通过" << std::endl;
    return true;
}

bool testEdgeCases() {
    std::cout << "测试极端情况..." << std::endl;
    
    InstrumentId instrument_id = 12345;
    OrderBook book(instrument_id, BookType::L2);
    UnixNanos now = 1000000000;
    
    OrderBookDelta updateNonExistent(instrument_id, OrderBookAction::UPDATE, true, 100.0, 1.0, now, now);
    book.applyDelta(updateNonExistent);
    
    TEST_ASSERT(book.bidSize(100.0) == 1.0, "更新不存在的价格级别应该创建新级别");
    
    OrderBookDelta deleteNonExistent(instrument_id, OrderBookAction::DELETE, false, 101.0, 0.0, now, now);
    book.applyDelta(deleteNonExistent);
    
    TEST_ASSERT(book.askSize(101.0) == 0.0, "删除不存在的价格级别应该没有效果");
    
    double large_price = 1000000.0;
    double small_price = 0.0000001;
    
    OrderBookDelta largePrice(instrument_id, OrderBookAction::ADD, true, large_price, 1.0, now, now);
    OrderBookDelta smallPrice(instrument_id, OrderBookAction::ADD, false, small_price, 1.0, now, now);
    
    book.applyDelta(largePrice);
    book.applyDelta(smallPrice);
    
    TEST_ASSERT(book.bidSize(large_price) == 1.0, "应该能处理极大的价格");
    TEST_ASSERT(book.askSize(small_price) == 1.0, "应该能处理极小的价格");
    
    std::cout << "极端情况测试通过" << std::endl;
    return true;
}

bool testOneSidedBook() {
    std::cout << "测试只有一侧有订单的情况..." << std::endl;
    
    InstrumentId instrument_id = 12345;
    OrderBook book(instrument_id, BookType::L2);
    UnixNanos now = 1000000000;
    
    OrderBookDelta delta1(instrument_id, OrderBookAction::ADD, true, 100.0, 1.5, now, now);
    book.applyDelta(delta1);
    
    TEST_ASSERT(!book.isEmpty(), "只有买单时订单簿不应为空，因为isEmpty()现在检查两侧都为空");
    TEST_ASSERT(book.bids().size() == 1, "应该有一个买单价格级别");
    TEST_ASSERT(book.asks().empty(), "不应有卖单");
    TEST_ASSERT(book.midPrice() == 0.0, "只有买单时中间价应为0");
    TEST_ASSERT(book.spread() == 0.0, "只有买单时价差应为0");
    
    book.clear();
    OrderBookDelta delta2(instrument_id, OrderBookAction::ADD, false, 101.0, 2.0, now, now);
    book.applyDelta(delta2);
    
    TEST_ASSERT(!book.isEmpty(), "只有卖单时订单簿不应为空，因为isEmpty()现在检查两侧都为空");
    TEST_ASSERT(book.asks().size() == 1, "应该有一个卖单价格级别");
    TEST_ASSERT(book.bids().empty(), "不应有买单");
    TEST_ASSERT(book.midPrice() == 0.0, "只有卖单时中间价应为0");
    TEST_ASSERT(book.spread() == 0.0, "只有卖单时价差应为0");
    
    std::cout << "只有一侧有订单的测试通过" << std::endl;
    return true;
}

bool testBookDepth() {
    std::cout << "测试获取指定深度的订单簿状态..." << std::endl;
    
    InstrumentId instrument_id = 12345;
    OrderBook book(instrument_id, BookType::L2);
    UnixNanos now = 1000000000;
    
    OrderBookDelta delta1(instrument_id, OrderBookAction::ADD, true, 100.0, 1.0, now, now);
    OrderBookDelta delta2(instrument_id, OrderBookAction::ADD, true, 99.0, 2.0, now, now);
    OrderBookDelta delta3(instrument_id, OrderBookAction::ADD, true, 98.0, 3.0, now, now);
    OrderBookDelta delta4(instrument_id, OrderBookAction::ADD, true, 97.0, 4.0, now, now);
    OrderBookDelta delta5(instrument_id, OrderBookAction::ADD, true, 96.0, 5.0, now, now);
    
    book.applyDelta(delta1);
    book.applyDelta(delta2);
    book.applyDelta(delta3);
    book.applyDelta(delta4);
    book.applyDelta(delta5);
    
    OrderBookDelta delta6(instrument_id, OrderBookAction::ADD, false, 101.0, 1.5, now, now);
    OrderBookDelta delta7(instrument_id, OrderBookAction::ADD, false, 102.0, 2.5, now, now);
    OrderBookDelta delta8(instrument_id, OrderBookAction::ADD, false, 103.0, 3.5, now, now);
    OrderBookDelta delta9(instrument_id, OrderBookAction::ADD, false, 104.0, 4.5, now, now);
    OrderBookDelta delta10(instrument_id, OrderBookAction::ADD, false, 105.0, 5.5, now, now);
    
    book.applyDelta(delta6);
    book.applyDelta(delta7);
    book.applyDelta(delta8);
    book.applyDelta(delta9);
    book.applyDelta(delta10);
    
    std::vector<OrderBookLevel> allBids = book.bids();
    std::vector<OrderBookLevel> allAsks = book.asks();
    
    TEST_ASSERT(allBids.size() == 5, "应该有5个买单价格级别");
    TEST_ASSERT(allAsks.size() == 5, "应该有5个卖单价格级别");
    
    // 指定深度(3)
    std::vector<OrderBookLevel> top3Bids = book.bids(3);
    std::vector<OrderBookLevel> top3Asks = book.asks(3);
    
    TEST_ASSERT(top3Bids.size() == 3, "应该有3个买单价格级别");
    TEST_ASSERT(top3Asks.size() == 3, "应该有3个卖单价格级别");
    
    // 验证价格排序
    TEST_ASSERT(top3Bids[0].price() == 100.0, "最优买价应为100.0");
    TEST_ASSERT(top3Bids[1].price() == 99.0, "第二买价应为99.0");
    TEST_ASSERT(top3Bids[2].price() == 98.0, "第三买价应为98.0");
    
    TEST_ASSERT(top3Asks[0].price() == 101.0, "最优卖价应为101.0");
    TEST_ASSERT(top3Asks[1].price() == 102.0, "第二卖价应为102.0");
    TEST_ASSERT(top3Asks[2].price() == 103.0, "第三卖价应为103.0");
    
    // 测试获取超过实际深度的情况
    std::vector<OrderBookLevel> top10Bids = book.bids(10);
    std::vector<OrderBookLevel> top10Asks = book.asks(10);
    
    TEST_ASSERT(top10Bids.size() == 5, "超过实际深度时应返回所有可用价格级别(5个买单)");
    TEST_ASSERT(top10Asks.size() == 5, "超过实际深度时应返回所有可用价格级别(5个卖单)");
    
    // 测试获取深度为0的情况
    std::vector<OrderBookLevel> zeroBids = book.bids(0);
    std::vector<OrderBookLevel> zeroAsks = book.asks(0);
    
    TEST_ASSERT(zeroBids.empty(), "深度为0时应返回空列表(买单)");
    TEST_ASSERT(zeroAsks.empty(), "深度为0时应返回空列表(卖单)");
    
    std::cout << "获取指定深度的订单簿状态测试通过" << std::endl;
    return true;
}

int main() {
    std::cout << "---------OrderBook Test---------" << std::endl;
    
    bool all_tests_passed = true;
    
    all_tests_passed &= testOrderBookInitialState();
    all_tests_passed &= testAddOrders();
    all_tests_passed &= testOneSidedBook();
    all_tests_passed &= testBookDepth();
    all_tests_passed &= testUpdateOrders();
    all_tests_passed &= testDeleteOrders();
    all_tests_passed &= testBatchUpdates();
    all_tests_passed &= testClearOrderBook();
    all_tests_passed &= testEdgeCases();
    
    if (all_tests_passed) {
        std::cout << "\nALL TESTS PASSED!" << std::endl;
        return 0;
    } else {
        std::cout << "\nTESTS FAILED!" << std::endl;
        return 1;
    }
} 