#include "loo/editor.hpp"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <functional>
#include <memory>

#include "loo/types.hpp"
#include "templates.hpp"

namespace loo {

namespace {

// ---- template access ------------------------------------------------------------------------

const Document& linkTemplate() {
    static const Document doc = readDocument(linkTemplateBytes());
    return doc;
}

const Document& attrTemplate() {
    static const Document doc = readDocument(attrTemplateBytes());
    return doc;
}

const Object& templateObject(const Document& d, std::uint32_t id, const char* cls) {
    for (const auto& o : d.objects) {
        if (o->base.id == id && o->cls == cls) return *o;
    }
    throw Error("internal error: template object " + std::to_string(id) + " (" + cls + ") missing");
}

// ---- small helpers --------------------------------------------------------------------------

constexpr int kMinEntityWidth = 82;
constexpr int kMinEntityHeight = 60;

std::string kindName(OwnerKind k) { return k == OwnerKind::Entity ? "entity" : "association"; }

double oleNow() {
    using namespace std::chrono;
    const double secs = duration<double>(system_clock::now().time_since_epoch()).count();
    return secs / 86400.0 + 25569.0;  // days since 1899-12-30
}

OleDate makeOleNow() {
    OleDate d;
    const double v = oleNow();
    std::memcpy(&d.bits, &v, sizeof(v));
    return d;
}

// Entity box size, fitted to the data Looping stores for entities it laid out itself:
// 82x60 minimum, +17 px per attribute beyond the first, width follows the longest text.
void fitEntity(Entite& e, const std::string& name, const std::vector<std::string>& attrNames) {
    int w = kMinEntityWidth;
    w = std::max(w, 11 + 10 * static_cast<int>(name.size()));
    for (const auto& a : attrNames) w = std::max(w, 10 + static_cast<int>(7.5 * static_cast<double>(a.size())));
    const int h = std::max(kMinEntityHeight, 43 + 17 * static_cast<int>(attrNames.size()));
    e.rect[2] = e.rect[0] + w;
    e.rect[3] = e.rect[1] + h;
}

}  // namespace

// ---- cardinality ----------------------------------------------------------------------------

std::string toString(Cardinality c) {
    switch (c) {
        case Cardinality::ZeroOne: return "0,1";
        case Cardinality::OneOne: return "1,1";
        case Cardinality::ZeroN: return "0,n";
        case Cardinality::OneN: return "1,n";
    }
    return "?";
}

Cardinality parseCardinality(const std::string& text) {
    std::string t;
    for (char c : text) {
        if (c == ' ') continue;
        t += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    if (t == "0,1" || t == "01") return Cardinality::ZeroOne;
    if (t == "1,1" || t == "11") return Cardinality::OneOne;
    if (t == "0,n" || t == "0n" || t == "0,*" || t == "0*") return Cardinality::ZeroN;
    if (t == "1,n" || t == "1n" || t == "1,*" || t == "1*") return Cardinality::OneN;
    throw Error("invalid cardinality '" + text + "' (expected 0,1  1,1  0,n  or 1,n)");
}

namespace {

// Link cardinality record, verified with link_base/01/11/1n.loo.
void writeCardinality(Param& p, Cardinality c) {
    switch (c) {
        case Cardinality::ZeroOne: p.cardinality = 0; p.d328 = 1; break;
        case Cardinality::OneOne: p.cardinality = 1; p.d328 = 0; break;
        case Cardinality::ZeroN: p.cardinality = 2; p.d328 = 0; break;
        case Cardinality::OneN: p.cardinality = 3; p.d328 = 0; break;
    }
}

Cardinality readCardinality(const Param& p) {
    switch (p.cardinality) {
        case 0: return Cardinality::ZeroOne;
        case 1: return Cardinality::OneOne;
        case 3: return Cardinality::OneN;
        default: return Cardinality::ZeroN;
    }
}

// A link's text holds record 0 = cardinality (d212 = 1) and, when the link has a role, a second record
// (d212 = 2) whose s240 is the role text (verified with link_base.loo / link_role.loo).
constexpr std::uint32_t kRoleRecord = 2;

Param* roleRecord(Texte& t) {
    for (auto& p : t.params) {
        if (p.d212 == kRoleRecord) return &p;
    }
    return nullptr;
}

void setRole(Texte& t, const std::string& role) {
    Param* existing = roleRecord(t);
    if (role.empty()) {
        if (existing) t.params.erase(t.params.begin() + (existing - t.params.data()));
        return;
    }
    if (existing) {
        existing->s240 = fromUtf8(role);
        return;
    }
    if (t.params.empty()) throw Error("the link has no cardinality record");
    Param p = t.params[0];
    writeCardinality(p, Cardinality::ZeroN);  // role records carry the defaults
    p.d212 = kRoleRecord;
    p.s240 = fromUtf8(role);
    t.params.push_back(std::move(p));
}

// "avec fuseau horaire" is only offered for date-time attributes.
void setTimeZone(Param& p, bool on) {
    if (on && formatType(p) != "DATETIME") throw Error("a time zone can only be set on a DATETIME attribute");
    p.withTimeZone = on ? 1 : 0;
}

// ---- document navigation --------------------------------------------------------------------

struct Doc {
    Document& d;

    Object* byId(std::uint32_t id) const {
        if (id == 0) return nullptr;
        for (auto& o : d.objects) {
            if (o->base.id == id) return o.get();
        }
        return nullptr;
    }

    Texte* text(std::uint32_t id) const { return dynamic_cast<Texte*>(byId(id)); }

    std::uint32_t nextId() const {
        std::uint32_t m = 0;
        for (auto& o : d.objects) m = std::max(m, o->base.id);
        return m + 1;
    }

    static bool isOwner(const Object& o) { return o.cls == "CDrawEntite" || o.cls == "CDrawRelation"; }

    // The owner's name is the first line of its first text.
    std::string nameOf(const Object& owner) const {
        if (auto* t = text(owner.refs[0]); t && !t->lines1128.empty()) return toUtf8(t->lines1128[0]);
        return {};
    }

    Object* findOwner(OwnerKind kind, const std::string& name) const {
        const char* cls = kind == OwnerKind::Entity ? "CDrawEntite" : "CDrawRelation";
        for (auto& o : d.objects) {
            if (o->cls == cls && nameOf(*o) == name) return o.get();
        }
        return nullptr;
    }

    Object& requireOwner(OwnerKind kind, const std::string& name) const {
        if (auto* o = findOwner(kind, name)) return *o;
        throw Error(kindName(kind) + " '" + name + "' not found");
    }

    bool nameTaken(const std::string& name) const {
        for (auto& o : d.objects) {
            if (isOwner(*o) && nameOf(*o) == name) return true;
        }
        return false;
    }

    Texte& attributeText(const Object& owner) const {
        if (auto* t = text(owner.refs[1])) return *t;
        throw Error("object " + std::to_string(owner.base.id) + " has no attribute text");
    }

    void eraseIds(const std::vector<std::uint32_t>& ids) {
        std::erase_if(d.objects, [&](const std::unique_ptr<Object>& o) {
            return std::find(ids.begin(), ids.end(), o->base.id) != ids.end();
        });
    }

    // A link is a chain of arcs (refs[3] = previous, refs[4] = next) between an entity and an association.
    // Looping draws a bent link with extra arcs that have no owner, and a link can start at either end
    // (verified with different_associations.loo). The first arc of the chain carries the link's texts
    // (name, cardinality record); the endpoints are the owners of the first and the last arc.
    struct LinkArcs {
        std::vector<Arc*> chain;
        Object* entity = nullptr;
        Object* assoc = nullptr;
        Arc* start() const { return chain.front(); }
    };

    std::vector<LinkArcs> links() const {
        std::vector<LinkArcs> out;
        for (auto& o : d.objects) {
            auto* a = dynamic_cast<Arc*>(o.get());
            if (!a || a->refs[3] != 0) continue;  // chains start at an arc with no predecessor
            LinkArcs l;
            std::vector<std::uint32_t> seen;
            for (Arc* cur = a; cur != nullptr; cur = dynamic_cast<Arc*>(byId(cur->refs[4]))) {
                if (std::find(seen.begin(), seen.end(), cur->base.id) != seen.end()) break;  // corrupt loop
                seen.push_back(cur->base.id);
                l.chain.push_back(cur);
            }
            Object* first = byId(l.chain.front()->refs[5]);
            Object* last = byId(l.chain.back()->refs[5]);
            if (!first || !last) continue;
            if (first->cls == "CDrawEntite" && last->cls == "CDrawRelation") {
                l.entity = first;
                l.assoc = last;
            } else if (first->cls == "CDrawRelation" && last->cls == "CDrawEntite") {
                l.entity = last;
                l.assoc = first;
            } else {
                continue;
            }
            out.push_back(std::move(l));
        }
        std::sort(out.begin(), out.end(), [](const LinkArcs& x, const LinkArcs& y) { return x.start()->base.id < y.start()->base.id; });
        return out;
    }

    // Looping lets an association carry attributes only if it becomes a table, i.e. at least two of its links
    // have maximum cardinality n (otherwise the foreign key goes into an entity and the association disappears).
    // Counts the links of `assoc` with cardinality 0,n or 1,n, ignoring those for which `skip` returns true.
    int manyLinks(const Object& assoc, const std::function<bool(const LinkArcs&)>& skip = nullptr) const {
        int n = 0;
        for (auto& l : links()) {
            if (l.assoc != &assoc || (skip && skip(l))) continue;
            auto* t = text(l.start()->refs[1]);
            if (!t || t->params.empty()) continue;
            const Cardinality c = readCardinality(t->params[0]);
            if (c == Cardinality::ZeroN || c == Cardinality::OneN) ++n;
        }
        return n;
    }

    bool hasAttributes(const Object& assoc) const {
        auto* t = text(assoc.refs[1]);
        return t && !t->params.empty();
    }

    std::string linkName(const LinkArcs& l) const {
        if (auto* t = text(l.start()->refs[0]); t && !t->lines1128.empty()) return toUtf8(t->lines1128[0]);
        return {};
    }

    // ids of every object forming a link (all its arcs and their texts).
    std::vector<std::uint32_t> linkObjectIds(const LinkArcs& l) const {
        std::vector<std::uint32_t> ids;
        for (auto* arc : l.chain) {
            ids.push_back(arc->base.id);
            for (int i = 0; i < 3; ++i) {
                if (arc->refs[i]) ids.push_back(arc->refs[i]);
            }
        }
        return ids;
    }

    void setOwnerLayout(Object& owner) const {
        if (auto* e = dynamic_cast<Entite*>(&owner)) {
            std::vector<std::string> names;
            for (auto& p : attributeText(owner).params) names.push_back(toUtf8(p.name));
            fitEntity(*e, nameOf(owner), names);
        }
    }
};

}  // namespace

// ---- Editor ---------------------------------------------------------------------------------

Editor Editor::create() {
    Editor e;
    e.doc_.header = linkTemplate().header;
    return e;
}

Editor Editor::open(std::span<const std::uint8_t> data) {
    Editor e;
    e.doc_ = readDocument(data);
    return e;
}

std::vector<std::uint8_t> Editor::save() const { return writeDocument(doc_); }

Schema Editor::schema() const {
    Doc doc{const_cast<Document&>(doc_)};
    Schema s;

    auto info = [&](Object& o) {
        OwnerInfo oi;
        oi.name = doc.nameOf(o);
        if (auto* c = doc.text(o.refs[2])) {
            for (std::size_t i = 0; i < c->lines1128.size(); ++i) oi.comment += (i ? "\n" : "") + toUtf8(c->lines1128[i]);
        }
        if (auto* e = dynamic_cast<Entite*>(&o)) oi.position = {e->rect[0], e->rect[1]};
        if (auto* r = dynamic_cast<Relation*>(&o)) oi.position = {r->x, r->y};
        if (auto* t = doc.text(o.refs[1])) {
            for (const auto& p : t->params) {
                AttributeInfo a;
                a.name = toUtf8(p.name);
                a.logicalName = p.logicalName.empty() ? a.name : toUtf8(p.logicalName);
                a.type = formatType(p);
                a.comment = toUtf8(p.s248);
                a.identifier = p.isIdentifier != 0;
                a.notNull = p.notNull != 0;
                a.unique = p.unique != 0;
                a.timeZone = p.withTimeZone != 0;
                oi.attributes.push_back(std::move(a));
            }
        }
        return oi;
    };

    for (auto& o : doc_.objects) {
        if (o->cls == "CDrawEntite") s.entities.push_back(info(*o));
        if (o->cls == "CDrawRelation") s.associations.push_back(info(*o));
    }

    for (auto& l : doc.links()) {
        LinkInfo li;
        li.name = doc.linkName(l);
        li.entity = doc.nameOf(*l.entity);
        li.association = doc.nameOf(*l.assoc);
        if (auto* t = doc.text(l.start()->refs[1]); t && !t->params.empty()) {
            li.cardinality = readCardinality(t->params[0]);
            if (const Param* r = roleRecord(*t)) li.role = toUtf8(r->s240);
        }
        s.links.push_back(std::move(li));
    }
    return s;
}

void Editor::addOwner(OwnerKind kind, const std::string& name, std::optional<Point> position) {
    Doc doc{doc_};
    if (name.empty()) throw Error("a name is required");
    if (doc.nameTaken(name)) throw Error("the name '" + name + "' is already used");

    const bool entity = kind == OwnerKind::Entity;
    const auto& tpl = linkTemplate();
    const std::uint32_t firstTemplateId = entity ? 1 : 5;
    const char* cls = entity ? "CDrawEntite" : "CDrawRelation";

    int index = 0;
    for (auto& o : doc_.objects) index += o->cls == cls ? 1 : 0;
    // Default grid: entities (x, y = top-left) in a row, associations (x, y = center) in a row below them.
    Point pos = position.value_or(entity ? Point{100 + 260 * (index % 4), 80 + 400 * (index / 4)}
                                         : Point{160 + 260 * (index % 4), 280 + 400 * (index / 4)});

    const std::uint32_t base = doc.nextId();
    auto owner = templateObject(tpl, firstTemplateId, cls).clone();
    owner->base.id = base;
    owner->refs = {base + 1, base + 2, base + 3, 0, 0, 0};
    if (auto* e = dynamic_cast<Entite*>(owner.get())) {
        const int w = e->rect[2] - e->rect[0], h = e->rect[3] - e->rect[1];
        e->rect = {pos.x, pos.y, pos.x + w, pos.y + h};
    } else if (auto* r = dynamic_cast<Relation*>(owner.get())) {
        r->x = pos.x;
        r->y = pos.y;
    }
    doc_.objects.push_back(std::move(owner));

    for (std::uint32_t i = 0; i < 3; ++i) {
        auto t = templateObject(tpl, firstTemplateId + 1 + i, "CDrawTexte").clone();
        t->base.id = base + 1 + i;
        t->refs = {0, 0, 0, 0, 0, base};
        if (i == 0) static_cast<Texte&>(*t).lines1128 = {fromUtf8(name)};
        doc_.objects.push_back(std::move(t));
    }
    doc.setOwnerLayout(*doc.byId(base));
}

void Editor::renameOwner(OwnerKind kind, const std::string& name, const std::string& newName) {
    Doc doc{doc_};
    if (newName.empty()) throw Error("a name is required");
    Object& owner = doc.requireOwner(kind, name);
    if (newName != name && doc.nameTaken(newName)) throw Error("the name '" + newName + "' is already used");
    doc.text(owner.refs[0])->lines1128 = {fromUtf8(newName)};
    doc.setOwnerLayout(owner);
}

void Editor::moveOwner(OwnerKind kind, const std::string& name, Point position) {
    Doc doc{doc_};
    Object& owner = doc.requireOwner(kind, name);
    if (auto* e = dynamic_cast<Entite*>(&owner)) {
        const int w = e->rect[2] - e->rect[0], h = e->rect[3] - e->rect[1];
        e->rect = {position.x, position.y, position.x + w, position.y + h};
    } else if (auto* r = dynamic_cast<Relation*>(&owner)) {
        r->x = position.x;
        r->y = position.y;
    }
}

void Editor::removeOwner(OwnerKind kind, const std::string& name) {
    Doc doc{doc_};
    Object& owner = doc.requireOwner(kind, name);
    const std::uint32_t id = owner.base.id;

    if (kind == OwnerKind::Entity) {
        // Removing the entity removes its links; refuse if that would leave an association with attributes
        // without two n links.
        for (auto& a : doc_.objects) {
            if (a->cls != "CDrawRelation" || !doc.hasAttributes(*a)) continue;
            bool touched = false;
            for (auto& l : doc.links()) touched = touched || (l.assoc == a.get() && l.entity->base.id == id);
            const int left = doc.manyLinks(*a, [&](const Doc::LinkArcs& x) { return x.entity->base.id == id; });
            if (touched && left < 2) {
                throw Error("removing '" + name + "' would leave association '" + doc.nameOf(*a) +
                            "' with attributes but fewer than two links with maximum cardinality n; remove those "
                            "attributes first");
            }
        }
    }

    std::vector<std::uint32_t> ids{id, owner.refs[0], owner.refs[1], owner.refs[2]};
    for (auto& l : doc.links()) {
        const bool attached = l.entity->base.id == id || l.assoc->base.id == id;
        if (!attached) continue;
        for (auto lid : doc.linkObjectIds(l)) ids.push_back(lid);
    }
    doc.eraseIds(ids);
}

// The owner's comment is stored as the text lines of its third text (verified for entities with
// comment_base.loo / comment_entity_comment.loo; associations have the same three-text structure).
void Editor::setOwnerComment(OwnerKind kind, const std::string& name, const std::string& comment) {
    Doc doc{doc_};
    Object& owner = doc.requireOwner(kind, name);
    Texte* t = doc.text(owner.refs[2]);
    if (!t) throw Error("object '" + name + "' has no comment text");
    t->lines1128.clear();
    if (comment.empty()) return;
    std::string line;
    auto flush = [&] {
        t->lines1128.push_back(fromUtf8(line));
        line.clear();
    };
    for (char c : comment) {
        if (c == '\n') {
            flush();
        } else if (c != '\r') {
            line += c;
        }
    }
    flush();
}

void Editor::addAttribute(OwnerKind kind, const std::string& owner, const AttributeSpec& spec) {
    Doc doc{doc_};
    if (spec.name.empty()) throw Error("an attribute name is required");
    Object& o = doc.requireOwner(kind, owner);
    if (kind == OwnerKind::Association && doc.manyLinks(o) < 2) {
        throw Error("association '" + owner + "' cannot have attributes: it needs at least two links with maximum "
                    "cardinality n (0,n or 1,n) so that it becomes a table. Add those links first. Otherwise the "
                    "association disappears from the logical model (a foreign key goes into an entity) and the "
                    "attribute would have no place; put the attribute on an entity instead.");
    }
    Texte& holder = doc.attributeText(o);
    const std::u16string name = fromUtf8(spec.name);
    for (auto& p : holder.params) {
        if (p.name == name) throw Error("attribute '" + spec.name + "' already exists on '" + owner + "'");
    }

    const auto& tpl = attrTemplate();
    const Texte& tplText = static_cast<const Texte&>(templateObject(tpl, 3, "CDrawTexte"));
    if (tplText.params.empty()) throw Error("internal error: attribute template has no attribute");
    Param p = tplText.params[0];
    p.name = name;
    p.logicalName = fromUtf8(spec.logicalName);
    p.s248 = fromUtf8(spec.comment);
    applyType(p, spec.type);
    p.isIdentifier = spec.identifier ? 1 : 0;
    p.notNull = (spec.notNull || spec.identifier) ? 1 : 0;
    p.unique = spec.unique ? 1 : 0;
    setTimeZone(p, spec.timeZone);
    p.created = makeOleNow();
    p.modified = p.created;
    holder.params.push_back(std::move(p));
    doc.setOwnerLayout(o);
}

void Editor::updateAttribute(OwnerKind kind, const std::string& owner, const std::string& attribute,
                             const AttributeUpdate& u) {
    Doc doc{doc_};
    Object& o = doc.requireOwner(kind, owner);
    Texte& holder = doc.attributeText(o);
    const std::u16string current = fromUtf8(attribute);
    auto it = std::find_if(holder.params.begin(), holder.params.end(), [&](const Param& p) { return p.name == current; });
    if (it == holder.params.end()) throw Error("attribute '" + attribute + "' not found on '" + owner + "'");
    Param& p = *it;

    if (u.name) {
        const std::u16string n = fromUtf8(*u.name);
        if (n.empty()) throw Error("an attribute name is required");
        for (auto& other : holder.params) {
            if (&other != &p && other.name == n) throw Error("attribute '" + *u.name + "' already exists on '" + owner + "'");
        }
        p.name = n;
    }
    if (u.type) applyType(p, *u.type);
    if (u.logicalName) p.logicalName = fromUtf8(*u.logicalName);
    if (u.comment) p.s248 = fromUtf8(*u.comment);
    if (u.identifier) {
        p.isIdentifier = *u.identifier ? 1 : 0;
        if (*u.identifier) p.notNull = 1;
    }
    if (u.notNull) p.notNull = *u.notNull ? 1 : 0;
    if (u.unique) p.unique = *u.unique ? 1 : 0;
    if (u.timeZone) {
        setTimeZone(p, *u.timeZone);
    } else if (u.type && p.withTimeZone != 0 && formatType(p) != "DATETIME") {
        p.withTimeZone = 0;  // the option only exists for date-time
    }
    p.modified = makeOleNow();
    doc.setOwnerLayout(o);
}

void Editor::removeAttribute(OwnerKind kind, const std::string& owner, const std::string& attribute) {
    Doc doc{doc_};
    Object& o = doc.requireOwner(kind, owner);
    Texte& holder = doc.attributeText(o);
    const std::u16string name = fromUtf8(attribute);
    auto it = std::find_if(holder.params.begin(), holder.params.end(), [&](const Param& p) { return p.name == name; });
    if (it == holder.params.end()) throw Error("attribute '" + attribute + "' not found on '" + owner + "'");
    holder.params.erase(it);
    doc.setOwnerLayout(o);
}

namespace {

// The link between an entity and an association. The same pair can be linked several times (a reflexive
// association); then `linkName` ("Lien 2", unique within the association) tells them apart.
Doc::LinkArcs pickLink(Doc& doc, const Object& entity, const Object& assoc, const std::string& entityName,
                       const std::string& assocName, const std::string& linkName) {
    std::vector<Doc::LinkArcs> found;
    for (auto& l : doc.links()) {
        if (l.entity != &entity || l.assoc != &assoc) continue;
        if (!linkName.empty() && doc.linkName(l) != linkName) continue;
        found.push_back(l);
    }
    if (found.empty()) {
        throw Error("'" + entityName + "' is not linked to '" + assocName + "'" +
                    (linkName.empty() ? "" : " by a link named '" + linkName + "'"));
    }
    if (found.size() > 1) {
        std::string names;
        for (auto& l : found) names += (names.empty() ? "" : ", ") + std::string("'") + doc.linkName(l) + "'";
        throw Error("'" + entityName + "' is linked to '" + assocName + "' more than once; say which link: " + names);
    }
    return found.front();
}

// Looping allows only one 1,1 link per association.
bool hasOneOneLink(Doc& doc, const Object& assoc, const Arc* except) {
    for (auto& l : doc.links()) {
        if (l.assoc != &assoc || l.start() == except) continue;
        if (auto* t = doc.text(l.start()->refs[1]); t && !t->params.empty() && readCardinality(t->params[0]) == Cardinality::OneOne) {
            return true;
        }
    }
    return false;
}

void requireSingleOneOne(Doc& doc, const Object& assoc, const std::string& assocName, const Arc* except) {
    if (hasOneOneLink(doc, assoc, except)) {
        throw Error("association '" + assocName + "' already has a 1,1 link; an association can have only one");
    }
}

}  // namespace

void Editor::addLink(const std::string& entity, const std::string& association, Cardinality cardinality,
                     const std::string& role) {
    Doc doc{doc_};
    Object& e = doc.requireOwner(OwnerKind::Entity, entity);
    Object& a = doc.requireOwner(OwnerKind::Association, association);
    if (cardinality == Cardinality::OneOne) requireSingleOneOne(doc, a, association, nullptr);

    const auto& tpl = linkTemplate();
    int number = 0;
    for (auto& l : doc.links()) {
        const std::string n = doc.linkName(l);
        if (n.rfind("Lien ", 0) == 0) {
            try { number = std::max(number, std::stoi(n.substr(5))); } catch (...) {}
        }
    }
    ++number;

    const std::uint32_t arc1 = doc.nextId(), arc2 = arc1 + 1, t1 = arc1 + 2, t2 = arc1 + 3, t3 = arc1 + 4;

    auto a1 = templateObject(tpl, 9, "CDrawArc").clone();
    a1->base.id = arc1;
    a1->refs = {t1, t2, t3, 0, arc2, e.base.id};
    auto a2 = templateObject(tpl, 10, "CDrawArc").clone();
    a2->base.id = arc2;
    a2->refs = {0, 0, 0, arc1, 0, a.base.id};  // Looping recreates this arc's texts when it loads the file

    doc_.objects.push_back(std::move(a1));
    doc_.objects.push_back(std::move(a2));

    const std::uint32_t texts[3] = {t1, t2, t3};
    for (int i = 0; i < 3; ++i) {
        auto t = templateObject(tpl, 11 + i, "CDrawTexte").clone();
        t->base.id = texts[i];
        t->refs = {0, 0, 0, 0, 0, arc1};
        auto& tx = static_cast<Texte&>(*t);
        if (i == 0) tx.lines1128 = {fromUtf8("Lien " + std::to_string(number))};
        if (i == 1) {
            if (tx.params.empty()) throw Error("internal error: link template has no cardinality record");
            writeCardinality(tx.params[0], cardinality);
            setRole(tx, role);
        }
        doc_.objects.push_back(std::move(t));
    }
}

void Editor::setLinkRole(const std::string& entity, const std::string& association, const std::string& role,
                         const std::string& link) {
    Doc doc{doc_};
    Object& e = doc.requireOwner(OwnerKind::Entity, entity);
    Object& a = doc.requireOwner(OwnerKind::Association, association);
    auto l = pickLink(doc, e, a, entity, association, link);
    Texte* t = doc.text(l.start()->refs[1]);
    if (!t) throw Error("the link has no cardinality record");
    setRole(*t, role);
}

void Editor::setCardinality(const std::string& entity, const std::string& association, Cardinality cardinality,
                            const std::string& link) {
    Doc doc{doc_};
    Object& e = doc.requireOwner(OwnerKind::Entity, entity);
    Object& a = doc.requireOwner(OwnerKind::Association, association);
    auto l = pickLink(doc, e, a, entity, association, link);
    Texte* t = doc.text(l.start()->refs[1]);
    if (!t || t->params.empty()) throw Error("the link has no cardinality record");
    if (cardinality == Cardinality::OneOne) requireSingleOneOne(doc, a, association, l.start());
    if (doc.hasAttributes(a)) {
        const bool becomesMany = cardinality == Cardinality::ZeroN || cardinality == Cardinality::OneN;
        const Arc* self = l.start();
        if (doc.manyLinks(a, [&](const Doc::LinkArcs& x) { return x.start() == self; }) + (becomesMany ? 1 : 0) < 2) {
            throw Error("association '" + association + "' has attributes, so it needs at least two links with maximum "
                        "cardinality n; remove its attributes first");
        }
    }
    writeCardinality(t->params[0], cardinality);
}

void Editor::removeLink(const std::string& entity, const std::string& association, const std::string& link) {
    Doc doc{doc_};
    Object& e = doc.requireOwner(OwnerKind::Entity, entity);
    Object& a = doc.requireOwner(OwnerKind::Association, association);
    auto l = pickLink(doc, e, a, entity, association, link);
    if (doc.hasAttributes(a)) {
        const Arc* self = l.start();
        if (doc.manyLinks(a, [&](const Doc::LinkArcs& x) { return x.start() == self; }) < 2) {
            throw Error("association '" + association + "' has attributes, so it needs at least two links with maximum "
                        "cardinality n; remove its attributes first");
        }
    }
    doc.eraseIds(doc.linkObjectIds(l));
}

}  // namespace loo
