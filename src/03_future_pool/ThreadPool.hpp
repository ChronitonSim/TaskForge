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
#include <type_traits>
#include <utility>
#include <vector>

class ThreadPool {
    private:
        std::vector<std::thread> workers_;
        std::queue<std::function<void()>> tasks_;

        std::mutex queue_mutex_;
        std::condition_variable condition_;
        bool stop_;

        void worker_loop();

    public:
        explicit ThreadPool(std::size_t num_threads);
        ~ThreadPool();

        // Delete copy operations to prevent illegal thread copying
        ThreadPool(const ThreadPool&) = delete;
        ThreadPool& operator=(const ThreadPool&) = delete;
        
        // C++17 - style enqueue template
        template<typename F, typename... Args>
        // use trailing return type for template clarity
        auto enqueue(F&& f, Args&&... args) -> std::future<std::invoke_result_t<F, Args...>> {
            
            // Deduce the return type of the submitted function.
            // E.g. If the user submits a function int multiply(int a, int b), 
            // then std::invoke_result_t<F, Args...> resolves to int.
            using return_type = std::invoke_result_t<F, Args...>;

            // Construct an std::shared_ptr of type std::packaged_task
            // accepting a callable with signature return_type(), i.e.
            // a callable returning return_type and accepting no arguments.
            auto task = std::make_shared<std::packaged_task<return_type()>> (
                // The make_shared constructor passes its arguments to the
                // packaged_task constructor, which in turn expects a callable
                // object to wrap. We provide that callable by passing the 
                // result of std::bind, which binds a function and its args
                // together into a new, zero-argument callable object.
                // Why std::bind? We need to glue the variadic arguments
                // (Args...) to the function (F) so it can be executed later.
                // In C++17, lambdas cannot easily capture or move variadic parameter 
                // packs. std::bind automatically handles the complex "decay-copy" 
                // and move semantics of the arguments for us, safely packaging 
                // them into a zero-argument callable that packaged_task can manage.
                std::bind(std::forward<F>(f), std::forward<Args>(args)...)
            );

            // Extract the future before we move the task into the queue.
            std::future<return_type> result = task->get_future();

            {
                std::lock_guard<std::mutex> lock(queue_mutex_);

                // HPC practice: assert the invariant
                // - compiles to nothing in Release builds (-DNDEBUG)
                // - but catches the bug during development.
                assert(!stop_ && "Fatal architectural error: enqueuing on a stopped ThreadPool.");
    
                // Wrap the shared_ptr into a void lambda that can be fed to 
                // std::queue::emplace() to be pushed into the queue.
                tasks_.emplace([task] {
                    // Dereference the shared_ptr and execute the packaged_task.
                    (*task)();
                });
            }
            
            // Signal a sleeping thread.
            condition_.notify_one();

            return result;
        }
};