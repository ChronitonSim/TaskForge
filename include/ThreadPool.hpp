#pragma once

#include <vector>
#include <thread>
#include <queue>
#include <functional>
#include <mutex>
#include <condition_variable>
#include <cstddef>


class ThreadPool {

    private:
        std::vector<std::thread> workers_;
        std::queue<std::function<void()>> tasks_;

        // synchronization primitives
        std::mutex queue_mutex_;
        std::condition_variable condition_;
        bool stop_;

        // the infinite loop executed by every worker thread
        void worker_loop();

    public:
        // pre-allocate and start the worker threads
        explicit ThreadPool(std::size_t num_threads);

        // safely shut down all threads
        ~ThreadPool();

        // prevent copying the ThreadPool
        // copying a vector of threads is undefined behavior
        ThreadPool(const ThreadPool&) = delete;
        ThreadPool& operator=(const ThreadPool&) = delete;
        
        // enqueue a new task into the thread pool
        void enqueue(std::function<void()> task);
};