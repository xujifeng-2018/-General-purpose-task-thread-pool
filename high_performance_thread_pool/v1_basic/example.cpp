#include "thread_pool.h"
#include <iostream>

int main() {
    ThreadPool pool(4);
    
    auto result = pool.enqueue([](int a, int b) {
        return a + b;
    }, 10, 20);
    
    std::cout << "Result: " << result.get() << std::endl;
    
    return 0;
}
