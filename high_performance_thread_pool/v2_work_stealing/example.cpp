#include "thread_pool.h"
#include <iostream>

int main() {
    ThreadPoolV2 pool(4);
    
    // 高优先级任务
    auto high = pool.enqueue_with_priority(TaskPriority::HIGH, []() {
        return 1;
    });
    
    // 普通任务
    auto normal = pool.enqueue([]() {
        return 2;
    });
    
    // 低优先级任务
    auto low = pool.enqueue_with_priority(TaskPriority::LOW, []() {
        return 3;
    });
    
    std::cout << "High: " << high.get() << std::endl;
    std::cout << "Normal: " << normal.get() << std::endl;
    std::cout << "Low: " << low.get() << std::endl;
    
    return 0;
}
