// Automatic placement of boxes joined by links, independent of the .loo format.
//
// Used by Editor::autoLayout to place entities and associations so that a schema looks tidy without anyone
// choosing coordinates. The algorithm is deterministic: the same input always gives the same output.
#pragma once

#include <cstddef>
#include <utility>
#include <vector>

namespace loo {

struct LayoutNode {
    double x = 0, y = 0;     // centre of the box
    double w = 100, h = 60;  // size of the box
    bool pinned = false;     // pinned boxes never move; the others are arranged around them
};

using LayoutEdge = std::pair<std::size_t, std::size_t>;  // indexes into the node list

// ---- Tuning: change these and rebuild to adjust how the layout looks (see docs/layout.md) -----------------------
// Smallest empty gap kept between two boxes, in pixels.
constexpr double kLayoutGap = 40;
// Typical distance between linked boxes, relative to box size: smaller packs the diagram tighter.
constexpr double kLayoutSpread = 0.85;
// How strongly long links are punished (cost per pixel); raise it to bring linked boxes closer together.
constexpr double kLayoutLinkLength = 0.15;
// Size assumed for an association's ellipse, which Looping computes itself (entities use their stored size):
// width = max(MinWidth, BaseWidth + CharWidth * longest text), height = Height + AttributeHeight * attributes.
constexpr int kAssociationMinWidth = 75, kAssociationBaseWidth = 32, kAssociationCharWidth = 7;
constexpr int kAssociationHeight = 60, kAssociationAttributeHeight = 17;
// ----------------------------------------------------------------------------------------------------------------

// Moves every unpinned node. With no pinned node, the result is also translated to start near the origin and
// unconnected parts are packed side by side; with pinned nodes, the pinned part keeps its coordinates.
// Coordinates of moved nodes are multiples of 10.
void arrangeGraph(std::vector<LayoutNode>& nodes, const std::vector<LayoutEdge>& edges);

// Quality of a placement (edges are straight lines between box centres).
struct LayoutQuality {
    int crossings = 0;       // pairs of links that cross (links sharing a box do not count)
    int throughBoxes = 0;    // links that pass over a box they do not end in
    int overlaps = 0;        // pairs of boxes closer than kLayoutGap
    double edgeLength = 0;   // total length of the links
};
LayoutQuality measureLayout(const std::vector<LayoutNode>& nodes, const std::vector<LayoutEdge>& edges);

}  // namespace loo
