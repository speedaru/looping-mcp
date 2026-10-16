// Derives the MLD (logical relational model) from an MCD schema and renders both as text for AI agents.
//
// Looping derives the MLD itself; this is a re-implementation of its rules, checked against Looping's own
// output for the cases in looping_file_examples (tests/mld_test.cpp). Rules implemented:
//   * entity            -> table (attributes as columns, identifiers as primary key)
//   * association with exactly 2 links where at least one side has maximum cardinality 1
//                       -> no table; the foreign key goes into the entity of the max-1 side
//                          (if both sides are max-1, the 1,1 side; if neither is 1,1, the first link)
//                          and the association's attributes move into that entity's table
//   * any other association (n,n or three or more links)
//                       -> a table whose primary key is the foreign keys of all its links
//   * a foreign key repeats the referenced identifier's column names, suffixed _1, _2... on a name clash
#pragma once

#include <string>
#include <vector>

#include "loo/editor.hpp"

namespace loo {

struct MldColumn {
    std::string name;
    std::string type;
    bool primaryKey = false;
    bool notNull = false;
    bool unique = false;
    std::string refTable;   // foreign key target, empty if not a foreign key
    std::string refColumn;
};

struct MldTable {
    std::string name;
    bool fromAssociation = false;
    std::vector<MldColumn> columns;
};

struct Mld {
    std::vector<MldTable> tables;
    std::vector<std::string> warnings;
};

Mld deriveMld(const Schema& schema);

// One-line form of a table, e.g. "commande (id_commande BIGINT PK, id_client INT FK->client.id_client NOT NULL)".
std::string formatTable(const MldTable& table);

// Text for an AI agent: the MCD (entities, associations, links), the derived MLD, and warnings.
std::string describe(const Schema& schema, const Mld& mld);

}  // namespace loo
