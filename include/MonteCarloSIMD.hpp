#pragma once

#include <cstddef>
#include <random>
#include <array>

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
        // We avoid std::transform_reduce to avoid TBB backend anomalies.
        // -O3 will automatically unroll and vectorize this raw loop using AVX.
        float num_points_inside_batch = 0.0f;
        
        for (std::size_t i = 0; i < current_batch_size; ++i) {
            float x = x_vals[i];
            float y = y_vals[i];
            
            // The compiler easily translates this directly into a vcmpleps 
            // (Vector Compare Less-Than-or-Equal) SIMD instruction.
            num_points_inside_batch += (x * x + y * y <= 1.0f) ? 1.0f : 0.0f;
        }

        num_points_inside += static_cast<std::size_t>(num_points_inside_batch);
        remaining -= current_batch_size;
    }

    return num_points_inside;
}