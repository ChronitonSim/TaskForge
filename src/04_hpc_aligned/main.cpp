#include <cstddef>
#include <thread>
#include <iostream>
#include <vector>
#include <iomanip>
#include "Timer.hpp"

// Tightly packed structure susceptible to false sharing.
// Size: 8 bytes; 8 of these fit into a 64-byte L1 cache line
struct PackedResult {
    volatile std::size_t value{0};
};

// Cache-aligned structure immune to false sharing.
// Size: 64 bytes, of which 56 are added by the compiler as padding.
struct AlignedResult {
    alignas(std::hardware_destructive_interference_size) volatile std::size_t value{0};
};

constexpr std::size_t NUM_ITERATIONS = 100'000'000;

template<typename T>
void worker_task(T& result) {
    for (std::size_t i = 0; i < NUM_ITERATIONS; ++i)
        // Triggers MESI-protocol invalidation if sharing a cache line!
        ++result.value;
}

int main() {
    const std::size_t NUM_THREADS = std::thread::hardware_concurrency();

    std::cout << "Starting L1 Cache Alignment Benchmark (" 
              << NUM_THREADS << " threads)...\n\n";
              
    std::vector<std::thread> threads;
    threads.reserve(NUM_THREADS);

    // --- TEST 1: False Sharing ---
    std::vector<PackedResult> packed_array(NUM_THREADS);

    Timer timer_packed;

    for (std::size_t i = 0; i < NUM_THREADS; ++i)
        threads.emplace_back(worker_task<PackedResult>, std::ref(packed_array[i]));

    for (auto &t : threads) t.join();

    double time_packed = timer_packed.elapsed_milliseconds();

    std::cout << "1. Packed Array (False Sharing)\n";
    std::cout << "   Struct size : " << sizeof(PackedResult) << " bytes\n";
    std::cout << "   Time elapsed: " << std::fixed << std::setprecision(2) << time_packed << " ms\n\n";

    threads.clear();

    // --- TEST 2: CACHE ALIGNED ---
    std::vector<AlignedResult> aligned_array(NUM_THREADS);

    Timer timer_aligned;

    for (std::size_t i = 0; i < NUM_THREADS; ++i)
        threads.emplace_back(worker_task<AlignedResult>, std::ref(aligned_array[i]));

    for (auto &t : threads) t.join();

    double time_aligned = timer_aligned.elapsed_milliseconds();

    std::cout << "2. Aligned Array (True Isolation)\n";
    std::cout << "   Struct size : " << sizeof(AlignedResult) << " bytes\n";
    std::cout << "   Time elapsed: " << time_aligned << " ms\n\n";

    // --- ANALYSIS ---
    double speedup = time_packed / time_aligned;
    std::cout << "Hardware Speedup: " << speedup << "x\n";

    return 0;
}