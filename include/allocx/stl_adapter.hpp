#ifndef ALLOCX_STL_ADAPTER_HPP
#define ALLOCX_STL_ADAPTER_HPP

#include <cstddef>
#include <memory>

namespace allocx {

/**
 * @brief STL-compatible allocator adapter
 *
 * Wraps any AllocX allocator to work with STL containers like
 * std::vector, std::list, std::map, etc.
 *
 * Usage:
 *   PoolAllocator pool(sizeof(int), 1000);
 *   STLAdapter<int, PoolAllocator> adapter(pool);
 *   std::vector<int, STLAdapter<int, PoolAllocator>> vec(adapter);
 */
template <typename T, typename Allocator> class STLAdapter {
public:
  // Required type definitions for STL allocator concept
  using value_type = T;
  using size_type = size_t;
  using difference_type = std::ptrdiff_t;
  using propagate_on_container_move_assignment = std::true_type;
  using is_always_equal = std::false_type;

  /**
   * @brief Construct with reference to underlying allocator
   */
  explicit STLAdapter(Allocator &allocator) noexcept
      : m_allocator(&allocator) {}

  /**
   * @brief Copy constructor
   */
  STLAdapter(const STLAdapter &other) noexcept
      : m_allocator(other.m_allocator) {}

  /**
   * @brief Rebind constructor for container node allocation
   */
  template <typename U>
  STLAdapter(const STLAdapter<U, Allocator> &other) noexcept
      : m_allocator(other.get_allocator_ptr()) {}

  /**
   * @brief Allocate memory for n objects of type T
   * @param n Number of objects
   * @return Pointer to allocated memory
   */
  T *allocate(size_type n) {
    if (n == 0)
      return nullptr;

    void *ptr = m_allocator->allocate(n * sizeof(T), alignof(T));
    if (!ptr) {
      throw std::bad_alloc();
    }
    return static_cast<T *>(ptr);
  }

  /**
   * @brief Deallocate memory
   * @param ptr Pointer to deallocate
   * @param n Number of objects (may be ignored by some allocators)
   */
  void deallocate(T *ptr, size_type n) noexcept {
    if (ptr) {
      m_allocator->deallocate(ptr, n * sizeof(T));
    }
  }

  /**
   * @brief Get pointer to underlying allocator
   */
  Allocator *get_allocator_ptr() const noexcept { return m_allocator; }

  /**
   * @brief Equality comparison
   */
  template <typename U>
  bool operator==(const STLAdapter<U, Allocator> &other) const noexcept {
    return m_allocator == other.get_allocator_ptr();
  }

  /**
   * @brief Inequality comparison
   */
  template <typename U>
  bool operator!=(const STLAdapter<U, Allocator> &other) const noexcept {
    return !(*this == other);
  }

  /**
   * @brief Rebind to different type
   */
  template <typename U> struct rebind {
    using other = STLAdapter<U, Allocator>;
  };

private:
  Allocator *m_allocator;
};

/**
 * @brief Non-member equality comparison
 */
template <typename T, typename U, typename Allocator>
bool operator==(const STLAdapter<T, Allocator> &lhs,
                const STLAdapter<U, Allocator> &rhs) noexcept {
  return lhs.get_allocator_ptr() == rhs.get_allocator_ptr();
}

/**
 * @brief Non-member inequality comparison
 */
template <typename T, typename U, typename Allocator>
bool operator!=(const STLAdapter<T, Allocator> &lhs,
                const STLAdapter<U, Allocator> &rhs) noexcept {
  return !(lhs == rhs);
}

} // namespace allocx

#endif // ALLOCX_STL_ADAPTER_HPP
