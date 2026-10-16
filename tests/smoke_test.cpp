#include "ak4522/version.hpp"

#include <catch2/catch_test_macros.hpp>

TEST_CASE("Library reports a version string", "[unit]")
{
    CHECK_FALSE(ak4522::versionString().empty());
}
