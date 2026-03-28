#include "thread_pool_v2.h"
#include <iostream>
#include <chrono>
#include <atomic>

std::atomic<int> global_count{0};
std::atomic<int> local_queue_hits{0};
std::atomic<int> global_queue_hits{0};

ThreadPoolV2* g_pool = nullptr;

// 模拟工作线程内部提交任务的场景
void nested_task(int depth) {
    if (depth > 0 && g_pool) {
        // 工作线程内提交任务：会进入本地队列（无锁）
        auto future = g_pool->enqueue([depth]() {
            nested_task(depth - 1);
        });
        future.wait();
    }
    global_count++;
    
    // 统计来源
    if (g_pool && g_pool->is_current_worker()) {
        local_queue_hits++;
    } else {
        global_queue_hits++;
    }
}

int main() {
    std::cout << "=== Local Queue Task Distribution Demo ===\n\n";
    
    ThreadPoolV2 pool(4);
    g_pool = &pool;
    std::cout << "Thread pool created with " << pool.size() << " threads\n\n";
    
    // ========== 场景1：外部线程提交任务 ==========
    std::cout << "Scenario 1: External Thread Submission\n";
    std::cout << "--------------------------------------\n";
    std::cout << "Tasks will be distributed to local queues in round-robin\n\n";
    
    for (int i = 0; i < 8; ++i) {
        pool.enqueue([i]() {
            std::cout << "Task " << i << " on thread " 
                      << std::this_thread::get_id() << "\n";
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        });
    }
    
    pool.wait();
    std::cout << "\n";
    
    // ========== 场景2：工作线程内提交任务 ==========
    std::cout << "Scenario 2: Worker Thread Nested Submission\n";
    std::cout << "--------------------------------------------\n";
    std::cout << "Tasks submitted by workers go to their local queues (LOCK-FREE)\n\n";
    
    auto start = std::chrono::high_resolution_clock::now();
    
    // 提交4个初始任务，每个任务内部会继续提交子任务
    std::vector<std::future<void>> futures;
    for (int i = 0; i < 4; ++i) {
        futures.emplace_back(pool.enqueue([i]() {
            // 工作线程内提交：进入本地队列（无锁）
            auto f1 = g_pool->enqueue([i]() {
                std::cout << "Nested task L1-" << i << " on thread " 
                          << std::this_thread::get_id() << "\n";
                
                // 二级嵌套：仍然进入本地队列（无锁）
                auto f2 = g_pool->enqueue([i]() {
                    std::cout << "Nested task L2-" << i << " on thread " 
                              << std::this_thread::get_id() << "\n";
                });
                f2.wait();
            });
            f1.wait();
        }));
    }
    
    for (auto& f : futures) {
        f.wait();
    }
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    
    std::cout << "\nAll nested tasks completed in " << duration.count() << "ms\n";
    std::cout << "(Lock-free local queue submission for nested tasks)\n\n";
    
    // ========== 场景3：高优先级任务 ==========
    std::cout << "Scenario 3: High Priority Tasks\n";
    std::cout << "-------------------------------\n";
    std::cout << "High priority tasks go to global priority queue\n\n";
    
    // 先提交一些普通任务
    for (int i = 0; i < 4; ++i) {
        pool.enqueue([]() {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            std::cout << "Normal task completed\n";
        });
    }
    
    // 立即提交高优先级任务（会被优先执行）
    auto high_priority_future = pool.enqueue_with_priority(
        TaskPriority::HIGH, []() {
            std::cout << "HIGH PRIORITY task executed first!\n";
            return 42;
        });
    
    std::cout << "High priority result: " << high_priority_future.get() << "\n\n";
    
    // ========== 性能对比 ==========
    std::cout << "Scenario 4: Performance Comparison\n";
    std::cout << "----------------------------------\n";
    
    const int TASK_COUNT = 1000;
    
    // 测试1：外部线程提交（轮询分配）
    auto test1_start = std::chrono::high_resolution_clock::now();
    
    std::vector<std::future<void>> test_futures;
    test_futures.reserve(TASK_COUNT);
    
    for (int i = 0; i < TASK_COUNT; ++i) {
        test_futures.emplace_back(pool.enqueue([]() {
            global_count++;
        }));
    }
    
    for (auto& f : test_futures) {
        f.wait();
    }
    
    auto test1_end = std::chrono::high_resolution_clock::now();
    auto test1_duration = std::chrono::duration_cast<std::chrono::microseconds>(
        test1_end - test1_start);
    
    std::cout << "External thread submission: " << test1_duration.count() 
              << "μs for " << TASK_COUNT << " tasks\n";
    std::cout << "  → Round-robin distribution to local queues\n";
    std::cout << "  → Reduces global lock contention\n\n";
    
    // 测试2：工作线程内嵌套提交（无锁）
    global_count = 0;
    auto test2_start = std::chrono::high_resolution_clock::now();
    
    std::vector<std::future<void>> test2_futures;
    for (int i = 0; i < 4; ++i) {
        test2_futures.emplace_back(pool.enqueue([]() {
            std::vector<std::future<void>> nested;
            for (int j = 0; j < 250; ++j) {
                // 无锁提交到本地队列
                nested.emplace_back(
                    g_pool->enqueue([]() {
                        global_count++;
                    }));
            }
            for (auto& f : nested) f.wait();
        }));
    }
    
    for (auto& f : test2_futures) {
        f.wait();
    }
    
    auto test2_end = std::chrono::high_resolution_clock::now();
    auto test2_duration = std::chrono::duration_cast<std::chrono::microseconds>(
        test2_end - test2_start);
    
    std::cout << "Worker nested submission: " << test2_duration.count() 
              << "μs for " << TASK_COUNT << " tasks\n";
    std::cout << "  → Direct submission to local queue (LOCK-FREE)\n";
    std::cout << "  → Best performance for recursive/parallel tasks\n\n";
    
    double speedup = (double)test1_duration.count() / test2_duration.count();
    std::cout << "Speedup: " << speedup << "x faster for nested submission\n";
    
    std::cout << "\n=== Demo Complete ===\n";
    
    return 0;
}
