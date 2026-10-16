#include "loo/mld.hpp"

#include <algorithm>
#include <cctype>
#include <map>
#include <sstream>

namespace loo {

namespace {

std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

bool hasColumn(const MldTable& t, const std::string& name) {
    return std::any_of(t.columns.begin(), t.columns.end(),
                       [&](const MldColumn& c) { return lower(c.name) == lower(name); });
}

// Looping suffixes a clashing column with _1, _2, ... (verified with different_associations.loo).
std::string freeName(const MldTable& t, const std::string& wanted) {
    if (!hasColumn(t, wanted)) return wanted;
    for (int i = 1;; ++i) {
        const std::string candidate = wanted + "_" + std::to_string(i);
        if (!hasColumn(t, candidate)) return candidate;
    }
}

// A counter becomes a plain integer when it is copied into a foreign key.
std::string foreignKeyType(const std::string& type) { return type == "COUNTER" ? "INT" : type; }

bool maxIsOne(Cardinality c) { return c == Cardinality::ZeroOne || c == Cardinality::OneOne; }

MldColumn fromAttribute(const AttributeInfo& a) {
    MldColumn c;
    c.name = a.logicalName.empty() ? a.name : a.logicalName;
    c.type = a.type;
    c.primaryKey = a.identifier;
    c.notNull = a.notNull || a.identifier;
    c.unique = a.unique;
    return c;
}

}  // namespace

Mld deriveMld(const Schema& schema) {
    Mld mld;
    std::map<std::string, std::size_t> tableIndex;

    auto addTable = [&](MldTable t) -> MldTable& {
        tableIndex[t.name] = mld.tables.size();
        mld.tables.push_back(std::move(t));
        return mld.tables.back();
    };

    // Tables for entities first (their primary keys are needed by every foreign key).
    for (const auto& e : schema.entities) {
        MldTable t;
        t.name = e.name;
        for (const auto& a : e.attributes) t.columns.push_back(fromAttribute(a));
        if (std::none_of(t.columns.begin(), t.columns.end(), [](const MldColumn& c) { return c.primaryKey; })) {
            mld.warnings.push_back("entity '" + e.name + "' has no identifier; no foreign key can reference it");
        }
        addTable(std::move(t));
    }

    auto primaryKeyOf = [&](const std::string& entity) {
        std::vector<MldColumn> pk;
        if (auto it = tableIndex.find(entity); it != tableIndex.end()) {
            for (const auto& c : mld.tables[it->second].columns) {
                if (c.primaryKey && c.refTable.empty()) pk.push_back(c);
            }
        }
        return pk;
    };

    // Adds to `table` one foreign key (all columns of the entity's identifier) towards `entity`.
    auto addForeignKey = [&](MldTable& table, const std::string& entity, bool primaryKey, bool notNull) {
        for (const auto& source : primaryKeyOf(entity)) {
            MldColumn c;
            c.name = freeName(table, source.name);
            c.type = foreignKeyType(source.type);
            c.primaryKey = primaryKey;
            c.notNull = notNull || primaryKey;
            c.refTable = entity;
            c.refColumn = source.name;
            table.columns.push_back(std::move(c));
        }
    };

    auto addAttributes = [&](MldTable& table, const OwnerInfo& association) {
        for (const auto& a : association.attributes) {
            MldColumn c = fromAttribute(a);
            c.primaryKey = false;
            c.name = freeName(table, c.name);
            table.columns.push_back(std::move(c));
        }
    };

    for (const auto& assoc : schema.associations) {
        std::vector<const LinkInfo*> links;
        for (const auto& l : schema.links) {
            if (l.association == assoc.name) links.push_back(&l);
        }
        if (links.size() < 2) {
            mld.warnings.push_back("association '" + assoc.name + "' has " + std::to_string(links.size()) +
                                   " link(s); it needs at least two, so it produces no table");
            continue;
        }
        for (const auto* l : links) {
            if (primaryKeyOf(l->entity).empty()) {
                mld.warnings.push_back("association '" + assoc.name + "': entity '" + l->entity +
                                       "' has no identifier, so its foreign key is missing");
            }
        }

        const bool binary = links.size() == 2;
        const bool anyMaxOne = maxIsOne(links[0]->cardinality) || maxIsOne(links[1]->cardinality);
        if (binary && anyMaxOne) {
            // The foreign key goes into the entity of a max-1 side, pointing at the other entity.
            std::size_t holder = 0;
            if (maxIsOne(links[0]->cardinality) && maxIsOne(links[1]->cardinality)) {
                holder = links[1]->cardinality == Cardinality::OneOne && links[0]->cardinality != Cardinality::OneOne ? 1 : 0;
            } else {
                holder = maxIsOne(links[0]->cardinality) ? 0 : 1;
            }
            const LinkInfo& h = *links[holder];
            const LinkInfo& other = *links[1 - holder];
            MldTable& table = mld.tables[tableIndex.at(h.entity)];
            addForeignKey(table, other.entity, false, h.cardinality == Cardinality::OneOne);
            if (!assoc.attributes.empty()) {
                // Looping does not allow this (an association needs two n links to hold attributes).
                mld.warnings.push_back("association '" + assoc.name +
                                       "' has attributes but does not become a table; they are ignored here");
            }
        } else {
            MldTable t;
            t.name = assoc.name;
            t.fromAssociation = true;
            MldTable& table = addTable(std::move(t));
            for (const auto* l : links) addForeignKey(table, l->entity, true, true);
            addAttributes(table, assoc);
            if (binary == false && std::any_of(links.begin(), links.end(), [](const LinkInfo* l) { return l->cardinality == Cardinality::OneOne; })) {
                mld.warnings.push_back("association '" + assoc.name +
                                       "' links three or more entities and has a 1,1 link; how Looping places the "
                                       "foreign keys in that case is not verified");
            }
        }
    }

    for (const auto& l : schema.links) {
        if (!l.role.empty()) {
            mld.warnings.push_back("link '" + l.name + "' (" + l.entity + " - " + l.association + ") has the role '" + l.role +
                                   "'; Looping may use it to name the foreign key, which is not reproduced here");
            break;
        }
    }
    return mld;
}

std::string formatTable(const MldTable& t) {
    std::string s = t.name + " (";
    for (std::size_t i = 0; i < t.columns.size(); ++i) {
        const auto& c = t.columns[i];
        if (i) s += ", ";
        s += c.name + " " + c.type;
        if (c.primaryKey) s += " PK";
        if (!c.refTable.empty()) s += " FK->" + c.refTable + "." + c.refColumn;
        if (c.notNull && !c.primaryKey) s += " NOT NULL";
        if (c.unique) s += " UNIQUE";
    }
    return s + ")";
}

std::string describe(const Schema& schema, const Mld& mld) {
    std::ostringstream o;
    o << "MCD (conceptual model)\n";
    o << "----------------------\n";
    o << "Entities:\n";
    if (schema.entities.empty()) o << "  (none)\n";
    for (const auto& e : schema.entities) {
        o << "  " << e.name;
        if (!e.comment.empty()) o << "  -- " << e.comment;
        o << "\n";
        for (const auto& a : e.attributes) {
            o << "    " << (a.identifier ? "* " : "  ") << a.name << " " << a.type;
            if (a.notNull && !a.identifier) o << " NOT NULL";
            if (a.unique) o << " UNIQUE";
            if (a.timeZone) o << " WITH TIME ZONE";
            if (!a.logicalName.empty() && a.logicalName != a.name) o << " (logical name: " << a.logicalName << ")";
            if (!a.comment.empty()) o << "  -- " << a.comment;
            o << "\n";
        }
    }
    o << "Associations:\n";
    if (schema.associations.empty()) o << "  (none)\n";
    for (const auto& a : schema.associations) {
        o << "  " << a.name;
        if (!a.comment.empty()) o << "  -- " << a.comment;
        o << "\n";
        for (const auto& at : a.attributes) {
            o << "      " << at.name << " " << at.type;
            if (at.notNull) o << " NOT NULL";
            if (!at.comment.empty()) o << "  -- " << at.comment;
            o << "\n";
        }
    }
    o << "Links (cardinality is read on the entity side):\n";
    if (schema.links.empty()) o << "  (none)\n";
    for (const auto& l : schema.links) {
        o << "  " << l.entity << " --" << toString(l.cardinality) << "-- " << l.association << "   [" << l.name;
        if (!l.role.empty()) o << ", role: " << l.role;
        o << "]\n";
    }
    o << "\nMLD (logical model, derived by looping-mcp; * identifier columns are the primary key)\n";
    o << "-----------------------------------------------------------------------------------\n";
    if (mld.tables.empty()) o << "  (no tables)\n";
    for (const auto& t : mld.tables) o << "  " << formatTable(t) << (t.fromAssociation ? "   [from association]" : "") << "\n";
    if (!mld.warnings.empty()) {
        o << "\nWarnings:\n";
        for (const auto& w : mld.warnings) o << "  - " << w << "\n";
    }
    return o.str();
}

}  // namespace loo
