#include "ThreadPool.hpp"
#include <cstddef>
#include <functional>
#include <mutex>

ThreadPool::ThreadPool(std::size_t num_threads) : stop_(false) {
    for (std::size_t i = 0; i < num_threads; ++i)
        // Each thread captures the 'this' pointer to access class members.
        workers_.emplace_back([this]() { worker_loop(); });
}

ThreadPool::~ThreadPool() {
    {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        stop_ = true;
    }

    condition_.notify_all();

    for (std::thread &worker : workers_) {
        if (worker.joinable()) worker.join();
    }
}

void ThreadPool::worker_loop() {
    while (true) {
        std::function<void()> task;
        
        {
            std::unique_lock<std::mutex> lock(queue_mutex_);

            // Sleep until a task arrives or the pool is shutting down.
            condition_.wait(lock, [this] {
                return stop_ || !tasks_.empty();
            });

            // If shutting down and no tasks remain,
            // exit the loop, terminating the thread.
            if (stop_ && tasks_.empty()) return;

            // pop the task safely
            task = std::move(tasks_.front());
            tasks_.pop();
        } // queue_mutex_ goes out of scope and is unlocked

        // Execute the computational payload outside the lock.
        // If it throws, the exception is caught by the std::packaged_task
        // and transfered to the std::future.
        task();
    }
}