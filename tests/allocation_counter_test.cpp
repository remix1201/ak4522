// The allocation counter must see allocations, or the real-time tests that
// rely on it prove nothing.

#include "support/allocation_counter.hpp"

#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <vector>

using ak4522::test::AllocationCounter;

TEST_CASE("Allocation counter detects heap use", "[unit][rt]")
{
    AllocationCounter counter;
    {
        std::vector<int> values(16);
        auto owned = std::make_unique<double>(1.0);
        alignas(64) static constexpr int unused = 0;
        static_cast<void>(unused);
        struct alignas(64) Wide { double data[8]; };
        auto wide = std::make_unique<Wide>();
    }
    CHECK(counter.allocations() == 3);
    CHECK(counter.deallocations() == 3);
}

TEST_CASE("Allocation counter reports zero when nothing allocates", "[unit][rt]")
{
    AllocationCounter counter;
    int stackValue = 0;
    stackValue += 1;
    CHECK(stackValue == 1);
    CHECK(counter.allocations() == 0);
}
