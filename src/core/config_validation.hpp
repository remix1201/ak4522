#pragma once

// Configuration checks shared by DacCore::prepare and HostProcessor::prepare.
// Internal: not part of the public API.

#include "ak4522/config.hpp"
#include "ak4522/dac_stream.hpp"
#include "ak4522/host_processor.hpp"
#include "ak4522/status.hpp"

namespace ak4522::detail {

// DAC sample-rate limits in Hz (D-04).
inline constexpr double minDacRate = 16000.0;
inline constexpr double maxDacRate = 48000.0;

// Relative tolerance when checking that MCLK equals ratio x fs (P-35).
inline constexpr double masterClockTolerance = 1e-9;

// Largest DacStream rate denominator; keeps converter arithmetic within
// 64 bits (P-100).
inline constexpr std::uint64_t maxRateDenominator = std::uint64_t{1} << 20;

// MCLK in Hz implied by CMODE and the DAC rate (I-C01).
[[nodiscard]] double derivedMasterClockHz(CmodePin cmode, double dacRate) noexcept;

// Checks the DAC rate (D-04), optional MCLK synchronization with LRCK (D-C05,
// P-35), which implies the MCLK range (I-C02), enum ranges, and profile
// availability (O-03).
[[nodiscard]] Status validate(const DacConfig& config) noexcept;

// Checks the host rate (P-32), block and event capacities, and the DAC config.
[[nodiscard]] Status validate(const HostConfig& config) noexcept;

// Checks the host rate (P-32), the exact DAC rate (P-100), mode, format,
// capacities, the pull callback, and the DAC config at the exact rate.
[[nodiscard]] Status validate(const DacStreamConfig& config) noexcept;

} // namespace ak4522::detail
