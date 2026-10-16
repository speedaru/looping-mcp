#include "loo/editor.hpp"

#include <algorithm>
#include <chrono>
#include <cstring>
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

    // The two arcs of a link: first arc is owned by the entity, second by the association.
    struct LinkArcs {
        Arc* entityArc = nullptr;
        Arc* assocArc = nullptr;
    };

    std::vector<LinkArcs> links() const {
        std::vector<LinkArcs> out;
        for (auto& o : d.objects) {
            auto* a = dynamic_cast<Arc*>(o.get());
            if (!a) continue;
            Object* owner = byId(a->refs[5]);
            if (!owner || owner->cls != "CDrawEntite") continue;
            auto* partner = dynamic_cast<Arc*>(byId(a->refs[4]));
            if (!partner) continue;
            out.push_back({a, partner});
        }
        return out;
    }

    // ids of every object forming a link (arcs and their texts).
    std::vector<std::uint32_t> linkObjectIds(const LinkArcs& l) const {
        std::vector<std::uint32_t> ids{l.entityArc->base.id, l.assocArc->base.id};
        for (auto* arc : {l.entityArc, l.assocArc}) {
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
        if (auto* t = doc.text(l.entityArc->refs[0]); t && !t->lines1128.empty()) li.name = toUtf8(t->lines1128[0]);
        if (auto* e = doc.byId(l.entityArc->refs[5])) li.entity = doc.nameOf(*e);
        if (auto* a = doc.byId(l.assocArc->refs[5])) li.association = doc.nameOf(*a);
        if (auto* t = doc.text(l.entityArc->refs[1]); t && !t->params.empty()) {
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

    std::vector<std::uint32_t> ids{id, owner.refs[0], owner.refs[1], owner.refs[2]};
    for (auto& l : doc.links()) {
        const bool attached = l.entityArc->refs[5] == id || l.assocArc->refs[5] == id;
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

std::optional<Doc::LinkArcs> findLink(Doc& doc, const Object& entity, const Object& assoc) {
    for (auto& l : doc.links()) {
        if (l.entityArc->refs[5] == entity.base.id && l.assocArc->refs[5] == assoc.base.id) return l;
    }
    return std::nullopt;
}

}  // namespace

void Editor::addLink(const std::string& entity, const std::string& association, Cardinality cardinality,
                     const std::string& role) {
    Doc doc{doc_};
    Object& e = doc.requireOwner(OwnerKind::Entity, entity);
    Object& a = doc.requireOwner(OwnerKind::Association, association);
    if (findLink(doc, e, a)) throw Error("'" + entity + "' is already linked to '" + association + "'");

    const auto& tpl = linkTemplate();
    int number = 0;
    for (auto& l : doc.links()) {
        if (auto* t = doc.text(l.entityArc->refs[0]); t && !t->lines1128.empty()) {
            const std::string n = toUtf8(t->lines1128[0]);
            if (n.rfind("Lien ", 0) == 0) {
                try { number = std::max(number, std::stoi(n.substr(5))); } catch (...) {}
            }
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

void Editor::setLinkRole(const std::string& entity, const std::string& association, const std::string& role) {
    Doc doc{doc_};
    Object& e = doc.requireOwner(OwnerKind::Entity, entity);
    Object& a = doc.requireOwner(OwnerKind::Association, association);
    auto l = findLink(doc, e, a);
    if (!l) throw Error("'" + entity + "' is not linked to '" + association + "'");
    Texte* t = doc.text(l->entityArc->refs[1]);
    if (!t) throw Error("the link has no cardinality record");
    setRole(*t, role);
}

void Editor::setCardinality(const std::string& entity, const std::string& association, Cardinality cardinality) {
    Doc doc{doc_};
    Object& e = doc.requireOwner(OwnerKind::Entity, entity);
    Object& a = doc.requireOwner(OwnerKind::Association, association);
    auto l = findLink(doc, e, a);
    if (!l) throw Error("'" + entity + "' is not linked to '" + association + "'");
    Texte* t = doc.text(l->entityArc->refs[1]);
    if (!t || t->params.empty()) throw Error("the link has no cardinality record");
    writeCardinality(t->params[0], cardinality);
}

void Editor::removeLink(const std::string& entity, const std::string& association) {
    Doc doc{doc_};
    Object& e = doc.requireOwner(OwnerKind::Entity, entity);
    Object& a = doc.requireOwner(OwnerKind::Association, association);
    auto l = findLink(doc, e, a);
    if (!l) throw Error("'" + entity + "' is not linked to '" + association + "'");
    doc.eraseIds(doc.linkObjectIds(*l));
}

}  // namespace loo
