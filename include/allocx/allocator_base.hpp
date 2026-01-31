#ifndef ALLOCX_ALLOCATOR_BASE_HPP
#define ALLOCX_ALLOCATOR_BASE_HPP

#include <cstddef>

namespace allocx {

/**
 * @brief Abstract base interface for all allocators
 * 
 * Defines the common interface that all custom allocators must implement.
 * Uses virtual functions for polymorphic usage, but concrete implementations
 * can be used directly for zero-overhead.
 */
class IAllocator {
public:
    virtual ~IAllocator() = default;

    /**
     * @brief Allocate memory with specified size and alignment
     * @param size Number of bytes to allocate
     * @param alignment Required alignment (must be power of 2)
     * @return Pointer to allocated memory, or nullptr on failure
     */
    virtual void* allocate(size_t size, size_t alignment = alignof(std::max_align_t)) = 0;

    /**
     * @brief Deallocate previously allocated memory
     * @param ptr Pointer returned by allocate()
     * @param size Size passed to allocate() (may be ignored by some allocators)
     */
    virtual void deallocate(void* ptr, size_t size = 0) = 0;

    /**
     * @brief Reset allocator state (bulk deallocation)
     * 
     * Not all allocators support reset. Default implementation does nothing.
     */
    virtual void reset() {}

    /**
     * @brief Check if allocator owns a pointer
     * @param ptr Pointer to check
     * @return true if ptr was allocated by this allocator
     */
    virtual bool owns(void* ptr) const = 0;

    /**
     * @brief Get total memory managed by this allocator
     * @return Total bytes of backing memory
     */
    virtual size_t total_size() const = 0;

    /**
     * @brief Get currently used memory
     * @return Bytes currently allocated
     */
    virtual size_t used_size() const = 0;

    // Prevent copying
    IAllocator(const IAllocator&) = delete;
    IAllocator& operator=(const IAllocator&) = delete;

protected:
    IAllocator() = default;
};

} // namespace allocx

#endif // ALLOCX_ALLOCATOR_BASE_HPP
