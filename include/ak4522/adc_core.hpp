// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ross Baker

#pragma once

#include "ak4522/buffers.hpp"
#include "ak4522/config.hpp"
#include "ak4522/dac_core.hpp"
#include "ak4522/diagnostics.hpp"
#include "ak4522/events.hpp"
#include "ak4522/pcm.hpp"
#include "ak4522/status.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>

namespace ak4522 {

// Converts an analog input voltage to the AdcCore's normalized input: the
// voltage across AINx+ and AINx- (differential), or on one input relative to
// VCOM (single-ended). Full scale is 3.0 Vpp at VREFH = 5 V in both cases,
// so 1.0 is 0.3 x VREFH volts (AD-14, AD-A07, I-AD04).
[[nodiscard]] constexpr double adcInputFromVolts(double volts, double vrefh = 5.0) noexcept
{
    return volts / (0.3 * vrefh);
}

// How the analog signal reaches the inputs (AD-02). Full scale is the same
// in both (I-AD04); single-ended input distorts more near full scale, which
// the NominalAnalog profile reflects (AD-A10, P-110).
enum class AdcInputMode : std::uint8_t {
    differential, // AINx+ and AINx- in antiphase (the datasheet's test condition)
    singleEnded,  // one input driven, the other AC-grounded (Fig.7)
};

// Chip-level ADC configuration. The ADC shares the clocks and PD pin with the
// DAC (AGENTS.md section 17); its rate limits and the extended range are the
// DAC's (D-04, AD-01, REQ-CLOCK-02).
struct AdcConfig {
    double adcRate = 44100.0;       // Hz, [16000, 48000]
    bool allowExtendedRate = false; // up to 50400 Hz (REQ-CLOCK-02, P-103)
    CmodePin cmode = CmodePin::low;
    std::optional<double> masterClockHz; // validated against CMODE when set
    ModelProfile profile = ModelProfile::specification;
    // NominalAnalog only: noise, distortion, channel mismatch, and crosstalk
    // at the ADC's typical values (REQ-ADC-07, P-110). dcOffset is not
    // accepted: the HPF removes any offset (AD-09).
    AnalogOptions analog{};
    AdcInputMode inputMode = AdcInputMode::differential;
};

struct AdcLatencyInfo {
    double adcRate = 0.0;
    double coreAdcFrames = 0.0;    // modeled analog input to output register: 29.3125 (P-105)
    double nominalAdcFrames = 29.3; // the datasheet's typical value (AD-05)
    std::uint32_t initializationFrames = 516; // zero output after PD release (AD-10, P-108)
    double totalSeconds = 0.0;     // coreAdcFrames / adcRate
};

// The ADC section (REQ-ADC-01): normalized analog input at 8x the ADC rate
// in, one 20-bit frame per LRCK cycle out. Signal path: optional
// NominalAnalog input imperfections, decimation filter, high-pass filter,
// 20-bit quantization with saturation (AD-09, AD-13, P-105 to P-108, P-110). The input runs at the rate DacCore outputs, so a loopback
// needs no rate conversion.
//
// Events are in ADC frames. Only powerDown affects the ADC; deemphasis and
// softMute are DAC pins and have no effect here. While PD is low the output
// is zero; after release the output stays zero for 516 frames while the
// filters already run (AD-10, P-108). prepare() and reset() start the same
// way, as after power-up.
//
// Threading as DacCore.
class AdcCore {
public:
    static constexpr std::size_t oversampling = DacCore::oversampling;

    AdcCore();
    ~AdcCore();
    AdcCore(AdcCore&&) noexcept;
    AdcCore& operator=(AdcCore&&) noexcept;
    AdcCore(const AdcCore&) = delete;
    AdcCore& operator=(const AdcCore&) = delete;

    [[nodiscard]] Status prepare(const AdcConfig& config, std::size_t maxBlockFrames);
    void reset() noexcept;

    // Converts output.size() frames from input holding oversampling *
    // output.size() samples per channel. Blocks above maxBlockFrames are
    // processed in bounded chunks.
    [[nodiscard]] ProcessResult process(StereoView<const double> input, std::span<const ControlEvent> events,
                                        std::span<Pcm20Frame> output) noexcept;

    [[nodiscard]] bool isPrepared() const noexcept;
    [[nodiscard]] double inputRate() const noexcept; // adcRate * oversampling
    [[nodiscard]] AdcLatencyInfo latency() const noexcept;
    [[nodiscard]] Diagnostics diagnostics() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace ak4522
