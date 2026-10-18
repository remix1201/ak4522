// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ross Baker

// SDTO encoder (REQ-ADC-04; AD-11, AD-13, AD-15, AD-16, DISC-10; P-109). Slot
// positions are checked against Figs.1 to 4 directly; modes 2 and 3 also
// round-trip through SerialDecoder, whose SDTI formats match.

#include "ak4522/serial.hpp"
#include "support/allocation_counter.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <random>
#include <vector>

using namespace ak4522;

namespace {

SerialEncoder prepared(SerialFormat format)
{
    SerialEncoder e;
    REQUIRE(e.prepare(format) == Status::ok);
    return e;
}

bool isI2s(SerialFormat f)
{
    return f == SerialFormat::mode3I2s;
}

// LRCK level of half h: halves alternate left, right; left is high except in I2S.
bool lrckFor(SerialFormat f, std::size_t half)
{
    const bool left = half % 2 == 0;
    return isI2s(f) ? !left : left;
}

// Sends frames, `slots` bit clocks per half, after one lead-in half-frame
// (which only teaches the encoder the LRCK level). Returns bits[half][slot].
std::vector<std::vector<bool>> send(SerialEncoder& e, SerialFormat f, const std::vector<Pcm20Frame>& frames,
                                    std::size_t slots)
{
    for (std::size_t s = 0; s < slots; ++s) {
        static_cast<void>(e.sclkFallingEdge(lrckFor(f, 1))); // lead-in: a right half
    }
    std::vector<std::vector<bool>> bits;
    for (std::size_t half = 0; half < 2 * frames.size() + 1; ++half) {
        if (half % 2 == 0 && half / 2 < frames.size()) {
            e.setFrame(frames[half / 2]);
        }
        std::vector<bool> row;
        for (std::size_t s = 0; s < slots; ++s) {
            row.push_back(e.sclkFallingEdge(lrckFor(f, half)));
        }
        bits.push_back(row);
    }
    return bits;
}

bool bitOf(std::int32_t code, int b)
{
    return ((static_cast<std::uint32_t>(code) >> b) & 1u) != 0;
}

const std::vector<Pcm20Frame> sample{{Pcm20{0x5A3C7}, Pcm20{-123457}}, {Pcm20{Pcm20::maxCode}, Pcm20{Pcm20::minCode}}};

} // namespace

TEST_CASE("Modes 0 to 2 at 64fs: bits 19 to 0 from the LRCK edge, then 0", "[unit][serial][sdto]")
{
    const SerialFormat f = GENERATE(SerialFormat::mode0RightJustified16, SerialFormat::mode1RightJustified20,
                                    SerialFormat::mode2LeftJustified20);
    CAPTURE(static_cast<int>(f));
    SerialEncoder e = prepared(f);
    const auto bits = send(e, f, sample, 32);
    for (std::size_t half = 0; half < 4; ++half) {
        const Pcm20 word = half % 2 == 0 ? sample[half / 2].left : sample[half / 2].right;
        for (std::size_t s = 0; s < 32; ++s) {
            const bool expected = s < 20 && bitOf(word.code, 19 - static_cast<int>(s)); // AD-15
            REQUIRE(bits[half][s] == expected);
        }
    }
}

TEST_CASE("Mode 0 at 32fs carries bits 19 to 4", "[unit][serial][sdto]")
{
    SerialEncoder e = prepared(SerialFormat::mode0RightJustified16);
    const auto bits = send(e, SerialFormat::mode0RightJustified16, sample, 16);
    for (std::size_t half = 0; half < 4; ++half) {
        const Pcm20 word = half % 2 == 0 ? sample[half / 2].left : sample[half / 2].right;
        for (std::size_t s = 0; s < 16; ++s) {
            REQUIRE(bits[half][s] == bitOf(word.code, 19 - static_cast<int>(s))); // AD-16, DISC-10
        }
    }
}

TEST_CASE("Mode 3 at 64fs: bit 19 one clock after the edge through bit 0 in slot 20", "[unit][serial][sdto]")
{
    SerialEncoder e = prepared(SerialFormat::mode3I2s);
    const auto bits = send(e, SerialFormat::mode3I2s, sample, 32);
    for (std::size_t half = 0; half < 4; ++half) {
        const Pcm20 word = half % 2 == 0 ? sample[half / 2].left : sample[half / 2].right;
        for (std::size_t s = 0; s < 32; ++s) {
            const bool expected = s >= 1 && s <= 20 && bitOf(word.code, 20 - static_cast<int>(s)); // AD-15
            REQUIRE(bits[half][s] == expected);
        }
    }
}

TEST_CASE("Mode 3 at 32fs: bits 19 to 5, then bit 4 in the next half's first clock", "[unit][serial][sdto]")
{
    SerialEncoder e = prepared(SerialFormat::mode3I2s);
    const auto bits = send(e, SerialFormat::mode3I2s, sample, 16);
    for (std::size_t half = 0; half < 4; ++half) {
        const Pcm20 word = half % 2 == 0 ? sample[half / 2].left : sample[half / 2].right;
        for (std::size_t s = 1; s < 16; ++s) {
            REQUIRE(bits[half][s] == bitOf(word.code, 20 - static_cast<int>(s)));
        }
        REQUIRE(bits[half + 1][0] == bitOf(word.code, 4)); // AD-16 (Fig.4)
    }
}

TEST_CASE("Modes 2 and 3 round-trip through the serial decoder", "[unit][serial][sdto]")
{
    struct Case {
        SerialFormat format;
        std::size_t slots;
        bool truncated; // 16 bits carried
    };
    const Case c = GENERATE(Case{SerialFormat::mode2LeftJustified20, 20, false},
                            Case{SerialFormat::mode2LeftJustified20, 32, false},
                            Case{SerialFormat::mode2LeftJustified20, 64, false},
                            Case{SerialFormat::mode3I2s, 24, false}, Case{SerialFormat::mode3I2s, 32, false},
                            Case{SerialFormat::mode3I2s, 16, true});
    CAPTURE(static_cast<int>(c.format), c.slots);
    std::mt19937_64 rng(4);
    std::uniform_int_distribution<std::int32_t> code(Pcm20::minCode, Pcm20::maxCode);
    std::vector<Pcm20Frame> frames(200);
    for (auto& fr : frames) {
        fr = {Pcm20{code(rng)}, Pcm20{code(rng)}};
    }
    frames[0] = {Pcm20{Pcm20::minCode}, Pcm20{Pcm20::maxCode}};

    SerialEncoder enc = prepared(c.format);
    SerialDecoder dec;
    REQUIRE(dec.prepare(c.format) == Status::ok);
    std::vector<Pcm20Frame> decoded;
    // The encoder changes SDTO on a falling edge; the decoder latches it on
    // the following rising edge, with the same LRCK level.
    auto clock = [&](bool lrck) {
        const bool sdto = enc.sclkFallingEdge(lrck);
        const SerialEdgeResult r = dec.sclkRisingEdge(lrck, sdto);
        if (r.frame) {
            decoded.push_back(*r.frame);
        }
    };
    for (std::size_t s = 0; s < c.slots; ++s) {
        clock(lrckFor(c.format, 1));
    }
    for (std::size_t half = 0; half < 2 * frames.size() + 2; ++half) {
        if (half % 2 == 0 && half / 2 < frames.size()) {
            enc.setFrame(frames[half / 2]);
        }
        for (std::size_t s = 0; s < c.slots; ++s) {
            clock(lrckFor(c.format, half));
        }
    }
    REQUIRE(decoded.size() >= frames.size());
    for (std::size_t k = 0; k < frames.size(); ++k) {
        Pcm20Frame expected = frames[k];
        if (c.truncated) {
            expected.left.code &= ~0xF; // the low four bits are not carried (AD-16)
            expected.right.code &= ~0xF;
        }
        REQUIRE(decoded[k] == expected);
    }
}

TEST_CASE("SDTO is 0 until the first LRCK edge, and a frame repeats until replaced", "[unit][serial][sdto]")
{
    const SerialFormat f = SerialFormat::mode2LeftJustified20;
    SerialEncoder e = prepared(f);
    e.setFrame({Pcm20{Pcm20::minCode}, Pcm20{Pcm20::minCode}}); // MSB set
    for (int s = 0; s < 32; ++s) {
        CHECK_FALSE(e.sclkFallingEdge(true)); // no edge yet: a possibly partial half
    }
    CHECK_FALSE(e.sclkFallingEdge(false)); // right half of the empty frame
    for (int s = 1; s < 32; ++s) {
        static_cast<void>(e.sclkFallingEdge(false));
    }
    CHECK(e.sclkFallingEdge(true)); // left half: the frame's MSB
    for (int s = 1; s < 32; ++s) {
        static_cast<void>(e.sclkFallingEdge(true));
    }
    for (int s = 0; s < 32; ++s) {
        static_cast<void>(e.sclkFallingEdge(false));
    }
    CHECK(e.sclkFallingEdge(true)); // not replaced: sent again (P-109)

    e.reset();
    CHECK_FALSE(e.sclkFallingEdge(false));
    CHECK_FALSE(e.sclkFallingEdge(true)); // after reset: the frame is gone
}

TEST_CASE("Encoder prepare rejects an invalid format; encoding does not allocate", "[unit][serial][sdto][rt]")
{
    SerialEncoder e;
    CHECK(e.prepare(static_cast<SerialFormat>(4)) == Status::invalidConfiguration);
    REQUIRE(e.prepare(SerialFormat::mode3I2s) == Status::ok);
    test::AllocationCounter counter;
    for (int i = 0; i < 1000; ++i) {
        e.setFrame({Pcm20{i}, Pcm20{-i}});
        static_cast<void>(e.sclkFallingEdge(i % 64 < 32));
    }
    e.reset();
    CHECK(counter.allocations() == 0);
}
