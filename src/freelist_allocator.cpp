#include "allocx/freelist_allocator.hpp"
#include <algorithm>
#include <cassert>
#include <limits>
#include <new>
#include <utility>

namespace allocx {

FreeListAllocator::FreeListAllocator(size_t size, Strategy strategy)
    : m_memory(nullptr), m_size(size), m_used(0), m_strategy(strategy),
      m_free_list(nullptr), m_owns_memory(true) {
  if (size > HEADER_SIZE) {
    m_memory = ::operator new(size);
    init();
  }
}

FreeListAllocator::FreeListAllocator(void *buffer, size_t size,
                                     Strategy strategy)
    : m_memory(buffer), m_size(size), m_used(0), m_strategy(strategy),
      m_free_list(nullptr), m_owns_memory(false) {
  assert(buffer != nullptr || size == 0);
  if (size > HEADER_SIZE) {
    init();
  }
}

FreeListAllocator::~FreeListAllocator() {
  if (m_owns_memory && m_memory) {
    ::operator delete(m_memory);
  }
}

FreeListAllocator::FreeListAllocator(FreeListAllocator &&other) noexcept
    : m_memory(other.m_memory), m_size(other.m_size), m_used(other.m_used),
      m_strategy(other.m_strategy), m_free_list(other.m_free_list),
      m_owns_memory(other.m_owns_memory) {
  other.m_memory = nullptr;
  other.m_size = 0;
  other.m_used = 0;
  other.m_free_list = nullptr;
  other.m_owns_memory = false;
}

FreeListAllocator &
FreeListAllocator::operator=(FreeListAllocator &&other) noexcept {
  if (this != &other) {
    if (m_owns_memory && m_memory) {
      ::operator delete(m_memory);
    }

    m_memory = other.m_memory;
    m_size = other.m_size;
    m_used = other.m_used;
    m_strategy = other.m_strategy;
    m_free_list = other.m_free_list;
    m_owns_memory = other.m_owns_memory;

    other.m_memory = nullptr;
    other.m_size = 0;
    other.m_used = 0;
    other.m_free_list = nullptr;
    other.m_owns_memory = false;
  }
  return *this;
}

void FreeListAllocator::init() {
  // Create initial free block spanning entire memory
  m_free_list = static_cast<BlockHeader *>(m_memory);
  m_free_list->size = m_size - HEADER_SIZE;
  m_free_list->next = nullptr;
  m_free_list->is_free = true;
  m_free_list->padding = 0;
  m_used = 0;
}

void *FreeListAllocator::allocate(size_t size, size_t alignment) {
  if (size == 0)
    return nullptr;

  // Ensure minimum size
  size = std::max(size, MIN_BLOCK_SIZE);

  // Find suitable block based on strategy
  BlockHeader *block = nullptr;
  switch (m_strategy) {
  case Strategy::FirstFit:
    block = find_first_fit(size, alignment);
    break;
  case Strategy::BestFit:
    block = find_best_fit(size, alignment);
    break;
  case Strategy::WorstFit:
    block = find_worst_fit(size, alignment);
    break;
  }

  if (!block)
    return nullptr; // No suitable block found

  // Calculate required padding for alignment
  uintptr_t data_start = reinterpret_cast<uintptr_t>(block) + HEADER_SIZE;
  size_t padding = utils::calc_padding(data_start, alignment);

  // Split block if there's enough remaining space
  size_t total_size = padding + size;
  if (block->size >= total_size + HEADER_SIZE + MIN_BLOCK_SIZE) {
    split_block(block, size, padding);
  }

  // Remove from free list and mark as used
  remove_free_block(block);
  block->is_free = false;
  block->padding = static_cast<uint8_t>(padding);

  m_used += HEADER_SIZE + block->size;

  // Return aligned data pointer
  return reinterpret_cast<char *>(block) + HEADER_SIZE + padding;
}

void FreeListAllocator::deallocate(void *ptr, size_t /*size*/) {
  if (ptr == nullptr)
    return;

  // Recover block header
  // We need to find the actual header by checking padding
  // For simplicity, we'll scan backwards - but this assumes ptr is valid
  char *data = static_cast<char *>(ptr);

  // The header is before the data, accounting for any padding
  // We try to find a valid header by scanning
  BlockHeader *block = nullptr;
  for (size_t offset = HEADER_SIZE;
       offset <= HEADER_SIZE + alignof(std::max_align_t); ++offset) {
    BlockHeader *candidate = reinterpret_cast<BlockHeader *>(data - offset);
    if (reinterpret_cast<char *>(candidate) >= static_cast<char *>(m_memory) &&
        !candidate->is_free && candidate->padding == offset - HEADER_SIZE) {
      block = candidate;
      break;
    }
  }

  if (!block) {
    // Fallback: assume HEADER_SIZE offset with no padding
    block = reinterpret_cast<BlockHeader *>(data - HEADER_SIZE);
  }

#ifdef DEBUG
  assert(owns(ptr) && "Pointer does not belong to this allocator");
  assert(!block->is_free && "Double free detected");
#endif

  m_used -= HEADER_SIZE + block->size;

  // Mark as free and add to free list
  block->is_free = true;
  insert_free_block(block);

  // Coalesce adjacent free blocks
  coalesce();
}

void FreeListAllocator::reset() {
  if (m_size > HEADER_SIZE) {
    init();
  }
}

bool FreeListAllocator::owns(void *ptr) const {
  const char *p = static_cast<const char *>(ptr);
  const char *start = static_cast<const char *>(m_memory);
  const char *end = start + m_size;
  return p >= start && p < end;
}

size_t FreeListAllocator::total_size() const { return m_size; }

size_t FreeListAllocator::used_size() const { return m_used; }

size_t FreeListAllocator::free_block_count() const noexcept {
  size_t count = 0;
  BlockHeader *current = m_free_list;
  while (current) {
    ++count;
    current = current->next;
  }
  return count;
}

size_t FreeListAllocator::largest_free_block() const noexcept {
  size_t largest = 0;
  BlockHeader *current = m_free_list;
  while (current) {
    if (current->size > largest) {
      largest = current->size;
    }
    current = current->next;
  }
  return largest;
}

FreeListAllocator::BlockHeader *
FreeListAllocator::find_first_fit(size_t size, size_t alignment) const {
  BlockHeader *current = m_free_list;
  while (current) {
    uintptr_t data_start = reinterpret_cast<uintptr_t>(current) + HEADER_SIZE;
    size_t padding = utils::calc_padding(data_start, alignment);

    if (current->size >= size + padding) {
      return current;
    }
    current = current->next;
  }
  return nullptr;
}

FreeListAllocator::BlockHeader *
FreeListAllocator::find_best_fit(size_t size, size_t alignment) const {
  BlockHeader *best = nullptr;
  size_t smallest_suitable = std::numeric_limits<size_t>::max();

  BlockHeader *current = m_free_list;
  while (current) {
    uintptr_t data_start = reinterpret_cast<uintptr_t>(current) + HEADER_SIZE;
    size_t padding = utils::calc_padding(data_start, alignment);
    size_t required = size + padding;

    if (current->size >= required && current->size < smallest_suitable) {
      best = current;
      smallest_suitable = current->size;
      if (current->size == required)
        break; // Exact fit
    }
    current = current->next;
  }
  return best;
}

FreeListAllocator::BlockHeader *
FreeListAllocator::find_worst_fit(size_t size, size_t alignment) const {
  BlockHeader *worst = nullptr;
  size_t largest_suitable = 0;

  BlockHeader *current = m_free_list;
  while (current) {
    uintptr_t data_start = reinterpret_cast<uintptr_t>(current) + HEADER_SIZE;
    size_t padding = utils::calc_padding(data_start, alignment);
    size_t required = size + padding;

    if (current->size >= required && current->size > largest_suitable) {
      worst = current;
      largest_suitable = current->size;
    }
    current = current->next;
  }
  return worst;
}

void FreeListAllocator::split_block(BlockHeader *block, size_t size,
                                    size_t padding) {
  size_t remaining = block->size - size - padding - HEADER_SIZE;

  // Create new block after the allocated space
  BlockHeader *new_block = reinterpret_cast<BlockHeader *>(
      reinterpret_cast<char *>(block) + HEADER_SIZE + padding + size);
  new_block->size = remaining;
  new_block->is_free = true;
  new_block->padding = 0;

  // Update original block size
  block->size = padding + size;

  // Insert new block into free list
  new_block->next = block->next;
  block->next = new_block;
}

void FreeListAllocator::coalesce() {
  // Sort free list by address for coalescing
  // For simplicity, we just check adjacent blocks
  BlockHeader *current = m_free_list;
  while (current && current->next) {
    BlockHeader *next = current->next;

    // Check if blocks are adjacent
    char *current_end =
        reinterpret_cast<char *>(current) + HEADER_SIZE + current->size;
    if (current_end == reinterpret_cast<char *>(next)) {
      // Merge blocks
      current->size += HEADER_SIZE + next->size;
      current->next = next->next;
      // Don't advance - check if we can merge with the new next
    } else {
      current = current->next;
    }
  }
}

void FreeListAllocator::insert_free_block(BlockHeader *block) {
  // Insert at head for O(1)
  block->next = m_free_list;
  m_free_list = block;
}

void FreeListAllocator::remove_free_block(BlockHeader *block) {
  if (m_free_list == block) {
    m_free_list = block->next;
    return;
  }

  BlockHeader *current = m_free_list;
  while (current && current->next != block) {
    current = current->next;
  }

  if (current) {
    current->next = block->next;
  }
}

} // namespace allocx
