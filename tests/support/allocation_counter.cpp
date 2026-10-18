// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ross Baker

#include "support/allocation_counter.hpp"

#include <cstddef>
#include <cstdlib>
#if defined(_MSC_VER)
#include <malloc.h>
#endif
#include <new>

namespace {

thread_local bool armed = false;
thread_local std::uint64_t allocationCount = 0;
thread_local std::uint64_t deallocationCount = 0;

// Over-aligned allocations use the platform's aligned allocator, which on
// MSVC must be paired with _aligned_free.
void* allocate(std::size_t size, std::size_t alignment)
{
    if (armed) {
        ++allocationCount;
    }
    if (size == 0) {
        size = 1;
    }
    void* pointer = nullptr;
    if (alignment <= alignof(std::max_align_t)) {
        pointer = std::malloc(size);
    } else {
#if defined(_MSC_VER)
        pointer = _aligned_malloc(size, alignment);
#else
        const std::size_t rounded = (size + alignment - 1) / alignment * alignment;
        pointer = std::aligned_alloc(alignment, rounded);
#endif
    }
    if (pointer == nullptr) {
        throw std::bad_alloc();
    }
    return pointer;
}

void release(void* pointer, bool aligned = false) noexcept
{
    if (pointer == nullptr) {
        return;
    }
    if (armed) {
        ++deallocationCount;
    }
#if defined(_MSC_VER)
    if (aligned) {
        _aligned_free(pointer);
        return;
    }
#endif
    static_cast<void>(aligned);
    std::free(pointer);
}

} // namespace

namespace ak4522::test {

AllocationCounter::AllocationCounter() noexcept
    : startAllocations_(allocationCount)
    , startDeallocations_(deallocationCount)
    , previouslyArmed_(armed)
{
    armed = true;
}

AllocationCounter::~AllocationCounter()
{
    armed = previouslyArmed_;
}

std::uint64_t AllocationCounter::allocations() const noexcept
{
    return allocationCount - startAllocations_;
}

std::uint64_t AllocationCounter::deallocations() const noexcept
{
    return deallocationCount - startDeallocations_;
}

} // namespace ak4522::test

// Global replacements. Every form routes through allocate and release.
void* operator new(std::size_t size) { return allocate(size, 0); }
void* operator new[](std::size_t size) { return allocate(size, 0); }
void* operator new(std::size_t size, std::align_val_t a) { return allocate(size, static_cast<std::size_t>(a)); }
void* operator new[](std::size_t size, std::align_val_t a) { return allocate(size, static_cast<std::size_t>(a)); }

void* operator new(std::size_t size, const std::nothrow_t&) noexcept
{
    try { return allocate(size, 0); } catch (...) { return nullptr; }
}
void* operator new[](std::size_t size, const std::nothrow_t&) noexcept
{
    try { return allocate(size, 0); } catch (...) { return nullptr; }
}
void* operator new(std::size_t size, std::align_val_t a, const std::nothrow_t&) noexcept
{
    try { return allocate(size, static_cast<std::size_t>(a)); } catch (...) { return nullptr; }
}
void* operator new[](std::size_t size, std::align_val_t a, const std::nothrow_t&) noexcept
{
    try { return allocate(size, static_cast<std::size_t>(a)); } catch (...) { return nullptr; }
}

void operator delete(void* p) noexcept { release(p); }
void operator delete[](void* p) noexcept { release(p); }
void operator delete(void* p, std::size_t) noexcept { release(p); }
void operator delete[](void* p, std::size_t) noexcept { release(p); }
void operator delete(void* p, std::align_val_t) noexcept { release(p, true); }
void operator delete[](void* p, std::align_val_t) noexcept { release(p, true); }
void operator delete(void* p, std::size_t, std::align_val_t) noexcept { release(p, true); }
void operator delete[](void* p, std::size_t, std::align_val_t) noexcept { release(p, true); }
void operator delete(void* p, const std::nothrow_t&) noexcept { release(p); }
void operator delete[](void* p, const std::nothrow_t&) noexcept { release(p); }
void operator delete(void* p, std::align_val_t, const std::nothrow_t&) noexcept { release(p, true); }
void operator delete[](void* p, std::align_val_t, const std::nothrow_t&) noexcept { release(p, true); }
