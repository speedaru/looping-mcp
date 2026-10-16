#include "loo/document.hpp"

#include <map>

namespace loo {

namespace {

constexpr std::uint16_t kNewClassTag = 0xFFFF;
constexpr std::uint16_t kOldClassTag = 0x8000;

}  // namespace

Document readDocument(std::span<const std::uint8_t> data) {
    if (data.size() < kHeaderSize) throw Error("file too small to be a .loo file");

    Document doc;
    doc.header.assign(data.begin(), data.begin() + kHeaderSize);

    Archive ar = Archive::reader(data, kHeaderSize);
    std::uint32_t count = 0;
    ar.count(count);

    // CArchive map: one slot per new class and per object, starting at index 1. Only class slots are
    // looked up here (objects are always written in full in the top-level list).
    std::vector<std::string> slotClass;

    for (std::uint32_t i = 0; i < count; ++i) {
        std::uint16_t tag = 0;
        ar.u16(tag);
        std::string cls;
        if (tag == kNewClassTag) {
            std::uint16_t schema = 0, len = 0;
            ar.u16(schema);
            ar.u16(len);
            cls.resize(len);
            ar.bytes(reinterpret_cast<std::uint8_t*>(cls.data()), len);
            if (schema != 0) throw Error("unsupported class schema for " + cls);
            slotClass.push_back(cls);  // the class slot
        } else if ((tag & kOldClassTag) != 0 && (tag & 0x7FFF) >= 1 && (tag & 0x7FFF) <= slotClass.size()) {
            cls = slotClass[(tag & 0x7FFF) - 1];
            if (cls.empty()) throw Error("object tag points at an object slot, not a class");
        } else {
            throw Error("unsupported object tag " + std::to_string(tag) + " at offset " + std::to_string(ar.pos() - 2));
        }
        slotClass.emplace_back();  // the object slot

        auto obj = makeObject(cls);
        obj->serialize(ar);
        doc.objects.push_back(std::move(obj));
    }

    for (auto& obj : doc.objects) obj->serializeRefs(ar);

    if (!ar.atEnd()) throw Error("unexpected data after the object list at offset " + std::to_string(ar.pos()));
    return doc;
}

std::vector<std::uint8_t> writeDocument(const Document& doc) {
    if (doc.header.size() != kHeaderSize) throw Error("document header must be " + std::to_string(kHeaderSize) + " bytes");

    Archive ar = Archive::writer();
    ar.bytes(const_cast<std::uint8_t*>(doc.header.data()), doc.header.size());

    auto count = static_cast<std::uint32_t>(doc.objects.size());
    ar.count(count);

    std::map<std::string, std::uint16_t> classSlot;
    std::uint32_t mapCount = 1;

    for (const auto& obj : doc.objects) {
        auto it = classSlot.find(obj->cls);
        if (it == classSlot.end()) {
            std::uint16_t tag = kNewClassTag, schema = 0;
            auto len = static_cast<std::uint16_t>(obj->cls.size());
            ar.u16(tag);
            ar.u16(schema);
            ar.u16(len);
            std::string name = obj->cls;
            ar.bytes(reinterpret_cast<std::uint8_t*>(name.data()), name.size());
            classSlot[obj->cls] = static_cast<std::uint16_t>(mapCount++);
        } else {
            auto tag = static_cast<std::uint16_t>(kOldClassTag | it->second);
            ar.u16(tag);
        }
        ++mapCount;  // the object itself

        obj->serialize(ar);
    }

    for (const auto& obj : doc.objects) obj->serializeRefs(ar);

    return ar.take();
}

}  // namespace loo
