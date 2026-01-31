#ifndef ALLOCX_UTILS_HPP
#define ALLOCX_UTILS_HPP

#include <cstddef>
#include <cstdint>
#include <cassert>

namespace allocx {
namespace utils {

/**
 * @brief Align a value up to the nearest multiple of alignment
 * @param value The value to align
 * @param alignment Alignment boundary (must be power of 2)
 * @return Aligned value >= input value
 */
inline constexpr size_t align_up(size_t value, size_t alignment) noexcept {
    assert((alignment & (alignment - 1)) == 0 && "Alignment must be power of 2");
    return (value + alignment - 1) & ~(alignment - 1);
}

/**
 * @brief Align a pointer up to the nearest aligned address
 * @param ptr The pointer to align
 * @param alignment Alignment boundary (must be power of 2)
 * @return Aligned pointer >= input pointer
 */
inline void* align_pointer(void* ptr, size_t alignment) noexcept {
    assert((alignment & (alignment - 1)) == 0 && "Alignment must be power of 2");
    uintptr_t addr = reinterpret_cast<uintptr_t>(ptr);
    uintptr_t aligned = align_up(addr, alignment);
    return reinterpret_cast<void*>(aligned);
}

/**
 * @brief Check if a pointer is properly aligned
 * @param ptr The pointer to check
 * @param alignment Alignment to check against
 * @return true if pointer is aligned to boundary
 */
inline bool is_aligned(const void* ptr, size_t alignment) noexcept {
    return (reinterpret_cast<uintptr_t>(ptr) & (alignment - 1)) == 0;
}

/**
 * @brief Calculate padding needed to align an address
 * @param address The address to align
 * @param alignment Desired alignment
 * @return Number of padding bytes needed
 */
inline constexpr size_t calc_padding(size_t address, size_t alignment) noexcept {
    return (alignment - (address & (alignment - 1))) & (alignment - 1);
}

/**
 * @brief Calculate padding with header space
 * @param address The address to align
 * @param alignment Desired alignment
 * @param header_size Size of header to include
 * @return Padding bytes including space for header
 */
inline size_t calc_padding_with_header(size_t address, size_t alignment, size_t header_size) noexcept {
    size_t padding = calc_padding(address, alignment);
    
    if (padding < header_size) {
        // Need more space for header
        size_t needed = header_size - padding;
        // Make sure we still maintain alignment
        padding += alignment * ((needed + alignment - 1) / alignment);
    }
    
    return padding;
}

/**
 * @brief Check if a value is a power of 2
 * @param value Value to check
 * @return true if value is power of 2
 */
inline constexpr bool is_power_of_two(size_t value) noexcept {
    return value && ((value & (value - 1)) == 0);
}

/**
 * @brief Get the next power of 2 >= value
 * @param value Input value
 * @return Smallest power of 2 >= value
 */
inline size_t next_power_of_two(size_t value) noexcept {
    if (value == 0) return 1;
    --value;
    value |= value >> 1;
    value |= value >> 2;
    value |= value >> 4;
    value |= value >> 8;
    value |= value >> 16;
    if constexpr (sizeof(size_t) > 4) {
        value |= value >> 32;
    }
    return value + 1;
}

/**
 * @brief Advance a pointer by a byte offset
 * @param ptr Base pointer
 * @param offset Bytes to advance
 * @return Pointer advanced by offset bytes
 */
inline void* ptr_add(void* ptr, size_t offset) noexcept {
    return static_cast<char*>(ptr) + offset;
}

/**
 * @brief Advance a const pointer by a byte offset
 * @param ptr Base pointer
 * @param offset Bytes to advance
 * @return Pointer advanced by offset bytes
 */
inline const void* ptr_add(const void* ptr, size_t offset) noexcept {
    return static_cast<const char*>(ptr) + offset;
}

/**
 * @brief Calculate byte distance between two pointers
 * @param end End pointer
 * @param start Start pointer
 * @return Number of bytes from start to end
 */
inline ptrdiff_t ptr_diff(const void* end, const void* start) noexcept {
    return static_cast<const char*>(end) - static_cast<const char*>(start);
}

} // namespace utils
} // namespace allocx

#endif // ALLOCX_UTILS_HPP
