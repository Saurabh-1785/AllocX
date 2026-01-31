#include "allocx/stack_allocator.hpp"
#include <new>
#include <cassert>
#include <utility>

namespace allocx {

StackAllocator::StackAllocator(size_t size)
    : m_memory(nullptr)
    , m_size(size)
    , m_offset(0)
    , m_owns_memory(true)
{
    if (size > 0) {
        m_memory = ::operator new(size);
    }
}

StackAllocator::StackAllocator(void* buffer, size_t size)
    : m_memory(buffer)
    , m_size(size)
    , m_offset(0)
    , m_owns_memory(false)
{
    assert(buffer != nullptr || size == 0);
}

StackAllocator::~StackAllocator() {
    if (m_owns_memory && m_memory) {
        ::operator delete(m_memory);
    }
}

StackAllocator::StackAllocator(StackAllocator&& other) noexcept
    : m_memory(other.m_memory)
    , m_size(other.m_size)
    , m_offset(other.m_offset)
    , m_owns_memory(other.m_owns_memory)
{
    other.m_memory = nullptr;
    other.m_size = 0;
    other.m_offset = 0;
    other.m_owns_memory = false;
}

StackAllocator& StackAllocator::operator=(StackAllocator&& other) noexcept {
    if (this != &other) {
        if (m_owns_memory && m_memory) {
            ::operator delete(m_memory);
        }
        
        m_memory = other.m_memory;
        m_size = other.m_size;
        m_offset = other.m_offset;
        m_owns_memory = other.m_owns_memory;
        
        other.m_memory = nullptr;
        other.m_size = 0;
        other.m_offset = 0;
        other.m_owns_memory = false;
    }
    return *this;
}

void* StackAllocator::allocate(size_t size, size_t alignment) {
    if (size == 0) return nullptr;
    
    // Calculate aligned offset
    size_t current_addr = reinterpret_cast<uintptr_t>(m_memory) + m_offset;
    size_t padding = utils::calc_padding(current_addr, alignment);
    
    // Check if we have enough space
    if (m_offset + padding + size > m_size) {
        return nullptr; // Out of memory
    }
    
    // Calculate aligned address
    size_t aligned_offset = m_offset + padding;
    void* ptr = static_cast<char*>(m_memory) + aligned_offset;
    
    // Update offset
    m_offset = aligned_offset + size;
    
    return ptr;
}

void StackAllocator::deallocate(void* /*ptr*/, size_t /*size*/) {
    // Stack allocator doesn't support individual deallocation
    // Use rollback() or reset() instead
}

void StackAllocator::reset() {
    m_offset = 0;
}

StackAllocator::Marker StackAllocator::get_marker() const noexcept {
    return m_offset;
}

void StackAllocator::rollback(Marker marker) {
    assert(marker <= m_offset && "Cannot rollback to future state");
    m_offset = marker;
}

bool StackAllocator::owns(void* ptr) const {
    const char* p = static_cast<const char*>(ptr);
    const char* start = static_cast<const char*>(m_memory);
    const char* end = start + m_size;
    return p >= start && p < end;
}

size_t StackAllocator::total_size() const {
    return m_size;
}

size_t StackAllocator::used_size() const {
    return m_offset;
}

size_t StackAllocator::free_size() const noexcept {
    return m_size - m_offset;
}

} // namespace allocx
