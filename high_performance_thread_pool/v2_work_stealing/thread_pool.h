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

enum class TaskPriority : int {
    LOW = 0,
    NORMAL = 1,
    HIGH = 2
};

struct Task {
    TaskPriority priority;
    std::function<void()> func;
    
    bool operator<(const Task& other) const {
        return static_cast<int>(priority) < static_cast<int>(other.priority);
    }
};

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

    template<typename F, typename... Args>
    auto enqueue(F&& f, Args&&... args) 
        -> std::future<typename std::invoke_result<F, Args...>::type>;
    
    template<typename F, typename... Args>
    auto enqueue_with_priority(TaskPriority priority, F&& f, Args&&... args)
        -> std::future<typename std::invoke_result<F, Args...>::type>;

    size_t size() const { return workers.size(); }
    void wait();
    bool is_current_worker() const { return is_worker_thread_; }

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
    
    std::atomic<bool> stop_;
    std::atomic<size_t> active_tasks_;
    std::atomic<size_t> waiting_threads_;
    std::atomic<size_t> next_thread_index_;
    
    static thread_local size_t thread_index_;
    static thread_local bool is_worker_thread_;
};

thread_local size_t ThreadPoolV2::thread_index_ = 0;
thread_local bool ThreadPoolV2::is_worker_thread_ = false;

inline ThreadPoolV2::ThreadPoolV2(size_t threads) 
    : stop_(false), active_tasks_(0), waiting_threads_(0), next_thread_index_(0) {
    
    for (size_t i = 0; i < threads; ++i) {
        local_queues_.emplace_back(std::make_unique<WorkStealingQueue>());
    }
    
    for (size_t i = 0; i < threads; ++i) {
        workers.emplace_back(&ThreadPoolV2::worker_thread, this, i);
    }
}

inline void ThreadPoolV2::worker_thread(size_t index) {
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
            
            if (--active_tasks_ == 0) {
                std::unique_lock<std::mutex> lock(completion_mutex_);
                completion_condition_.notify_all();
            }
        } else {
            std::unique_lock<std::mutex> lock(global_mutex_);
            ++waiting_threads_;
            
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
    
    std::function<void()> wrapped_task = [task]() { (*task)(); };
    
    if (priority == TaskPriority::HIGH) {
        std::lock_guard<std::mutex> lock(global_mutex_);
        global_queue_.push({priority, std::move(wrapped_task)});
        
        if (waiting_threads_ > 0) {
            global_condition_.notify_one();
        }
    } 
    else if (is_worker_thread_) {
        local_queues_[thread_index_]->push(std::move(wrapped_task));
        
        if (waiting_threads_ > 0) {
            global_condition_.notify_one();
        }
    }
    else {
        size_t target_index = next_thread_index_.fetch_add(1) % local_queues_.size();
        local_queues_[target_index]->push(std::move(wrapped_task));
        
        if (waiting_threads_ > 0) {
            global_condition_.notify_one();
        }
    }
    
    return res;
}

inline void ThreadPoolV2::wait() {
    std::unique_lock<std::mutex> lock(completion_mutex_);
    completion_condition_.wait(lock, [this] {
        std::lock_guard<std::mutex> global_lock(global_mutex_);
        if (!global_queue_.empty() || active_tasks_ != 0) {
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

inline ThreadPoolV2::~ThreadPoolV2() {
    stop_ = true;
    global_condition_.notify_all();
    
    for (std::thread& worker : workers) {
        if (worker.joinable()) {
            worker.join();
        }
    }
}

#endif
