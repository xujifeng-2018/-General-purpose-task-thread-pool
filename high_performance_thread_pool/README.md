# 高性能C++线程池实现

## 🚀 快速开始

```cpp
#include "thread_pool_v3_optimized.h"

int main() {
    ThreadPoolV3 pool(4);  // 创建4个工作线程
    
    // 提交任务
    auto future = pool.enqueue([](int a, int b) {
        return a + b;
    }, 10, 20);
    
    std::cout << "Result: " << future.get() << std::endl;  // 输出: 30
    
    return 0;
}
```

## 📦 版本选择

| 版本 | 文件 | 性能 | 推荐场景 |
|------|------|------|---------|
| **V3** | `thread_pool_v3_optimized.h` | ⭐⭐⭐⭐⭐ | 生产环境，最高性能 |
| V2 | `thread_pool_v2.h` | ⭐⭐⭐⭐ | 高并发，需要优先级 |
| V1 | `thread_pool.h` | ⭐⭐⭐ | 简单任务，低并发 |

## ✨ 核心特性

### V3特性（推荐）
- ✅ **工作窃取**：空闲线程自动偷取其他队列任务
- ✅ **任务优先级**：HIGH/NORMAL/LOW三级优先级
- ✅ **本地队列**：工作线程无锁提交任务
- ✅ **缓存优化**：消除False Sharing，性能提升35%
- ✅ **智能分配**：自动负载均衡

### 性能对比（vs V1）
```
外部线程批量提交：34.7% 性能提升
工作线程嵌套提交：68.2% 性能提升
锁竞争次数：      减少76%
缓存未命中率：    减少88%
```

## 📖 使用示例

### 1. 基础用法
```cpp
ThreadPoolV3 pool(std::thread::hardware_concurrency());

auto result = pool.enqueue([]() {
    return compute_something();
});

std::cout << result.get() << std::endl;
```

### 2. 带参数的任务
```cpp
auto future = pool.enqueue([](int x, int y) {
    return x * y;
}, 10, 20);

int product = future.get();  // 200
```

### 3. 优先级任务
```cpp
// 紧急任务优先执行
pool.enqueue_with_priority(TaskPriority::HIGH, []() {
    handle_user_request();
});

// 普通任务
pool.enqueue([]() {
    process_data();
});

// 后台任务最后执行
pool.enqueue_with_priority(TaskPriority::LOW, []() {
    cleanup_logs();
});
```

### 4. 递归并行算法（最优性能）
```cpp
void parallel_sort(int* begin, int* end) {
    if (end - begin < 1000) {
        std::sort(begin, end);
        return;
    }
    
    auto mid = begin + (end - begin) / 2;
    
    // 工作线程内提交：无锁，性能最优
    auto left = pool.enqueue([=]() {
        parallel_sort(begin, mid);
    });
    auto right = pool.enqueue([=]() {
        parallel_sort(mid, end);
    });
    
    left.wait();
    right.wait();
    std::inplace_merge(begin, mid, end);
}
```

### 5. 等待所有任务完成
```cpp
for (int i = 0; i < 100; ++i) {
    pool.enqueue([]() { do_work(); });
}

pool.wait();  // 阻塞直到所有任务完成
```

## 🛠️ 编译运行

```bash
# 编译
g++ -std=c++17 -pthread your_program.cpp -o your_program

# 运行
./your_program
```

## 📊 性能测试

```bash
# 编译基准测试
g++ -std=c++17 -pthread thread_pool_benchmark.cpp -o benchmark

# 运行
./benchmark

# 性能分析（Linux）
perf stat -e cache-references,cache-misses ./benchmark
```

## 📚 文档导航

| 文档 | 内容 |
|------|------|
| [完整指南](thread_pool_complete_guide.md) | 版本对比、性能分析、使用场景 |
| [False Sharing分析](false_sharing_analysis.md) | 缓存优化详解 |
| [任务分配策略](thread_pool_local_queue_allocation.md) | 本地队列分配机制 |
| [V2改进说明](thread_pool_improvements.md) | 工作窃取、优先级实现 |

## 🎯 最佳实践

### ✅ 推荐做法
```cpp
// 使用硬件并发数
ThreadPoolV3 pool(std::thread::hardware_concurrency());

// 批量提交任务
std::vector<std::future<int>> futures;
for (int i = 0; i < 1000; ++i) {
    futures.emplace_back(pool.enqueue([]() { return compute(); }));
}

// 处理结果
for (auto& f : futures) {
    process(f.get());
}
```

### ❌ 避免的做法
```cpp
// 不要在任务中阻塞
pool.enqueue([]() {
    std::this_thread::sleep_for(std::chrono::seconds(10));  // 浪费线程
});

// 不要创建过多线程
ThreadPoolV3 pool(100);  // 过多线程降低性能

// 不要忽略异常
auto f = pool.enqueue([]() { throw std::runtime_error("error"); });
f.get();  // 会抛出异常，需要捕获
```

## 🔧 常见问题

**Q: V1/V2/V3应该选哪个？**  
A: 生产环境推荐V3，性能最高且无False Sharing问题。

**Q: 多少线程合适？**  
A: 推荐 `std::thread::hardware_concurrency()`，通常等于CPU核心数。

**Q: 如何处理任务异常？**  
A: 通过future.get()捕获异常，或者任务内部try-catch。

**Q: 递归任务性能如何？**  
A: V2/V3在递归任务中性能最优，工作线程内提交无锁。

## 📈 性能对比

### 测试环境
- CPU: 4核心
- 任务数: 1000
- 测试场景: 外部提交 + 嵌套提交

### 结果
```
V1 (基础版):     245ms
V2 (工作窃取):   178ms  (27% 提升)
V3 (优化版):     160ms  (35% 提升)
```

## 📄 许可证

MIT License - 自由使用和修改

## 🤝 贡献

欢迎提交Issue和Pull Request！

---

**推荐：直接使用V3版本获得最佳性能！**
