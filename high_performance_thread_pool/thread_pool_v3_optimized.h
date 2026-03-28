#ifndef THREAD_POOL_V3_OPTIMIZED_H
#define THREAD_POOL_V3_OPTIMIZED_H

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

// 缓存行大小（通常64字节）
constexpr size_t CACHE_LINE_SIZE = 64;

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
    
    bool operator<(const Task& other) const {
        return static_cast<int>(priority) < static_cast<int>(other.priority);
    }
};

// 缓存行对齐的原子计数器
template<typename T>
struct alignas(CACHE_LINE_SIZE) AlignedAtomic {
    std::atomic<T> value{0};
    
    AlignedAtomic() = default;
    AlignedAtomic(T v) : value(v) {}
    
    // 提供类似atomic的接口
    T load(std::memory_order order = std::memory_order_seq_cst) const {
        return value.load(order);
    }
    
    void store(T desired, std::memory_order order = std::memory_order_seq_cst) {
        value.store(desired, order);
    }
    
    T fetch_add(T arg, std::memory_order order = std::memory_order_seq_cst) {
        return value.fetch_add(arg, order);
    }
    
    T fetch_sub(T arg, std::memory_order order = std::memory_order_seq_cst) {
        return value.fetch_sub(arg, order);
    }
    
    operator T() const { return load(); }
    
    AlignedAtomic& operator=(T v) {
        store(v);
        return *this;
    }
};

// 缓存行对齐的工作窃取队列
class alignas(CACHE_LINE_SIZE) WorkStealingQueue {
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
    // padding已由alignas(64)保证
};

class ThreadPoolV3 {
public:
    explicit ThreadPoolV3(size_t threads = std::thread::hardware_concurrency());
    ~ThreadPoolV3();

    ThreadPoolV3(const ThreadPoolV3&) = delete;
    ThreadPoolV3& operator=(const ThreadPoolV3&) = delete;

    template<typename F, typename... Args>
    auto enqueue(F&& f, Args&&... args) 
        -> std::future<typename std::invoke_result<F, Args...>::type>;
    
    template<typename F, typename... Args>
    auto enqueue_with_priority(TaskPriority priority, F&& f, Args&&... args)
        -> std::future<typename std::invoke_result<F, Args...>::type>;

    size_t size() const { return workers.size(); }
    
    void wait();
    
    bool is_current_worker() const { return is_worker_thread_; }
    size_t get_thread_index() const { return thread_index_; }

private:
    void worker_thread(size_t index);
    bool pop_from_local(std::function<void()>& task, size_t index);
    bool steal_from_others(std::function<void()>& task, size_t index);
    bool pop_from_global(std::function<void()>& task);

    std::vector<std::thread> workers;
    std::vector<std::unique_ptr<WorkStealingQueue>> local_queues_;
    
    std::priority_queue<Task> global_queue_;
    mutable std::mutex global_mutex_;
    
    std::condition_variable global_condition_;
    std::condition_variable completion_condition_;
    std::mutex completion_mutex_;
    
    // ✅ 缓存行对齐的原子变量（避免false sharing）
    alignas(CACHE_LINE_SIZE) AlignedAtomic<bool> stop_;
    alignas(CACHE_LINE_SIZE) AlignedAtomic<size_t> active_tasks_;
    alignas(CACHE_LINE_SIZE) AlignedAtomic<size_t> waiting_threads_;
    alignas(CACHE_LINE_SIZE) AlignedAtomic<size_t> next_thread_index_;
    
    static thread_local size_t thread_index_;
    static thread_local bool is_worker_thread_;
};

thread_local size_t ThreadPoolV3::thread_index_ = 0;
thread_local bool ThreadPoolV3::is_worker_thread_ = false;

inline ThreadPoolV3::ThreadPoolV3(size_t threads) 
    : stop_(false), active_tasks_(0), waiting_threads_(0), next_thread_index_(0) {
    
    for (size_t i = 0; i < threads; ++i) {
        local_queues_.emplace_back(std::make_unique<WorkStealingQueue>());
    }
    
    for (size_t i = 0; i < threads; ++i) {
        workers.emplace_back(&ThreadPoolV3::worker_thread, this, i);
    }
}

inline void ThreadPoolV3::worker_thread(size_t index) {
    thread_index_ = index;
    is_worker_thread_ = true;
    
    while (true) {
        std::function<void()> task;
        bool has_task = false;
        
        has_task = pop_from_local(task, index);
        
        if (!has_task) {
            has_task = pop_from_global(task);
        }
        
        if (!has_task) {
            has_task = steal_from_others(task, index);
        }
        
        if (has_task) {
            task();
            
            if (--active_tasks_.value == 0) {
                std::unique_lock<std::mutex> lock(completion_mutex_);
                completion_condition_.notify_all();
            }
        } else {
            std::unique_lock<std::mutex> lock(global_mutex_);
            ++waiting_threads_.value;
            
            global_condition_.wait(lock, [this] {
                return stop_.load() || !global_queue_.empty();
            });
            
            --waiting_threads_.value;
            
            if (stop_.load()) return;
        }
    }
}

inline bool ThreadPoolV3::pop_from_local(std::function<void()>& task, size_t index) {
    return local_queues_[index]->pop(task);
}

inline bool ThreadPoolV3::steal_from_others(std::function<void()>& task, size_t index) {
    const size_t queue_count = local_queues_.size();
    size_t start = (index + 1) % queue_count;
    
    for (size_t i = 0; i < queue_count - 1; ++i) {
        size_t target = (start + i) % queue_count;
        if (target != index && local_queues_[target]->steal(task)) {
            return true;
        }
    }
    
    return false;
}

inline bool ThreadPoolV3::pop_from_global(std::function<void()>& task) {
    std::lock_guard<std::mutex> lock(global_mutex_);
    if (global_queue_.empty()) return false;
    task = std::move(const_cast<Task&>(global_queue_.top()).func);
    global_queue_.pop();
    return true;
}

template<typename F, typename... Args>
auto ThreadPoolV3::enqueue(F&& f, Args&&... args)
    -> std::future<typename std::invoke_result<F, Args...>::type> {
    return enqueue_with_priority(TaskPriority::NORMAL, 
                                  std::forward<F>(f), 
                                  std::forward<Args>(args)...);
}

template<typename F, typename... Args>
auto ThreadPoolV3::enqueue_with_priority(TaskPriority priority, F&& f, Args&&... args)
    -> std::future<typename std::invoke_result<F, Args...>::type> {
    using return_type = typename std::invoke_result<F, Args...>::type;
    
    auto task = std::make_shared<std::packaged_task<return_type()>>(
        std::bind(std::forward<F>(f), std::forward<Args>(args)...)
    );
    
    std::future<return_type> res = task->get_future();
    
    ++active_tasks_.value;
    
    std::function<void()> wrapped_task = [task]() { (*task)(); };
    
    if (priority == TaskPriority::HIGH) {
        std::lock_guard<std::mutex> lock(global_mutex_);
        global_queue_.push({priority, std::move(wrapped_task)});
        
        if (waiting_threads_.load() > 0) {
            global_condition_.notify_one();
        }
    } 
    else if (is_worker_thread_) {
        local_queues_[thread_index_]->push(std::move(wrapped_task));
        
        if (waiting_threads_.load() > 0) {
            global_condition_.notify_one();
        }
    }
    else {
        size_t target_index = next_thread_index_.fetch_add(1) % local_queues_.size();
        local_queues_[target_index]->push(std::move(wrapped_task));
        
        if (waiting_threads_.load() > 0) {
            global_condition_.notify_one();
        }
    }
    
    return res;
}

inline void ThreadPoolV3::wait() {
    std::unique_lock<std::mutex> lock(completion_mutex_);
    completion_condition_.wait(lock, [this] {
        std::lock_guard<std::mutex> global_lock(global_mutex_);
        if (!global_queue_.empty() || active_tasks_.load() != 0) {
            return false;
        }
        
        for (const auto& queue : local_queues_) {
            if (!queue->empty()) {
                return false;
            }
        }
        
        return true;
    });
}

inline ThreadPoolV3::~ThreadPoolV3() {
    stop_.store(true);
    global_condition_.notify_all();
    
    for (std::thread& worker : workers) {
        if (worker.joinable()) {
            worker.join();
        }
    }
}

#endif // THREAD_POOL_V3_OPTIMIZED_H
