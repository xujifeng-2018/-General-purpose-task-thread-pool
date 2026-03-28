CXX = g++
CXXFLAGS = -std=c++17 -pthread -O2 -Wall -Wextra

# 目标文件
TARGETS = v1_example v2_example v3_benchmark local_queue_demo

# 默认目标
all: $(TARGETS)

# V1 示例
v1_example: thread_pool_example.cpp thread_pool.h
	$(CXX) $(CXXFLAGS) thread_pool_example.cpp -o v1_example

# V2 示例
v2_example: thread_pool_v2_example.cpp thread_pool_v2.h
	$(CXX) $(CXXFLAGS) thread_pool_v2_example.cpp -o v2_example

# V3 性能测试
v3_benchmark: thread_pool_benchmark.cpp thread_pool_v3_optimized.h
	$(CXX) $(CXXFLAGS) thread_pool_benchmark.cpp -o v3_benchmark

# 本地队列演示
local_queue_demo: thread_pool_local_queue_demo.cpp thread_pool_v3_optimized.h
	$(CXX) $(CXXFLAGS) thread_pool_local_queue_demo.cpp -o local_queue_demo

# 清理
clean:
	rm -f $(TARGETS)

# 运行测试
test: v3_benchmark
	./v3_benchmark

.PHONY: all clean test
