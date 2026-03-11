#include <cstddef>
#include <numeric>
#include <thread>
#include <iostream>
#include <vector>
#include <iomanip>
#include "Timer.hpp"
#include "MonteCarlo.hpp"


void worker_task(std::size_t num_samples, std::size_t& result_out) {
    result_out = calculate_pi_chunk(num_samples);
}

int main() {

    constexpr std::size_t NUM_SAMPLES = 100'000'000;

    // determine how many threads to spawn
    const std::size_t NUM_THREADS = std::thread::hardware_concurrency();
    const std::size_t SAMPLES_PER_THREAD = NUM_SAMPLES / NUM_THREADS;
    const std::size_t REMAINDER = NUM_SAMPLES % NUM_THREADS;

    std::cout << "Starting naive parallel Monte Carlo with " << NUM_THREADS << " threads. \n";

    Timer timer;

    std::vector<std::thread> threads;
    std::vector<std::size_t> results(NUM_THREADS, 0);

    // spawn the threads
    for (std::size_t i = 0; i < NUM_THREADS; ++i) {
        // if this thread's index is less than the remainder,
        // it gets one extra unit of work; ensures load balancing
        std::size_t chunk = SAMPLES_PER_THREAD + (i < REMAINDER ? 1 : 0);

        threads.emplace_back(worker_task, chunk, std::ref(results[i]));
    }

    // wait for all threads to finish and join
    for (auto& t : threads) {
        if(t.joinable()) t.join();
    }

    double elapsed = timer.elapsed_milliseconds();

    // accumulate results
    std::size_t num_points_inside_circle = std::accumulate(results.begin(), 
                                                            results.end(), 
                                                            0ULL);
    double pi_estimate = 4 * static_cast<double> (num_points_inside_circle) / NUM_SAMPLES;

    std::cout << "Estimated Pi = " << std::setprecision(7) << pi_estimate << "\n";
    std::cout << "Time elapsed : " << elapsed << " ms\n";

    return 0;
}