#ifndef ALLOCX_THREAD_SAFE_HPP
#define ALLOCX_THREAD_SAFE_HPP

#include <cstddef>
#include <mutex>

namespace allocx {

/**
 * @brief Thread-safe wrapper for any allocator
 *
 * Uses mutex-based locking to serialize all allocator operations.
 * Simple and correct, but serializes all threads.
 *
 * For higher concurrency, consider:
 * - Thread-local allocators
 * - Lock-free implementations
 * - Arena-per-thread patterns
 *
 * Usage:
 *   PoolAllocator pool(64, 1000);
 *   ThreadSafeAllocator<PoolAllocator> safe_pool(pool);
 */
template <typename Allocator> class ThreadSafeAllocator {
public:
  /**
   * @brief Construct with reference to underlying allocator
   */
  explicit ThreadSafeAllocator(Allocator &allocator) noexcept
      : m_allocator(&allocator) {}

  /**
   * @brief Thread-safe allocation
   */
  void *allocate(size_t size, size_t alignment = alignof(std::max_align_t)) {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_allocator->allocate(size, alignment);
  }

  /**
   * @brief Thread-safe deallocation
   */
  void deallocate(void *ptr, size_t size = 0) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_allocator->deallocate(ptr, size);
  }

  /**
   * @brief Thread-safe reset
   */
  void reset() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_allocator->reset();
  }

  /**
   * @brief Check ownership (thread-safe)
   */
  bool owns(void *ptr) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_allocator->owns(ptr);
  }

  /**
   * @brief Get total size (thread-safe)
   */
  size_t total_size() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_allocator->total_size();
  }

  /**
   * @brief Get used size (thread-safe)
   */
  size_t used_size() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_allocator->used_size();
  }

  /**
   * @brief Get reference to underlying allocator (NOT thread-safe!)
   *
   * Use only when you have external synchronization.
   */
  Allocator &get_underlying() noexcept { return *m_allocator; }

  // Prevent copying
  ThreadSafeAllocator(const ThreadSafeAllocator &) = delete;
  ThreadSafeAllocator &operator=(const ThreadSafeAllocator &) = delete;

private:
  Allocator *m_allocator;
  mutable std::mutex m_mutex;
};

} // namespace allocx

#endif // ALLOCX_THREAD_SAFE_HPP
