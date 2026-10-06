#ifndef PSXRECOMP_GPU_TEMPORAL_MESH_H
#define PSXRECOMP_GPU_TEMPORAL_MESH_H

#include "gpu_temporal_match.h"
#include <string.h>

/* Current-frame topology reconciles the histories found by triangle matching.
 * It never finds temporal identity from projected coordinates. Known-depth
 * corners share exact current XY and absolute GTE depth. Without depth, only
 * an unambiguous, oppositely traversed complete shared edge establishes a weld.
 * Texture q is normalized separately by each polygon and is never a weld key.
 */
typedef struct GpuTemporalMeshStats {
    int seeded_vertices;
    int shared_vertices;
    int propagated_vertices;
    int conflicting_vertices;
    int held_vertices;
    int anchored_junctions;
} GpuTemporalMeshStats;

#define GPU_TEMPORAL_MESH_VOTE_EPSILON (1.0 / 64.0)

typedef struct GpuTemporalMeshNode {
    int parent, size, first, next, votes, conflict, held, pending, pending_next;
    int link;
    float depth;
    double min_dx, max_dx, min_dy, max_dy, dx, dy;
} GpuTemporalMeshNode;

typedef struct GpuTemporalMeshPoint {
    float x, y, depth;
    int corner;
} GpuTemporalMeshPoint;

typedef struct GpuTemporalMeshEdge {
    float ax, ay, bx, by;
    int a, b, triangle, direction;
} GpuTemporalMeshEdge;

typedef struct GpuTemporalMeshFace {
    int topology, eligible, matched, queued;
} GpuTemporalMeshFace;

typedef struct GpuTemporalMeshLink {
    int vertex, next;
} GpuTemporalMeshLink;

static inline int gpu_temporal_mesh_compare_float(float a, float b) {
    return a < b ? -1 : a > b ? 1 : 0;
}

static inline int gpu_temporal_mesh_compare_point(const void *left,
                                                  const void *right) {
    const GpuTemporalMeshPoint *a = (const GpuTemporalMeshPoint *)left;
    const GpuTemporalMeshPoint *b = (const GpuTemporalMeshPoint *)right;
    int order = gpu_temporal_mesh_compare_float(a->x, b->x);
    if (!order) order = gpu_temporal_mesh_compare_float(a->y, b->y);
    if (!order) order = gpu_temporal_mesh_compare_float(a->depth, b->depth);
    return order;
}

static inline int gpu_temporal_mesh_compare_edge(const void *left,
                                                 const void *right) {
    const GpuTemporalMeshEdge *a = (const GpuTemporalMeshEdge *)left;
    const GpuTemporalMeshEdge *b = (const GpuTemporalMeshEdge *)right;
    int order = gpu_temporal_mesh_compare_float(a->ax, b->ax);
    if (!order) order = gpu_temporal_mesh_compare_float(a->ay, b->ay);
    if (!order) order = gpu_temporal_mesh_compare_float(a->bx, b->bx);
    if (!order) order = gpu_temporal_mesh_compare_float(a->by, b->by);
    return order;
}

static inline int gpu_temporal_mesh_root(GpuTemporalMeshNode *nodes, int corner) {
    int root = corner;
    while (nodes[root].parent != root) root = nodes[root].parent;
    while (nodes[corner].parent != corner) {
        const int next = nodes[corner].parent;
        nodes[corner].parent = root;
        corner = next;
    }
    return root;
}

static inline void gpu_temporal_mesh_union(GpuTemporalMeshNode *nodes,
                                            int left, int right) {
    int a = gpu_temporal_mesh_root(nodes, left);
    int b = gpu_temporal_mesh_root(nodes, right);
    if (a == b) return;
    if (nodes[a].depth > 0.0f && nodes[b].depth > 0.0f &&
        nodes[a].depth != nodes[b].depth) return;
    if (nodes[a].size < nodes[b].size) { const int swap = a; a = b; b = swap; }
    nodes[b].parent = a;
    nodes[a].size += nodes[b].size;
    nodes[a].held |= nodes[b].held;
    nodes[a].conflict |= nodes[b].conflict;
    if (nodes[a].depth == 0.0f) nodes[a].depth = nodes[b].depth;
}

static inline int gpu_temporal_mesh_valid(const GpuTemporalTriangle *triangle) {
    return gpu_temporal_valid(triangle, NULL, NULL);
}

static inline int gpu_temporal_mesh_topology(const GpuTemporalTriangle *triangle) {
    if (!triangle->world) return 0;
    for (int vertex = 0; vertex < 3; ++vertex)
        if (!gpu_temporal_finite(triangle->x[vertex]) ||
            !gpu_temporal_finite(triangle->y[vertex])) return 0;
    return 1;
}

static inline int gpu_temporal_mesh_known_depth(float depth) {
    /* SZ=65535 may be a saturated distant projection, not a shared 3D point. */
    return gpu_temporal_finite(depth) && depth > 0.0f && depth < 65535.0f;
}

static inline int gpu_temporal_mesh_depth_compatible(
        const GpuTemporalTriangle *triangles, int a, int b) {
    const float da = triangles[a / 3].depth[a % 3];
    const float db = triangles[b / 3].depth[b % 3];
    return !gpu_temporal_mesh_known_depth(da) ||
           !gpu_temporal_mesh_known_depth(db) || da == db;
}

static inline int gpu_temporal_mesh_group_depth_compatible(
        GpuTemporalMeshNode *nodes, int a, int b) {
    const int ra = gpu_temporal_mesh_root(nodes, a);
    const int rb = gpu_temporal_mesh_root(nodes, b);
    if (nodes[ra].depth == 0.0f || nodes[rb].depth == 0.0f ||
        nodes[ra].depth == nodes[rb].depth) return 1;
    /* An unknown corner must not bridge two different known-depth surfaces.
     * Keep the groups distinct and pin the ambiguous coincident endpoints. */
    nodes[ra].held = nodes[rb].held = 1;
    nodes[ra].conflict = nodes[rb].conflict = 1;
    return 0;
}

static inline int gpu_temporal_mesh_compare_connection(const void *left,
                                                       const void *right) {
    const GpuTemporalMeshEdge *a = (const GpuTemporalMeshEdge *)left;
    const GpuTemporalMeshEdge *b = (const GpuTemporalMeshEdge *)right;
    if (a->a != b->a) return a->a < b->a ? -1 : 1;
    return a->b < b->b ? -1 : a->b > b->b ? 1 : 0;
}

static inline int gpu_temporal_mesh_has_connection(
        const GpuTemporalMeshEdge *edges, int count, int a, int b) {
    int lo = 0, hi = count;
    if (a > b) { const int swap = a; a = b; b = swap; }
    while (lo < hi) {
        const int middle = lo + (hi - lo) / 2;
        const GpuTemporalMeshEdge *edge = &edges[middle];
        if (edge->a < a || (edge->a == a && edge->b < b)) lo = middle + 1;
        else hi = middle;
    }
    return lo < count && edges[lo].a == a && edges[lo].b == b;
}

/* A-M/M-B plus A-B forms a confirmed current-frame T junction when precise
 * positions are collinear and absolute depth agrees projectively. Pin those
 * three groups locally: independent screen-space lerps of A, M and B need not
 * keep M on the long edge. No position is snapped at the real-frame endpoint.
 * Degree is bounded to 16, and long-edge lookup is logarithmic. */
static inline void gpu_temporal_mesh_anchor_junctions(
        const GpuTemporalTriangle *curr, int vertex_count,
        GpuTemporalMeshNode *nodes, GpuTemporalMeshEdge *edges, int edge_count,
        GpuTemporalMeshLink *links, GpuTemporalMeshStats *stats) {
    int link_count = 0;
    for (int index = 0; index < edge_count; ++index) {
        int a = gpu_temporal_mesh_root(nodes, edges[index].a);
        int b = gpu_temporal_mesh_root(nodes, edges[index].b);
        if (a > b) { const int swap = a; a = b; b = swap; }
        edges[index].a = a; edges[index].b = b;
    }
    qsort(edges, (size_t)edge_count, sizeof(*edges), gpu_temporal_mesh_compare_connection);
    for (int index = 0; index < edge_count; ++index) {
        const int a = edges[index].a, b = edges[index].b;
        if (a == b || (index && a == edges[index - 1].a && b == edges[index - 1].b)) continue;
        links[link_count].vertex = b; links[link_count].next = nodes[a].link;
        nodes[a].link = link_count++;
        links[link_count].vertex = a; links[link_count].next = nodes[b].link;
        nodes[b].link = link_count++;
    }
    for (int middle = 0; middle < vertex_count; ++middle) {
        int neighbors[16], degree = 0;
        double mx, my;
        if (nodes[middle].parent != middle || nodes[middle].depth <= 0.0f) continue;
        for (int link = nodes[middle].link; link >= 0; link = links[link].next) {
            if (degree == 16) { ++degree; break; }
            neighbors[degree++] = links[link].vertex;
        }
        if (degree > 16) continue;
        mx = curr[middle / 3].x[middle % 3];
        my = curr[middle / 3].y[middle % 3];
        for (int first = 0; first < degree; ++first) for (int second = first + 1; second < degree; ++second) {
            const int a = neighbors[first], b = neighbors[second];
            double ax, ay, dx, dy, length2, parameter, cross, expected_depth;
            if (nodes[a].depth <= 0.0f || nodes[b].depth <= 0.0f ||
                !gpu_temporal_mesh_has_connection(edges, edge_count, a, b)) continue;
            ax = curr[a / 3].x[a % 3]; ay = curr[a / 3].y[a % 3];
            dx = (double)curr[b / 3].x[b % 3] - ax;
            dy = (double)curr[b / 3].y[b % 3] - ay;
            length2 = dx * dx + dy * dy;
            if (length2 <= 0.0) continue;
            parameter = ((mx - ax) * dx + (my - ay) * dy) / length2;
            if (parameter <= 0.000001 || parameter >= 0.999999) continue;
            cross = (mx - ax) * dy - (my - ay) * dx;
            if (cross * cross > length2 / (32.0 * 32.0)) continue;
            expected_depth = 1.0 / ((1.0 - parameter) / nodes[a].depth + parameter / nodes[b].depth);
            if (fabs(expected_depth - nodes[middle].depth) > 2.0) continue;
            nodes[a].held = nodes[middle].held = nodes[b].held = 1;
            if (stats) ++stats->anchored_junctions;
        }
    }
}

static inline void gpu_temporal_mesh_write_triangle(
        const GpuTemporalTriangle *current, int triangle,
        GpuTemporalMeshNode *nodes, GpuTemporalTriangle *out) {
    for (int vertex = 0; vertex < 3; ++vertex) {
        const int root = gpu_temporal_mesh_root(nodes, triangle * 3 + vertex);
        out->x[vertex] = (float)((double)current->x[vertex] + nodes[root].dx);
        out->y[vertex] = (float)((double)current->y[vertex] + nodes[root].dy);
    }
}

/* Returns the number of current triangles with retained XY or texture-q
 * motion. `from` has current indexing and is always initialized from current;
 * it is safe to interpolate every output with its corresponding current face.
 * Only a matched face receives its own prior normalized q. The output depth
 * remains current metadata. HUD, invalid inputs and allocation failure hold.
 * Stats except propagated_vertices count unique eligible vertex groups;
 * propagated_vertices counts unmatched face corners receiving shared motion.
 */
static inline int gpu_temporal_mesh_prepare(
        const GpuTemporalTriangle *prev, int prev_n,
        const GpuTemporalTriangle *curr, int curr_n,
        const int *matches, GpuTemporalTriangle *from,
        GpuTemporalMeshStats *stats) {
    GpuTemporalMeshNode *nodes = NULL;
    GpuTemporalMeshPoint *points = NULL;
    GpuTemporalMeshEdge *edges = NULL;
    GpuTemporalMeshFace *faces = NULL;
    GpuTemporalMeshLink *links = NULL;
    int *queue = NULL;
    int point_count = 0, edge_count = 0, queue_head = 0, queue_tail = 0;
    int queue_count = 0, moving_triangles = 0;
    int vertex_count;
    if (stats) memset(stats, 0, sizeof(*stats));
    if (!curr || !from || curr_n <= 0) return 0;
    for (int triangle = 0; triangle < curr_n; ++triangle) from[triangle] = curr[triangle];
    if (!prev || !matches || prev_n <= 0 ||
        curr_n > GPU_TEMPORAL_MAX_TRIANGLES ||
        prev_n > GPU_TEMPORAL_MAX_TRIANGLES) return 0;
    vertex_count = curr_n * 3;
    nodes = (GpuTemporalMeshNode *)calloc((size_t)vertex_count, sizeof(*nodes));
    points = (GpuTemporalMeshPoint *)malloc((size_t)vertex_count * sizeof(*points));
    edges = (GpuTemporalMeshEdge *)malloc((size_t)vertex_count * sizeof(*edges));
    faces = (GpuTemporalMeshFace *)calloc((size_t)curr_n, sizeof(*faces));
    queue = (int *)malloc((size_t)curr_n * sizeof(*queue));
    links = (GpuTemporalMeshLink *)malloc((size_t)vertex_count * 2 * sizeof(*links));
    if (!nodes || !points || !edges || !faces || !queue || !links) goto cleanup;

    for (int corner = 0; corner < vertex_count; ++corner) {
        nodes[corner].parent = corner;
        nodes[corner].size = 1;
        nodes[corner].first = -1;
        nodes[corner].link = -1;
        if (gpu_temporal_mesh_known_depth(curr[corner / 3].depth[corner % 3]))
            nodes[corner].depth = curr[corner / 3].depth[corner % 3];
    }
    for (int triangle = 0; triangle < curr_n; ++triangle) {
        const GpuTemporalTriangle *t = &curr[triangle];
        faces[triangle].topology = gpu_temporal_mesh_topology(t);
        faces[triangle].eligible = gpu_temporal_mesh_valid(t);
        if (!faces[triangle].topology) continue;
        for (int vertex = 0; vertex < 3; ++vertex) {
            const int next = (vertex + 1) % 3;
            GpuTemporalMeshEdge *edge = &edges[edge_count++];
            int a = vertex, b = next;
            edge->direction = 1;
            if (t->x[a] > t->x[b] || (t->x[a] == t->x[b] && t->y[a] > t->y[b])) {
                a = next; b = vertex; edge->direction = -1;
            }
            edge->ax = t->x[a]; edge->ay = t->y[a];
            edge->bx = t->x[b]; edge->by = t->y[b];
            edge->a = triangle * 3 + a; edge->b = triangle * 3 + b;
            edge->triangle = triangle;
            if (gpu_temporal_mesh_known_depth(t->depth[vertex])) {
                GpuTemporalMeshPoint *point = &points[point_count++];
                point->x = t->x[vertex]; point->y = t->y[vertex];
                point->depth = t->depth[vertex];
                point->corner = triangle * 3 + vertex;
            }
        }
    }

    qsort(points, (size_t)point_count, sizeof(*points), gpu_temporal_mesh_compare_point);
    for (int point = 1; point < point_count; ++point)
        if (!gpu_temporal_mesh_compare_point(&points[point - 1], &points[point]))
            gpu_temporal_mesh_union(nodes, points[point - 1].corner, points[point].corner);
    qsort(edges, (size_t)edge_count, sizeof(*edges), gpu_temporal_mesh_compare_edge);
    for (int first = 0; first < edge_count;) {
        int end = first + 1;
        while (end < edge_count && !gpu_temporal_mesh_compare_edge(&edges[first], &edges[end]))
            ++end;
        if (end - first == 2) {
            const GpuTemporalMeshEdge *a = &edges[first], *b = &edges[first + 1];
            if (a->triangle != b->triangle && a->direction != b->direction &&
                gpu_temporal_mesh_depth_compatible(curr, a->a, b->a) &&
                gpu_temporal_mesh_depth_compatible(curr, a->b, b->b)) {
                const int compatible_a = gpu_temporal_mesh_group_depth_compatible(nodes, a->a, b->a);
                const int compatible_b = gpu_temporal_mesh_group_depth_compatible(nodes, a->b, b->b);
                if (!compatible_a || !compatible_b) {
                    /* Reject the edge atomically, including motion: pinning
                     * only its depth-conflicting endpoint would let the other
                     * endpoint separate while these groups remain unwelded. */
                    const int endpoints[4] = {a->a, a->b, b->a, b->b};
                    for (int endpoint = 0; endpoint < 4; ++endpoint) {
                        const int root = gpu_temporal_mesh_root(nodes, endpoints[endpoint]);
                        nodes[root].held = nodes[root].conflict = 1;
                    }
                    first = end;
                    continue;
                }
                gpu_temporal_mesh_union(nodes, a->a, b->a);
                gpu_temporal_mesh_union(nodes, a->b, b->b);
            }
        }
        first = end;
    }

    /* Build incidence lists after welding. They also bound safety propagation:
     * each group can be pinned once and visits each of its corners once. */
    for (int corner = 0; corner < vertex_count; ++corner) {
        int root;
        if (!faces[corner / 3].topology) continue;
        root = gpu_temporal_mesh_root(nodes, corner);
        nodes[corner].next = nodes[root].first;
        nodes[root].first = corner;
        /* A tiny/degenerate or otherwise ineligible WORLD face still shares
         * topology. It anchors all of its corners so adjacent moving faces
         * cannot detach from it. HUD faces never participate in this graph. */
        if (!faces[corner / 3].eligible) nodes[root].held = 1;
    }
    gpu_temporal_mesh_anchor_junctions(curr, vertex_count, nodes, edges, edge_count, links, stats);
    for (int triangle = 0; triangle < curr_n; ++triangle) {
        const int previous = matches[triangle];
        if (!faces[triangle].eligible || previous < 0 || previous >= prev_n ||
            !gpu_temporal_mesh_valid(&prev[previous]) ||
            !gpu_temporal_compatible(&prev[previous], &curr[triangle])) continue;
        faces[triangle].matched = 1;
        for (int vertex = 0; vertex < 3; ++vertex) {
            const int root = gpu_temporal_mesh_root(nodes, triangle * 3 + vertex);
            GpuTemporalMeshNode *node = &nodes[root];
            const double dx = (double)prev[previous].x[vertex] - curr[triangle].x[vertex];
            const double dy = (double)prev[previous].y[vertex] - curr[triangle].y[vertex];
            if (!node->votes) {
                node->min_dx = node->max_dx = dx;
                node->min_dy = node->max_dy = dy;
            } else {
                if (dx < node->min_dx) node->min_dx = dx;
                if (dx > node->max_dx) node->max_dx = dx;
                if (dy < node->min_dy) node->min_dy = dy;
                if (dy > node->max_dy) node->max_dy = dy;
            }
            ++node->votes;
            from[triangle].q[vertex] = prev[previous].q[vertex];
        }
    }
    for (int corner = 0; corner < vertex_count; ++corner) {
        GpuTemporalMeshNode *node = &nodes[corner];
        double spread_x, spread_y;
        if (node->parent != corner || node->first < 0 || !node->votes) continue;
        spread_x = node->max_dx - node->min_dx;
        spread_y = node->max_dy - node->min_dy;
        if (spread_x * spread_x + spread_y * spread_y >
            GPU_TEMPORAL_MESH_VOTE_EPSILON * GPU_TEMPORAL_MESH_VOTE_EPSILON) {
            node->conflict = node->held = 1;
        } else if (!node->held) {
            /* A bounding-box midpoint makes consensus independent of triangle
             * submission order, unlike an accumulating floating-point mean. */
            node->dx = (node->min_dx + node->max_dx) * 0.5;
            node->dy = (node->min_dy + node->max_dy) * 0.5;
        }
    }

    for (int triangle = 0; triangle < curr_n; ++triangle) {
        if (!faces[triangle].eligible) continue;
        queue[queue_tail] = triangle;
        queue_tail = (queue_tail + 1) % curr_n;
        ++queue_count;
        faces[triangle].queued = 1;
    }
    while (queue_count) {
        const int batch_count = queue_count;
        int pending_head = -1;
        /* Evaluate a whole batch before applying any pins: a face's safety
         * decision cannot depend on whether its neighbor was submitted first. */
        for (int item = 0; item < batch_count; ++item) {
            const int triangle = queue[queue_head];
            GpuTemporalTriangle candidate = from[triangle];
            queue_head = (queue_head + 1) % curr_n;
            --queue_count;
            faces[triangle].queued = 0;
            gpu_temporal_mesh_write_triangle(&curr[triangle], triangle, nodes, &candidate);
            if (gpu_temporal_mesh_valid(&candidate) &&
                gpu_temporal_compatible(&curr[triangle], &candidate)) continue;
            for (int vertex = 0; vertex < 3; ++vertex) {
                const int root = gpu_temporal_mesh_root(nodes, triangle * 3 + vertex);
                GpuTemporalMeshNode *node = &nodes[root];
                if (node->pending || (node->dx == 0.0 && node->dy == 0.0)) continue;
                node->pending = 1;
                node->pending_next = pending_head;
                pending_head = root;
            }
        }
        while (pending_head >= 0) {
            GpuTemporalMeshNode *node = &nodes[pending_head];
            pending_head = node->pending_next;
            node->dx = node->dy = 0.0;
            node->held = 1;
            node->pending = 0;
            for (int corner = node->first; corner >= 0; corner = nodes[corner].next) {
                const int affected = corner / 3;
                if (!faces[affected].eligible || faces[affected].queued) continue;
                queue[queue_tail] = affected;
                queue_tail = (queue_tail + 1) % curr_n;
                ++queue_count;
                faces[affected].queued = 1;
            }
        }
    }

    for (int triangle = 0; triangle < curr_n; ++triangle) {
        int moving = 0, position_motion = 0;
        if (!faces[triangle].eligible) continue;
        gpu_temporal_mesh_write_triangle(&curr[triangle], triangle, nodes, &from[triangle]);
        for (int vertex = 0; vertex < 3; ++vertex) {
            const int root = gpu_temporal_mesh_root(nodes, triangle * 3 + vertex);
            const int changed = from[triangle].x[vertex] != curr[triangle].x[vertex] ||
                                from[triangle].y[vertex] != curr[triangle].y[vertex];
            position_motion |= changed;
            if (stats && !faces[triangle].matched && changed && nodes[root].votes)
                ++stats->propagated_vertices;
        }
        /* A completely held face keeps its current perspective mapping too. */
        if (!position_motion)
            for (int vertex = 0; vertex < 3; ++vertex)
                from[triangle].q[vertex] = curr[triangle].q[vertex];
        for (int vertex = 0; vertex < 3; ++vertex)
            moving |= from[triangle].x[vertex] != curr[triangle].x[vertex] ||
                      from[triangle].y[vertex] != curr[triangle].y[vertex] ||
                      from[triangle].q[vertex] != curr[triangle].q[vertex];
        moving_triangles += moving != 0;
    }
    if (stats) for (int corner = 0; corner < vertex_count; ++corner) {
        const GpuTemporalMeshNode *node = &nodes[corner];
        if (node->parent != corner || node->first < 0) continue;
        stats->seeded_vertices += node->votes > 0;
        stats->shared_vertices += node->size > 1;
        stats->conflicting_vertices += node->conflict != 0;
        stats->held_vertices += node->held || !node->votes;
    }

cleanup:
    free(links);
    free(queue);
    free(faces);
    free(edges);
    free(points);
    free(nodes);
    return moving_triangles;
}

#endif /* PSXRECOMP_GPU_TEMPORAL_MESH_H */
