#include "ak4522/status.hpp"

#include <catch2/catch_test_macros.hpp>

#include <set>
#include <string_view>

using ak4522::Status;

TEST_CASE("Every status has a distinct description", "[unit]")
{
    const Status all[] = {
        Status::ok, Status::notPrepared, Status::invalidDacRate, Status::invalidMasterClock,
        Status::unsupportedHostRate, Status::unsupportedRatePair, Status::invalidBlockSize,
        Status::invalidConfiguration, Status::channelSizeMismatch,
        Status::insufficientOutputCapacity, Status::eventOutOfRange, Status::eventsNotSorted,
        Status::formatMismatch, Status::modeMismatch,
    };
    std::set<std::string_view> seen;
    for (const Status status : all) {
        const std::string_view text = ak4522::toString(status);
        CHECK_FALSE(text.empty());
        CHECK(text != "unknown status");
        CHECK(seen.insert(text).second);
    }
}
