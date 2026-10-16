#pragma once

#include <cstdint>
#include <string_view>

namespace ak4522 {

// Result of preparation and processing calls. Every value is a documented,
// bounded outcome; no call throws.
enum class Status : std::uint8_t {
    ok,
    notPrepared,                // process called before a successful prepare
    invalidDacRate,             // outside [16000, 48000] Hz (D-04)
    invalidMasterClock,         // MCLK outside the CMODE range (D-C02 to D-C04)
    unsupportedHostRate,        // not in the supported host-rate set (P-32)
    unsupportedRatePair,        // host/DAC pair outside the tested matrix
    invalidBlockSize,           // zero maximum block size at prepare
    invalidConfiguration,       // inconsistent or out-of-range option
    channelSizeMismatch,        // left and right views differ in length
    insufficientOutputCapacity, // output view too small for the input
    eventOutOfRange,            // event offset at or beyond the block length
    eventsNotSorted,            // event offsets decrease
    formatMismatch,             // push overload does not match the configured sample format
    modeMismatch,               // push called on a pull-mode stream
};

[[nodiscard]] std::string_view toString(Status status) noexcept;

} // namespace ak4522
