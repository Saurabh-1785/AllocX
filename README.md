# AllocX - High-Performance Memory Allocator

Custom memory allocation library implementing specialized allocators for performance-critical systems. Designed for game engines, HFT systems, and real-time applications where standard `malloc`/`new` introduce unacceptable latency and fragmentation.

## Features

- **Stack Allocator**: O(1) linear allocation with bulk deallocation (frame-scope allocations)
- **Pool Allocator**: O(1) fixed-size object pools with zero fragmentation
- **Free-List Allocator**: Variable-size allocations with coalescing for fragmentation control
- **STL Integration**: Custom allocator adapters for `std::vector`, `std::list`, `std::map`, etc.
- **Thread Safety**: Mutex-based thread-safe wrapper
- **Comprehensive Benchmarks**: Latency, throughput analysis vs malloc/new

## Performance

| Allocator | Allocation | Deallocation | Use Case |
|-----------|-----------|--------------|----------|
| Stack | ~5-10ns | N/A (bulk reset) | Per-frame allocations |
| Pool | ~10-20ns | ~10-20ns | Fixed-size objects |
| Free-List | ~20-100ns | ~20-50ns | Variable sizes |
| malloc | ~50-200ns | ~50-200ns | General purpose |

**Expected speedup: 5-20x faster** than malloc for specialized patterns.

## Building

```bash
mkdir build && cd build
cmake ..
make          # or cmake --build .
```

## Running

```bash
# Run tests
./allocx_tests

# Run benchmarks
./allocx_benchmark

# Run examples
./basic_usage
./stl_integration
```

## Quick Start

### Stack Allocator (Per-Frame Allocations)

```cpp
#include "allocx/stack_allocator.hpp"

allocx::StackAllocator frame(1024 * 1024);  // 1MB

// Allocate temporary data
auto marker = frame.get_marker();
int* data = (int*)frame.allocate(100 * sizeof(int));
// ... use data ...
frame.rollback(marker);  // Or frame.reset() for full reset
```

### Pool Allocator (Object Pools)

```cpp
#include "allocx/pool_allocator.hpp"

allocx::PoolAllocator pool(sizeof(Particle), 10000);

Particle* p = (Particle*)pool.allocate();
// ... use particle ...
pool.deallocate(p);  // O(1) return to pool
```

### Free-List Allocator (Variable Sizes)

```cpp
#include "allocx/freelist_allocator.hpp"

allocx::FreeListAllocator alloc(64 * 1024);

void* small = alloc.allocate(32);
void* large = alloc.allocate(1024);
alloc.deallocate(small);
alloc.deallocate(large);
```

### STL Integration

```cpp
#include "allocx/stl_adapter.hpp"
#include "allocx/freelist_allocator.hpp"
#include <vector>

allocx::FreeListAllocator alloc(256 * 1024);
allocx::STLAdapter<int, allocx::FreeListAllocator> adapter(alloc);

std::vector<int, decltype(adapter)> vec(adapter);
vec.push_back(42);  // Uses custom allocator
```

### Thread Safety

```cpp
#include "allocx/thread_safe.hpp"

allocx::PoolAllocator pool(64, 1000);
allocx::ThreadSafeAllocator<allocx::PoolAllocator> safe(pool);

// Safe to use from multiple threads
void* ptr = safe.allocate();
safe.deallocate(ptr);
```

## When to Use Each Allocator

| Pattern | Allocator | Why |
|---------|-----------|-----|
| Per-frame game data | Stack | Bulk reset, zero overhead |
| Particles, bullets | Pool | Same size, high churn |
| Network packets | Pool | Fixed buffer sizes |
| General subsystem | Free-List | Flexibility needed |
| Parser temporaries | Stack | Scoped lifetime |

## Project Structure

```
AllocX/
├── include/allocx/
│   ├── allocator_base.hpp    # Abstract interface
│   ├── utils.hpp             # Alignment utilities
│   ├── stack_allocator.hpp   # LIFO allocator
│   ├── pool_allocator.hpp    # Fixed-size pool
│   ├── freelist_allocator.hpp # Variable-size
│   ├── stl_adapter.hpp       # STL compatibility
│   └── thread_safe.hpp       # Thread-safe wrapper
├── src/                      # Implementation files
├── benchmarks/               # Performance benchmarks
├── tests/                    # Unit tests
└── examples/                 # Usage examples
```
