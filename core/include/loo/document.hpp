// A whole .loo file: the fixed document header followed by the object list and the second pass.
#pragma once

#include <cstdint>
#include <memory>
#include <span>
#include <vector>

#include "loo/objects.hpp"

namespace loo {

inline constexpr std::size_t kHeaderSize = 2592;  // document settings; identical across the corpus

struct Document {
    std::vector<std::uint8_t> header;                 // kHeaderSize bytes, copied verbatim
    std::vector<std::unique_ptr<Object>> objects;     // in file order
};

// Parses a .loo file. Throws loo::Error on malformed input or on unsupported object classes.
Document readDocument(std::span<const std::uint8_t> data);

// Serializes a document. MFC class tags and map indices are regenerated from the object order.
std::vector<std::uint8_t> writeDocument(const Document& doc);

}  // namespace loo
