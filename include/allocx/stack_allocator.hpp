#ifndef ALLOCX_STACK_ALLOCATOR_HPP
#define ALLOCX_STACK_ALLOCATOR_HPP

#include "allocator_base.hpp"
#include "utils.hpp"
#include <cstddef>
#include <cstdint>

namespace allocx {

/**
 * @brief Stack (Linear) Allocator for LIFO allocation patterns
 * 
 * Pre-allocates a contiguous memory block and allocates by incrementing
 * an offset pointer. Supports markers for nested scope rollback and
 * bulk reset for frame-based deallocation.
 * 
 * Time Complexity:
 * - Allocation: O(1)
 * - Deallocation (rollback): O(1)
 * - Reset: O(1)
 * 
 * Use Cases:
 * - Per-frame game allocations
 * - Parser temporary data
 * - Scoped allocations with bulk cleanup
 */
class StackAllocator : public IAllocator {
public:
    /**
     * @brief Marker for nested allocation scopes
     * 
     * Stores the offset at a point in time for later rollback.
     */
    using Marker = size_t;

    /**
     * @brief Construct a stack allocator with given size
     * @param size Total size of memory block to manage
     */
    explicit StackAllocator(size_t size);

    /**
     * @brief Construct using external memory buffer
     * @param buffer Pre-allocated memory buffer
     * @param size Size of the buffer
     */
    StackAllocator(void* buffer, size_t size);

    ~StackAllocator() override;

    // Move semantics
    StackAllocator(StackAllocator&& other) noexcept;
    StackAllocator& operator=(StackAllocator&& other) noexcept;

    /**
     * @brief Allocate memory from the stack
     * @param size Number of bytes to allocate
     * @param alignment Required alignment (default: max align)
     * @return Pointer to allocated memory, or nullptr if full
     */
    void* allocate(size_t size, size_t alignment = alignof(std::max_align_t)) override;

    /**
     * @brief Deallocate is a no-op for stack allocator
     * 
     * Use rollback() or reset() for deallocation.
     */
    void deallocate(void* ptr, size_t size = 0) override;

    /**
     * @brief Reset allocator to initial state (bulk deallocation)
     */
    void reset() override;

    /**
     * @brief Get a marker for current allocation state
     * @return Marker that can be used with rollback()
     */
    Marker get_marker() const noexcept;

    /**
     * @brief Roll back to a previous allocation state
     * @param marker Marker obtained from get_marker()
     */
    void rollback(Marker marker);

    // IAllocator interface
    bool owns(void* ptr) const override;
    size_t total_size() const override;
    size_t used_size() const override;

    /**
     * @brief Get remaining free space
     * @return Bytes available for allocation
     */
    size_t free_size() const noexcept;

private:
    void* m_memory;       // Base pointer to memory block
    size_t m_size;        // Total size of block
    size_t m_offset;      // Current allocation offset
    bool m_owns_memory;   // Whether we should free m_memory
};

} // namespace allocx

#endif // ALLOCX_STACK_ALLOCATOR_HPP
