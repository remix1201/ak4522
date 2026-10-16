#pragma once

#include <cstdint>

namespace ak4522 {

// Latency by component and time domain (REQ-LAT-01). Group delay is a
// passband phase-slope measurement; buffering is causal sample delay.
struct LatencyInfo {
    double dacRate = 0.0;               // Hz
    double hostRate = 0.0;              // Hz; zero for DacCore alone
    bool outsideDocumentedRate = false; // dacRate above 48 kHz, extended range (P-103)

    double inputSrcHostFrames = 0.0;    // input SRC group delay
    double coreDacFrames = 0.0;         // chip model delay; nominal 14.7 (D-F06)
    double outputSrcHostFrames = 0.0;   // output SRC group delay
    double alignmentHostFrames = 0.0;   // added fractional alignment delay
    std::uint32_t bufferingHostFrames = 0; // causal buffering in adapters

    double totalSeconds = 0.0;          // sum of all components
    std::uint32_t reportedHostFrames = 0; // integer latency reported to the host (REQ-LAT-03)
};

} // namespace ak4522
