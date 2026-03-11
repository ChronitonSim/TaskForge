#include <cstddef>
#include <iostream>
#include <iomanip>
#include "Timer.hpp"
#include "MonteCarlo.hpp"

int main() {

    // 100 million to see measurable CPU time 
    constexpr std::size_t NUM_SAMPLES = 100'000'000;

    std::cout << "Starting Sequential MonteCarlo Pi Calculation... \n";
    
    Timer timer;

    // this call does all the work
    std::size_t num_points_inside_circle = calculate_pi_chunk(NUM_SAMPLES);

    double elapsed = timer.elapsed_milliseconds();

    double pi_estimate = 4.0 * static_cast<double>(num_points_inside_circle) / NUM_SAMPLES;

    std::cout << "Estimated Pi = " << std::setprecision(7) << pi_estimate << "\n";
    std::cout << "Time elapsed : " << elapsed << " ms\n";
    
    return 0;
}
