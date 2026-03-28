#include "thread_pool.h"
#include <iostream>
#include <chrono>
#include <vector>

// 示例任务函数
int compute(int a, int b) {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    return a + b;
}

int main() {
    // 创建线程池，使用CPU核心数
    ThreadPool pool(4);
    std::cout << "ThreadPool created with " << pool.size() << " threads\n";

    // 提交多个任务
    std::vector<std::future<int>> results;
    
    for (int i = 0; i < 20; ++i) {
        results.emplace_back(pool.enqueue(compute, i, i * 2));
    }

    // 获取结果
    std::cout << "Results:\n";
    for (size_t i = 0; i < results.size(); ++i) {
        std::cout << "Task " << i << ": " << results[i].get() << "\n";
    }

    std::cout << "\n=== Lambda Example ===\n";
    
    // 使用Lambda表达式
    auto future1 = pool.enqueue([](int n) {
        int sum = 0;
        for (int i = 1; i <= n; ++i) {
            sum += i;
        }
        return sum;
    }, 100);
    
    std::cout << "Sum 1..100 = " << future1.get() << "\n";

    std::cout << "\n=== Wait Example ===\n";
    
    // 提交多个任务并等待完成
    for (int i = 0; i < 5; ++i) {
        pool.enqueue([](int id) {
            std::cout << "Task " << id << " running on thread " 
                      << std::this_thread::get_id() << "\n";
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
        }, i);
    }
    
    std::cout << "Waiting for all tasks to complete...\n";
    pool.wait();
    std::cout << "All tasks completed!\n";

    return 0;
}
