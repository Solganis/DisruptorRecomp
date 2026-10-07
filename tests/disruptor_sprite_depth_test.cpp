#include "cpu_state.h"

#include <array>
#include <cstdint>
#include <iostream>
#include <map>
#include <vector>

namespace {

struct Note {
    std::uint32_t packet;
    std::int32_t depth;
};

struct Placed {
    std::uint32_t packet;
    std::int32_t x;
    std::int32_t dy;
};

/* x_gpr and y_gpr are 0 where the game keeps the operand on its stack. */
struct TestFunnel {
    std::uint32_t projection;
    int depth_gpr;
    std::array<std::uint32_t, 2> packets;
    int x_gpr;
    int y_gpr;
};

constexpr std::array<TestFunnel, 4> kTestFunnels{{
    {0x8003B98Cu, 16, {0x8003BB88u, 0u}, 7, 5},
    {0x8003BDA0u, 16, {0x8003BFB0u, 0u}, 7, 6},
    {0x8003C4B0u, 21, {0x8003C848u, 0x8003CAD4u}, 0, 0},
    {0x8003D078u, 20, {0x8003D488u, 0u}, 8, 5},
}};
constexpr std::uint32_t kStack = 0x801FF000u;
constexpr std::uint32_t kStoreWord = 0xAFA20010u;
constexpr std::uint32_t kTestRecordSite = 0x8003D1B4u;
constexpr std::uint32_t kTestRecordWord = 0xA4379EA0u;
constexpr std::uint32_t kTestDeferredSite = 0x800433A0u;
constexpr std::uint32_t kPacket = 0x800C4000u;
constexpr std::int32_t kUnits = 8;

std::vector<Note> g_notes;
std::vector<Placed> g_places;
std::uint32_t g_stack_x = 0;
std::uint32_t g_stack_y = 0;
int g_failures = 0;

struct Sized {
    std::uint32_t packet;
    std::int32_t row;
    std::int32_t width;
    std::int32_t height;
    int shadow;
    int whole;
};
std::vector<Sized> g_sizes;
std::map<std::uint32_t, std::uint8_t> g_memory;

std::uint8_t read_byte(std::uint32_t address) {
    const auto found = g_memory.find(address);
    return found == g_memory.end() ? 0x5Au : found->second;
}

std::uint16_t read_half(std::uint32_t address) {
    return static_cast<std::uint16_t>(read_byte(address) | read_byte(address + 1u) << 8);
}

std::uint32_t read_stack(std::uint32_t address) {
    if (address == kStack + 0x18u) return g_stack_x;
    if (address == kStack + 0x20u) return g_stack_y;
    if (g_memory.count(address) == 0u) return 0x7FFFFFFFu;
    return read_half(address) | static_cast<std::uint32_t>(read_half(address + 2u)) << 16;
}

void put(std::uint32_t address, std::uint32_t value, int bytes) {
    for (int byte = 0; byte < bytes; ++byte) g_memory[address + static_cast<std::uint32_t>(byte)] = static_cast<std::uint8_t>(value >> (8 * byte));
}

void expect(bool condition, const char *message) {
    if (condition) return;
    ++g_failures;
    std::cerr << "FAIL: " << message << '\n';
}

}  // namespace

extern "C" void gpu_temporal_note_sprite(CPUState *, std::uint32_t packet, std::int32_t depth) {
    g_notes.push_back({packet, depth});
}

extern "C" void gpu_temporal_place_sprite(std::uint32_t packet, std::int32_t x, std::int32_t dy) {
    g_places.push_back({packet, x, dy});
}

extern "C" void gpu_temporal_size_sprite(std::uint32_t packet, std::int32_t row, std::int32_t width, std::int32_t height,
                                         int shadow, int whole) {
    g_sizes.push_back({packet, row, width, height, shadow, whole});
}

/* Classic wide as the runtime does it: three quarters about column 160, rounded and unrounded. */
extern "C" int psx_ws_project_x(int x) {
    const int scaled = (x - 160) * 3;
    return 160 + (scaled + (scaled >= 0 ? 2 : -2)) / 4;
}

extern "C" std::int32_t psx_ws_project_x16(int x, std::int32_t fraction16) {
    return 160 * 65536 + static_cast<std::int32_t>((static_cast<std::int64_t>(x - 160) * 65536 + fraction16) * 3 / 4);
}

#include "../src/disruptor_sprite_depth.cpp"

namespace {

void project(CPUState &cpu, const TestFunnel &funnel, std::int32_t depth) {
    cpu.gpr[funnel.depth_gpr] = static_cast<std::uint32_t>(depth);
    disruptor_sprite_depth_instruction_hook(&cpu, funnel.projection, kStoreWord, 1);
}

bool noted_once(std::uint32_t packet, std::int32_t depth) {
    const bool right = g_notes.size() == 1 && g_notes[0].packet == packet && g_notes[0].depth == depth;
    g_notes.clear();
    return right;
}

void test_every_funnel_hands_its_depth_over() {
    CPUState cpu{};
    std::int32_t depth = 100;
    for (const TestFunnel &funnel : kTestFunnels)
        for (std::uint32_t site : funnel.packets) {
            if (site == 0u) continue;
            depth += 37;
            project(cpu, funnel, depth);
            disruptor_sprite_depth_packet(&cpu, site, kPacket + static_cast<std::uint32_t>(depth));
            expect(noted_once(kPacket + static_cast<std::uint32_t>(depth), depth * kUnits),
                   "a funnel's packet carries the depth of its own projection in GTE units");
            disruptor_sprite_depth_packet(&cpu, site, kPacket);
            expect(g_notes.empty(), "a depth is handed over once");
        }
}

void test_a_depth_stays_in_its_funnel() {
    CPUState cpu{};
    project(cpu, kTestFunnels[0], 300);
    disruptor_sprite_depth_packet(&cpu, kTestFunnels[1].packets[0], kPacket);
    expect(g_notes.empty(), "another funnel's packet does not take a depth left behind by a culled sprite");
    disruptor_sprite_depth_packet(&cpu, kTestFunnels[0].packets[0], kPacket);
    expect(g_notes.empty(), "and the depth it refused is gone");

    project(cpu, kTestFunnels[0], 300);
    project(cpu, kTestFunnels[2], 500);
    disruptor_sprite_depth_packet(&cpu, kTestFunnels[2].packets[1], kPacket);
    expect(noted_once(kPacket, 500 * kUnits), "the latest projection is the pending one");
}

void test_only_the_reviewed_instructions_count() {
    CPUState cpu{};
    cpu.gpr[16] = 300;
    disruptor_sprite_depth_instruction_hook(&cpu, kTestFunnels[0].projection, kStoreWord ^ 1u, 1);
    disruptor_sprite_depth_instruction_hook(&cpu, kTestFunnels[0].projection, kStoreWord, 0);
    disruptor_sprite_depth_instruction_hook(&cpu, kTestFunnels[0].projection + 4u, kStoreWord, 1);
    disruptor_sprite_depth_instruction_hook(nullptr, kTestFunnels[0].projection, kStoreWord, 1);
    disruptor_sprite_depth_packet(&cpu, kTestFunnels[0].packets[0], kPacket);
    expect(g_notes.empty(), "a changed word, another phase or address and a missing CPU record nothing");

    for (std::int32_t depth : {0, -5, INT32_MAX / kUnits + 1}) {
        project(cpu, kTestFunnels[0], depth);
        disruptor_sprite_depth_packet(&cpu, kTestFunnels[0].packets[0], kPacket);
        expect(g_notes.empty(), "a depth that is not positive or does not fit is not passed on");
    }
    project(cpu, kTestFunnels[0], 300);
    disruptor_sprite_depth_packet(&cpu, 0x80010000u, kPacket);
    disruptor_sprite_depth_packet(nullptr, kTestFunnels[0].packets[0], kPacket);
    expect(g_notes.empty(), "an unknown packet site and a missing CPU hand nothing over");
    disruptor_sprite_depth_packet(&cpu, kTestFunnels[0].packets[0], kPacket);
    expect(noted_once(kPacket, 300 * kUnits), "and neither of them spends the pending depth");
}

/* What the game holds at the seam for a point (x, y, depth): its operands, and its own two quotients. */
void at_the_seam(CPUState &cpu, const TestFunnel &funnel, std::int32_t x, std::int32_t y, std::int32_t depth) {
    cpu.gpr[29] = kStack;
    cpu.read_word = read_stack;
    cpu.gpr[funnel.depth_gpr] = static_cast<std::uint32_t>(depth);
    if (funnel.x_gpr) cpu.gpr[funnel.x_gpr] = static_cast<std::uint32_t>(x); else g_stack_x = static_cast<std::uint32_t>(x);
    if (funnel.y_gpr) cpu.gpr[funnel.y_gpr] = static_cast<std::uint32_t>(y); else g_stack_y = static_cast<std::uint32_t>(y);
    cpu.gpr[2] = static_cast<std::uint32_t>(psx_ws_project_x(160 + 160 * x / depth));
    cpu.gpr[3] = static_cast<std::uint32_t>(160 * y / depth);
}

bool placed_once(std::uint32_t packet, std::int32_t x, std::int32_t dy) {
    const bool right = g_places.size() == 1 && g_places[0].packet == packet && g_places[0].x == x && g_places[0].dy == dy;
    g_places.clear();
    g_notes.clear();
    g_sizes.clear();
    return right;
}

bool sized_once(std::int32_t row, std::int32_t width, std::int32_t height, int shadow, int whole) {
    const bool right = g_sizes.size() == 1 && g_sizes[0].packet == kPacket && g_sizes[0].row == row && g_sizes[0].width == width &&
                       g_sizes[0].height == height && g_sizes[0].shadow == shadow && g_sizes[0].whole == whole;
    g_sizes.clear();
    g_places.clear();
    g_notes.clear();
    return right;
}

bool unsized() {
    const bool right = g_sizes.empty() && g_places.size() == 1;
    g_sizes.clear();
    g_places.clear();
    g_notes.clear();
    return right;
}

constexpr std::uint32_t kActor = 0x80120000u;
constexpr std::uint32_t kBody = 0x80130000u;
constexpr std::uint32_t kPictures = 0x80140000u;
constexpr std::uint32_t kTable = 0x80057E98u;
/* The point of every size case: 160 * 100 / 640 and 160 * 37 / 640, row 120 - 9.25 = 110.75. */
constexpr std::int32_t kRow = 7258112;

CPUState ready(const TestFunnel &funnel) {
    CPUState cpu{};
    at_the_seam(cpu, funnel, 100, 37, 640);
    cpu.read_half = read_half;
    cpu.read_byte = read_byte;
    return cpu;
}

void run(CPUState &cpu, const TestFunnel &funnel, std::uint32_t site) {
    disruptor_sprite_depth_instruction_hook(&cpu, funnel.projection, kStoreWord, 1);
    disruptor_sprite_depth_packet(&cpu, site, kPacket);
}

/* An actor of the fourth funnel: picture 28 x 7 at scales 1024 and 2048 over 64, which is 448 x 224 before the depth. */
void fourth_actor(CPUState &cpu, std::uint32_t kind, std::uint32_t frame, std::uint32_t turned) {
    g_memory.clear();
    cpu.gpr[21] = kActor;
    cpu.gpr[22] = kBody;
    cpu.gpr[16] = 0xABCD00u | turned;
    put(kActor + 0x3Au, kind, 1);
    put(kActor + 0x3Bu, frame, 1);
    put(kBody, kPictures, 4);
    put(kPictures + 20u * (frame + (kind == 7u || kind == 8u ? 0u : turned)) + 8u, 28u | 7u << 8, 2);
    put(kBody + 0x1Cu, 1024u, 4);
    put(kBody + 0x18u, 2048u, 4);
    put(kBody + 0xCu, 64u, 4);
    put(kTable + 2u * 64u, 1024u, 2);
    put(kTable + 2u * 640u, 102u, 2);
    cpu.gpr[18] = 111u;  /* (448 * 160 * 102) >> 16 */
    cpu.gpr[19] = 55u;   /* (224 * 160 * 102) >> 16 */
}

void test_a_sprite_keeps_its_size_before_the_cut() {
    {
        CPUState cpu = ready(kTestFunnels[0]);
        g_memory.clear();
        cpu.gpr[21] = kActor;
        put(kActor + 4u, 36u, 4);
        put(kActor + 8u, 52u, 4);
        run(cpu, kTestFunnels[0], kTestFunnels[0].packets[0]);
        expect(sized_once(kRow, 9 * 65536, 13 * 65536, 0, 0), "the first funnel's size is two words of its definition times 160 over the depth");
        put(kActor + 4u, 0u, 4);
        run(cpu, kTestFunnels[0], kTestFunnels[0].packets[0]);
        expect(unsized(), "a size of nothing is not handed over");
        cpu.read_half = nullptr;
        put(kActor + 4u, 36u, 4);
        run(cpu, kTestFunnels[0], kTestFunnels[0].packets[0]);
        expect(unsized(), "nor a size that cannot be read");
    }
    {
        CPUState cpu = ready(kTestFunnels[1]);
        g_memory.clear();
        cpu.gpr[18] = kActor;
        put(kActor + 8u, 24u | 48u << 8, 2);
        run(cpu, kTestFunnels[1], kTestFunnels[1].packets[0]);
        expect(sized_once(kRow, 6 * 65536, 12 * 65536, 0, 0), "the second funnel's size is two bytes of its definition");
    }
    {
        CPUState cpu = ready(kTestFunnels[2]);
        put(640u + 4u, 36u, 4);
        put(640u + 8u, 52u, 4);
        run(cpu, kTestFunnels[2], kTestFunnels[2].packets[0]);
        expect(unsized(), "the third funnel's size is not read");
    }
    const TestFunnel &fourth = kTestFunnels[3];
    const auto deferred = [&fourth](CPUState &cpu) {
        at_the_seam(cpu, fourth, 100, 37, 640);
        disruptor_sprite_depth_instruction_hook(&cpu, fourth.projection, kStoreWord, 1);
        cpu.gpr[3] = 20u * 6u;
        disruptor_sprite_depth_instruction_hook(&cpu, kTestRecordSite, kTestRecordWord, 1);
        const std::uint32_t body = cpu.gpr[22];
        cpu.gpr[22] = 6u;
        disruptor_sprite_depth_packet(&cpu, kTestDeferredSite, kPacket);
        cpu.gpr[22] = body;
    };
    for (const std::uint32_t kind : {0u, 6u, 7u, 8u, 9u}) {
        CPUState cpu = ready(fourth);
        fourth_actor(cpu, kind, 5u, 3u);
        deferred(cpu);
        expect(sized_once(kRow, 112 * 65536, 56 * 65536, 0, 1),
               "an actor's size follows the game's own steps and is marked as spanning its packet whole");
    }
    {
        CPUState cpu = ready(fourth);
        fourth_actor(cpu, 0u, 5u, 3u);
        put(kActor + 0x3Au, 7u, 1);
        deferred(cpu);
        expect(unsized(), "a picture taken without the actor's turn is another picture");
        fourth_actor(cpu, 0u, 5u, 3u);
        cpu.gpr[18] = 112u;
        deferred(cpu);
        expect(unsized(), "a width the steps do not end on leaves the place and no size");
        fourth_actor(cpu, 0u, 5u, 3u);
        cpu.gpr[19] = 56u;
        deferred(cpu);
        expect(unsized(), "and so does a height");
        fourth_actor(cpu, 0u, 5u, 3u);
        put(kTable + 2u * 640u, 1024u, 2);
        cpu.gpr[18] = 1120u;
        cpu.gpr[19] = 560u;
        deferred(cpu);
        expect(unsized(), "nor does a side past the game's cap, where it rescales the other");
    }    for (const int broken : {0, 1, 2, 3}) {
        /* A shadow 60 x 32 units, 320 units under the eye, at depth 640: 15 x 8 pixels, centred on row 120 - 80. */
        CPUState cpu = ready(fourth);
        fourth_actor(cpu, 0u, 5u, 3u);
        put(kBody + 0xF8u, 60u, 4);
        put(kBody + 0xFCu, 32u, 4);
        put(kBody + 0xF4u, 100u, 4);
        put(kActor + 0x28u, 500u, 2);
        put(0x800775D0u, 80u, 4);
        put(kPacket + 8u, 200u | (36u + (broken == 3)) << 16, 4);
        put(kPacket + 16u, (214u + (broken == 1)) | 36u << 16, 4);
        put(kPacket + 24u, 200u | (43u + (broken == 2) + (broken == 3)) << 16, 4);
        run(cpu, fourth, fourth.packets[0]);
        if (broken) {
            expect(g_sizes.empty() && g_places.size() == 1 && g_places[0].dy == 0,
                   "a shadow whose numbers do not give its packet keeps the caster's column and no row or size");
            g_places.clear();
            g_notes.clear();
        } else {
            expect(g_places.size() == 1 && g_places[0].dy == 0, "a shadow drops the caster's row");
            expect(sized_once(40 * 65536, 15 * 65536, 8 * 65536, 1, 0), "and takes its own row and size, marked as a shadow");
        }
    }
    g_memory.clear();
}


void test_a_projection_keeps_what_the_divisions_dropped();

void test_sizes_run_after_places() {
    test_a_projection_keeps_what_the_divisions_dropped();
    test_a_sprite_keeps_its_size_before_the_cut();
}

void test_a_projection_keeps_what_the_divisions_dropped() {
    for (const TestFunnel &funnel : kTestFunnels) {
        CPUState cpu{};
        /* 160 * 100 / 640 = 25 exactly, column 185, 178.75 after the squash. 160 * 37 / 640 = 9.25: a quarter pixel up. */
        at_the_seam(cpu, funnel, 100, 37, 640);
        disruptor_sprite_depth_instruction_hook(&cpu, funnel.projection, kStoreWord, 1);
        disruptor_sprite_depth_packet(&cpu, funnel.packets[0], kPacket);
        /* The fourth funnel's own packet site is its shadow, which does not keep the caster's row. */
        const bool shadow = funnel.packets[0] == 0x8003D488u;
        expect(placed_once(kPacket, 11714560, shadow ? 0 : -16384), "the unrounded column and the dropped part of the row go with the packet");
        /* 160 * -77 / 1000 = -12.32: column 148 and -0.32 more, 150.76 after the squash. 160 * -45 / 1000 = -7.2: 0.2 down. */
        at_the_seam(cpu, funnel, -77, -45, 1000);
        disruptor_sprite_depth_instruction_hook(&cpu, funnel.projection, kStoreWord, 1);
        disruptor_sprite_depth_packet(&cpu, funnel.packets[0], kPacket);
        expect(placed_once(kPacket, 9880208, shadow ? 0 : 13107), "a remainder left of the centre and above it keeps its sign");

        at_the_seam(cpu, funnel, 100, 37, 640);
        ++cpu.gpr[3];
        disruptor_sprite_depth_instruction_hook(&cpu, funnel.projection, kStoreWord, 1);
        disruptor_sprite_depth_packet(&cpu, funnel.packets[0], kPacket);
        expect(g_places.empty() && noted_once(kPacket, 640 * kUnits), "a row quotient the operands do not give leaves the depth and no place");
        at_the_seam(cpu, funnel, 100, 37, 640);
        ++cpu.gpr[2];
        disruptor_sprite_depth_instruction_hook(&cpu, funnel.projection, kStoreWord, 1);
        disruptor_sprite_depth_packet(&cpu, funnel.packets[0], kPacket);
        expect(g_places.empty() && noted_once(kPacket, 640 * kUnits), "a stored column the operands do not give leaves the depth and no place");
        at_the_seam(cpu, funnel, 26000, 37, 640);
        disruptor_sprite_depth_instruction_hook(&cpu, funnel.projection, kStoreWord, 1);
        disruptor_sprite_depth_packet(&cpu, funnel.packets[0], kPacket);
        expect(g_places.empty() && noted_once(kPacket, 640 * kUnits), "a column too far out for 16.16 is not placed");
    }
    CPUState cpu{};
    at_the_seam(cpu, kTestFunnels[2], 100, 37, 640);
    cpu.read_word = nullptr;
    disruptor_sprite_depth_instruction_hook(&cpu, kTestFunnels[2].projection, kStoreWord, 1);
    disruptor_sprite_depth_packet(&cpu, kTestFunnels[2].packets[0], kPacket);
    expect(g_places.empty() && noted_once(kPacket, 640 * kUnits), "a stack operand that cannot be read leaves the depth and no place");

    const TestFunnel &deferring = kTestFunnels[3];
    at_the_seam(cpu, deferring, -77, -45, 1000);
    disruptor_sprite_depth_instruction_hook(&cpu, deferring.projection, kStoreWord, 1);
    cpu.gpr[3] = 20u * 9u;
    disruptor_sprite_depth_instruction_hook(&cpu, kTestRecordSite, kTestRecordWord, 1);
    at_the_seam(cpu, deferring, 100, 37, 640);
    disruptor_sprite_depth_instruction_hook(&cpu, deferring.projection, kStoreWord, 1);
    cpu.gpr[22] = 9u;
    disruptor_sprite_depth_packet(&cpu, kTestDeferredSite, kPacket + 80u);
    expect(placed_once(kPacket + 80u, 9880208, 13107), "a deferred actor's place waits under its record with its depth");
    cpu.gpr[22] = 9u;
    disruptor_sprite_depth_packet(&cpu, kTestDeferredSite, kPacket + 80u);
    expect(g_places.empty() && g_notes.empty(), "and is handed over once");
    disruptor_sprite_depth_packet(&cpu, deferring.packets[0], kPacket);
    g_places.clear();
    g_notes.clear();
}

void test_deferred_actor_waits_under_its_record() {
    CPUState cpu{};
    const TestFunnel &deferring = kTestFunnels[3];
    project(cpu, deferring, 410);
    cpu.gpr[3] = 20u * 7u;
    disruptor_sprite_depth_instruction_hook(&cpu, kTestRecordSite, kTestRecordWord, 1);
    project(cpu, deferring, 520);
    cpu.gpr[3] = 20u * 2u;
    disruptor_sprite_depth_instruction_hook(&cpu, kTestRecordSite, kTestRecordWord, 1);

    cpu.gpr[22] = 2u;
    disruptor_sprite_depth_packet(&cpu, kTestDeferredSite, kPacket + 40u);
    expect(noted_once(kPacket + 40u, 520 * kUnits), "a deferred packet takes the depth stored under its record");
    cpu.gpr[22] = 7u;
    disruptor_sprite_depth_packet(&cpu, kTestDeferredSite, kPacket);
    expect(noted_once(kPacket, 410 * kUnits), "records are told apart by their index, in any order");
    disruptor_sprite_depth_packet(&cpu, kTestDeferredSite, kPacket);
    expect(g_notes.empty(), "a record's depth is handed over once");

    project(cpu, kTestFunnels[0], 300);
    cpu.gpr[3] = 20u * 4u;
    disruptor_sprite_depth_instruction_hook(&cpu, kTestRecordSite, kTestRecordWord, 1);
    cpu.gpr[22] = 4u;
    disruptor_sprite_depth_packet(&cpu, kTestDeferredSite, kPacket);
    expect(g_notes.empty(), "a record does not take a depth projected by another funnel");

    project(cpu, deferring, 410);
    for (std::uint32_t offset : {20u * 4u + 1u, 20u * 256u}) {
        cpu.gpr[3] = offset;
        disruptor_sprite_depth_instruction_hook(&cpu, kTestRecordSite, kTestRecordWord, 1);
    }
    cpu.gpr[3] = 20u * 4u;
    disruptor_sprite_depth_instruction_hook(&cpu, kTestRecordSite, kTestRecordWord ^ 1u, 1);
    for (std::uint32_t index : {4u, 256u, 0xFFFFFFFFu}) {
        cpu.gpr[22] = index;
        disruptor_sprite_depth_packet(&cpu, kTestDeferredSite, kPacket);
    }
    expect(g_notes.empty(), "a misaligned or out-of-range record and a changed word store nothing");
}

}  // namespace

int main() {
    test_every_funnel_hands_its_depth_over();
    test_a_depth_stays_in_its_funnel();
    test_only_the_reviewed_instructions_count();
    test_deferred_actor_waits_under_its_record();
    test_sizes_run_after_places();
    expect(g_places.empty() && g_sizes.empty(), "no test leaves a place or a size behind");
    if (g_failures) return 1;
    std::cout << "Disruptor sprite depth tests passed\n";
    return 0;
}
