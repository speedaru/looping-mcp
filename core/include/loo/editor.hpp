// High-level editing of a Looping document: entities, associations, attributes and links.
//
// New objects are cloned from real template objects saved by Looping, so every field we do not
// understand keeps Looping's own default. Editing an existing file touches only the objects involved.
#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "loo/document.hpp"

namespace loo {

enum class Cardinality { ZeroOne, OneOne, ZeroN, OneN };
std::string toString(Cardinality c);                    // "0,1" "1,1" "0,n" "1,n"
Cardinality parseCardinality(const std::string& text);  // accepts "0,1", "0,n", "0n", "1,N", ...

enum class OwnerKind { Entity, Association };

struct Point {
    int x = 0;
    int y = 0;
};

struct AttributeSpec {
    std::string name;
    std::string type = "INT";  // see loo/types.hpp
    bool identifier = false;
    bool notNull = false;
    bool unique = false;
    bool timeZone = false;     // DATETIME only: "avec fuseau horaire"
    std::string logicalName;   // empty: same as name
    std::string comment;
};

struct AttributeUpdate {
    std::optional<std::string> name, type, logicalName, comment;
    std::optional<bool> identifier, notNull, unique, timeZone;
};

struct AttributeInfo {
    std::string name, logicalName, type, comment;
    bool identifier = false, notNull = false, unique = false, timeZone = false;
};

struct OwnerInfo {
    std::string name;
    std::string comment;  // the "Commentaire" box of the entity / association dialog; lines joined with \n
    Point position;
    std::vector<AttributeInfo> attributes;
};

struct LinkInfo {
    std::string name, entity, association;
    Cardinality cardinality = Cardinality::ZeroN;
    std::string role;  // "Libelle" typed in the link dialog; empty if none
};

struct Schema {
    std::vector<OwnerInfo> entities;
    std::vector<OwnerInfo> associations;
    std::vector<LinkInfo> links;
};

class Editor {
public:
    static Editor create();                              // empty document
    static Editor open(std::span<const std::uint8_t> data);
    std::vector<std::uint8_t> save() const;

    Schema schema() const;

    // Entities and associations ("owners"). Names are unique across both kinds.
    void addOwner(OwnerKind kind, const std::string& name, std::optional<Point> position = std::nullopt);
    void renameOwner(OwnerKind kind, const std::string& name, const std::string& newName);
    void moveOwner(OwnerKind kind, const std::string& name, Point position);
    void removeOwner(OwnerKind kind, const std::string& name);  // also removes its links
    void setOwnerComment(OwnerKind kind, const std::string& name, const std::string& comment);  // "" clears

    void addAttribute(OwnerKind kind, const std::string& owner, const AttributeSpec& spec);
    void updateAttribute(OwnerKind kind, const std::string& owner, const std::string& attribute,
                         const AttributeUpdate& update);
    void removeAttribute(OwnerKind kind, const std::string& owner, const std::string& attribute);

    // A link joins one entity and one association.
    void addLink(const std::string& entity, const std::string& association, Cardinality cardinality,
                 const std::string& role = {});
    // When an entity is linked to the same association more than once (a reflexive association), pass the
    // link's name ("Lien 2", unique within the association) in `link`.
    // An association can have only one 1,1 link (as in Looping); addLink / setCardinality enforce it.
    void setLinkRole(const std::string& entity, const std::string& association, const std::string& role,
                     const std::string& link = {});  // "" removes the role
    void setCardinality(const std::string& entity, const std::string& association, Cardinality cardinality,
                        const std::string& link = {});
    void removeLink(const std::string& entity, const std::string& association, const std::string& link = {});

private:
    Editor() = default;
    Document doc_;
    // implementation helpers live in editor.cpp
};

}  // namespace loo
