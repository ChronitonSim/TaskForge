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

---

## Phase 2: Basic Thread Pool & Dynamic Load Balancing

### Architecture & Objective
Phase 2 introduces a centralized Task Queue managed by a pre-allocated Thread Pool (`std::mutex`, `std::condition_variable`). The objective is to replace the static workload distribution of Phase 1 with a dynamic "micro-chunking" strategy to mitigate the heterogeneous bottleneck.

### The Mathematics of Load Balancing
To understand the necessity of this architecture, we must define the mathematical bounds of heterogeneous execution.

**1. Static Load Balancing (Phase 1):**
Let $W$ be the total workload and $N$ be the number of threads. Each thread is assigned an equal chunk $W_i = \frac{W}{N}$.
The execution time for a specific core $i$ is $T_i = \frac{W_i}{P_i}$, where $P_i$ is the core's processing power.
Due to the `join()` barrier, total execution time is strictly bounded by the weakest core:
$$T_{total} = \max(T_1, T_2, ..., T_N)$$

**2. Dynamic Load Balancing (Phase 2):**
The workload $W$ is divided into $M$ micro-chunks ($M \gg N$), placed into a thread-safe queue. Fast P-cores will independently cycle through the queue, processing dozens of chunks in the time it takes an LP E-core to process one. 
Total execution time is no longer dictated by the slowest core, but by the aggregate power of the entire chip, plus the overhead of acquiring the mutex $M$ times:
$$T_{total} \approx \frac{W}{\sum_{i=1}^{N} P_i} + T_{overhead}$$

### Benchmark Results (Averaged over 5 runs)
*Total samples: 100,000,000. Divided into 1,000 tasks.*

| Implementation | Threads | Execution Time (ms) | Variance (ms) | Speedup vs Seq | Speedup vs Naive |
| :--- | :--- | :--- | :--- | :--- | :--- |
| Naive Parallel | 22 | 1488 | ± 24 | ~10.67x | - |
| Basic Thread Pool | 22 | 1272 | ± 14 | ~12.49x | ~1.17x |

### Architectural Analysis: Reclaiming Lost Silicon
The thread pool achieved a **~17% relative speedup** over the naive implementation, dropping the execution time to 1272 ms. 

1. **Integrating Active Silicon:** This 17% gain represents the exact area under the curve where the P-cores were previously sitting idle waiting for the LP E-cores. By continually feeding the fast silicon, we optimized the integral of active computation over time. Extracting this performance purely through a software-based load-balancing architectural shift—while simultaneously absorbing the overhead of 1,000 mutex locks—validates the core design.
2. **The Entropy Bottleneck Discovery:** Initial thread pool runs showed massive variance (± 132 ms). Profiling revealed that invoking `std::random_device` concurrently 1,000 times triggered severe OS-level locks on the kernel's entropy pool. By enforcing `thread_local` storage for the RNG machinery, system calls were reduced from 1,000 down to exactly 22. This eliminated the kernel bottleneck and allowed the true silicon throughput to be observed.
3. **Variance Reduction:** Execution variance dropped from ± 24 ms to ± 14 ms. The centralized queue acts as a self-regulating system. If the OS scheduler momentarily interrupts a P-core, the other cores naturally pull more tasks from the queue to compensate, resulting in a statistically robust execution profile.

---

## Phase 3: Modernizing the API with Asynchronous Futures

### Architecture & Objective
While Phase 2 successfully maximized hardware utilization, retrieving the computed data required the client code to manage a shared pre-allocated array, manually calculating offsets to avoid data races. In a production HPC environment, forcing the caller to manage shared memory is a critical vulnerability.

Phase 3 upgrades the Thread Pool to use the **Asynchronous Return Channel** pattern via C++17 template metaprogramming.
* The `enqueue` method uses variadic templates to accept arbitrary functions and arguments.
* Tasks are wrapped in `std::packaged_task` and returned to the caller as a `std::future`, allowing for precise, per-task synchronization without shared state variables.
* To satisfy the copy-constructibility constraints of the `std::function` queue, the move-only `std::packaged_task` is dynamically allocated on the heap via `std::shared_ptr`.

### Benchmark Results (Averaged over 5 runs)
*Total samples: 100,000,000. Divided into 1,000 tasks.*

| Implementation | Threads | Execution Time (ms) | Variance (ms) |
| :--- | :--- | :--- | :--- |
| Basic Thread Pool (Raw Lambdas) | 22 | 1272 | ± 14 |
| Future-Based Pool (Heap + Type Erasure) | 22 | 1274 | ± 11 |

### Architectural Analysis: Hiding the Cost of Abstraction
The transition to an asynchronous API introduced heavy abstraction overhead: 1,000 heap allocations (`std::make_shared`), multiple layers of type-erasure (`std::bind`), and smart pointer reference counting. Yet, the execution time effectively remained identical (a statistically negligible 2 ms difference). 

This zero-cost reality is achieved because the hardware aggressively hides the latency of our software abstractions through three distinct mechanisms:

#### 1. The Payload-to-Overhead Ratio (Amdahl's Shadow)
The computation required to process one micro-chunk is overwhelmingly larger than the cost of the abstraction. 
* **$T_{compute}$ Estimate:** Based on the Phase 1 sequential baseline (15889 ms for 100M samples), a single task of 100,000 samples takes exactly $15.889$ ms ($15,889,000$ ns) of raw CPU time.
* **$T_{overhead}$ Estimate:** Profiling a standard glibc `std::make_shared` allocation and `std::bind` operation on modern Intel silicon yields roughly $100$ ns.
* **The Ratio:** The computational payload is nearly $160,000\times$ larger than the allocation cost. The overhead accounts for $\approx 0.0006\%$ of the task's execution time, making it invisible at the macro scale.

#### 2. Asynchronous Pipelining
The abstraction overhead is executed concurrently alongside the payloads, completely hiding the latency behind the parallel throughput of the workers. Concretely, this means:
1. The main thread enters a loop to enqueue 1,000 tasks. 
2. It spends $\approx 100$ ns allocating Task 1, pushes it to the queue, and signals a worker.
3. Core 1 wakes up, grabs the task, and begins its massive $15.8$ ms floating-point calculation.
4. *Crucially, the main thread does not wait.* It immediately proceeds to allocate Task 2 (another 100 ns). 
5. The main thread rapidly allocates all 1,000 tasks in a total elapsed time of roughly $0.1$ ms ($1000 \times 100$ ns). It then blocks at `future.get()`.
By the time the main thread has finished all administrative abstraction work, the worker threads are only $0.1$ ms into their first $15.8$ ms compute cycles. The abstraction overhead does not extend the critical path; it is completely swallowed by the parallel timeline of the workers.

#### 3. Memory Allocation Optmization
Instead of performing two separate allocations (one for the object, one for the reference count), `std::make_shared` guarantees a single, contiguous heap allocation. Furthermore, modern Linux memory allocators utilize Thread-Caching (`tcache`), allowing the main thread to pull memory directly from a hot, lock-free cache, significantly driving down the latency of the 1,000 enqueue operations.

**Summary:** The abstraction overhead is hidden because it is astronomically small compared to the math, and the main thread front-loads all of those tiny allocations simultaneously while the 22 worker cores are already busy crunching the massive calculations.