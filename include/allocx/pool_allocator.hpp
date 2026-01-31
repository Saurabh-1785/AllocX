#ifndef ALLOCX_POOL_ALLOCATOR_HPP
#define ALLOCX_POOL_ALLOCATOR_HPP

#include "allocator_base.hpp"
#include "utils.hpp"
#include <cstddef>
#include <cstdint>

namespace allocx {

/**
 * @brief Pool Allocator for fixed-size object allocation
 * 
 * Pre-allocates an array of fixed-size chunks and manages them using
 * an intrusive free-list. Zero fragmentation, O(1) allocation and
 * deallocation.
 * 
 * Time Complexity:
 * - Allocation: O(1)
 * - Deallocation: O(1)
 * - No fragmentation possible
 * 
 * Use Cases:
 * - Game entities, particles
 * - Network packet buffers
 * - Frequently allocated/freed same-size objects
 */
class PoolAllocator : public IAllocator {
public:
    /**
     * @brief Construct a pool allocator
     * @param chunk_size Size of each chunk (must be >= sizeof(void*))
     * @param chunk_count Number of chunks in the pool
     * @param alignment Chunk alignment (default: max align)
     */
    explicit PoolAllocator(size_t chunk_size, size_t chunk_count, 
                           size_t alignment = alignof(std::max_align_t));

    /**
     * @brief Construct using external memory buffer
     * @param buffer Pre-allocated memory buffer
     * @param buffer_size Size of the buffer
     * @param chunk_size Size of each chunk
     * @param alignment Chunk alignment
     */
    PoolAllocator(void* buffer, size_t buffer_size, size_t chunk_size,
                  size_t alignment = alignof(std::max_align_t));

    ~PoolAllocator() override;

    // Move semantics
    PoolAllocator(PoolAllocator&& other) noexcept;
    PoolAllocator& operator=(PoolAllocator&& other) noexcept;

    /**
     * @brief Allocate a chunk from the pool
     * @param size Ignored (chunks are fixed size)
     * @param alignment Ignored (alignment set at construction)
     * @return Pointer to allocated chunk, or nullptr if pool exhausted
     */
    void* allocate(size_t size = 0, size_t alignment = 0) override;

    /**
     * @brief Return a chunk to the pool
     * @param ptr Pointer to chunk obtained from allocate()
     * @param size Ignored
     */
    void deallocate(void* ptr, size_t size = 0) override;

    /**
     * @brief Reset pool to initial state (all chunks free)
     */
    void reset() override;

    // IAllocator interface
    bool owns(void* ptr) const override;
    size_t total_size() const override;
    size_t used_size() const override;

    /**
     * @brief Get the chunk size
     * @return Size of each chunk in bytes
     */
    size_t chunk_size() const noexcept;

    /**
     * @brief Get total chunk count
     * @return Number of chunks in pool
     */
    size_t chunk_count() const noexcept;

    /**
     * @brief Get number of free chunks
     * @return Available chunks for allocation
     */
    size_t free_count() const noexcept;

private:
    // Rebuild the free list (used by reset and constructors)
    void init_free_list();

    void* m_memory;           // Base pointer to memory block
    size_t m_memory_size;     // Total allocated memory size
    size_t m_chunk_size;      // Size of each chunk (aligned)
    size_t m_chunk_count;     // Total number of chunks
    size_t m_free_count;      // Number of free chunks
    size_t m_alignment;       // Chunk alignment
    void* m_free_list;        // Head of intrusive free list
    bool m_owns_memory;       // Whether we should free m_memory
};

} // namespace allocx

#endif // ALLOCX_POOL_ALLOCATOR_HPP
