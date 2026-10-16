#include "loo/layout.hpp"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <random>

namespace loo {
namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kMargin = 60;  // empty border around a freshly arranged schema
constexpr int kGrid = 10;       // coordinates of moved boxes are multiples of this

// mt19937 gives the same numbers on every platform; std::uniform_real_distribution does not.
struct Rng {
    std::mt19937 gen;
    explicit Rng(unsigned seed) : gen(seed) {}
    double next() { return static_cast<double>(gen() >> 8) / 16777216.0; }  // [0, 1)
};

double snap(double v) { return std::round(v / kGrid) * kGrid; }
double ceilGrid(double v) { return std::ceil(v / kGrid) * kGrid; }

struct Bounds {
    double minX = 1e18, minY = 1e18, maxX = -1e18, maxY = -1e18;
};

Bounds boundsOf(const std::vector<LayoutNode>& n) {
    Bounds b;
    for (const auto& node : n) {
        b.minX = std::min(b.minX, node.x - node.w / 2);
        b.maxX = std::max(b.maxX, node.x + node.w / 2);
        b.minY = std::min(b.minY, node.y - node.h / 2);
        b.maxY = std::max(b.maxY, node.y + node.h / 2);
    }
    return b;
}

// ---- geometry ---------------------------------------------------------------------------------

double orient(double ax, double ay, double bx, double by, double cx, double cy) {
    return (bx - ax) * (cy - ay) - (by - ay) * (cx - ax);
}

// True when the open segments p1p2 and p3p4 cross each other.
bool segmentsCross(double x1, double y1, double x2, double y2, double x3, double y3, double x4, double y4) {
    const double d1 = orient(x3, y3, x4, y4, x1, y1), d2 = orient(x3, y3, x4, y4, x2, y2);
    const double d3 = orient(x1, y1, x2, y2, x3, y3), d4 = orient(x1, y1, x2, y2, x4, y4);
    return ((d1 > 0) != (d2 > 0)) && ((d3 > 0) != (d4 > 0)) && d1 != 0 && d2 != 0 && d3 != 0 && d4 != 0;
}

// Liang-Barsky: does the segment pass through the (shrunk) rectangle of node b?
bool segmentHitsBox(double x1, double y1, double x2, double y2, const LayoutNode& b) {
    const double left = b.x - b.w / 2 + 2, right = b.x + b.w / 2 - 2, top = b.y - b.h / 2 + 2, bottom = b.y + b.h / 2 - 2;
    double t0 = 0, t1 = 1;
    const double dx = x2 - x1, dy = y2 - y1;
    const double p[4] = {-dx, dx, -dy, dy};
    const double q[4] = {x1 - left, right - x1, y1 - top, bottom - y1};
    for (int i = 0; i < 4; ++i) {
        if (p[i] == 0) {
            if (q[i] < 0) return false;
        } else {
            const double t = q[i] / p[i];
            if (p[i] < 0) t0 = std::max(t0, t);
            else t1 = std::min(t1, t);
            if (t0 > t1) return false;
        }
    }
    return true;
}

// ---- overlap removal ----------------------------------------------------------------------------

// Pushes boxes apart along the axis of least penetration until every pair is `gap` apart (or a pass limit is hit).
void removeOverlaps(std::vector<LayoutNode>& n, double gap) {
    for (int pass = 0; pass < 3000; ++pass) {
        bool moved = false;
        for (std::size_t i = 0; i < n.size(); ++i) {
            for (std::size_t j = i + 1; j < n.size(); ++j) {
                if (n[i].pinned && n[j].pinned) continue;
                double dx = n[j].x - n[i].x, dy = n[j].y - n[i].y;
                const double ox = (n[i].w + n[j].w) / 2 + gap - std::fabs(dx);
                const double oy = (n[i].h + n[j].h) / 2 + gap - std::fabs(dy);
                if (ox <= 0 || oy <= 0) continue;
                moved = true;
                double mx = 0, my = 0;
                if (ox < oy) mx = (ox + 0.5) * (dx < 0 ? -1 : 1);
                else my = (oy + 0.5) * (dy < 0 ? -1 : 1);
                const double shareI = n[i].pinned ? 0 : (n[j].pinned ? 1 : 0.5);
                const double shareJ = n[j].pinned ? 0 : (n[i].pinned ? 1 : 0.5);
                n[i].x -= mx * shareI;
                n[i].y -= my * shareI;
                n[j].x += mx * shareJ;
                n[j].y += my * shareJ;
            }
        }
        if (!moved) return;
    }
}

// ---- force-directed placement ---------------------------------------------------------------------

void forceDirected(std::vector<LayoutNode>& n, const std::vector<LayoutEdge>& edges, double k) {
    const int iterations = 300;
    const std::size_t count = n.size();
    std::vector<double> dx(count), dy(count);

    double pinnedX = 0, pinnedY = 0;
    std::size_t pinnedCount = 0;
    for (const auto& node : n) {
        if (node.pinned) {
            pinnedX += node.x;
            pinnedY += node.y;
            ++pinnedCount;
        }
    }

    for (int it = 0; it < iterations; ++it) {
        const double temperature = k * (1.0 - static_cast<double>(it) / iterations) + 1;
        std::fill(dx.begin(), dx.end(), 0.0);
        std::fill(dy.begin(), dy.end(), 0.0);

        for (std::size_t i = 0; i < count; ++i) {
            for (std::size_t j = i + 1; j < count; ++j) {
                double ex = n[i].x - n[j].x, ey = n[i].y - n[j].y;
                double d = std::sqrt(ex * ex + ey * ey);
                if (d < 0.01) {  // coincident: separate along a fixed direction
                    ex = 1;
                    ey = 0.5;
                    d = std::sqrt(1.25);
                }
                const double f = k * k / d;
                dx[i] += ex / d * f;
                dy[i] += ey / d * f;
                dx[j] -= ex / d * f;
                dy[j] -= ey / d * f;
            }
        }
        for (const auto& [a, b] : edges) {
            const double ex = n[a].x - n[b].x, ey = n[a].y - n[b].y;
            const double d = std::sqrt(ex * ex + ey * ey) + 0.01;
            const double f = d * d / k;
            dx[a] -= ex / d * f;
            dy[a] -= ey / d * f;
            dx[b] += ex / d * f;
            dy[b] += ey / d * f;
        }

        // Weak pull towards the centre keeps unconnected boxes from drifting away.
        double cx = 0, cy = 0;
        if (pinnedCount) {
            cx = pinnedX / static_cast<double>(pinnedCount);
            cy = pinnedY / static_cast<double>(pinnedCount);
        } else {
            for (const auto& node : n) {
                cx += node.x;
                cy += node.y;
            }
            cx /= static_cast<double>(count);
            cy /= static_cast<double>(count);
        }

        for (std::size_t i = 0; i < count; ++i) {
            if (n[i].pinned) continue;
            dx[i] -= (n[i].x - cx) * 0.05;
            dy[i] -= (n[i].y - cy) * 0.05;
            const double len = std::sqrt(dx[i] * dx[i] + dy[i] * dy[i]);
            if (len < 1e-9) continue;
            const double step = std::min(len, temperature);
            n[i].x += dx[i] / len * step;
            n[i].y += dy[i] / len * step;
        }
    }
}

// Cost terms (lower is better). Crossings and boxes in the way matter most, then keeping links horizontal or
// vertical, then short links and a compact landscape shape.
constexpr double kCrossing = 1000, kThroughBox = 800, kOverlap = 5000, kSlant = 0.2;
constexpr double kLength = kLayoutLinkLength;

double edgeCost(const std::vector<LayoutNode>& n, const LayoutEdge& e) {
    const double dx = std::fabs(n[e.first].x - n[e.second].x), dy = std::fabs(n[e.first].y - n[e.second].y);
    return kLength * std::hypot(dx, dy) + kSlant * std::min(dx, dy);
}

// Cost of the overall shape: charged for being portrait or very long and thin.
double shapeCost(const std::vector<LayoutNode>& n) {
    const Bounds b = boundsOf(n);
    const double w = b.maxX - b.minX, h = b.maxY - b.minY, ratio = w / std::max(1.0, h);
    double cost = 0.0001 * w * h;
    if (ratio < 1) cost += 300 * (1 / std::max(ratio, 0.01) - 1);
    if (ratio > 2.2) cost += 300 * (ratio - 2.2);
    return cost;
}

double score(const std::vector<LayoutNode>& n, const LayoutQuality& q, const std::vector<LayoutEdge>& edges) {
    double cost = kCrossing * q.crossings + kThroughBox * q.throughBoxes + kOverlap * q.overlaps + shapeCost(n);
    for (const auto& e : edges) cost += edgeCost(n, e);
    return cost;
}

bool boxesTooClose(const LayoutNode& a, const LayoutNode& b) {
    return std::fabs(b.x - a.x) < (a.w + b.w) / 2 + kLayoutGap && std::fabs(b.y - a.y) < (a.h + b.h) / 2 + kLayoutGap;
}

// Every cost term that involves node i, so a move of i can be judged without re-scoring the whole graph.
double localCost(const std::vector<LayoutNode>& n, const std::vector<LayoutEdge>& edges, std::size_t i) {
    double cost = 0;
    for (std::size_t m = 0; m < n.size(); ++m) {
        if (m != i && boxesTooClose(n[i], n[m])) cost += kOverlap;
    }
    for (std::size_t x = 0; x < edges.size(); ++x) {
        const auto [a, b] = edges[x];
        if (a == i || b == i) {
            cost += edgeCost(n, edges[x]);
            for (std::size_t m = 0; m < n.size(); ++m) {
                if (m != a && m != b && segmentHitsBox(n[a].x, n[a].y, n[b].x, n[b].y, n[m])) cost += kThroughBox;
            }
            for (std::size_t y = 0; y < edges.size(); ++y) {
                const auto [c, d] = edges[y];
                if (a == c || a == d || b == c || b == d) continue;
                if (segmentsCross(n[a].x, n[a].y, n[b].x, n[b].y, n[c].x, n[c].y, n[d].x, n[d].y)) cost += kCrossing;
            }
        } else if (segmentHitsBox(n[a].x, n[a].y, n[b].x, n[b].y, n[i])) {
            cost += kThroughBox;
        }
    }
    return cost;
}

// Local search on the grid: moves one box at a time (small steps, or lining up with a neighbour) while that helps.
void improve(std::vector<LayoutNode>& n, const std::vector<LayoutEdge>& edges) {
    std::vector<std::vector<std::size_t>> neighbours(n.size());
    for (const auto& [a, b] : edges) {
        neighbours[a].push_back(b);
        neighbours[b].push_back(a);
    }
    static const double steps[] = {10, 20, 30, 50, 80, 130};

    for (int pass = 0; pass < 20; ++pass) {
        bool better = false;
        for (std::size_t i = 0; i < n.size(); ++i) {
            if (n[i].pinned) continue;
            const double ox = n[i].x, oy = n[i].y;
            std::vector<std::pair<double, double>> candidates;
            for (double s : steps) {
                candidates.push_back({ox + s, oy});
                candidates.push_back({ox - s, oy});
                candidates.push_back({ox, oy + s});
                candidates.push_back({ox, oy - s});
            }
            for (std::size_t m : neighbours[i]) {
                candidates.push_back({snap(n[m].x), oy});  // same column as a neighbour
                candidates.push_back({ox, snap(n[m].y)});  // same row as a neighbour
            }

            const double base = localCost(n, edges, i) + shapeCost(n);
            double bestCost = base - 1e-6, bestX = ox, bestY = oy;
            for (const auto& [cx, cy] : candidates) {
                if (cx == ox && cy == oy) continue;
                n[i].x = cx;
                n[i].y = cy;
                const double cost = localCost(n, edges, i) + shapeCost(n);
                if (cost < bestCost) {
                    bestCost = cost;
                    bestX = cx;
                    bestY = cy;
                }
            }
            n[i].x = bestX;
            n[i].y = bestY;
            if (bestX != ox || bestY != oy) better = true;
        }
        if (!better) return;
    }
}

// Arranges one connected piece (or, with pinned boxes, the whole graph): several seeded attempts, best one wins.
void arrangePiece(std::vector<LayoutNode>& nodes, const std::vector<LayoutEdge>& edges) {
    std::size_t freeCount = 0;
    double areaSum = 0, pinnedX = 0, pinnedY = 0;
    std::size_t pinnedCount = 0;
    for (const auto& node : nodes) {
        areaSum += (node.w + kLayoutGap) * (node.h + kLayoutGap);
        if (node.pinned) {
            pinnedX += node.x;
            pinnedY += node.y;
            ++pinnedCount;
        } else {
            ++freeCount;
        }
    }
    if (freeCount == 0) return;

    const double k = kLayoutSpread * std::sqrt(areaSum / static_cast<double>(nodes.size()));
    const double centreX = pinnedCount ? pinnedX / static_cast<double>(pinnedCount) : 0;
    const double centreY = pinnedCount ? pinnedY / static_cast<double>(pinnedCount) : 0;
    const double radius = 0.6 * k * std::sqrt(static_cast<double>(freeCount));
    const int attempts = nodes.size() > 60 ? 6 : nodes.size() > 30 ? 12 : 30;

    std::vector<LayoutNode> best;
    double bestScore = 1e300;
    for (int attempt = 0; attempt < attempts; ++attempt) {
        Rng rng(1000u + static_cast<unsigned>(attempt));
        std::vector<LayoutNode> trial = nodes;
        for (auto& node : trial) {
            if (node.pinned) continue;
            const double angle = 2 * kPi * rng.next(), r = radius * std::sqrt(rng.next());
            node.x = centreX + r * std::cos(angle);
            node.y = centreY + r * std::sin(angle);
        }
        forceDirected(trial, edges, k);
        removeOverlaps(trial, kLayoutGap + kGrid);  // spare room for the snap to the grid below
        for (auto& node : trial) {
            if (!node.pinned) {
                node.x = snap(node.x);
                node.y = snap(node.y);
            }
        }
        improve(trial, edges);
        const double s = score(trial, measureLayout(trial, edges), edges);
        if (s < bestScore) {
            bestScore = s;
            best = std::move(trial);
        }
    }
    nodes = std::move(best);
}

}  // namespace

LayoutQuality measureLayout(const std::vector<LayoutNode>& n, const std::vector<LayoutEdge>& edges) {
    LayoutQuality q;
    for (std::size_t i = 0; i < n.size(); ++i) {
        for (std::size_t j = i + 1; j < n.size(); ++j) {
            const double ox = (n[i].w + n[j].w) / 2 + kLayoutGap - std::fabs(n[j].x - n[i].x);
            const double oy = (n[i].h + n[j].h) / 2 + kLayoutGap - std::fabs(n[j].y - n[i].y);
            if (ox > 0 && oy > 0) ++q.overlaps;
        }
    }
    for (std::size_t i = 0; i < edges.size(); ++i) {
        const auto [a, b] = edges[i];
        q.edgeLength += std::hypot(n[a].x - n[b].x, n[a].y - n[b].y);
        for (std::size_t m = 0; m < n.size(); ++m) {
            if (m != a && m != b && segmentHitsBox(n[a].x, n[a].y, n[b].x, n[b].y, n[m])) ++q.throughBoxes;
        }
        for (std::size_t j = i + 1; j < edges.size(); ++j) {
            const auto [c, d] = edges[j];
            if (a == c || a == d || b == c || b == d) continue;
            if (segmentsCross(n[a].x, n[a].y, n[b].x, n[b].y, n[c].x, n[c].y, n[d].x, n[d].y)) ++q.crossings;
        }
    }
    return q;
}

void arrangeGraph(std::vector<LayoutNode>& nodes, const std::vector<LayoutEdge>& edges) {
    if (nodes.empty()) return;

    const bool anyPinned = std::any_of(nodes.begin(), nodes.end(), [](const LayoutNode& n) { return n.pinned; });
    if (anyPinned) {
        arrangePiece(nodes, edges);
        return;
    }

    // Connected pieces are arranged on their own, then packed in rows.
    std::vector<std::size_t> parent(nodes.size());
    std::iota(parent.begin(), parent.end(), std::size_t{0});
    auto find = [&](std::size_t v) {
        while (parent[v] != v) v = parent[v] = parent[parent[v]];
        return v;
    };
    for (const auto& [a, b] : edges) parent[find(a)] = find(b);

    struct Piece {
        std::vector<std::size_t> members;  // indexes into `nodes`
        std::vector<LayoutNode> boxes;
        double width = 0, height = 0, area = 0;
    };
    std::vector<Piece> pieces;
    std::vector<int> pieceOf(nodes.size(), -1);
    std::vector<int> rootPiece(nodes.size(), -1);
    for (std::size_t i = 0; i < nodes.size(); ++i) {
        const std::size_t root = find(i);
        if (rootPiece[root] < 0) {
            rootPiece[root] = static_cast<int>(pieces.size());
            pieces.emplace_back();
        }
        pieceOf[i] = rootPiece[root];
        pieces[static_cast<std::size_t>(pieceOf[i])].members.push_back(i);
    }

    for (auto& piece : pieces) {
        std::vector<std::size_t> local(nodes.size(), 0);
        for (std::size_t m = 0; m < piece.members.size(); ++m) {
            local[piece.members[m]] = m;
            piece.boxes.push_back(nodes[piece.members[m]]);
        }
        std::vector<LayoutEdge> localEdges;
        for (const auto& [a, b] : edges) {
            if (pieceOf[a] == pieceOf[piece.members[0]]) localEdges.emplace_back(local[a], local[b]);
        }
        if (piece.boxes.size() > 1) arrangePiece(piece.boxes, localEdges);
        else piece.boxes[0].x = piece.boxes[0].y = 0;
        const Bounds b = boundsOf(piece.boxes);
        piece.width = b.maxX - b.minX;
        piece.height = b.maxY - b.minY;
        piece.area = piece.width * piece.height;
        // Shift so the piece's top-left corner is at the origin while centres stay on the grid.
        const double sx = ceilGrid(-b.minX), sy = ceilGrid(-b.minY);
        for (auto& box : piece.boxes) {
            box.x += sx;
            box.y += sy;
        }
        piece.width += sx + b.minX;  // distance from the origin to the right edge after the shift
        piece.height += sy + b.minY;
    }

    std::vector<std::size_t> order(pieces.size());
    std::iota(order.begin(), order.end(), std::size_t{0});
    std::stable_sort(order.begin(), order.end(),
                     [&](std::size_t a, std::size_t b) { return pieces[a].area > pieces[b].area; });

    double totalArea = 0, widest = 0;
    for (const auto& p : pieces) {
        totalArea += (p.width + 2 * kLayoutGap) * (p.height + 2 * kLayoutGap);
        widest = std::max(widest, p.width);
    }
    const double rowWidth = std::max(widest, 1.6 * std::sqrt(totalArea));
    const double gap = ceilGrid(2 * kLayoutGap);

    double cursorX = kMargin, cursorY = kMargin, rowHeight = 0;
    for (std::size_t index : order) {
        Piece& p = pieces[index];
        if (cursorX > kMargin && cursorX - kMargin + p.width > rowWidth) {
            cursorX = kMargin;
            cursorY += ceilGrid(rowHeight + gap);
            rowHeight = 0;
        }
        for (std::size_t m = 0; m < p.members.size(); ++m) {
            nodes[p.members[m]].x = p.boxes[m].x + cursorX;
            nodes[p.members[m]].y = p.boxes[m].y + cursorY;
        }
        cursorX += ceilGrid(p.width + gap);
        rowHeight = std::max(rowHeight, p.height);
    }
}

}  // namespace loo
