# Phase 6: Instruction-Level Parallelism with SIMD Vectorization

This phase transitions our optimization strategy from Thread-Level Parallelism (distributing work across multiple CPU cores) to Instruction-Level Parallelism (processing multiple data points simultaneously within a single CPU core) using SIMD (Single Instruction, Multiple Data).

## The Objective: Utilizing Vector Registers
Modern CPUs feature wide vector registers (e.g., 256-bit AVX2 registers) capable of holding multiple floating-point numbers. Instead of scalar execution (processing one $x^2 + y^2 \le 1.0$ equation at a time), SIMD allows the CPU to calculate 8 of these equations in a single clock cycle. This phase achieves this using modern, hardware-agnostic C++ standard algorithms rather than hardware-specific compiler intrinsics.

## Architectural Deep Dive

### 1. The RNG State Bottleneck: Mersenne Twister vs. SIMD
The primary obstacle to vectorizing a Monte Carlo simulation is the random number generator itself. `std::mt19937` (Mersenne Twister) maintains a massive internal state array (over 2KB) that mutates sequentially with every generated number. 

Because the state of iteration $N$ strictly depends on the state of iteration $N-1$, the generation of random numbers fundamentally cannot be executed in parallel SIMD lanes. Attempting to force vectorization on the RNG will either result in compiler refusal or memory corruption. 

The solution is to decouple the *generation* phase from the *evaluation* phase. We sequentially fill a localized buffer with random floats, and then unleash SIMD vectorization strictly on the mathematical evaluation of that buffer.

### 2. Memory Architecture: `std::array` vs. `std::vector`
To buffer the random numbers, we use `std::array<float, 1024>` rather than `std::vector<float>`. 

* **The Heap Contention of `std::vector`:** A `std::vector` allocates its data buffer dynamically on the heap. If 22 threads continuously construct and destroy vectors inside a tight `while` loop, they will violently hammer the OS memory allocator, introducing massive lock contention and system-call overhead that would negate any SIMD performance gains.
* **The Stack Efficiency of `std::array`:** `std::array` is a zero-overhead wrapper around a C-style array. It allocates memory exclusively on the fast, thread-local stack. Furthermore, because its size is permanently baked into the type at compile-time, the compiler can perform aggressive loop unrolling and perfectly align the memory blocks to feed the CPU's AVX registers. A 1024-float array consumes exactly 4KB, locking it permanently into the ultra-fast L1 cache.

### 3. The Vectorization Engine: `std::transform_reduce`
To perform the math, we replace manual loops with `std::transform_reduce` from the `<numeric>` header. It operates as a high-performance, two-stage assembly line:
1. **Transform (Map):** It maps our condition `(x * x + y * y <= 1.0f) ? 1 : 0` over the arrays.
2. **Reduce (Sum):** It folds those binary results into a single running total using `std::plus<>{}`.

**The Execution Policy: `std::execution::unseq`**
Standard C++ strictly enforces sequential loop iteration. By passing the `unseq` (Unsequenced) policy from the `<execution>` header, we sign a contract with the compiler. We guarantee that the loop iterations are completely independent, share no state, and allocate no memory. Released from sequential constraints, the compiler's auto-vectorizer packs the transformation lambda into SIMD instructions, drastically accelerating throughput.

## The New Execution Flow: Step-by-Step

1. **Task Submission:** The main thread enqueues a massive chunk of work (e.g., 100,000 samples) to the `ThreadPool`.
2. **Worker Awakening:** A worker thread pulls the task from the queue and enters the payload function.
3. **Stack Initialization:** The worker instantly allocates two 4KB `std::array` buffers on its local stack for X and Y coordinates.
4. **Sequential Batching:** The worker enters a `while` loop, processing the 100,000 samples in bite-sized batches of 1024. It uses the Mersenne Twister to sequentially fill the X and Y arrays with random floats.
5. **SIMD Evaluation:** The worker calls `std::transform_reduce(std::execution::unseq, ...)`. The CPU loads the X and Y arrays into its wide vector registers, simultaneously calculating 8 data points per clock cycle, and summing the points that fall inside the circle.
6. **Accumulation & Return:** The batch results are accumulated. Once all 100,000 samples are processed, the total is seamlessly returned to the main thread via the `std::future` channel.