// Checks the automatic layout: no overlaps, readable links, determinism, pinned boxes, and the editor integration.
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>

#include "loo/editor.hpp"
#include "loo/layout.hpp"

using namespace loo;

static int failures = 0, checks = 0;

static void check(bool ok, const std::string& what) {
    ++checks;
    if (!ok) {
        ++failures;
        std::cout << "FAIL  " << what << "\n";
    }
}

static std::vector<LayoutNode> boxes(std::size_t count, double w = 110, double h = 80) {
    std::vector<LayoutNode> n(count);
    for (auto& b : n) {
        b.w = w;
        b.h = h;
    }
    return n;
}

static bool onGrid(const LayoutNode& n) { return std::fmod(n.x, 10) == 0 && std::fmod(n.y, 10) == 0; }

static void graphTests() {
    // A chain has no reason to cross anything.
    {
        auto n = boxes(9);
        std::vector<LayoutEdge> e;
        for (std::size_t i = 0; i + 1 < n.size(); ++i) e.emplace_back(i, i + 1);
        arrangeGraph(n, e);
        const auto q = measureLayout(n, e);
        check(q.overlaps == 0 && q.crossings == 0 && q.throughBoxes == 0, "chain is clean");
        check(std::all_of(n.begin(), n.end(), onGrid), "chain lies on the grid");
    }
    // A hub with many neighbours: nothing may sit on top of anything or on a link.
    {
        auto n = boxes(11);
        std::vector<LayoutEdge> e;
        for (std::size_t i = 1; i < n.size(); ++i) e.emplace_back(0, i);
        arrangeGraph(n, e);
        const auto q = measureLayout(n, e);
        check(q.overlaps == 0 && q.crossings == 0 && q.throughBoxes == 0, "star is clean");
    }
    // Same input, same output.
    {
        auto a = boxes(14), b = boxes(14);
        std::vector<LayoutEdge> e;
        for (std::size_t i = 0; i + 1 < a.size(); ++i) e.emplace_back(i, i + 1);
        e.emplace_back(0, 7);
        e.emplace_back(3, 11);
        arrangeGraph(a, e);
        arrangeGraph(b, e);
        bool same = true;
        for (std::size_t i = 0; i < a.size(); ++i) same = same && a[i].x == b[i].x && a[i].y == b[i].y;
        check(same, "layout is deterministic");
    }
    // Several unconnected pieces and loose boxes are packed without overlapping and start near the origin.
    {
        auto n = boxes(15);
        std::vector<LayoutEdge> e = {{0, 1}, {1, 2}, {3, 4}, {4, 5}, {5, 3}, {6, 7}};
        arrangeGraph(n, e);
        const auto q = measureLayout(n, e);
        check(q.overlaps == 0 && q.throughBoxes == 0, "separate pieces do not overlap");
        double minX = 1e9, minY = 1e9;
        for (const auto& b : n) {
            minX = std::min(minX, b.x - b.w / 2);
            minY = std::min(minY, b.y - b.h / 2);
        }
        check(minX >= 0 && minX < 200 && minY >= 0 && minY < 200, "result starts near the top-left corner");
    }
    // Bigger graph (a ring with chords), the shape of a real schema.
    {
        auto n = boxes(48);
        std::vector<LayoutEdge> e;
        for (std::size_t i = 0; i < n.size(); ++i) e.emplace_back(i, (i + 1) % n.size());
        for (std::size_t i = 0; i < n.size(); i += 3) e.emplace_back(i, (i * 7 + 5) % n.size());
        e.erase(std::remove_if(e.begin(), e.end(), [](const LayoutEdge& x) { return x.first == x.second; }), e.end());
        arrangeGraph(n, e);
        check(measureLayout(n, e).overlaps == 0, "48 connected boxes do not overlap");
    }
    // Pinned boxes stay; the others are arranged around them without touching them.
    {
        auto n = boxes(8);
        n[0].x = 500;
        n[0].y = 300;
        n[1].x = 900;
        n[1].y = 300;
        n[0].pinned = n[1].pinned = true;
        std::vector<LayoutEdge> e = {{0, 2}, {1, 2}, {2, 3}, {3, 4}, {1, 5}, {5, 6}, {6, 7}};
        arrangeGraph(n, e);
        check(n[0].x == 500 && n[0].y == 300 && n[1].x == 900 && n[1].y == 300, "pinned boxes keep their place");
        check(measureLayout(n, e).overlaps == 0, "free boxes keep clear of pinned ones");
    }
}

static bool overlap(const OwnerInfo& a, const OwnerInfo& b, bool aEntity, bool bEntity) {
    auto centre = [](const OwnerInfo& o, bool entity) {
        return std::pair<double, double>{o.position.x + (entity ? o.width / 2.0 : 0),
                                         o.position.y + (entity ? o.height / 2.0 : 0)};
    };
    const auto [ax, ay] = centre(a, aEntity);
    const auto [bx, by] = centre(b, bEntity);
    return std::fabs(ax - bx) < (a.width + b.width) / 2.0 + kLayoutGap - 1.5 &&
           std::fabs(ay - by) < (a.height + b.height) / 2.0 + kLayoutGap - 1.5;  // 1.5: rounding to whole pixels
}

static int overlaps(const Schema& s) {
    std::vector<std::pair<const OwnerInfo*, bool>> all;
    for (const auto& e : s.entities) all.emplace_back(&e, true);
    for (const auto& a : s.associations) all.emplace_back(&a, false);
    int count = 0;
    for (std::size_t i = 0; i < all.size(); ++i)
        for (std::size_t j = i + 1; j < all.size(); ++j)
            if (overlap(*all[i].first, *all[j].first, all[i].second, all[j].second)) ++count;
    return count;
}

static void editorTests(const std::filesystem::path& corpus) {
    // A schema built from nothing: everything starts on the default grid, then gets arranged.
    auto ed = Editor::create();
    for (const char* name : {"client", "commande", "produit", "categorie", "fournisseur"}) ed.addOwner(OwnerKind::Entity, name);
    for (const char* name : {"passer", "contenir", "classer", "fournir"}) ed.addOwner(OwnerKind::Association, name);
    ed.addAttribute(OwnerKind::Entity, "client", {"id_client", "INT", true});
    ed.addAttribute(OwnerKind::Entity, "client", {"nom", "VARCHAR(80)"});
    ed.addLink("client", "passer", Cardinality::ZeroN);
    ed.addLink("commande", "passer", Cardinality::OneOne);
    ed.addLink("commande", "contenir", Cardinality::ZeroN);
    ed.addLink("produit", "contenir", Cardinality::ZeroN);
    ed.addLink("produit", "classer", Cardinality::OneOne);
    ed.addLink("categorie", "classer", Cardinality::ZeroN);
    ed.addLink("produit", "fournir", Cardinality::ZeroN);
    ed.addLink("fournisseur", "fournir", Cardinality::ZeroN);

    const std::size_t moved = ed.autoLayout();
    check(moved > 0, "autoLayout moves something");
    const Schema s = ed.schema();
    check(overlaps(s) == 0, "no overlapping boxes after autoLayout");
    check(s.entities.size() == 5 && s.associations.size() == 4 && s.links.size() == 8, "autoLayout keeps the content");
    check(s.entities[0].attributes.size() == 2, "autoLayout keeps the attributes");

    // The layout survives a save and reload, and a second run changes nothing.
    auto reopened = Editor::open(ed.save());
    check(reopened.autoLayout() == 0, "autoLayout is stable on its own output");
    const Schema s2 = reopened.schema();
    bool same = true;
    for (std::size_t i = 0; i < s.entities.size(); ++i) same = same && s.entities[i].position.x == s2.entities[i].position.x;
    check(same, "positions survive save and reload");

    // `only`: the other objects stay exactly where they are.
    ed.addOwner(OwnerKind::Entity, "pays");
    ed.addOwner(OwnerKind::Association, "situer");
    ed.addLink("pays", "situer", Cardinality::ZeroN);
    ed.addLink("fournisseur", "situer", Cardinality::OneOne);
    const Schema before = ed.schema();
    ed.autoLayout({"pays", "situer"});
    const Schema after = ed.schema();
    bool kept = true;
    for (std::size_t i = 0; i + 1 < before.entities.size(); ++i)
        kept = kept && before.entities[i].position.x == after.entities[i].position.x &&
               before.entities[i].position.y == after.entities[i].position.y;
    for (std::size_t i = 0; i + 1 < before.associations.size(); ++i)
        kept = kept && before.associations[i].position.x == after.associations[i].position.x;
    check(kept, "objects not named in `only` stay in place");
    check(overlaps(after) == 0, "new objects do not land on old ones");
    bool threw = false;
    try {
        ed.autoLayout({"nope"});
    } catch (const Error&) {
        threw = true;
    }
    check(threw, "unknown name in `only` is refused");

    // A big real model made in Looping.
    std::ifstream f(corpus / "looping_file_example.loo", std::ios::binary);
    std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    auto big = Editor::open(bytes);
    const Schema bigBefore = big.schema();
    big.autoLayout();
    const Schema bigAfter = big.schema();
    check(bigAfter.entities.size() == bigBefore.entities.size() && bigAfter.links.size() == bigBefore.links.size(),
          "layout of the big model keeps its content");
    check(overlaps(bigAfter) == 0, "big model has no overlapping boxes");
    check(Editor::open(big.save()).schema().links.size() == bigBefore.links.size(), "big model reloads");
}

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "usage: layout_test CORPUS_DIR\n";
        return 2;
    }
    graphTests();
    editorTests(argv[1]);
    std::cout << checks << " checks, " << failures << " failed\n";
    return failures ? 1 : 0;
}
