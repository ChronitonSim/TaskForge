# Phase 6: Instruction-Level Parallelism with SIMD Vectorization

This phase augments Thread-Level Parallelism (distributing work across multiple CPU cores) with Instruction-Level Parallelism (processing multiple data points simultaneously within a single CPU core) using SIMD (Single Instruction, Multiple Data).

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
* **The Stack Efficiency of `std::array` (Size Baked into the Type):** `std::vector<float>` is a single type, regardless of its size, forcing the compiler to inject runtime logic to ask the OS for dynamic heap memory. `std::array<float, 1024>` uses a *non-type template parameter*. To the compiler, `1024` is part of the genetic makeup of the type itself. Because the size is permanently hardcoded, the compiler knows exactly how many bytes it needs (4096 bytes) before the program runs. It simply moves the stack pointer by exactly 4096 bytes the microsecond the function is called—yielding zero allocation latency.

### 3. Hardware-Level Mechanics: Alignment, Unrolling, and AVX
Let us examine how the compiler maps `std::array` and our vectorized loops to the physical silicon.

#### The AVX Register Orchestra
Modern Intel/AMD chips feature YMM registers (AVX/AVX2) that are exactly 256 bits wide. Since a `float` is 32 bits, one YMM register perfectly holds 8 scalar floats. The vectorizer utilizes an orchestra of these registers simultaneously:
* **Register A (`ymm0`):** Loads 8 $x$ values.
* **Register B (`ymm1`):** Loads 8 $y$ values.
* **Register C (`ymm2`):** Holds the constant $1.0f$, copied 8 times across the register.

**The Clock Cycle Breakdown:**
1. **Multiply:** A vector-multiply instruction squares Register A ($x^2$) and Register B ($y^2$) in parallel.
2. **Add:** A vector-add instruction merges them into Register A ($x^2 + y^2$).
3. **Compare:** A vector-compare evaluates Register A against Register C ($\le 1.0f$), instantly generating a mask of 1s and 0s for all 8 points.

#### Memory Alignment: Feeding the Registers
AVX execution units are physically wired via a hardware data bus to pull exactly 256 bits (32 bytes) from the L1 cache in a single clock cycle. However, the L1 cache organizes memory in rigid 64-byte blocks called Cache Lines. 

If our 4KB array starts at an unaligned address (e.g., `5050`), a 32-byte chunk might straddle two different cache lines. Since cache lines are 64-byte aligned (e.g., Cache Line A spans `4992-5055`, and Cache Line B spans `5056-5119`), a 32-byte read from `5050` means 6 bytes (`5050-5055`) sit in Cache Line A, while the remaining 26 bytes (`5056-5081`) spill into Cache Line B. The CPU hardware must physically issue two separate read commands to the L1 cache and stitch the halves together, incurring a severe "Cache Line Split" performance penalty.

Because `std::array` lives on the stack, the compiler can guarantee **32-byte alignment**. If the OS stack pointer sits at address `5000`, the compiler inserts 24 bytes of invisible padding to permanently anchor the array at exactly `5024` ($5024 / 32 = 157$). Now, the CPU sweeps through the array 128 times, loading exactly 32 bytes per batch. Because the address is perfectly aligned, every single read fits entirely within a cache line boundary, preventing memory stalls.

#### Loop Unrolling and Superscalar Concurrency
Iterating over 1024 floats in batches of 8 requires 128 loop iterations. A naive loop incurs constant administrative overhead: checking the branch condition (`i < 1024`), jumping, and incrementing. 

Because our `std::array` size is baked into the type, the compiler **unrolls** the loop. It reduces the jumps from 128 down to 32 by sequentially copy-pasting the SIMD instructions:

```cpp
// Conceptually Unrolled Loop (Only 32 jumps instead of 128)
for (int i = 0; i < 1024; i += 32) { 
    // We evaluate the branch condition only once every 32 floats
    std::size_t sum1 = simd_calculate_8_points(&x_vals[i]);      // Floats 0-7
    std::size_t sum2 = simd_calculate_8_points(&x_vals[i + 8]);  // Floats 8-15
    std::size_t sum3 = simd_calculate_8_points(&x_vals[i + 16]); // Floats 16-23
    std::size_t sum4 = simd_calculate_8_points(&x_vals[i + 24]); // Floats 24-31
    
    total += (sum1 + sum2 + sum3 + sum4);
}
```
This unrolling eliminates branch-prediction stalls, but it also unleashes **Superscalar Concurrency**. Because `sum1`, `sum2`, `sum3`, and `sum4` do not depend on one another, the CPU's hardware scheduler dispatches these instructions to different execution ports concurrently. Unrolling allows the processor to juggle up to 32 equations simultaneously across its pipeline, pushing the silicon to 100% saturation.

#### The Cost Model: Why Limit Unrolling to 4?
While unrolling unleashes concurrency, the compiler limits the unroll factor (in this case, processing 4 batches of 8 floats simultaneously) to avoid hitting a hard physical ceiling known as **Register Pressure**.

The x86-64 AVX2 architecture contains 16 YMM registers (`ymm0` through `ymm15`). The compiler's internal Cost Model calculates the exact register consumption before generating the assembly code:
* **Per Operation:** Each 8-float batch calculation requires 3 registers (1 to load the X values, 1 to load the Y values, and 1 dedicated accumulator for the running sum).
* **Shared State:** The condition constant (`1.0f`) is loaded once into a single shared register, acting as a read-only reference for all concurrent operations.
* **The Math:** For an unroll factor of 4, the compiler requires `(3 registers/op * 4 ops) + 1 shared constant = 13 total registers`.

Because 13 is safely below the 16-register limit, all data remains instantly accessible in the silicon. If the compiler aggressively unrolled by 8, it would require 25 registers (`3 * 8 + 1`). Lacking physical space, the CPU would be forced to temporarily evict ("spill") intermediate calculations out of the registers and into the L1 cache. This phenomenon, known as "Register Spilling," introduces severe memory latency and destroys pipeline momentum. By capping the unroll factor at 4, the compiler intelligently saturates the FMA (Fused Multiply-Add) execution ports without overflowing the physical silicon.

### 4. The Abstraction Penalty: TBB vs. Raw Auto-Vectorization
Initially, this project utilized `std::transform_reduce` with the `std::execution::unseq` policy to force vectorization. However, this execution policy quietly delegates the math to the Intel Thread Building Blocks (TBB) backend.

**The TBB Anomaly:**
Applying an industrial-strength partitioning library designed for millions of heap-allocated elements to a microscopic, 4KB stack-resident array introduced massive administrative overhead. Furthermore, it exposed a type-casting anomaly within the backend's horizontal reduction tree, leading to garbage bits corrupting the accumulator and resulting in incorrect Pi calculations.

**The Solution: Compiler Auto-Vectorization**
We stripped away the TBB abstraction in favor of a raw C++ `for` loop. When compiled with `-O3`, the compiler's Cost Model automatically unrolls and vectorizes this loop. 

**Portability and Pointer Aliasing:**
Remarkably, we achieved this without writing a single hardware-specific intrinsic or using compiler-specific `__restrict__` extensions. Compilers often refuse to vectorize loops due to "Pointer Aliasing" (the fear that arrays might overlap in memory). However, because our accumulator is a standalone float variable, the static analyzer can prove it cannot overlap with a 4KB `std::array`. Thus, it safely and aggressively vectorizes the loop, making this architecture 100% portable across Intel AVX, ARM NEON, or Apple Silicon architectures.

## The Execution Flow

1. **Task Submission:** The main thread enqueues a massive chunk of work (e.g., 100,000 samples) to the `ThreadPool`.
2. **Worker Awakening:** A worker thread pulls the task from the queue and enters the payload function.
3. **Stack Initialization:** The worker allocates two 4KB `std::array` buffers on its local stack for X and Y coordinates, perfectly aligned for AVX.
4. **Sequential Batching:** The worker enters a `while` loop, processing the 100,000 samples in bite-sized batches of 1024. It uses the Mersenne Twister to sequentially fill the X and Y arrays with random floats.
5. **Auto-Vectorized Evaluation:** The worker iterates through the arrays using a raw `for` loop. Under `-O3` optimization, the CPU loads the X and Y arrays into its wide vector registers, simultaneously calculating multiple data points per clock cycle, and summing the points that fall inside the circle.
6. **Accumulation & Return:** The batch results are accumulated. Once all 100,000 samples are processed, the total is seamlessly returned to the main thread via the `std::future` channel.