#include "loo/objects.hpp"

namespace loo {

// Field order below is the version-51 store order of each Serialize method (docs/format.md).

void Param::serialize(Archive& ar) {
    ar.u32(isIdentifier);
    ar.str(name);
    ar.u32(type);
    ar.u32(textKind);
    ar.u32(textCharset);
    ar.str(s72);
    ar.str(s80);
    ar.str(s88);
    ar.u32(withTimeZone);
    ar.str(logicalName);
    ar.str(s280);
    ar.str(s312);
    ar.u32(d320);
    ar.u32(d324);
    ar.u32(d332);
    ar.u32(d328);
    ar.u32(numericKind);
    ar.u32(intSize);
    ar.u32(realPrecision);
    ar.u32(dateKind);
    ar.u32(d132);
    ar.u32(notNull);
    ar.u32(unique);
    ar.u32(d144);
    ar.str(s152);
    ar.u64(q160);
    ar.u32(d168);
    created.serialize(ar);
    modified.serialize(ar);
    ar.u32(d208);
    ar.u32(d100);
    for (auto& v : d104_128) ar.u32(v);
    ar.u32(d212);
    ar.u32(cardinality);
    ar.str(s224);
    ar.str(s232);
    ar.str(s240);
    ar.u32(t300);
    ar.u32(t304);
    ar.u32(d308);
    ar.str(s248);
    ar.str(s256);
    ar.u32(d264);
    ar.u32(d268);
    ar.u32(d272);
    ar.u32(d276);
    ar.str(s288);
    ar.str(s64);
    ar.u32(d296);
}

void DrawBase::serialize(Archive& ar) {
    ar.u32(f320);
    ar.u16(w216);
    ar.u16(w228);
    ar.u16(w220);
    ar.u16(w236);
    ar.u16(w224);
    ar.u16(w244);
    ar.raw(rect40);
    ar.raw(s120);
    ar.raw(rect56);
    ar.raw(s152);
    ar.raw(rect72);
    ar.raw(s184);
    ar.u32(id);
    ar.u32(f776);
    ar.u32(f780);
    ar.u32(f784);
    ar.u32(f788);
    ar.u32(f792);
    ar.u16(w232);
    ar.u16(w240);
    ar.u16(w248);
    ar.raw(s136);
    ar.raw(s168);
    ar.raw(s200);
    ar.u32(f824);
}

void Rattach::serialize(Archive& ar) {
    ar.u16(w);
    ar.raw(raw);
    ar.u32(zero);
}

void Object::serializeRefs(Archive& ar) {
    for (auto& r : refs) ar.u32(r);
}

void Entite::serialize(Archive& ar) {
    base.serialize(ar);
    ar.u16(flags);
    for (auto& v : rect) ar.i32(v);
}

static void serializeLines(Archive& ar, std::vector<std::u16string>& lines) {
    std::uint32_t n = static_cast<std::uint32_t>(lines.size());
    ar.count(n);
    if (ar.reading()) lines.resize(n);
    for (auto& l : lines) ar.str(l);
}

void Texte::serialize(Archive& ar) {
    base.serialize(ar);
    ar.u32(d1112);
    ar.u32(d1116);
    serializeLines(ar, lines1128);
    serializeLines(ar, lines1184);
    ar.u32(d1240);
    ar.str(s1528);
    ar.u32(d1120);
    ar.u32(zero0);
    ar.u32(zero1);
    ar.raw(blk912);
    ar.raw(blk916);
    ar.raw(blk1012);
    ar.raw(blk1016);
    ar.u16(w1244);
    std::uint32_t n = static_cast<std::uint32_t>(params.size());
    ar.u32(n);
    if (ar.reading()) params.resize(n);
    for (auto& p : params) p.serialize(ar);
}

void Relation::serialize(Archive& ar) {
    base.serialize(ar);
    ar.i32(x);
    ar.i32(y);
    ar.u32(d920);
    ar.u32(d928);
    ar.raw(raw932);
    ar.u32(d940);
    ar.u32(d924);
    ar.u32(zero0);
    ar.u32(zero1);
    ar.str(tail);
}

void Arc::serialize(Archive& ar) {
    base.serialize(ar);
    ar.u32(d968);
    ar.u32(d972);
    ar.u32(d976);
    ar.str(s984);
}

void Arc::serializeRefs(Archive& ar) {
    Object::serializeRefs(ar);
    rattach.serialize(ar);
}

std::unique_ptr<Object> makeObject(const std::string& cls) {
    if (cls == "CDrawEntite") return std::make_unique<Entite>();
    if (cls == "CDrawTexte") return std::make_unique<Texte>();
    if (cls == "CDrawRelation") return std::make_unique<Relation>();
    if (cls == "CDrawArc") return std::make_unique<Arc>();
    throw Error("unsupported object class '" + cls +
                "' (only entities, associations and links are supported in this version)");
}

}  // namespace loo
