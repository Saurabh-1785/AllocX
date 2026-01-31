#include "allocx/pool_allocator.hpp"
#include <new>
#include <cassert>
#include <utility>
#include <algorithm>

namespace allocx {

PoolAllocator::PoolAllocator(size_t chunk_size, size_t chunk_count, size_t alignment)
    : m_memory(nullptr)
    , m_memory_size(0)
    , m_chunk_size(0)
    , m_chunk_count(chunk_count)
    , m_free_count(chunk_count)
    , m_alignment(alignment)
    , m_free_list(nullptr)
    , m_owns_memory(true)
{
    // Ensure chunk size is at least sizeof(void*) for intrusive list
    // and properly aligned
    m_chunk_size = std::max(chunk_size, sizeof(void*));
    m_chunk_size = utils::align_up(m_chunk_size, alignment);
    
    m_memory_size = m_chunk_size * chunk_count;
    
    if (m_memory_size > 0) {
        // Allocate aligned memory
        m_memory = ::operator new(m_memory_size + alignment);
        m_memory = utils::align_pointer(m_memory, alignment);
        init_free_list();
    }
}

PoolAllocator::PoolAllocator(void* buffer, size_t buffer_size, size_t chunk_size, size_t alignment)
    : m_memory(nullptr)
    , m_memory_size(0)
    , m_chunk_size(0)
    , m_chunk_count(0)
    , m_free_count(0)
    , m_alignment(alignment)
    , m_free_list(nullptr)
    , m_owns_memory(false)
{
    assert(buffer != nullptr || buffer_size == 0);
    
    // Align the buffer
    m_memory = utils::align_pointer(buffer, alignment);
    size_t offset = static_cast<char*>(m_memory) - static_cast<char*>(buffer);
    m_memory_size = buffer_size - offset;
    
    // Ensure chunk size is at least sizeof(void*) and aligned
    m_chunk_size = std::max(chunk_size, sizeof(void*));
    m_chunk_size = utils::align_up(m_chunk_size, alignment);
    
    // Calculate how many chunks fit
    m_chunk_count = m_memory_size / m_chunk_size;
    m_free_count = m_chunk_count;
    
    if (m_chunk_count > 0) {
        init_free_list();
    }
}

PoolAllocator::~PoolAllocator() {
    if (m_owns_memory && m_memory) {
        // Need to recover original pointer for deletion
        // This is simplified - actual implementation might need to store original
        ::operator delete(m_memory);
    }
}

PoolAllocator::PoolAllocator(PoolAllocator&& other) noexcept
    : m_memory(other.m_memory)
    , m_memory_size(other.m_memory_size)
    , m_chunk_size(other.m_chunk_size)
    , m_chunk_count(other.m_chunk_count)
    , m_free_count(other.m_free_count)
    , m_alignment(other.m_alignment)
    , m_free_list(other.m_free_list)
    , m_owns_memory(other.m_owns_memory)
{
    other.m_memory = nullptr;
    other.m_memory_size = 0;
    other.m_chunk_count = 0;
    other.m_free_count = 0;
    other.m_free_list = nullptr;
    other.m_owns_memory = false;
}

PoolAllocator& PoolAllocator::operator=(PoolAllocator&& other) noexcept {
    if (this != &other) {
        if (m_owns_memory && m_memory) {
            ::operator delete(m_memory);
        }
        
        m_memory = other.m_memory;
        m_memory_size = other.m_memory_size;
        m_chunk_size = other.m_chunk_size;
        m_chunk_count = other.m_chunk_count;
        m_free_count = other.m_free_count;
        m_alignment = other.m_alignment;
        m_free_list = other.m_free_list;
        m_owns_memory = other.m_owns_memory;
        
        other.m_memory = nullptr;
        other.m_memory_size = 0;
        other.m_chunk_count = 0;
        other.m_free_count = 0;
        other.m_free_list = nullptr;
        other.m_owns_memory = false;
    }
    return *this;
}

void PoolAllocator::init_free_list() {
    // Build intrusive linked list through chunks
    char* chunk = static_cast<char*>(m_memory);
    m_free_list = chunk;
    
    for (size_t i = 0; i < m_chunk_count - 1; ++i) {
        void** current = reinterpret_cast<void**>(chunk);
        chunk += m_chunk_size;
        *current = chunk;  // Point to next chunk
    }
    
    // Last chunk points to null
    void** last = reinterpret_cast<void**>(chunk);
    *last = nullptr;
    
    m_free_count = m_chunk_count;
}

void* PoolAllocator::allocate(size_t /*size*/, size_t /*alignment*/) {
    if (m_free_list == nullptr) {
        return nullptr;  // Pool exhausted
    }
    
    // Pop from free list
    void* ptr = m_free_list;
    m_free_list = *static_cast<void**>(m_free_list);
    --m_free_count;
    
    return ptr;
}

void PoolAllocator::deallocate(void* ptr, size_t /*size*/) {
    if (ptr == nullptr) return;
    
#ifdef DEBUG
    assert(owns(ptr) && "Pointer does not belong to this pool");
#endif
    
    // Push to free list
    *static_cast<void**>(ptr) = m_free_list;
    m_free_list = ptr;
    ++m_free_count;
}

void PoolAllocator::reset() {
    if (m_chunk_count > 0) {
        init_free_list();
    }
}

bool PoolAllocator::owns(void* ptr) const {
    const char* p = static_cast<const char*>(ptr);
    const char* start = static_cast<const char*>(m_memory);
    const char* end = start + m_memory_size;
    
    if (p < start || p >= end) {
        return false;
    }
    
    // Check if ptr is chunk-aligned
    size_t offset = p - start;
    return (offset % m_chunk_size) == 0;
}

size_t PoolAllocator::total_size() const {
    return m_memory_size;
}

size_t PoolAllocator::used_size() const {
    return (m_chunk_count - m_free_count) * m_chunk_size;
}

size_t PoolAllocator::chunk_size() const noexcept {
    return m_chunk_size;
}

size_t PoolAllocator::chunk_count() const noexcept {
    return m_chunk_count;
}

size_t PoolAllocator::free_count() const noexcept {
    return m_free_count;
}

} // namespace allocx
