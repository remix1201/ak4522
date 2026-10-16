#pragma once

#include <cstdint>
#include <optional>

namespace ak4522 {

// CMODE is a three-state pin: low, high, or left open (D-C01).
enum class CmodePin : std::uint8_t { low, high, open };

// MCLK / fs ratio selected by CMODE: L = 256fs, H = 384fs, open = 512fs (D-C01).
[[nodiscard]] constexpr std::uint32_t masterClockRatio(CmodePin pin) noexcept
{
    switch (pin) {
    case CmodePin::low: return 256;
    case CmodePin::high: return 384;
    case CmodePin::open: return 512;
    }
    return 0;
}

// Serial input modes. Underlying values equal the DIF1:DIF0 pin bits (D-S01 to D-S04).
enum class SerialFormat : std::uint8_t {
    mode0RightJustified16 = 0b00,
    mode1RightJustified20 = 0b01,
    mode2LeftJustified20 = 0b10,
    mode3I2s = 0b11,
};

[[nodiscard]] constexpr SerialFormat serialFormatFromPins(bool dif1, bool dif0) noexcept
{
    return static_cast<SerialFormat>((dif1 ? 0b10u : 0u) | (dif0 ? 0b01u : 0u));
}

// De-emphasis selection. Underlying values equal the DEM1:DEM0 pin bits (D-E02):
// 00 = 44.1 kHz, 01 = OFF, 10 = 48 kHz, 11 = 32 kHz.
enum class DeemphasisSelect : std::uint8_t {
    fs44100 = 0b00,
    off = 0b01,
    fs48000 = 0b10,
    fs32000 = 0b11,
};

[[nodiscard]] constexpr DeemphasisSelect deemphasisFromPins(bool dem1, bool dem0) noexcept
{
    return static_cast<DeemphasisSelect>((dem1 ? 0b10u : 0u) | (dem0 ? 0b01u : 0u));
}

enum class ModelProfile : std::uint8_t {
    specification, // deterministic, datasheet-constrained linear model
    nominalAnalog, // synthetic noise and imperfections at typical targets
    measured,      // fitted to captures of an identified device; prepare rejects
                   // it with invalidConfiguration until a profile exists
};

// Harmonic character of the NominalAnalog distortion. Every shape gives the
// same total distortion at 0 dBFS, 1 kHz (P-96).
enum class DistortionShape : std::uint8_t {
    equal2nd3rd, // equal 2nd- and 3rd-harmonic power (default)
    mostly3rd,   // odd order only, as from a balanced stage
    mostly2nd,   // even order only, as from a single-ended stage
};

// Frequency shape of the NominalAnalog crosstalk; -110 dB at 1 kHz (P-97).
enum class CrosstalkShape : std::uint8_t {
    capacitive, // rising 6 dB per octave, +90 degrees (default)
    flat,       // the same at every frequency, in phase
};

// How the NominalAnalog channel gain mismatch is chosen (P-98).
enum class MismatchMode : std::uint8_t {
    fixed,       // left +0.1 dB, right -0.1 dB (default)
    perInstance, // drawn from noiseSeed around 0.2 dB, at most 0.5 dB
};

// Optional analog imperfections, used only by the NominalAnalog profile.
// Each flag is independent (REQ-SCOPE-03); all are synthetic until measured.
struct AnalogOptions {
    bool noise = false;
    bool distortion = false;
    bool channelMismatch = false;
    bool crosstalk = false;
    bool dcOffset = false;
    DistortionShape distortionShape = DistortionShape::equal2nd3rd;
    CrosstalkShape crosstalkShape = CrosstalkShape::capacitive;
    MismatchMode mismatchMode = MismatchMode::fixed;
    std::uint64_t noiseSeed = 0x4B4135323256464Bull; // persisted in presets (P-91)

    // Every imperfection on except the DC offset, with the default shapes.
    [[nodiscard]] static constexpr AnalogOptions nominal() noexcept
    {
        AnalogOptions o;
        o.noise = o.distortion = o.channelMismatch = o.crosstalk = true;
        return o;
    }
};

// Chip-level configuration, applied by prepare(). Defaults follow P-01; they
// are project defaults, not power-on pin states.
struct DacConfig {
    double dacRate = 44100.0;                 // Hz, [16000, 48000] (D-04)
    CmodePin cmode = CmodePin::low;           // MCLK ratio selection
    std::optional<double> masterClockHz;      // validated against CMODE when set
    ModelProfile profile = ModelProfile::specification;
    AnalogOptions analog{};

    // Pin states latched when reset is released (P-72).
    DeemphasisSelect initialDeemphasis = DeemphasisSelect::off;
    bool initialSoftMute = false;
};

} // namespace ak4522
