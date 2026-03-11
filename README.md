# TaskForge
A high-performance C++17 thread pool. Documents the architectural evolution and benchmarking from naive thread spawning to a cache-optimized, lock-aware task queue.

## System Specifications
To ensure benchmark reproducibility and accurate architectural analysis, all tests are executed under the following hardware and software constraints:
* **CPU:** Intel Core Ultra 7 155H (Meteor Lake Architecture)
* **Topology:** 22 Logical Threads (6 P-Cores with Hyperthreading, 8 E-Cores, 2 LP E-Cores)
* **OS:** Ubuntu 24.04 (WSL2 on Windows 11)
* **Compiler:** GCC/Clang via CMake (C++17 Standard, Extensions Disabled)



## Phase 1: Baseline & Naive Parallelism

### Architecture & Objective
The objective of Phase 1 is to establish a baseline for a CPU-bound workload (Monte Carlo estimation of Pi with 100,000,000 samples) and measure the overhead of naive thread management. 

Two executables were developed:
1. `01_sequential`: Processes the entire workload on a single thread.
2. `01_naive_threads`: Queries `std::thread::hardware_concurrency()` and dynamically spawns a `std::thread` for each logical core, statically dividing the workload before joining all threads.

### Implementation Notes
* **Thread-Local RNG:** Utilized C++11 `<random>` (`std::random_device`, `std::mt19937`) with thread-local instances to prevent false sharing and lock contention during parallel execution.
* **Load Balancing:** Addressed integer truncation during static workload partitioning by distributing the remainder using a ternary operator, ensuring the maximum variation among thread workloads is exactly 1 sample.

### Benchmark Results (Averaged over 5 runs)

| Implementation | Threads | Execution Time (ms) | Variance (ms) | Speedup |
| :--- | :--- | :--- | :--- | :--- |
| Sequential Baseline | 1 | 15889 | ± 1731 | 1.00x |
| Naive Parallel | 22 | 1488 | ± 24 | ~10.67x |

### Architectural Analysis: The Heterogeneous Bottleneck
While a ~10.67x speedup is significant, it falls short of the theoretical 22x linear scaling. This exposes a critical flaw in naive static workload distribution on modern heterogeneous hardware.



1. **The Join Barrier Problem:** The Meteor Lake CPU features highly powerful P-cores and very slow LP E-cores. Because the workload was divided statically and equally (approx. 4.54 million samples per thread), the P-cores finished their calculations rapidly. However, the `std::thread::join()` barrier forced the entire program to halt and wait for the weakest LP E-cores to finish their identical chunks. The application was bottlenecked by its slowest silicon.
2. **Execution Variance:** The sequential run exhibited massive variance (± 1731 ms) due to the OS scheduler migrating the single active thread across different core types (P to E to LP-E) over its 15-second lifespan. In contrast, the parallel run exhibited minimal variance (± 24 ms) because saturating all 22 cores immediately triggered the CPU's thermal/power limits (PL1/PL2), locking the processor into a steady, non-migratory state.
