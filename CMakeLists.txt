cmake_minimum_required(VERSION 3.10)
project(TaskForge VERSION 1.0)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Enforce Modern C++
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF) # Disable compiler-specific extensions (e.g., gnu++17)

# Force Release mode for aggressive -O3 optimizations
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
    message(STATUS "Setting build type to 'Release' (-O3 optimizations enabled)")
endif()

# --- Enable Compiler Diagnostics for Vectorization ---
if (CMAKE_CXX_COMPILER_ID MATCHES "GNU")
    # GCC flag: Prints successful vectorization loops
    add_compile_options(-fopt-info-vec-optimized)
elseif (CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    # Clang flags: Prints loop vectorization and unrolling details
    add_compile_options(-Rpass=loop-vectorize -Rpass=unroll)
endif()

# Make sure the compiler can find our header files
include_directories(include)

# Find the standard threading library (required for Linux/WSL)
find_package(Threads REQUIRED)

# Executable 1: Sequential
add_executable(01_sequential src/01_sequential/main.cpp)

# Executable 2: Naive Threads
add_executable(01_naive_threads src/01_naive_threads/main.cpp)
target_link_libraries(01_naive_threads PRIVATE Threads::Threads)

# Executable 3: Basic Thread Pool
add_executable(02_basic_pool 
    src/02_basic_pool/main.cpp 
    src/02_basic_pool/ThreadPool.cpp
)
target_link_libraries(02_basic_pool PRIVATE Threads::Threads)

# Executable 4: Future-Based Thread Pool (Phase 3)
add_executable(03_future_pool 
    src/03_future_pool/main.cpp 
    src/03_future_pool/ThreadPool.cpp
)
target_link_libraries(03_future_pool PRIVATE Threads::Threads)

# Executable 5: Hardware Cache Alignment Benchmark (Phase 4)
add_executable(04_hpc_aligned 
    src/04_hpc_aligned/main.cpp 
)
target_link_libraries(04_hpc_aligned PRIVATE Threads::Threads)

# Executable 6: C++20 jthread Pool (Phase 5)
add_executable(05_jthread_pool 
    src/05_jthread_pool/main.cpp 
    src/05_jthread_pool/ThreadPool.cpp
)
target_link_libraries(05_jthread_pool PRIVATE Threads::Threads)

# Executable 7: SIMD Vectorization (Phase 6)
add_executable(06_simd_vectorization 
    src/06_simd_vectorization/main.cpp 
    src/06_simd_vectorization/ThreadPool.cpp
)
target_link_libraries(06_simd_vectorization PRIVATE Threads::Threads)

# --- RNG Isolation Benchmark ---
add_executable(rng_benchmark src/rng_benchmark.cpp)