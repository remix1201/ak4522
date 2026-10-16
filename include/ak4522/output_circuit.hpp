#pragma once

// The datasheet's example external analog circuit (Fig.11, [DS] p.16): an
// AC-coupled inverting op-amp stage after each DAC output. Board behavior,
// not part of the chip; DacCore never applies it (REQ-ANALOG-02). HostProcessor
// and DacStream can apply it at the host rate on request.

#include "ak4522/buffers.hpp"
#include "ak4522/status.hpp"

namespace ak4522 {

struct ExampleCircuitOptions {
    // The stage gain -18k / 10k = -1.8 (5.1 dB; the figure's "6 dB" label
    // disagrees with its components, DISC-05). false keeps the DAC's level and
    // polarity handling below, for listening without the 5.1 dB rise.
    bool includeGain = true;
    // The stage inverts (D-O02). false restores the DAC's polarity.
    bool invert = true;
    // An external load in ohms, in parallel with the 27 kohm output resistor;
    // 0 means none. A lower load raises the output coupling's corner.
    double loadOhms = 0.0;
};

// Two first-order high-pass sections around an ideal gain (P-104):
//   27 uF into the 10 kohm input resistor, corner 0.589 Hz;
//   10 uF into 27 kohm (and any load), corner 0.589 Hz with no load.
// Input and output are in DAC units, where 1.0 is the DAC's nominal peak
// (1.45 V at VREFH = 5 V); the output is centered on 0 V, so DC offsets are
// removed. Not modeled: op-amp noise, bandwidth, and output swing; supply
// limits. Real-time safe after prepare.
class ExampleOutputCircuit {
public:
    // Component values from Fig.11.
    static constexpr double inputCapacitance = 27e-6;  // F
    static constexpr double inputResistance = 10e3;    // ohm
    static constexpr double feedbackResistance = 18e3; // ohm
    static constexpr double outputCapacitance = 10e-6; // F
    static constexpr double outputResistance = 27e3;   // ohm

    // Validates the options and designs the sections for `sampleRate` Hz.
    [[nodiscard]] Status prepare(double sampleRate, const ExampleCircuitOptions& options = {});
    void reset() noexcept;

    // In place, any frame count.
    void process(StereoView<double> audio) noexcept;
    void process(StereoView<float> audio) noexcept;
    void processFrame(double& left, double& right) noexcept;

    [[nodiscard]] double gain() const noexcept { return gain_; }
    [[nodiscard]] double inputCornerHz() const noexcept { return inputCornerHz_; }
    [[nodiscard]] double outputCornerHz() const noexcept { return outputCornerHz_; }

private:
    struct HighPass {
        double b0 = 1.0; // y = b0 (x - x1) - a1 y1
        double a1 = 0.0;
        void design(double cornerHz, double sampleRate) noexcept;
    };
    struct State {
        double x1 = 0.0, y1 = 0.0; // input section
        double u1 = 0.0, v1 = 0.0; // output section
    };
    [[nodiscard]] double step(State& s, double x) const noexcept;

    HighPass input_;
    HighPass output_;
    double gain_ = -1.8;
    double inputCornerHz_ = 0.0;
    double outputCornerHz_ = 0.0;
    State left_;
    State right_;
};

} // namespace ak4522
