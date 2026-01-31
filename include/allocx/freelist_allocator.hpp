#ifndef ALLOCX_FREELIST_ALLOCATOR_HPP
#define ALLOCX_FREELIST_ALLOCATOR_HPP

#include "allocator_base.hpp"
#include "utils.hpp"
#include <cstddef>
#include <cstdint>

namespace allocx {

/**
 * @brief Free-List Allocator for variable-size allocations
 *
 * Manages a linked list of free blocks with size metadata. Supports
 * first-fit allocation strategy, block splitting, and adjacent
 * block coalescing to reduce fragmentation.
 *
 * Time Complexity:
 * - Allocation: O(n) worst case (linear search)
 * - Deallocation: O(1) to O(n) (with coalescing)
 *
 * Use Cases:
 * - Variable-sized allocations
 * - General-purpose subsystem allocator
 * - When pool allocator is too restrictive
 */
class FreeListAllocator : public IAllocator {
public:
  /**
   * @brief Allocation strategy for finding free blocks
   */
  enum class Strategy {
    FirstFit, // Use first block that fits (fast)
    BestFit,  // Use smallest block that fits (less waste, slower)
    WorstFit  // Use largest block (keeps large blocks available)
  };

  /**
   * @brief Construct a free-list allocator
   * @param size Total size of memory block to manage
   * @param strategy Allocation strategy (default: FirstFit)
   */
  explicit FreeListAllocator(size_t size,
                             Strategy strategy = Strategy::FirstFit);

  /**
   * @brief Construct using external memory buffer
   * @param buffer Pre-allocated memory buffer
   * @param size Size of the buffer
   * @param strategy Allocation strategy
   */
  FreeListAllocator(void *buffer, size_t size,
                    Strategy strategy = Strategy::FirstFit);

  ~FreeListAllocator() override;

  // Move semantics
  FreeListAllocator(FreeListAllocator &&other) noexcept;
  FreeListAllocator &operator=(FreeListAllocator &&other) noexcept;

  /**
   * @brief Allocate memory of specified size
   * @param size Number of bytes to allocate
   * @param alignment Required alignment
   * @return Pointer to allocated memory, or nullptr if none available
   */
  void *allocate(size_t size,
                 size_t alignment = alignof(std::max_align_t)) override;

  /**
   * @brief Free previously allocated memory
   * @param ptr Pointer returned by allocate()
   * @param size Ignored (size stored in header)
   */
  void deallocate(void *ptr, size_t size = 0) override;

  /**
   * @brief Reset allocator to initial state
   */
  void reset() override;

  // IAllocator interface
  bool owns(void *ptr) const override;
  size_t total_size() const override;
  size_t used_size() const override;

  /**
   * @brief Get number of free blocks
   * @return Count of blocks in free list
   */
  size_t free_block_count() const noexcept;

  /**
   * @brief Get largest available block size
   * @return Size of largest free block
   */
  size_t largest_free_block() const noexcept;

private:
  // Block header stored before each allocation
  struct BlockHeader {
    size_t size;       // Size of data (not including header)
    BlockHeader *next; // Next free block (if free)
    bool is_free;      // Block status
    uint8_t padding;   // Alignment padding used
  };

  static constexpr size_t HEADER_SIZE = sizeof(BlockHeader);
  static constexpr size_t MIN_BLOCK_SIZE =
      sizeof(void *); // Minimum usable block

  void init();
  BlockHeader *find_first_fit(size_t size, size_t alignment) const;
  BlockHeader *find_best_fit(size_t size, size_t alignment) const;
  BlockHeader *find_worst_fit(size_t size, size_t alignment) const;
  void split_block(BlockHeader *block, size_t size, size_t padding);
  void coalesce();
  void insert_free_block(BlockHeader *block);
  void remove_free_block(BlockHeader *block);

  void *m_memory;           // Base pointer to memory block
  size_t m_size;            // Total size of block
  size_t m_used;            // Currently used bytes
  Strategy m_strategy;      // Allocation strategy
  BlockHeader *m_free_list; // Head of free block list
  bool m_owns_memory;       // Whether we should free m_memory
};

} // namespace allocx

#endif // ALLOCX_FREELIST_ALLOCATOR_HPP
