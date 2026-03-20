#pragma once

#include <cstddef>
#include <execution>
#include <numeric>
#include <random>
#include <array>
#include <numeric>
#include <execution>

inline std::size_t calculate_pi_chunk_simd(std::size_t num_samples){

    thread_local std::random_device rd;
    thread_local std::mt19937 gen(rd());

    // Use float instead of double to fit 
    // 8 floats instead of 4 doubles in the 
    // AVX-register 256 bits.
    thread_local std::uniform_real_distribution<float> dis(0.0f, 1.0f);

    std::size_t num_points_inside = 0;

    // Allocate arrays of 1024 floats to be SIMD processed later.
    // This amounts to 4KB, which easily fits the L1 cache.
    // This is necessary because each update of the Marsenne engine 
    // depends on the previous iteration, so updates cannot be 
    // parallelized.
    constexpr std::size_t BATCH_SIZE = 1024;
    std::array<float, BATCH_SIZE> x_vals;
    std::array<float, BATCH_SIZE> y_vals;

    std::size_t remaining = num_samples;
    while (remaining > 0) {
        std::size_t current_batch_size = std::min(remaining, BATCH_SIZE);

        // Sequential generation (the new bottleneck)
        for (std::size_t i = 0; i < current_batch_size; ++i) {
            x_vals[i] = dis(gen);
            y_vals[i] = dis(gen);
        }

        // SIMD-vectorized evaluation.
        // std::execution::unseq authorizes the compiler
        // to use AVX/SSE vector lanes.
        std::size_t num_points_inside_batch = std::transform_reduce(
            std::execution::unseq,  // execution policy
            x_vals.begin(), x_vals.end() + current_batch_size,  // ranges to apply the transformation to
            y_vals.begin(),
            0ULL,  // initial accumulator value
            std::plus<>{},  // reduction operation (summing the results of the transformation)
            [](float x, float y) -> size_t {  // transformation operation
                return (x*x + y*y <= 1.0f) ? 1 : 0;
            }                  
        );

        num_points_inside += num_points_inside_batch;
        remaining -= current_batch_size;
    }

    return num_points_inside;
}