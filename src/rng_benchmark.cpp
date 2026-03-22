#include <iostream>
#include <random>
#include "Timer.hpp"

int main() {
    constexpr std::size_t NUM_SAMPLES = 100'000'000;
    
    std::cout << "Benchmarking std::mt19937 (100 Million Pairs)...\n";

    // Setup RNG
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_real_distribution<float> dis(0.0f, 1.0f);

    // We need an accumulator so that -O3 doesn't optimize the loop away.
    float volatile_sink = 0.0f;

    Timer timer;

    // Generate 100M X and Y pairs sequentially
    for (std::size_t i = 0; i < NUM_SAMPLES; ++i) {
        volatile_sink += dis(gen);
        volatile_sink += dis(gen);
    }

    double elapsed = timer.elapsed_milliseconds();

    std::cout << "Sequential RNG Time : " << elapsed << " ms\n";
    std::cout << "Dummy output to prevent loop elision: " << volatile_sink << "\n";

    return 0;
}