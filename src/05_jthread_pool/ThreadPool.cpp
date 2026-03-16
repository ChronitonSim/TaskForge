#include "ThreadPool.hpp"
#include <cstddef>
#include <stop_token>

ThreadPool::ThreadPool(std::size_t num_threads) {
    for (std::size_t i = 0; i < num_threads; ++i)
        // C++ 20: the jthread ctor automatically passes its internal
        // stop_token to the lambda if the lambda accepts it.
        workers_.emplace_back([this](std::stop_token stoken) {
            worker_loop(stoken);
        });
}

void ThreadPool::worker_loop(std::stop_token stoken) {
    // Exit only if a stop is requested AND the queue is empty.
    while(!stoken.stop_requested() || !tasks_.empty()) {
        std::function<void()> task;

        {
            std::unique_lock<std::mutex> lock(queue_mutex_);

            // C++ 20: condition_variable_any::wait internally evaluates
            // both the stop_token and the predicate. When it returns,
            // it provides the final result of the predicate. 
            // It also automatically unblocks (returns) if the jthread
            // requests a stop. Specifically:
            // 
            // STOP = FALSE, PREDICATE = FALSE
            // The pool is running normally, but there are no tasks.
            // Do not return; release the mutex and suspend the thread.
            // 
            // STOP = FALSE, PREDICATE = TRUE
            // The pool is running normally, and there are tasks available.
            // Return true.
            //
            // STOP = TRUE, PREDICATE = TRUE
            // The pool destructor was called, but there are still tasks
            // in the queue. 
            // Return true, to ensure all remaining tasks are processed.
            //
            // STOP = TRUE, PREDICATE = FALSE
            // The pool destructor was called, and there are no tasks left.
            // Return false.
            bool awakened_status = condition_.wait(lock, stoken, [this] {
                return !tasks_.empty();
            });

            if (!awakened_status) return;

            // Retrieve the task from the queue.
            task = std::move(tasks_.front());
            tasks_.pop();

        } // Release the mutex.

        // Execute the task.
        task();
    }
}