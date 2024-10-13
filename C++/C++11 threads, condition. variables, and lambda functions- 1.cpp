#include <iostream>
#include <vector>
#include <queue>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <future>

class ThreadPool {
public:
    ThreadPool(size_t threads);
    ~ThreadPool();

    // Adds a task to the queue and returns a future to get the result
    template<class F, class... Args>
    auto enqueue(F&& f, Args&&... args) -> std::future<typename std::result_of<F(Args...)>::type>;

private:
    // Workers
    std::vector<std::thread> workers;
    
    // Task queue
    std::queue<std::function<void()>> tasks;
    
    // Synchronization
    std::mutex queueMutex;
    std::condition_variable condition;
    bool stop;
};

// Constructor: Initialize thread pool and workers
ThreadPool::ThreadPool(size_t threads) : stop(false) {
    for (size_t i = 0; i < threads; ++i) {
        workers.emplace_back([this] {
            for (;;) {
                std::function<void()> task;
                
                // Critical section: Wait for tasks or stop signal
                {
                    std::unique_lock<std::mutex> lock(this->queueMutex);
                    this->condition.wait(lock, [this] {
                        return this->stop || !this->tasks.empty();
                    });
                    
                    if (this->stop && this->tasks.empty()) return;
                    task = std::move(this->tasks.front());
                    this->tasks.pop();
                }
                
                // Execute the task
                task();
            }
        });
    }
}

// Destructor: Stop all threads
ThreadPool::~ThreadPool() {
    {
        std::unique_lock<std::mutex> lock(queueMutex);
        stop = true;
    }
    condition.notify_all();
    
    // Join all threads
    for (std::thread &worker : workers) {
        worker.join();
    }
}

// Add task to the queue
template<class F, class... Args>
auto ThreadPool::enqueue(F&& f, Args&&... args) -> std::future<typename std::result_of<F(Args...)>::type> {
    using returnType = typename std::result_of<F(Args...)>::type;
    
    auto task = std::make_shared<std::packaged_task<returnType()>>(
        std::bind(std::forward<F>(f), std::forward<Args>(args)...)
    );
    
    std::future<returnType> res = task->get_future();
    {
        std::unique_lock<std::mutex> lock(queueMutex);
        
        // Don't allow adding tasks after stopping the pool
        if (stop) throw std::runtime_error("Enqueue on stopped ThreadPool");
        
        tasks.emplace([task]() { (*task)(); });
    }
    condition.notify_one();
    return res;
}

// Example task: Fibonacci calculation
int fibonacci(int n) {
    if (n <= 1) return n;
    return fibonacci(n - 1) + fibonacci(n - 2);
}

int main() {
    // Create a thread pool with 4 threads
    ThreadPool pool(4);
    
    // Submit tasks
    std::vector<std::future<int>> results;
    for (int i = 10; i <= 20; ++i) {
        results.emplace_back(pool.enqueue(fibonacci, i));
    }
    
    // Get and display results
    for (auto &&result : results) {
        std::cout << "Fibonacci result: " << result.get() << std::endl;
    }

    return 0;
}
