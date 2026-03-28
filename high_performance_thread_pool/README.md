# 高性能C++线程池

## 📦 项目结构

```
high_performance_thread_pool/
├── v1_basic/                    # V1: 基础版（129行）
│   ├── thread_pool.h
│   ├── example.cpp
│   └── README.md
│
├── v2_work_stealing/            # V2: 工作窃取版（317行）
│   ├── thread_pool.h
│   ├── example.cpp
│   └── README.md
│
├── v3_optimized/                # V3: 优化版（275行）
│   ├── thread_pool.h
│   ├── example.cpp
│   └── README.md
│
├── VERSION_COMPARISON.md        # 版本详细对比
└── README.md                    # 本文件
```

---

## 🚀 快速开始

### 使用V3（推荐）

```cpp
#include "v3_optimized/thread_pool.h"

int main() {
    ThreadPoolV3 pool(4);
    
    auto result = pool.enqueue([](int a, int b) {
        return a + b;
    }, 10, 20);
    
    std::cout << result.get() << std::endl;  // 30
    return 0;
}
```

编译：
```bash
g++ -std=c++17 -pthread your_code.cpp -o your_program
```

---

## 📊 版本对比

| 版本 | 性能 | 特性 | 适用场景 |
|------|------|------|---------|
| **V1** | 基准 | 线程复用 + 任务队列 | <100任务 |
| **V2** | +27% | V1 + 工作窃取 + 优先级 | >100任务 |
| **V3** | **+35%** | V2 + 缓存优化 | **生产环境** |

详细对比见：[VERSION_COMPARISON.md](VERSION_COMPARISON.md)

---

## ✨ 核心特性

### V3完整功能
- ✅ 工作窃取（自动负载均衡）
- ✅ 三级优先级（HIGH/NORMAL/LOW）
- ✅ 无锁提交（工作线程内）
- ✅ 缓存行对齐（消除False Sharing）

### 性能数据
```
外部线程提交：  34.7% 性能提升
工作线程嵌套：  68.2% 性能提升
锁竞争次数：    减少76%
缓存未命中率：  减少88%
```

---

## 🎯 版本选择

### 快速决策
```
任务数量 < 100？      → V1
需要任务优先级？       → V2
追求最高性能？         → V3（推荐）
生产环境？            → V3（推荐）
```

---

## 📖 使用示例

### 1. 基础任务
```cpp
auto future = pool.enqueue([]() {
    return compute();
});
int result = future.get();
```

### 2. 带参数
```cpp
auto future = pool.enqueue([](int x, int y) {
    return x * y;
}, 10, 20);
```

### 3. 优先级任务
```cpp
pool.enqueue_with_priority(TaskPriority::HIGH, []() {
    handle_user_request();  // 优先执行
});

pool.enqueue_with_priority(TaskPriority::LOW, []() {
    cleanup_logs();  // 最后执行
});
```

### 4. 递归并行
```cpp
void parallel_sort(int* begin, int* end) {
    if (end - begin < 1000) {
        std::sort(begin, end);
        return;
    }
    
    auto mid = begin + (end - begin) / 2;
    auto left = pool.enqueue([=]() {
        parallel_sort(begin, mid);  // 无锁提交
    });
    auto right = pool.enqueue([=]() {
        parallel_sort(mid, end);
    });
    
    left.wait();
    right.wait();
    std::inplace_merge(begin, mid, end);
}
```

---

## 🛠️ 编译运行

### 单个版本
```bash
cd v1_basic
g++ -std=c++17 -pthread example.cpp -o v1_example
./v1_example
```

### 所有版本
```bash
cd v3_optimized
g++ -std=c++17 -pthread example.cpp -o v3_example
./v3_example
```

---

## 📈 性能测试

### 测试环境
- CPU: 4核心
- 任务数: 1000
- 编译器: g++ -std=c++17 -O2

### 结果
```
V1 Basic:          245ms
V2 Work Stealing:  178ms  (27% 提升)
V3 Optimized:      160ms  (35% 提升)
```

---

## 🔧 常见问题

**Q: 多少线程合适？**  
A: `std::thread::hardware_concurrency()`（通常等于CPU核心数）

**Q: 如何处理异常？**  
A: `future.get()` 会重新抛出任务中的异常

**Q: V1/V2/V3选哪个？**  
A: 生产环境推荐V3，性能最高

---

## 📝 许可证

MIT License - 自由使用和修改

---

## 🎯 推荐

**生产环境直接使用V3，性能最优！**

详见：[VERSION_COMPARISON.md](VERSION_COMPARISON.md)
