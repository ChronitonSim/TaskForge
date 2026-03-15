#include <cstddef>
#include <future>
#include <iostream>
#include <vector>
#include <iomanip>
#include "Timer.hpp"
#include "ThreadPool.hpp"
#include "MonteCarlo.hpp"

int main() {

    constexpr std::size_t NUM_SAMPLES = 100'000'000;

    // dynamic load balancing via over-subscription
    // we create significantly more tasks than we have threads
    constexpr std::size_t NUM_TASKS = 1000;
    constexpr std::size_t NUM_SAMPLES_PER_TASK = NUM_SAMPLES / NUM_TASKS;
    constexpr std::size_t REMAINDER = NUM_SAMPLES % NUM_TASKS;

    std::size_t NUM_THREADS = std::thread::hardware_concurrency();

    std::cout << "Starting thread-pool Monte Carlo with " << NUM_THREADS
              << "threads and " << NUM_TASKS << " tasks.\n";

    // vector to hold the results of each micro-chunk
    // pre-allocate and initialize to avoid data races during insertion
    std::vector<std::size_t> results(NUM_TASKS, 0);

    Timer timer;

    std::size_t num_points_inside_circle = 0;

    {
        ThreadPool pool(NUM_THREADS);

        // std::vector to hold the future tickets returned
        // by the thread pool
        std::vector<std::future<std::size_t>> futures;
        futures.reserve(NUM_TASKS);

        // Dispatch phase.
        for (std::size_t i = 0; i < NUM_TASKS; ++i) {

            std::size_t chunk = NUM_SAMPLES_PER_TASK + (i < REMAINDER ? 1 : 0);

            // This call becomes clean. No more need for a void lambda 
            // capturing by reference a potentially unsafe vector of results
            // handled by the caller.
            futures.emplace_back(pool.enqueue(calculate_pi_chunk, chunk));
        }

        // Accumulation phase.
        for (auto &fut: futures) {
            // fut.get() is a blocking call. If the task linked to fut 
            // is finished, it returns immediately. Otherwise, the main
            // thread sleeps here until the associated worker thread
            // fulfills the packaged_task.
            num_points_inside_circle += fut.get();
        }
    }

    double elapsed = timer.elapsed_milliseconds();

    double pi_estimate = 4 * static_cast<double> (num_points_inside_circle) / NUM_SAMPLES;

    std::cout << "Estimated Pi = " << std::setprecision(7) << pi_estimate << "\n";
    std::cout << "Time elapsed : " << elapsed << " ms\n";
    return 0;
}