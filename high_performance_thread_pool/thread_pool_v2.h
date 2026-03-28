#ifndef THREAD_POOL_V2_H
#define THREAD_POOL_V2_H

#include <vector>
#include <queue>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <future>
#include <atomic>
#include <memory>
#include <deque>

// 任务优先级
enum class TaskPriority : int {
    LOW = 0,
    NORMAL = 1,
    HIGH = 2
};

// 带优先级的任务包装
struct Task {
    TaskPriority priority;
    std::function<void()> func;
    
    // 优先级高的任务排在前面
    bool operator<(const Task& other) const {
        return static_cast<int>(priority) < static_cast<int>(other.priority);
    }
};

// 线程本地工作队列（支持双端操作）
class WorkStealingQueue {
public:
    void push(std::function<void()> task) {
        std::lock_guard<std::mutex> lock(mutex_);
        queue_.push_back(std::move(task));
    }
    
    bool pop(std::function<void()>& task) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (queue_.empty()) return false;
        task = std::move(queue_.front());
        queue_.pop_front();
        return true;
    }
    
    // 从尾部偷任务（工作窃取）
    bool steal(std::function<void()>& task) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (queue_.empty()) return false;
        task = std::move(queue_.back());
        queue_.pop_back();
        return true;
    }
    
    bool empty() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return queue_.empty();
    }
    
    size_t size() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return queue_.size();
    }

private:
    mutable std::mutex mutex_;
    std::deque<std::function<void()>> queue_;
};

class ThreadPoolV2 {
public:
    explicit ThreadPoolV2(size_t threads = std::thread::hardware_concurrency());
    ~ThreadPoolV2();

    ThreadPoolV2(const ThreadPoolV2&) = delete;
    ThreadPoolV2& operator=(const ThreadPoolV2&) = delete;

    // 提交任务（支持优先级）
    template<typename F, typename... Args>
    auto enqueue(F&& f, Args&&... args) 
        -> std::future<typename std::invoke_result<F, Args...>::type>;
    
    // 提交任务（带优先级）
    template<typename F, typename... Args>
    auto enqueue_with_priority(TaskPriority priority, F&& f, Args&&... args)
        -> std::future<typename std::invoke_result<F, Args...>::type>;

    size_t size() const { return workers.size(); }
    
    void wait();
    
    // 判断当前线程是否是工作线程
    bool is_current_worker() const { return is_worker_thread_; }
    
    // 获取当前线程的索引（仅在工作线程中有效）
    size_t get_thread_index() const { return thread_index_; }

private:
    void worker_thread(size_t index);
    bool pop_from_local(std::function<void()>& task, size_t index);
    bool steal_from_others(std::function<void()>& task, size_t index);
    bool pop_from_global(std::function<void()>& task);

    std::vector<std::thread> workers;
    std::vector<std::unique_ptr<WorkStealingQueue>> local_queues_;
    
    // 全局优先级队列
    std::priority_queue<Task> global_queue_;
    mutable std::mutex global_mutex_;
    
    // 同步原语
    std::condition_variable global_condition_;
    std::condition_variable completion_condition_;
    std::mutex completion_mutex_;
    
    // 状态
    std::atomic<bool> stop_;
    std::atomic<size_t> active_tasks_;
    std::atomic<size_t> waiting_threads_;
    std::atomic<size_t> next_thread_index_;  // 轮询分配索引
    
    // 线程本地存储索引
    static thread_local size_t thread_index_;
    
    // 标记线程是否属于线程池
    static thread_local bool is_worker_thread_;
};

thread_local size_t ThreadPoolV2::thread_index_ = 0;
thread_local bool ThreadPoolV2::is_worker_thread_ = false;

inline ThreadPoolV2::ThreadPoolV2(size_t threads) 
    : stop_(false), active_tasks_(0), waiting_threads_(0), next_thread_index_(0) {
    
    // 创建工作队列
    for (size_t i = 0; i < threads; ++i) {
        local_queues_.emplace_back(std::make_unique<WorkStealingQueue>());
    }
    
    // 创建工作线程
    for (size_t i = 0; i < threads; ++i) {
        workers.emplace_back(&ThreadPoolV2::worker_thread, this, i);
    }
}

inline void ThreadPoolV2::worker_thread(size_t index) {
    thread_index_ = index;
    is_worker_thread_ = true;  // 标记为工作线程
    
    while (true) {
        std::function<void()> task;
        bool has_task = false;
        
        // 1. 先尝试从本地队列获取
        has_task = pop_from_local(task, index);
        
        // 2. 尝试从全局优先级队列获取
        if (!has_task) {
            has_task = pop_from_global(task);
        }
        
        // 3. 工作窃取：从其他线程队列偷任务
        if (!has_task) {
            has_task = steal_from_others(task, index);
        }
        
        if (has_task) {
            task();
            
            // 任务完成，减少计数
            if (--active_tasks_ == 0) {
                std::unique_lock<std::mutex> lock(completion_mutex_);
                completion_condition_.notify_all();
            }
        } else {
            // 没有任务，等待
            std::unique_lock<std::mutex> lock(global_mutex_);
            ++waiting_threads_;
            
            // 等待任务或停止信号
            global_condition_.wait(lock, [this] {
                return stop_ || !global_queue_.empty();
            });
            
            --waiting_threads_;
            
            if (stop_) return;
        }
    }
}

inline bool ThreadPoolV2::pop_from_local(std::function<void()>& task, size_t index) {
    return local_queues_[index]->pop(task);
}

inline bool ThreadPoolV2::steal_from_others(std::function<void()>& task, size_t index) {
    const size_t queue_count = local_queues_.size();
    
    // 随机顺序窃取，避免竞争热点
    size_t start = (index + 1) % queue_count;
    
    for (size_t i = 0; i < queue_count - 1; ++i) {
        size_t target = (start + i) % queue_count;
        if (target != index && local_queues_[target]->steal(task)) {
            return true;
        }
    }
    
    return false;
}

inline bool ThreadPoolV2::pop_from_global(std::function<void()>& task) {
    std::lock_guard<std::mutex> lock(global_mutex_);
    if (global_queue_.empty()) return false;
    task = std::move(const_cast<Task&>(global_queue_.top()).func);
    global_queue_.pop();
    return true;
}

template<typename F, typename... Args>
auto ThreadPoolV2::enqueue(F&& f, Args&&... args)
    -> std::future<typename std::invoke_result<F, Args...>::type> {
    return enqueue_with_priority(TaskPriority::NORMAL, 
                                  std::forward<F>(f), 
                                  std::forward<Args>(args)...);
}

template<typename F, typename... Args>
auto ThreadPoolV2::enqueue_with_priority(TaskPriority priority, F&& f, Args&&... args)
    -> std::future<typename std::invoke_result<F, Args...>::type> {
    using return_type = typename std::invoke_result<F, Args...>::type;
    
    auto task = std::make_shared<std::packaged_task<return_type()>>(
        std::bind(std::forward<F>(f), std::forward<Args>(args)...)
    );
    
    std::future<return_type> res = task->get_future();
    
    ++active_tasks_;
    
    // 包装成可调用对象
    std::function<void()> wrapped_task = [task]() { (*task)(); };
    
    // 分配策略：
    // 1. 高优先级任务 → 全局优先级队列（确保优先执行）
    // 2. 工作线程提交 → 提交到自己的本地队列（无锁，最快）
    // 3. 外部线程提交 → 轮询分配到某个线程的本地队列
    
    if (priority == TaskPriority::HIGH) {
        // 高优先级任务放入全局队列，确保优先级生效
        std::lock_guard<std::mutex> lock(global_mutex_);
        global_queue_.push({priority, std::move(wrapped_task)});
        
        if (waiting_threads_ > 0) {
            global_condition_.notify_one();
        }
    } 
    else if (is_worker_thread_) {
        // 工作线程：提交到自己的本地队列（无锁，性能最优）
        local_queues_[thread_index_]->push(std::move(wrapped_task));
        
        // 唤醒一个等待的线程
        if (waiting_threads_ > 0) {
            global_condition_.notify_one();
        }
    }
    else {
        // 外部线程：轮询分配到某个线程的本地队列
        size_t target_index = next_thread_index_.fetch_add(1) % local_queues_.size();
        local_queues_[target_index]->push(std::move(wrapped_task));
        
        // 唤醒一个等待的线程
        if (waiting_threads_ > 0) {
            global_condition_.notify_one();
        }
    }
    
    return res;
}

inline void ThreadPoolV2::wait() {
    std::unique_lock<std::mutex> lock(completion_mutex_);
    completion_condition_.wait(lock, [this] {
        // 检查全局队列
        std::lock_guard<std::mutex> global_lock(global_mutex_);
        if (!global_queue_.empty() || active_tasks_ != 0) {
            return false;
        }
        
        // 检查所有本地队列
        for (const auto& queue : local_queues_) {
            if (!queue->empty()) {
                return false;
            }
        }
        
        return true;
    });
}

inline ThreadPoolV2::~ThreadPoolV2() {
    stop_ = true;
    
    // 析构时唤醒所有线程
    global_condition_.notify_all();
    
    for (std::thread& worker : workers) {
        if (worker.joinable()) {
            worker.join();
        }
    }
}

#endif // THREAD_POOL_V2_H
