# Custom High-Performance Memory Allocator in C++

## Complete Project Specification & Implementation Guide

---

## 1. Project Overview & Motivation

### Why Standard new/delete Are Suboptimal

Standard `new` and `delete` use the general-purpose heap allocator (typically `malloc`/`free` underneath). While correct, they’re optimized for the average case across all possible allocation patterns. This creates several problems:

**Unpredictable Latency**: Each allocation involves system calls, lock contention, and complex bookkeeping. In a game running at 60 FPS, you have 16ms per frame—a single malloc can take 100+ microseconds.

**Memory Fragmentation**: General allocators fragment over time. Your process might have 100MB allocated but only 80MB usable because free blocks are scattered.

**Cache Inefficiency**: Heap-allocated objects land anywhere in memory. Accessing scattered objects thrashes CPU caches, destroying performance.

**Overhead**: Every allocation carries metadata (typically 16-32 bytes). Allocating thousands of small objects wastes significant memory.

**Non-Determinism**: Performance varies wildly based on heap state. This is unacceptable in real-time systems.

### Real-World Domains Requiring Custom Allocators

**Game Engines**: Allocate thousands of entities per frame. Use frame allocators that bulk-deallocate everything at once. Uncharted 4 uses 8+ specialized allocators.

**High-Frequency Trading**: Nanoseconds matter. Pre-allocate pools for order objects. Zero allocations in the critical path. A 1μs allocation is unacceptable.

**Embedded Systems**: Limited RAM with no OS heap. All memory pre-allocated at boot. Predictable behavior is mandatory.

**Real-Time Simulations**: Physics engines, robotics control systems. Strict timing guarantees required. RTOS environments prohibit dynamic allocation during runtime.

**Network Servers**: Handle 100K+ concurrent connections. Pool packet buffers to avoid allocation storms.

### What “High-Performance” Means

**Low Latency**: Allocations complete in predictable, minimal time (O(1) for pools).

**Predictability**: No worst-case scenarios. Every allocation takes similar time.

**Fragmentation Control**: Reuse memory efficiently without leaving gaps.

**Cache Locality**: Keep related objects contiguous in memory.

**Zero Runtime Allocation**: In critical sections, allocate nothing—use pre-allocated pools.

---

## 2. Problem Statement (Engineering Perspective)

### Core Problems Being Solved

**Problem 1: Allocation Pattern Mismatch**
General allocators handle arbitrary allocation patterns. But most code has predictable patterns:
- Game entities: allocate at start, deallocate at end (frame scope)
- Particle systems: thousands of fixed-size objects
- Network packets: same-size buffers allocated/freed constantly

Using a general allocator for these is like using a swiss army knife when you need a scalpel.

**Problem 2: Temporal Locality Waste**
Objects allocated near the same time often have similar lifetimes. General allocators don’t exploit this—they interleave short-lived and long-lived objects, creating fragmentation.

**Problem 3: Size-Class Overhead**
Small allocations (8-64 bytes) waste proportionally more memory on metadata. A 16-byte allocation might actually use 48 bytes (metadata + alignment).

**Problem 4: Synchronization Cost**
Global heap requires locks. Every thread contends for the same allocator. This serializes allocation in multi-threaded systems.

### Common Allocation Patterns

**Pattern 1: Scoped/Temporary Allocations**
Allocate many objects, use them briefly, deallocate all at once. Example: per-frame game allocations, parser temporaries.

**Pattern 2: Object Pools**
Fixed number of same-sized objects. Allocate/free constantly but total count stable. Example: bullet objects, network packets.

**Pattern 3: Persistent Allocations**
Allocated at startup, deallocated at shutdown. Example: loaded assets, lookup tables.

**Pattern 4: Streaming Allocations**
Sequential allocations, processed in order, deallocated in order. Example: audio buffers, log entries.

General allocators optimize for none of these specifically.

### Performance & Correctness Goals

**Performance Goals**:
- Stack allocator: O(1) allocation, bulk deallocation in O(1)
- Pool allocator: O(1) allocation and deallocation, zero fragmentation
- Free-list allocator: O(1) to O(n) allocation depending on strategy, controllable fragmentation

**Correctness Goals**:
- No memory leaks
- No double frees
- Proper alignment guarantees
- Bounds checking in debug builds
- Thread safety where required
- No undefined behavior in pointer arithmetic

---

## 3. Allocator Types to Implement

### Stack Allocator (Linear Allocator)

**When to Use**:
Per-frame allocations in games, temporary parser data, any allocations with strict LIFO deallocation order.

**How It Works**:
Pre-allocate a large contiguous block. Maintain a single offset pointer. Allocating moves the pointer forward. Deallocating moves it backward. Bulk-reset drops offset to zero.

```
Initial state:
[--------- 1MB Block ---------]
 ^
 offset = 0

After allocating 100 bytes:
[XXX-----------------]
    ^
    offset = 100

After allocating another 50 bytes:
[XXXYYY--------------]
       ^
       offset = 150

After bulk reset:
[---------------------]
 ^
 offset = 0
```

**Time Complexity**:
- Allocation: O(1) - increment pointer
- Deallocation (individual): O(1) - decrement pointer (must be LIFO)
- Bulk reset: O(1) - set offset to zero

**Trade-offs**:
- ✅ Blazing fast: just pointer arithmetic
- ✅ Perfect cache locality
- ✅ Zero fragmentation
- ✅ Minimal metadata overhead
- ❌ Strict LIFO deallocation required (or bulk reset)
- ❌ Can’t free individual objects mid-frame
- ❌ Can waste space if max usage is overestimated

**Implementation Details**:

```cpp
class StackAllocator {
    void* m_memory;      // Base pointer to block
    size_t m_size;       // Total size
    size_t m_offset;     // Current allocation offset

public:
    void* allocate(size_t size, size_t alignment) {
        // Calculate aligned offset
        size_t padding = (alignment - (m_offset % alignment)) % alignment;
        size_t aligned_offset = m_offset + padding;

        // Check bounds
        if (aligned_offset + size > m_size) return nullptr;

        void* ptr = static_cast<char*>(m_memory) + aligned_offset;
        m_offset = aligned_offset + size;
        return ptr;
    }

    void reset() { m_offset = 0; }
};
```

**Common Pitfalls**:
- Forgetting to reset before next frame causes memory exhaustion
- Deallocating out of order corrupts the stack
- Pointer arithmetic without proper casting causes undefined behavior

---

### Pool Allocator (Fixed-Size Object Pool)

**When to Use**:
Same-sized objects with high allocation/deallocation frequency. Game entities, particles, network packets, small object optimization.

**How It Works**:
Pre-allocate array of fixed-size chunks. Maintain free-list of available chunks using intrusive linked list. Store next-pointer in the free chunk itself. Allocation pops from list. Deallocation pushes to list.

```
Initial pool (4 chunks of 32 bytes):
[Chunk0] -> [Chunk1] -> [Chunk2] -> [Chunk3] -> NULL
 ^
 free_list

After allocating chunk:
[Chunk0(allocated)] [Chunk1] -> [Chunk2] -> [Chunk3] -> NULL
                     ^
                     free_list

After deallocating chunk0:
[Chunk0] -> [Chunk1] -> [Chunk2] -> [Chunk3] -> NULL
 ^
 free_list
```

**Time Complexity**:
- Allocation: O(1) - pop from free list
- Deallocation: O(1) - push to free list
- No fragmentation possible (all blocks same size)

**Trade-offs**:
- ✅ Constant-time operations
- ✅ Zero external fragmentation
- ✅ Excellent cache locality when objects accessed sequentially
- ✅ Minimal metadata (just free-list pointers in unused chunks)
- ❌ Only one object size per pool
- ❌ Wastes space if objects smaller than pointer size
- ❌ Fixed capacity (can chain pools if needed)

**Implementation Details**:

```cpp
class PoolAllocator {
    void* m_memory;
    size_t m_chunk_size;
    size_t m_chunk_count;
    void** m_free_list;  // Points to first free chunk

public:
    PoolAllocator(size_t chunk_size, size_t count)
        : m_chunk_size(chunk_size), m_chunk_count(count) {
        m_memory = ::operator new(chunk_size * count);

        // Initialize free list (intrusive)
        m_free_list = static_cast<void**>(m_memory);
        char* chunk = static_cast<char*>(m_memory);

        for (size_t i = 0; i < count - 1; ++i) {
            void** current = reinterpret_cast<void**>(chunk + i * chunk_size);
            *current = chunk + (i + 1) * chunk_size;
        }

        // Last chunk points to null
        void** last = reinterpret_cast<void**>(chunk + (count - 1) * chunk_size);
        *last = nullptr;
    }

    void* allocate() {
        if (!m_free_list) return nullptr;

        void* ptr = m_free_list;
        m_free_list = *static_cast<void**>(m_free_list);
        return ptr;
    }

    void deallocate(void* ptr) {
        *static_cast<void**>(ptr) = m_free_list;
        m_free_list = static_cast<void**>(ptr);
    }
};
```

**Common Pitfalls**:
- Chunk size smaller than `sizeof(void*)` breaks intrusive list
- Deallocating pointer not from this pool causes corruption
- Accessing deallocated chunk corrupts free-list
- Not aligning chunks properly causes crashes on some architectures

---

### Free-List Allocator (Variable-Size Blocks)

**When to Use**:
Variable-sized allocations with unpredictable lifetimes. General-purpose allocator for subsystems. More flexible than pools but less optimal.

**How It Works**:
Maintain linked list of free blocks with size metadata. On allocation, search for suitable block (first-fit, best-fit, or worst-fit). Split block if too large. On deallocation, return block to list and coalesce adjacent free blocks to reduce fragmentation.

```
Memory layout with headers:
[Header|Data][Header|Data][Header|Data]...

Header structure:
- size: size of data block
- next: pointer to next free block (if free)
- is_free: flag indicating status

Free list after several operations:
[Used|40B][Free|100B] -> [Free|80B] -> NULL
          ^
          free_list

After allocating 60B (using first-fit):
[Used|40B][Used|60B][Free|40B] -> [Free|80B] -> NULL
                     ^
                     free_list (split remaining)
```

**Time Complexity**:
- Allocation: O(n) worst case (scan free list)
- Deallocation: O(1) to O(n) depending on coalescing strategy
- Fragmentation: moderate, mitigated by coalescing

**Allocation Strategies**:

**First-Fit**: Use first block large enough. Fast but can fragment.

**Best-Fit**: Use smallest block that fits. Reduces waste but slow and creates tiny unusable blocks.

**Worst-Fit**: Use largest block. Counterintuitive but keeps remaining blocks large and usable.

**Trade-offs**:
- ✅ Handles variable sizes
- ✅ More flexible than pools
- ✅ Coalescing reduces fragmentation
- ❌ Slower than stack or pool
- ❌ Requires metadata per block
- ❌ Fragmentation still possible despite coalescing
- ❌ Searching free list is not cache-friendly

**Implementation Details**:

```cpp
struct BlockHeader {
    size_t size;
    BlockHeader* next;  // Next free block (if free)
    bool is_free;
};

class FreeListAllocator {
    void* m_memory;
    size_t m_size;
    BlockHeader* m_free_list;

    static constexpr size_t HEADER_SIZE = sizeof(BlockHeader);

public:
    void* allocate(size_t size) {
        BlockHeader* prev = nullptr;
        BlockHeader* current = m_free_list;

        // First-fit search
        while (current) {
            if (current->size >= size) {
                // Found suitable block

                // Should we split?
                if (current->size >= size + HEADER_SIZE + 8) {
                    // Split block
                    BlockHeader* new_block = reinterpret_cast<BlockHeader*>(
                        reinterpret_cast<char*>(current) + HEADER_SIZE + size
                    );
                    new_block->size = current->size - size - HEADER_SIZE;
                    new_block->is_free = true;
                    new_block->next = current->next;

                    current->size = size;
                    current->next = new_block;
                }

                // Remove from free list
                if (prev) prev->next = current->next;
                else m_free_list = current->next;

                current->is_free = false;
                return reinterpret_cast<char*>(current) + HEADER_SIZE;
            }
            prev = current;
            current = current->next;
        }

        return nullptr;  // Out of memory
    }

    void deallocate(void* ptr) {
        if (!ptr) return;

        BlockHeader* block = reinterpret_cast<BlockHeader*>(
            static_cast<char*>(ptr) - HEADER_SIZE
        );
        block->is_free = true;

        // Coalesce with next block if free
        BlockHeader* next = reinterpret_cast<BlockHeader*>(
            reinterpret_cast<char*>(block) + HEADER_SIZE + block->size
        );

        if (next->is_free) {
            block->size += HEADER_SIZE + next->size;
            block->next = next->next;
        }

        // Add to free list
        block->next = m_free_list;
        m_free_list = block;

        //TODO: Coalesce with previous block (requires back-pointer or scan)
    }
};
```

**Common Pitfalls**:
- Not aligning block sizes causes unaligned allocations
- Forgetting to coalesce creates permanent fragmentation
- Off-by-one errors in splitting logic corrupt adjacent blocks
- Not validating pointers in deallocate causes crashes

---

## 4. Memory Alignment & Correctness

### Why Alignment Matters

**Hardware Requirements**: CPUs require certain types to align to specific boundaries:
- `char`: 1-byte aligned (any address)
- `short`: 2-byte aligned (addresses divisible by 2)
- `int`: 4-byte aligned (addresses divisible by 4)
- `double`: 8-byte aligned (addresses divisible by 8)
- SIMD types (`__m128`): 16-byte aligned

**What Happens With Misalignment**:

On x86/x64: Unaligned access works but is slower (multiple memory transactions, cache line splits).

On ARM: Unaligned access triggers hardware exception and crashes (or kernel intervention with massive slowdown).

On some embedded systems: Undefined behavior, silent corruption, or hang.

**Example**:

```cpp
char buffer[100];
int* ptr = reinterpret_cast<int*>(buffer + 1);  // Misaligned!
*ptr = 42;  // May crash on ARM, slow on x86
```

### Calculating Aligned Addresses

**Alignment Formula**:

```
aligned_address = (address + alignment - 1) & ~(alignment - 1)
```

**Or equivalently**:

```
padding = (alignment - (address % alignment)) % alignment
aligned_address = address + padding
```

**Example** (align to 16 bytes):
- address = 37
- padding = (16 - (37 % 16)) % 16 = (16 - 5) % 16 = 11
- aligned_address = 37 + 11 = 48 ✓

**Implementation**:

```cpp
inline size_t align_up(size_t value, size_t alignment) {
    return (value + alignment - 1) & ~(alignment - 1);
}

inline void* align_pointer(void* ptr, size_t alignment) {
    uintptr_t addr = reinterpret_cast<uintptr_t>(ptr);
    uintptr_t aligned = align_up(addr, alignment);
    return reinterpret_cast<void*>(aligned);
}
```

### Platform-Independent Alignment

**Use Standard Library** (C++11+):

```cpp
#include<cstddef>
constexpr size_t alignment = alignof(T);  // Get alignment requirement
alignas(16) char buffer[256];            // Declare with alignment
```

**Use** `std::align` **for pointer adjustment**:

```cpp
void* ptr = buffer;
size_t space = sizeof(buffer);
void* aligned_ptr = std::align(alignment, size, ptr, space);
```

**Query Maximum Alignment**:

```cpp
constexpr size_t max_align = alignof(std::max_align_t);  // Usually 8 or 16
```

### Storing Alignment Metadata

When allocating aligned memory, you need to store the offset so you can retrieve the original pointer during deallocation:

```cpp
struct AllocationHeader {
    size_t size;
    uint8_t offset;  // Padding bytes added for alignment
};

void* aligned_allocate(size_t size, size_t alignment) {
    size_t total_size = size + alignment + sizeof(AllocationHeader);
    void* raw_memory = ::operator new(total_size);

    uintptr_t raw_addr = reinterpret_cast<uintptr_t>(raw_memory);
    uintptr_t aligned_addr = align_up(raw_addr + sizeof(AllocationHeader), alignment);

    uint8_t offset = static_cast<uint8_t>(aligned_addr - raw_addr);

    AllocationHeader* header = reinterpret_cast<AllocationHeader*>(aligned_addr - sizeof(AllocationHeader));
    header->size = size;
    header->offset = offset;

    return reinterpret_cast<void*>(aligned_addr);
}

void aligned_deallocate(void* ptr) {
    AllocationHeader* header = reinterpret_cast<AllocationHeader*>(
        static_cast<char*>(ptr) - sizeof(AllocationHeader)
    );

    void* raw_memory = static_cast<char*>(ptr) - header->offset;
    ::operator delete(raw_memory);
}
```

---

## 5. Low-Level Implementation Details

### Pointer Arithmetic Techniques

**Rule 1: Only arithmetic on** `char*` **is well-defined**

The C++ standard guarantees `char`, `signed char`, and `unsigned char` have size 1 and can alias any type.

```cpp
void* memory = ...;

// CORRECT
char* ptr = static_cast<char*>(memory);
char* next = ptr + 100;

// WRONG (undefined behavior)
void* next = memory + 100;  // Won't compile
int* next = static_cast<int*>(memory) + 1;  // Advances sizeof(int) bytes, not 1!
```

**Rule 2: Use** `reinterpret_cast` **for pointer type punning**

```cpp
void* memory = ...;
int* int_ptr = reinterpret_cast<int*>(memory);  // View memory as int*
```

**Rule 3: Use** `uintptr_t` **for address arithmetic**

```cpp
#include<cstdint>

void* ptr = ...;
uintptr_t addr = reinterpret_cast<uintptr_t>(ptr);
addr += 128;
ptr = reinterpret_cast<void*>(addr);
```

### Managing Raw Memory Blocks

**Allocate Raw Memory**:

```cpp
// Option 1: Global operator new (just memory, no constructor)
void* memory = ::operator new(size);

// Option 2: malloc (C-style)
void* memory = std::malloc(size);

// Option 3: Aligned allocation (C++17)
void* memory = std::aligned_alloc(alignment, size);

// Option 4: OS-specific (mmap, VirtualAlloc)
```

**Deallocate Raw Memory**:

```cpp
::operator delete(memory);
std::free(memory);
```

**Placement new**: Construct object in pre-allocated memory:

```cpp
void* memory = allocator.allocate(sizeof(T));
T* obj = new (memory) T(args...);  // Placement new
obj->~T();  // Explicit destructor call
allocator.deallocate(memory);
```

### Avoiding Undefined Behavior

**Common UB Pitfalls**:

1. **Dereferencing null or invalid pointers**

```cpp
void* ptr = allocate();
if (!ptr) return;  // Always check
```

1. **Pointer arithmetic outside allocated object**

```cpp
char buffer[100];
char* end = buffer + 100;  // OK (one-past-end)
char* invalid = buffer + 101;  // UB!
```

1. **Strict aliasing violations**

```cpp
int x = 42;
float* f = reinterpret_cast<float*>(&x);
float y = *f;  // UB! (Use memcpy instead)
```

1. **Unaligned access**

```cpp
char buffer[10];
int* ptr = reinterpret_cast<int*>(buffer + 1);  // May be misaligned
*ptr = 42;  // UB on some platforms
```

**Safe Patterns**:

```cpp
// Type punning via memcpy (optimizes to direct access)
float as_float;
std::memcpy(&as_float, &int_value, sizeof(float));

// Pointer validation
if (ptr >= memory_start && ptr < memory_end) { /* safe */ }

// Alignment check
if (reinterpret_cast<uintptr_t>(ptr) % alignment == 0) { /* aligned */ }
```

### Metadata Storage Strategies

**Strategy 1: Header Prefix**
Store metadata immediately before allocated block.

```cpp
[Header][User Data]
```

Pros: Simple, O(1) access. Cons: Wastes space, complicates alignment.

**Strategy 2: External Mapping**
Store metadata in separate structure (hash map, array).

```cpp
std::unordered_map<void*, BlockInfo> metadata;
```

Pros: Clean separation, easy debugging. Cons: Allocation overhead, cache misses.

**Strategy 3: Intrusive Storage**
Store metadata in the allocated block itself (when free).

```cpp
struct FreeBlock {
    FreeBlock* next;  // Stored in the block's memory
};
```

Pros: Zero overhead. Cons: Only works for free blocks, minimum size constraints.

**Strategy 4: Bitsets for Pools**
Track allocation status with bits.

```cpp
std::bitset<1024> allocation_bitmap;  // 1 bit per chunk
```

Pros: Minimal memory, fast queries. Cons: Limited to fixed-size pools.

---

## 6. STL-Compatible Allocator Interface

### Required Allocator Interface (C++17)

To use custom allocators with STL containers:

```cpp
template <typename T>
class STLAdapter {
public:
    using value_type = T;

    // Required: allocation
    T* allocate(size_t n) {
        return static_cast<T*>(underlying_allocator.allocate(n * sizeof(T), alignof(T)));
    }

    // Required: deallocation
    void deallocate(T* ptr, size_t n) {
        underlying_allocator.deallocate(ptr, n * sizeof(T));
    }

    // Optional but recommended: equality
    bool operator==(const STLAdapter& other) const {
        return &underlying_allocator == &other.underlying_allocator;
    }

    bool operator!=(const STLAdapter& other) const {
        return !(*this == other);
    }

private:
    MyAllocator& underlying_allocator;
};
```

### Allocator Traits

C++11 introduced `std::allocator_traits` to provide default implementations:

```cpp
template <typename T>
struct MyAllocator {
    using value_type = T;
    using size_type = size_t;
    using difference_type = std::ptrdiff_t;
    using propagate_on_container_move_assignment = std::true_type;

    template <typename U>
    struct rebind {
        using other = MyAllocator<U>;
    };
};

// Usage by STL:
using Traits = std::allocator_traits<MyAllocator<int>>;
int* ptr = Traits::allocate(alloc, 10);
Traits::deallocate(alloc, ptr, 10);
```

### Stateful vs Stateless Allocators

**Stateless** (empty class):

```cpp
template <typename T>
struct PoolAllocatorSTL {
    T* allocate(size_t n) {
        return static_cast<T*>(global_pool.allocate());
    }
};
// Can be default-constructed anywhere
```

**Stateful** (holds reference/pointer):

```cpp
template <typename T>
struct PoolAllocatorSTL {
    PoolAllocator& pool;

    PoolAllocatorSTL(PoolAllocator& p) : pool(p) {}

    T* allocate(size_t n) {
        return static_cast<T*>(pool.allocate());
    }
};
```

### Using Custom Allocators with STL

```cpp
// Pool allocator for vectors of small objects
PoolAllocator particle_pool(sizeof(Particle), 1000);
PoolAllocatorSTL<Particle> stl_adapter(particle_pool);

std::vector<Particle, PoolAllocatorSTL<Particle>> particles(stl_adapter);
particles.push_back(Particle{});  // Uses pool allocation

// Stack allocator for temporary containers
StackAllocator frame_allocator(1024 * 1024);  // 1MB
StackAllocatorSTL<int> stl_frame(frame_allocator);

std::vector<int, StackAllocatorSTL<int>> temp_data(stl_frame);
// ... use temp_data ...
frame_allocator.reset();  // Bulk deallocate
```

---

## 7. Global Allocator Replacement

### Overloading Global operator new/delete

You can replace the global allocator for the entire program:

```cpp
void* operator new(size_t size) {
    void* ptr = my_custom_allocator.allocate(size);
    if (!ptr) throw std::bad_alloc();
    return ptr;
}

void operator delete(void* ptr) noexcept {
    my_custom_allocator.deallocate(ptr);
}

void* operator new[](size_t size) {
    return ::operator new(size);  // Reuse single-object version
}

void operator delete[](void* ptr) noexcept {
    ::operator delete(ptr);
}
```

### Aligned Allocation (C++17)

```cpp
void* operator new(size_t size, std::align_val_t alignment) {
    return my_custom_allocator.allocate(size, static_cast<size_t>(alignment));
}

void operator delete(void* ptr, std::align_val_t alignment) noexcept {
    my_custom_allocator.deallocate(ptr);
}
```

### Placement new (Don’t overload)

Never overload placement new—it’s used for constructing objects in existing memory:

```cpp
void* operator new(size_t, void* ptr) noexcept {  // DON'T DO THIS
    return ptr;
}
```

### Risks & Safeguards

**Risk 1: Static Initialization Order Fiasco**
Global `new` might be called before your allocator is constructed.

**Solution**: Use Meyers singleton:

```cpp
MyAllocator& get_allocator() {
    static MyAllocator allocator;  // Guaranteed initialized on first use
    return allocator;
}

void* operator new(size_t size) {
    return get_allocator().allocate(size);
}
```

**Risk 2: Mixing Allocators**
Object allocated with one allocator freed with another = corruption.

**Solution**: Tag allocations or use per-module allocators.

**Risk 3: Standard Library Allocations**
STL, iostreams, exceptions all use `new`. Your allocator must handle everything.

**Solution**: Ensure your allocator is robust, or only override in controlled contexts.

**Risk 4: Throwing During Termination**
Throwing `std::bad_alloc` during shutdown can cause termination.

**Solution**: Make delete noexcept, handle OOM gracefully.

### Toggling Between Allocators

Use preprocessor or runtime flag:

```cpp
#ifdef USE_CUSTOM_ALLOCATOR
void* operator new(size_t size) {
    return custom_allocator.allocate(size);
}
#endif
```

Or runtime:

```cpp
inline bool use_custom = false;

void* operator new(size_t size) {
    if (use_custom) return custom_allocator.allocate(size);
    return std::malloc(size);
}
```

---

## 8. Thread Safety & Concurrency

### Why Thread Safety Matters

In multi-threaded programs, multiple threads allocating simultaneously without synchronization causes:

**Race Conditions**: Two threads read `free_list`, both think same block is free, both allocate it, corruption ensues.

**ABA Problem**: Thread A reads pointer, gets preempted, thread B frees and reallocates same address, thread A resumes with stale pointer.

**Memory Visibility**: Thread A frees memory, thread B allocates, but cached view of free list is stale.

### Approach 1: Coarse-Grained Locking (Mutex)

Simplest approach: one mutex protects the entire allocator.

```cpp
class ThreadSafePoolAllocator {
    std::mutex m_mutex;
    PoolAllocator m_allocator;

public:
    void* allocate() {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_allocator.allocate();
    }

    void deallocate(void* ptr) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_allocator.deallocate(ptr);
    }
};
```

**Pros**: Simple, correct, no ABA problem.
**Cons**: Serializes all allocations. Lock contention under high concurrency. Not real-time safe (locks have unbounded wait).

### Approach 2: Fine-Grained Locking

Lock smaller granularities (per-chunk, per-freelist-segment).

```cpp
class FineGrainedAllocator {
    static constexpr size_t NUM_SEGMENTS = 16;
    std::array<std::mutex, NUM_SEGMENTS> m_locks;
    std::array<void*, NUM_SEGMENTS> m_free_lists;

    size_t get_segment(void* ptr) {
        return (reinterpret_cast<uintptr_t>(ptr) >> 12) % NUM_SEGMENTS;
    }

public:
    void* allocate(size_t size) {
        size_t seg = hash(size) % NUM_SEGMENTS;
        std::lock_guard<std::mutex> lock(m_locks[seg]);
        // Allocate from segment...
    }
};
```

**Pros**: Better concurrency. **Cons**: More complex, still has lock overhead.

### Approach 3: Lock-Free (Advanced)

Use atomic operations on free lists.

```cpp
class LockFreePool {
    struct Node {
        std::atomic<Node*> next;
    };

    std::atomic<Node*> m_free_list;

public:
    void* allocate() {
        Node* old_head = m_free_list.load(std::memory_order_acquire);
        Node* new_head;

        do {
            if (!old_head) return nullptr;
            new_head = old_head->next.load(std::memory_order_relaxed);
        } while (!m_free_list.compare_exchange_weak(
            old_head, new_head,
            std::memory_order_release,
            std::memory_order_acquire
        ));

        return old_head;
    }

    void deallocate(void* ptr) {
        Node* node = static_cast<Node*>(ptr);
        Node* old_head = m_free_list.load(std::memory_order_relaxed);

        do {
            node->next.store(old_head, std::memory_order_relaxed);
        } while (!m_free_list.compare_exchange_weak(
            old_head, node,
            std::memory_order_release,
            std::memory_order_acquire
        ));
    }
};
```

**Pros**: No lock contention, scales perfectly. **Cons**: Complex, ABA problem requires tagged pointers or hazard pointers. Hard to debug.

### Approach 4: Thread-Local Allocators

Give each thread its own allocator instance.

```cpp
thread_local PoolAllocator t_allocator(64, 1000);

void* allocate() {
    return t_allocator.allocate();  // No synchronization needed!
}
```

**Pros**: Zero synchronization overhead, perfect scaling. **Cons**: Can’t share allocations between threads. Higher memory usage (one pool per thread).

### Recommended Strategy for Portfolio Project

**Use mutex-based locking** for correctness and simplicity. Document the trade-offs. If you have time, implement a thread-local version as an optimization.

```cpp
// Simple but correct
class ThreadSafeAllocator {
    std::mutex m_mutex;
    MyAllocator m_alloc;

public:
    void* allocate(size_t size, size_t align) {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_alloc.allocate(size, align);
    }

    void deallocate(void* ptr, size_t size) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_alloc.deallocate(ptr, size);
    }
};
```

Document: “Uses coarse-grained locking for correctness. Production systems use lock-free algorithms or thread-local storage for higher concurrency.”

---

## 9. Benchmarking & Performance Evaluation

### Metrics to Measure

**1. Allocation Latency**: Time to complete single allocation.
**2. Deallocation Latency**: Time to complete single deallocation.
**3. Throughput**: Operations per second.
**4. Memory Fragmentation**: Ratio of used vs allocated memory.
**5. Memory Overhead**: Metadata size vs useful data.
**6. Cache Efficiency**: Cache misses per operation (use perf tools).

### Benchmark Design

```cpp
struct BenchmarkResult {
    double avg_alloc_ns;
    double avg_dealloc_ns;
    double p50_alloc_ns;
    double p99_alloc_ns;
    size_t total_allocated;
    size_t total_wasted;
};

BenchmarkResult benchmark_allocator(Allocator& alloc, const Config& config) {
    constexpr size_t WARMUP = 1000;
    constexpr size_t ITERATIONS = 10000;

    // Warm up (fill caches, TLB)
    for (size_t i = 0; i < WARMUP; ++i) {
        void* ptr = alloc.allocate(config.size);
        alloc.deallocate(ptr);
    }

    std::vector<uint64_t> alloc_times;
    alloc_times.reserve(ITERATIONS);

    for (size_t i = 0; i < ITERATIONS; ++i) {
        auto start = std::chrono::high_resolution_clock::now();
        void* ptr = alloc.allocate(config.size);
        auto end = std::chrono::high_resolution_clock::now();

        alloc_times.push_back(
            std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count()
        );

        alloc.deallocate(ptr);
    }

    // Calculate statistics
    std::sort(alloc_times.begin(), alloc_times.end());

    BenchmarkResult result;
    result.avg_alloc_ns = std::accumulate(alloc_times.begin(), alloc_times.end(), 0.0) / ITERATIONS;
    result.p50_alloc_ns = alloc_times[ITERATIONS / 2];
    result.p99_alloc_ns = alloc_times[ITERATIONS * 99 / 100];

    return result;
}
```

### Realistic Workload Patterns

**Pattern 1: Burst Allocation**
Allocate many objects, use briefly, deallocate all.

```cpp
void benchmark_burst(Allocator& alloc) {
    constexpr size_t BURST_SIZE = 1000;
    std::vector<void*> ptrs;

    auto start = now();
    for (size_t i = 0; i < BURST_SIZE; ++i) {
        ptrs.push_back(alloc.allocate(64));
    }
    auto alloc_time = now() - start;

    start = now();
    for (void* ptr : ptrs) {
        alloc.deallocate(ptr);
    }
    auto dealloc_time = now() - start;
}
```

**Pattern 2: Churn**
Allocate and deallocate randomly.

```cpp
void benchmark_churn(Allocator& alloc) {
    std::vector<void*> active;
    std::mt19937 rng;

    for (size_t i = 0; i < 10000; ++i) {
        if (active.empty() || (active.size() < 100 && rng() % 2)) {
            active.push_back(alloc.allocate(64));
        } else {
            size_t idx = rng() % active.size();
            alloc.deallocate(active[idx]);
            active.erase(active.begin() + idx);
        }
    }
}
```

**Pattern 3: Mixed Sizes**
Allocate variable sizes like real applications.

```cpp
size_t get_realistic_size(std::mt19937& rng) {
    // Most allocations small, few large (power-law distribution)
    double r = std::uniform_real_distribution<>(0, 1)(rng);
    if (r < 0.7) return 16 + rng() % 48;      // 70%: 16-64 bytes
    if (r < 0.9) return 64 + rng() % 192;     // 20%: 64-256 bytes
    return 256 + rng() % 768;                  // 10%: 256-1024 bytes
}
```

### Measuring Fragmentation

```cpp
double calculate_fragmentation(Allocator& alloc) {
    size_t total_requested = 0;
    size_t total_committed = 0;

    // Allocate many blocks
    std::vector<std::pair<void*, size_t>> allocations;
    for (size_t size : {16, 32, 48, 64, 128, 256}) {
        for (size_t i = 0; i < 100; ++i) {
            void* ptr = alloc.allocate(size);
            allocations.push_back({ptr, size});
            total_requested += size;
        }
    }

    // Deallocate random blocks
    std::shuffle(allocations.begin(), allocations.end(), rng);
    for (size_t i = 0; i < allocations.size() / 2; ++i) {
        alloc.deallocate(allocations[i].first);
    }

    total_committed = alloc.get_total_memory();  // Must implement

    return 1.0 - (static_cast<double>(total_requested) / total_committed);
}
```

### Comparing Against malloc/new

```cpp
void compare_allocators() {
    PoolAllocator pool(64, 1000);

    // Benchmark pool
    auto pool_result = benchmark_allocator(pool, {.size = 64});

    // Benchmark malloc
    auto malloc_result = benchmark_malloc({.size = 64});

    std::cout << "Pool allocator: " << pool_result.avg_alloc_ns << " ns\n";
    std::cout << "malloc:         " << malloc_result.avg_alloc_ns << " ns\n";
    std::cout << "Speedup:        " << (malloc_result.avg_alloc_ns / pool_result.avg_alloc_ns) << "x\n";
}
```

Expected results:
- Pool allocator: 5-20 ns
- Stack allocator: 2-10 ns
- malloc: 50-200 ns
- Custom allocator should be 5-20x faster for specialized patterns

### Noise Reduction Techniques

**1. Pin to CPU Core**: Prevent thread migration.

```cpp
cpu_set_t cpuset;
CPU_ZERO(&cpuset);
CPU_SET(0, &cpuset);
pthread_setaffinity_np(pthread_self(), sizeof(cpuset), &cpuset);
```

**2. Disable CPU Frequency Scaling**:

```bash
sudo cpupower frequency-set --governor performance
```

**3. Run Multiple Iterations**: Average out noise.

**4. Drop Outliers**: Ignore top/bottom 1% of measurements.

**5. Measure TSC Directly**: Use `rdtsc` for precise timing:

```cpp
inline uint64_t rdtsc() {
    uint32_t lo, hi;
    asm volatile("rdtsc" : "=a"(lo), "=d"(hi));
    return (static_cast<uint64_t>(hi) << 32) | lo;
}
```

---

## 10. Architecture & Module Breakdown

### Clean Project Structure

```
custom-allocator/
├── include/
│   └── allocator/
│       ├── allocator_base.hpp      # Abstract interface
│       ├── stack_allocator.hpp
│       ├── pool_allocator.hpp
│       ├── freelist_allocator.hpp
│       ├── stl_adapter.hpp         # STL compatibility
│       └── utils.hpp               # Alignment, math utilities
├── src/
│   ├── stack_allocator.cpp
│   ├── pool_allocator.cpp
│   └── freelist_allocator.cpp
├── benchmarks/
│   ├── benchmark_main.cpp
│   ├── bench_pool.cpp
│   ├── bench_stack.cpp
│   └── workloads.hpp               # Realistic patterns
├── tests/
│   ├── test_pool.cpp
│   ├── test_stack.cpp
│   └── test_alignment.cpp
├── examples/
│   ├── game_frame_allocator.cpp
│   ├── stl_integration.cpp
│   └── object_pool_demo.cpp
└── CMakeLists.txt
```

### Module Responsibilities

**allocator_base.hpp**: Abstract interface ensuring consistency.

```cpp
class IAllocator {
public:
    virtual ~IAllocator() = default;
    virtual void* allocate(size_t size, size_t alignment) = 0;
    virtual void deallocate(void* ptr, size_t size) = 0;
    virtual void reset() {}  // Optional bulk deallocation
};
```

**utils.hpp**: Shared utilities.

```cpp
namespace allocator_utils {
    size_t align_up(size_t value, size_t alignment);
    void* align_pointer(void* ptr, size_t alignment);
    bool is_aligned(void* ptr, size_t alignment);
    size_t align_size(size_t size, size_t alignment);
}
```

**stl_adapter.hpp**: Bridge to STL.

```cpp
template <typename T, typename Allocator>
class STLAllocatorAdapter {
    // Implementation from section 6
};
```

### Separation of Concerns

**Core Allocators**: No dependencies, pure allocation logic.

**Thread Safety**: Separate wrapper classes (`ThreadSafePoolAllocator`).

**Debugging**: Conditional compilation with guards, bounds checking, leak detection.

**Benchmarking**: Isolated from core, can be excluded from production builds.

**STL Integration**: Optional layer, doesn’t pollute core allocators.

### Evolution to Reusable Library

**Phase 1**: Single-header implementation for simplicity.

**Phase 2**: Split headers/implementation for compile-time optimization.

**Phase 3**: Add CMake, install targets, pkg-config.

**Phase 4**: Documentation with Doxygen.

**Phase 5**: CI/CD with sanitizers (ASan, UBSan, TSan).

---

## 11. Expected Learnings

### Low-Level Memory Control

- Understanding memory as contiguous byte arrays
- Managing lifetimes manually without RAII (in allocator internals)
- Trading safety for performance with explicit control
- Visualizing memory layouts and fragmentation

### Advanced Pointer Arithmetic

- Calculating offsets with `char*` arithmetic
- Converting between pointer types safely with `reinterpret_cast`
- Using `uintptr_t` for address calculations
- Understanding pointer aliasing rules and strict aliasing

### Operator Overloading

- Global `operator new`/`delete` for custom allocation
- Placement `new` for in-place construction
- Aligned allocation operators (C++17)
- When overloading is appropriate vs template alternatives

### Memory-Management Data Structures

- Intrusive linked lists for zero-overhead free lists
- Metadata headers with size and flags
- Bitmaps for tracking allocation status
- Coalescing strategies to combat fragmentation

### Performance Profiling & Benchmarking

- Using `std::chrono` for microsecond/nanosecond timing
- Statistical analysis of latency distributions (p50, p99)
- Identifying bottlenecks with profiling tools (perf, Instruments, VTune)
- Understanding cache effects on memory access patterns

### Engineering Trade-Offs

- **Speed vs Flexibility**: Stack allocators are fastest but most restrictive
- **Memory Overhead vs Complexity**: Simple headers waste space, external tracking adds indirection
- **Thread Safety vs Performance**: Locks serialize, lock-free is complex
- **Compile-Time vs Runtime**: Template metaprogramming vs virtual dispatch
- **Portability vs Optimization**: Standard C++ vs platform-specific intrinsics

---

## 12. Resume-Ready Project Description

### Resume Bullet Points

**Option 1** (Systems Focus):
> Engineered high-performance memory allocation system in C++ with stack, pool, and free-list allocators, achieving 10-20x speedup over malloc for specialized workloads through pointer arithmetic optimization and cache-aware data structure design.

**Option 2** (Performance Focus):
> Designed and benchmarked custom memory allocators (LIFO stack, fixed-size pool, variable-size free-list) optimized for game engine workloads, reducing per-frame allocation latency from 150μs to 8μs through contiguous memory layouts and O(1) allocation strategies.

### GitHub README Description

```markdown
# High-Performance Memory Allocators

Custom memory allocation library implementing specialized allocators for performance-critical systems.
Designed for game engines, HFT systems, and real-time applications where standard `malloc`/`new` introduce
unacceptable latency and fragmentation.

## Features

-**Stack Allocator**: O(1) linear allocation with bulk deallocation (frame-scope allocations)
-**Pool Allocator**: O(1) fixed-size object pools with zero fragmentation
-**Free-List Allocator**: Variable-size allocations with coalescing for fragmentation control
-**STL Integration**: Custom allocator adapters for `std::vector`, `std::unordered_map`, etc.
-**Thread Safety**: Mutex-based and thread-local implementations
-**Comprehensive Benchmarks**: Latency, throughput, fragmentation analysis vs malloc

## Performance

-Stack: 5-10ns allocation, bulk deallocation in O(1)
-Pool: 10-20ns allocation/deallocation, 5-15x faster than malloc for small objects
-Free-List: 20-50ns allocation, competitive with malloc but with lower fragmentation

## Use Cases

Game engines (per-frame allocations), high-frequency trading (order object pools),
embedded systems (static pre-allocation), physics simulations (predictable latency).
```

### Keywords for Technical SEO

Systems programming, memory management, C++ allocators, low-latency, cache optimization,
pointer arithmetic, performance engineering, real-time systems, game engine architecture,
HFT infrastructure, embedded systems, STL allocators, lock-free programming, memory fragmentation,
custom allocators, operator overloading, template metaprogramming, benchmarking, profiling.

---

## 13. Interview Talking Points

### Explaining the Project

**Opening** (30 seconds):
“I built a custom memory allocation library in C++ that optimizes for specific allocation patterns found in performance-critical systems like game engines. While malloc is general-purpose, specialized allocators can be 10-20x faster for workloads with predictable patterns. I implemented three allocator types: a stack allocator for frame-scoped allocations, a pool allocator for fixed-size objects, and a free-list allocator for variable sizes with fragmentation control.”

**Deep Dive** (2 minutes):
“The stack allocator pre-allocates a large block and maintains a single offset pointer. Allocating just increments the offset—it’s essentially pointer arithmetic, which takes a few nanoseconds. At the end of each frame, we bulk-reset the offset to zero. This is perfect for temporary data that only lives one frame.

The pool allocator manages fixed-size chunks using an intrusive free-list. Each free chunk stores a pointer to the next free chunk in its own memory. Allocation pops from the list, deallocation pushes back—both O(1) operations with zero fragmentation since all chunks are the same size.

The free-list allocator handles variable sizes. It maintains a linked list of free blocks with size metadata. On allocation, we search for a suitable block using first-fit, split it if it’s too large, and return it. On deallocation, we coalesce adjacent free blocks to prevent fragmentation. This is slower than the other two but more flexible.”

### Common Follow-Up Questions

**Q: How do you handle alignment?**
“All allocations must respect alignment requirements—for example, doubles need 8-byte alignment. I calculate the padding needed to align the address: `padding = (alignment - (address % alignment)) % alignment`. For pool allocators, I ensure the chunk size is a multiple of the required alignment. For variable-size allocators, I store the alignment padding in metadata so I can retrieve the original pointer during deallocation.”

**Q: How did you make it thread-safe?**
“I implemented two approaches. First, a simple mutex-based version that locks around allocate/deallocate. It’s correct but serializes access. Second, a thread-local version where each thread gets its own allocator instance, eliminating synchronization entirely. The trade-off is higher memory usage since each thread maintains its own pool. For a production system, I’d explore lock-free algorithms using atomic compare-and-swap, but that introduces complexity like the ABA problem.”

**Q: How do you measure performance?**
“I wrote a benchmarking suite that measures allocation latency, deallocation latency, throughput, and fragmentation. I warm up the cache with dummy allocations, then time operations using `std::chrono::high_resolution_clock` or `rdtsc` for nanosecond precision. I run thousands of iterations and calculate p50, p99 latencies to understand tail behavior. I compare against malloc to show speedup. For realistic testing, I use workload patterns like burst allocation (allocate 1000 objects, free all) and churn (random alloc/free mix).”

**Q: What’s the biggest challenge you faced?**
“Pointer arithmetic and avoiding undefined behavior. C++ is strict about when pointer arithmetic is valid—you can only do arithmetic on `char*` or within allocated objects. I had to be careful with type casting between `void*`, `char*`, and typed pointers using `reinterpret_cast`. Also, strict aliasing rules mean you can’t just cast an int pointer to float and dereference—you need `memcpy` for type punning. Debugging segfaults from misaligned pointers or off-by-one errors in splitting blocks was challenging but taught me to be meticulous.”

**Q: How does this integrate with existing code?**
“I implemented the standard STL allocator interface so custom allocators can be used with STL containers: `std::vector<Particle, PoolAllocatorAdapter<Particle>>`. I also provided global operator new/delete overloads so existing code automatically uses custom allocation, though this requires careful initialization order handling using a Meyers singleton to avoid the static initialization order fiasco.”

### Trade-Offs & Limitations to Discuss

**Acknowledge restrictions openly**:

“Stack allocators require strict LIFO deallocation or bulk reset. If you need to free objects individually and out of order, you need a pool or free-list.”

“Pool allocators only work for one size. If your objects vary in size, you need multiple pools or a free-list allocator.”

“Free-list allocators still fragment over time despite coalescing. After millions of allocations, you might need to defragment by copying live objects and compacting memory, which is expensive.”

“Thread-safe allocators either sacrifice performance with locks or sacrifice simplicity with lock-free algorithms. There’s no free lunch.”

**Show understanding of context**:

“This project targets scenarios where allocation patterns are known and predictable. For a general-purpose application with unpredictable allocations, the standard allocator’s sophistication is valuable. But in a game engine where you allocate thousands of particles per frame with known lifetimes, specialized allocators win.”

### Demonstrating Systems Thinking

“This project taught me that performance isn’t just about algorithms—it’s about understanding the hardware. Cache misses dominate latency, so keeping allocations contiguous matters. Fragmentation isn’t just wasted space—it’s scattered memory that thrashes the cache. Alignment isn’t just a correctness issue—misaligned loads can be orders of magnitude slower or crash on ARM. Real-world systems engineering requires thinking about the full stack from CPU caches to OS page tables.”

---

## 14. Implementation Constraints & Style

### Code Quality Standards

**Correctness First**: No memory leaks, no undefined behavior, proper RAII where appropriate.

**Const Correctness**: Mark everything that doesn’t modify state as `const`.

**Explicit Over Implicit**: Use explicit constructors, no implicit conversions.

**Modern C++**: Use C++17/20 features where appropriate (`std::byte`, `if constexpr`, structured bindings).

**No Exceptions in Allocators**: Return `nullptr` on failure, let caller decide how to handle.

**Document Assumptions**: Comment non-obvious invariants, alignment requirements, ordering constraints.

### Performance Guidelines

**Zero-Cost Abstractions**: Inline hot paths, avoid virtual dispatch in critical sections.

**Minimize Branching**: Cache prediction matters. Avoid conditionals in tight loops.

**Prefer Compile-Time**: Use templates and `constexpr` over runtime polymorphism where possible.

**Measure Don’t Guess**: Profile before optimizing. Don’t sacrifice readability for unmeasured “optimizations”.

### What to Avoid

**No UI**: This is a systems library, not an application. No GUI, no file I/O beyond benchmarks.

**No Over-Engineering**: Don’t implement features you don’t benchmark or test. Start simple, iterate.

**No Premature Generalization**: Implement three allocators well rather than a complex framework.

**No External Dependencies**: Keep it standard C++ so it’s portable and easy to evaluate.

### Emphasis Areas

**Correctness**: Valgrind/ASan clean, no leaks, no UB.

**Predictability**: Consistent performance, no surprises.

**Clarity**: Code should be readable. Comments explain *why*, not *what*.

**Performance**: Faster than malloc for specialized patterns, with data to prove it.

---

## Conclusion

This project demonstrates deep systems-level expertise: understanding how memory works at the byte level, optimizing for real hardware constraints (cache, alignment, fragmentation), and engineering solutions to concrete performance problems. By implementing, benchmarking, and articulating these allocators, you’re showing the kind of technical depth that elite teams (game studios, HFT firms, systems companies) value.

The key is not just building it, but understanding the *why*—why stack allocators are fast, why fragmentation matters, why alignment crashes on ARM—and being able to explain those trade-offs clearly in interviews.

Build it, benchmark it, break it, fix it, and you’ll have a portfolio piece that signals real engineering maturity.