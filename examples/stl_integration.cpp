#include <iostream>
#include <list>
#include <map>
#include <string>
#include <vector>

#include "allocx/freelist_allocator.hpp"
#include "allocx/pool_allocator.hpp"
#include "allocx/stl_adapter.hpp"

using namespace allocx;

int main() {
  std::cout << "╔════════════════════════════════════════════════╗\n";
  std::cout << "║         AllocX - STL Integration Example       ║\n";
  std::cout << "╚════════════════════════════════════════════════╝\n\n";

  // ========================================================================
  // Using Pool Allocator with std::vector
  // ========================================================================
  std::cout << "=== Pool Allocator + std::vector ===\n";
  {
    // Pool for integers
    PoolAllocator pool(sizeof(int) * 100, 100); // Room for 100 ints at a time
    STLAdapter<int, PoolAllocator> adapter(pool);

    // Note: vector may need larger chunks for reallocation
    // This example works best with fixed-size usage

    std::cout << "Pool created with " << pool.chunk_count() << " chunks\n";
    std::cout << "Each chunk: " << pool.chunk_size() << " bytes\n";

    // Allocate some integers manually
    int *arr = adapter.allocate(10);
    for (int i = 0; i < 10; ++i) {
      arr[i] = i * 10;
    }
    std::cout << "Allocated 10 ints: ";
    for (int i = 0; i < 10; ++i) {
      std::cout << arr[i] << " ";
    }
    std::cout << "\n";
    adapter.deallocate(arr, 10);
  }

  // ========================================================================
  // Using FreeList Allocator with std::list
  // ========================================================================
  std::cout << "\n=== FreeList Allocator + std::list ===\n";
  {
    FreeListAllocator alloc(64 * 1024); // 64KB
    STLAdapter<int, FreeListAllocator> adapter(alloc);

    // std::list with custom allocator
    std::list<int, STLAdapter<int, FreeListAllocator>> myList(adapter);

    // Add elements
    for (int i = 0; i < 20; ++i) {
      myList.push_back(i * 5);
    }
    std::cout << "List contains " << myList.size() << " elements\n";
    std::cout << "Allocator used: " << alloc.used_size() << " bytes\n";

    // Print elements
    std::cout << "Elements: ";
    for (const auto &val : myList) {
      std::cout << val << " ";
    }
    std::cout << "\n";

    // Remove some elements
    myList.remove_if([](int x) { return x % 10 == 0; });
    std::cout << "After removing multiples of 10: " << myList.size()
              << " elements\n";
  }

  // ========================================================================
  // Using FreeList Allocator with std::map
  // ========================================================================
  std::cout << "\n=== FreeList Allocator + std::map ===\n";
  {
    FreeListAllocator alloc(128 * 1024); // 128KB

    using MapAllocator =
        STLAdapter<std::pair<const int, std::string>, FreeListAllocator>;
    MapAllocator adapter(alloc);

    std::map<int, std::string, std::less<int>, MapAllocator> myMap(
        std::less<int>(), adapter);

    // Insert entries
    myMap[1] = "one";
    myMap[2] = "two";
    myMap[3] = "three";
    myMap[42] = "forty-two";
    myMap[100] = "one hundred";

    std::cout << "Map contains " << myMap.size() << " entries\n";
    std::cout << "Allocator used: " << alloc.used_size() << " bytes\n";

    // Print entries
    std::cout << "Entries:\n";
    for (const auto &[key, value] : myMap) {
      std::cout << "  " << key << " -> " << value << "\n";
    }

    // Lookup
    std::cout << "myMap[42] = " << myMap[42] << "\n";
  }

  // ========================================================================
  // Demonstrating allocator efficiency
  // ========================================================================
  std::cout << "\n=== Efficiency Demonstration ===\n";
  {
    // Compare memory usage
    FreeListAllocator alloc(256 * 1024);

    using VecAllocator = STLAdapter<int, FreeListAllocator>;
    VecAllocator adapter(alloc);

    size_t before = alloc.used_size();

    // Create vector with reserved capacity
    std::vector<int, VecAllocator> vec(adapter);

    // Add elements
    for (int i = 0; i < 1000; ++i) {
      vec.push_back(i);
    }

    size_t after = alloc.used_size();

    std::cout << "Vector with 1000 ints\n";
    std::cout << "  Memory before: " << before << " bytes\n";
    std::cout << "  Memory after: " << after << " bytes\n";
    std::cout << "  Memory used for vector: " << (after - before) << " bytes\n";
    std::cout << "  Theoretical minimum: " << (1000 * sizeof(int))
              << " bytes\n";
  }

  std::cout << "\n✓ STL integration examples completed!\n";
  return 0;
}
