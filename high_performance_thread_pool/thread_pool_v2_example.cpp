#include "thread_pool_v2.h"
#include <iostream>
#include <chrono>
#include <vector>
#include <string>

void print_task(const std::string& name, int duration) {
    std::cout << "[" << name << "] running on thread " 
              << std::this_thread::get_id() << "\n";
    std::this_thread::sleep_for(std::chrono::milliseconds(duration));
    std::cout << "[" << name << "] completed\n";
}

int main() {
    std::cout << "=== ThreadPool V2 Features Demo ===\n\n";
    
    ThreadPoolV2 pool(4);
    std::cout << "Created thread pool with " << pool.size() << " threads\n\n";
    
    // ==================== 特性1：任务优先级 ====================
    std::cout << "1. Priority Task Demo:\n";
    std::cout << "----------------------\n";
    
    auto low_task = pool.enqueue_with_priority(
        TaskPriority::LOW, 
        print_task, "LOW-PRIORITY", 200);
    
    auto normal_task1 = pool.enqueue_with_priority(
        TaskPriority::NORMAL,
        print_task, "NORMAL-PRIORITY", 150);
    
    auto normal_task2 = pool.enqueue(
        print_task, "DEFAULT-NORMAL", 100);
    
    auto high_task = pool.enqueue_with_priority(
        TaskPriority::HIGH,
        print_task, "HIGH-PRIORITY", 50);
    
    // 等待优先级任务完成
    high_task.wait();
    normal_task1.wait();
    normal_task2.wait();
    low_task.wait();
    
    std::cout << "\n";
    
    // ==================== 特性2：工作窃取 ====================
    std::cout << "2. Work Stealing Demo:\n";
    std::cout << "----------------------\n";
    std::cout << "Submitting 16 tasks to test work stealing...\n\n";
    
    std::vector<std::future<void>> results;
    
    auto start = std::chrono::high_resolution_clock::now();
    
    for (int i = 0; i < 16; ++i) {
        results.emplace_back(pool.enqueue([i] {
            int work = 0;
            for (int j = 0; j < 1000000; ++j) {
                work += j;
            }
            std::cout << "Task " << i << " done (thread " 
                      << std::this_thread::get_id() << ")\n";
        }));
    }
    
    // 等待所有任务完成
    pool.wait();
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    
    std::cout << "\nAll tasks completed in " << duration.count() << "ms\n";
    std::cout << "(Work stealing helps balance load across threads)\n\n";
    
    // ==================== 特性3：批量唤醒优化 ====================
    std::cout << "3. Efficient Wakeup Demo:\n";
    std::cout << "-------------------------\n";
    
    std::cout << "Submitting tasks one by one...\n";
    
    auto single_start = std::chrono::high_resolution_clock::now();
    
    std::vector<std::future<int>> futures;
    for (int i = 0; i < 20; ++i) {
        futures.emplace_back(pool.enqueue([](int n) {
            return n * n;
        }, i));
    }
    
    for (auto& f : futures) {
        f.get();
    }
    
    auto single_end = std::chrono::high_resolution_clock::now();
    auto single_duration = std::chrono::duration_cast<std::chrono::microseconds>(
        single_end - single_start);
    
    std::cout << "20 tasks completed in " << single_duration.count() << "μs\n";
    std::cout << "(Uses notify_one() instead of notify_all() for efficiency)\n\n";
    
    // ==================== 综合演示 ====================
    std::cout << "4. Combined Features Demo:\n";
    std::cout << "--------------------------\n";
    
    std::atomic<int> counter{0};
    
    // 提交高优先级计算任务
    auto compute_future = pool.enqueue_with_priority(
        TaskPriority::HIGH,
        [&counter](int n) {
            for (int i = 0; i < n; ++i) {
                counter.fetch_add(1, std::memory_order_relaxed);
            }
            return counter.load();
        }, 1000);
    
    // 提交普通IO任务
    auto io_future = pool.enqueue_with_priority(
        TaskPriority::NORMAL,
        []() {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            return "IO Task Complete";
        });
    
    // 提交低优先级清理任务
    auto cleanup_future = pool.enqueue_with_priority(
        TaskPriority::LOW,
        []() {
            return "Cleanup Complete";
        });
    
    std::cout << "High priority result: " << compute_future.get() << "\n";
    std::cout << "Normal priority result: " << io_future.get() << "\n";
    std::cout << "Low priority result: " << cleanup_future.get() << "\n";
    
    std::cout << "\n=== Demo Complete ===\n";
    
    return 0;
}
