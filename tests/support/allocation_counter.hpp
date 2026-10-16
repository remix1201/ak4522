#pragma once

// Counts heap allocations made by the current thread while armed, for
// real-time safety tests (REQ-RT-01). The test executable replaces global
// operator new and delete; see allocation_counter.cpp.

#include <cstdint>

namespace ak4522::test {

class AllocationCounter {
public:
    AllocationCounter() noexcept;
    ~AllocationCounter();
    AllocationCounter(const AllocationCounter&) = delete;
    AllocationCounter& operator=(const AllocationCounter&) = delete;

    // Allocations and deallocations on this thread since construction.
    [[nodiscard]] std::uint64_t allocations() const noexcept;
    [[nodiscard]] std::uint64_t deallocations() const noexcept;

private:
    std::uint64_t startAllocations_;
    std::uint64_t startDeallocations_;
    bool previouslyArmed_;
};

} // namespace ak4522::test
