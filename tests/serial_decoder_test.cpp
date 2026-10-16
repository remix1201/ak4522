// Serial SDTI decoder (REQ-SERIAL-01, REQ-SERIAL-03; D-S01 to D-S11, I-T01;
// P-20, P-21, P-23). Bit streams come from an encoder in this file that
// places each bit by its slot number, following [DS] Figures 1 to 4. It does
// not share code or method with the decoder.

#include "ak4522/serial.hpp"
#include "support/allocation_counter.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <algorithm>
#include <cstdint>
#include <optional>
#include <random>
#include <vector>

using namespace ak4522;

namespace {

struct Edge {
    bool lrck;
    bool sdti;
};

struct Half {
    std::uint32_t word;  // 20-bit two's complement bits (16-bit for mode 0)
    std::uint32_t slots; // bit clocks in this half-frame
};

bool bitOf(std::uint32_t word, int index)
{
    return ((word >> index) & 1u) != 0;
}

bool leftLevel(SerialFormat format)
{
    return format != SerialFormat::mode3I2s;
}

// Data bit for slot s of a half, or nullopt for padding (don't care).
// Slot positions follow [DS] Figures 1 to 4.
std::optional<bool> slotBit(SerialFormat format, const Half& half, std::uint32_t s,
                            const Half* previous)
{
    const auto n = half.slots;
    switch (format) {
    case SerialFormat::mode0RightJustified16: // Fig.1: LSB just before the boundary
        if (s + 16 >= n) {
            return bitOf(half.word, static_cast<int>(15 - (s - (n - 16))));
        }
        return std::nullopt;
    case SerialFormat::mode1RightJustified20: // Fig.2
        if (s + 20 >= n) {
            return bitOf(half.word, static_cast<int>(19 - (s - (n - 20))));
        }
        return std::nullopt;
    case SerialFormat::mode2LeftJustified20: // Fig.3: MSB at the boundary
        if (s < 20) {
            return bitOf(half.word, static_cast<int>(19 - s));
        }
        return std::nullopt;
    case SerialFormat::mode3I2s: { // Fig.4: MSB one bit clock after the boundary
        if (s == 0) {
            // Slot 0 carries the previous word's bit at data index n_prev - 1.
            if (previous == nullptr) {
                return std::nullopt;
            }
            const std::uint32_t index = previous->slots - 1;
            const std::uint32_t k = std::min<std::uint32_t>(20, previous->slots);
            if (index < k) {
                return bitOf(previous->word, static_cast<int>(19 - index));
            }
            return std::nullopt;
        }
        const std::uint32_t index = s - 1;
        const std::uint32_t k = std::min<std::uint32_t>(20, n);
        if (index < k) {
            return bitOf(half.word, static_cast<int>(19 - index));
        }
        return std::nullopt;
    }
    }
    return std::nullopt;
}

// Builds edges for a partial lead-in half, the given halves (left first),
// and a closing half that ends the last word. Padding bits are random.
std::vector<Edge> encode(SerialFormat format, const std::vector<Half>& halves,
                         std::mt19937_64& rng, std::uint32_t leadIn = 5)
{
    std::bernoulli_distribution coin;
    const bool left = leftLevel(format);
    std::vector<Edge> edges;
    for (std::uint32_t s = 0; s < leadIn; ++s) {
        edges.push_back({!left, coin(rng)});
    }
    for (std::size_t h = 0; h < halves.size(); ++h) {
        const bool level = (h % 2 == 0) ? left : !left;
        const Half* previous = h > 0 ? &halves[h - 1] : nullptr;
        for (std::uint32_t s = 0; s < halves[h].slots; ++s) {
            const auto bit = slotBit(format, halves[h], s, previous);
            edges.push_back({level, bit ? *bit : coin(rng)});
        }
    }
    // Closing edge: next half's slot 0 (carries the last I2S bit).
    const bool closing = (halves.size() % 2 == 0) ? left : !left;
    const Half dummy{0, 64};
    const auto bit = slotBit(format, dummy, 0, halves.empty() ? nullptr : &halves.back());
    edges.push_back({closing, bit ? *bit : coin(rng)});
    return edges;
}

struct Decoded {
    std::vector<Pcm20Frame> frames;
    std::vector<SerialEvent> events; // non-none, non-frame events
};

Decoded decode(SerialFormat format, const std::vector<Edge>& edges)
{
    SerialDecoder decoder;
    REQUIRE(decoder.prepare(format) == Status::ok);
    Decoded out;
    for (const Edge& edge : edges) {
        const SerialEdgeResult result = decoder.sclkRisingEdge(edge.lrck, edge.sdti);
        if (result.event == SerialEvent::frameComplete) {
            REQUIRE(result.frame.has_value());
            out.frames.push_back(*result.frame);
        } else {
            REQUIRE_FALSE(result.frame.has_value());
            if (result.event != SerialEvent::none) {
                out.events.push_back(result.event);
            }
        }
    }
    return out;
}

// Expected decoded value of a transmitted word.
Pcm20 expected(SerialFormat format, const Half& half)
{
    if (format == SerialFormat::mode0RightJustified16) {
        return Pcm20{static_cast<std::int16_t>(static_cast<std::uint16_t>(half.word)) * 16};
    }
    std::uint32_t bits = half.word & 0xFFFFFu;
    if (format == SerialFormat::mode3I2s && half.slots < 20) {
        bits &= ~((1u << (20 - half.slots)) - 1u); // low bits not transmitted (D-S06)
    }
    const auto value = static_cast<std::int32_t>(bits);
    return Pcm20{(bits & 0x80000u) != 0 ? value - 0x100000 : value};
}

std::vector<std::uint32_t> boundaryWords(SerialFormat format)
{
    if (format == SerialFormat::mode0RightJustified16) {
        return {0x8000u, 0x7FFFu, 0x0000u, 0xFFFFu, 0x0001u, 0xAAAAu, 0x5555u, 0x8001u};
    }
    return {0x80000u, 0x7FFFFu, 0x00000u, 0xFFFFFu, 0x00001u, 0xAAAAAu, 0x55555u, 0x80001u};
}

} // namespace

TEST_CASE("Encoder follows the datasheet figures", "[unit][serial]")
{
    const std::uint32_t a = 0xA5C3Fu; // left word
    const std::uint32_t b = 0x3C96Au; // right word

    SECTION("Fig.1 mode 0 at 32fs: bits 15 to 0 fill the channel")
    {
        const Half half{0xBEEFu, 16};
        for (std::uint32_t s = 0; s < 16; ++s) {
            CHECK(slotBit(SerialFormat::mode0RightJustified16, half, s, nullptr)
                  == bitOf(0xBEEFu, static_cast<int>(15 - s)));
        }
    }
    SECTION("Fig.2 mode 1 at 64fs: 12 don't-care slots, then bits 19 to 0")
    {
        const Half half{a, 32};
        for (std::uint32_t s = 0; s < 12; ++s) {
            CHECK_FALSE(slotBit(SerialFormat::mode1RightJustified20, half, s, nullptr).has_value());
        }
        for (std::uint32_t s = 12; s < 32; ++s) {
            CHECK(slotBit(SerialFormat::mode1RightJustified20, half, s, nullptr)
                  == bitOf(a, static_cast<int>(31 - s)));
        }
    }
    SECTION("Fig.3 mode 2 at 64fs: bits 19 to 0, then don't care")
    {
        const Half half{a, 32};
        CHECK(slotBit(SerialFormat::mode2LeftJustified20, half, 0, nullptr) == bitOf(a, 19));
        CHECK(slotBit(SerialFormat::mode2LeftJustified20, half, 19, nullptr) == bitOf(a, 0));
        CHECK_FALSE(slotBit(SerialFormat::mode2LeftJustified20, half, 20, nullptr).has_value());
    }
    SECTION("Fig.4 mode 3 at 32fs: slot 0 is the previous word's bit 4, then 19 to 5")
    {
        const Half left{a, 16};
        const Half right{b, 16};
        CHECK(slotBit(SerialFormat::mode3I2s, right, 0, &left) == bitOf(a, 4));
        for (std::uint32_t s = 1; s < 16; ++s) {
            CHECK(slotBit(SerialFormat::mode3I2s, right, s, &left)
                  == bitOf(b, static_cast<int>(20 - s)));
        }
    }
    SECTION("Fig.4 mode 3 at 64fs: slot 0 don't care, bits 19 to 0 in slots 1 to 20")
    {
        const Half left{a, 32};
        const Half right{b, 32};
        CHECK_FALSE(slotBit(SerialFormat::mode3I2s, right, 0, &left).has_value());
        CHECK(slotBit(SerialFormat::mode3I2s, right, 1, &left) == bitOf(b, 19));
        CHECK(slotBit(SerialFormat::mode3I2s, right, 20, &left) == bitOf(b, 0));
        CHECK_FALSE(slotBit(SerialFormat::mode3I2s, right, 21, &left).has_value());
    }
}

TEST_CASE("Every mode decodes boundary and random words at every valid SCLK ratio", "[unit][serial]")
{
    // Bit clocks per channel half: 32fs = 16, 40fs = 20, 64fs = 32, 128fs = 64.
    const auto [format, slots] = GENERATE(table<SerialFormat, std::uint32_t>({
        {SerialFormat::mode0RightJustified16, 16},
        {SerialFormat::mode0RightJustified16, 20},
        {SerialFormat::mode0RightJustified16, 32},
        {SerialFormat::mode0RightJustified16, 64},
        {SerialFormat::mode1RightJustified20, 20},
        {SerialFormat::mode1RightJustified20, 24},
        {SerialFormat::mode1RightJustified20, 32},
        {SerialFormat::mode1RightJustified20, 64},
        {SerialFormat::mode2LeftJustified20, 20},
        {SerialFormat::mode2LeftJustified20, 24},
        {SerialFormat::mode2LeftJustified20, 32},
        {SerialFormat::mode2LeftJustified20, 64},
        {SerialFormat::mode3I2s, 16},
        {SerialFormat::mode3I2s, 18},
        {SerialFormat::mode3I2s, 20},
        {SerialFormat::mode3I2s, 21},
        {SerialFormat::mode3I2s, 32},
        {SerialFormat::mode3I2s, 64},
    }));
    CAPTURE(static_cast<int>(format), slots);

    std::mt19937_64 rng(slots * 31 + static_cast<unsigned>(format));
    const std::uint32_t mask = format == SerialFormat::mode0RightJustified16 ? 0xFFFFu : 0xFFFFFu;
    std::uniform_int_distribution<std::uint32_t> random(0, mask);

    std::vector<Half> halves;
    for (const std::uint32_t word : boundaryWords(format)) {
        halves.push_back({word, slots});
    }
    for (int i = 0; i < 200; ++i) {
        halves.push_back({random(rng), slots});
    }
    if (halves.size() % 2 != 0) {
        halves.push_back({0, slots});
    }

    const Decoded result = decode(format, encode(format, halves, rng));
    CHECK(result.events.empty());
    REQUIRE(result.frames.size() == halves.size() / 2);
    for (std::size_t f = 0; f < result.frames.size(); ++f) {
        CAPTURE(f);
        REQUIRE(result.frames[f].left == expected(format, halves[2 * f]));
        REQUIRE(result.frames[f].right == expected(format, halves[2 * f + 1]));
    }
}

TEST_CASE("Unequal left and right half lengths decode correctly", "[unit][serial]")
{
    const SerialFormat format = GENERATE(SerialFormat::mode0RightJustified16,
                                         SerialFormat::mode1RightJustified20,
                                         SerialFormat::mode2LeftJustified20, SerialFormat::mode3I2s);
    CAPTURE(static_cast<int>(format));
    std::mt19937_64 rng(42);
    const std::vector<Half> halves{
        {0x12345u & 0xFFFFu, 32}, {0x0ABCDu & 0xFFFFu, 33},
        {0x7FFFFu & 0xFFFFu, 31}, {0x80000u & 0xFFFFu, 32},
        {0x55555u & 0xFFFFu, 64}, {0xAAAAAu & 0xFFFFu, 21},
    };
    std::vector<Half> words = halves;
    if (format != SerialFormat::mode0RightJustified16) {
        const std::uint32_t full[] = {0x12345u, 0x0ABCDu, 0x7FFFFu, 0x80000u, 0x55555u, 0xAAAAAu};
        for (std::size_t i = 0; i < words.size(); ++i) {
            words[i].word = full[i];
        }
    }
    const Decoded result = decode(format, encode(format, words, rng));
    CHECK(result.events.empty());
    REQUIRE(result.frames.size() == 3);
    for (std::size_t f = 0; f < 3; ++f) {
        CHECK(result.frames[f].left == expected(format, words[2 * f]));
        CHECK(result.frames[f].right == expected(format, words[2 * f + 1]));
    }
}

TEST_CASE("I2S at 32fs carries the final bit across the LRCK edge", "[unit][serial]")
{
    // Only the closing edge's SDTI differs between the two streams; it is
    // bit 4 of the right word, so it must change the decoded value by 16.
    std::mt19937_64 rng(3);
    const std::vector<Half> halves{{0x00000u, 16}, {0x00000u, 16}};
    auto edges = encode(SerialFormat::mode3I2s, halves, rng);
    REQUIRE_FALSE(edges.back().sdti);
    const Decoded zero = decode(SerialFormat::mode3I2s, edges);
    edges.back().sdti = true;
    const Decoded withBit = decode(SerialFormat::mode3I2s, edges);
    REQUIRE(zero.frames.size() == 1);
    REQUIRE(withBit.frames.size() == 1);
    CHECK(zero.frames[0].right.code == 0);
    CHECK(withBit.frames[0].right.code == 16);
    CHECK(withBit.frames[0].left.code == 0);
}

TEST_CASE("The partial half-frame at startup is discarded silently", "[unit][serial]")
{
    const std::uint32_t leadIn = GENERATE(1u, 2u, 7u, 40u);
    std::mt19937_64 rng(leadIn);
    const std::vector<Half> halves{{0x11111u, 32}, {0x22222u, 32}};
    const Decoded result = decode(SerialFormat::mode2LeftJustified20,
                                  encode(SerialFormat::mode2LeftJustified20, halves, rng, leadIn));
    CHECK(result.events.empty());
    REQUIRE(result.frames.size() == 1);
    CHECK(result.frames[0].left.code == 0x11111);
}

TEST_CASE("Starting on the right channel drops that frame", "[unit][serial]")
{
    // Lead-in at the left level means the first full half is the right channel.
    std::mt19937_64 rng(9);
    std::vector<Edge> edges;
    for (int s = 0; s < 4; ++s) {
        edges.push_back({true, false}); // partial left half
    }
    const std::vector<Half> halves{{0x0F0F0u, 32}, {0x12345u, 32}, {0x54321u, 32}};
    // Encode as if starting from the right channel: build left-first and drop
    // the generated lead-in, then swap levels by prefixing our own.
    std::vector<Half> shifted{{0, 32}};
    shifted.insert(shifted.end(), halves.begin(), halves.end());
    auto body = encode(SerialFormat::mode2LeftJustified20, shifted, rng, 0);
    body.erase(body.begin(), body.begin() + 32); // remove the dummy left half
    edges.insert(edges.end(), body.begin(), body.end());

    const Decoded result = decode(SerialFormat::mode2LeftJustified20, edges);
    REQUIRE(result.events.size() == 1);
    CHECK(result.events[0] == SerialEvent::incompleteFrame);
    REQUIRE(result.frames.size() == 1);
    CHECK(result.frames[0].left.code == 0x12345);
    CHECK(result.frames[0].right.code == 0x54321);
}

TEST_CASE("Too few bit clocks per channel are reported and discarded", "[unit][serial]")
{
    const auto [format, slots] = GENERATE(table<SerialFormat, std::uint32_t>({
        {SerialFormat::mode0RightJustified16, 15},
        {SerialFormat::mode1RightJustified20, 16}, // 32fs is below the 40fs minimum
        {SerialFormat::mode1RightJustified20, 19},
        {SerialFormat::mode2LeftJustified20, 16},
        {SerialFormat::mode3I2s, 15},
    }));
    CAPTURE(static_cast<int>(format), slots);
    std::mt19937_64 rng(5);
    const std::vector<Half> halves{{0x1234u, slots}, {0x4321u, slots}};
    const Decoded result = decode(format, encode(format, halves, rng));
    CHECK(result.frames.empty());
    REQUIRE(result.events.size() == 2);
    CHECK(result.events[0] == SerialEvent::sclkRatioTooLow);
    CHECK(result.events[1] == SerialEvent::sclkRatioTooLow);
}

TEST_CASE("A short left word invalidates only its own frame", "[unit][serial]")
{
    std::mt19937_64 rng(6);
    const std::vector<Half> halves{
        {0x11111u, 12}, {0x22222u, 32}, {0x33333u, 32}, {0x44444u, 32}};
    const Decoded result = decode(SerialFormat::mode1RightJustified20,
                                  encode(SerialFormat::mode1RightJustified20, halves, rng));
    REQUIRE(result.events.size() == 2);
    CHECK(result.events[0] == SerialEvent::sclkRatioTooLow);
    CHECK(result.events[1] == SerialEvent::incompleteFrame);
    REQUIRE(result.frames.size() == 1);
    CHECK(result.frames[0].left.code == 0x33333);
    CHECK(result.frames[0].right.code == 0x44444);
}

TEST_CASE("Reset discards a partial word and resynchronizes", "[unit][serial]")
{
    std::mt19937_64 rng(8);
    const std::vector<Half> halves{{0x0AAAAu, 32}, {0x05555u, 32}};
    const auto edges = encode(SerialFormat::mode2LeftJustified20, halves, rng);

    SerialDecoder decoder;
    REQUIRE(decoder.prepare(SerialFormat::mode2LeftJustified20) == Status::ok);
    for (std::size_t i = 0; i < 40; ++i) { // stop mid-way through the right half
        static_cast<void>(decoder.sclkRisingEdge(edges[i].lrck, edges[i].sdti));
    }
    decoder.reset();
    std::vector<Pcm20Frame> frames;
    for (const Edge& edge : edges) {
        const auto result = decoder.sclkRisingEdge(edge.lrck, edge.sdti);
        if (result.frame) {
            frames.push_back(*result.frame);
        }
    }
    REQUIRE(frames.size() == 1);
    CHECK(frames[0].left.code == 0x0AAAA);
    CHECK(frames[0].right.code == 0x05555);
}

TEST_CASE("Reset forgets a completed left word", "[unit][serial]")
{
    // Reset right after a left word completes, then resume on the right
    // channel: the old left word must not pair with the new right word.
    std::mt19937_64 rng(11);
    const std::vector<Half> before{{0x13579u, 32}, {0x2468Au, 32}};
    const auto first = encode(SerialFormat::mode2LeftJustified20, before, rng);

    SerialDecoder decoder;
    REQUIRE(decoder.prepare(SerialFormat::mode2LeftJustified20) == Status::ok);
    // Lead-in (5) + left half (32) + first right bit clock completes the left word.
    for (std::size_t i = 0; i < 5 + 32 + 1; ++i) {
        REQUIRE_FALSE(decoder.sclkRisingEdge(first[i].lrck, first[i].sdti).frame.has_value());
    }
    decoder.reset();

    // New stream: partial left lead-in, then right, left, right.
    std::vector<Half> after{{0, 32}, {0x11111u, 32}, {0x22222u, 32}, {0x33333u, 32}};
    auto second = encode(SerialFormat::mode2LeftJustified20, after, rng, 0);
    second.erase(second.begin(), second.begin() + 28); // keep 4 bit clocks of the left half

    std::vector<Pcm20Frame> frames;
    std::vector<SerialEvent> events;
    for (const Edge& edge : second) {
        const auto result = decoder.sclkRisingEdge(edge.lrck, edge.sdti);
        if (result.frame) {
            frames.push_back(*result.frame);
        } else if (result.event != SerialEvent::none) {
            events.push_back(result.event);
        }
    }
    REQUIRE(events.size() == 1);
    CHECK(events[0] == SerialEvent::incompleteFrame);
    REQUIRE(frames.size() == 1);
    CHECK(frames[0].left.code == 0x22222);
    CHECK(frames[0].right.code == 0x33333);
}

TEST_CASE("Prepare rejects an invalid format", "[unit][serial]")
{
    SerialDecoder decoder;
    CHECK(decoder.prepare(static_cast<SerialFormat>(4)) == Status::invalidConfiguration);
}

TEST_CASE("Decoding does not allocate", "[unit][serial][rt]")
{
    std::mt19937_64 rng(10);
    std::vector<Half> halves;
    for (int i = 0; i < 64; ++i) {
        halves.push_back({static_cast<std::uint32_t>(i * 0x1111) & 0xFFFFFu, 32});
    }
    const auto edges = encode(SerialFormat::mode3I2s, halves, rng);
    SerialDecoder decoder;
    REQUIRE(decoder.prepare(SerialFormat::mode3I2s) == Status::ok);

    std::size_t frames = 0;
    ak4522::test::AllocationCounter counter;
    for (const Edge& edge : edges) {
        if (decoder.sclkRisingEdge(edge.lrck, edge.sdti).frame) {
            ++frames;
        }
    }
    decoder.reset();
    CHECK(counter.allocations() == 0);
    CHECK(frames == 32);
}
