// Compile-time checks of the public API contract. These run before any
// implementation exists and pin down documented mappings, defaults, and the
// noexcept guarantees required by REQ-RT-02.

#include "ak4522/ak4522.hpp"

#include <catch2/catch_test_macros.hpp>

#include <type_traits>
#include <utility>

namespace {

using namespace ak4522;

// CMODE ratios (D-C01).
static_assert(masterClockRatio(CmodePin::low) == 256);
static_assert(masterClockRatio(CmodePin::high) == 384);
static_assert(masterClockRatio(CmodePin::open) == 512);

// DIF1:DIF0 mapping (D-S01 to D-S04).
static_assert(serialFormatFromPins(false, false) == SerialFormat::mode0RightJustified16);
static_assert(serialFormatFromPins(false, true) == SerialFormat::mode1RightJustified20);
static_assert(serialFormatFromPins(true, false) == SerialFormat::mode2LeftJustified20);
static_assert(serialFormatFromPins(true, true) == SerialFormat::mode3I2s);

// DEM1:DEM0 mapping (D-E02).
static_assert(deemphasisFromPins(false, false) == DeemphasisSelect::fs44100);
static_assert(deemphasisFromPins(false, true) == DeemphasisSelect::off);
static_assert(deemphasisFromPins(true, false) == DeemphasisSelect::fs48000);
static_assert(deemphasisFromPins(true, true) == DeemphasisSelect::fs32000);

// PCM range (D-P02).
static_assert(Pcm20::minCode == -(1 << 19));
static_assert(Pcm20::maxCode == (1 << 19) - 1);

// Same-frame event priority: reset, then de-emphasis, then mute (P-42).
static_assert(ControlType::powerDown < ControlType::deemphasis);
static_assert(ControlType::deemphasis < ControlType::softMute);

// Default configuration (P-01).
constexpr DacConfig defaultDac{};
static_assert(defaultDac.dacRate == 44100.0);
static_assert(defaultDac.profile == ModelProfile::specification);
static_assert(defaultDac.initialDeemphasis == DeemphasisSelect::off);
static_assert(!defaultDac.initialSoftMute);
static_assert(!defaultDac.analog.noise && !defaultDac.analog.distortion
              && !defaultDac.analog.channelMismatch && !defaultDac.analog.crosstalk
              && !defaultDac.analog.dcOffset);
static_assert(HostConfig{}.inputResolution == InputResolution::bits20);
static_assert(!HostConfig{}.inputDither);
static_assert(HostConfig{}.maxPendingEvents == 256);

// D-02: 8x interpolation.
static_assert(DacCore::oversampling == 8);

// Audio-thread entry points never throw (REQ-RT-02).
static_assert(noexcept(std::declval<DacCore&>().process(
    std::declval<std::span<const Pcm20Frame>>(),
    std::declval<std::span<const ControlEvent>>(),
    std::declval<StereoView<double>>())));
static_assert(noexcept(std::declval<DacCore&>().reset()));
static_assert(noexcept(std::declval<HostProcessor&>().process(
    std::declval<StereoView<const float>>(),
    std::declval<StereoView<float>>(),
    std::declval<std::span<const ControlEvent>>())));
static_assert(noexcept(std::declval<HostProcessor&>().process(
    std::declval<StereoView<const double>>(),
    std::declval<StereoView<double>>(),
    std::declval<std::span<const ControlEvent>>())));
static_assert(noexcept(std::declval<HostProcessor&>().reset()));
static_assert(noexcept(std::declval<DacStream&>().render(std::declval<StereoView<float>>())));
static_assert(noexcept(std::declval<DacStream&>().render(std::declval<StereoView<double>>())));
static_assert(noexcept(std::declval<DacStream&>().push(std::declval<StereoView<const std::int32_t>>(),
                                                       std::declval<std::span<const ControlEvent>>())));
static_assert(noexcept(std::declval<DacStream&>().push(std::declval<StereoView<const float>>(),
                                                       std::declval<std::span<const ControlEvent>>())));
static_assert(noexcept(std::declval<DacStream&>().push(std::declval<StereoView<const double>>(),
                                                       std::declval<std::span<const ControlEvent>>())));
static_assert(noexcept(std::declval<DacStream&>().reset()));
static_assert(noexcept(std::declval<const DacStream&>().dacFramesFor(1)));
static_assert(std::is_nothrow_invocable_v<DacPullCallback, void*, DacPullBlock&>);
static_assert(noexcept(std::declval<SerialDecoder&>().sclkRisingEdge(true, false)));
static_assert(noexcept(std::declval<TimingValidator&>().observe(PinEdge{})));
static_assert(noexcept(quantizeToPcm20(0.0, std::declval<QuantizeCounters&>())));

// Processors are movable but not copyable.
static_assert(!std::is_copy_constructible_v<DacCore>);
static_assert(std::is_nothrow_move_constructible_v<DacCore>);
static_assert(!std::is_copy_constructible_v<HostProcessor>);
static_assert(std::is_nothrow_move_constructible_v<HostProcessor>);
static_assert(!std::is_copy_constructible_v<DacStream>);
static_assert(std::is_nothrow_move_constructible_v<DacStream>);

} // namespace

TEST_CASE("Event factories encode pin levels", "[unit][api]")
{
    CHECK(ControlEvent::softMute(7, true) == ControlEvent{7, ControlType::softMute, 1});
    CHECK(ControlEvent::powerDown(0, false) == ControlEvent{0, ControlType::powerDown, 0});
    CHECK(ControlEvent::deemphasis(3, DeemphasisSelect::fs32000)
          == ControlEvent{3, ControlType::deemphasis, 0b11});
}

TEST_CASE("Stereo views report frame count and consistency", "[unit][api]")
{
    float left[4]{};
    float right[3]{};
    const StereoView<float> matched{left, left};
    const StereoView<float> mismatched{left, right};
    CHECK(matched.frames() == 4);
    CHECK(matched.consistent());
    CHECK_FALSE(mismatched.consistent());
}
