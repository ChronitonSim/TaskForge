cmake_minimum_required(VERSION 3.10)
project(TaskForge VERSION 1.0)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Enforce Modern C++
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF) # Disable compiler-specific extensions (e.g., gnu++17)

# Make sure the compiler can find our header files
include_directories(include)

# Find the standard threading library (required for Linux/WSL)
find_package(Threads REQUIRED)

# Executable 1: Sequential
add_executable(01_sequential src/01_sequential/main.cpp)

# Executable 2: Naive Threads
add_executable(01_naive_threads src/01_naive_threads/main.cpp)
target_link_libraries(01_naive_threads PRIVATE Threads::Threads)
