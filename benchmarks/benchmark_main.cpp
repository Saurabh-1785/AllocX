#include <algorithm>
#include <chrono>
#include <cstring>
#include <iostream>
#include <numeric>
#include <random>
#include <vector>

#include "allocx/freelist_allocator.hpp"
#include "allocx/pool_allocator.hpp"
#include "allocx/stack_allocator.hpp"

using namespace allocx;
using Clock = std::chrono::high_resolution_clock;

// ============================================================================
// Benchmark Utilities
// ============================================================================

struct BenchmarkResult {
  double avg_ns;
  double p50_ns;
  double p99_ns;
  double min_ns;
  double max_ns;
};

template <typename Func>
BenchmarkResult run_benchmark(const char *name, size_t iterations,
                              Func &&func) {
  std::vector<double> times;
  times.reserve(iterations);

  // Warmup
  for (size_t i = 0; i < iterations / 10; ++i) {
    func();
  }

  // Actual benchmark
  for (size_t i = 0; i < iterations; ++i) {
    auto start = Clock::now();
    func();
    auto end = Clock::now();
    times.push_back(
        std::chrono::duration<double, std::nano>(end - start).count());
  }

  std::sort(times.begin(), times.end());

  BenchmarkResult result;
  result.avg_ns = std::accumulate(times.begin(), times.end(), 0.0) / iterations;
  result.p50_ns = times[iterations / 2];
  result.p99_ns = times[iterations * 99 / 100];
  result.min_ns = times.front();
  result.max_ns = times.back();

  std::cout << "  " << name << ":\n";
  std::cout << "    Avg: " << result.avg_ns << " ns\n";
  std::cout << "    P50: " << result.p50_ns << " ns\n";
  std::cout << "    P99: " << result.p99_ns << " ns\n";
  std::cout << "    Min: " << result.min_ns << " ns, Max: " << result.max_ns
            << " ns\n";

  return result;
}

// ============================================================================
// Stack Allocator Benchmarks
// ============================================================================

void benchmark_stack_allocator() {
  std::cout << "\n=== Stack Allocator Benchmarks ===\n";

  constexpr size_t POOL_SIZE = 1024 * 1024; // 1MB
  constexpr size_t ITERATIONS = 100000;
  constexpr size_t ALLOC_SIZE = 64;

  StackAllocator stack(POOL_SIZE);

  // Single allocation benchmark
  run_benchmark("Single Alloc (64B)", ITERATIONS, [&]() {
    void *ptr = stack.allocate(ALLOC_SIZE);
    (void)ptr;
    stack.reset();
  });

  // Burst allocation benchmark
  std::cout << "\n  Burst Alloc (1000 x 64B):\n";
  {
    auto start = Clock::now();
    for (size_t i = 0; i < 1000; ++i) {
      void *ptr = stack.allocate(ALLOC_SIZE);
      (void)ptr;
    }
    auto end = Clock::now();
    double total_ns =
        std::chrono::duration<double, std::nano>(end - start).count();
    std::cout << "    Total: " << total_ns / 1000 << " ns/alloc\n";
    stack.reset();
  }

  // Reset benchmark
  run_benchmark("Reset", ITERATIONS, [&]() { stack.reset(); });
}

// ============================================================================
// Pool Allocator Benchmarks
// ============================================================================

void benchmark_pool_allocator() {
  std::cout << "\n=== Pool Allocator Benchmarks ===\n";

  constexpr size_t CHUNK_SIZE = 64;
  constexpr size_t CHUNK_COUNT = 10000;
  constexpr size_t ITERATIONS = 100000;

  PoolAllocator pool(CHUNK_SIZE, CHUNK_COUNT);

  // Single alloc/dealloc benchmark
  run_benchmark("Alloc + Dealloc (64B)", ITERATIONS, [&]() {
    void *ptr = pool.allocate();
    pool.deallocate(ptr);
  });

  // Multiple alloc then dealloc
  std::cout << "\n  Burst Alloc + Dealloc (1000 chunks):\n";
  {
    std::vector<void *> ptrs;
    ptrs.reserve(1000);

    auto start = Clock::now();
    for (size_t i = 0; i < 1000; ++i) {
      ptrs.push_back(pool.allocate());
    }
    auto alloc_end = Clock::now();
    for (void *ptr : ptrs) {
      pool.deallocate(ptr);
    }
    auto dealloc_end = Clock::now();

    double alloc_ns =
        std::chrono::duration<double, std::nano>(alloc_end - start).count();
    double dealloc_ns =
        std::chrono::duration<double, std::nano>(dealloc_end - alloc_end)
            .count();
    std::cout << "    Alloc: " << alloc_ns / 1000 << " ns/op\n";
    std::cout << "    Dealloc: " << dealloc_ns / 1000 << " ns/op\n";
  }
}

// ============================================================================
// Free-List Allocator Benchmarks
// ============================================================================

void benchmark_freelist_allocator() {
  std::cout << "\n=== Free-List Allocator Benchmarks ===\n";

  constexpr size_t POOL_SIZE = 1024 * 1024; // 1MB
  constexpr size_t ITERATIONS = 10000;

  FreeListAllocator freelist(POOL_SIZE);

  // Single alloc/dealloc
  run_benchmark("Alloc + Dealloc (64B)", ITERATIONS, [&]() {
    void *ptr = freelist.allocate(64);
    freelist.deallocate(ptr);
  });

  // Variable size allocations
  std::cout << "\n  Variable Size Alloc (16B-256B):\n";
  {
    std::mt19937 rng(42);
    std::vector<void *> ptrs;
    ptrs.reserve(500);

    auto start = Clock::now();
    for (size_t i = 0; i < 500; ++i) {
      size_t size = 16 + (rng() % 240);
      ptrs.push_back(freelist.allocate(size));
    }
    auto alloc_end = Clock::now();

    for (void *ptr : ptrs) {
      freelist.deallocate(ptr);
    }
    auto dealloc_end = Clock::now();

    double alloc_ns =
        std::chrono::duration<double, std::nano>(alloc_end - start).count();
    double dealloc_ns =
        std::chrono::duration<double, std::nano>(dealloc_end - alloc_end)
            .count();
    std::cout << "    Alloc: " << alloc_ns / 500 << " ns/op\n";
    std::cout << "    Dealloc: " << dealloc_ns / 500 << " ns/op\n";
  }
}

// ============================================================================
// Comparison with malloc/new
// ============================================================================

void benchmark_malloc_comparison() {
  std::cout << "\n=== Comparison: Custom Allocators vs malloc ===\n";

  constexpr size_t ITERATIONS = 100000;
  constexpr size_t ALLOC_SIZE = 64;

  // malloc benchmark
  double malloc_avg = 0;
  {
    std::vector<double> times;
    times.reserve(ITERATIONS);

    for (size_t i = 0; i < ITERATIONS; ++i) {
      auto start = Clock::now();
      void *ptr = std::malloc(ALLOC_SIZE);
      auto end = Clock::now();
      std::free(ptr);
      times.push_back(
          std::chrono::duration<double, std::nano>(end - start).count());
    }
    std::sort(times.begin(), times.end());
    malloc_avg = std::accumulate(times.begin(), times.end(), 0.0) / ITERATIONS;
    std::cout << "  malloc (64B): Avg " << malloc_avg << " ns, P99 "
              << times[ITERATIONS * 99 / 100] << " ns\n";
  }

  // Pool allocator benchmark
  PoolAllocator pool(ALLOC_SIZE, ITERATIONS);
  double pool_avg = 0;
  {
    std::vector<double> times;
    times.reserve(ITERATIONS);

    for (size_t i = 0; i < ITERATIONS; ++i) {
      auto start = Clock::now();
      void *ptr = pool.allocate();
      auto end = Clock::now();
      pool.deallocate(ptr);
      times.push_back(
          std::chrono::duration<double, std::nano>(end - start).count());
    }
    std::sort(times.begin(), times.end());
    pool_avg = std::accumulate(times.begin(), times.end(), 0.0) / ITERATIONS;
    std::cout << "  PoolAllocator (64B): Avg " << pool_avg << " ns, P99 "
              << times[ITERATIONS * 99 / 100] << " ns\n";
  }

  // Stack allocator benchmark
  StackAllocator stack(ITERATIONS * ALLOC_SIZE);
  double stack_avg = 0;
  {
    std::vector<double> times;
    times.reserve(ITERATIONS);

    for (size_t i = 0; i < ITERATIONS; ++i) {
      auto start = Clock::now();
      void *ptr = stack.allocate(ALLOC_SIZE);
      (void)ptr;
      auto end = Clock::now();
      times.push_back(
          std::chrono::duration<double, std::nano>(end - start).count());
    }
    std::sort(times.begin(), times.end());
    stack_avg = std::accumulate(times.begin(), times.end(), 0.0) / ITERATIONS;
    std::cout << "  StackAllocator (64B): Avg " << stack_avg << " ns, P99 "
              << times[ITERATIONS * 99 / 100] << " ns\n";
    stack.reset();
  }

  std::cout << "\n  Speedup vs malloc:\n";
  std::cout << "    Pool: " << (malloc_avg / pool_avg) << "x\n";
  std::cout << "    Stack: " << (malloc_avg / stack_avg) << "x\n";
}

// ============================================================================
// Main
// ============================================================================

int main() {
  std::cout << "╔════════════════════════════════════════════════╗\n";
  std::cout << "║      AllocX - Memory Allocator Benchmarks      ║\n";
  std::cout << "╚════════════════════════════════════════════════╝\n";

  benchmark_stack_allocator();
  benchmark_pool_allocator();
  benchmark_freelist_allocator();
  benchmark_malloc_comparison();

  std::cout << "\n✓ Benchmarks completed.\n";
  return 0;
}
