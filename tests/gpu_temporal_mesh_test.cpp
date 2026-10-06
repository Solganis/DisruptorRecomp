#include "gpu_temporal_mesh.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <iostream>
#include <limits>
#include <vector>

namespace {

int failures = 0;

void expect(bool condition, const char *message) {
    if (condition) return;
    ++failures;
    std::cerr << "FAIL: " << message << '\n';
}

GpuTemporalTriangle first_triangle(float offset = 0.0f) {
    GpuTemporalTriangle t{};
    t.material = 1;
    t.world = 1;
    t.x[0] = offset; t.x[1] = offset + 8; t.x[2] = offset;
    t.y[0] = 0; t.y[1] = 0; t.y[2] = 8;
    t.depth[0] = 100; t.depth[1] = 200; t.depth[2] = 150;
    t.q[0] = 1; t.q[1] = 0.5f; t.q[2] = 100.0f / 150.0f;
    return t;
}

GpuTemporalTriangle second_triangle() {
    auto t = first_triangle();
    t.material = 2;
    t.x[0] = 0; t.x[1] = 8; t.x[2] = 8;
    t.y[0] = 8; t.y[1] = 0; t.y[2] = 8;
    t.depth[0] = 150; t.depth[1] = 200; t.depth[2] = 300;
    t.q[0] = 1; t.q[1] = 0.75f; t.q[2] = 0.5f;
    return t;
}

GpuTemporalTriangle translated(GpuTemporalTriangle t, float dx, float dy = 0) {
    for (int vertex = 0; vertex < 3; ++vertex) { t.x[vertex] += dx; t.y[vertex] += dy; }
    return t;
}

bool same_positions(const GpuTemporalTriangle &a, const GpuTemporalTriangle &b) {
    for (int vertex = 0; vertex < 3; ++vertex)
        if (a.x[vertex] != b.x[vertex] || a.y[vertex] != b.y[vertex]) return false;
    return true;
}

void expect_shared_edge(const std::array<GpuTemporalTriangle, 2> &from,
                        const std::array<GpuTemporalTriangle, 2> &current) {
    for (float alpha : {0.0f, 0.25f, 0.5f, 0.75f, 1.0f}) {
        std::array<GpuTemporalTriangle, 2> out{};
        for (int face = 0; face < 2; ++face)
            gpu_temporal_lerp(&from[face], &current[face], alpha, &out[face]);
        expect(out[0].x[1] == out[1].x[1] && out[0].y[1] == out[1].y[1] &&
                   out[0].x[2] == out[1].x[0] && out[0].y[2] == out[1].y[0],
               "all intermediate shared-edge endpoints coincide");
        if (alpha == 1.0f)
            expect(same_positions(out[0], current[0]) && same_positions(out[1], current[1]),
                   "alpha-one geometry remains exactly current");
    }
}

void half_matched_quad() {
    const std::array<GpuTemporalTriangle, 2> current{first_triangle(), second_triangle()};
    auto previous = translated(current[0], -4);
    for (float &q : previous.q) q *= 0.9f;
    const std::array<int, 2> matches{0, -1};
    std::array<GpuTemporalTriangle, 2> from{};
    GpuTemporalMeshStats stats{};
    expect(gpu_temporal_mesh_prepare(&previous, 1, current.data(), 2, matches.data(),
                                      from.data(), &stats) == 2,
           "matched and unmatched neighboring faces retain motion");
    expect_shared_edge(from, current);
    expect(from[1].x[2] == current[1].x[2], "unseeded outer corner stays current");
    expect(from[0].q[1] == previous.q[1] && from[1].q[1] == current[1].q[1] &&
               from[0].q[1] != from[1].q[1],
           "differently normalized polygon q is never averaged across a shared vertex");
    expect(stats.seeded_vertices == 3 && stats.shared_vertices == 2 &&
               stats.propagated_vertices == 2 && stats.conflicting_vertices == 0,
           "statistics distinguish seeded groups and propagated unmatched corners");

    auto unknown = current;
    for (auto &triangle : unknown) std::fill(std::begin(triangle.depth), std::end(triangle.depth), 0.0f);
    auto unknown_previous = translated(unknown[0], -4);
    expect(gpu_temporal_mesh_prepare(&unknown_previous, 1, unknown.data(), 2, matches.data(),
                                      from.data(), &stats) == 2,
           "unknown depth uses a complete unique reversed shared edge");
    expect_shared_edge(from, unknown);
}

void depth_and_topology_guards() {
    auto a = first_triangle();
    auto previous = translated(a, -4);
    std::array<GpuTemporalTriangle, 2> current{a, a}, from{};
    current[1].material = 2;
    for (float &depth : current[1].depth) depth *= 2;
    const std::array<int, 2> matches{0, -1};
    gpu_temporal_mesh_prepare(&previous, 1, current.data(), 2, matches.data(), from.data(), nullptr);
    expect(!same_positions(from[0], current[0]) && same_positions(from[1], current[1]),
           "overlapping independent depth planes never share motion");

    current[1] = current[0];
    current[1].material = 2;
    std::fill(std::begin(current[0].depth), std::end(current[0].depth), 0.0f);
    std::fill(std::begin(current[1].depth), std::end(current[1].depth), 0.0f);
    previous = translated(current[0], -4);
    gpu_temporal_mesh_prepare(&previous, 1, current.data(), 2, matches.data(), from.data(), nullptr);
    expect(same_positions(from[1], current[1]),
           "unknown depth does not weld overlapping faces with the same winding");

    current[1] = translated(current[0], 0, -8);
    /* A single coincident projected vertex is not an unknown-depth weld. */
    gpu_temporal_mesh_prepare(&previous, 1, current.data(), 2, matches.data(), from.data(), nullptr);
    expect(same_positions(from[1], current[1]), "isolated XY equality cannot create adjacency");
}

void conflicts_and_order() {
    const std::array<GpuTemporalTriangle, 2> current{first_triangle(), second_triangle()};
    const std::array<GpuTemporalTriangle, 2> previous{
        translated(current[0], -4), translated(current[1], 4)};
    const std::array<int, 2> matches{0, 1};
    std::array<GpuTemporalTriangle, 2> from{};
    GpuTemporalMeshStats stats{};
    gpu_temporal_mesh_prepare(previous.data(), 2, current.data(), 2, matches.data(), from.data(), &stats);
    expect_shared_edge(from, current);
    expect(from[0].x[1] == current[0].x[1] && from[0].x[2] == current[0].x[2],
           "conflicting histories hold the same shared corners on both faces");
    expect(from[0].x[0] != current[0].x[0] && from[1].x[2] != current[1].x[2],
           "a conflict preserves independent neighboring motion");
    expect(stats.conflicting_vertices == 2, "conflicts count vertex groups");
    const std::array<GpuTemporalTriangle, 2> reordered_current{current[1], current[0]};
    const std::array<GpuTemporalTriangle, 2> reordered_previous{previous[1], previous[0]};
    std::array<GpuTemporalTriangle, 2> reordered_from{};
    gpu_temporal_mesh_prepare(reordered_previous.data(), 2, reordered_current.data(), 2,
                              matches.data(), reordered_from.data(), nullptr);
    expect(same_positions(from[0], reordered_from[1]) && same_positions(from[1], reordered_from[0]),
           "triangle submission order does not decide shared vertex history");
}

void invalid_and_hud() {
    const auto current = first_triangle();
    const auto previous = translated(current, -4);
    const int match = 0;
    GpuTemporalTriangle from{};
    for (float depth : {-1.0f, 65535.0f, std::numeric_limits<float>::infinity(),
                         std::numeric_limits<float>::quiet_NaN()}) {
        std::array<GpuTemporalTriangle, 2> overlap{current, current}, history{};
        for (auto &triangle : overlap)
            std::fill(std::begin(triangle.depth), std::end(triangle.depth), depth);
        overlap[1].material = 2;
        const std::array<int, 2> matches{0, -1};
        gpu_temporal_mesh_prepare(&previous, 1, overlap.data(), 2, matches.data(), history.data(), nullptr);
        expect(!same_positions(history[0], overlap[0]) && same_positions(history[1], overlap[1]),
               "invalid or saturated depth cannot create a known-depth point weld");
    }
    auto invalid = current;
    invalid.y[2] = 0;
    expect(gpu_temporal_mesh_prepare(&previous, 1, &invalid, 1, &match, &from, nullptr) == 0 &&
               same_positions(from, invalid), "degenerate geometry stays current");
    auto hud = current;
    hud.world = 0;
    expect(gpu_temporal_mesh_prepare(&previous, 1, &hud, 1, &match, &from, nullptr) == 0 &&
               same_positions(from, hud), "HUD geometry is unchanged");
    expect(gpu_temporal_mesh_prepare(nullptr, 0, &current, 1, &match, &from, nullptr) == 0 &&
               same_positions(from, current), "missing history initializes a safe current fallback");
}

void held_world_anchors() {
    const auto a = first_triangle();
    const auto previous = translated(a, -4);
    const std::array<int, 2> matches{0, -1};
    for (int kind = 0; kind < 3; ++kind) {
        std::array<GpuTemporalTriangle, 2> current{a, second_triangle()}, from{};
        if (kind < 2) {
            current[1].x[2] = 4;
            current[1].y[2] = kind == 0 ? 4.03f : 4;
        } else current[1].q[0] = 0;
        gpu_temporal_mesh_prepare(&previous, 1, current.data(), 2, matches.data(), from.data(), nullptr);
        expect(same_positions(from[1], current[1]),
               "tiny, degenerate and mixed-q world faces remain fixed anchors");
        expect(from[0].x[1] == current[0].x[1] && from[0].x[2] == current[0].x[2] &&
                   from[0].x[0] != current[0].x[0],
               "ineligible world faces hold shared groups while preserving other corner motion");
        expect_shared_edge(from, current);
    }
}

void safety_propagation() {
    const std::array<GpuTemporalTriangle, 3> current{
        first_triangle(), second_triangle(), first_triangle(100)};
    const std::array<GpuTemporalTriangle, 2> previous{
        translated(current[0], 20), translated(current[2], -4)};
    const std::array<int, 3> matches{0, -1, 1};
    std::array<GpuTemporalTriangle, 3> from{};
    GpuTemporalMeshStats stats{};
    expect(gpu_temporal_mesh_prepare(previous.data(), 2, current.data(), 3, matches.data(),
                                      from.data(), &stats) == 1,
           "unsafe local reconciliation preserves unrelated scene interpolation");
    expect(same_positions(from[0], current[0]) && same_positions(from[1], current[1]) &&
               !same_positions(from[2], current[2]),
           "winding safety pins and rechecks only incident vertex groups");
    expect(stats.held_vertices >= 3, "winding safeguards report held groups");
    auto reversed_current = current;
    std::reverse(reversed_current.begin(), reversed_current.end());
    const std::array<int, 3> reversed_matches{1, -1, 0};
    std::array<GpuTemporalTriangle, 3> reversed_from{};
    gpu_temporal_mesh_prepare(previous.data(), 2, reversed_current.data(), 3,
                              reversed_matches.data(), reversed_from.data(), nullptr);
    expect(same_positions(from[0], reversed_from[2]) && same_positions(from[1], reversed_from[1]) &&
               same_positions(from[2], reversed_from[0]),
           "cascading winding safeguards are independent of submission order");
}

void connected_and_scale() {
    std::array<GpuTemporalTriangle, 3> current{first_triangle(), second_triangle(), second_triangle()};
    current[2].x[0] = 0; current[2].y[0] = 8;
    current[2].x[1] = 8; current[2].y[1] = 8;
    current[2].x[2] = 0; current[2].y[2] = 16;
    current[2].depth[0] = 150; current[2].depth[1] = 300; current[2].depth[2] = 300;
    current[2].material = 3;
    const auto previous = translated(current[0], -2);
    const std::array<int, 3> matches{0, -1, -1};
    std::array<GpuTemporalTriangle, 3> from{};
    gpu_temporal_mesh_prepare(&previous, 1, current.data(), 3, matches.data(), from.data(), nullptr);
    expect(from[0].x[2] == from[1].x[0] && from[1].x[0] == from[2].x[0] && from[2].x[0] == -2,
           "one shared vertex history reaches every incident triangle");

    constexpr int count = GPU_TEMPORAL_MAX_TRIANGLES;
    std::vector<GpuTemporalTriangle> many_current, many_previous, many_from(count);
    std::vector<int> many_matches;
    for (int face = 0; face < count; ++face) {
        many_current.push_back(first_triangle(static_cast<float>(face) * 40));
        many_previous.push_back(translated(many_current.back(), -2));
        many_matches.push_back(face);
    }
    expect(gpu_temporal_mesh_prepare(many_previous.data(), count, many_current.data(), count,
                                      many_matches.data(), many_from.data(), nullptr) == count,
           "maximum scene capacity preserves independent smooth geometry");
}

void unknown_depth_bridge() {
    std::array<GpuTemporalTriangle, 3> current{first_triangle(), first_triangle(), second_triangle()};
    std::fill(std::begin(current[0].depth), std::end(current[0].depth), 0.0f);
    current[1].material = 2;
    current[1].x[0] = 8; current[1].y[0] = 0;
    current[1].x[1] = 0; current[1].y[1] = 0;
    current[1].x[2] = 8; current[1].y[2] = -8;
    std::fill(std::begin(current[1].depth), std::end(current[1].depth), 100.0f);
    current[2].material = 3;
    std::fill(std::begin(current[2].depth), std::end(current[2].depth), 200.0f);
    const auto previous = translated(current[1], -2);
    const std::array<int, 3> matches{-1, 0, -1};
    std::array<GpuTemporalTriangle, 3> from{};
    GpuTemporalMeshStats stats{};
    gpu_temporal_mesh_prepare(&previous, 1, current.data(), 3, matches.data(), from.data(), &stats);
    expect(same_positions(from[2], current[2]),
           "unknown-depth triangle cannot transfer history between different known-depth planes");
    expect(from[1].x[2] != current[1].x[2],
           "ambiguous depth bridge does not stop independent motion on the known face");
    expect(stats.conflicting_vertices > 0, "transitive depth ambiguity is reported");

    const std::array<GpuTemporalTriangle, 2> both_previous{
        translated(current[1], -2), translated(current[2], 2)};
    const std::array<int, 3> both_matches{-1, 0, 1};
    gpu_temporal_mesh_prepare(both_previous.data(), 2, current.data(), 3, both_matches.data(),
                              from.data(), &stats);
    expect(from[0].x[1] == from[2].x[1] && from[0].y[1] == from[2].y[1] &&
               from[0].x[2] == from[2].x[0] && from[0].y[2] == from[2].y[0],
           "rejected whole-edge weld pins both endpoints on both incident faces");
    expect(from[2].x[2] != current[2].x[2],
           "whole-edge rejection still preserves the adjacent independent corner's motion");
}

void t_junctions() {
    std::array<GpuTemporalTriangle, 3> current{};
    for (int face = 0; face < 3; ++face) {
        current[face].world = 1;
        current[face].material = static_cast<std::uint64_t>(face + 1);
        std::fill(std::begin(current[face].q), std::end(current[face].q), 1.0f);
    }
    current[0].x[0] = 0; current[0].y[0] = 0; current[0].depth[0] = 100;
    current[0].x[1] = 8; current[0].y[1] = 0; current[0].depth[1] = 300;
    current[0].x[2] = 0; current[0].y[2] = -8; current[0].depth[2] = 200;
    current[1].x[0] = 0; current[1].y[0] = 0; current[1].depth[0] = 100;
    current[1].x[1] = 4; current[1].y[1] = 4; current[1].depth[1] = 200;
    current[1].x[2] = 4; current[1].y[2] = 0; current[1].depth[2] = 150;
    current[2].x[0] = 4; current[2].y[0] = 0; current[2].depth[0] = 150;
    current[2].x[1] = 4; current[2].y[1] = 4; current[2].depth[1] = 200;
    current[2].x[2] = 8; current[2].y[2] = 0; current[2].depth[2] = 300;
    const auto previous = translated(current[0], 0, -2);
    const std::array<int, 3> matches{0, -1, -1};
    std::array<GpuTemporalTriangle, 3> from{};
    GpuTemporalMeshStats stats{};
    gpu_temporal_mesh_prepare(&previous, 1, current.data(), 3, matches.data(), from.data(), &stats);
    expect(stats.anchored_junctions == 1, "A-M/M-B plus A-B identifies a depth-confirmed T junction");
    expect(from[0].y[0] == 0 && from[0].y[1] == 0 && from[1].y[2] == 0 &&
               from[0].y[2] == -10,
           "T-junction anchors only its endpoints and midpoint, preserving other motion");
    for (float alpha : {0.25f, 0.5f, 0.75f}) {
        GpuTemporalTriangle long_edge{}, short_edge{};
        gpu_temporal_lerp(&from[0], &current[0], alpha, &long_edge);
        gpu_temporal_lerp(&from[1], &current[1], alpha, &short_edge);
        expect(short_edge.y[2] == long_edge.y[0] && long_edge.y[0] == long_edge.y[1],
               "intermediate T-junction midpoint stays on the long edge");
    }
    current[1].depth[2] = current[2].depth[0] = 400;
    gpu_temporal_mesh_prepare(&previous, 1, current.data(), 3, matches.data(), from.data(), &stats);
    expect(stats.anchored_junctions == 0 && from[0].y[0] == -2,
           "projected collinearity alone cannot anchor an unrelated depth plane");
}

} // namespace

int main() {
    half_matched_quad();
    depth_and_topology_guards();
    conflicts_and_order();
    invalid_and_hud();
    held_world_anchors();
    safety_propagation();
    connected_and_scale();
    unknown_depth_bridge();
    t_junctions();
    if (failures) return 1;
    std::cout << "GPU temporal mesh continuity tests passed\n";
    return 0;
}
