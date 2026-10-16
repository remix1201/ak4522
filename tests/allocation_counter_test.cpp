// The allocation counter must see allocations, or the real-time tests that
// rely on it prove nothing. Direct new and delete keep the expected counts
// independent of standard-library internals (MSVC's debug containers, for
// example, allocate extra bookkeeping objects).

#include "support/allocation_counter.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>

using ak4522::test::AllocationCounter;

namespace {

struct alignas(64) Wide {
    double data[8];
};

} // namespace

TEST_CASE("Allocation counter detects heap use", "[unit][rt]")
{
    std::uint64_t allocations = 0;
    std::uint64_t deallocations = 0;
    {
        AllocationCounter counter;
        int* single = new int(1);
        double* array = new double[16];
        Wide* wide = new Wide{};   // over-aligned: the aligned operator new
        delete wide;
        delete[] array;
        delete single;
        // Read the counts before any assertion can allocate.
        allocations = counter.allocations();
        deallocations = counter.deallocations();
    }
    CHECK(allocations == 3);
    CHECK(deallocations == 3);
}

TEST_CASE("Allocation counter reports zero when nothing allocates", "[unit][rt]")
{
    std::uint64_t allocations = 0;
    {
        AllocationCounter counter;
        volatile int stackValue = 0;
        stackValue = stackValue + 1;
        allocations = counter.allocations();
    }
    CHECK(allocations == 0);
}
