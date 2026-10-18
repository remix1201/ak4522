// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ross Baker

#include "ak4522/serial.hpp"

namespace ak4522 {

namespace {

// The 20-bit two's complement word of a code.
std::uint32_t word20(Pcm20 sample) noexcept
{
    return static_cast<std::uint32_t>(sample.code) & 0xFFFFFu;
}

} // namespace

Status SerialEncoder::prepare(SerialFormat format)
{
    if (static_cast<std::uint8_t>(format) > static_cast<std::uint8_t>(SerialFormat::mode3I2s)) {
        return Status::invalidConfiguration;
    }
    format_ = format;
    reset();
    return Status::ok;
}

void SerialEncoder::reset() noexcept
{
    levelKnown_ = false;
    synchronized_ = false;
    lrck_ = false;
    slot_ = 0;
    word_ = 0;
    delayed_ = false;
    pending_ = {};
    current_ = {};
}

void SerialEncoder::setFrame(const Pcm20Frame& frame) noexcept
{
    pending_ = frame;
}

bool SerialEncoder::msbJustifiedBit() const noexcept
{
    return slot_ < 20 && ((word_ >> (19 - slot_)) & 1u) != 0;
}

bool SerialEncoder::sclkFallingEdge(bool lrck) noexcept
{
    const bool i2s = format_ == SerialFormat::mode3I2s;
    if (!levelKnown_) {
        // The first call only learns the LRCK level; the half-frame it is in
        // may be partial, so nothing is sent until the next edge.
        levelKnown_ = true;
        lrck_ = lrck;
        return false;
    }
    if (lrck != lrck_) {
        synchronized_ = true;
        lrck_ = lrck;
        slot_ = 0;
        const bool left = i2s ? !lrck : lrck; // Table 2 L/R levels (AD-11)
        if (left) {
            current_ = pending_; // a new frame starts with its left word (P-109)
        }
        word_ = word20(left ? current_.left : current_.right);
    } else if (synchronized_) {
        ++slot_;
    } else {
        return false; // no LRCK edge yet since reset
    }
    const bool bit = msbJustifiedBit();
    if (!i2s) {
        return bit;
    }
    const bool out = delayed_; // one bit clock later (AD-15, AD-16)
    delayed_ = bit;
    return out;
}

} // namespace ak4522
