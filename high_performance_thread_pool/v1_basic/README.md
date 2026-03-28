# V1 Basic Thread Pool

## 特性

- ✅ 线程复用，避免频繁创建销毁
- ✅ 任务队列，FIFO调度
- ✅ Future支持，获取任务结果
- ✅ wait()方法，等待所有任务完成

## 限制

- ❌ 全局锁竞争（单队列）
- ❌ 无任务优先级
- ❌ 无工作窃取
- ❌ notify_all()性能开销

## 性能

- 基准性能：1000任务耗时245ms
- 适用场景：任务数<100，简单应用

## 编译

```bash
g++ -std=c++17 -pthread example.cpp -o v1_example
./v1_example
```
