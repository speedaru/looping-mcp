// Tests for the editing layer, checked against the corpus saved by Looping.
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>

#include "loo/editor.hpp"
#include "loo/types.hpp"

namespace fs = std::filesystem;

static int failures = 0;
static int checks = 0;

#define CHECK(cond)                                                                     \
    do {                                                                                \
        ++checks;                                                                       \
        if (!(cond)) {                                                                  \
            ++failures;                                                                 \
            std::cout << "FAIL " << __FILE__ << ":" << __LINE__ << "  " #cond << "\n";  \
        }                                                                               \
    } while (0)

#define CHECK_EQ(a, b)                                                                              \
    do {                                                                                            \
        ++checks;                                                                                   \
        const auto va = (a);                                                                        \
        const auto vb = (b);                                                                        \
        if (!(va == vb)) {                                                                          \
            ++failures;                                                                             \
            std::cout << "FAIL " << __FILE__ << ":" << __LINE__ << "  " #a " == " #b << "  (" << va \
                      << " vs " << vb << ")\n";                                                     \
        }                                                                                           \
    } while (0)

static fs::path corpus;

static std::vector<std::uint8_t> slurp(const std::string& name) {
    std::ifstream f(corpus / name, std::ios::binary);
    if (!f) throw std::runtime_error("cannot open " + name);
    return {std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>()};
}

static loo::Schema load(const std::string& name) { return loo::Editor::open(slurp(name)).schema(); }

static void readsCardinalities() {
    CHECK_EQ(loo::toString(load("link_base.loo").links.at(0).cardinality), std::string("0,n"));
    CHECK_EQ(loo::toString(load("link_01.loo").links.at(0).cardinality), std::string("0,1"));
    CHECK_EQ(loo::toString(load("link_11.loo").links.at(0).cardinality), std::string("1,1"));
    CHECK_EQ(loo::toString(load("link_1n.loo").links.at(0).cardinality), std::string("1,n"));

    const auto s = load("link_base.loo");
    CHECK_EQ(s.links.at(0).entity, std::string("e"));
    CHECK_EQ(s.links.at(0).association, std::string("r"));
    CHECK_EQ(s.links.at(0).name, std::string("Lien 1"));
}

static void readsAttributes() {
    const auto s = load("entity_with_2_attributes_one_marked_as_identifier.loo");
    CHECK_EQ(s.entities.size(), std::size_t(1));
    const auto& e = s.entities[0];
    CHECK_EQ(e.name, std::string("my_ent"));
    CHECK_EQ(e.attributes.size(), std::size_t(2));
    CHECK_EQ(e.attributes[0].name, std::string("my_identifier_attribute"));
    CHECK(e.attributes[0].identifier);
    CHECK(e.attributes[0].notNull);
    CHECK_EQ(e.attributes[0].comment, std::string("this attribute is an identifier of integers 64 bits"));
    CHECK_EQ(e.attributes[1].name, std::string("my_non_identifier_attrib"));
    CHECK(!e.attributes[1].identifier);
    CHECK_EQ(e.attributes[1].type, std::string("VARCHAR(50)"));
    // The file was re-saved with a 64-bit identifier (its comment says "integers 64 bits").
    CHECK_EQ(e.attributes[0].type, std::string("BIGINT"));

    const auto l = load("attr_logical_name.loo");
    CHECK_EQ(l.entities.at(0).attributes.at(0).logicalName, std::string("a_logical"));
    const auto nn = load("attr_notnull_unique.loo");
    CHECK(nn.entities.at(0).attributes.at(0).notNull);
    CHECK(nn.entities.at(0).attributes.at(0).unique);
}

static void readsAllTypes() {
    // Attribute names in all_sql_types.loo describe the type the user picked in the dialog.
    const auto s = load("all_sql_types.loo");
    const std::pair<const char*, const char*> expected[] = {
        {"varchar_ascii", "VARCHAR(50)"}, {"varchar_unicode", "NVARCHAR(50)"}, {"varchar_binaire", "VARBINARY(50)"},
        {"char_fixe_ascii", "CHAR(50)"},  {"texte_volumineux_ascii", "TEXT"},  {"int8", "TINYINT"},
        {"int16", "SMALLINT"},          {"int32", "INT"},                {"int64", "BIGINT"},
        {"decimal", "DECIMAL(15,2)"},   {"float32", "FLOAT"},            {"double64", "DOUBLE"},
        {"money", "MONEY"},             {"counter", "COUNTER"},          {"date", "DATE"},
        {"heure", "TIME"},              {"date_heure", "DATETIME"},      {"bool", "BOOLEAN"},
        {"xml", "XML"},                 {"geometric", "GEOMETRY"},       {"geography", "GEOGRAPHY"},
    };
    CHECK_EQ(s.entities.at(0).attributes.size(), std::size(expected));
    for (const auto& [name, type] : expected) {
        bool found = false;
        for (const auto& a : s.entities[0].attributes) {
            if (a.name == name) {
                found = true;
                CHECK_EQ(a.type, std::string(type));
            }
        }
        CHECK(found);
    }
}

static void typeNamesRoundTrip() {
    for (const char* t : {"TEXT", "NTEXT", "BLOB", "TINYINT", "SMALLINT", "INT", "BIGINT", "FLOAT", "DOUBLE",
                          "MONEY", "COUNTER", "DATE", "TIME", "DATETIME", "BOOLEAN", "XML", "GEOGRAPHY", "GEOMETRY"}) {
        loo::Param p;
        loo::applyType(p, t);
        CHECK_EQ(loo::formatType(p), std::string(t));
    }
    // Text types carry a length (default 50), kept separately for each charset x kind.
    for (const char* t : {"VARCHAR", "NVARCHAR", "VARBINARY", "CHAR", "NCHAR", "BINARY"}) {
        loo::Param p;
        loo::applyType(p, t);
        CHECK_EQ(loo::formatType(p), std::string(t) + "(50)");
        loo::applyType(p, std::string(t) + "(255)");
        CHECK_EQ(loo::formatType(p), std::string(t) + "(255)");
    }
    // DECIMAL(digits, decimals): default 15,2; one number means no decimals.
    {
        loo::Param d;
        loo::applyType(d, "DECIMAL");
        CHECK_EQ(loo::formatType(d), std::string("DECIMAL(15,2)"));
        loo::applyType(d, "decimal(10, 4)");
        CHECK_EQ(loo::formatType(d), std::string("DECIMAL(10,4)"));
        loo::applyType(d, "DECIMAL(6)");
        CHECK_EQ(loo::formatType(d), std::string("DECIMAL(6,0)"));
        loo::applyType(d, "DECIMAL");  // keeps what was stored
        CHECK_EQ(loo::formatType(d), std::string("DECIMAL(6,0)"));
        for (const char* bad : {"DECIMAL(5,6)", "DECIMAL(0)", "DECIMAL(99,2)", "DECIMAL(1,2,3)", "INT(4)", "DATE(2)"}) {
            bool t = false;
            try { loo::applyType(d, bad); } catch (const loo::Error&) { t = true; }
            CHECK(t);
        }
    }
    loo::Param p;
    loo::applyType(p, " varchar ( 40 ) ");
    CHECK_EQ(loo::formatType(p), std::string("VARCHAR(40)"));
    bool threw = false;
    try { loo::applyType(p, "VARCHAR(0)"); } catch (const loo::Error&) { threw = true; }
    CHECK(threw);
    threw = false;
    try { loo::applyType(p, "TEXT(10)"); } catch (const loo::Error&) { threw = true; }
    CHECK(threw);
    threw = false;
    try { loo::applyType(p, "NOPE"); } catch (const loo::Error&) { threw = true; }
    CHECK(threw);
}

static void readsLengthsAndRoles() {
    // longueur.loo: lengths 11..16 typed in the dialog for the six charset x kind combinations.
    const auto s = load("longueur.loo");
    const char* expected[] = {"VARCHAR(11)", "CHAR(12)", "NVARCHAR(13)", "NCHAR(14)", "VARBINARY(15)", "BINARY(16)"};
    CHECK_EQ(s.entities.at(0).attributes.size(), std::size_t(6));
    for (std::size_t i = 0; i < 6; ++i) CHECK_EQ(s.entities[0].attributes[i].type, std::string(expected[i]));

    // link_role.loo: role "this is the role" next to the default 0,n cardinality.
    const auto r = load("link_role.loo");
    CHECK_EQ(r.links.at(0).role, std::string("this is the role"));
    CHECK_EQ(loo::toString(r.links.at(0).cardinality), std::string("0,n"));
    CHECK_EQ(load("link_base.loo").links.at(0).role, std::string(""));

    // Editing a role writes the same two-record layout Looping uses.
    auto ed = loo::Editor::open(slurp("link_base.loo"));
    ed.setLinkRole("e", "r", "this is the role");
    CHECK_EQ(ed.schema().links.at(0).role, std::string("this is the role"));
    ed.setLinkRole("e", "r", "");
    CHECK_EQ(ed.schema().links.at(0).role, std::string(""));
    CHECK(ed.save() == slurp("link_base.loo"));  // removing the role restores the original bytes

    // Lengths survive a save, and each charset x kind keeps its own length.
    auto fresh = loo::Editor::create();
    fresh.addOwner(loo::OwnerKind::Entity, "t");
    fresh.addAttribute(loo::OwnerKind::Entity, "t", {.name = "a", .type = "VARCHAR(255)"});
    fresh.addAttribute(loo::OwnerKind::Entity, "t", {.name = "b", .type = "CHAR(10)"});
    fresh.addAttribute(loo::OwnerKind::Entity, "t", {.name = "c", .type = "NVARCHAR(30)"});
    const auto back = loo::Editor::open(fresh.save()).schema().entities.at(0).attributes;
    CHECK_EQ(back.at(0).type, std::string("VARCHAR(255)"));
    CHECK_EQ(back.at(1).type, std::string("CHAR(10)"));
    CHECK_EQ(back.at(2).type, std::string("NVARCHAR(30)"));
}

static void readsAndWritesComments() {
    const auto base = load("comment_base.loo");
    CHECK_EQ(base.entities.at(0).comment, std::string(""));
    const auto onEntity = load("comment_entity_comment.loo");
    CHECK_EQ(onEntity.entities.at(0).comment, std::string("commentaire sur une entite"));
    CHECK_EQ(onEntity.entities.at(0).attributes.at(0).comment, std::string(""));
    const auto onAttribute = load("comment_attribute_comment.loo");
    CHECK_EQ(onAttribute.entities.at(0).comment, std::string(""));
    CHECK_EQ(onAttribute.entities.at(0).attributes.at(0).comment, std::string("commentaire sur un attribu"));

    // Setting the entity comment on the base file gives the same schema as the file Looping saved.
    auto ed = loo::Editor::open(slurp("comment_base.loo"));
    ed.setOwnerComment(loo::OwnerKind::Entity, "e", "commentaire sur une entite");
    CHECK_EQ(ed.schema().entities.at(0).comment, std::string("commentaire sur une entite"));
    // (Looping also reorders objects when it saves an edit, so the bytes are not compared here, only content.)
    CHECK_EQ(ed.save().size(), slurp("comment_entity_comment.loo").size());
    ed.setOwnerComment(loo::OwnerKind::Entity, "e", "");
    CHECK(ed.save() == slurp("comment_base.loo"));

    // Multi-line comments are stored as one text line each and read back joined by newlines.
    ed.setOwnerComment(loo::OwnerKind::Entity, "e", "ligne 1\r\nligne 2\nligne 3");
    CHECK_EQ(loo::Editor::open(ed.save()).schema().entities.at(0).comment, std::string("ligne 1\nligne 2\nligne 3"));

    // Associations too.
    auto fresh = loo::Editor::create();
    fresh.addOwner(loo::OwnerKind::Association, "r");
    fresh.setOwnerComment(loo::OwnerKind::Association, "r", "relation d'achat \xc3\xa9");
    CHECK_EQ(loo::Editor::open(fresh.save()).schema().associations.at(0).comment, std::string("relation d'achat \xc3\xa9"));
}

static void readsDecimalAndTimeZone() {
    const auto d = load("entity_with_1_attribute_decimal.loo");
    CHECK_EQ(d.entities.at(0).attributes.at(0).type, std::string("DECIMAL(8,3)"));
    CHECK(!d.entities.at(0).attributes.at(0).timeZone);

    const auto with = load("date_heure_with_fuseau.loo").entities.at(0).attributes.at(0);
    const auto without = load("date_heure_no_fuseau.loo").entities.at(0).attributes.at(0);
    CHECK_EQ(with.type, std::string("DATETIME"));
    CHECK_EQ(without.type, std::string("DATETIME"));
    CHECK(with.timeZone);
    CHECK(!without.timeZone);

    // Builder: set the option, keep it through a save, and refuse it on other types.
    auto ed = loo::Editor::create();
    ed.addOwner(loo::OwnerKind::Entity, "t");
    ed.addAttribute(loo::OwnerKind::Entity, "t", {.name = "d", .type = "DATETIME", .timeZone = true});
    ed.addAttribute(loo::OwnerKind::Entity, "t", {.name = "p", .type = "DECIMAL(10,2)"});
    auto attrs = loo::Editor::open(ed.save()).schema().entities.at(0).attributes;
    CHECK(attrs.at(0).timeZone);
    CHECK_EQ(attrs.at(1).type, std::string("DECIMAL(10,2)"));
    bool threw = false;
    try { ed.addAttribute(loo::OwnerKind::Entity, "t", {.name = "x", .type = "INT", .timeZone = true}); }
    catch (const loo::Error&) { threw = true; }
    CHECK(threw);
    // Changing the type away from DATETIME clears the flag.
    ed.updateAttribute(loo::OwnerKind::Entity, "t", "d", {.type = std::string("DATE")});
    CHECK(!ed.schema().entities.at(0).attributes.at(0).timeZone);
    ed.updateAttribute(loo::OwnerKind::Entity, "t", "d", {.type = std::string("DATETIME"), .timeZone = true});
    CHECK(ed.schema().entities.at(0).attributes.at(0).timeZone);
    ed.updateAttribute(loo::OwnerKind::Entity, "t", "d", {.timeZone = false});
    CHECK(!ed.schema().entities.at(0).attributes.at(0).timeZone);
}

static void buildsFromScratch() {
    using loo::OwnerKind;
    auto ed = loo::Editor::create();
    ed.addOwner(OwnerKind::Entity, "client");
    ed.addOwner(OwnerKind::Entity, "commande", loo::Point{500, 100});
    ed.addOwner(OwnerKind::Association, "passer");
    ed.addAttribute(OwnerKind::Entity, "client", {.name = "id_client", .type = "INT", .identifier = true});
    ed.addAttribute(OwnerKind::Entity, "client", {.name = "nom", .type = "VARCHAR(50)", .notNull = true});
    ed.addAttribute(OwnerKind::Entity, "commande", {.name = "id_commande", .type = "BIGINT", .identifier = true});
    ed.addAttribute(OwnerKind::Association, "passer", {.name = "date_commande", .type = "DATE"});
    ed.addLink("client", "passer", loo::Cardinality::ZeroN);
    ed.addLink("commande", "passer", loo::Cardinality::OneOne, "livraison");

    // Save, reopen, and check the schema survived.
    auto bytes = ed.save();
    auto again = loo::Editor::open(bytes);
    auto s = again.schema();
    CHECK_EQ(s.entities.size(), std::size_t(2));
    CHECK_EQ(s.associations.size(), std::size_t(1));
    CHECK_EQ(s.links.size(), std::size_t(2));
    CHECK_EQ(s.entities[0].name, std::string("client"));
    CHECK_EQ(s.entities[0].attributes.size(), std::size_t(2));
    CHECK(s.entities[0].attributes[0].identifier);
    CHECK(s.entities[0].attributes[1].notNull);
    CHECK_EQ(s.entities[1].position.x, 500);
    CHECK_EQ(s.entities[1].attributes[0].type, std::string("BIGINT"));
    CHECK_EQ(s.associations[0].attributes[0].type, std::string("DATE"));
    CHECK_EQ(s.links[0].name, std::string("Lien 1"));
    CHECK_EQ(s.links[1].name, std::string("Lien 2"));
    CHECK_EQ(loo::toString(s.links[1].cardinality), std::string("1,1"));
    CHECK_EQ(s.links[1].role, std::string("livraison"));
    CHECK_EQ(s.links[0].role, std::string(""));
    CHECK(again.save() == bytes);  // what we write reads back and writes identically

    // Edits.
    again.setCardinality("client", "passer", loo::Cardinality::OneN);
    again.renameOwner(OwnerKind::Entity, "client", "customer");
    again.updateAttribute(OwnerKind::Entity, "customer", "nom", {.type = std::string("NVARCHAR"), .unique = true});
    again.removeAttribute(OwnerKind::Entity, "customer", "id_client");
    s = again.schema();
    CHECK_EQ(s.links[0].entity, std::string("customer"));
    CHECK_EQ(loo::toString(s.links[0].cardinality), std::string("1,n"));
    CHECK_EQ(s.entities[0].attributes.size(), std::size_t(1));
    CHECK_EQ(s.entities[0].attributes[0].type, std::string("NVARCHAR(50)"));
    CHECK(s.entities[0].attributes[0].unique);

    again.removeLink("commande", "passer");
    CHECK_EQ(again.schema().links.size(), std::size_t(1));
    again.removeOwner(OwnerKind::Entity, "customer");  // takes its link with it
    s = again.schema();
    CHECK_EQ(s.entities.size(), std::size_t(1));
    CHECK_EQ(s.links.size(), std::size_t(0));
    CHECK(loo::Editor::open(again.save()).schema().entities.size() == 1);

    // Errors.
    bool threw = false;
    try { again.addOwner(OwnerKind::Entity, "commande"); } catch (const loo::Error&) { threw = true; }
    CHECK(threw);
    threw = false;
    try { again.addLink("commande", "missing", loo::Cardinality::ZeroN); } catch (const loo::Error&) { threw = true; }
    CHECK(threw);
}

static void editingKeepsExistingFile() {
    // Opening a real file and saving it unchanged must not alter a single byte.
    for (const char* f : {"looping_file_example.loo", "cardinalities 0,1, 1,1 and 1,n.loo", "all_sql_types.loo"}) {
        const auto bytes = slurp(f);
        CHECK(loo::Editor::open(bytes).save() == bytes);
    }
    // Adding to the big real file keeps what was there.
    auto ed = loo::Editor::open(slurp("looping_file_example.loo"));
    const auto before = ed.schema();
    ed.addOwner(loo::OwnerKind::Entity, "nouvelle_entite");
    const auto after = ed.schema();
    CHECK_EQ(after.entities.size(), before.entities.size() + 1);
    CHECK_EQ(after.links.size(), before.links.size());
    CHECK_EQ(after.associations.size(), before.associations.size());
}

int main(int argc, char** argv) {
    corpus = argc > 1 ? argv[1] : "looping_file_examples";
    try {
        readsCardinalities();
        readsAttributes();
        readsAllTypes();
        typeNamesRoundTrip();
        readsLengthsAndRoles();
        readsDecimalAndTimeZone();
        readsAndWritesComments();
        buildsFromScratch();
        editingKeepsExistingFile();
    } catch (const std::exception& e) {
        std::cout << "EXCEPTION: " << e.what() << "\n";
        return 1;
    }
    std::cout << (checks - failures) << "/" << checks << " checks passed\n";
    return failures == 0 ? 0 : 1;
}
