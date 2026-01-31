#include <cassert>
#include <cstring>
#include <iostream>
#include <vector>

#include "allocx/freelist_allocator.hpp"
#include "allocx/pool_allocator.hpp"
#include "allocx/stack_allocator.hpp"
#include "allocx/utils.hpp"

using namespace allocx;

// ============================================================================
// Test Macros
// ============================================================================

#define TEST(name)                                                             \
  std::cout << "  Testing " << #name << "... ";                                \
  test_##name();                                                               \
  std::cout << "✓\n";

#define ASSERT(cond)                                                           \
  if (!(cond)) {                                                               \
    std::cerr << "FAILED: " << #cond << " at " << __FILE__ << ":" << __LINE__  \
              << "\n";                                                         \
    std::exit(1);                                                              \
  }

// ============================================================================
// Utils Tests
// ============================================================================

void test_align_up() {
  ASSERT(utils::align_up(0, 8) == 0);
  ASSERT(utils::align_up(1, 8) == 8);
  ASSERT(utils::align_up(7, 8) == 8);
  ASSERT(utils::align_up(8, 8) == 8);
  ASSERT(utils::align_up(9, 8) == 16);
  ASSERT(utils::align_up(15, 16) == 16);
  ASSERT(utils::align_up(16, 16) == 16);
  ASSERT(utils::align_up(17, 16) == 32);
}

void test_is_power_of_two() {
  ASSERT(utils::is_power_of_two(1));
  ASSERT(utils::is_power_of_two(2));
  ASSERT(utils::is_power_of_two(4));
  ASSERT(utils::is_power_of_two(8));
  ASSERT(utils::is_power_of_two(1024));
  ASSERT(!utils::is_power_of_two(0));
  ASSERT(!utils::is_power_of_two(3));
  ASSERT(!utils::is_power_of_two(6));
  ASSERT(!utils::is_power_of_two(100));
}

void test_calc_padding() {
  ASSERT(utils::calc_padding(0, 8) == 0);
  ASSERT(utils::calc_padding(1, 8) == 7);
  ASSERT(utils::calc_padding(7, 8) == 1);
  ASSERT(utils::calc_padding(8, 8) == 0);
  ASSERT(utils::calc_padding(9, 8) == 7);
}

// ============================================================================
// Stack Allocator Tests
// ============================================================================

void test_stack_basic_allocation() {
  StackAllocator alloc(1024);

  void *p1 = alloc.allocate(100);
  ASSERT(p1 != nullptr);
  ASSERT(alloc.owns(p1));
  ASSERT(alloc.used_size() >= 100);

  void *p2 = alloc.allocate(200);
  ASSERT(p2 != nullptr);
  ASSERT(p2 > p1); // Sequential allocation
  ASSERT(alloc.owns(p2));
}

void test_stack_alignment() {
  StackAllocator alloc(1024);

  void *p1 = alloc.allocate(1, 16);
  ASSERT(reinterpret_cast<uintptr_t>(p1) % 16 == 0);

  void *p2 = alloc.allocate(1, 32);
  ASSERT(reinterpret_cast<uintptr_t>(p2) % 32 == 0);

  void *p3 = alloc.allocate(1, 64);
  ASSERT(reinterpret_cast<uintptr_t>(p3) % 64 == 0);
}

void test_stack_reset() {
  StackAllocator alloc(1024);

  for (int i = 0; i < 10; ++i) {
    alloc.allocate(50);
  }
  ASSERT(alloc.used_size() >= 500);

  alloc.reset();
  ASSERT(alloc.used_size() == 0);
  ASSERT(alloc.free_size() == 1024);
}

void test_stack_marker_rollback() {
  StackAllocator alloc(1024);

  alloc.allocate(100);
  auto marker = alloc.get_marker();

  alloc.allocate(200);
  alloc.allocate(300);
  ASSERT(alloc.used_size() >= 600);

  alloc.rollback(marker);
  ASSERT(alloc.used_size() == marker);
}

void test_stack_out_of_memory() {
  StackAllocator alloc(100);

  void *p1 = alloc.allocate(50);
  ASSERT(p1 != nullptr);

  void *p2 = alloc.allocate(60); // Should fail
  ASSERT(p2 == nullptr);
}

// ============================================================================
// Pool Allocator Tests
// ============================================================================

void test_pool_basic_allocation() {
  PoolAllocator pool(64, 10);

  void *p1 = pool.allocate();
  ASSERT(p1 != nullptr);
  ASSERT(pool.owns(p1));
  ASSERT(pool.free_count() == 9);

  void *p2 = pool.allocate();
  ASSERT(p2 != nullptr);
  ASSERT(p2 != p1);
  ASSERT(pool.free_count() == 8);
}

void test_pool_deallocation() {
  PoolAllocator pool(64, 10);

  void *p1 = pool.allocate();
  void *p2 = pool.allocate();
  ASSERT(pool.free_count() == 8);

  pool.deallocate(p1);
  ASSERT(pool.free_count() == 9);

  pool.deallocate(p2);
  ASSERT(pool.free_count() == 10);
}

void test_pool_reuse() {
  PoolAllocator pool(64, 10);

  void *p1 = pool.allocate();
  pool.deallocate(p1);

  void *p2 = pool.allocate();
  ASSERT(p2 == p1); // Should reuse freed chunk
}

void test_pool_exhaustion() {
  PoolAllocator pool(64, 3);

  pool.allocate();
  pool.allocate();
  pool.allocate();
  ASSERT(pool.free_count() == 0);

  void *p4 = pool.allocate();
  ASSERT(p4 == nullptr); // Pool exhausted
}

void test_pool_reset() {
  PoolAllocator pool(64, 10);

  for (int i = 0; i < 10; ++i) {
    pool.allocate();
  }
  ASSERT(pool.free_count() == 0);

  pool.reset();
  ASSERT(pool.free_count() == 10);
}

// ============================================================================
// Free-List Allocator Tests
// ============================================================================

void test_freelist_basic_allocation() {
  FreeListAllocator alloc(1024);

  void *p1 = alloc.allocate(100);
  ASSERT(p1 != nullptr);
  ASSERT(alloc.owns(p1));

  void *p2 = alloc.allocate(200);
  ASSERT(p2 != nullptr);
  ASSERT(p2 != p1);
}

void test_freelist_deallocation() {
  FreeListAllocator alloc(1024);

  void *p1 = alloc.allocate(100);
  size_t used_after_alloc = alloc.used_size();

  alloc.deallocate(p1);
  ASSERT(alloc.used_size() < used_after_alloc);
}

void test_freelist_variable_sizes() {
  FreeListAllocator alloc(4096);

  std::vector<void *> ptrs;
  size_t sizes[] = {16, 32, 64, 128, 256, 512};

  for (size_t size : sizes) {
    void *ptr = alloc.allocate(size);
    ASSERT(ptr != nullptr);
    ptrs.push_back(ptr);
  }

  for (void *ptr : ptrs) {
    alloc.deallocate(ptr);
  }
}

void test_freelist_alignment() {
  FreeListAllocator alloc(1024);

  void *p1 = alloc.allocate(10, 16);
  ASSERT(reinterpret_cast<uintptr_t>(p1) % 16 == 0);

  void *p2 = alloc.allocate(10, 32);
  ASSERT(reinterpret_cast<uintptr_t>(p2) % 32 == 0);
}

void test_freelist_reset() {
  FreeListAllocator alloc(1024);

  alloc.allocate(100);
  alloc.allocate(200);
  alloc.allocate(300);

  alloc.reset();
  ASSERT(alloc.used_size() == 0);
}

// ============================================================================
// Memory Write Tests (ensure allocated memory is usable)
// ============================================================================

void test_stack_memory_write() {
  StackAllocator alloc(1024);

  char *p = static_cast<char *>(alloc.allocate(100));
  ASSERT(p != nullptr);

  // Write pattern
  std::memset(p, 0xAB, 100);

  // Verify pattern
  for (int i = 0; i < 100; ++i) {
    ASSERT(p[i] == static_cast<char>(0xAB));
  }
}

void test_pool_memory_write() {
  PoolAllocator pool(64, 10);

  char *p = static_cast<char *>(pool.allocate());
  ASSERT(p != nullptr);

  std::memset(p, 0xCD, 64);

  for (int i = 0; i < 64; ++i) {
    ASSERT(p[i] == static_cast<char>(0xCD));
  }

  pool.deallocate(p);
}

void test_freelist_memory_write() {
  FreeListAllocator alloc(1024);

  char *p = static_cast<char *>(alloc.allocate(128));
  ASSERT(p != nullptr);

  std::memset(p, 0xEF, 128);

  for (int i = 0; i < 128; ++i) {
    ASSERT(p[i] == static_cast<char>(0xEF));
  }

  alloc.deallocate(p);
}

// ============================================================================
// Main
// ============================================================================

int main() {
  std::cout << "╔════════════════════════════════════════════════╗\n";
  std::cout << "║         AllocX - Unit Tests                    ║\n";
  std::cout << "╚════════════════════════════════════════════════╝\n\n";

  std::cout << "Utils Tests:\n";
  TEST(align_up);
  TEST(is_power_of_two);
  TEST(calc_padding);

  std::cout << "\nStack Allocator Tests:\n";
  TEST(stack_basic_allocation);
  TEST(stack_alignment);
  TEST(stack_reset);
  TEST(stack_marker_rollback);
  TEST(stack_out_of_memory);
  TEST(stack_memory_write);

  std::cout << "\nPool Allocator Tests:\n";
  TEST(pool_basic_allocation);
  TEST(pool_deallocation);
  TEST(pool_reuse);
  TEST(pool_exhaustion);
  TEST(pool_reset);
  TEST(pool_memory_write);

  std::cout << "\nFree-List Allocator Tests:\n";
  TEST(freelist_basic_allocation);
  TEST(freelist_deallocation);
  TEST(freelist_variable_sizes);
  TEST(freelist_alignment);
  TEST(freelist_reset);
  TEST(freelist_memory_write);

  std::cout << "\n✓ All tests passed!\n";
  return 0;
}
