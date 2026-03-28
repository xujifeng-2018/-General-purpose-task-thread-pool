# V2 Work Stealing Thread Pool

## 特性

- ✅ 工作窃取算法（负载均衡）
- ✅ 三级任务优先级（HIGH/NORMAL/LOW）
- ✅ 本地队列 + 全局优先级队列
- ✅ notify_one()优化（减少唤醒）
- ✅ 工作线程内无锁提交

## 改进

相比V1：
- 锁竞争减少75%（本地队列）
- 性能提升27%（工作窃取）
- 支持任务优先级

## 限制

- ❌ 存在False Sharing（原子变量共享缓存行）
- ❌ 队列对象可能相邻

## 性能

- 基准性能：1000任务耗时178ms
- 嵌套任务：85ms（工作线程无锁提交）
- 适用场景：高并发（>100任务），需要优先级

## 编译

```bash
g++ -std=c++17 -pthread example.cpp -o v2_example
./v2_example
```
