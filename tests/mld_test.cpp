// Compares our MLD derivation with the MLD that Looping itself displayed (screenshots supplied by the user).
// Only names, primary keys and foreign keys are compared: that is what the screenshots show.
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <set>

#include "loo/editor.hpp"
#include "loo/mld.hpp"

namespace fs = std::filesystem;

// "name(col*, col>table.col, col*>table.col)": * = primary key, >t.c = foreign key to t.c
static std::string compact(const loo::MldTable& t) {
    std::string s = t.name + "(";
    for (std::size_t i = 0; i < t.columns.size(); ++i) {
        const auto& c = t.columns[i];
        if (i) s += ",";
        s += c.name;
        if (c.primaryKey) s += "*";
        if (!c.refTable.empty()) s += ">" + c.refTable + "." + c.refColumn;
    }
    return s + ")";
}

static int failures = 0;

static void compare(const std::string& what, const loo::Mld& mld, std::set<std::string> expected) {
    std::set<std::string> actual;
    for (const auto& t : mld.tables) actual.insert(compact(t));
    if (actual == expected) {
        std::cout << "ok    " << what << " (" << actual.size() << " tables match Looping)\n";
        return;
    }
    ++failures;
    std::cout << "FAIL  " << what << ": our MLD differs from Looping's\n";
    for (const auto& e : expected) {
        if (!actual.count(e)) std::cout << "      Looping has, we lack:  " << e << "\n";
    }
    for (const auto& a : actual) {
        if (!expected.count(a)) std::cout << "      we have, Looping lacks: " << a << "\n";
    }
}

int main(int argc, char** argv) {
    const fs::path corpus = argc > 1 ? argv[1] : "looping_file_examples";

    // different_associations.loo, MLD as shown by Looping.
    {
        std::ifstream f(corpus / "different_associations.loo", std::ios::binary);
        const std::vector<std::uint8_t> bytes{std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>()};
        const auto schema = loo::Editor::open(bytes).schema();
        compare("different_associations.loo", loo::deriveMld(schema),
                {
                    "e1(id1*,id2>e2.id2)",                                          // 0,1 - 0,1: FK in the first entity
                    "e2(id2*)",
                    "e1_1(id1*)",                                                    // 0,1 - 1,1: FK in the 1,1 entity
                    "e2_1(id2*,id1>e1_1.id1)",
                    "e1_2(id1*)", "e2_2(id2*)", "e3_2(id2*)",                        // three 0,n links: association table,
                    "asso1_2(id1*>e1_2.id1,id2*>e2_2.id2,id2_1*>e3_2.id2)",          //   clashing id2 becomes id2_1
                    "e2_3(id2*,id2_1>e2_3.id2)",                                     // reflexive 0,1 + 1,1: FK to itself
                });
    }

    // The demo schema built by our own editor (out/demo.loo), MLD as shown by Looping.
    {
        using loo::OwnerKind;
        auto ed = loo::Editor::create();
        for (auto n : {"client", "commande", "produit"}) ed.addOwner(OwnerKind::Entity, n);
        for (auto n : {"passer", "contenir"}) ed.addOwner(OwnerKind::Association, n);
        ed.addAttribute(OwnerKind::Entity, "client", {.name = "id_client", .type = "INT", .identifier = true});
        ed.addAttribute(OwnerKind::Entity, "client", {.name = "nom", .type = "VARCHAR", .notNull = true});
        ed.addAttribute(OwnerKind::Entity, "client", {.name = "adresse_\xc3\xa9lectronique", .type = "VARCHAR", .unique = true});
        ed.addAttribute(OwnerKind::Entity, "commande", {.name = "id_commande", .type = "BIGINT", .identifier = true});
        ed.addAttribute(OwnerKind::Entity, "commande", {.name = "date_commande", .type = "DATETIME"});
        ed.addAttribute(OwnerKind::Entity, "produit", {.name = "id_produit", .type = "INT", .identifier = true});
        ed.addAttribute(OwnerKind::Entity, "produit", {.name = "prix", .type = "DECIMAL", .notNull = true});
        ed.addLink("client", "passer", loo::Cardinality::ZeroN);
        ed.addLink("commande", "passer", loo::Cardinality::OneOne);
        ed.addLink("commande", "contenir", loo::Cardinality::OneN);
        ed.addLink("produit", "contenir", loo::Cardinality::ZeroN);
        // An association can only take attributes once it has two n links (it is then a table).
        ed.addAttribute(OwnerKind::Association, "contenir", {.name = "quantit\xc3\xa9", .type = "SMALLINT"});
        compare("demo (client, commande, produit)", loo::deriveMld(ed.schema()),
                {
                    "client(id_client*,nom,adresse_\xc3\xa9lectronique)",
                    "commande(id_commande*,date_commande,id_client>client.id_client)",
                    "produit(id_produit*,prix)",
                    "contenir(id_commande*>commande.id_commande,id_produit*>produit.id_produit,quantit\xc3\xa9)",
                });
    }

    std::cout << (failures == 0 ? "MLD matches Looping in every case\n" : "MLD DOES NOT match Looping\n");
    return failures == 0 ? 0 : 1;
}
