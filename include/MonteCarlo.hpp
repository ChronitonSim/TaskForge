#pragma once

#include <random>
#include <cstddef>

// calculates how many random points fall inside
// the upper right quadrant of the unit circle.
inline std::size_t calculate_pi_chunk(std::size_t num_samples) {
    std::size_t num_points_inside = 0;

    // thread_local ensures initialization happens exactly once 
    // per worker thread; this is critical to prevent catastrophic 
    // system-call overhead when over-subscribing 
    // i.e. when the number of calls to this function is >>
    // than the number of threads

    // ask the OS for true, non-deterministic randomness 
    // from hardware noise; slow, only do it once
    thread_local std::random_device rd;
    
    // use the true random seed to initialize the Marsenne
    // Twister Engine, with a massive period length of $2^{19937} - 1$.
    thread_local std::mt19937 gen(rd());

    // convert the raw integers from the engine into double
    // values uniformly distributed between 0.0 and 1.0
    thread_local std::uniform_real_distribution<double> dis(0.0, 1.0);

    for(std::size_t i = 0; i < num_samples; ++i) {
        double x = dis(gen);
        double y = dis(gen);

        // If the point is inside the quarter circle
        if (x*x + y*y <= 1.0) num_points_inside++;
    }

    return num_points_inside;
}