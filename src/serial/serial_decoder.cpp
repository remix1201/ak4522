// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ross Baker

#include "ak4522/serial.hpp"

namespace ak4522 {

namespace {

constexpr std::uint32_t wordBits = 20;

// Minimum bit clocks per channel half-frame ([DS] p.9 Table 2: 32fs for the
// 16-bit and short I2S cases, 40fs for 20-bit modes).
std::uint32_t minimumSlots(SerialFormat format) noexcept
{
    switch (format) {
    case SerialFormat::mode0RightJustified16: return 16;
    case SerialFormat::mode1RightJustified20: return 20;
    case SerialFormat::mode2LeftJustified20: return 20;
    case SerialFormat::mode3I2s: return 16;
    }
    return wordBits;
}

// LRCK level of the left channel: high in modes 0 to 2, low in I2S (D-S01 to D-S04).
bool leftLevel(SerialFormat format) noexcept
{
    return format != SerialFormat::mode3I2s;
}

} // namespace

Status SerialDecoder::prepare(SerialFormat format)
{
    if (static_cast<std::uint8_t>(format) > static_cast<std::uint8_t>(SerialFormat::mode3I2s)) {
        return Status::invalidConfiguration;
    }
    format_ = format;
    reset();
    return Status::ok;
}

void SerialDecoder::reset() noexcept
{
    started_ = false;
    synchronized_ = false;
    startHalf(false);
    left_ = Pcm20{};
    leftValid_ = false;
}

void SerialDecoder::startHalf(bool lrck) noexcept
{
    lrck_ = lrck;
    slots_ = 0;
    tail_ = 0;
    head_ = 0;
    headBits_ = 0;
}

void SerialDecoder::appendBit(bool bit) noexcept
{
    const std::uint32_t value = bit ? 1u : 0u;
    tail_ = (tail_ << 1) | value;
    if (headBits_ < wordBits) {
        head_ = (head_ << 1) | value;
        ++headBits_;
    }
}

SerialEdgeResult SerialDecoder::finishHalf() noexcept
{
    const bool isLeft = lrck_ == leftLevel(format_);
    const bool valid = slots_ >= minimumSlots(format_);

    Pcm20 word{};
    if (valid) {
        switch (format_) {
        case SerialFormat::mode0RightJustified16:
            word = pcm20FromInt16(static_cast<std::int16_t>(static_cast<std::uint16_t>(tail_ & 0xFFFFu)));
            break;
        case SerialFormat::mode1RightJustified20:
            word = pcm20FromRaw(tail_);
            break;
        case SerialFormat::mode2LeftJustified20:
        case SerialFormat::mode3I2s:
            // Left-align the received bits; missing low bits are zero.
            word = pcm20FromRaw(head_ << (wordBits - headBits_));
            break;
        }
    }

    SerialEdgeResult result;
    if (isLeft) {
        left_ = word;
        leftValid_ = valid;
        if (!valid) {
            result.event = SerialEvent::sclkRatioTooLow;
        }
        return result;
    }

    if (!valid) {
        result.event = SerialEvent::sclkRatioTooLow;
    } else if (!leftValid_) {
        result.event = SerialEvent::incompleteFrame;
    } else {
        result.event = SerialEvent::frameComplete;
        result.frame = Pcm20Frame{left_, word};
    }
    // leftValid_ needs no clearing here: LRCK alternates, so the next half is
    // a left half and overwrites it.
    return result;
}

SerialEdgeResult SerialDecoder::sclkRisingEdge(bool lrck, bool sdti) noexcept
{
    const bool i2s = format_ == SerialFormat::mode3I2s;

    if (!started_) {
        // The half-frame in progress at reset may be partial; skip to its end.
        started_ = true;
        lrck_ = lrck;
        return {};
    }

    if (lrck == lrck_) {
        if (synchronized_) {
            appendBit(sdti);
            ++slots_;
        }
        return {};
    }

    SerialEdgeResult result;
    if (synchronized_) {
        if (i2s) {
            appendBit(sdti); // the ending word's final bit follows the LRCK edge (D-S07)
        }
        result = finishHalf();
    }
    synchronized_ = true;
    startHalf(lrck);
    if (!i2s) {
        appendBit(sdti); // modes 0 to 2: the new half's first bit clock carries data
    }
    slots_ = 1;
    return result;
}

} // namespace ak4522
