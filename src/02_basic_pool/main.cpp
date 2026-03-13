#include <cstddef>
#include <thread>
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

    {
        // initialize the pool to spawn the OS threads
        ThreadPool pool(NUM_THREADS);

        // enqueue the tasks
        for (std::size_t i = 0; i < NUM_TASKS; ++i) {
            
            std::size_t chunk = NUM_SAMPLES_PER_TASK + (i < REMAINDER ? 1 : 0);

            // capture results by reference to store the data
            pool.enqueue(
                [&results, i, chunk] {
                    results[i] = calculate_pi_chunk(chunk);
                }
            );
        }
    } // the pool goes out of scope here; its destructor is invoked,
      // calling notify_all() and then join() on every worker
      // this implicitly guarantees all tasks are finished before 
      // we proceed to the accumulation step below

    double elapsed = timer.elapsed_milliseconds();

    std::size_t num_points_inside_circle = std::accumulate(results.begin(), 
                                                            results.end(), 
                                                            0ULL);
    double pi_estimate = 4 * static_cast<double> (num_points_inside_circle) / NUM_SAMPLES;

    std::cout << "Estimated Pi = " << std::setprecision(7) << pi_estimate << "\n";
    std::cout << "Time elapsed : " << elapsed << " ms\n";

    return 0;
}