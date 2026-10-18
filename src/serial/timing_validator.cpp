// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ross Baker

#include "ak4522/serial.hpp"

#include <array>
#include <cmath>
#include <limits>
#include <utility>

namespace ak4522 {

namespace {

constexpr double unseen = std::numeric_limits<double>::quiet_NaN();

// Slave-mode serial limits in ns ([DS] p.7).
constexpr double sclkPeriodMin = 160.0;  // D-T01
constexpr double sclkWidthMin = 65.0;    // D-T02
constexpr double lrckSclkMin = 45.0;     // D-T03, D-T04
constexpr double sdtiHoldMin = 40.0;     // D-T05
constexpr double sdtiSetupMin = 25.0;    // D-T06
constexpr double dutyMin = 0.45;         // D-T07
constexpr double dutyMax = 0.55;
constexpr double lrckHzMin = 16000.0;    // D-04
constexpr double lrckHzMax = 48000.0;
constexpr double pdWidthMin = 150.0;     // D-R03
// A running clock silent for longer than one LRCK period at the slowest
// documented rate (16 kHz) has stopped (D-C07, P-31).
constexpr double clockStopNs = 1e9 / lrckHzMin;

// Relative tolerance on frequency limits, so a clock exactly at a limit is not
// rejected for floating-point rounding of its timestamps (P-36).
constexpr double frequencyTolerance = 1e-9;

bool frequencyWithin(double hz, double minHz, double maxHz) noexcept
{
    return hz >= minHz * (1.0 - frequencyTolerance) && hz <= maxHz * (1.0 + frequencyTolerance);
}

struct MasterClockLimits {
    double minHz;
    double maxHz;
    double minWidthNs;
};

// MCLK frequency range and minimum pulse width per CMODE ratio (D-C02 to D-C04).
MasterClockLimits masterClockLimits(CmodePin cmode) noexcept
{
    switch (cmode) {
    case CmodePin::low: return {4.096e6, 12.288e6, 27.0};
    case CmodePin::high: return {6.144e6, 18.432e6, 20.0};
    case CmodePin::open: return {8.192e6, 24.576e6, 15.0};
    }
    return {0.0, 0.0, 0.0};
}

bool seen(double t) noexcept
{
    return !std::isnan(t);
}

} // namespace

Status TimingValidator::prepare(CmodePin cmode)
{
    if (static_cast<std::uint8_t>(cmode) > static_cast<std::uint8_t>(CmodePin::open)) {
        return Status::invalidConfiguration;
    }
    cmode_ = cmode;
    reset();
    return Status::ok;
}

void TimingValidator::reset() noexcept
{
    counts_.fill(0);
    lastTimeNs_ = 0.0;
    anyEdge_ = false;
    known_.fill(false);
    level_.fill(false);
    sclkRise_ = sclkFall_ = unseen;
    lrckRise_ = lrckFall_ = lrckEdge_ = unseen;
    sdtiEdge_ = unseen;
    mclkRise_ = mclkFall_ = unseen;
    pdFall_ = unseen;
    pdRise_ = unseen;
    stopped_.fill(false);
    mclkRisesThisLrck_ = 0;
    mclkCounting_ = false;
}

std::uint64_t TimingValidator::violations(TimingViolation kind) const noexcept
{
    const auto index = static_cast<std::size_t>(kind);
    return index < counts_.size() ? counts_[index] : 0;
}

std::uint64_t TimingValidator::totalViolations() const noexcept
{
    std::uint64_t total = 0;
    for (const std::uint64_t value : counts_) {
        total += value;
    }
    return total;
}

void TimingValidator::count(TimingViolation kind) noexcept
{
    ++counts_[static_cast<std::size_t>(kind)];
}

void TimingValidator::observe(const PinEdge& edge) noexcept
{
    const auto pin = static_cast<std::size_t>(edge.pin);
    if (pin >= pinCount || !std::isfinite(edge.timeNs)) {
        return;
    }
    if (anyEdge_ && edge.timeNs < lastTimeNs_) {
        return; // out of order
    }
    if (known_[pin] && level_[pin] == edge.level) {
        return; // not a transition
    }
    anyEdge_ = true;
    checkClocks(edge.timeNs);
    lastTimeNs_ = edge.timeNs;
    known_[pin] = true;
    level_[pin] = edge.level;
    stopped_[pin] = false;

    switch (edge.pin) {
    case Pin::sclk: onSclk(edge.timeNs, edge.level); break;
    case Pin::lrck: onLrck(edge.timeNs, edge.level); break;
    case Pin::sdti: onSdti(edge.timeNs); break;
    case Pin::mclk: onMclk(edge.timeNs, edge.level); break;
    case Pin::pd: onPd(edge.timeNs, edge.level); break;
    }
}

void TimingValidator::onSclk(double t, bool rising) noexcept
{
    if (!rising) {
        if (seen(sclkRise_) && t - sclkRise_ < sclkWidthMin) {
            count(TimingViolation::sclkPulseWidth); // high time
        }
        sclkFall_ = t;
        return;
    }
    if (seen(sclkRise_) && t - sclkRise_ < sclkPeriodMin) {
        count(TimingViolation::sclkPeriod);
    }
    if (seen(sclkFall_) && t - sclkFall_ < sclkWidthMin) {
        count(TimingViolation::sclkPulseWidth); // low time
    }
    if (seen(lrckEdge_)) {
        const double sinceLrck = t - lrckEdge_;
        if (sinceLrck == 0.0) {
            count(TimingViolation::sclkRiseCoincidesWithLrck);
        } else if (sinceLrck < lrckSclkMin) {
            count(TimingViolation::lrckToSclkRise);
        }
    }
    if (seen(sdtiEdge_) && t - sdtiEdge_ < sdtiSetupMin) {
        count(TimingViolation::sdtiSetup);
    }
    sclkRise_ = t;
    // tLRS applies to the first SCLK rise after an LRCK edge only.
    lrckEdge_ = unseen;
}

void TimingValidator::onLrck(double t, bool rising) noexcept
{
    if (seen(sclkRise_)) {
        const double sinceSclk = t - sclkRise_;
        if (sinceSclk == 0.0) {
            count(TimingViolation::sclkRiseCoincidesWithLrck);
        } else if (sinceSclk < lrckSclkMin) {
            count(TimingViolation::sclkRiseToLrck);
        }
    }
    lrckEdge_ = t;

    if (!rising) {
        lrckFall_ = t;
        return;
    }
    if (seen(lrckRise_)) {
        const double period = t - lrckRise_;
        const double hz = 1e9 / period;
        if (!frequencyWithin(hz, lrckHzMin, lrckHzMax)) {
            count(TimingViolation::lrckFrequency);
        }
        if (seen(lrckFall_) && lrckFall_ > lrckRise_) {
            const double duty = (lrckFall_ - lrckRise_) / period;
            if (!(duty >= dutyMin && duty <= dutyMax)) {
                count(TimingViolation::lrckDutyCycle);
            }
        }
        if (mclkCounting_ && mclkRisesThisLrck_ != masterClockRatio(cmode_)) {
            count(TimingViolation::mclkNotSynchronized);
        }
    }
    lrckRise_ = t;
    mclkRisesThisLrck_ = 0;
    // Count MCLK cycles only over LRCK periods that start with MCLK already
    // running, so a stream without MCLK, or one that starts it mid-period, is
    // not reported as unsynchronized.
    mclkCounting_ = seen(mclkRise_);
}

void TimingValidator::onSdti(double t) noexcept
{
    if (seen(sclkRise_) && t - sclkRise_ < sdtiHoldMin) {
        count(TimingViolation::sdtiHold);
    }
    sdtiEdge_ = t;
}

void TimingValidator::onMclk(double t, bool rising) noexcept
{
    const MasterClockLimits limits = masterClockLimits(cmode_);
    if (!rising) {
        if (seen(mclkRise_) && t - mclkRise_ < limits.minWidthNs) {
            count(TimingViolation::mclkPulseWidth);
        }
        mclkFall_ = t;
        return;
    }
    if (seen(mclkFall_) && t - mclkFall_ < limits.minWidthNs) {
        count(TimingViolation::mclkPulseWidth);
    }
    if (seen(mclkRise_)) {
        const double hz = 1e9 / (t - mclkRise_);
        if (!frequencyWithin(hz, limits.minHz, limits.maxHz)) {
            count(TimingViolation::mclkFrequency);
        }
    }
    mclkRise_ = t;
    ++mclkRisesThisLrck_;
}

void TimingValidator::advanceTo(double timeNs) noexcept
{
    if (!anyEdge_ || !std::isfinite(timeNs) || timeNs < lastTimeNs_) {
        return;
    }
    checkClocks(timeNs);
    lastTimeNs_ = timeNs;
}

void TimingValidator::checkClocks(double t) noexcept
{
    // Clocks may stop while PD is low (D-R01); an unobserved PD counts as high.
    const auto pd = static_cast<std::size_t>(Pin::pd);
    if (known_[pd] && !level_[pd]) {
        return;
    }
    const auto latest = [](double a, double b) { return seen(a) && (!seen(b) || a > b) ? a : b; };
    const std::array<std::pair<Pin, double>, 3> clocks{{
        {Pin::mclk, latest(mclkRise_, mclkFall_)},
        {Pin::sclk, latest(sclkRise_, sclkFall_)},
        {Pin::lrck, latest(lrckRise_, lrckFall_)},
    }};
    for (const auto& [clockPin, lastEdge] : clocks) {
        const auto index = static_cast<std::size_t>(clockPin);
        if (!seen(lastEdge) || stopped_[index]) {
            continue; // never running, or already reported
        }
        // After PD is released the clocks have one limit to resume.
        const double since = latest(lastEdge, pdRise_);
        if (t - since > clockStopNs) {
            count(TimingViolation::clockStopped);
            stopped_[index] = true;
        }
    }
}

void TimingValidator::onPd(double t, bool rising) noexcept
{
    if (!rising) {
        pdFall_ = t;
        return;
    }
    pdRise_ = t;
    if (seen(pdFall_) && t - pdFall_ < pdWidthMin) {
        count(TimingViolation::pdPulseWidth);
    }
}

} // namespace ak4522
