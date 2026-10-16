// In-memory form of the drawing objects stored in a .loo file (file version 51).
// Field order and sizes come from the reverse-engineered Serialize methods, see docs/format.md.
// Fields whose meaning is unknown are kept under their `this+offset` names so files round-trip exactly.
#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "loo/archive.hpp"

namespace loo {

// COleDateTime as written by MFC: status + double (kept as raw bits).
struct OleDate {
    std::uint32_t status = 0;
    std::uint64_t bits = 0;
    void serialize(Archive& ar) {
        ar.u32(status);
        ar.u64(bits);
    }
};

// CUnParametre: one attribute ("rubrique") of an entity, or the cardinality record of a link.
struct Param {
    std::uint32_t isIdentifier = 0;  // d8
    std::u16string name;             // +16
    std::uint32_t type = 0;          // d32: 0 text, 1 numeric, 2 date/time, 3 bool, 5 xml, 6 geography, 7 geometry
    std::uint32_t textKind = 1;      // d36: 0 fixed, 1 variable, 2 large object
    std::uint32_t textCharset = 0;   // d40: 0 ascii, 1 unicode, 2 binary
    std::u16string s72, s80, s88;
    std::uint32_t withTimeZone = 0;  // d96: "avec fuseau horaire" (date-time)
    std::u16string logicalName;      // +24
    std::u16string s280, s312;
    std::uint32_t d320 = 0, d324 = 0, d332 = 0;
    std::uint32_t d328 = 0;          // set for 0,1 cardinality records
    std::uint32_t numericKind = 0;   // d44: 0 integer, 1 decimal, 2 real, 3 money, 4 counter
    std::uint32_t intSize = 2;       // d48: 0 8-bit, 1 16-bit, 2 32-bit, 3 64-bit
    std::uint32_t realPrecision = 1; // d52: 0 float32, otherwise double
    std::uint32_t dateKind = 0;      // d56: 0 date, 1 time, 2 date-time
    std::uint32_t d132 = 0;
    std::uint32_t notNull = 0;       // d136
    std::uint32_t unique = 0;        // d140
    std::uint32_t d144 = 0;
    std::u16string s152;
    std::uint64_t q160 = 0;
    std::uint32_t d168 = 0;
    OleDate created, modified;       // +176, +192
    std::uint32_t d208 = 0, d100 = 0;
    // d104..d128: text lengths (see types.cpp); d124 = decimal digits in total, d128 = digits after the point.
    std::array<std::uint32_t, 7> d104_128{};
    std::uint32_t d212 = 0;
    std::uint32_t cardinality = 0;   // d216 on link records: 0 -> 0,1  1 -> 1,1  2 -> 0,n  3 -> 1,n
    std::u16string s224, s232, s240;
    std::uint32_t t300 = 0, t304 = 0, d308 = 0;
    std::u16string s248, s256;
    std::uint32_t d264 = 0, d268 = 0, d272 = 0, d276 = 0;
    std::u16string s288, s64;
    std::uint32_t d296 = 0;

    void serialize(Archive& ar);
};

// CDrawObj: base of every drawing object (170 bytes).
struct DrawBase {
    std::uint32_t f320 = 0;  // object kind marker: entity 4, relation 1, arc 32, text 64
    std::uint16_t w216 = 0, w228 = 0, w220 = 0, w236 = 0, w224 = 0, w244 = 0;
    Bytes<16> rect40{};
    Bytes<12> s120{};
    Bytes<16> rect56{};
    Bytes<12> s152{};
    Bytes<16> rect72{};
    Bytes<12> s184{};
    std::uint32_t id = 0;  // f16: object id, referenced by the second pass
    std::uint32_t f776 = 0, f780 = 0, f784 = 0, f788 = 0, f792 = 0;
    std::uint16_t w232 = 0, w240 = 0, w248 = 0;
    Bytes<12> s136{}, s168{}, s200{};
    std::uint32_t f824 = 0;

    void serialize(Archive& ar);
};

// CRattach: attachment record written after a link's ids in the second pass (14 bytes).
struct Rattach {
    std::uint16_t w = 0;
    Bytes<8> raw{};
    std::uint32_t zero = 0;
    void serialize(Archive& ar);
};

struct Object {
    std::string cls;  // MFC class name, e.g. "CDrawEntite"
    DrawBase base;
    // Second pass: ids of the three texts, two chain links, and the owner (0 = none).
    std::array<std::uint32_t, 6> refs{};

    Object() = default;
    Object(const Object&) = default;
    Object& operator=(const Object&) = default;
    virtual ~Object() = default;
    virtual std::unique_ptr<Object> clone() const = 0;
    virtual void serialize(Archive& ar) = 0;
    virtual void serializeRefs(Archive& ar);
};

struct Entite : Object {
    std::uint16_t flags = 0;                // bit0, bit1: two booleans (meaning unknown)
    std::array<std::int32_t, 4> rect{};     // left, top, right, bottom (pixels)
    Entite() { cls = "CDrawEntite"; }
    std::unique_ptr<Object> clone() const override { return std::make_unique<Entite>(*this); }
    void serialize(Archive& ar) override;
};

struct Texte : Object {
    std::uint32_t d1112 = 0, d1116 = 0;
    std::vector<std::u16string> lines1128;  // text lines (entity name etc.)
    std::vector<std::u16string> lines1184;
    std::uint32_t d1240 = 0;
    std::u16string s1528;
    std::uint32_t d1120 = 0;
    std::uint32_t zero0 = 0, zero1 = 0;
    Bytes<100> blk912{};
    Bytes<92> blk916{};
    Bytes<100> blk1012{};
    Bytes<92> blk1016{};
    std::uint16_t w1244 = 0;
    std::vector<Param> params;
    Texte() { cls = "CDrawTexte"; }
    std::unique_ptr<Object> clone() const override { return std::make_unique<Texte>(*this); }
    void serialize(Archive& ar) override;
};

struct Relation : Object {
    std::int32_t x = 0, y = 0;  // f912: position of the association
    std::uint32_t d920 = 0, d928 = 0;
    Bytes<8> raw932{};
    std::uint32_t d940 = 0, d924 = 0;
    std::uint32_t zero0 = 0, zero1 = 0;
    std::u16string tail;
    Relation() { cls = "CDrawRelation"; }
    std::unique_ptr<Object> clone() const override { return std::make_unique<Relation>(*this); }
    void serialize(Archive& ar) override;
};

struct Arc : Object {
    std::uint32_t d968 = 0, d972 = 0, d976 = 0;
    std::u16string s984;
    Rattach rattach;
    Arc() { cls = "CDrawArc"; }
    std::unique_ptr<Object> clone() const override { return std::make_unique<Arc>(*this); }
    void serialize(Archive& ar) override;
    void serializeRefs(Archive& ar) override;
};

// Creates an object for an MFC class name; throws Error for classes outside the supported set.
std::unique_ptr<Object> makeObject(const std::string& cls);

}  // namespace loo
