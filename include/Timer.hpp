#pragma once 

# include <chrono>

class Timer {
    private:
        std::chrono::time_point<std::chrono::high_resolution_clock> start_time_;

    public:
        // Starts the clock when the timer object is created
        Timer() : start_time_(std::chrono::high_resolution_clock::now()) {}

        double elapsed_milliseconds() const {
            auto end_time = std::chrono::high_resolution_clock::now();
            std::chrono::duration<double, std::milli> duration = end_time - start_time_;
            return duration.count();
        }

        void reset() {
            start_time_ = std::chrono::high_resolution_clock::now();
        }

};