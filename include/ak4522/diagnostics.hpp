#pragma once

#include <cstdint>

namespace ak4522 {

// Counters written on the audio thread and readable from any thread as a
// snapshot. Counters only increase until the next prepare.
struct Diagnostics {
    std::uint64_t nonFiniteInputSamples = 0; // replaced by zero (P-10)
    std::uint64_t clippedInputSamples = 0;   // saturated during quantization (P-10)
    std::uint64_t chunkedBlocks = 0;         // blocks above the prepared maximum, split internally
    std::uint64_t rejectedBlocks = 0;        // process calls that returned a non-ok status
    std::uint64_t ignoredEvents = 0;         // repeated same-level events (P-53)
    std::uint64_t droppedEvents = 0;         // pending-event capacity exceeded
    std::uint64_t hostUnderruns = 0;         // host output frames not yet available; always 0 by design
};

} // namespace ak4522
