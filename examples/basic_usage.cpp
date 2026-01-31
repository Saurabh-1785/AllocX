#include <cstring>
#include <iostream>

#include "allocx/freelist_allocator.hpp"
#include "allocx/pool_allocator.hpp"
#include "allocx/stack_allocator.hpp"

using namespace allocx;

// Example struct to allocate
struct Particle {
  float x, y, z;
  float vx, vy, vz;
  float lifetime;
  int type;
};

int main() {
  std::cout << "╔════════════════════════════════════════════════╗\n";
  std::cout << "║         AllocX - Basic Usage Example           ║\n";
  std::cout << "╚════════════════════════════════════════════════╝\n\n";

  // ========================================================================
  // Stack Allocator Example (per-frame allocations)
  // ========================================================================
  std::cout << "=== Stack Allocator (Frame Allocations) ===\n";
  {
    // Simulate per-frame memory for a game
    StackAllocator frame_allocator(1024 * 1024); // 1MB frame buffer

    // Frame 1
    std::cout << "Frame 1:\n";
    auto marker = frame_allocator.get_marker();

    int *temp_data =
        static_cast<int *>(frame_allocator.allocate(100 * sizeof(int)));
    for (int i = 0; i < 100; ++i)
      temp_data[i] = i * i;

    char *debug_string = static_cast<char *>(frame_allocator.allocate(256));
    std::strcpy(debug_string, "Frame 1 debug info");

    std::cout << "  Allocated " << frame_allocator.used_size() << " bytes\n";
    std::cout << "  Debug: " << debug_string << "\n";

    // End of frame - bulk reset
    frame_allocator.rollback(marker);
    std::cout << "  After rollback: " << frame_allocator.used_size()
              << " bytes\n";

    // Frame 2 - memory is reused
    std::cout << "\nFrame 2:\n";
    float *vertices =
        static_cast<float *>(frame_allocator.allocate(1000 * sizeof(float)));
    for (int i = 0; i < 1000; ++i)
      vertices[i] = static_cast<float>(i) * 0.1f;
    std::cout << "  Allocated " << frame_allocator.used_size() << " bytes\n";

    frame_allocator.reset(); // Complete reset
  }

  // ========================================================================
  // Pool Allocator Example (particle system)
  // ========================================================================
  std::cout << "\n=== Pool Allocator (Particle System) ===\n";
  {
    // Create pool for particles
    PoolAllocator particle_pool(sizeof(Particle), 1000);

    std::cout << "Particle size: " << sizeof(Particle) << " bytes\n";
    std::cout << "Pool capacity: " << particle_pool.chunk_count()
              << " particles\n";

    // Spawn particles
    Particle *particles[100];
    for (int i = 0; i < 100; ++i) {
      particles[i] = static_cast<Particle *>(particle_pool.allocate());
      particles[i]->x = static_cast<float>(i);
      particles[i]->y = 0.0f;
      particles[i]->z = 0.0f;
      particles[i]->lifetime = 5.0f;
    }
    std::cout << "Spawned 100 particles\n";
    std::cout << "Free chunks: " << particle_pool.free_count() << "\n";

    // Despawn some particles
    for (int i = 0; i < 50; ++i) {
      particle_pool.deallocate(particles[i]);
    }
    std::cout << "Despawned 50 particles\n";
    std::cout << "Free chunks: " << particle_pool.free_count() << "\n";

    // Spawn new particles (reuses freed memory)
    for (int i = 0; i < 25; ++i) {
      Particle *p = static_cast<Particle *>(particle_pool.allocate());
      p->lifetime = 10.0f;
    }
    std::cout << "Spawned 25 new particles\n";
    std::cout << "Free chunks: " << particle_pool.free_count() << "\n";
  }

  // ========================================================================
  // Free-List Allocator Example (variable sizes)
  // ========================================================================
  std::cout << "\n=== Free-List Allocator (Variable Sizes) ===\n";
  {
    FreeListAllocator alloc(64 * 1024); // 64KB

    // Allocate various sizes
    void *small = alloc.allocate(32);
    void *medium = alloc.allocate(256);
    void *large = alloc.allocate(1024);
    void *xlarge = alloc.allocate(4096);

    std::cout << "Allocated: 32B + 256B + 1KB + 4KB\n";
    std::cout << "Used: " << alloc.used_size() << " bytes\n";
    std::cout << "Free blocks: " << alloc.free_block_count() << "\n";
    std::cout << "Largest free: " << alloc.largest_free_block() << " bytes\n";

    // Free in random order
    alloc.deallocate(medium);
    alloc.deallocate(small);
    std::cout << "\nFreed small and medium\n";
    std::cout << "Free blocks: " << alloc.free_block_count() << "\n";

    // Allocate again
    void *new_alloc = alloc.allocate(200);
    std::cout << "Allocated 200B: " << (new_alloc ? "success" : "failed")
              << "\n";

    alloc.deallocate(large);
    alloc.deallocate(xlarge);
    alloc.deallocate(new_alloc);
  }

  std::cout << "\n✓ Examples completed successfully!\n";
  return 0;
}
