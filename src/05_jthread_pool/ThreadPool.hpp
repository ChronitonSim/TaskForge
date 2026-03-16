#pragma once

#include <cassert>
#include <condition_variable>
#include <cstddef>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <queue>
#include <thread>
#include <stop_token>
#include <type_traits>
#include <utility>
#include <vector>

class ThreadPool {
    private:
        std::mutex queue_mutex_;
        // C++ 20: upgrade to condition_variable_any to work with stop_token
        std::condition_variable_any condition_;
        std::queue<std::function<void()>> tasks_;

        // C++ 20: jthreads must be declared last, so they are destroyed first.
        // This ensures threads join before the queue and mutex are destroyed.
        std::vector<std::jthread> workers_;

        // C++ 20: The loop now accepts a stop-token 
        // automatically injected by jthread.
        void worker_loop(std::stop_token stoken);

    public:
        explicit ThreadPool(std::size_t num_threads);

        // C++ 20: the compiler-generated destructor is now safe.
        // When workers_ is destroyed, jthread automatically requests stop
        // and joins.
        ~ThreadPool() = default;

        ThreadPool(const ThreadPool&) = delete;
        ThreadPool& operator=(const ThreadPool&) = delete;

        template<typename F, typename... Args>
        auto enqueue(F&& f, Args&&... args) -> std::future<std::invoke_result_t<F, Args...>> {

            using return_type = std::invoke_result_t<F, Args...>;

            auto task = std::make_shared<std::packaged_task<return_type()>> (
                std::bind(std::forward<F>(f), std::forward<Args>(args)...)
            );

            std::future<return_type> result = task -> get_future();

            {
                std::lock_guard<std::mutex> lock(queue_mutex_);
                tasks_.emplace([task] {
                    (*task)();
                });
            }

            condition_.notify_one();
            return result;
        }
};