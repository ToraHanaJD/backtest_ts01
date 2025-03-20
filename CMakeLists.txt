cmake_minimum_required(VERSION 3.10)
project(backtest)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 添加头文件目录
include_directories(${PROJECT_SOURCE_DIR}/include)

# 添加源文件
set(SOURCES
    src/accumulator.cpp
    src/clock.cpp
    src/timer.cpp
    src/data.cpp
    src/order_book.cpp
    src/exchange.cpp
    src/execution_client.cpp
    src/parallel_engine.cpp
)

# 创建库
add_library(backtest STATIC ${SOURCES})

# 设置编译选项
target_compile_options(backtest PRIVATE -Wall -Wextra -O3)

# 链接线程库
target_link_libraries(backtest PRIVATE pthread)

# 设置输出目录
set_target_properties(backtest PROPERTIES
    ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)

# 添加测试示例
add_executable(backtest_example examples/backtest_example.cpp)
target_link_libraries(backtest_example PRIVATE backtest)

# 添加高性能并行回测示例
add_executable(parallel_backtest_example examples/parallel_backtest_example.cpp)
target_link_libraries(parallel_backtest_example PRIVATE backtest)

# 添加RealtimeClock测试
add_executable(test_realtime_clock tests/test_realtime_clock.cpp)
target_link_libraries(test_realtime_clock PRIVATE backtest)

# 添加ExecutionClient测试
add_executable(test_execution_client tests/test_execution_client.cpp)
target_link_libraries(test_execution_client PRIVATE backtest) 