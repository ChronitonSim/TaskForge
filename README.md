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

---

## Phase 4: Hardware Cache Alignment & False Sharing

### Architecture & Objective: False Sharing and Cache Coherence

This final phase steps outside of software engineering to address a catastrophic hardware-level bottleneck: **False Sharing**. 

**The Theory:** Modern CPUs fetch memory in 64-byte contiguous blocks called *Cache Lines*. To maintain data consistency across multiple cores, Intel processors employ a cache coherence protocol (typically MESI: Modified, Exclusive, Shared, Invalid). Crucially, MESI enforces coherence at the *cache line granularity*, not the individual variable granularity. 

False sharing occurs when independent threads modify distinct, logically isolated variables that happen to reside on the same 64-byte cache line. When Core A writes to its variable, the MESI protocol marks the entire cache line as *Modified* and broadcasts an *Invalidate* signal. Core B, attempting to write to its own adjacent variable, suffers a cache miss and must expensively re-fetch the entire line from the L3 cache. The cores are not sharing data, but they are fighting over a physical address boundary, creating a relentless invalidation storm that cripples execution speed.

**The Objective:** We demonstrate this phenomenon using a dedicated benchmark (`04_hpc_aligned`) where 22 threads continuously increment independent integers. We will compare the catastrophic performance of a tightly packed array against a structure padded to the CPU's exact cache line size, physically forcing L1 cache isolation.

### Retrospective: False Sharing in the Thread Pool
A crucial architectural question arises: why didn't False Sharing destroy the performance of our Thread Pool in Phases 2 and 3?

**Phase 2 Defense: Local Accumulation**
In Phase 2, we passed a pre-allocated `std::vector<std::size_t>` to the workers. Since vectors are contiguous, the 8-byte results physically shared cache lines. However, inside `calculate_pi_chunk`, the `points_inside` counter was a local variable, kept securely in a CPU register for the entire 15.8 ms loop. The thread only wrote to the shared memory array *exactly once* at the very end of the task. 

This pattern is **Local Accumulation**. Quantitatively, for 1,000 tasks, we incurred only 1,000 writes to the shared vector. Assuming a conservative 100 ns penalty per MESI invalidation:
1000 writes * 100 ns = 100,000 ns = 0.1 ms
The false sharing penalty was a microscopic 0.1 ms. Had we scaled the workload to 10,000,000 micro-chunks, the cache invalidation penalty would have exploded to a full 1.0 second (10,000,000 * 100 ns), proving that memory layout becomes paramount at scale.

**Phase 3 Defense: Heap Dispersion & Asynchronous Separation**
Phase 3 introduced a `std::vector<std::future<std::size_t>>`. While the futures themselves are tightly packed in the contiguous vector, the asynchronous architecture naturally defeated false sharing by decoupling the read and write destinations:
1. **The Write Destination:** Worker threads do not write to the contiguous `std::future` objects. They write to the hidden `std::packaged_task` "Shared State", dynamically allocated on the heap via `std::make_shared`. These shared states are large (often >64 bytes) and pseudo-randomly scattered across system memory by the OS allocator. The 22 cores wrote to widely dispersed, physically isolated memory addresses, completely preventing cache line overlap.
2. **The Read Destination:** The contiguous array of futures is only ever accessed by the single main thread calling `fut.get()`. With no concurrent writes to the vector, the MESI protocol remains dormant.

Ironically, the heavy abstraction overhead of `std::future` unintentionally provided perfect hardware isolation.

### Implementation Notes: The Standalone Benchmark
To truly demonstrate the devastating effects of the MESI protocol without the protective abstractions of our pool, the following benchmark forces threads to continuously write directly to shared memory.

*(Note: The `volatile` keyword in the structs below is critical. Without it, the GCC/Clang optimizer would recognize the loop and collapse the 100,000,000 increments into a single `value += 100000000` instruction. `volatile` forces the CPU to execute a real L1 cache memory write on every single iteration, guaranteeing hardware contention).*

**1. The Packed Struct (Triggering False Sharing)**
```cpp
struct PackedResult {
    volatile std::size_t value{0}; // 8 bytes
};
```
Eight of these structs fit perfectly into a single 64-byte cache line. When we allocate `std::vector<PackedResult> packed_array(22)`, we pack the thread targets perfectly adjacent to each other.

**2. The Aligned Struct (Defeating False Sharing)**
```cpp
struct AlignedResult {
    alignas(std::hardware_destructive_interference_size) volatile std::size_t value{0};
};
```
Using C++17's `alignas` coupled with `std::hardware_destructive_interference_size` ensures true platform portability. The compiler automatically injects 56 bytes of invisible padding, forcing every `AlignedResult` to occupy its own isolated cache line.

### Benchmark Results (100,000,000 iterations per thread)

| Memory Layout | Size per Element | Execution Time (ms) | Variance (ms) | Speedup |
| :--- | :--- | :--- | :--- | :--- |
| Packed Array (False Sharing) | 8 bytes | ~1198 | ± 35 | 1.00x |
| Cache-Aligned Array (Isolated) | 64 bytes | ~116 | ± 2 | **~10.32x** |

### Architectural Analysis: The Invalidation Storm
The `alignas` defense yielded a massive **~10.32x hardware speedup**. 

1. **The Cost of Cache Contention:** In the tightly packed array, the CPU spent roughly 116 ms actually computing the mathematical increments, and a staggering 1,082 ms stalled. The cores were trapped in an invalidation storm, continuously halting their execution pipelines while waiting for the L3 cache to arbitrate ownership of the memory block.
2. **Sacrificing RAM for Throughput:** By padding the struct out to 64 bytes, we intentionally wasted 1,232 bytes of system RAM ($22 \times 56$ bytes of padding). In exchange for this microscopic memory footprint, we completely decoupled the L1 caches. The invalidation broadcasts dropped to zero, allowing the silicon to run unthrottled.
3. **Restoring Determinism:** The variance plunged from ± 35 ms down to an incredibly tight ± 2 ms. By removing the unpredictable OS-level hardware contention, the execution profile became perfectly predictable.

## Phase 5: Modernizing with C++20 `std::jthread` and Stop Tokens

### Architecture & Objective
The objective of Phase 5 is to elevate the codebase to the C++20 standard, specifically targeting thread lifecycle management. In earlier phases, safely shutting down the pool required a delicate, manual choreography of a boolean `stop_` flag, mutex locking, and `notify_all()` broadcasts to prevent the "Deadlock of the Sleeping Worker."

This phase replaces `std::thread` with `std::jthread`, which guarantees automatic joining upon destruction. Furthermore, the manual shutdown logic is completely eliminated by integrating C++20's `std::stop_token`. To facilitate this, `std::condition_variable` was upgraded to `std::condition_variable_any`, allowing the worker threads to suspend and wake up natively based on the pool's destruction state.

### Benchmark Results (Averaged over 5 runs)
*Total samples: 100,000,000. Divided into 1,000 tasks.*

| Implementation | Threads | Execution Time (ms) | Variance (ms) |
| :--- | :--- | :--- | :--- |
| Future-Based Pool (Phase 3) | 22 | 1274 | ± 11 |
| C++20 jthread Pool (Phase 5) | 22 | 1304 | ± 17 |

### Architectural Analysis: The Cost of Developer Safety
The benchmark reveals a consistent performance regression of **~30 milliseconds** (a 2.3% overhead) compared to the C++17 implementations. This marks our first architectural change where a higher-level abstraction demonstrably reduced bare-metal throughput.

1. **The Weight of `condition_variable_any`:** The standard `std::condition_variable` is fiercely optimized by compiler developers to map directly to bare-metal OS synchronization primitives (like `pthread_cond_t`), as it only accepts a `std::unique_lock<std::mutex>`. Upgrading to `condition_variable_any` provides type-erased flexibility to accept any lockable type, but introduces heavier internal state management.
2. **Hidden Callback Management:** When `condition_.wait(lock, stoken, predicate)` is invoked, the C++20 standard library dynamically registers a hidden `std::stop_callback` to the `std::stop_token` before putting the thread to sleep. When the thread wakes up normally to process a task, it must safely deregister and destroy that callback. Across 22 threads constantly hitting an empty queue, these hidden atomic reference counts and memory barriers take a measurable toll.
3. **Conclusion:** This phase highlights the quintessential systems engineering trade-off: **Developer Safety vs. Bare-Metal Performance.** We traded a relatively small 2.3% performance hit for the complete elimination of manual teardown logic, certainty against shutdown deadlocks, and a strictly RAII-compliant codebase. In an enterprise environment outside of strict real-time latency budgets, this is a highly favorable trade.